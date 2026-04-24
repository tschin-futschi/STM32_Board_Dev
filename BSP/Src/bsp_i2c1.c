/**
  * @file    bsp_i2c1.c
  * @brief   I2C1 BSP — thin wrapper over common I2C driver
  *
  * Hardware: I2C1, PB6(SCL) / PB7(SDA), AF4, 400 kHz
  * Purpose:  INA power/current measurement (8-bit register address)
  */

#include "bsp_i2c1.h"
#include "bsp_i2c.h"

static const BSP_I2C_Config_t k_i2c1Cfg = {
    .periph       = BSP_I2C1_PERIPH,
    .rccApb1      = BSP_I2C1_RCC_APB1,
    .speed        = BSP_I2C1_SPEED,
    .timeout      = BSP_I2C1_TIMEOUT,
    .sclPort      = BSP_I2C1_SCL_GPIO_PORT,
    .sclClk       = BSP_I2C1_SCL_GPIO_CLK,
    .sclPin       = BSP_I2C1_SCL_PIN,
    .sclPinSource = BSP_I2C1_SCL_PIN_SOURCE,
    .sclAf        = BSP_I2C1_SCL_AF,
    .sdaPort      = BSP_I2C1_SDA_GPIO_PORT,
    .sdaClk       = BSP_I2C1_SDA_GPIO_CLK,
    .sdaPin       = BSP_I2C1_SDA_PIN,
    .sdaPinSource = BSP_I2C1_SDA_PIN_SOURCE,
    .sdaAf        = BSP_I2C1_SDA_AF,
    .regAddr16    = 0U,
    .pfnPostRecover = NULL,
};

ErrorStatus BSP_I2C1_Init(void)
{
    return BSP_I2C_Init(&k_i2c1Cfg);
}

void BSP_I2C1_RecoverBus(void)
{
    BSP_I2C_RecoverBus(&k_i2c1Cfg);
}

ErrorStatus BSP_I2C1_WriteReg(uint8_t devAddr, uint8_t reg,
                               const uint8_t *pData, uint16_t len)
{
    return BSP_I2C_WriteReg(&k_i2c1Cfg, devAddr, (uint16_t)reg, pData, len);
}

ErrorStatus BSP_I2C1_ReadReg(uint8_t devAddr, uint8_t reg,
                              uint8_t *pData, uint16_t len)
{
    return BSP_I2C_ReadReg(&k_i2c1Cfg, devAddr, (uint16_t)reg, pData, len);
}

ErrorStatus BSP_I2C1_ReadRegs(uint8_t devAddr, uint8_t startReg,
                               uint8_t *pData, uint16_t len)
{
    return BSP_I2C_ReadReg(&k_i2c1Cfg, devAddr, (uint16_t)startReg, pData, len);
}

ErrorStatus BSP_I2C1_Scan(uint8_t *pAddrList, uint8_t *pCount)
{
    return BSP_I2C_Scan(&k_i2c1Cfg, pAddrList, pCount);
}
