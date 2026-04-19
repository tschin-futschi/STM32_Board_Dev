/**
  * @file    bsp_i2c3.c
  * @brief   I2C3 BSP — polling + software timeout, PMIC communication
  *
  * Hardware: I2C3, PA8(SCL) / PC9(SDA), AF4, 400 kHz
  *
  * Note: SCL and SDA are on different GPIO ports (GPIOA / GPIOC),
  *       so each pin is configured independently.
  */

#include "bsp_i2c3.h"

/*--------------------------------------------------------------------------*/
/*                          Static helpers                                  */
/*--------------------------------------------------------------------------*/

/**
  * @brief Configure PA8(SCL) and PC9(SDA) as AF open-drain for I2C3.
  */
static void ConfigGpioAF(void)
{
    GPIO_InitTypeDef gpioInit;

    gpioInit.GPIO_Mode  = GPIO_Mode_AF;
    gpioInit.GPIO_Speed = GPIO_Speed_50MHz;
    gpioInit.GPIO_OType = GPIO_OType_OD;
    gpioInit.GPIO_PuPd  = GPIO_PuPd_NOPULL;

    /* SCL: PA8 */
    gpioInit.GPIO_Pin = BSP_I2C3_SCL_PIN;
    GPIO_Init(BSP_I2C3_SCL_GPIO_PORT, &gpioInit);
    GPIO_PinAFConfig(BSP_I2C3_SCL_GPIO_PORT, BSP_I2C3_SCL_PIN_SOURCE, BSP_I2C3_SCL_AF);

    /* SDA: PC9 */
    gpioInit.GPIO_Pin = BSP_I2C3_SDA_PIN;
    GPIO_Init(BSP_I2C3_SDA_GPIO_PORT, &gpioInit);
    GPIO_PinAFConfig(BSP_I2C3_SDA_GPIO_PORT, BSP_I2C3_SDA_PIN_SOURCE, BSP_I2C3_SDA_AF);
}

/**
  * @brief Initialize I2C3 peripheral registers (400 kHz, 7-bit).
  */
static void I2C3PeriphInit(void)
{
    I2C_InitTypeDef i2cInit;

    I2C_DeInit(BSP_I2C3_PERIPH);

    i2cInit.I2C_Mode                = I2C_Mode_I2C;
    i2cInit.I2C_DutyCycle           = I2C_DutyCycle_2;
    i2cInit.I2C_OwnAddress1         = 0x00U;
    i2cInit.I2C_Ack                 = I2C_Ack_Enable;
    i2cInit.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    i2cInit.I2C_ClockSpeed          = BSP_I2C3_SPEED;

    I2C_Init(BSP_I2C3_PERIPH, &i2cInit);
    I2C_Cmd(BSP_I2C3_PERIPH, ENABLE);
}

/*--------------------------------------------------------------------------*/
/*                            Public API                                    */
/*--------------------------------------------------------------------------*/

/**
  * @brief  Initialize I2C3: clocks, GPIO, peripheral.
  *         If bus is found stuck after init, RecoverBus() is called once.
  * @retval SUCCESS / ERROR (bus stuck after recovery)
  */
ErrorStatus BSP_I2C3_Init(void)
{
    /* 1. Enable clocks: GPIOA, GPIOC and I2C3 */
    RCC_AHB1PeriphClockCmd(BSP_I2C3_SCL_GPIO_CLK, ENABLE);
    RCC_AHB1PeriphClockCmd(BSP_I2C3_SDA_GPIO_CLK, ENABLE);
    RCC_APB1PeriphClockCmd(BSP_I2C3_RCC_APB1,     ENABLE);

    /* 2. Configure GPIO as AF open-drain */
    ConfigGpioAF();

    /* 3. Initialize I2C3 peripheral */
    I2C3PeriphInit();

    /* 4. Check for stuck bus; recover once if needed */
    if (I2C_GetFlagStatus(BSP_I2C3_PERIPH, I2C_FLAG_BUSY) != RESET)
    {
        BSP_I2C3_RecoverBus();

        if (I2C_GetFlagStatus(BSP_I2C3_PERIPH, I2C_FLAG_BUSY) != RESET)
        {
            return ERROR;
        }
    }

    return SUCCESS;
}

/**
  * @brief  Recover a stuck I2C3 bus (9 SCL pulses + manual STOP).
  */
void BSP_I2C3_RecoverBus(void)
{
    GPIO_InitTypeDef  gpioInit;
    uint8_t           i;
    volatile uint32_t delay;

    /* 1. Disable and software-reset I2C3 */
    I2C_Cmd(BSP_I2C3_PERIPH, DISABLE);
    I2C_SoftwareResetCmd(BSP_I2C3_PERIPH, ENABLE);
    I2C_SoftwareResetCmd(BSP_I2C3_PERIPH, DISABLE);
    I2C_DeInit(BSP_I2C3_PERIPH);

    /* 2. Reconfigure PA8/PC9 as GPIO output open-drain, drive both high */
    gpioInit.GPIO_Mode  = GPIO_Mode_OUT;
    gpioInit.GPIO_Speed = GPIO_Speed_50MHz;
    gpioInit.GPIO_OType = GPIO_OType_OD;
    gpioInit.GPIO_PuPd  = GPIO_PuPd_NOPULL;

    gpioInit.GPIO_Pin = BSP_I2C3_SCL_PIN;
    GPIO_Init(BSP_I2C3_SCL_GPIO_PORT, &gpioInit);
    GPIO_SetBits(BSP_I2C3_SCL_GPIO_PORT, BSP_I2C3_SCL_PIN);

    gpioInit.GPIO_Pin = BSP_I2C3_SDA_PIN;
    GPIO_Init(BSP_I2C3_SDA_GPIO_PORT, &gpioInit);
    GPIO_SetBits(BSP_I2C3_SDA_GPIO_PORT, BSP_I2C3_SDA_PIN);

    /* 3. Toggle SCL 9 times to release stuck slave */
    for (i = 0U; i < 9U; i++)
    {
        GPIO_ResetBits(BSP_I2C3_SCL_GPIO_PORT, BSP_I2C3_SCL_PIN);
        for (delay = 0U; delay < 200U; delay++) { __NOP(); }
        GPIO_SetBits(BSP_I2C3_SCL_GPIO_PORT, BSP_I2C3_SCL_PIN);
        for (delay = 0U; delay < 200U; delay++) { __NOP(); }
    }

    /* 4. Manual STOP: SDA low while SCL high, then SDA high */
    GPIO_ResetBits(BSP_I2C3_SDA_GPIO_PORT, BSP_I2C3_SDA_PIN);
    for (delay = 0U; delay < 200U; delay++) { __NOP(); }
    GPIO_SetBits(BSP_I2C3_SDA_GPIO_PORT, BSP_I2C3_SDA_PIN);
    for (delay = 0U; delay < 200U; delay++) { __NOP(); }

    /* 5. Restore GPIO as AF and re-initialize I2C3 */
    ConfigGpioAF();
    I2C3PeriphInit();
}

/*--------------------------------------------------------------------------*/
/*                     Register read/write helpers                          */
/*--------------------------------------------------------------------------*/

static ErrorStatus WritePhase(uint8_t devAddr, uint8_t reg)
{
    uint32_t timeout;

    timeout = BSP_I2C3_TIMEOUT;
    while (I2C_GetFlagStatus(BSP_I2C3_PERIPH, I2C_FLAG_BUSY) != RESET)
    {
        if (--timeout == 0U) { return ERROR; }
    }

    I2C_GenerateSTART(BSP_I2C3_PERIPH, ENABLE);
    timeout = BSP_I2C3_TIMEOUT;
    while (!I2C_CheckEvent(BSP_I2C3_PERIPH, I2C_EVENT_MASTER_MODE_SELECT))
    {
        if (--timeout == 0U) { return ERROR; }
    }

    I2C_Send7bitAddress(BSP_I2C3_PERIPH, (uint8_t)(devAddr << 1U),
                        I2C_Direction_Transmitter);
    timeout = BSP_I2C3_TIMEOUT;
    while (!I2C_CheckEvent(BSP_I2C3_PERIPH,
                           I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED))
    {
        if (I2C_GetFlagStatus(BSP_I2C3_PERIPH, I2C_FLAG_AF) != RESET)
        {
            I2C_ClearFlag(BSP_I2C3_PERIPH, I2C_FLAG_AF);
            return ERROR;
        }
        if (--timeout == 0U) { return ERROR; }
    }

    I2C_SendData(BSP_I2C3_PERIPH, reg);
    timeout = BSP_I2C3_TIMEOUT;
    while (!I2C_CheckEvent(BSP_I2C3_PERIPH, I2C_EVENT_MASTER_BYTE_TRANSMITTED))
    {
        if (--timeout == 0U) { return ERROR; }
    }

    return SUCCESS;
}

/*--------------------------------------------------------------------------*/
/*                      Public register read/write API                      */
/*--------------------------------------------------------------------------*/

ErrorStatus BSP_I2C3_WriteReg(uint8_t devAddr, uint8_t reg,
                               const uint8_t *pData, uint16_t len)
{
    uint32_t timeout;
    uint16_t i;

    if (WritePhase(devAddr, reg) != SUCCESS) { goto error; }

    for (i = 0U; i < len; i++)
    {
        I2C_SendData(BSP_I2C3_PERIPH, pData[i]);
        timeout = BSP_I2C3_TIMEOUT;
        while (!I2C_CheckEvent(BSP_I2C3_PERIPH, I2C_EVENT_MASTER_BYTE_TRANSMITTED))
        {
            if (--timeout == 0U) { goto error; }
        }
    }

    I2C_GenerateSTOP(BSP_I2C3_PERIPH, ENABLE);
    return SUCCESS;

error:
    I2C_GenerateSTOP(BSP_I2C3_PERIPH, ENABLE);
    BSP_I2C3_RecoverBus();
    return ERROR;
}

ErrorStatus BSP_I2C3_ReadReg(uint8_t devAddr, uint8_t reg,
                              uint8_t *pData, uint16_t len)
{
    uint32_t timeout;
    uint16_t i;

    if (len == 0U) { return SUCCESS; }

    if (WritePhase(devAddr, reg) != SUCCESS) { goto error; }

    I2C_GenerateSTART(BSP_I2C3_PERIPH, ENABLE);
    timeout = BSP_I2C3_TIMEOUT;
    while (!I2C_CheckEvent(BSP_I2C3_PERIPH, I2C_EVENT_MASTER_MODE_SELECT))
    {
        if (--timeout == 0U) { goto error; }
    }

    I2C_Send7bitAddress(BSP_I2C3_PERIPH, (uint8_t)(devAddr << 1U),
                        I2C_Direction_Receiver);

    if (len == 1U)
    {
        timeout = BSP_I2C3_TIMEOUT;
        while (I2C_GetFlagStatus(BSP_I2C3_PERIPH, I2C_FLAG_ADDR) == RESET)
        {
            if (--timeout == 0U) { goto error; }
        }
        I2C_AcknowledgeConfig(BSP_I2C3_PERIPH, DISABLE);
        (void)BSP_I2C3_PERIPH->SR1;
        (void)BSP_I2C3_PERIPH->SR2;
        I2C_GenerateSTOP(BSP_I2C3_PERIPH, ENABLE);

        timeout = BSP_I2C3_TIMEOUT;
        while (I2C_GetFlagStatus(BSP_I2C3_PERIPH, I2C_FLAG_RXNE) == RESET)
        {
            if (--timeout == 0U) { goto error; }
        }
        pData[0] = I2C_ReceiveData(BSP_I2C3_PERIPH);
    }
    else
    {
        timeout = BSP_I2C3_TIMEOUT;
        while (!I2C_CheckEvent(BSP_I2C3_PERIPH,
                               I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED))
        {
            if (--timeout == 0U) { goto error; }
        }

        I2C_AcknowledgeConfig(BSP_I2C3_PERIPH, ENABLE);

        for (i = 0U; i < len; i++)
        {
            if (i == len - 1U)
            {
                I2C_AcknowledgeConfig(BSP_I2C3_PERIPH, DISABLE);
                I2C_GenerateSTOP(BSP_I2C3_PERIPH, ENABLE);
            }

            timeout = BSP_I2C3_TIMEOUT;
            while (I2C_GetFlagStatus(BSP_I2C3_PERIPH, I2C_FLAG_RXNE) == RESET)
            {
                if (--timeout == 0U) { goto error; }
            }
            pData[i] = I2C_ReceiveData(BSP_I2C3_PERIPH);
        }
    }

    I2C_AcknowledgeConfig(BSP_I2C3_PERIPH, ENABLE);
    return SUCCESS;

error:
    I2C_AcknowledgeConfig(BSP_I2C3_PERIPH, ENABLE);
    I2C_GenerateSTOP(BSP_I2C3_PERIPH, ENABLE);
    BSP_I2C3_RecoverBus();
    return ERROR;
}

ErrorStatus BSP_I2C3_ReadRegs(uint8_t devAddr, uint8_t startReg,
                               uint8_t *pData, uint16_t len)
{
    return BSP_I2C3_ReadReg(devAddr, startReg, pData, len);
}

/*--------------------------------------------------------------------------*/
/*                              Bus scan                                    */
/*--------------------------------------------------------------------------*/

ErrorStatus BSP_I2C3_Scan(uint8_t *pAddrList, uint8_t *pCount)
{
    uint8_t  addr;
    uint32_t timeout;

    *pCount = 0U;

    for (addr = 1U; addr <= 126U; addr++)
    {
        timeout = BSP_I2C3_TIMEOUT;
        while (I2C_GetFlagStatus(BSP_I2C3_PERIPH, I2C_FLAG_BUSY) != RESET)
        {
            if (--timeout == 0U) { BSP_I2C3_RecoverBus(); break; }
        }

        I2C_GenerateSTART(BSP_I2C3_PERIPH, ENABLE);
        timeout = BSP_I2C3_TIMEOUT;
        while (!I2C_CheckEvent(BSP_I2C3_PERIPH, I2C_EVENT_MASTER_MODE_SELECT))
        {
            if (--timeout == 0U) { goto next; }
        }

        I2C_Send7bitAddress(BSP_I2C3_PERIPH, (uint8_t)(addr << 1U),
                            I2C_Direction_Transmitter);
        timeout = BSP_I2C3_TIMEOUT;
        while (!I2C_CheckEvent(BSP_I2C3_PERIPH,
                               I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED))
        {
            if (I2C_GetFlagStatus(BSP_I2C3_PERIPH, I2C_FLAG_AF) != RESET)
            {
                I2C_ClearFlag(BSP_I2C3_PERIPH, I2C_FLAG_AF);
                goto next;
            }
            if (--timeout == 0U) { goto next; }
        }

        pAddrList[(*pCount)++] = addr;

    next:
        I2C_GenerateSTOP(BSP_I2C3_PERIPH, ENABLE);
    }

    return SUCCESS;
}
