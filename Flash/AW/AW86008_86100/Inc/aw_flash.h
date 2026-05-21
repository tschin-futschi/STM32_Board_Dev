/**
  * @file    aw_flash.h
  * @brief   AW86008 / AW86100 烧录顶层 API
  *
  * 给应用层（main / 协议解析层）调用的一站式烧录入口。
  * 内部按顺序执行：aw_boot_control() → aw_i2c_isp_download() → aw_reset_chip()。
  */

#ifndef __AW_FLASH_H
#define __AW_FLASH_H

#include "aw_uboot_isp.h"

/**
  * @brief  完整烧录一颗 AW86008 / AW86100：进入 UBOOT → 写 Flash → 复位运行。
  * @param  addr     目标 Flash 起始地址（须在 [AW_FLASH_BASE, AW_FLASH_TOP) 区间）
  * @param  bin_buf  固件数据缓冲区
  * @param  len      固件长度，单位为字 (word = 4 字节)，不是字节
  * @retval ISP_OK 全部成功；其它返回值为失败子流程的错误码（见 enum isp_status）
  * @note   调用前必须已 aw_isp_init(&g_awOpsStm32) 注册回调，否则返 ISP_NOT_INITED
  * @note   bin_buf 须 ≥ len*4 字节；本项目协议层用 64 KB staging buffer（s_fwBuf），
  *         单次 session 最大烧 64 KB；若需烧满 AW_FLASH_SIZE (128 KB) 分两次 session
  */
ISP_STATUS_E AW_86008_86100_Flash_Run(uint32_t addr, uint8_t *bin_buf, uint32_t len);

#endif /* __AW_FLASH_H */
