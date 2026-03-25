/**
  * @file    main.c
  * @brief   Main program — LED heartbeat (diagnostic: software delay)
  */

#include "main.h"
#include "bsp_tick.h"
#include "bsp_led.h"

static void SoftDelay(volatile uint32_t count)
{
    while (count--) {}
}

int main(void)
{
    /* NVIC priority group — must be first */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);

    /* LED init only — no SysTick, no clock dependency */
    BSP_LED_Init();

    /* Diagnostic: pure software delay blink — proves GPIO works */
    while (1)
    {
        BSP_LED1_Toggle();
        SoftDelay(800000);
    }
}
