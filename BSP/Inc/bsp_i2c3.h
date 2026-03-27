/**
  * @file    bsp_i2c3.h
  * @brief   I2C3 BSP — polling + software timeout, PMIC communication
  *
  * Hardware: I2C3, PA8(SCL) / PC9(SDA), AF4, 400 kHz
  */

#ifndef __BSP_I2C3_H
#define __BSP_I2C3_H

#include "stm32f4xx.h"

/*--------------------------------------------------------------------------*/
/*                          Hardware configuration                          */
/*--------------------------------------------------------------------------*/

#define BSP_I2C3_PERIPH             I2C3
#define BSP_I2C3_RCC_APB1           RCC_APB1Periph_I2C3

/* SCL: PA8, AF4 */
#define BSP_I2C3_SCL_GPIO_PORT      GPIOA
#define BSP_I2C3_SCL_GPIO_CLK       RCC_AHB1Periph_GPIOA
#define BSP_I2C3_SCL_PIN            GPIO_Pin_8
#define BSP_I2C3_SCL_PIN_SOURCE     GPIO_PinSource8
#define BSP_I2C3_SCL_AF             GPIO_AF_I2C3

/* SDA: PC9, AF4 */
#define BSP_I2C3_SDA_GPIO_PORT      GPIOC
#define BSP_I2C3_SDA_GPIO_CLK       RCC_AHB1Periph_GPIOC
#define BSP_I2C3_SDA_PIN            GPIO_Pin_9
#define BSP_I2C3_SDA_PIN_SOURCE     GPIO_PinSource9
#define BSP_I2C3_SDA_AF             GPIO_AF_I2C3

/* Speed & timeout */
#define BSP_I2C3_SPEED              400000U     /* 400 kHz fast mode            */
#define BSP_I2C3_TIMEOUT            10000U      /* Software timeout loop count  */

/*--------------------------------------------------------------------------*/
/*                               API                                        */
/*--------------------------------------------------------------------------*/

ErrorStatus BSP_I2C3_Init(void);
ErrorStatus BSP_I2C3_WriteReg(uint8_t devAddr, uint8_t reg,
                               const uint8_t *pData, uint16_t len);
ErrorStatus BSP_I2C3_ReadReg(uint8_t devAddr, uint8_t reg,
                              uint8_t *pData, uint16_t len);
ErrorStatus BSP_I2C3_ReadRegs(uint8_t devAddr, uint8_t startReg,
                               uint8_t *pData, uint16_t len);
ErrorStatus BSP_I2C3_Scan(uint8_t *pAddrList, uint8_t *pCount);
void        BSP_I2C3_RecoverBus(void);

#endif /* __BSP_I2C3_H */
