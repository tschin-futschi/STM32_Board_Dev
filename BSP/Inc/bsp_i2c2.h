/**
  * @file    bsp_i2c2.h
  * @brief   I2C2 BSP — software (bit-bang) I2C, motor IC communication
  *
  * Hardware: PB10(SCL) / PB11(SDA), GPIO open-drain bit-bang, 1 MHz
  */

#ifndef __BSP_I2C2_H
#define __BSP_I2C2_H

#include "stm32f4xx.h"

/*--------------------------------------------------------------------------*/
/*                        Hardware configuration                            */
/*--------------------------------------------------------------------------*/

/* SCL: PB10 */
#define BSP_I2C2_SCL_GPIO_PORT      GPIOB
#define BSP_I2C2_SCL_GPIO_CLK       RCC_AHB1Periph_GPIOB
#define BSP_I2C2_SCL_PIN            GPIO_Pin_10

/* SDA: PB11 */
#define BSP_I2C2_SDA_GPIO_PORT      GPIOB
#define BSP_I2C2_SDA_GPIO_CLK       RCC_AHB1Periph_GPIOB
#define BSP_I2C2_SDA_PIN            GPIO_Pin_11

/*--------------------------------------------------------------------------*/
/*                    Software I2C timing (DWT cycles)                      */
/*--------------------------------------------------------------------------*/

#define BSP_I2C2_SW_SYSCLK_HZ       180000000U
#define BSP_I2C2_SW_I2C_HZ          1000000U
#define BSP_I2C2_SW_HALF_CYCLE      (BSP_I2C2_SW_SYSCLK_HZ / BSP_I2C2_SW_I2C_HZ / 2U) /* 90 cycles = 500 ns */
#define BSP_I2C2_SW_SCL_LOW_ADJ     6U          /* GPIO ops compensation    */
#define BSP_I2C2_SW_STRETCH_TIMEOUT 36000U      /* 200 us clock stretching  */
#define BSP_I2C2_SW_RECOVER_PULSES  9U          /* Bus recovery SCL pulses  */

/*--------------------------------------------------------------------------*/
/*                          Motor IC runtime config                         */
/*--------------------------------------------------------------------------*/

extern volatile uint8_t  g_motorIcAddr;      /* 7-bit I2C address, set by protocol   */
extern volatile char     g_motorIcName[16];  /* Human-readable name, set by protocol  */

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
uint8_t     BSP_I2C2_GetAndClearRecoveryFlag(void);

/* AW Firmware I2C passthrough — opaque byte-stream write/read.
 * AddrSize == 0 skips the address phase entirely.
 * Bytes in pAddr / pData are forwarded to the bus in array order. */
ErrorStatus BSP_I2C2_TransparentWrite(uint8_t devAddr,
                                       const uint8_t *pAddr, uint8_t addrSize,
                                       const uint8_t *pData, uint16_t dataLen);
ErrorStatus BSP_I2C2_TransparentRead(uint8_t devAddr,
                                      const uint8_t *pAddr, uint8_t addrSize,
                                      uint8_t *pData, uint16_t dataLen);

#endif /* __BSP_I2C2_H */
