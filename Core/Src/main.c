/**
  * @file    main.c
  * @brief   Main program — LED heartbeat
  */

#include "main.h"
#include "bsp_tick.h"
#include "bsp_led.h"

int main(void)
{
    /* NVIC priority group — must be first */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);

    /* SysTick 1 ms */
    BSP_Tick_Init();

    /* LED */
    BSP_LED_Init();

    uint32_t prevTick = BSP_GetTick();

    while (1)
    {
        uint32_t now = BSP_GetTick();

        /* Heartbeat LED1: toggle every 500 ms */
        if (now - prevTick >= 500)
        {
            prevTick = now;
            BSP_LED1_Toggle();
        }
    }
}
