/**
  * @file    bsp_i2c2.h
  * @brief   I2C2 BSP — polling + software timeout, motor IC communication
  *
  * Hardware: I2C2, PB10(SCL) / PB11(SDA), AF4, 400 kHz
  */

#ifndef __BSP_I2C2_H
#define __BSP_I2C2_H

#include "stm32f4xx.h"

/*--------------------------------------------------------------------------*/
/*                          Hardware configuration                          */
/*--------------------------------------------------------------------------*/

#define BSP_I2C2_PERIPH             I2C2
#define BSP_I2C2_RCC_APB1           RCC_APB1Periph_I2C2

/* SCL: PB10, AF4 */
#define BSP_I2C2_SCL_GPIO_PORT      GPIOB
#define BSP_I2C2_SCL_GPIO_CLK       RCC_AHB1Periph_GPIOB
#define BSP_I2C2_SCL_PIN            GPIO_Pin_10
#define BSP_I2C2_SCL_PIN_SOURCE     GPIO_PinSource10
#define BSP_I2C2_SCL_AF             GPIO_AF_I2C2

/* SDA: PB11, AF4 */
#define BSP_I2C2_SDA_GPIO_PORT      GPIOB
#define BSP_I2C2_SDA_GPIO_CLK       RCC_AHB1Periph_GPIOB
#define BSP_I2C2_SDA_PIN            GPIO_Pin_11
#define BSP_I2C2_SDA_PIN_SOURCE     GPIO_PinSource11
#define BSP_I2C2_SDA_AF             GPIO_AF_I2C2

/* Speed & timeout */
#define BSP_I2C2_SPEED              400000U     /* 400 kHz fast mode            */
#define BSP_I2C2_TIMEOUT            10000U      /* Software timeout loop count  */

/*--------------------------------------------------------------------------*/
/*                          Motor IC runtime config                         */
/*--------------------------------------------------------------------------*/

extern uint8_t  g_motorIcAddr;      /* 7-bit I2C address, set by protocol   */
extern char     g_motorIcName[16];  /* Human-readable name, set by protocol  */

/*--------------------------------------------------------------------------*/
/*                               API                                        */
/*--------------------------------------------------------------------------*/

ErrorStatus BSP_I2C2_Init(void);
ErrorStatus BSP_I2C2_WriteReg(uint8_t devAddr, uint16_t reg,
                               const uint8_t *pData, uint16_t len);
ErrorStatus BSP_I2C2_ReadReg(uint8_t devAddr, uint16_t reg,
                              uint8_t *pData, uint16_t len);
ErrorStatus BSP_I2C2_ReadRegs(uint8_t devAddr, uint16_t startReg,
                               uint8_t *pData, uint16_t len);
ErrorStatus BSP_I2C2_Scan(uint8_t *pAddrList, uint8_t *pCount);
void        BSP_I2C2_RecoverBus(void);

#endif /* __BSP_I2C2_H */
