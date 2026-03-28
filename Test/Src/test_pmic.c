/**
  * @file    test_pmic.c
  * @brief   PMIC 测试：每 100 ms 读一个寄存器，依次轮询 0x00~0x14，通过 UART 上报。
  *
  * 输出格式（ASCII）：
  *   "PMIC[XX]=YY\r\n"   ← I2C 成功，XX=寄存器地址，YY=寄存器值
  *   "PMIC[XX]=ERR\r\n"  ← I2C 失败
  */

#include "test_config.h"

#if TEST_PMIC_PID_READ

#include "test_pmic.h"
#include "bsp_pmic.h"
#include "bsp_i2c3.h"
#include "bsp_uart.h"

#define TEST_PMIC_INTERVAL_MS   100U
#define TEST_PMIC_REG_FIRST     0x00U
#define TEST_PMIC_REG_LAST      0x14U

/* 十六进制字符表 */
static const uint8_t k_hex[] = "0123456789ABCDEF";

/**
  * @brief  每 TEST_PMIC_INTERVAL_MS 读取一个 PMIC 寄存器并通过 UART 上报，
  *         从 0x00 到 0x14 依次轮询，到末尾后重新从头开始。
  * @param  tick  当前系统毫秒计数（BSP_GetTick 返回值）
  */
void Test_PMIC_PidRead_Poll(uint32_t tick)
{
    static uint32_t s_lastTick = 0U;
    static uint8_t  s_curReg   = TEST_PMIC_REG_FIRST;

    if (tick - s_lastTick < TEST_PMIC_INTERVAL_MS)
    {
        return;
    }
    s_lastTick = tick;

    uint8_t val;
    uint8_t buf[15];

    /* "PMIC[XX]=" */
    buf[0] = 'P'; buf[1] = 'M'; buf[2] = 'I'; buf[3] = 'C';
    buf[4] = '[';
    buf[5] = k_hex[(s_curReg >> 4) & 0x0FU];
    buf[6] = k_hex[s_curReg & 0x0FU];
    buf[7] = ']'; buf[8] = '=';

    if (BSP_I2C3_ReadReg(BSP_PMIC_I2C_ADDR, s_curReg, &val, 1U) == SUCCESS)
    {
        /* "YY\r\n" — 共 13 字节 */
        buf[9]  = k_hex[(val >> 4) & 0x0FU];
        buf[10] = k_hex[val & 0x0FU];
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

    /* 推进到下一个寄存器，到末尾后循环 */
    if (s_curReg >= TEST_PMIC_REG_LAST)
    {
        s_curReg = TEST_PMIC_REG_FIRST;
    }
    else
    {
        s_curReg++;
    }
}

#endif /* TEST_PMIC_PID_READ */
