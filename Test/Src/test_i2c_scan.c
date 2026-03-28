/**
  * @file    test_i2c_scan.c
  * @brief   I2C 总线扫描测试：扫描 I2C1 / I2C2 / I2C3，结果通过 UART 上报。
  *
  * 输出格式（每路一行，ASCII）：
  *   "I2C1: 0xXX 0xXX ...\r\n"   ← 找到的设备地址（十六进制）
  *   "I2C1: none\r\n"             ← 未找到任何设备
  */

#include "test_config.h"

#if TEST_I2C_SCAN

#include "test_i2c_scan.h"
#include "bsp_i2c1.h"
#include "bsp_i2c2.h"
#include "bsp_i2c3.h"
#include "bsp_uart.h"
#include "bsp_tick.h"

#define SCAN_ADDR_BUF_SIZE   127U
#define SCAN_TX_BUF_SIZE     64U    /* label(4) + ": "(2) + 11×"0xXX "(55) + "\r\n"(2) */
#define SCAN_TX_TIMEOUT_MS   50U    /* 等待上次 DMA 发送完成的最长时间 */

/* 十六进制字符表 */
static const uint8_t k_hex[] = "0123456789ABCDEF";

/**
  * @brief  等待 UART DMA 发送完成（tick 延时）。
  *         115200 baud，64 字节最长约 5.6 ms，等 SCAN_TX_TIMEOUT_MS 足够。
  */
static void WaitTxReady(void)
{
    uint32_t t0 = BSP_GetTick();
    while (BSP_GetTick() - t0 < SCAN_TX_TIMEOUT_MS) { ; }
}

/**
  * @brief  将单路 I2C 扫描结果格式化为一行并通过 UART 上报（单次发送）。
  */
static void ReportResult(const uint8_t *pLabel, uint8_t labelLen,
                         const uint8_t *pAddrs, uint8_t count)
{
    uint8_t buf[SCAN_TX_BUF_SIZE];
    uint8_t pos = 0U;
    uint8_t i;

    /* "I2Cx: " */
    for (i = 0U; i < labelLen; i++)
    {
        buf[pos++] = pLabel[i];
    }
    buf[pos++] = ':';
    buf[pos++] = ' ';

    if (count == 0U)
    {
        buf[pos++] = 'n'; buf[pos++] = 'o';
        buf[pos++] = 'n'; buf[pos++] = 'e';
    }
    else
    {
        for (i = 0U; i < count; i++)
        {
            if (pos + 5U >= SCAN_TX_BUF_SIZE) { break; }   /* 防溢出 */
            buf[pos++] = '0';
            buf[pos++] = 'x';
            buf[pos++] = k_hex[(pAddrs[i] >> 4) & 0x0FU];
            buf[pos++] = k_hex[pAddrs[i] & 0x0FU];
            buf[pos++] = ' ';
        }
    }

    buf[pos++] = '\r';
    buf[pos++] = '\n';

    /* 等待上次发送完成，再发本行 */
    WaitTxReady();
    (void)BSP_UART_Transmit(buf, pos);
}

/**
  * @brief  扫描 I2C1 / I2C2 / I2C3，结果通过 UART 上报。
  *         在初始化完成后调用一次即可。
  */
void Test_I2C_Scan_Run(void)
{
    uint8_t addrBuf[SCAN_ADDR_BUF_SIZE];
    uint8_t count;

    /* I2C1 — INA */
    count = 0U;
    (void)BSP_I2C1_Scan(addrBuf, &count);
    ReportResult((const uint8_t *)"I2C1", 4U, addrBuf, count);

    /* I2C2 — motor IC */
    count = 0U;
    (void)BSP_I2C2_Scan(addrBuf, &count);
    ReportResult((const uint8_t *)"I2C2", 4U, addrBuf, count);

    /* I2C3 — PMIC */
    count = 0U;
    (void)BSP_I2C3_Scan(addrBuf, &count);
    ReportResult((const uint8_t *)"I2C3", 4U, addrBuf, count);
}

#endif /* TEST_I2C_SCAN */
