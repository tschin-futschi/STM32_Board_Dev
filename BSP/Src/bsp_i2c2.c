/**
  * @file    bsp_i2c2.c
  * @brief   I2C2 BSP — thin wrapper over common I2C driver
  *
  * Hardware: I2C2, PB10(SCL) / PB11(SDA), AF4, 400 kHz
  * Purpose:  Motor IC communication (16-bit register address)
  *
  * Also hosts motor IC runtime config (g_motorIcAddr / g_motorIcName)
  * and bus recovery flag, per CLAUDE.md convention.
  */

#include "bsp_i2c2.h"
#include "bsp_i2c.h"

/*--------------------------------------------------------------------------*/
/*                        Motor IC runtime config                           */
/*--------------------------------------------------------------------------*/

uint8_t g_motorIcAddr     = 0x00U;
char    g_motorIcName[16] = "UNKNOWN";

/*--------------------------------------------------------------------------*/
/*                        Recovery flag                                     */
/*--------------------------------------------------------------------------*/

static uint8_t s_busRecovered = 0U;

static void PostRecoverCallback(void)
{
    s_busRecovered = 1U;
}

/*--------------------------------------------------------------------------*/
/*                        Instance configuration                           */
/*--------------------------------------------------------------------------*/

static const BSP_I2C_Config_t k_i2c2Cfg = {
    .periph       = BSP_I2C2_PERIPH,
    .rccApb1      = BSP_I2C2_RCC_APB1,
    .speed        = BSP_I2C2_SPEED,
    .timeout      = BSP_I2C2_TIMEOUT,
    .sclPort      = BSP_I2C2_SCL_GPIO_PORT,
    .sclClk       = BSP_I2C2_SCL_GPIO_CLK,
    .sclPin       = BSP_I2C2_SCL_PIN,
    .sclPinSource = BSP_I2C2_SCL_PIN_SOURCE,
    .sclAf        = BSP_I2C2_SCL_AF,
    .sdaPort      = BSP_I2C2_SDA_GPIO_PORT,
    .sdaClk       = BSP_I2C2_SDA_GPIO_CLK,
    .sdaPin       = BSP_I2C2_SDA_PIN,
    .sdaPinSource = BSP_I2C2_SDA_PIN_SOURCE,
    .sdaAf        = BSP_I2C2_SDA_AF,
    .regAddr16    = 1U,
    .pfnPostRecover = PostRecoverCallback,
};

/*--------------------------------------------------------------------------*/
/*                          Public API                                      */
/*--------------------------------------------------------------------------*/

ErrorStatus BSP_I2C2_Init(void)
{
    return BSP_I2C_Init(&k_i2c2Cfg);
}

void BSP_I2C2_RecoverBus(void)
{
    BSP_I2C_RecoverBus(&k_i2c2Cfg);
}

ErrorStatus BSP_I2C2_WriteReg(uint8_t devAddr, uint16_t reg,
                               const uint8_t *pData, uint16_t len)
{
    return BSP_I2C_WriteReg(&k_i2c2Cfg, devAddr, reg, pData, len);
}

ErrorStatus BSP_I2C2_ReadReg(uint8_t devAddr, uint16_t reg,
                              uint8_t *pData, uint16_t len)
{
    return BSP_I2C_ReadReg(&k_i2c2Cfg, devAddr, reg, pData, len);
}

ErrorStatus BSP_I2C2_ReadRegs(uint8_t devAddr, uint16_t startReg,
                               uint8_t *pData, uint16_t len)
{
    return BSP_I2C_ReadReg(&k_i2c2Cfg, devAddr, startReg, pData, len);
}

uint8_t BSP_I2C2_GetAndClearRecoveryFlag(void)
{
    uint8_t flag = s_busRecovered;
    s_busRecovered = 0U;
    return flag;
}

ErrorStatus BSP_I2C2_Scan(uint8_t *pAddrList, uint8_t *pCount)
{
    return BSP_I2C_Scan(&k_i2c2Cfg, pAddrList, pCount);
}
