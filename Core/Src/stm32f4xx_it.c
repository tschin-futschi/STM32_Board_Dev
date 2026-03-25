/**
  * @file    stm32f4xx_it.c
  * @brief   All interrupt service routines
  */

#include "stm32f4xx_it.h"
#include "bsp_tick.h"

/*--------------------------------------------------------------------------*/
/*                    Cortex-M4 Processor Exception Handlers                */
/*--------------------------------------------------------------------------*/

void NMI_Handler(void)
{
}

void HardFault_Handler(void)
{
    while (1) {}
}

void MemManage_Handler(void)
{
    while (1) {}
}

void BusFault_Handler(void)
{
    while (1) {}
}

void UsageFault_Handler(void)
{
    while (1) {}
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

void SysTick_Handler(void)
{
    BSP_Tick_Increment();
}
