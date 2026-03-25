/**
  * @file    bsp_tick.h
  * @brief   SysTick millisecond counter
  */

#ifndef __BSP_TICK_H
#define __BSP_TICK_H

#include "stm32f4xx.h"

void     BSP_Tick_Init(void);
uint32_t BSP_GetTick(void);
void     BSP_Tick_Increment(void);

#endif /* __BSP_TICK_H */
