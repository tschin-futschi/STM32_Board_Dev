/**
  * @file    bsp_led.h
  * @brief   LED driver — PG13 (heartbeat), PG14 (error)
  */

#ifndef __BSP_LED_H
#define __BSP_LED_H

#include "stm32f4xx.h"

/* ---- Hardware macros (change pin here only) ---- */
#define BSP_LED_GPIO_PORT       GPIOF
#define BSP_LED_GPIO_CLK        RCC_AHB1Periph_GPIOF

#define BSP_LED1_PIN            GPIO_Pin_13   /* Heartbeat */

/* ---- API ---- */
void BSP_LED_Init(void);
void BSP_LED1_Toggle(void);
void BSP_LED1_On(void);
void BSP_LED1_Off(void);

#endif /* __BSP_LED_H */
