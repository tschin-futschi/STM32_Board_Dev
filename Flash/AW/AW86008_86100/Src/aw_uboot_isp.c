/*
 * ============================================================================
 *  本地修改记录（供应商原始文件 → 本项目集成版的改动，按时间顺序自上而下追加）
 * ----------------------------------------------------------------------------
 *  2026-05-19  AW 自定义类型 AW_U8 / AW_U32 → 标准 C 类型 uint8_t / uint32_t
 *  2026-05-19  引入回调架构：3 个 stub 改成 static 转发器调 s_ops 表；
 *              aw_i2c_write/read 签名扩展 (DevId, AddrSize, pAddr, Size, Data) 返回 int；
 *              新增 #define AW_UBOOT_I2C_ADDR 0x6FU；
 *              所有真代码 vendor 调用点更新为新签名（返回值暂 (void) 丢弃，错误传播下一步）；
 *              aw_i2c_isp_download / _upload 顶部加 NULL 防御返回 ISP_NOT_INITED
 *  2026-05-19  AW_FLASH_BASE / AW_FLASH_TOP 使用点全部加 AW_ 前缀，配合 isp.h 同步改名，
 *              避免与 STM32 CMSIS 的 FLASH_BASE (0x08000000) 重名冲突
 *  2026-05-19  aw_reset_chip / aw_boot_control 伪代码 → 真代码：抽出 AwWakeSequence
 *              helper（7 写 + 1 读校验，读期望 0x01 否则返 ISP_FLASH_ERROR），
 *              aw_boot_control 收尾用 for 循环 20× aw_i2c_stop_uboot() + 每次 1ms 延时；
 *              新增 #define AW_CHIP_I2C_ADDR 0x6BU
 *  2026-05-19  清理 vendor warnings 共 7 处：
 *              + aw_i2c_isp_upload 末尾补 return ret;（vendor 真 bug，避免返回值未定义）
 *              + aw_flash_jump_check 加 (void)addr; 标记参数暂未用
 *              + aw_flash_pack_read 删除未用变量 d_buff；内层 for 改用外层 i 消除 shadow + sign-compare
 *              + aw_flash_read 删除未用变量 block_cnt 及其赋值
 *  2026-05-21  aw_flash_jump_check 函数体内加 IMPORTANT 警告注释，
 *              说明 stub 行为：始终返回 ISP_OK，jump 后是否真正运行新固件无法判断；
 *              aw_reset_chip 函数体内加注释说明实际是 wake-out-of-uboot
 *              （非硬件 reset），沿用 vendor 命名以保持 API 兼容
 * ============================================================================
 */

/*
* Copyright © Shanghai Awinic Technology Co., Ltd. 2019 2019 . All rights reserved.
* Description: spi correlation function
*/
#include "aw_uboot_isp.h"
#include "string.h"

/*--------------------------------------------------------------------------*/
/*  Local constants & callback state                                         */
/*--------------------------------------------------------------------------*/

/* AW86006 / AW86100 进入 stay-in-uboot 后所有 ISP 协议帧通信用的 I2C 7-bit 地址。
 * 推断自 aw_boot_control() 末尾的 20 次 0xAC 序列（写到 0x6f）。如实测不对改这里。 */
#define AW_UBOOT_I2C_ADDR    0x6FU

/* AW86xxx 上电 wake 序列阶段使用的 I2C 7-bit 地址（与 stay-in-uboot 阶段的
 * AW_UBOOT_I2C_ADDR=0x6F 不是同一个地址，但都在同一条物理 I2C2 总线上响应）。 */
#define AW_CHIP_I2C_ADDR     0x6BU

static const aw_isp_ops_t *s_ops = NULL;

/*--------------------------------------------------------------------------*/
/*  Public init API                                                          */
/*--------------------------------------------------------------------------*/

ISP_STATUS_E aw_isp_init(const aw_isp_ops_t *ops)
{
	if ((ops == NULL) ||
	    (ops->delay_ms == NULL) ||
	    (ops->i2c_write == NULL) ||
	    (ops->i2c_read == NULL)) {
		return ISP_NOT_INITED;
	}
	s_ops = ops;
	return ISP_OK;
}

/*--------------------------------------------------------------------------*/
/*  Internal forwarders — vendor 低层调用转给 BSP 注册的 callback。           */
/*  返回值在 vendor 调用点暂未检查，错误传播留作下一步。                       */
/*--------------------------------------------------------------------------*/

static void delay_ms(uint32_t xms)
{
	if (s_ops != NULL) {
		s_ops->delay_ms(xms);
	}
}

static int aw_i2c_write(uint8_t DevId, uint8_t AddrSize, uint8_t *pAddr,
                        uint8_t WrSize, uint8_t *WrData)
{
	if (s_ops == NULL) {
		return -1;
	}
	return s_ops->i2c_write(DevId, AddrSize, pAddr, WrSize, WrData);
}

static int aw_i2c_read(uint8_t DevId, uint8_t AddrSize, uint8_t *pAddr,
                       uint8_t RdSize, uint8_t *pRdBuf)
{
	if (s_ops == NULL) {
		return -1;
	}
	return s_ops->i2c_read(DevId, AddrSize, pAddr, RdSize, pRdBuf);
}

//stay in UBOOT state (stop UBOOT)
//It needs to be sent multiple times within 8ms when power on
void aw_i2c_stop_uboot(void)
{
	uint8_t tmp = 0xAC;
	(void)aw_i2c_write(AW_UBOOT_I2C_ADDR, 0U, NULL, 1U, &tmp);
}

/*--------------------------------------------------------------------------*/
/*  AwWakeSequence — 7 写 + 1 读校验，aw_reset_chip 与 aw_boot_control 共用 */
/*--------------------------------------------------------------------------*/

static ISP_STATUS_E AwWakeSequence(void)
{
	(void)aw_i2c_write(AW_CHIP_I2C_ADDR, 0U, NULL, 3U, (uint8_t[]){ 0xff, 0xa3, 0x02 });
	delay_ms(2);
	(void)aw_i2c_write(AW_CHIP_I2C_ADDR, 0U, NULL, 3U, (uint8_t[]){ 0xff, 0xb7, 0x5a });
	delay_ms(2);
	(void)aw_i2c_write(AW_CHIP_I2C_ADDR, 0U, NULL, 3U, (uint8_t[]){ 0xff, 0xb7, 0x00 });
	delay_ms(15);
	(void)aw_i2c_write(AW_CHIP_I2C_ADDR, 0U, NULL, 10U,
	    (uint8_t[]){ 0xff, 0xf0, 0x20, 0x20, 0x02, 0x02, 0x19, 0x29, 0x19, 0x29 });

	{
		uint8_t rdval = 0U;
		(void)aw_i2c_read(AW_CHIP_I2C_ADDR, 2U,
		    (uint8_t[]){ 0xff, 0xf0 }, 1U, &rdval);
		if (rdval != 0x01U) {
			return ISP_FLASH_ERROR;
		}
	}
	delay_ms(2);

	(void)aw_i2c_write(AW_CHIP_I2C_ADDR, 0U, NULL, 3U, (uint8_t[]){ 0xff, 0xb4, 0x00 });
	delay_ms(2);
	(void)aw_i2c_write(AW_CHIP_I2C_ADDR, 0U, NULL, 3U, (uint8_t[]){ 0xff, 0xa3, 0x00 });
	delay_ms(2);
	(void)aw_i2c_write(AW_CHIP_I2C_ADDR, 0U, NULL, 3U, (uint8_t[]){ 0xff, 0xb4, 0x01 });

	return ISP_OK;
}

ISP_STATUS_E aw_reset_chip(void)
{
	/* 实际是 wake-out-of-uboot 序列（7 写 + 1 读校验 + 15ms 收尾），非硬件 reset。
	 * 对 AW86008/AW86100 等价于"让芯片从 Flash 运行用户固件"。
	 * 沿用 vendor 命名以保持 API 兼容；改名会破坏 vendor SDK 升级合并。 */
	ISP_STATUS_E ret = AwWakeSequence();
	if (ret != ISP_OK) {
		return ret;
	}
	delay_ms(15);
	return ISP_OK;
}

ISP_STATUS_E aw_boot_control(void)
{
	ISP_STATUS_E ret = AwWakeSequence();
	if (ret != ISP_OK) {
		return ret;
	}
	delay_ms(7);

	/* stay-in-uboot：20 次单字节 0xac 写到 0x6f，每次后 1ms 延时 */
	for (uint8_t i = 0U; i < 20U; i++) {
		aw_i2c_stop_uboot();
		delay_ms(1);
	}

	return ISP_OK;
}

//len is the length of the word(4Byte)
ISP_STATUS_E aw_i2c_isp_download(uint32_t addr, uint8_t *bin_buf, uint32_t len)
{
	ISP_STATUS_E ret = ISP_OK;
	uint32_t space = 0;

	if (s_ops == NULL) {
		return ISP_NOT_INITED;
	}

	//bin_buff pointer check
	if (bin_buf == (void *)0) {
		return ISP_PBUF_ERROR;
	}

	//isp download address check
	if ((addr < AW_FLASH_BASE) || (addr >= AW_FLASH_TOP)) {
		return ISP_ADDR_ERROR;
	}

	//isp download space check
	space = AW_FLASH_TOP - addr;
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
ISP_STATUS_E aw_i2c_isp_upload(uint32_t addr, uint8_t *bin_buf, uint32_t len)
{
	ISP_STATUS_E ret = ISP_OK;
	uint32_t space = 0;

	if (s_ops == NULL) {
		return ISP_NOT_INITED;
	}

	//bin_buff pointer check
	if (bin_buf == (void *)0) {
		return ISP_PBUF_ERROR;
	}

	//isp download address check
	if ((addr < AW_FLASH_BASE) || (addr >= AW_FLASH_TOP)) {
		return ISP_ADDR_ERROR;
	}

	//isp download space check
	space = AW_FLASH_TOP - addr;
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

	return ret;
}

ISP_STATUS_E aw_flash_jump_check(uint32_t addr)
{
	/* IMPORTANT: 此函数永远返回 ISP_OK。vendor 真实现（下方注释块）未集成
	 * — jump 后是否真正运行新固件无法在此层判断。调用此函数后
	 * aw_i2c_isp_download 返回 ISP_OK 仅等于 "erase + write 子步骤都 ACK 了"，
	 * 不等于新固件已运行。PC 端必须用"烧录后回读 Flash 校验"或"0x37 RESET_CHIP
	 * + 后续操作可达性测试"二次确认烧录结果。联调阶段确认 vendor 真实现的
	 * ACK 格式后再启用。 */
	(void)addr;   /* stub：addr 暂未使用 */

	/*ISP_STATUS_E ret = ISP_OK;
	uint8_t i = 0;
	uint8_t tmp = 0;
	uint8_t l_buff[4]  = {0};
	uint8_t w_buff[13] = {0};
	uint8_t d_buff[1]  = {0};
	uint8_t r_buff[10] = {0};

	for (i = 0; i < 4; i++) {
		l_buff[i] = (uint8_t)(addr >> (i * 8));
	}

	UBOOT_BUF_TYPE_S uboot_structure;
	uboot_structure.uboot_ways = UBOOT_I2C;
	uboot_structure.uboot_module = UBOOT_END;
	uboot_structure.uboot_event = END_FLASH_JUMP;
	uboot_structure.uboot_ack = UBOOT_CTL;
	uboot_structure.uboot_data_len = 4;
	uboot_structure.p_uboot_data = l_buff;
	aw_uboot_buff_built(w_buff, &uboot_structure);

	(void)aw_i2c_write(AW_UBOOT_I2C_ADDR, 0U, NULL, 13U, w_buff);

	delay_ms(10);

	(void)aw_i2c_read(AW_UBOOT_I2C_ADDR, 0U, NULL, 10U, r_buff);

	uboot_structure.uboot_event = END_FLASH_JUMP_ACK;
	uboot_structure.uboot_ack = UBOOT_ACK;
	uboot_structure.uboot_data_len = 1;
	uboot_structure.p_uboot_data = d_buff;
	aw_uboot_buff_built(w_buff, &uboot_structure);

	tmp = (uint8_t)memcmp(w_buff, r_buff, 10);
	if (tmp != 0) {
		ret = ISP_JUMP_ERROR;
		return ret;
	}
	return ret;*/
	return ISP_OK;
}

ISP_STATUS_E aw_flash_pack_read(uint32_t u32Addr, uint32_t u32PackNum, uint8_t* byDataBuff, uint32_t u32ReadLen)
{
	ISP_STATUS_E ret = ISP_OK;
	uint8_t l_buff[6] = { 0 };
	uint8_t w_buff[78] = { 0 };
	uint8_t r_buff[69] = { 0 };
	uint32_t i = 0;
	uint8_t tmp = -1;

	for (i = 0; i < 2; i++) {
		l_buff[i] = (uint8_t)((u32ReadLen * 4) >> (i * 8));
	}
	for (i = 0; i < 4; i++) {
		l_buff[i + 2] = (uint8_t)((u32Addr + u32PackNum * 64) >> (i * 8));
	}

	UBOOT_BUF_TYPE_S uboot_struct;
	uboot_struct.uboot_ways = UBOOT_I2C;
	uboot_struct.uboot_module = UBOOT_FLASH;
	uboot_struct.uboot_event = FLASH_READ;
	uboot_struct.uboot_ack = UBOOT_CTL;
	uboot_struct.uboot_data_len = 6;
	uboot_struct.p_uboot_data = l_buff;
	aw_uboot_buff_built(w_buff, &uboot_struct);

	(void)aw_i2c_write(AW_UBOOT_I2C_ADDR, 0U, NULL, 15U, w_buff);

	delay_ms(1);

	(void)aw_i2c_read(AW_UBOOT_I2C_ADDR, 0U, NULL, (uint8_t)(u32ReadLen * 4 + 1 + 4 + 9), r_buff);

	if (r_buff[9] == 0) {
		tmp = 0;
	}

	if (tmp != 0) {
		ret = ISP_FLASH_ERROR;
		return ret;
	}

	for (i = 0; i < u32ReadLen * 4; i++) {
		byDataBuff[u32PackNum * 64 + i] = r_buff[14 + i];
	}

	return ret;
}

ISP_STATUS_E aw_flash_read(uint32_t addr, uint32_t len, uint8_t* dataBuff)
{
	ISP_STATUS_E ret = ISP_OK;
	uint32_t divisor = 0;
	uint8_t remaind = 0;
	uint32_t i = 0;

	if (dataBuff == (void*)0) {
		ret = ISP_PBUF_ERROR;
		return ret;
	}

	divisor = len / 16;
	remaind = len % 16;

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

ISP_STATUS_E aw_flash_block_erase_check(uint32_t addr, uint32_t len)
{
	ISP_STATUS_E ret = ISP_OK;
	uint8_t i = 0;
	uint8_t tmp = 0;
	uint8_t l_buff[6]  = {0};
	uint8_t w_buff[15] = {0};
	uint8_t d_buff[1]  = {0};
	uint8_t r_buff[10] = {0};

	l_buff[0] = (uint8_t)len;
	l_buff[1] = 0x00;

	for (i = 0; i < 4; i++) {
		l_buff[i + 2] = (uint8_t)(addr >> (i * 8));
	}

	UBOOT_BUF_TYPE_S uboot_structure;
	uboot_structure.uboot_ways = UBOOT_I2C;
	uboot_structure.uboot_module = UBOOT_FLASH;
	uboot_structure.uboot_event = FLASH_ERASE_BLOCK;
	uboot_structure.uboot_ack = UBOOT_CTL;
	uboot_structure.uboot_data_len = 6;
	uboot_structure.p_uboot_data = l_buff;
	aw_uboot_buff_built(w_buff, &uboot_structure);

	(void)aw_i2c_write(AW_UBOOT_I2C_ADDR, 0U, NULL, 15U, w_buff);

	delay_ms(25 * len);

	(void)aw_i2c_read(AW_UBOOT_I2C_ADDR, 0U, NULL, 10U, r_buff);

	uboot_structure.uboot_event = FLASH_ERASE_BLOCK_ACK;
	uboot_structure.uboot_ack = UBOOT_ACK;
	uboot_structure.uboot_data_len = 1;
	uboot_structure.p_uboot_data = d_buff;
	aw_uboot_buff_built(w_buff, &uboot_structure);

	tmp = (uint8_t)memcmp(w_buff, r_buff, 10);
	if (tmp != 0) {
		ret = ISP_FLASH_ERROR;
		return ret;
	}

	return ret;
}

ISP_STATUS_E aw_flash_block_write_ckeck(uint32_t addr, uint32_t block_num, uint8_t *bin_buf, uint32_t len)
{
	ISP_STATUS_E ret = ISP_OK;
	uint8_t i = 0;
	uint8_t tmp = 0;
	uint8_t l_buff[68]  = {0};
	uint8_t w_buff[77] = {0};
	uint8_t d_buff[1]  = {0};
	uint8_t r_buff[10] = {0};

	for (i = 0; i < 4; i++) {
		l_buff[i] = (uint8_t)((addr + block_num * 64) >> (i * 8));
	}
	for (i = 0; i < (len * 4); i++) {
		l_buff[i + 4] = (bin_buf + block_num * 64)[i];
	}

	UBOOT_BUF_TYPE_S uboot_structure;
	uboot_structure.uboot_ways = UBOOT_I2C;
	uboot_structure.uboot_module = UBOOT_FLASH;
	uboot_structure.uboot_event = FLASH_WRITE;
	uboot_structure.uboot_ack = UBOOT_CTL;
	uboot_structure.uboot_data_len = (uint8_t)(4 + 4 * len);
	uboot_structure.p_uboot_data = l_buff;
	aw_uboot_buff_built(w_buff, &uboot_structure);

	(void)aw_i2c_write(AW_UBOOT_I2C_ADDR, 0U, NULL, (uint8_t)(13 + 4 * len), w_buff);

	delay_ms(1);

	(void)aw_i2c_read(AW_UBOOT_I2C_ADDR, 0U, NULL, 10U, r_buff);

	uboot_structure.uboot_event = FLASH_WRITE_ACK;
	uboot_structure.uboot_ack = UBOOT_ACK;
	uboot_structure.uboot_data_len = 1;
	uboot_structure.p_uboot_data = d_buff;
	aw_uboot_buff_built(w_buff, &uboot_structure);

	tmp = (uint8_t)memcmp(w_buff, r_buff, 10);
	if (tmp != 0) {
		ret = ISP_FLASH_ERROR;
		return ret;
	}

	return ret;
}


ISP_STATUS_E aw_flash_download_check(uint32_t addr, uint8_t *bin_buf, uint32_t len)
{
	ISP_STATUS_E ret = ISP_OK;
	uint8_t remaind = 0;
	uint32_t block_cnt = 0;
	uint32_t i = 0;
	uint32_t divisor = 0;

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
	uint8_t tmp = 0;
	uint8_t w_buff[255] = {0};
	uint8_t r_buff[255] = {0};
	uint8_t d_buff[5]   = {0x00, 0x01,0x00, 0x00, 0x00};

	UBOOT_BUF_TYPE_S uboot_structure;
	uboot_structure.uboot_ways = UBOOT_I2C;
	uboot_structure.uboot_ack = UBOOT_CTL;
	uboot_structure.uboot_module = UBOOT_HANK;
	uboot_structure.uboot_event = HANK_CONNECT;
	uboot_structure.uboot_data_len = 0;
	uboot_structure.p_uboot_data =  0;
	aw_uboot_buff_built(w_buff, &uboot_structure);

	(void)aw_i2c_write(AW_UBOOT_I2C_ADDR, 0U, NULL, 9U, w_buff);

	delay_ms(2);

	(void)aw_i2c_read(AW_UBOOT_I2C_ADDR, 0U, NULL, 14U, r_buff);

	uboot_structure.uboot_event = HANK_CONNECT_ACK;
	uboot_structure.uboot_ack = UBOOT_ACK;
	uboot_structure.uboot_data_len = 5;
	uboot_structure.p_uboot_data = d_buff;
	aw_uboot_buff_built(w_buff, &uboot_structure);

	tmp = (uint8_t)memcmp(w_buff, r_buff, 14);
	if (tmp != 0) {
		ret = ISP_HANK_ERROR;
	}

	return ret;
}
