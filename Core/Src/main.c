/**
  * @file    main.c
  * @brief   Main program — LED heartbeat
  */

#include "main.h"
#include "bsp_tick.h"
#include "bsp_led.h"
#include "bsp_uart.h"
#include "app_protocol.h"

#define HEARTBEAT_INTERVAL_MS   500U

int main(void)
{
    /* NVIC priority group — must be first */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);

    /* SysTick 1 ms */
    BSP_Tick_Init();

    /* LED */
    BSP_LED_Init();

    /* UART */
    BSP_UART_Init();

    /* App */
    App_Protocol_Init();

    uint32_t prevTick = BSP_GetTick();

    while (1)
    {
        uint32_t now = BSP_GetTick();

        App_Protocol_Poll();

        /* Heartbeat LED1: toggle every HEARTBEAT_INTERVAL_MS */
        if (now - prevTick >= HEARTBEAT_INTERVAL_MS)
        {
            prevTick = now;
            BSP_LED1_Toggle();
        }
    }
}
