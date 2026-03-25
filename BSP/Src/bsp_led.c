/**
  * @file    bsp_led.c
  * @brief   LED driver — PG13 (heartbeat), PG14 (error)
  */

#include "bsp_led.h"

/**
  * @brief  Initialize LED GPIO pins as push-pull output
  */
void BSP_LED_Init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_AHB1PeriphClockCmd(BSP_LED_GPIO_CLK, ENABLE);

    gpio.GPIO_Pin   = BSP_LED1_PIN | BSP_LED2_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_OUT;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_Speed = GPIO_Low_Speed;
    gpio.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    GPIO_Init(BSP_LED_GPIO_PORT, &gpio);

    /* Start with both LEDs off (active high) */
    GPIO_ResetBits(BSP_LED_GPIO_PORT, BSP_LED1_PIN | BSP_LED2_PIN);
}

void BSP_LED1_Toggle(void)
{
    GPIO_ToggleBits(BSP_LED_GPIO_PORT, BSP_LED1_PIN);
}

void BSP_LED2_Toggle(void)
{
    GPIO_ToggleBits(BSP_LED_GPIO_PORT, BSP_LED2_PIN);
}

void BSP_LED1_On(void)
{
    GPIO_SetBits(BSP_LED_GPIO_PORT, BSP_LED1_PIN);
}

void BSP_LED1_Off(void)
{
    GPIO_ResetBits(BSP_LED_GPIO_PORT, BSP_LED1_PIN);
}

void BSP_LED2_On(void)
{
    GPIO_SetBits(BSP_LED_GPIO_PORT, BSP_LED2_PIN);
}

void BSP_LED2_Off(void)
{
    GPIO_ResetBits(BSP_LED_GPIO_PORT, BSP_LED2_PIN);
}
