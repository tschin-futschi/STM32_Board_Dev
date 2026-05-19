/*
* Copyright © Shanghai Awinic Technology Co., Ltd. 2019 2019 . All rights reserved.
* Description: spi correlation function
*/
#include "aw_uboot_bufbuilt.h"


static UBOOT_PROTOCOL_TYPE_S protocol_struct = {0};

UBOOT_STATUS_E aw_uboot_buff_built(unsigned char* buf, UBOOT_BUF_TYPE_S *uboot_struct)
{
	unsigned char i = 0;
	UBOOT_STATUS_E ret = UBOOT_ERROR;
	unsigned char *p_protocol = (unsigned char*)&protocol_struct;

	if ((buf == (void *)0) || ((uboot_struct == (void *)0))) {
		return ret;
	}

	assert_param(IS_UBOOT_WAYS(uboot_struct->uboot_ways));
	assert_param(IS_UBOOT_ACK(uboot_struct->uboot_ack));
	assert_param(IS_HANK_MODULE(uboot_struct->uboot_module));

	switch(uboot_struct->uboot_module) {
		case UBOOT_HANK:
			assert_param(IS_HANK_EVENT(uboot_struct->uboot_event));
			break;
		case UBOOT_SRAM:
			assert_param(IS_SRAM_EVENT(uboot_struct->uboot_event));
			break;
		case UBOOT_FLASH:
			assert_param(IS_FLASH_EVENT(uboot_struct->uboot_event));
			break;
		case UBOOT_END:
			assert_param(IS_END_EVENT(uboot_struct->uboot_event));
			break;
		default:
			break;
	}

	if (uboot_struct->p_uboot_data == (void *)0) {
		uboot_struct->uboot_data_len = 0;
		protocol_struct.len[0] = 0;
	}

	protocol_struct.protocol_ver = 0x01;

	if (uboot_struct->uboot_ack == UBOOT_CTL) {
		protocol_struct.addr = 0x82;
	} else {
		protocol_struct.addr = 0x28;
	}
	protocol_struct.module = uboot_struct->uboot_module;
	protocol_struct.event = uboot_struct->uboot_event;
	protocol_struct.ack = 0x01;

	protocol_struct.len[0] = uboot_struct->uboot_data_len;
	protocol_struct.sum = data_sum(uboot_struct);
	protocol_struct.checksum = protocol_sum();


	if (uboot_struct->uboot_ways == UBOOT_I2C) {
		for(i = 0; i < 9; i++) {
			buf[i] = p_protocol[i];
		}

		for(i = 0; i < (protocol_struct.len[0]); i++) {
			buf[i + 9] = uboot_struct->p_uboot_data[i];
		}
	} else {
		buf[0] = 0x5A;
		for(i = 0; i < 9; i++) {
			buf[i + 1] = p_protocol[i];
		}

		for(i = 0; i < (protocol_struct.len[0]); i++) {
			buf[i + 10] = uboot_struct->p_uboot_data[i];
		}
	}

	ret = UBOOT_OK;
	return ret;
}


unsigned char protocol_sum(void)
{
	unsigned char sum = 0;
	unsigned char i = 0;
	unsigned char *p_protocol = (unsigned char*)&protocol_struct;

	for (i = 1; i < 9 ; i++) {
		sum += p_protocol[i];
	}
	return sum;
}

unsigned char data_sum(UBOOT_BUF_TYPE_S *uboot_struct)
{
	unsigned char sum = 0;
	unsigned char i = 0;

	for (i = 0; i < uboot_struct->uboot_data_len; i++) {
		sum += uboot_struct->p_uboot_data[i];
	}
	return sum;
}
