/**
  * @file    app_motor.c
  * @brief   Motor IC register access — thin wrapper over BSP_I2C2
  *
  * Registers are 16-bit big-endian: byte[0] = high, byte[1] = low.
  */

#include "app_motor.h"
#include "bsp_i2c2.h"

/**
  * @brief  Read a 16-bit register from the motor IC.
  * @param  reg   Register address (16-bit).
  * @param  pVal  Output: register value (host byte order uint16).
  * @retval SUCCESS / ERROR
  */
ErrorStatus App_Motor_ReadReg(uint16_t reg, uint16_t *pVal)
{
    uint8_t buf[2];

    if (BSP_I2C2_ReadReg(g_motorIcAddr, reg, buf, 2U) != SUCCESS)
    {
        return ERROR;
    }

    /* Big-endian: buf[0] = high byte, buf[1] = low byte */
    *pVal = ((uint16_t)buf[0] << 8U) | (uint16_t)buf[1];
    return SUCCESS;
}

/**
  * @brief  Write a 16-bit register to the motor IC.
  * @param  reg  Register address (16-bit).
  * @param  val  Value to write (host byte order uint16).
  * @retval SUCCESS / ERROR
  */
ErrorStatus App_Motor_WriteReg(uint16_t reg, uint16_t val)
{
    uint8_t buf[2];

    /* Big-endian: high byte first */
    buf[0] = (uint8_t)(val >> 8U);
    buf[1] = (uint8_t)(val & 0xFFU);

    return BSP_I2C2_WriteReg(g_motorIcAddr, reg, buf, 2U);
}
