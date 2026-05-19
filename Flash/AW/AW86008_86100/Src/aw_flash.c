/**
  * @file    aw_flash.c
  * @brief   AW86008 / AW86100 烧录顶层 API 实现
  */

#include "aw_flash.h"

ISP_STATUS_E AW_86008_86100_Flash_Run(uint32_t addr, uint8_t *bin_buf, uint32_t len)
{
	ISP_STATUS_E ret;

	/* Step 1: 进入 UBOOT 模式 + stay-in-uboot */
	ret = aw_boot_control();
	if (ret != ISP_OK) {
		return ret;
	}

	/* Step 2: 写 Flash + 校验 */
	ret = aw_i2c_isp_download(addr, bin_buf, len);
	if (ret != ISP_OK) {
		return ret;
	}

	/* Step 3: 复位芯片跳出 UBOOT 运行新固件 */
	ret = aw_reset_chip();
	if (ret != ISP_OK) {
		return ret;
	}

	return ISP_OK;
}
