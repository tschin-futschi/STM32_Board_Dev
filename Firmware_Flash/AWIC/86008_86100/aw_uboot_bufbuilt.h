/*
* Copyright © Shanghai Awinic Technology Co., Ltd. 2019 2020 . All rights reserved.
* Description: IIC header file, IIC related parameter definition file
*/
#ifndef __AW_UBOOT_BUFBUILT_H
#define __AW_UBOOT_BUFBUILT_H

enum uboot_status{
	UBOOT_OK = 0,
	UBOOT_ERROR,
};
typedef enum uboot_status UBOOT_STATUS_E;

enum uboot_way{
	UBOOT_I2C = 0,
	UBOOT_SPI,
};
typedef enum uboot_way UBOOT_WAY_E;

enum uboot_ack{
	UBOOT_CTL = 0,
	UBOOT_ACK,
};
typedef enum uboot_ack UBOOT_ACK_E;


enum uboot_module{
	UBOOT_HANK  = 0x01,
	UBOOT_SRAM   =0x02,
	UBOOT_FLASH = 0x03,
	UBOOT_END   = 0x04,
};
typedef enum uboot_module UBOOT_MODULE_E;

enum hank_event{
	HANK_CONNECT        = 0x01,
	HANK_CONNECT_ACK    = 0x02,
	HANK_PROTICOL       = 0x11,
	HANK_PROTICOL_ACK   = 0x12,
	HANK_VERSION        = 0x21,
	HANK_VERSION_ACK    = 0x22,
	HANK_ID             = 0x31,
	HANK_ID_ACK         = 0x32,
	HANK_DATE           = 0x33,
	HANK_DATA_ACK       = 0x34,
};
typedef enum hank_event HANK_EVENT_E;

enum sram_event{
	SRAM_WRITE          = 0x01,
	SRAM_WRITE_ACK      = 0x02,
	SRAM_READ           = 0x11,
	SRAM_READ_ACK       = 0x12,
};
typedef enum sram_event SRAM_EVENT_E;

enum flash_event{
	FLASH_WRITE           = 0x01,
	FLASH_WRITE_ACK       = 0x02,
	FLASH_READ            = 0x11,
	FLASH_READ_ACK        = 0x12,
	FLASH_ERASE_BLOCK     = 0x21,
	FLASH_ERASE_BLOCK_ACK = 0x22,
	FLASH_ERASE_CHIP      = 0x23,
	FLASH_ERASE_CHIP_ACK  = 0x24,
};
typedef enum flash_event FLASH_EVENT_E;



enum end_event{
	END_SRAM_JUMP           = 0x01,
	END_SRAM_JUMP_ACK       = 0x02,
	END_FLASH_JUMP          = 0x11,
	END_FLASH_JUMP_ACK      = 0x12,
	END_ERASE_ROM_JUMP      = 0x21,
	END_ERASE_ROM_JUMP_ACK  = 0x22,
};
typedef enum end_event END_EVENT_E;

struct uboot_protocol{
	unsigned char  checksum;
	unsigned char  protocol_ver;
	unsigned char  addr;
	unsigned char  module;
	unsigned char  event;
	unsigned char  len[2];
	unsigned char  ack;
	unsigned char  sum;
	unsigned char  reserved[3];
};
typedef struct uboot_protocol UBOOT_PROTOCOL_TYPE_S;

struct uboot_buf{
	unsigned char uboot_ways;		//UBOOT通信方式I2C或SPI
	unsigned char uboot_module;		//UBOOT模块Module
	unsigned char uboot_event;		//UBOOT事件Event
	unsigned char uboot_ack;		//UBOOT发送还是应答帧
	unsigned char uboot_data_len;	//数据层字节长度
	unsigned char reserved[3];
	unsigned char *p_uboot_data;	//数据层buff指针

};
typedef struct uboot_buf UBOOT_BUF_TYPE_S;


#define IS_UBOOT_WAYS(WAYS)				((WAYS ==UBOOT_I2C) || (WAYS ==UBOOT_SPI))

#define IS_UBOOT_ACK(ACK)				((ACK ==UBOOT_CTL) || (ACK == UBOOT_ACK))

#define IS_HANK_MODULE(MODULE)			((MODULE ==UBOOT_HANK) || (MODULE ==UBOOT_SRAM) || \
										 (MODULE ==UBOOT_FLASH) || (MODULE ==UBOOT_END))

#define IS_HANK_EVENT(EVENT)			((EVENT ==HANK_CONNECT) || (EVENT ==HANK_PROTICOL) || \
										 (EVENT ==HANK_VERSION) || (EVENT ==HANK_ID) || (EVENT ==HANK_DATE))

#define IS_SRAM_EVENT(EVENT)			((EVENT ==SRAM_WRITE) || (EVENT ==SRAM_READ))

#define IS_FLASH_EVENT(EVENT)			((EVENT ==FLASH_WRITE) || (EVENT ==FLASH_READ) || \
										 (EVENT ==FLASH_ERASE_BLOCK) || (EVENT ==FLASH_ERASE_CHIP))

#define IS_END_EVENT(EVENT)				((EVENT ==END_SRAM_JUMP) || (EVENT ==END_FLASH_JUMP) || \
										 (EVENT ==END_ERASE_ROM_JUMP))

UBOOT_STATUS_E aw_uboot_buff_built(unsigned char* buf, UBOOT_BUF_TYPE_S *uboot_struct);
unsigned char protocol_sum(void);
unsigned char data_sum(UBOOT_BUF_TYPE_S *uboot_struct);

#define assert_param(expr) ((void)0)

#endif
