/*
* Copyright © Shanghai Awinic Technology Co., Ltd. 2019 2020 . All rights reserved.
* Description: IIC header file, IIC related parameter definition file
*/
#ifndef __AW_UBOOT_REGISTER_H
#define __AW_UBOOT_REGISTER_H

#define FLASH_BASE		(0x01000000)
#define FLASH_SIZE		(0x20000)
#define FLASH_TOP		(FLASH_BASE + FLASH_SIZE)

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
};
typedef enum isp_status ISP_STATUS_E;

ISP_STATUS_E aw_boot_control();
ISP_STATUS_E aw_reset_chip();
ISP_STATUS_E aw_i2c_isp_download(AW_U32 addr, AW_U8 *bin_buf, AW_U32 len);
ISP_STATUS_E aw_flash_download_check(AW_U32 addr, AW_U8 *bin_buf, AW_U32 len);
ISP_STATUS_E aw_flash_block_erase_check(AW_U32 addr, AW_U32 len);
ISP_STATUS_E aw_flash_block_write_ckeck(AW_U32 addr, AW_U32 block_num, AW_U8 *bin_buf, AW_U32 len);
ISP_STATUS_E aw_hank_connect_check(void);
ISP_STATUS_E aw_flash_jump_check(AW_U32 addr);
ISP_STATUS_E aw_flash_read(AW_U32 addr, AW_U32 len, AW_U8* dataBuff);
ISP_STATUS_E aw_flash_pack_read(AW_U32 u32Addr, AW_U32 u32PackNum, AW_U8* byDataBuff, AW_U32 u32ReadLen);

void delay_ms(AW_U32 xms);
void aw_i2c_stop_uboot(void);
void aw_i2c_write(AW_U8 *buff, AW_U8 len);
void aw_i2c_read (AW_U8 *buff, AW_U8 len);

#endif
