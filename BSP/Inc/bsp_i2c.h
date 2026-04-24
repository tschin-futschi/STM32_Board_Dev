/**
  * @file    bsp_i2c.h
  * @brief   Common I2C driver — instance-based polling + software timeout
  *
  * Each I2C bus is described by a BSP_I2C_Config_t instance.
  * Bus-specific wrappers (bsp_i2c1/2/3.c) provide the traditional API.
  */

#ifndef __BSP_I2C_H
#define __BSP_I2C_H

#include "stm32f4xx.h"
#include <stddef.h>

/*--------------------------------------------------------------------------*/
/*                       Instance configuration                            */
/*--------------------------------------------------------------------------*/

/** Post-recovery callback (optional, may be NULL) */
typedef void (*BSP_I2C_PostRecoverFn)(void);

typedef struct
{
    /* I2C peripheral */
    I2C_TypeDef    *periph;
    uint32_t        rccApb1;        /* RCC_APB1Periph_I2Cx              */
    uint32_t        speed;          /* I2C clock speed (Hz)             */
    uint32_t        timeout;        /* Software timeout loop count      */

    /* SCL pin */
    GPIO_TypeDef   *sclPort;
    uint32_t        sclClk;         /* RCC_AHB1Periph_GPIOx             */
    uint16_t        sclPin;         /* GPIO_Pin_x                       */
    uint16_t        sclPinSource;   /* GPIO_PinSourcex                  */
    uint8_t         sclAf;          /* GPIO_AF_I2Cx                     */

    /* SDA pin */
    GPIO_TypeDef   *sdaPort;
    uint32_t        sdaClk;         /* RCC_AHB1Periph_GPIOx             */
    uint16_t        sdaPin;         /* GPIO_Pin_x                       */
    uint16_t        sdaPinSource;   /* GPIO_PinSourcex                  */
    uint8_t         sdaAf;          /* GPIO_AF_I2Cx                     */

    /* Behavioral flags */
    uint8_t         regAddr16;      /* 1 = 16-bit reg address (motor IC) */

    /* Optional callback after successful bus recovery */
    BSP_I2C_PostRecoverFn pfnPostRecover;
} BSP_I2C_Config_t;

/*--------------------------------------------------------------------------*/
/*                          Common API                                      */
/*--------------------------------------------------------------------------*/

ErrorStatus BSP_I2C_Init(const BSP_I2C_Config_t *cfg);
void        BSP_I2C_RecoverBus(const BSP_I2C_Config_t *cfg);
ErrorStatus BSP_I2C_WriteReg(const BSP_I2C_Config_t *cfg,
                              uint8_t devAddr, uint16_t reg,
                              const uint8_t *pData, uint16_t len);
ErrorStatus BSP_I2C_ReadReg(const BSP_I2C_Config_t *cfg,
                             uint8_t devAddr, uint16_t reg,
                             uint8_t *pData, uint16_t len);
ErrorStatus BSP_I2C_Scan(const BSP_I2C_Config_t *cfg,
                          uint8_t *pAddrList, uint8_t *pCount);

#endif /* __BSP_I2C_H */
