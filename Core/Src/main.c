/**
  * @file    main.c
  * @brief   Main program — LED heartbeat
  */

#include "main.h"
#include "bsp_tick.h"
#include "bsp_led.h"
#include "bsp_uart.h"
#include "bsp_i2c1.h"
#include "bsp_i2c3.h"
#include "bsp_pmic.h"
#include "app_protocol.h"

#define HEARTBEAT_INTERVAL_MS   100U

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

    /* I2C1 (motor IC) */
    if (BSP_I2C1_Init() != SUCCESS)
    {
        while (1) { ; }   /* Bus stuck — halt for debug */
    }

    /* I2C3 (PMIC) */
    if (BSP_I2C3_Init() != SUCCESS)
    {
        while (1) { ; }   /* Bus stuck — halt for debug */
    }

    /* PMIC RT5112WSC: power sequencing */
    if (BSP_PMIC_Init() != SUCCESS)
    {
        while (1) { ; }   /* PMIC init failed — halt for debug */
    }

    /* HWEN interrupt — enable after PMIC init to avoid spurious re-init */
    BSP_PMIC_HwenInit();

    /* App */
    App_Protocol_Init();

    uint32_t prevTick = BSP_GetTick();

    while (1)
    {
        uint32_t now = BSP_GetTick();

        App_Protocol_Poll();

        /* PMIC re-init on HWEN rising edge */
        if (g_pmicHwenFlag != 0U)
        {
            g_pmicHwenFlag = 0U;
            (void)BSP_PMIC_Init();
        }

        /* Heartbeat LED1: toggle every HEARTBEAT_INTERVAL_MS */
        if (now - prevTick >= HEARTBEAT_INTERVAL_MS)
        {
            prevTick = now;
            BSP_LED1_Toggle();
        }
    }
}
