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
#include "app_flashstore.h"

#include "aw_port_stm32.h"

#include "test_config.h"
#if TEST_PMIC_PID_READ
#include "test_pmic.h"
#endif
#if TEST_I2C_SCAN
#include "test_i2c_scan.h"
#endif

#define HEARTBEAT_INTERVAL_MS         500U
#define HALT_BLINK_INTERVAL_MS        500U   /* 启动失败统一闪烁频率（与心跳同频，诊断走串口 0x0B 帧） */
#define HALT_BLINK_NOUART_MS          100U   /* UART 自身故障：快闪，区别于 500ms，现场无串口可辨识 */

/**
  * @brief  启动失败时：先经串口发 0x0B BOOT_STATUS 帧告知 PC 具体模块，
  *         等 TX DMA 完成，再进入 LED 闪烁死循环。
  *         闪烁频率统一（500 ms），LED 仅作"卡死指示"，模块识别走串口。
  */
static void HaltOnInitFail(Proto_BootStatus_t status)
{
    App_Protocol_SendBootStatus(status);
    BSP_UART_TxWait();
    while (1)
    {
        uint32_t t0 = BSP_GetTick();
        while ((BSP_GetTick() - t0) < HALT_BLINK_INTERVAL_MS) { ; }
        BSP_LED1_Toggle();
    }
}

/**
  * @brief  UART 自身初始化失败时的死循环：诊断通道（串口）不可用，
  *         不能走 HaltOnInitFail，改用快闪 LED（HALT_BLINK_NOUART_MS）
  *         作可见指示，使现场能与"其它模块故障（500ms 慢闪 + 串口帧）"区分。
  */
static void HaltNoUart(void)
{
    while (1)
    {
        uint32_t t0 = BSP_GetTick();
        while ((BSP_GetTick() - t0) < HALT_BLINK_NOUART_MS) { ; }
        BSP_LED1_Toggle();
    }
}

int main(void)
{
    /* NVIC priority group — must be first */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);

    /* SysTick 1 ms */
    BSP_Tick_Init();

    /* LED */
    BSP_LED_Init();

    /* UART — 诊断上报的唯一通道；自身失败时只能用快闪 LED 指示 */
    if (BSP_UART_Init() != SUCCESS)
    {
        HaltNoUart();
    }

    /* I2C1 (INA power/current measurement) */
    if (BSP_I2C1_Init() != SUCCESS)
    {
        HaltOnInitFail(PROTO_INIT_FAIL_I2C1);
    }

    /* I2C3 (PMIC) — must run BEFORE I2C2 so PMIC can power up motor IC pull-ups */
    if (BSP_I2C3_Init() != SUCCESS)
    {
        HaltOnInitFail(PROTO_INIT_FAIL_I2C3);
    }

    /* PMIC RT5112WSC: pre-set voltages (LDOs not yet enabled) */
    if (BSP_PMIC_Init() != SUCCESS)
    {
        HaltOnInitFail(PROTO_INIT_FAIL_PMIC);
    }

    /* PMIC: enable LDO outputs so IOVDD powers up motor IC SDA/SCL pull-ups */
    if (BSP_PMIC_EnableSequence() != SUCCESS)
    {
        HaltOnInitFail(PROTO_INIT_FAIL_PMIC_EN);
    }

    /* HWEN interrupt — arm after PMIC fully up to avoid spurious re-init */
    BSP_PMIC_HwenInit();

    /* I2C2 (motor IC) — after PMIC LDOs so IOVDD has powered up SDA/SCL pull-ups */
    if (BSP_I2C2_Init() != SUCCESS)
    {
        HaltOnInitFail(PROTO_INIT_FAIL_I2C2);
    }

    /* AW ISP callback registration — must be after BSP I2C2 + Tick are up */
    if (aw_isp_init(&g_awOpsStm32) != ISP_OK)
    {
        HaltOnInitFail(PROTO_INIT_FAIL_AWISP);
    }

    /* App */
    App_Protocol_Init();
    App_Sample_Init();
    /* FLASH file storage: read metadata, init CRC table, clear session state.
     * Return value (FS_OK / FS_EMPTY / FS_CORRUPT) is informational only —
     * not a boot-fatal condition. PC discovers state via 0x3C READ_BEGIN. */
    (void)App_FlashStore_Init();

#if TEST_I2C_SCAN
    /* 等待 USB-UART 桥就绪，避免上电时输出丢失 */
    {
        uint32_t t0 = BSP_GetTick();
        while (BSP_GetTick() - t0 < 1000U) {}
    }
    Test_I2C_Scan_Run();
#endif

    /* 全部初始化通过，告知 PC 系统就绪 */
    App_Protocol_SendBootStatus(PROTO_BOOT_OK);

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
