/*
* Copyright © Shanghai Awinic Technology Co., Ltd. 2019 2019 . All rights reserved.
* Description: spi correlation function
*/
#include "aw_uboot_isp.h"
#include "string.h"

void delay_ms(AW_U32 xms)
{
}

//stay in UBOOT state (stop UBOOT)
//It needs to be sent multiple times within 8ms when power on
void aw_i2c_stop_uboot(void)
{
	AW_U8 tmp = 0xAC;
	aw_i2c_write(&tmp, 1);
}

//The underlying interface
void aw_i2c_write(AW_U8 *buff, AW_U8 len)
{
}


void aw_i2c_read(AW_U8 *buff, AW_U8 len)
{
}

ISP_STATUS_E aw_reset_chip()
{
	// I2CSlaveAddr, Write, Data,
	0x6b, w, 0xff, 0xa3, 0x02,
	// Delay 2 ms
	delay_ms(2)
	0x6b, w, 0xff, 0xb7, 0x5a,
	// Delay 2 ms
	delay_ms(2)
	0x6b, w, 0xff, 0xb7, 0x00,
	// Delay 15 ms
	delay_ms(15)
	0x6b, w, 0xff, 0xf0, 0x20, 0x20, 0x02, 0x02, 0x19, 0x29, 0x19, 0x29,
	0x6b, r, 0xff, 0xf0 = 0x01,
	// Delay 2 ms
	delay_ms(2)
	0x6b, w, 0xff, 0xb4, 0x00,
	// Delay 2 ms
	delay_ms(2)
	0x6b, w, 0xff, 0xa3, 0x00,
	// Delay 2 ms
	delay_ms(2)
	0x6b, w, 0xff, 0xb4, 0x01,

	delay_ms(15)
}

ISP_STATUS_E aw_boot_control()
{
	// I2CSlaveAddr, Write, Data,
	0x6b, w, 0xff, 0xa3, 0x02,
	// Delay 2 ms
	delay_ms(2)
	0x6b, w, 0xff, 0xb7, 0x5a,
	// Delay 2 ms
	delay_ms(2)
	0x6b, w, 0xff, 0xb7, 0x00,
	// Delay 15 ms
	delay_ms(15)
	0x6b, w, 0xff, 0xf0, 0x20, 0x20, 0x02, 0x02, 0x19, 0x29, 0x19, 0x29,
	0x6b, r, 0xff, 0xf0 = 0x01,
	// Delay 2 ms
	delay_ms(2)
	0x6b, w, 0xff, 0xb4, 0x00,
	// Delay 2 ms
	delay_ms(2)
	0x6b, w, 0xff, 0xa3, 0x00,
	// Delay 2 ms
	delay_ms(2)
	0x6b, w, 0xff, 0xb4, 0x01,
	// Delay 7 ms
	delay_ms(7)
	// I2CSlaveAddr, Write, Data,
	// 20 times
	0x6f, w, 0xac,
	0x6f, w, 0xac,
	0x6f, w, 0xac,
	0x6f, w, 0xac,
	0x6f, w, 0xac,
	0x6f, w, 0xac,
	0x6f, w, 0xac,
	0x6f, w, 0xac,
	0x6f, w, 0xac,
	0x6f, w, 0xac,
	0x6f, w, 0xac,
	0x6f, w, 0xac,
	0x6f, w, 0xac,
	0x6f, w, 0xac,
	0x6f, w, 0xac,
	0x6f, w, 0xac,
	0x6f, w, 0xac,
	0x6f, w, 0xac,
	0x6f, w, 0xac,
	0x6f, w, 0xac,
}

//len is the length of the word(4Byte)
ISP_STATUS_E aw_i2c_isp_download(AW_U32 addr, AW_U8 *bin_buf, AW_U32 len)
{
	ISP_STATUS_E ret = ISP_OK;
	AW_U32 space = 0;


	//bin_buff pointer check
	if (bin_buf == (void *)0) {
		return ISP_PBUF_ERROR;
	}

	//isp download address check
	if ((addr < FLASH_BASE) || (addr >= FLASH_TOP)) {
		return ISP_ADDR_ERROR;
	}

	//isp download space check
	space = FLASH_TOP - addr;
	if (space < len * 4) {
		return ISP_SPACE_ERROR;
	}

//Send a handshake frame to confirm that the chip works properly
	if (aw_hank_connect_check() != ISP_OK) {
		return ISP_HANK_ERROR ;
	}

//Download the program to flash ISP
	if (aw_flash_download_check(addr, bin_buf, len) != ISP_OK) {
		return ISP_FLASH_ERROR ;
	}

//Jump to flash ISP
	if (aw_flash_jump_check(addr) != ISP_OK) {
		return ISP_JUMP_ERROR ;
	}
	return ret;
}

//len is the length of the word(4Byte)
ISP_STATUS_E aw_i2c_isp_upload(AW_U32 addr, AW_U8 *bin_buf, AW_U32 len)
{
	ISP_STATUS_E ret = ISP_OK;
	AW_U32 space = 0;


	//bin_buff pointer check
	if (bin_buf == (void *)0) {
		return ISP_PBUF_ERROR;
	}

	//isp download address check
	if ((addr < FLASH_BASE) || (addr >= FLASH_TOP)) {
		return ISP_ADDR_ERROR;
	}

	//isp download space check
	space = FLASH_TOP - addr;
	if (space < len * 4) {
		return ISP_SPACE_ERROR;
	}

//Send a handshake frame to confirm that the chip works properly
	if (aw_hank_connect_check() != ISP_OK) {
		return ISP_HANK_ERROR ;
	}

	if(aw_flash_read(addr, len, bin_buf)){
		return ISP_FLASH_ERROR ;
	}
}

ISP_STATUS_E aw_flash_jump_check(AW_U32 addr)
{
	/*ISP_STATUS_E ret = ISP_OK;
	AW_U8 i = 0;
	AW_U8 tmp = 0;
	AW_U8 l_buff[4]  = {0};
	AW_U8 w_buff[13] = {0};
	AW_U8 d_buff[1]  = {0};
	AW_U8 r_buff[10] = {0};

	for (i = 0; i < 4; i++) {
		l_buff[i] = (AW_U8)(addr >> (i * 8));
	}

	UBOOT_BUF_TYPE_S uboot_structure;
	uboot_structure.uboot_ways = UBOOT_I2C;
	uboot_structure.uboot_module = UBOOT_END;
	uboot_structure.uboot_event = END_FLASH_JUMP;
	uboot_structure.uboot_ack = UBOOT_CTL;
	uboot_structure.uboot_data_len = 4;
	uboot_structure.p_uboot_data = l_buff;
	aw_uboot_buff_built(w_buff, &uboot_structure);

	aw_i2c_write(w_buff, 13);

	delay_ms(10);

	aw_i2c_read(r_buff, 10);

	uboot_structure.uboot_event = END_FLASH_JUMP_ACK;
	uboot_structure.uboot_ack = UBOOT_ACK;
	uboot_structure.uboot_data_len = 1;
	uboot_structure.p_uboot_data = d_buff;
	aw_uboot_buff_built(w_buff, &uboot_structure);

	tmp = (AW_U8)memcmp(w_buff, r_buff, 10);
	if (tmp != 0) {
		ret = ISP_JUMP_ERROR;
		return ret;
	}
	return ret;*/
	return ISP_OK;
}

ISP_STATUS_E aw_flash_pack_read(AW_U32 u32Addr, AW_U32 u32PackNum, AW_U8* byDataBuff, AW_U32 u32ReadLen)
{
	ISP_STATUS_E ret = ISP_OK;
	AW_U8 l_buff[6] = { 0 };
	AW_U8 w_buff[78] = { 0 };
	AW_U8 r_buff[69] = { 0 };
	AW_U8 d_buff[69] = { 0 };
	AW_U32 i = 0;
	AW_U8 tmp = -1;

	for (i = 0; i < 2; i++) {
		l_buff[i] = (AW_U8)((u32ReadLen * 4) >> (i * 8));
	}
	for (i = 0; i < 4; i++) {
		l_buff[i + 2] = (AW_U8)((u32Addr + u32PackNum * 64) >> (i * 8));
	}

	UBOOT_BUF_TYPE_S uboot_struct;
	uboot_struct.uboot_ways = UBOOT_I2C;
	uboot_struct.uboot_module = UBOOT_FLASH;
	uboot_struct.uboot_event = FLASH_READ;
	uboot_struct.uboot_ack = UBOOT_CTL;
	uboot_struct.uboot_data_len = 6;
	uboot_struct.p_uboot_data = l_buff;
	aw_uboot_buff_built(w_buff, &uboot_struct);

	aw_i2c_write(w_buff, 15);

	delay_ms(1);

	aw_i2c_read(r_buff, u32ReadLen * 4 + 1 + 4 + 9);

	if (r_buff[9] == 0) {
		tmp = 0;
	}

	if (tmp != 0) {
		ret = ISP_FLASH_ERROR;
		return ret;
	}

	for (int i = 0; i < u32ReadLen * 4; i++) {
		byDataBuff[u32PackNum * 64 + i] = r_buff[14 + i];
	}

	return ret;
}

ISP_STATUS_E aw_flash_read(AW_U32 addr, AW_U32 len, AW_U8* dataBuff)
{
	ISP_STATUS_E ret = ISP_OK;
	AW_U32 divisor = 0;
	AW_U32 block_cnt = 0;
	AW_U8 remaind = 0;
	AW_U32 i = 0;

	if (dataBuff == (void*)0) {
		ret = ISP_PBUF_ERROR;
		return ret;
	}

	divisor = len / 16;
	remaind = len % 16;
	block_cnt = len / 1024 + ((len % 1024) ? 1 : 0);

	for (i = 0; i < divisor; i++) {
		if (aw_flash_pack_read(addr, i, dataBuff, 16) != ISP_OK) {
			ret = ISP_FLASH_ERROR;
			return ret;
		}
	}
	if (remaind != 0) {
		if (aw_flash_pack_read(addr, i, dataBuff, remaind) != ISP_OK) {
			ret = ISP_FLASH_ERROR;
			return ret;
		}
	}

	return ret;
}

ISP_STATUS_E aw_flash_block_erase_check(AW_U32 addr, AW_U32 len)
{
	ISP_STATUS_E ret = ISP_OK;
	AW_U8 i = 0;
	AW_U8 tmp = 0;
	AW_U8 l_buff[6]  = {0};
	AW_U8 w_buff[15] = {0};
	AW_U8 d_buff[1]  = {0};
	AW_U8 r_buff[10] = {0};

	l_buff[0] = (AW_U8)len;
	l_buff[1] = 0x00;

	for (i = 0; i < 4; i++) {
		l_buff[i + 2] = (AW_U8)(addr >> (i * 8));
	}

	UBOOT_BUF_TYPE_S uboot_structure;
	uboot_structure.uboot_ways = UBOOT_I2C;
	uboot_structure.uboot_module = UBOOT_FLASH;
	uboot_structure.uboot_event = FLASH_ERASE_BLOCK;
	uboot_structure.uboot_ack = UBOOT_CTL;
	uboot_structure.uboot_data_len = 6;
	uboot_structure.p_uboot_data = l_buff;
	aw_uboot_buff_built(w_buff, &uboot_structure);

	aw_i2c_write(w_buff, 15);

	delay_ms(25 * len);

	aw_i2c_read(r_buff, 10);

	uboot_structure.uboot_event = FLASH_ERASE_BLOCK_ACK;
	uboot_structure.uboot_ack = UBOOT_ACK;
	uboot_structure.uboot_data_len = 1;
	uboot_structure.p_uboot_data = d_buff;
	aw_uboot_buff_built(w_buff, &uboot_structure);

	tmp = (AW_U8)memcmp(w_buff, r_buff, 10);
	if (tmp != 0) {
		ret = ISP_FLASH_ERROR;
		return ret;
	}

	return ret;
}

ISP_STATUS_E aw_flash_block_write_ckeck(AW_U32 addr, AW_U32 block_num, AW_U8 *bin_buf, AW_U32 len)
{
	ISP_STATUS_E ret = ISP_OK;
	AW_U8 i = 0;
	AW_U8 tmp = 0;
	AW_U8 l_buff[68]  = {0};
	AW_U8 w_buff[77] = {0};
	AW_U8 d_buff[1]  = {0};
	AW_U8 r_buff[10] = {0};

	for (i = 0; i < 4; i++) {
		l_buff[i] = (AW_U8)((addr + block_num * 64) >> (i * 8));
	}
	for (i = 0; i < (len * 4); i++) {
		l_buff[i + 4] = (bin_buf + block_num * 64)[i];
	}

	UBOOT_BUF_TYPE_S uboot_structure;
	uboot_structure.uboot_ways = UBOOT_I2C;
	uboot_structure.uboot_module = UBOOT_FLASH;
	uboot_structure.uboot_event = FLASH_WRITE;
	uboot_structure.uboot_ack = UBOOT_CTL;
	uboot_structure.uboot_data_len = (AW_U8)(4 + 4 * len);
	uboot_structure.p_uboot_data = l_buff;
	aw_uboot_buff_built(w_buff, &uboot_structure);

	aw_i2c_write(w_buff, (AW_U8)(13 + 4 * len));

	delay_ms(1);

	aw_i2c_read(r_buff, 10);

	uboot_structure.uboot_event = FLASH_WRITE_ACK;
	uboot_structure.uboot_ack = UBOOT_ACK;
	uboot_structure.uboot_data_len = 1;
	uboot_structure.p_uboot_data = d_buff;
	aw_uboot_buff_built(w_buff, &uboot_structure);

	tmp = (AW_U8)memcmp(w_buff, r_buff, 10);
	if (tmp != 0) {
		ret = ISP_FLASH_ERROR;
		return ret;
	}

	return ret;
}


ISP_STATUS_E aw_flash_download_check(AW_U32 addr, AW_U8 *bin_buf, AW_U32 len)
{
	ISP_STATUS_E ret = ISP_OK;
	AW_U8 remaind = 0;
	AW_U32 block_cnt = 0;
	AW_U32 i = 0;
	AW_U32 divisor = 0;

	divisor = len / 16;
	remaind = len % 16;
	block_cnt = len / 1024 + ((len % 1024)? 1 : 0);

	//Erases sectors that need to be used
	if (aw_flash_block_erase_check(addr, block_cnt) != ISP_OK) {
		return ISP_FLASH_ERROR;
	}

	for (i = 0; i < divisor; i++) {
		//Data is written to the FLASH sector and checked
		if (aw_flash_block_write_ckeck(addr, i, bin_buf,16) != ISP_OK) {
			return ISP_FLASH_ERROR;
		}
	}
	if (remaind != 0) {
		//Data is written to the FLASH sector and checked
		if (aw_flash_block_write_ckeck(addr, i, bin_buf, remaind) != ISP_OK) {
			return ISP_FLASH_ERROR;
		}
	}

	return ret;
}

ISP_STATUS_E aw_hank_connect_check(void)
{
	ISP_STATUS_E ret = ISP_OK;
	AW_U8 tmp = 0;
	AW_U8 w_buff[255] = {0};
	AW_U8 r_buff[255] = {0};
	AW_U8 d_buff[5]   = {0x00, 0x01,0x00, 0x00, 0x00};

	UBOOT_BUF_TYPE_S uboot_structure;
	uboot_structure.uboot_ways = UBOOT_I2C;
	uboot_structure.uboot_ack = UBOOT_CTL;
	uboot_structure.uboot_module = UBOOT_HANK;
	uboot_structure.uboot_event = HANK_CONNECT;
	uboot_structure.uboot_data_len = 0;
	uboot_structure.p_uboot_data =  0;
	aw_uboot_buff_built(w_buff, &uboot_structure);

	aw_i2c_write(w_buff, 9);

	delay_ms(2);

	aw_i2c_read(r_buff, 14);

	uboot_structure.uboot_event = HANK_CONNECT_ACK;
	uboot_structure.uboot_ack = UBOOT_ACK;
	uboot_structure.uboot_data_len = 5;
	uboot_structure.p_uboot_data = d_buff;
	aw_uboot_buff_built(w_buff, &uboot_structure);

	tmp = (AW_U8)memcmp(w_buff, r_buff, 14);
	if (tmp != 0) {
		ret = ISP_HANK_ERROR;
	}

	return ret;
}
