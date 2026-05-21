/**
  * @file    bsp_pmic.h
  * @brief   PMIC BSP — RT5112WSC, I2C3 (0x20), power sequencing
  *
  * Power-on sequence: LDO2(DRVVDD) → LDO1(IOVDD) → LDO3(VCMVDD)
  * LDO4 permanently disabled.
  * Voltage formula:   reg = (volt_V - 0.6) × 80  (step 12.5 mV, range 0.6~3.775 V)
  */

#ifndef __BSP_PMIC_H
#define __BSP_PMIC_H

#include "stm32f4xx.h"

/*--------------------------------------------------------------------------*/
/*                          I2C device address                              */
/*--------------------------------------------------------------------------*/

#define BSP_PMIC_I2C_ADDR           0x20U   /* 7-bit address                */

/*--------------------------------------------------------------------------*/
/*                          Register addresses                              */
/*--------------------------------------------------------------------------*/

#define BSP_PMIC_REG_BUCK_CTRL      0x00U
#define BSP_PMIC_REG_LDO1_CTRL      0x01U
#define BSP_PMIC_REG_LDO2_CTRL      0x02U
#define BSP_PMIC_REG_LDO3_CTRL      0x03U
#define BSP_PMIC_REG_LDO4_CTRL      0x04U
#define BSP_PMIC_REG_BOOST_CTRL     0x05U
#define BSP_PMIC_REG_BUCK2_CTRL     0x06U
#define BSP_PMIC_REG_PRODUCT_ID     0x07U
#define BSP_PMIC_REG_BOOST_EN       0x08U
#define BSP_PMIC_REG_SEQ_PROG       0x09U
#define BSP_PMIC_REG_LATCH          0x0AU
#define BSP_PMIC_REG_DISCHARGE      0x0BU
#define BSP_PMIC_REG_BUCK_VOUT      0x0CU
#define BSP_PMIC_REG_LDO1_VOUT      0x0DU
#define BSP_PMIC_REG_LDO2_VOUT      0x0EU
#define BSP_PMIC_REG_LDO3_VOUT      0x0FU
#define BSP_PMIC_REG_LDO4_VOUT      0x10U
#define BSP_PMIC_REG_BOOST_VOUT     0x11U
#define BSP_PMIC_REG_BUCK2_VOUT     0x12U
#define BSP_PMIC_REG_LX_SR          0x13U
#define BSP_PMIC_REG_DVS            0x14U

/*--------------------------------------------------------------------------*/
/*                          Channel identifiers                             */
/*--------------------------------------------------------------------------*/

#define BSP_PMIC_CH_BUCK            0U
#define BSP_PMIC_CH_LDO1            1U
#define BSP_PMIC_CH_LDO2            2U
#define BSP_PMIC_CH_LDO3            3U
#define BSP_PMIC_CH_LDO4            4U

/*--------------------------------------------------------------------------*/
/*                      Control register bit values                         */
/*--------------------------------------------------------------------------*/

/* Bit7=1: output enabled; Bit7=0: disabled; Bits[3:2]=mode (keep 0x0C) */
#define BSP_PMIC_CTRL_EN_BIT        0x80U   /* Bit7 mask for enable readback */
#define BSP_PMIC_CTRL_ENABLE        0x8CU
#define BSP_PMIC_CTRL_DISABLE       0x0CU

/*--------------------------------------------------------------------------*/
/*                          Voltage formula                                 */
/*--------------------------------------------------------------------------*/

#define BSP_PMIC_VOLT_MIN_V         0.6f
#define BSP_PMIC_VOLT_MAX_V         3.775f
#define BSP_PMIC_VOLT_SCALE         80.0f

/*--------------------------------------------------------------------------*/
/*                   Voltage config structure & defaults                    */
/*--------------------------------------------------------------------------*/

/* Values are actual voltage (V) × 100, uint16_t.  E.g. 1.80V → 180 */
#define BSP_PMIC_DEFAULT_DRVVDD     180U    /* LDO2, 1.80V */
#define BSP_PMIC_DEFAULT_IOVDD      280U    /* LDO1, 2.80V */
#define BSP_PMIC_DEFAULT_VCMVDD     320U    /* LDO3, 3.20V */

/* Valid range: 0.60V~3.77V → 60~377 */
#define BSP_PMIC_VOLT_MIN_X100      60U
#define BSP_PMIC_VOLT_MAX_X100      377U

typedef struct
{
    uint16_t drvVdd;    /* LDO2 voltage, V × 100 */
    uint16_t ioVdd;     /* LDO1 voltage, V × 100 */
    uint16_t vcmVdd;    /* LDO3 voltage, V × 100 */
} PMIC_Voltage_t;

extern volatile PMIC_Voltage_t g_pmicVoltage;

/*--------------------------------------------------------------------------*/
/*                  Power-on inter-step delays (ms)                         */
/*--------------------------------------------------------------------------*/

#define BSP_PMIC_SEQ_DELAY_MS       10U     /* Delay between LDO enable steps */

/*--------------------------------------------------------------------------*/
/*                      HWEN signal: PE4 / EXTI4                            */
/*--------------------------------------------------------------------------*/

#define BSP_PMIC_HWEN_GPIO_PORT     GPIOE
#define BSP_PMIC_HWEN_GPIO_CLK      RCC_AHB1Periph_GPIOE
#define BSP_PMIC_HWEN_PIN           GPIO_Pin_4
#define BSP_PMIC_HWEN_EXTI_PORT     EXTI_PortSourceGPIOE
#define BSP_PMIC_HWEN_EXTI_PIN      EXTI_PinSource4
#define BSP_PMIC_HWEN_EXTI_LINE     EXTI_Line4
#define BSP_PMIC_HWEN_IRQn          EXTI4_IRQn
#define BSP_PMIC_HWEN_IRQ_PRIORITY  8U

/*--------------------------------------------------------------------------*/
/*                          Global flag                                     */
/*--------------------------------------------------------------------------*/

/** Set to 1 by EXTI4 ISR when HWEN rises; cleared by main loop after BSP_PMIC_Init(). */
extern volatile uint8_t g_pmicHwenFlag;

/*--------------------------------------------------------------------------*/
/*                               API                                        */
/*--------------------------------------------------------------------------*/

ErrorStatus BSP_PMIC_Init(void);
void        BSP_PMIC_HwenInit(void);
ErrorStatus BSP_PMIC_ReadPid(uint8_t *pPid);
ErrorStatus BSP_PMIC_SetVout(uint8_t ch, float voltV);
ErrorStatus BSP_PMIC_SetEnable(uint8_t ch, FunctionalState state);
ErrorStatus BSP_PMIC_EnableSequence(void);
ErrorStatus BSP_PMIC_DisableSequence(void);

#endif /* __BSP_PMIC_H */
