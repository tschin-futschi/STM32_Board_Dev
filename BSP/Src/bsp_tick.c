/**
  * @file    bsp_tick.c
  * @brief   SysTick millisecond counter
  */

#include "bsp_tick.h"

static volatile uint32_t s_tickMs;

/**
  * @brief  Configure SysTick for 1 ms interrupt
  */
void BSP_Tick_Init(void)
{
    /* SystemCoreClock is set by SystemInit() — 180 MHz */
    SysTick_Config(SystemCoreClock / 1000);
}

/**
  * @brief  Return current tick in milliseconds
  */
uint32_t BSP_GetTick(void)
{
    return s_tickMs;
}

/**
  * @brief  Called from SysTick_Handler ISR
  */
void BSP_Tick_Increment(void)
{
    s_tickMs++;
}
