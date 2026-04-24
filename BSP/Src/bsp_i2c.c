/**
  * @file    bsp_i2c.c
  * @brief   Common I2C driver — instance-based polling + software timeout
  *
  * All I2C bus operations are implemented once here.
  * Bus-specific files (bsp_i2c1/2/3.c) provide thin wrappers with
  * static const config instances, preserving the existing BSP_I2Cx_ API.
  */

#include "bsp_i2c.h"

/*--------------------------------------------------------------------------*/
/*                          Static helpers                                  */
/*--------------------------------------------------------------------------*/

/**
  * @brief Configure SCL/SDA pins as AF open-drain.
  *        SCL and SDA are initialized independently to support
  *        buses where they reside on different GPIO ports (e.g. I2C3).
  */
static void ConfigGpioAF(const BSP_I2C_Config_t *cfg)
{
    GPIO_InitTypeDef gpioInit;

    gpioInit.GPIO_Mode  = GPIO_Mode_AF;
    gpioInit.GPIO_Speed = GPIO_Speed_50MHz;
    gpioInit.GPIO_OType = GPIO_OType_OD;
    gpioInit.GPIO_PuPd  = GPIO_PuPd_NOPULL;

    /* SCL */
    gpioInit.GPIO_Pin = cfg->sclPin;
    GPIO_Init(cfg->sclPort, &gpioInit);
    GPIO_PinAFConfig(cfg->sclPort, cfg->sclPinSource, cfg->sclAf);

    /* SDA */
    gpioInit.GPIO_Pin = cfg->sdaPin;
    GPIO_Init(cfg->sdaPort, &gpioInit);
    GPIO_PinAFConfig(cfg->sdaPort, cfg->sdaPinSource, cfg->sdaAf);
}

/**
  * @brief Initialize I2C peripheral registers (7-bit addressing).
  */
static void PeriphInit(const BSP_I2C_Config_t *cfg)
{
    I2C_InitTypeDef i2cInit;

    I2C_DeInit(cfg->periph);

    i2cInit.I2C_Mode                = I2C_Mode_I2C;
    i2cInit.I2C_DutyCycle           = I2C_DutyCycle_2;
    i2cInit.I2C_OwnAddress1         = 0x00U;
    i2cInit.I2C_Ack                 = I2C_Ack_Enable;
    i2cInit.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    i2cInit.I2C_ClockSpeed          = cfg->speed;

    I2C_Init(cfg->periph, &i2cInit);
    I2C_Cmd(cfg->periph, ENABLE);
}

/**
  * @brief  Write phase: START -> addr+W -> register address.
  *         For 16-bit reg address mode, sends high byte then low byte.
  *         For 8-bit reg address mode, sends only the low byte.
  */
static ErrorStatus WritePhase(const BSP_I2C_Config_t *cfg,
                               uint8_t devAddr, uint16_t reg)
{
    uint32_t timeout;

    /* Wait for bus free */
    timeout = cfg->timeout;
    while (I2C_GetFlagStatus(cfg->periph, I2C_FLAG_BUSY) != RESET)
    {
        if (--timeout == 0U) { return ERROR; }
    }

    /* START */
    I2C_GenerateSTART(cfg->periph, ENABLE);
    timeout = cfg->timeout;
    while (!I2C_CheckEvent(cfg->periph, I2C_EVENT_MASTER_MODE_SELECT))
    {
        if (--timeout == 0U) { return ERROR; }
    }

    /* Device address + WRITE */
    I2C_Send7bitAddress(cfg->periph, (uint8_t)(devAddr << 1U),
                        I2C_Direction_Transmitter);
    timeout = cfg->timeout;
    while (!I2C_CheckEvent(cfg->periph,
                           I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED))
    {
        /* AF set means NACK — device not present */
        if (I2C_GetFlagStatus(cfg->periph, I2C_FLAG_AF) != RESET)
        {
            I2C_ClearFlag(cfg->periph, I2C_FLAG_AF);
            return ERROR;
        }
        if (--timeout == 0U) { return ERROR; }
    }

    /* Register address */
    if (cfg->regAddr16 != 0U)
    {
        /* 16-bit: high byte first */
        I2C_SendData(cfg->periph, (uint8_t)(reg >> 8U));
        timeout = cfg->timeout;
        while (!I2C_CheckEvent(cfg->periph, I2C_EVENT_MASTER_BYTE_TRANSMITTED))
        {
            if (--timeout == 0U) { return ERROR; }
        }

        /* Low byte */
        I2C_SendData(cfg->periph, (uint8_t)(reg & 0xFFU));
        timeout = cfg->timeout;
        while (!I2C_CheckEvent(cfg->periph, I2C_EVENT_MASTER_BYTE_TRANSMITTED))
        {
            if (--timeout == 0U) { return ERROR; }
        }
    }
    else
    {
        /* 8-bit: single byte */
        I2C_SendData(cfg->periph, (uint8_t)reg);
        timeout = cfg->timeout;
        while (!I2C_CheckEvent(cfg->periph, I2C_EVENT_MASTER_BYTE_TRANSMITTED))
        {
            if (--timeout == 0U) { return ERROR; }
        }
    }

    return SUCCESS;
}

/*--------------------------------------------------------------------------*/
/*                          Public API                                      */
/*--------------------------------------------------------------------------*/

/**
  * @brief  Initialize I2C bus: clocks, GPIO, peripheral.
  *         If bus is stuck after init, RecoverBus() is called once.
  * @retval SUCCESS / ERROR (bus stuck after recovery)
  */
ErrorStatus BSP_I2C_Init(const BSP_I2C_Config_t *cfg)
{
    /* 1. Enable clocks */
    RCC_AHB1PeriphClockCmd(cfg->sclClk, ENABLE);
    if (cfg->sdaClk != cfg->sclClk)
    {
        RCC_AHB1PeriphClockCmd(cfg->sdaClk, ENABLE);
    }
    RCC_APB1PeriphClockCmd(cfg->rccApb1, ENABLE);

    /* 2. Configure GPIO as AF open-drain */
    ConfigGpioAF(cfg);

    /* 3. Initialize peripheral */
    PeriphInit(cfg);

    /* 4. Check for stuck bus; recover once if needed */
    if (I2C_GetFlagStatus(cfg->periph, I2C_FLAG_BUSY) != RESET)
    {
        BSP_I2C_RecoverBus(cfg);

        if (I2C_GetFlagStatus(cfg->periph, I2C_FLAG_BUSY) != RESET)
        {
            return ERROR;
        }
    }

    return SUCCESS;
}

/**
  * @brief  Recover a stuck I2C bus.
  *         1. Disable + software-reset peripheral
  *         2. Reconfigure pins as GPIO output open-drain
  *         3. Toggle SCL 9 times
  *         4. Manual STOP condition
  *         5. Restore AF + re-init peripheral
  *         6. Call pfnPostRecover if bus is now free
  */
void BSP_I2C_RecoverBus(const BSP_I2C_Config_t *cfg)
{
    GPIO_InitTypeDef  gpioInit;
    uint8_t           i;
    volatile uint32_t delay;

    /* 1. Disable and software-reset */
    I2C_Cmd(cfg->periph, DISABLE);
    I2C_SoftwareResetCmd(cfg->periph, ENABLE);
    I2C_SoftwareResetCmd(cfg->periph, DISABLE);
    I2C_DeInit(cfg->periph);

    /* 2. Reconfigure pins as GPIO output open-drain, drive high */
    gpioInit.GPIO_Mode  = GPIO_Mode_OUT;
    gpioInit.GPIO_Speed = GPIO_Speed_50MHz;
    gpioInit.GPIO_OType = GPIO_OType_OD;
    gpioInit.GPIO_PuPd  = GPIO_PuPd_NOPULL;

    gpioInit.GPIO_Pin = cfg->sclPin;
    GPIO_Init(cfg->sclPort, &gpioInit);
    GPIO_SetBits(cfg->sclPort, cfg->sclPin);

    gpioInit.GPIO_Pin = cfg->sdaPin;
    GPIO_Init(cfg->sdaPort, &gpioInit);
    GPIO_SetBits(cfg->sdaPort, cfg->sdaPin);

    /* 3. Toggle SCL 9 times */
    for (i = 0U; i < 9U; i++)
    {
        GPIO_ResetBits(cfg->sclPort, cfg->sclPin);
        for (delay = 0U; delay < 200U; delay++) { __NOP(); }
        GPIO_SetBits(cfg->sclPort, cfg->sclPin);
        for (delay = 0U; delay < 200U; delay++) { __NOP(); }
    }

    /* 4. Manual STOP: SDA low while SCL high, then SDA high */
    GPIO_ResetBits(cfg->sdaPort, cfg->sdaPin);
    for (delay = 0U; delay < 200U; delay++) { __NOP(); }
    GPIO_SetBits(cfg->sdaPort, cfg->sdaPin);
    for (delay = 0U; delay < 200U; delay++) { __NOP(); }

    /* 5. Restore GPIO as AF and re-init peripheral */
    ConfigGpioAF(cfg);
    PeriphInit(cfg);

    /* 6. Post-recovery callback (e.g. set recovery flag) */
    if ((cfg->pfnPostRecover != NULL) &&
        (I2C_GetFlagStatus(cfg->periph, I2C_FLAG_BUSY) == RESET))
    {
        cfg->pfnPostRecover();
    }
}

/**
  * @brief  Write len bytes to register reg of device devAddr.
  * @retval SUCCESS / ERROR (timeout -> RecoverBus called)
  */
ErrorStatus BSP_I2C_WriteReg(const BSP_I2C_Config_t *cfg,
                              uint8_t devAddr, uint16_t reg,
                              const uint8_t *pData, uint16_t len)
{
    uint32_t timeout;
    uint16_t i;

    if (WritePhase(cfg, devAddr, reg) != SUCCESS) { goto error; }

    /* Send data bytes */
    for (i = 0U; i < len; i++)
    {
        I2C_SendData(cfg->periph, pData[i]);
        timeout = cfg->timeout;
        while (!I2C_CheckEvent(cfg->periph, I2C_EVENT_MASTER_BYTE_TRANSMITTED))
        {
            if (--timeout == 0U) { goto error; }
        }
    }

    I2C_GenerateSTOP(cfg->periph, ENABLE);
    return SUCCESS;

error:
    I2C_GenerateSTOP(cfg->periph, ENABLE);
    BSP_I2C_RecoverBus(cfg);
    return ERROR;
}

/**
  * @brief  Read len bytes from register reg of device devAddr.
  * @retval SUCCESS / ERROR (timeout -> RecoverBus called)
  */
ErrorStatus BSP_I2C_ReadReg(const BSP_I2C_Config_t *cfg,
                             uint8_t devAddr, uint16_t reg,
                             uint8_t *pData, uint16_t len)
{
    uint32_t timeout;
    uint16_t i;

    if (len == 0U) { return SUCCESS; }

    if (WritePhase(cfg, devAddr, reg) != SUCCESS) { goto error; }

    /* Repeated START */
    I2C_GenerateSTART(cfg->periph, ENABLE);
    timeout = cfg->timeout;
    while (!I2C_CheckEvent(cfg->periph, I2C_EVENT_MASTER_MODE_SELECT))
    {
        if (--timeout == 0U) { goto error; }
    }

    /* Device address + READ */
    I2C_Send7bitAddress(cfg->periph, (uint8_t)(devAddr << 1U),
                        I2C_Direction_Receiver);

    if (len == 1U)
    {
        /* Single byte: disable ACK before clearing ADDR */
        timeout = cfg->timeout;
        while (I2C_GetFlagStatus(cfg->periph, I2C_FLAG_ADDR) == RESET)
        {
            if (--timeout == 0U) { goto error; }
        }
        I2C_AcknowledgeConfig(cfg->periph, DISABLE);
        /* Clear ADDR by reading SR1 then SR2 */
        (void)cfg->periph->SR1;
        (void)cfg->periph->SR2;
        I2C_GenerateSTOP(cfg->periph, ENABLE);

        timeout = cfg->timeout;
        while (I2C_GetFlagStatus(cfg->periph, I2C_FLAG_RXNE) == RESET)
        {
            if (--timeout == 0U) { goto error; }
        }
        pData[0] = I2C_ReceiveData(cfg->periph);
    }
    else
    {
        /* Multi-byte: clear ADDR with ACK enabled */
        timeout = cfg->timeout;
        while (!I2C_CheckEvent(cfg->periph,
                               I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED))
        {
            if (--timeout == 0U) { goto error; }
        }

        I2C_AcknowledgeConfig(cfg->periph, ENABLE);

        for (i = 0U; i < len; i++)
        {
            /* Before last byte: disable ACK and issue STOP */
            if (i == len - 1U)
            {
                I2C_AcknowledgeConfig(cfg->periph, DISABLE);
                I2C_GenerateSTOP(cfg->periph, ENABLE);
            }

            timeout = cfg->timeout;
            while (I2C_GetFlagStatus(cfg->periph, I2C_FLAG_RXNE) == RESET)
            {
                if (--timeout == 0U) { goto error; }
            }
            pData[i] = I2C_ReceiveData(cfg->periph);
        }
    }

    /* Restore ACK for next transfer */
    I2C_AcknowledgeConfig(cfg->periph, ENABLE);
    return SUCCESS;

error:
    I2C_AcknowledgeConfig(cfg->periph, ENABLE);
    I2C_GenerateSTOP(cfg->periph, ENABLE);
    BSP_I2C_RecoverBus(cfg);
    return ERROR;
}

/**
  * @brief  Scan all 7-bit addresses (1-126).
  * @retval SUCCESS always
  */
ErrorStatus BSP_I2C_Scan(const BSP_I2C_Config_t *cfg,
                          uint8_t *pAddrList, uint8_t *pCount)
{
    uint8_t  addr;
    uint32_t timeout;

    *pCount = 0U;

    for (addr = 1U; addr <= 126U; addr++)
    {
        /* Wait for bus free */
        timeout = cfg->timeout;
        while (I2C_GetFlagStatus(cfg->periph, I2C_FLAG_BUSY) != RESET)
        {
            if (--timeout == 0U) { BSP_I2C_RecoverBus(cfg); break; }
        }

        /* START */
        I2C_GenerateSTART(cfg->periph, ENABLE);
        timeout = cfg->timeout;
        while (!I2C_CheckEvent(cfg->periph, I2C_EVENT_MASTER_MODE_SELECT))
        {
            if (--timeout == 0U) { goto next; }
        }

        /* Send address + WRITE, check for ACK */
        I2C_Send7bitAddress(cfg->periph, (uint8_t)(addr << 1U),
                            I2C_Direction_Transmitter);
        timeout = cfg->timeout;
        while (!I2C_CheckEvent(cfg->periph,
                               I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED))
        {
            if (I2C_GetFlagStatus(cfg->periph, I2C_FLAG_AF) != RESET)
            {
                I2C_ClearFlag(cfg->periph, I2C_FLAG_AF);
                goto next;
            }
            if (--timeout == 0U) { goto next; }
        }

        /* ACK received — device present */
        pAddrList[(*pCount)++] = addr;

    next:
        I2C_GenerateSTOP(cfg->periph, ENABLE);
    }

    return SUCCESS;
}
