/**
  * @file    bsp_i2c1.h
  * @brief   I2C1 BSP — polling + software timeout, motor IC communication
  *
  * Hardware: I2C1, PB6(SCL) / PB7(SDA), AF4, 400 kHz
  */

#ifndef __BSP_I2C1_H
#define __BSP_I2C1_H

#include "stm32f4xx.h"

/*--------------------------------------------------------------------------*/
/*                          Hardware configuration                          */
/*--------------------------------------------------------------------------*/

#define BSP_I2C1_PERIPH             I2C1
#define BSP_I2C1_RCC_APB1           RCC_APB1Periph_I2C1

/* SCL: PB6, AF4 */
#define BSP_I2C1_SCL_GPIO_PORT      GPIOB
#define BSP_I2C1_SCL_GPIO_CLK       RCC_AHB1Periph_GPIOB
#define BSP_I2C1_SCL_PIN            GPIO_Pin_6
#define BSP_I2C1_SCL_PIN_SOURCE     GPIO_PinSource6
#define BSP_I2C1_SCL_AF             GPIO_AF_I2C1

/* SDA: PB7, AF4 */
#define BSP_I2C1_SDA_GPIO_PORT      GPIOB
#define BSP_I2C1_SDA_GPIO_CLK       RCC_AHB1Periph_GPIOB
#define BSP_I2C1_SDA_PIN            GPIO_Pin_7
#define BSP_I2C1_SDA_PIN_SOURCE     GPIO_PinSource7
#define BSP_I2C1_SDA_AF             GPIO_AF_I2C1

/* Speed & timeout */
#define BSP_I2C1_SPEED              400000U     /* 400 kHz fast mode            */
#define BSP_I2C1_TIMEOUT            10000U      /* Software timeout loop count  */

/*--------------------------------------------------------------------------*/
/*                          Motor IC runtime config                         */
/*--------------------------------------------------------------------------*/

extern uint8_t  g_motorIcAddr;      /* 7-bit I2C address, set by protocol   */
extern char     g_motorIcName[16];  /* Human-readable name, set by protocol  */

/*--------------------------------------------------------------------------*/
/*                               API                                        */
/*--------------------------------------------------------------------------*/

ErrorStatus BSP_I2C1_Init(void);
ErrorStatus BSP_I2C1_WriteReg(uint8_t devAddr, uint8_t reg,
                               const uint8_t *pData, uint16_t len);
ErrorStatus BSP_I2C1_ReadReg(uint8_t devAddr, uint8_t reg,
                              uint8_t *pData, uint16_t len);
ErrorStatus BSP_I2C1_ReadRegs(uint8_t devAddr, uint8_t startReg,
                               uint8_t *pData, uint16_t len);
ErrorStatus BSP_I2C1_Scan(uint8_t *pAddrList, uint8_t *pCount);
void        BSP_I2C1_RecoverBus(void);

#endif /* __BSP_I2C1_H */
