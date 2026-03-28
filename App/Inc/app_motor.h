/**
  * @file    app_motor.h
  * @brief   Motor IC register access — thin wrapper over BSP_I2C2
  *
  * All registers are 16-bit big-endian (high byte first).
  * The target I2C address is taken from g_motorIcAddr at call time.
  */

#ifndef __APP_MOTOR_H
#define __APP_MOTOR_H

#include "stm32f4xx.h"

/*--------------------------------------------------------------------------*/
/*                               API                                        */
/*--------------------------------------------------------------------------*/

ErrorStatus App_Motor_ReadReg(uint8_t reg, uint16_t *pVal);
ErrorStatus App_Motor_WriteReg(uint8_t reg, uint16_t val);

#endif /* __APP_MOTOR_H */
