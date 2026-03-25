/**
  * @file    bsp_led.c
  * @brief   LED driver — PF13 (heartbeat)
  */

#include "bsp_led.h"

/**
  * @brief  Initialize LED GPIO pin as push-pull output
  */
void BSP_LED_Init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_AHB1PeriphClockCmd(BSP_LED_GPIO_CLK, ENABLE);

    gpio.GPIO_Pin   = BSP_LED1_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_OUT;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_Speed = GPIO_Low_Speed;
    gpio.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    GPIO_Init(BSP_LED_GPIO_PORT, &gpio);

    /* Start with LED off (active high) */
    GPIO_ResetBits(BSP_LED_GPIO_PORT, BSP_LED1_PIN);
}

void BSP_LED1_Toggle(void)
{
    GPIO_ToggleBits(BSP_LED_GPIO_PORT, BSP_LED1_PIN);
}

void BSP_LED1_On(void)
{
    GPIO_SetBits(BSP_LED_GPIO_PORT, BSP_LED1_PIN);
}

void BSP_LED1_Off(void)
{
    GPIO_ResetBits(BSP_LED_GPIO_PORT, BSP_LED1_PIN);
}
