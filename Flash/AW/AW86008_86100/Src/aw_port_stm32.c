/**
  * @file    aw_port_stm32.c
  * @brief   AW ISP 回调实现层 — 把 aw_uboot_isp.c 的 callback 接到本项目 BSP
  *
  * 翻译关系：
  *   aw_delay_ms_fn_t    → BSP_GetTick() 忙等
  *   aw_i2c_write_fn_t   → BSP_I2C2_TransparentWrite()
  *   aw_i2c_read_fn_t    → BSP_I2C2_TransparentRead()
  *   aw_on_progress_fn_t → App_Protocol_SendFlashExecProgress() （协议 0x38）
  *
  * Signature mismatch 翻译细节：
  *   - 参数顺序：AW (DevId, AddrSize, pAddr, Size, Data) ↔ BSP (devAddr, pAddr, addrSize, pData, dataLen)
  *   - 返回类型：AW int (0=OK) ↔ BSP ErrorStatus (SUCCESS / ERROR)
  *   - 长度类型：AW uint8_t ↔ BSP uint16_t (隐式提升，无信息损失)
  */

#include "aw_port_stm32.h"
#include "app_protocol.h"
#include "bsp_i2c2.h"
#include "bsp_tick.h"

/*--------------------------------------------------------------------------*/
/*  Port functions — 把 AW 回调签名翻译成 BSP 签名                            */
/*--------------------------------------------------------------------------*/

static void AwPort_DelayMs(uint32_t xms)
{
	uint32_t start = BSP_GetTick();
	while ((BSP_GetTick() - start) < xms) {
		/* busy-wait */
	}
}

static int AwPort_I2cWrite(uint8_t DevId, uint8_t AddrSize, uint8_t *pAddr,
                            uint8_t WrSize, uint8_t *WrData)
{
	ErrorStatus s = BSP_I2C2_TransparentWrite(DevId, pAddr, AddrSize, WrData, WrSize);
	return (s == SUCCESS) ? 0 : -1;
}

static int AwPort_I2cRead(uint8_t DevId, uint8_t AddrSize, uint8_t *pAddr,
                           uint8_t RdSize, uint8_t *pRdBuf)
{
	ErrorStatus s = BSP_I2C2_TransparentRead(DevId, pAddr, AddrSize, pRdBuf, RdSize);
	return (s == SUCCESS) ? 0 : -1;
}

/* 2026-05-24 EXEC 进度上报：组 9 字节载荷 + 发 0x38 主动帧。
 * SendFrameWithRetry 内部已自带 BSP_UART_TxWait，调用方无需补等待。
 * 中断 / DMA TX 在 ISP 主循环阻塞期间仍正常工作（R3 验证结论）。 */
static void AwPort_OnFlashProgress(uint8_t phase, uint32_t done, uint32_t total)
{
	App_Protocol_SendFlashExecProgress(phase, done, total);
}

/*--------------------------------------------------------------------------*/
/*  Public ops table — 由 main.c 通过 aw_isp_init(&g_awOpsStm32) 注册        */
/*--------------------------------------------------------------------------*/

const aw_isp_ops_t g_awOpsStm32 = {
	.delay_ms    = AwPort_DelayMs,
	.i2c_write   = AwPort_I2cWrite,
	.i2c_read    = AwPort_I2cRead,
	.on_progress = AwPort_OnFlashProgress,
};
