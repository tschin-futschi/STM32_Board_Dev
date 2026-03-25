/**
  * @file    bsp_led.h
  * @brief   LED driver — PG13 (heartbeat), PG14 (error)
  */

#ifndef __BSP_LED_H
#define __BSP_LED_H

#include "stm32f4xx.h"

/* ---- Hardware macros (change pin here only) ---- */
#define BSP_LED_GPIO_PORT       GPIOG
#define BSP_LED_GPIO_CLK        RCC_AHB1Periph_GPIOG

#define BSP_LED1_PIN            GPIO_Pin_13   /* Heartbeat */
#define BSP_LED2_PIN            GPIO_Pin_14   /* Error     */

/* ---- API ---- */
void BSP_LED_Init(void);
void BSP_LED1_Toggle(void);
void BSP_LED2_Toggle(void);
void BSP_LED1_On(void);
void BSP_LED1_Off(void);
void BSP_LED2_On(void);
void BSP_LED2_Off(void);

#endif /* __BSP_LED_H */
