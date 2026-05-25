/**
  * @file    stm32f4xx_conf.h
  * @brief   SPL library configuration — enable only the peripherals in use.
  */

#ifndef __STM32F4xx_CONF_H
#define __STM32F4xx_CONF_H

/* Peripheral header files — enable as needed per phase */
#include "stm32f4xx_rcc.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_usart.h"
#include "stm32f4xx_dma.h"
#include "stm32f4xx_i2c.h"
#include "stm32f4xx_exti.h"
#include "stm32f4xx_syscfg.h"
#include "stm32f4xx_tim.h"
#include "stm32f4xx_flash.h"
#include "misc.h"

/* assert_param definition */
#ifdef USE_FULL_ASSERT
  #define assert_param(expr) ((expr) ? (void)0 : assert_failed((uint8_t *)__FILE__, __LINE__))
  void assert_failed(uint8_t* file, uint32_t line);
#else
  #define assert_param(expr) ((void)0)
#endif

#endif /* __STM32F4xx_CONF_H */
