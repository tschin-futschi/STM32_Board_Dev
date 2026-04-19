/**
  * @file    bsp_pmic.c
  * @brief   PMIC BSP — RT5112WSC power sequencing via I2C3
  *
  * Power-on sequence: LDO2(DRVVDD) → LDO1(IOVDD) → LDO3(VCMVDD)
  * LDO4 permanently disabled.
  * Init only pre-sets voltages; EnableSequence/DisableSequence control LDO on/off.
  *
  * HWEN (PE4 / EXTI4): rising edge triggers re-init + enable from main loop.
  */

#include "bsp_pmic.h"
#include "bsp_i2c3.h"
#include "bsp_tick.h"

/*--------------------------------------------------------------------------*/
/*                          Global flag                                     */
/*--------------------------------------------------------------------------*/

volatile uint8_t g_pmicHwenFlag = 0U;

PMIC_Voltage_t g_pmicVoltage = {
    .drvVdd  = BSP_PMIC_DEFAULT_DRVVDD,    /* LDO2, 1.80V */
    .ioVdd   = BSP_PMIC_DEFAULT_IOVDD,     /* LDO1, 2.80V */
    .vcmVdd  = BSP_PMIC_DEFAULT_VCMVDD,    /* LDO3, 3.20V */
};

/*--------------------------------------------------------------------------*/
/*                          Static helpers                                  */
/*--------------------------------------------------------------------------*/

/**
  * @brief Blocking millisecond wait using system tick (BSP_GetTick).
  */
static void WaitMs(uint32_t ms)
{
    uint32_t start = BSP_GetTick();
    while ((BSP_GetTick() - start) < ms) { ; }
}

/**
  * @brief Write a single byte to a PMIC register.
  */
static ErrorStatus WriteReg(uint8_t reg, uint8_t val)
{
    return BSP_I2C3_WriteReg(BSP_PMIC_I2C_ADDR, reg, &val, 1U);
}

/**
  * @brief Read a single byte from a PMIC register.
  */
static ErrorStatus ReadReg(uint8_t reg, uint8_t *pVal)
{
    return BSP_I2C3_ReadReg(BSP_PMIC_I2C_ADDR, reg, pVal, 1U);
}

/*--------------------------------------------------------------------------*/
/*                            Public API                                    */
/*--------------------------------------------------------------------------*/

/**
  * @brief  PMIC initialisation — pre-set voltages only, LDOs remain disabled.
  *         Call BSP_PMIC_EnableSequence() separately to enable LDOs.
  * @retval SUCCESS / ERROR (I2C communication failure)
  */
ErrorStatus BSP_PMIC_Init(void)
{
    float drvV  = (float)g_pmicVoltage.drvVdd  / 100.0f;
    float ioV   = (float)g_pmicVoltage.ioVdd   / 100.0f;
    float vcmV  = (float)g_pmicVoltage.vcmVdd  / 100.0f;

    /* Pre-set output voltages (LDO2=DRVVDD, LDO1=IOVDD, LDO3=VCMVDD) */
    if (BSP_PMIC_SetVout(BSP_PMIC_CH_LDO2, drvV) != SUCCESS) { return ERROR; }
    if (BSP_PMIC_SetVout(BSP_PMIC_CH_LDO1, ioV)  != SUCCESS) { return ERROR; }
    if (BSP_PMIC_SetVout(BSP_PMIC_CH_LDO3, vcmV) != SUCCESS) { return ERROR; }

    return SUCCESS;
}

/**
  * @brief  Enable LDOs in fixed sequence: LDO2 → 10ms → LDO1 → 10ms → LDO3.
  *         Pre-sets voltages from g_pmicVoltage before enabling.
  *         Read-back verifies Bit7 is set for each channel.
  * @retval SUCCESS / ERROR
  */
ErrorStatus BSP_PMIC_EnableSequence(void)
{
    uint8_t tmp;

    /* Pre-set voltages first */
    if (BSP_PMIC_Init() != SUCCESS) { return ERROR; }

    /* Enable in sequence: LDO2(DRVVDD) → LDO1(IOVDD) → LDO3(VCMVDD) */
    if (BSP_PMIC_SetEnable(BSP_PMIC_CH_LDO2, ENABLE) != SUCCESS) { return ERROR; }
    WaitMs(BSP_PMIC_SEQ_DELAY_MS);

    if (BSP_PMIC_SetEnable(BSP_PMIC_CH_LDO1, ENABLE) != SUCCESS) { return ERROR; }
    WaitMs(BSP_PMIC_SEQ_DELAY_MS);

    if (BSP_PMIC_SetEnable(BSP_PMIC_CH_LDO3, ENABLE) != SUCCESS) { return ERROR; }

    /* Read-back to verify Bit7 (enable bit) is set */
    if (ReadReg(BSP_PMIC_REG_LDO2_CTRL, &tmp) != SUCCESS) { return ERROR; }
    if ((tmp & BSP_PMIC_CTRL_EN_BIT) == 0U) { return ERROR; }

    if (ReadReg(BSP_PMIC_REG_LDO1_CTRL, &tmp) != SUCCESS) { return ERROR; }
    if ((tmp & BSP_PMIC_CTRL_EN_BIT) == 0U) { return ERROR; }

    if (ReadReg(BSP_PMIC_REG_LDO3_CTRL, &tmp) != SUCCESS) { return ERROR; }
    if ((tmp & BSP_PMIC_CTRL_EN_BIT) == 0U) { return ERROR; }

    return SUCCESS;
}

/**
  * @brief  Disable LDOs in reverse sequence: LDO3 → 10ms → LDO1 → 10ms → LDO2.
  *         Read-back verifies Bit7 is cleared for each channel.
  * @retval SUCCESS / ERROR
  */
ErrorStatus BSP_PMIC_DisableSequence(void)
{
    uint8_t tmp;

    /* Disable in reverse: LDO3(VCMVDD) → LDO1(IOVDD) → LDO2(DRVVDD) */
    if (BSP_PMIC_SetEnable(BSP_PMIC_CH_LDO3, DISABLE) != SUCCESS) { return ERROR; }
    WaitMs(BSP_PMIC_SEQ_DELAY_MS);

    if (BSP_PMIC_SetEnable(BSP_PMIC_CH_LDO1, DISABLE) != SUCCESS) { return ERROR; }
    WaitMs(BSP_PMIC_SEQ_DELAY_MS);

    if (BSP_PMIC_SetEnable(BSP_PMIC_CH_LDO2, DISABLE) != SUCCESS) { return ERROR; }

    /* Read-back to verify Bit7 (enable bit) is cleared */
    if (ReadReg(BSP_PMIC_REG_LDO3_CTRL, &tmp) != SUCCESS) { return ERROR; }
    if ((tmp & BSP_PMIC_CTRL_EN_BIT) != 0U) { return ERROR; }

    if (ReadReg(BSP_PMIC_REG_LDO1_CTRL, &tmp) != SUCCESS) { return ERROR; }
    if ((tmp & BSP_PMIC_CTRL_EN_BIT) != 0U) { return ERROR; }

    if (ReadReg(BSP_PMIC_REG_LDO2_CTRL, &tmp) != SUCCESS) { return ERROR; }
    if ((tmp & BSP_PMIC_CTRL_EN_BIT) != 0U) { return ERROR; }

    return SUCCESS;
}

/**
  * @brief  Configure PE4 as EXTI4 (rising-edge) to monitor PMIC HWEN.
  *         Call AFTER BSP_PMIC_Init() to avoid spurious re-init at startup.
  */
void BSP_PMIC_HwenInit(void)
{
    GPIO_InitTypeDef gpioInit;
    EXTI_InitTypeDef extiInit;
    NVIC_InitTypeDef nvicInit;

    /* Enable clocks */
    RCC_AHB1PeriphClockCmd(BSP_PMIC_HWEN_GPIO_CLK,    ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG,      ENABLE);

    /* PE4 as floating input */
    gpioInit.GPIO_Pin   = BSP_PMIC_HWEN_PIN;
    gpioInit.GPIO_Mode  = GPIO_Mode_IN;
    gpioInit.GPIO_Speed = GPIO_Speed_50MHz;
    gpioInit.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    GPIO_Init(BSP_PMIC_HWEN_GPIO_PORT, &gpioInit);

    /* Map PE4 → EXTI Line4 */
    SYSCFG_EXTILineConfig(BSP_PMIC_HWEN_EXTI_PORT, BSP_PMIC_HWEN_EXTI_PIN);

    /* Rising-edge interrupt */
    extiInit.EXTI_Line    = BSP_PMIC_HWEN_EXTI_LINE;
    extiInit.EXTI_LineCmd = ENABLE;
    extiInit.EXTI_Mode    = EXTI_Mode_Interrupt;
    extiInit.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_Init(&extiInit);

    /* NVIC: priority 8, sub-priority unused (Group_4) */
    nvicInit.NVIC_IRQChannel                   = BSP_PMIC_HWEN_IRQn;
    nvicInit.NVIC_IRQChannelPreemptionPriority = BSP_PMIC_HWEN_IRQ_PRIORITY;
    nvicInit.NVIC_IRQChannelSubPriority        = 0U;
    nvicInit.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvicInit);
}

/**
  * @brief  Read PRODUCT_ID register. Used for startup self-check.
  * @param  pPid  Output: product ID byte.
  * @retval SUCCESS / ERROR (I2C failure)
  */
ErrorStatus BSP_PMIC_ReadPid(uint8_t *pPid)
{
    return ReadReg(BSP_PMIC_REG_PRODUCT_ID, pPid);
}

/**
  * @brief  Set output voltage for a given channel.
  *         reg = (voltV - 0.6) × 80, range 0.6~3.775 V, step 12.5 mV.
  * @retval SUCCESS / ERROR
  */
ErrorStatus BSP_PMIC_SetVout(uint8_t ch, float voltV)
{
    uint8_t regAddr;
    uint8_t regVal;

    /* Range check (inclusive bounds) */
    if (!((voltV >= BSP_PMIC_VOLT_MIN_V) && (voltV <= BSP_PMIC_VOLT_MAX_V)))
    {
        return ERROR;
    }

    regVal = (uint8_t)((voltV - BSP_PMIC_VOLT_MIN_V) * BSP_PMIC_VOLT_SCALE + 0.5f);

    switch (ch)
    {
        case BSP_PMIC_CH_BUCK: regAddr = BSP_PMIC_REG_BUCK_VOUT; break;
        case BSP_PMIC_CH_LDO1: regAddr = BSP_PMIC_REG_LDO1_VOUT; break;
        case BSP_PMIC_CH_LDO2: regAddr = BSP_PMIC_REG_LDO2_VOUT; break;
        case BSP_PMIC_CH_LDO3: regAddr = BSP_PMIC_REG_LDO3_VOUT; break;
        case BSP_PMIC_CH_LDO4: regAddr = BSP_PMIC_REG_LDO4_VOUT; break;
        default: return ERROR;
    }

    return WriteReg(regAddr, regVal);
}

/**
  * @brief  Enable or disable a channel.
  *         ENABLE  → writes BSP_PMIC_CTRL_ENABLE  (0x8C)
  *         DISABLE → writes BSP_PMIC_CTRL_DISABLE (0x0C)
  * @retval SUCCESS / ERROR
  */
ErrorStatus BSP_PMIC_SetEnable(uint8_t ch, FunctionalState state)
{
    uint8_t regAddr;
    uint8_t val = (state == ENABLE) ? BSP_PMIC_CTRL_ENABLE : BSP_PMIC_CTRL_DISABLE;

    switch (ch)
    {
        case BSP_PMIC_CH_BUCK: regAddr = BSP_PMIC_REG_BUCK_CTRL; break;
        case BSP_PMIC_CH_LDO1: regAddr = BSP_PMIC_REG_LDO1_CTRL; break;
        case BSP_PMIC_CH_LDO2: regAddr = BSP_PMIC_REG_LDO2_CTRL; break;
        case BSP_PMIC_CH_LDO3: regAddr = BSP_PMIC_REG_LDO3_CTRL; break;
        case BSP_PMIC_CH_LDO4: regAddr = BSP_PMIC_REG_LDO4_CTRL; break;
        default: return ERROR;
    }

    return WriteReg(regAddr, val);
}
