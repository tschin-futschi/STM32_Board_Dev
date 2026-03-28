/**
  * @file    test_config.h
  * @brief   测试项使能宏 — 1=启用 / 0=禁用（默认全 0，不影响正式固件）
  */

#ifndef TEST_CONFIG_H
#define TEST_CONFIG_H

/* PMIC -------------------------------------------------------------------- */
#define TEST_PMIC_PID_READ   0   /* 每 100 ms 读 Product ID，通过 UART 上报   */

/* I2C --------------------------------------------------------------------- */
#define TEST_I2C_SCAN        1   /* 上电后扫描 I2C1/I2C2/I2C3，结果通过 UART 上报 */

#endif /* TEST_CONFIG_H */
