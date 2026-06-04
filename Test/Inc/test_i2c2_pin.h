/**
  * @file    test_i2c2_pin.h
  * @brief   I2C2 引脚识别诊断模块声明
  */

#ifndef TEST_I2C2_PIN_H
#define TEST_I2C2_PIN_H

/* 永不返回：PB10 输出 1kHz 方波、PB11(SDA) 恒低，供逻辑分析仪确认 SCL/SDA 串扰 */
void Test_I2C2_PinId_Run(void);

#endif /* TEST_I2C2_PIN_H */
