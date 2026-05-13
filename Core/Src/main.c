/**
  * @file    main.c
  * @brief   Main program — LED heartbeat
  */

#include "main.h"
#include "bsp_tick.h"
#include "bsp_led.h"
#include "bsp_uart.h"
#include "bsp_i2c1.h"
#include "bsp_i2c2.h"
#include "bsp_i2c3.h"
#include "bsp_pmic.h"
#include "app_protocol.h"
#include "app_sample.h"

#include "test_config.h"
#if TEST_PMIC_PID_READ
#include "test_pmic.h"
#endif
#if TEST_I2C_SCAN
#include "test_i2c_scan.h"
#endif

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

    /* I2C1 (INA power/current measurement) */
    if (BSP_I2C1_Init() != SUCCESS)
    {
        while (1) { ; }   /* Bus stuck — halt for debug */
    }

    /* I2C2 (motor IC) */
    if (BSP_I2C2_Init() != SUCCESS)
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
    App_Sample_Init();

#if TEST_I2C_SCAN
    /* 等待 USB-UART 桥就绪，避免上电时输出丢失 */
    {
        uint32_t t0 = BSP_GetTick();
        while (BSP_GetTick() - t0 < 1000U) {}
    }
    Test_I2C_Scan_Run();
#endif

    uint32_t prevTick = BSP_GetTick();

    while (1)
    {
        uint32_t now = BSP_GetTick();

        App_Protocol_Poll();
        App_Sample_Poll();

#if TEST_PMIC_PID_READ
        Test_PMIC_PidRead_Poll(now);
#endif

        /* PMIC re-init + enable on HWEN rising edge */
        if (g_pmicHwenFlag != 0U)
        {
            g_pmicHwenFlag = 0U;
            (void)BSP_PMIC_EnableSequence();
        }

        /* Heartbeat LED1: toggle every HEARTBEAT_INTERVAL_MS */
        if (now - prevTick >= HEARTBEAT_INTERVAL_MS)
        {
            prevTick = now;
            BSP_LED1_Toggle();
        }
    }
}
