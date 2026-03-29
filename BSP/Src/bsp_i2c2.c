/**
  * @file    bsp_i2c2.c
  * @brief   I2C2 BSP — polling + software timeout, motor IC communication
  *
  * Hardware: I2C2, PB10(SCL) / PB11(SDA), AF4, 400 kHz
  */

#include "bsp_i2c2.h"

/*--------------------------------------------------------------------------*/
/*                        Motor IC runtime config                           */
/*--------------------------------------------------------------------------*/

uint8_t g_motorIcAddr     = 0x00U;
char    g_motorIcName[16] = "UNKNOWN";

/*--------------------------------------------------------------------------*/
/*                          Static helpers                                  */
/*--------------------------------------------------------------------------*/

/**
  * @brief Configure PB10/PB11 as AF open-drain for I2C2.
  */
static void ConfigGpioAF(void)
{
    GPIO_InitTypeDef gpioInit;

    gpioInit.GPIO_Pin   = BSP_I2C2_SCL_PIN | BSP_I2C2_SDA_PIN;
    gpioInit.GPIO_Mode  = GPIO_Mode_AF;
    gpioInit.GPIO_Speed = GPIO_Speed_50MHz;
    gpioInit.GPIO_OType = GPIO_OType_OD;
    gpioInit.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    GPIO_Init(BSP_I2C2_SCL_GPIO_PORT, &gpioInit);

    GPIO_PinAFConfig(BSP_I2C2_SCL_GPIO_PORT, BSP_I2C2_SCL_PIN_SOURCE, BSP_I2C2_SCL_AF);
    GPIO_PinAFConfig(BSP_I2C2_SDA_GPIO_PORT, BSP_I2C2_SDA_PIN_SOURCE, BSP_I2C2_SDA_AF);
}

/**
  * @brief Initialize I2C2 peripheral registers (400 kHz, 7-bit).
  */
static void I2C2PeriphInit(void)
{
    I2C_InitTypeDef i2cInit;

    I2C_DeInit(BSP_I2C2_PERIPH);

    i2cInit.I2C_Mode                = I2C_Mode_I2C;
    i2cInit.I2C_DutyCycle           = I2C_DutyCycle_2;
    i2cInit.I2C_OwnAddress1         = 0x00U;
    i2cInit.I2C_Ack                 = I2C_Ack_Enable;
    i2cInit.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    i2cInit.I2C_ClockSpeed          = BSP_I2C2_SPEED;

    I2C_Init(BSP_I2C2_PERIPH, &i2cInit);
    I2C_Cmd(BSP_I2C2_PERIPH, ENABLE);
}

/*--------------------------------------------------------------------------*/
/*                            Public API                                    */
/*--------------------------------------------------------------------------*/

/**
  * @brief  Initialize I2C2: clocks, GPIO, peripheral.
  *         If bus is found stuck after init, RecoverBus() is called once.
  * @retval SUCCESS / ERROR (bus stuck after recovery)
  */
ErrorStatus BSP_I2C2_Init(void)
{
    /* 1. Enable clocks: GPIOB and I2C2 */
    RCC_AHB1PeriphClockCmd(BSP_I2C2_SCL_GPIO_CLK, ENABLE);
    RCC_APB1PeriphClockCmd(BSP_I2C2_RCC_APB1,     ENABLE);

    /* 2. Configure GPIO as AF open-drain */
    ConfigGpioAF();

    /* 3. Initialize I2C2 peripheral */
    I2C2PeriphInit();

    /* 4. Check for stuck bus; recover once if needed */
    if (I2C_GetFlagStatus(BSP_I2C2_PERIPH, I2C_FLAG_BUSY) != RESET)
    {
        BSP_I2C2_RecoverBus();

        if (I2C_GetFlagStatus(BSP_I2C2_PERIPH, I2C_FLAG_BUSY) != RESET)
        {
            return ERROR;
        }
    }

    return SUCCESS;
}

/**
  * @brief  Recover a stuck I2C bus.
  *
  *         Procedure:
  *           1. Disable and software-reset I2C2
  *           2. Reconfigure PB10/PB11 as GPIO output open-drain
  *           3. Toggle SCL 9 times to let slave finish its byte
  *           4. Issue a manual STOP condition (SCL high, SDA low→high)
  *           5. Restore GPIO as AF and re-initialize I2C2
  */
void BSP_I2C2_RecoverBus(void)
{
    GPIO_InitTypeDef  gpioInit;
    uint8_t           i;
    volatile uint32_t delay;

    /* 1. Disable and software-reset I2C2 */
    I2C_Cmd(BSP_I2C2_PERIPH, DISABLE);
    I2C_SoftwareResetCmd(BSP_I2C2_PERIPH, ENABLE);
    I2C_SoftwareResetCmd(BSP_I2C2_PERIPH, DISABLE);
    I2C_DeInit(BSP_I2C2_PERIPH);

    /* 2. Reconfigure PB10/PB11 as GPIO output open-drain, drive both high */
    gpioInit.GPIO_Pin   = BSP_I2C2_SCL_PIN | BSP_I2C2_SDA_PIN;
    gpioInit.GPIO_Mode  = GPIO_Mode_OUT;
    gpioInit.GPIO_Speed = GPIO_Speed_50MHz;
    gpioInit.GPIO_OType = GPIO_OType_OD;
    gpioInit.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    GPIO_Init(BSP_I2C2_SCL_GPIO_PORT, &gpioInit);
    GPIO_SetBits(BSP_I2C2_SCL_GPIO_PORT, BSP_I2C2_SCL_PIN | BSP_I2C2_SDA_PIN);

    /* 3. Toggle SCL 9 times (~1.25 us half-period at 400 kHz) */
    for (i = 0U; i < 9U; i++)
    {
        GPIO_ResetBits(BSP_I2C2_SCL_GPIO_PORT, BSP_I2C2_SCL_PIN);
        for (delay = 0U; delay < 200U; delay++) { __NOP(); }
        GPIO_SetBits(BSP_I2C2_SCL_GPIO_PORT, BSP_I2C2_SCL_PIN);
        for (delay = 0U; delay < 200U; delay++) { __NOP(); }
    }

    /* 4. Manual STOP: SDA low while SCL high, then SDA high */
    GPIO_ResetBits(BSP_I2C2_SDA_GPIO_PORT, BSP_I2C2_SDA_PIN);
    for (delay = 0U; delay < 200U; delay++) { __NOP(); }
    GPIO_SetBits(BSP_I2C2_SDA_GPIO_PORT, BSP_I2C2_SDA_PIN);
    for (delay = 0U; delay < 200U; delay++) { __NOP(); }

    /* 5. Restore GPIO as AF and re-initialize I2C2 */
    ConfigGpioAF();
    I2C2PeriphInit();
}

/*--------------------------------------------------------------------------*/
/*                     Register read/write helpers                          */
/*--------------------------------------------------------------------------*/

/**
  * @brief  Write phase: START → addr+W → reg address.
  *         Leaves bus in transmitter mode; caller must continue or STOP.
  */
static ErrorStatus WritePhase(uint8_t devAddr, uint16_t reg)
{
    uint32_t timeout;

    /* Wait for bus free */
    timeout = BSP_I2C2_TIMEOUT;
    while (I2C_GetFlagStatus(BSP_I2C2_PERIPH, I2C_FLAG_BUSY) != RESET)
    {
        if (--timeout == 0U) { return ERROR; }
    }

    /* START */
    I2C_GenerateSTART(BSP_I2C2_PERIPH, ENABLE);
    timeout = BSP_I2C2_TIMEOUT;
    while (!I2C_CheckEvent(BSP_I2C2_PERIPH, I2C_EVENT_MASTER_MODE_SELECT))
    {
        if (--timeout == 0U) { return ERROR; }
    }

    /* Device address + WRITE */
    I2C_Send7bitAddress(BSP_I2C2_PERIPH, (uint8_t)(devAddr << 1U),
                        I2C_Direction_Transmitter);
    timeout = BSP_I2C2_TIMEOUT;
    while (!I2C_CheckEvent(BSP_I2C2_PERIPH,
                           I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED))
    {
        /* AF set means NACK — device not present, fail immediately */
        if (I2C_GetFlagStatus(BSP_I2C2_PERIPH, I2C_FLAG_AF) != RESET)
        {
            I2C_ClearFlag(BSP_I2C2_PERIPH, I2C_FLAG_AF);
            return ERROR;
        }
        if (--timeout == 0U) { return ERROR; }
    }

    /* Register address high byte */
    I2C_SendData(BSP_I2C2_PERIPH, (uint8_t)(reg >> 8U));
    timeout = BSP_I2C2_TIMEOUT;
    while (!I2C_CheckEvent(BSP_I2C2_PERIPH, I2C_EVENT_MASTER_BYTE_TRANSMITTED))
    {
        if (--timeout == 0U) { return ERROR; }
    }

    /* Register address low byte */
    I2C_SendData(BSP_I2C2_PERIPH, (uint8_t)(reg & 0xFFU));
    timeout = BSP_I2C2_TIMEOUT;
    while (!I2C_CheckEvent(BSP_I2C2_PERIPH, I2C_EVENT_MASTER_BYTE_TRANSMITTED))
    {
        if (--timeout == 0U) { return ERROR; }
    }

    return SUCCESS;
}

/*--------------------------------------------------------------------------*/
/*                      Public register read/write API                      */
/*--------------------------------------------------------------------------*/

/**
  * @brief  Write len bytes from pData to register reg of device devAddr.
  * @retval SUCCESS / ERROR (timeout → RecoverBus called)
  */
ErrorStatus BSP_I2C2_WriteReg(uint8_t devAddr, uint16_t reg,
                               const uint8_t *pData, uint16_t len)
{
    uint32_t timeout;
    uint16_t i;

    if (WritePhase(devAddr, reg) != SUCCESS) { goto error; }

    /* Send data bytes */
    for (i = 0U; i < len; i++)
    {
        I2C_SendData(BSP_I2C2_PERIPH, pData[i]);
        timeout = BSP_I2C2_TIMEOUT;
        while (!I2C_CheckEvent(BSP_I2C2_PERIPH, I2C_EVENT_MASTER_BYTE_TRANSMITTED))
        {
            if (--timeout == 0U) { goto error; }
        }
    }

    I2C_GenerateSTOP(BSP_I2C2_PERIPH, ENABLE);
    return SUCCESS;

error:
    I2C_GenerateSTOP(BSP_I2C2_PERIPH, ENABLE);
    BSP_I2C2_RecoverBus();
    return ERROR;
}

/**
  * @brief  Read len bytes from register reg of device devAddr into pData.
  *
  *         Read sequence:
  *           Write phase (START → addr+W → reg)
  *           → Repeated START → addr+R
  *           → N=1: ACK=0 before ADDR clear, STOP, read
  *           → N≥2: clear ADDR, read bytes, ACK=0 + STOP before last byte
  *
  * @retval SUCCESS / ERROR (timeout → RecoverBus called)
  */
ErrorStatus BSP_I2C2_ReadReg(uint8_t devAddr, uint16_t reg,
                              uint8_t *pData, uint16_t len)
{
    uint32_t timeout;
    uint16_t i;

    if (len == 0U) { return SUCCESS; }

    if (WritePhase(devAddr, reg) != SUCCESS) { goto error; }

    /* Repeated START */
    I2C_GenerateSTART(BSP_I2C2_PERIPH, ENABLE);
    timeout = BSP_I2C2_TIMEOUT;
    while (!I2C_CheckEvent(BSP_I2C2_PERIPH, I2C_EVENT_MASTER_MODE_SELECT))
    {
        if (--timeout == 0U) { goto error; }
    }

    /* Device address + READ */
    I2C_Send7bitAddress(BSP_I2C2_PERIPH, (uint8_t)(devAddr << 1U),
                        I2C_Direction_Receiver);

    if (len == 1U)
    {
        /* Single byte: disable ACK before clearing ADDR */
        timeout = BSP_I2C2_TIMEOUT;
        while (I2C_GetFlagStatus(BSP_I2C2_PERIPH, I2C_FLAG_ADDR) == RESET)
        {
            if (--timeout == 0U) { goto error; }
        }
        I2C_AcknowledgeConfig(BSP_I2C2_PERIPH, DISABLE);
        /* Clear ADDR by reading SR1 then SR2 */
        (void)BSP_I2C2_PERIPH->SR1;
        (void)BSP_I2C2_PERIPH->SR2;
        I2C_GenerateSTOP(BSP_I2C2_PERIPH, ENABLE);

        timeout = BSP_I2C2_TIMEOUT;
        while (I2C_GetFlagStatus(BSP_I2C2_PERIPH, I2C_FLAG_RXNE) == RESET)
        {
            if (--timeout == 0U) { goto error; }
        }
        pData[0] = I2C_ReceiveData(BSP_I2C2_PERIPH);
    }
    else
    {
        /* Multi-byte: clear ADDR with ACK enabled */
        timeout = BSP_I2C2_TIMEOUT;
        while (!I2C_CheckEvent(BSP_I2C2_PERIPH,
                               I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED))
        {
            if (--timeout == 0U) { goto error; }
        }

        I2C_AcknowledgeConfig(BSP_I2C2_PERIPH, ENABLE);

        for (i = 0U; i < len; i++)
        {
            /* Before last byte: disable ACK and issue STOP */
            if (i == len - 1U)
            {
                I2C_AcknowledgeConfig(BSP_I2C2_PERIPH, DISABLE);
                I2C_GenerateSTOP(BSP_I2C2_PERIPH, ENABLE);
            }

            timeout = BSP_I2C2_TIMEOUT;
            while (I2C_GetFlagStatus(BSP_I2C2_PERIPH, I2C_FLAG_RXNE) == RESET)
            {
                if (--timeout == 0U) { goto error; }
            }
            pData[i] = I2C_ReceiveData(BSP_I2C2_PERIPH);
        }
    }

    /* Restore ACK for next transfer */
    I2C_AcknowledgeConfig(BSP_I2C2_PERIPH, ENABLE);
    return SUCCESS;

error:
    I2C_AcknowledgeConfig(BSP_I2C2_PERIPH, ENABLE);
    I2C_GenerateSTOP(BSP_I2C2_PERIPH, ENABLE);
    BSP_I2C2_RecoverBus();
    return ERROR;
}

/**
  * @brief  Read len bytes from consecutive registers starting at startReg.
  *         Identical to BSP_I2C2_ReadReg — provided for API symmetry.
  */
ErrorStatus BSP_I2C2_ReadRegs(uint8_t devAddr, uint16_t startReg,
                               uint8_t *pData, uint16_t len)
{
    return BSP_I2C2_ReadReg(devAddr, startReg, pData, len);
}

/*--------------------------------------------------------------------------*/
/*                              Bus scan                                    */
/*--------------------------------------------------------------------------*/

/**
  * @brief  Scan all 7-bit addresses (1–127). An address is considered present
  *         if the device ACKs the START + address + WRITE probe.
  * @param  pAddrList  Output array, must hold at least 127 entries.
  * @param  pCount     Output: number of responding devices found.
  * @retval SUCCESS always (individual probe failures are not fatal)
  */
ErrorStatus BSP_I2C2_Scan(uint8_t *pAddrList, uint8_t *pCount)
{
    uint8_t  addr;
    uint32_t timeout;

    *pCount = 0U;

    for (addr = 1U; addr <= 127U; addr++)
    {
        /* Wait for bus free */
        timeout = BSP_I2C2_TIMEOUT;
        while (I2C_GetFlagStatus(BSP_I2C2_PERIPH, I2C_FLAG_BUSY) != RESET)
        {
            if (--timeout == 0U) { BSP_I2C2_RecoverBus(); break; }
        }

        /* START */
        I2C_GenerateSTART(BSP_I2C2_PERIPH, ENABLE);
        timeout = BSP_I2C2_TIMEOUT;
        while (!I2C_CheckEvent(BSP_I2C2_PERIPH, I2C_EVENT_MASTER_MODE_SELECT))
        {
            if (--timeout == 0U) { goto next; }
        }

        /* Send address + WRITE, check for ACK */
        I2C_Send7bitAddress(BSP_I2C2_PERIPH, (uint8_t)(addr << 1U),
                            I2C_Direction_Transmitter);
        timeout = BSP_I2C2_TIMEOUT;
        while (!I2C_CheckEvent(BSP_I2C2_PERIPH,
                               I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED))
        {
            /* AF flag means NACK — no device at this address */
            if (I2C_GetFlagStatus(BSP_I2C2_PERIPH, I2C_FLAG_AF) != RESET)
            {
                I2C_ClearFlag(BSP_I2C2_PERIPH, I2C_FLAG_AF);
                goto next;
            }
            if (--timeout == 0U) { goto next; }
        }

        /* ACK received — device present */
        pAddrList[(*pCount)++] = addr;

    next:
        I2C_GenerateSTOP(BSP_I2C2_PERIPH, ENABLE);
    }

    return SUCCESS;
}
