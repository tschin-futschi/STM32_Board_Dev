/**
  * @file    bsp_i2c2.c
  * @brief   I2C2 BSP — software (bit-bang) I2C, 1 MHz
  *
  * Hardware: PB10(SCL) / PB11(SDA), GPIO open-drain, DWT timing
  * Purpose:  Motor IC communication (16-bit register address, big-endian)
  *
  * Replaces hardware I2C2 peripheral with bit-bang for 1 MHz operation.
  * Uses DWT->CYCCNT for precise half-cycle delays.
  * Interrupt protection: none — I2C tolerates clock stretching from ISR jitter.
  */

#include "bsp_i2c2.h"

/*--------------------------------------------------------------------------*/
/*                        Motor IC runtime config                           */
/*--------------------------------------------------------------------------*/

volatile uint8_t g_motorIcAddr     = 0x00U;
volatile char    g_motorIcName[16] = "UNKNOWN";

/* DIAGNOSTIC: filled by BSP_I2C2_Init on failure */
uint8_t g_i2c2InitDiag = 0U;

/*--------------------------------------------------------------------------*/
/*                          Recovery flag                                   */
/*--------------------------------------------------------------------------*/

static uint8_t s_busRecovered = 0U;

/* Set by any bit-level primitive whose WaitSclHigh() timed out (slave stuck /
 * gone). Cleared at the start of each public transaction; checked before a
 * SUCCESS return so a stretched-out read can no longer report stale bus bits
 * as real data. Not volatile: written and read within the same call chain on
 * one context (a transaction never spans the ISR/main boundary). */
static uint8_t s_busTimeout = 0U;

/*--------------------------------------------------------------------------*/
/*                       DWT cycle counter helpers                          */
/*--------------------------------------------------------------------------*/

static void DWT_Init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

static __inline void DWT_DelayFrom(uint32_t start, uint32_t cycles)
{
    while ((DWT->CYCCNT - start) < cycles) { ; }
}

/*--------------------------------------------------------------------------*/
/*                       GPIO direct-access macros                          */
/*--------------------------------------------------------------------------*/

/* SCL: PB10 — open-drain output, read via IDR */
/* BSRR[15:0] = set bits, BSRR[31:16] = reset bits */
#define SCL_HIGH()   (BSP_I2C2_SCL_GPIO_PORT->BSRR = (uint32_t)BSP_I2C2_SCL_PIN)
#define SCL_LOW()    (BSP_I2C2_SCL_GPIO_PORT->BSRR = (uint32_t)BSP_I2C2_SCL_PIN << 16U)
#define SCL_READ()   ((BSP_I2C2_SCL_GPIO_PORT->IDR & BSP_I2C2_SCL_PIN) != 0U)

/* SDA: PB11 — open-drain output, read via IDR */
#define SDA_HIGH()   (BSP_I2C2_SDA_GPIO_PORT->BSRR = (uint32_t)BSP_I2C2_SDA_PIN)
#define SDA_LOW()    (BSP_I2C2_SDA_GPIO_PORT->BSRR = (uint32_t)BSP_I2C2_SDA_PIN << 16U)
#define SDA_READ()   ((BSP_I2C2_SDA_GPIO_PORT->IDR & BSP_I2C2_SDA_PIN) != 0U)

/*--------------------------------------------------------------------------*/
/*                          Timing macros                                   */
/*--------------------------------------------------------------------------*/

#define DELAY_HALF()        do { uint32_t _s = DWT->CYCCNT; \
                                 DWT_DelayFrom(_s, BSP_I2C2_SW_HALF_CYCLE); } while(0)

#define DELAY_HALF_FROM(s)  DWT_DelayFrom((s), BSP_I2C2_SW_HALF_CYCLE)

#define DELAY_LOW_FROM(s)   DWT_DelayFrom((s), \
                                BSP_I2C2_SW_HALF_CYCLE - BSP_I2C2_SW_SCL_LOW_ADJ)

/*--------------------------------------------------------------------------*/
/*                     Clock stretching wait                                */
/*--------------------------------------------------------------------------*/

/** Wait for SCL to go high (slave may stretch clock). Returns 1 OK, 0 timeout. */
static __inline uint8_t WaitSclHigh(void)
{
    uint32_t t0 = DWT->CYCCNT;
    while (!SCL_READ())
    {
        if ((DWT->CYCCNT - t0) > BSP_I2C2_SW_STRETCH_TIMEOUT) { return 0U; }
    }
    return 1U;
}

/*--------------------------------------------------------------------------*/
/*                        Bit-level primitives                              */
/*--------------------------------------------------------------------------*/

static __inline void SendBit(uint8_t bit)
{
    uint32_t t;

    /* SCL low phase: set SDA then wait */
    if (bit) { SDA_HIGH(); } else { SDA_LOW(); }
    t = DWT->CYCCNT;
    DELAY_LOW_FROM(t);

    /* SCL high phase */
    SCL_HIGH();
    t = DWT->CYCCNT;
    if (!WaitSclHigh()) { s_busTimeout = 1U; }
    DELAY_HALF_FROM(t);

    SCL_LOW();
}

static __inline uint8_t RecvBit(void)
{
    uint8_t  bit;
    uint32_t t;

    /* Release SDA so slave can drive it */
    SDA_HIGH();
    t = DWT->CYCCNT;
    DELAY_LOW_FROM(t);

    /* SCL high phase: sample SDA */
    SCL_HIGH();
    t = DWT->CYCCNT;
    if (!WaitSclHigh()) {        /* slave stretched past timeout */
        s_busTimeout = 1U;
        SCL_LOW();              /* restore line low, do not leave SCL high */
        return 1U;             /* recessive bit; caller aborts via s_busTimeout */
    }
    bit = SDA_READ() ? 1U : 0U;
    DELAY_HALF_FROM(t);

    SCL_LOW();
    return bit;
}

/*--------------------------------------------------------------------------*/
/*                        Byte-level primitives                             */
/*--------------------------------------------------------------------------*/

/** Send 8 bits MSB-first, read ACK. Returns 1 if ACK received, 0 if NACK. */
static uint8_t SendByte(uint8_t byte)
{
    uint8_t i;
    for (i = 0U; i < 8U; i++)
    {
        SendBit((byte & 0x80U) ? 1U : 0U);
        byte <<= 1U;
    }
    /* ACK bit: slave pulls SDA low = ACK */
    return (RecvBit() == 0U) ? 1U : 0U;
}

/** Receive 8 bits MSB-first, send ACK (ack=1) or NACK (ack=0). */
static uint8_t RecvByte(uint8_t ack)
{
    uint8_t byte = 0U;
    uint8_t i;
    for (i = 0U; i < 8U; i++)
    {
        byte = (uint8_t)((byte << 1U) | RecvBit());
    }
    /* Master sends ACK (SDA low) or NACK (SDA high) */
    SendBit(ack ? 0U : 1U);
    return byte;
}

/*--------------------------------------------------------------------------*/
/*                     START / STOP / Repeated START                        */
/*--------------------------------------------------------------------------*/

static void I2C_Start(void)
{
    SDA_HIGH();
    SCL_HIGH();
    DELAY_HALF();
    if (!WaitSclHigh()) { s_busTimeout = 1U; }
    SDA_LOW();          /* SDA falls while SCL high = START */
    DELAY_HALF();
    SCL_LOW();
}

static void I2C_Stop(void)
{
    uint32_t t;

    SDA_LOW();
    t = DWT->CYCCNT;
    DELAY_LOW_FROM(t);

    SCL_HIGH();
    t = DWT->CYCCNT;
    if (!WaitSclHigh()) { s_busTimeout = 1U; }
    DELAY_HALF_FROM(t);

    SDA_HIGH();         /* SDA rises while SCL high = STOP */
    DELAY_HALF();
}

static void I2C_RepeatedStart(void)
{
    SDA_HIGH();
    DELAY_HALF();
    SCL_HIGH();
    DELAY_HALF();
    if (!WaitSclHigh()) { s_busTimeout = 1U; }
    SDA_LOW();          /* SDA falls while SCL high = repeated START */
    DELAY_HALF();
    SCL_LOW();
}

/*--------------------------------------------------------------------------*/
/*                           Public API                                     */
/*--------------------------------------------------------------------------*/

ErrorStatus BSP_I2C2_Init(void)
{
    GPIO_InitTypeDef gpioInit;

    /* 1. Enable GPIO clock */
    RCC_AHB1PeriphClockCmd(BSP_I2C2_SCL_GPIO_CLK | BSP_I2C2_SDA_GPIO_CLK,
                           ENABLE);

    /* 2. Configure pins, no internal pull.
     *    串扰诊断：
     *      - SCL 推挽 + 50MHz（master 单向驱动时钟，低阻、边沿锐利干净）；
     *      - SDA 开漏（从机需驱动 SDA 做 ACK/读）+ 2MHz 慢速（软化数据沿，
     *        减小窜入 SCL 的耦合尖刺）。 */
    gpioInit.GPIO_Mode  = GPIO_Mode_OUT;
    gpioInit.GPIO_PuPd  = GPIO_PuPd_NOPULL;

    /* SCL — push-pull, fast edge */
    gpioInit.GPIO_OType = GPIO_OType_PP;
    gpioInit.GPIO_Speed = GPIO_Speed_50MHz;
    gpioInit.GPIO_Pin   = BSP_I2C2_SCL_PIN;
    GPIO_Init(BSP_I2C2_SCL_GPIO_PORT, &gpioInit);

    /* SDA — open-drain, slow edge (reduce coupling spikes onto SCL) */
    gpioInit.GPIO_OType = GPIO_OType_OD;
    gpioInit.GPIO_Speed = GPIO_Speed_2MHz;
    gpioInit.GPIO_Pin   = BSP_I2C2_SDA_PIN;
    GPIO_Init(BSP_I2C2_SDA_GPIO_PORT, &gpioInit);

    /* 3. Release both lines (idle high) */
    SCL_HIGH();
    SDA_HIGH();

    /* 4. Enable DWT cycle counter */
    DWT_Init();

    /* 5. Wait for external pull-up to settle bus capacitance (RC charge) */
    {
        uint32_t t0 = DWT->CYCCNT;
        DWT_DelayFrom(t0, BSP_I2C2_SW_INIT_SETTLE_CYCLES);
    }

    /* 6. Check bus stuck (SDA held low) and recover */
    if (!SDA_READ())
    {
        BSP_I2C2_RecoverBus();
        if (!SDA_READ())
        {
            g_i2c2InitDiag = SCL_READ() ? 2U : 1U;
            return ERROR;
        }
    }

    return SUCCESS;
}

/*--------------------------------------------------------------------------*/

ErrorStatus BSP_I2C2_WriteReg(uint8_t devAddr, uint16_t reg,
                               const uint8_t *pData, uint16_t len)
{
    uint16_t i;

    s_busTimeout = 0U;
    I2C_Start();

    /* Address + W */
    if (!SendByte((uint8_t)(devAddr << 1U)))        { goto error; }

    /* 16-bit register address, big-endian */
    if (!SendByte((uint8_t)(reg >> 8U)))             { goto error; }
    if (!SendByte((uint8_t)(reg & 0xFFU)))           { goto error; }

    /* Data bytes */
    for (i = 0U; i < len; i++)
    {
        if (!SendByte(pData[i]))                     { goto error; }
    }

    if (s_busTimeout) { goto error; }   /* clock stretched past timeout */
    I2C_Stop();
    return SUCCESS;

error:
    I2C_Stop();
    BSP_I2C2_RecoverBus();
    return ERROR;
}

/*--------------------------------------------------------------------------*/

ErrorStatus BSP_I2C2_ReadReg(uint8_t devAddr, uint16_t reg,
                              uint8_t *pData, uint16_t len)
{
    uint16_t i;

    if (len == 0U) { return SUCCESS; }

    s_busTimeout = 0U;

    /* Write phase: START + addr+W + 16-bit reg addr */
    I2C_Start();
    if (!SendByte((uint8_t)(devAddr << 1U)))         { goto error; }
    if (!SendByte((uint8_t)(reg >> 8U)))              { goto error; }
    if (!SendByte((uint8_t)(reg & 0xFFU)))            { goto error; }

    /* Read phase: repeated START + addr+R */
    I2C_RepeatedStart();
    if (!SendByte((uint8_t)((devAddr << 1U) | 0x01U))) { goto error; }

    /* Read data bytes: ACK all except last (NACK) */
    for (i = 0U; i < len; i++)
    {
        pData[i] = RecvByte((i < (len - 1U)) ? 1U : 0U);
    }

    /* A stretch timeout during the read makes the sampled bits invalid. */
    if (s_busTimeout) { goto error; }
    I2C_Stop();
    return SUCCESS;

error:
    I2C_Stop();
    BSP_I2C2_RecoverBus();
    return ERROR;
}

/*--------------------------------------------------------------------------*/

ErrorStatus BSP_I2C2_ReadRegs(uint8_t devAddr, uint16_t startReg,
                               uint8_t *pData, uint16_t len)
{
    return BSP_I2C2_ReadReg(devAddr, startReg, pData, len);
}

/*--------------------------------------------------------------------------*/

ErrorStatus BSP_I2C2_TransparentWrite(uint8_t devAddr,
                                       const uint8_t *pAddr, uint8_t addrSize,
                                       const uint8_t *pData, uint16_t dataLen)
{
    uint16_t i;

    s_busTimeout = 0U;
    I2C_Start();
    if (!SendByte((uint8_t)(devAddr << 1U)))             { goto error; }

    for (i = 0U; i < addrSize; i++)
    {
        if (!SendByte(pAddr[i]))                          { goto error; }
    }

    for (i = 0U; i < dataLen; i++)
    {
        if (!SendByte(pData[i]))                          { goto error; }
    }

    if (s_busTimeout) { goto error; }   /* clock stretched past timeout */
    I2C_Stop();
    return SUCCESS;

error:
    I2C_Stop();
    BSP_I2C2_RecoverBus();
    return ERROR;
}

/*--------------------------------------------------------------------------*/

ErrorStatus BSP_I2C2_TransparentRead(uint8_t devAddr,
                                      const uint8_t *pAddr, uint8_t addrSize,
                                      uint8_t *pData, uint16_t dataLen)
{
    uint16_t i;

    if (dataLen == 0U) { return SUCCESS; }

    s_busTimeout = 0U;

    if (addrSize > 0U)
    {
        I2C_Start();
        if (!SendByte((uint8_t)(devAddr << 1U)))         { goto error; }
        for (i = 0U; i < addrSize; i++)
        {
            if (!SendByte(pAddr[i]))                      { goto error; }
        }

        I2C_RepeatedStart();
        if (!SendByte((uint8_t)((devAddr << 1U) | 0x01U))) { goto error; }
    }
    else
    {
        I2C_Start();
        if (!SendByte((uint8_t)((devAddr << 1U) | 0x01U))) { goto error; }
    }

    for (i = 0U; i < dataLen; i++)
    {
        pData[i] = RecvByte((i < (dataLen - 1U)) ? 1U : 0U);
    }

    /* A stretch timeout during the read makes the sampled bits invalid. */
    if (s_busTimeout) { goto error; }
    I2C_Stop();
    return SUCCESS;

error:
    I2C_Stop();
    BSP_I2C2_RecoverBus();
    return ERROR;
}

/*--------------------------------------------------------------------------*/

ErrorStatus BSP_I2C2_ProbeAddr(uint8_t devAddr)
{
    uint8_t ack;
    I2C_Start();
    ack = SendByte((uint8_t)(devAddr << 1U));
    I2C_Stop();
    return (ack != 0U) ? SUCCESS : ERROR;
}

/*--------------------------------------------------------------------------*/

ErrorStatus BSP_I2C2_Scan(uint8_t *pAddrList, uint8_t *pCount)
{
    uint8_t addr;
    *pCount = 0U;

    for (addr = 1U; addr <= 126U; addr++)
    {
        if (BSP_I2C2_ProbeAddr(addr) == SUCCESS)
        {
            pAddrList[(*pCount)++] = addr;
        }
    }

    return SUCCESS;
}

/*--------------------------------------------------------------------------*/

void BSP_I2C2_RecoverBus(void)
{
    uint8_t  i;
    uint32_t t;

    /* Toggle SCL 9 times to clock out stuck slave */
    for (i = 0U; i < BSP_I2C2_SW_RECOVER_PULSES; i++)
    {
        SCL_LOW();
        t = DWT->CYCCNT;
        DELAY_HALF_FROM(t);
        SCL_HIGH();
        t = DWT->CYCCNT;
        DELAY_HALF_FROM(t);
    }

    /* Manual STOP condition */
    SDA_LOW();
    t = DWT->CYCCNT;
    DELAY_HALF_FROM(t);
    SCL_HIGH();
    t = DWT->CYCCNT;
    DELAY_HALF_FROM(t);
    SDA_HIGH();
    DELAY_HALF();

    /* Set recovery flag if bus is now free */
    if (SDA_READ())
    {
        s_busRecovered = 1U;
    }
}

/*--------------------------------------------------------------------------*/

uint8_t BSP_I2C2_GetAndClearRecoveryFlag(void)
{
    uint8_t flag = s_busRecovered;
    s_busRecovered = 0U;
    return flag;
}
