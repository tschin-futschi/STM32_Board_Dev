/*
 * ============================================================================
 *  本地修改记录（供应商原始文件 → 本项目集成版的改动，按时间顺序自上而下追加）
 * ----------------------------------------------------------------------------
 *  2026-05-19  AW 自定义类型 AW_U8 / AW_U32 → 标准 C 类型 uint8_t / uint32_t；
 *              显式 #include <stdint.h>
 *  2026-05-19  引入回调架构（BSP 注入实现）：
 *              + 错误码 ISP_NOT_INITED；+ 回调表 aw_isp_ops_t；+ 注册 API aw_isp_init()；
 *              aw_i2c_write/read 签名扩展为 (DevId, AddrSize, pAddr, Size, Data) 并返回 int；
 *              delay_ms / aw_i2c_write / aw_i2c_read 下沉为 isp.c 内部 static 转发器，
 *              从公开 .h 移除（aw_i2c_stop_uboot 仍保留 extern）
 *  2026-05-19  vendor 宏 AW_FLASH_BASE / AW_FLASH_SIZE / AW_FLASH_TOP 加 AW_ 前缀，
 *              避免与 STM32 CMSIS stm32f4xx.h 中 AW_FLASH_BASE (0x08000000) 重名冲突
 *  2026-05-21  aw_reset_chip 函数声明加 NOTE 注释，说明实际是 wake-out-of-uboot
 *              （非硬件 reset），沿用 vendor 命名以保持 API 兼容
 * ============================================================================
 */

/*
* Copyright © Shanghai Awinic Technology Co., Ltd. 2019 2020 . All rights reserved.
* Description: IIC header file, IIC related parameter definition file
*/
#ifndef __AW_UBOOT_REGISTER_H
#define __AW_UBOOT_REGISTER_H

#define AW_FLASH_BASE		(0x01000000)
#define AW_FLASH_SIZE		(0x20000)
#define AW_FLASH_TOP		(AW_FLASH_BASE + AW_FLASH_SIZE)

#include <stdint.h>

#include "aw_uboot_bufbuilt.h"
#include "main.h"

enum isp_status{
	ISP_OK = 0,
	ISP_ADDR_ERROR,
	ISP_PBUF_ERROR,
	ISP_HANK_ERROR,
	ISP_JUMP_ERROR,
	ISP_FLASH_ERROR,
	ISP_SPACE_ERROR,
	ISP_NOT_INITED,
};
typedef enum isp_status ISP_STATUS_E;

ISP_STATUS_E aw_boot_control();
/* NOTE: 实为 wake-out-of-uboot 序列，非硬件 reset。沿用 vendor 命名。 */
ISP_STATUS_E aw_reset_chip();
ISP_STATUS_E aw_i2c_isp_download(uint32_t addr, uint8_t *bin_buf, uint32_t len);
ISP_STATUS_E aw_flash_download_check(uint32_t addr, uint8_t *bin_buf, uint32_t len);
ISP_STATUS_E aw_flash_block_erase_check(uint32_t addr, uint32_t len);
ISP_STATUS_E aw_flash_block_write_ckeck(uint32_t addr, uint32_t block_num, uint8_t *bin_buf, uint32_t len);
ISP_STATUS_E aw_hank_connect_check(void);
ISP_STATUS_E aw_flash_jump_check(uint32_t addr);
ISP_STATUS_E aw_flash_read(uint32_t addr, uint32_t len, uint8_t* dataBuff);
ISP_STATUS_E aw_flash_pack_read(uint32_t u32Addr, uint32_t u32PackNum, uint8_t* byDataBuff, uint32_t u32ReadLen);

void aw_i2c_stop_uboot(void);

/*--------------------------------------------------------------------------*/
/*  Callback architecture — BSP-facing                                       */
/*--------------------------------------------------------------------------*/

typedef void (*aw_delay_ms_fn_t)(uint32_t xms);
typedef int  (*aw_i2c_write_fn_t)(uint8_t DevId, uint8_t AddrSize, uint8_t *pAddr,
                                   uint8_t WrSize, uint8_t *WrData);
typedef int  (*aw_i2c_read_fn_t) (uint8_t DevId, uint8_t AddrSize, uint8_t *pAddr,
                                   uint8_t RdSize, uint8_t *pRdBuf);

typedef struct {
    aw_delay_ms_fn_t  delay_ms;
    aw_i2c_write_fn_t i2c_write;
    aw_i2c_read_fn_t  i2c_read;
} aw_isp_ops_t;

/* 注册 BSP 提供的回调实现。必须在调任何 aw_isp_* / aw_flash_* / aw_boot_control
 * 等 API 之前调用一次；传 NULL 或 ops 中任一成员为 NULL 时返回 ISP_NOT_INITED。 */
ISP_STATUS_E aw_isp_init(const aw_isp_ops_t *ops);

#endif
