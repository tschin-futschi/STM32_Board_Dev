/**
  * @file    test_pmic.c
  * @brief   PMIC 测试：每 100 ms 读 Product ID，通过 UART 上报。
  *
  * 输出格式（ASCII，每行 14 字节）：
  *   "PMIC[0F]=XX\r\n"   ← I2C 成功，XX 为寄存器十六进制值
  *   "PMIC[0F]=ERR\r\n"  ← I2C 失败（15 字节）
  */

#include "test_config.h"

#if TEST_PMIC_PID_READ

#include "test_pmic.h"
#include "bsp_pmic.h"
#include "bsp_uart.h"

#define TEST_PMIC_INTERVAL_MS   100U

/* 十六进制字符表 */
static const uint8_t k_hex[] = "0123456789ABCDEF";

/**
  * @brief  每 TEST_PMIC_INTERVAL_MS 读取一次 PMIC Product ID 并通过 UART 上报。
  * @param  tick  当前系统毫秒计数（BSP_GetTick 返回值）
  */
void Test_PMIC_PidRead_Poll(uint32_t tick)
{
    static uint32_t s_lastTick = 0U;

    if (tick - s_lastTick < TEST_PMIC_INTERVAL_MS)
    {
        return;
    }
    s_lastTick = tick;

    uint8_t pid;
    uint8_t buf[15];

    /* "PMIC[0F]=" */
    buf[0] = 'P'; buf[1] = 'M'; buf[2] = 'I'; buf[3] = 'C';
    buf[4] = '[';
    buf[5] = k_hex[(BSP_PMIC_REG_PRODUCT_ID >> 4) & 0x0FU];
    buf[6] = k_hex[BSP_PMIC_REG_PRODUCT_ID & 0x0FU];
    buf[7] = ']'; buf[8] = '=';

    if (BSP_PMIC_ReadPid(&pid) == SUCCESS)
    {
        /* "XX\r\n" — 共 13 字节 */
        buf[9]  = k_hex[(pid >> 4) & 0x0FU];
        buf[10] = k_hex[pid & 0x0FU];
        buf[11] = '\r'; buf[12] = '\n';
        (void)BSP_UART_Transmit(buf, 13U);
    }
    else
    {
        /* "ERR\r\n" — 共 14 字节 */
        buf[9]  = 'E'; buf[10] = 'R'; buf[11] = 'R';
        buf[12] = '\r'; buf[13] = '\n';
        (void)BSP_UART_Transmit(buf, 14U);
    }
}

#endif /* TEST_PMIC_PID_READ */
