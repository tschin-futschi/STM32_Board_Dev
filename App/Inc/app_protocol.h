/**
  * @file    app_protocol.h
  * @brief   Protocol application layer — control frame parser & dispatcher
  *
  * Frame format (protocol.MD v1.4):
  *   Control:     [0xAA][0x55][SEQ][CMD][LEN][DATA n][CRC16_H][CRC16_L]
  *   Data stream: [0xBB][MASK][LEN][DATA n][XOR]  — TX only, defined here for reference
  *
  * CRC16: poly=0x8005, init=0xFFFF, right-shift/LSB-first, result big-endian
 * NOTE: NOT standard CRC16-MODBUS (which uses reflected poly 0xA001)
  * CRC range: SEQ + CMD + LEN + DATA (excludes frame header)
  */

#ifndef __APP_PROTOCOL_H
#define __APP_PROTOCOL_H

#include "stm32f4xx.h"

/*--------------------------------------------------------------------------*/
/*                           Frame constants                                */
/*--------------------------------------------------------------------------*/

#define PROTO_SOF1              0xAAU   /* Control frame byte 1             */
#define PROTO_SOF2              0x55U   /* Control frame byte 2             */
#define PROTO_STREAM_SOF        0xBBU   /* Data stream frame header         */

#define PROTO_MAX_DATA_LEN      255U    /* Max bytes in DATA field          */
#define PROTO_FRAME_OVERHEAD    7U      /* SOF1+SOF2+SEQ+CMD+LEN+CRC_H+CRC_L */
#define PROTO_MAX_FRAME_LEN     (PROTO_FRAME_OVERHEAD + PROTO_MAX_DATA_LEN)

#define PROTO_CRC_ERR_SEQ       0xFFU   /* SEQ value when CRC fails (seq untrusted) */

/*--------------------------------------------------------------------------*/
/*                           Command codes                                  */
/*--------------------------------------------------------------------------*/

typedef enum
{
    /* System control group (0x00~0x1F) */
    PROTO_CMD_HEARTBEAT      = 0x00U,   /* Bidirectional, data empty                */
    PROTO_CMD_ERROR_RESP     = 0x01U,   /* STM32->PC, data = 1 byte error code      */
    PROTO_CMD_DEBUG_INFO     = 0x06U,   /* STM32->PC, data = ASCII string           */
    PROTO_CMD_SET_MOTOR_ADDR = 0x02U,   /* PC->STM32, data = 1 byte 7-bit I2C addr  */
    PROTO_CMD_SET_BAUDRATE   = 0x03U,   /* PC->STM32, data = 1 byte baudrate index  */
    PROTO_CMD_RESET          = 0x04U,   /* PC->STM32, data empty, triggers sw reset */
    PROTO_CMD_MOTOR_PING     = 0x05U,   /* PC->STM32, data empty, tests I2C ACK     */
    PROTO_CMD_I2C_SCAN       = 0x07U,   /* PC->STM32, data = 1 byte bus index (1/2/3); resp = [count][addr...] */
    PROTO_CMD_PMIC_ENABLE    = 0x08U,   /* PC->STM32, data empty, enable LDO sequence   */
    PROTO_CMD_PMIC_SET_VOLT  = 0x09U,   /* PC->STM32, data = 6 bytes [DRVVDD_H/L][IOVDD_H/L][VCMVDD_H/L] */
    PROTO_CMD_PMIC_DISABLE   = 0x0AU,   /* PC->STM32, data empty, disable LDO sequence  */

    /* Register read/write group (0x20~0x4F) */
    PROTO_CMD_READ_REG       = 0x20U,   /* PC->STM32, data = 2 bytes [regH][regL]                   */
    PROTO_CMD_WRITE_REG      = 0x21U,   /* PC->STM32, data = 4 bytes [regH][regL][valH][valL]        */
    PROTO_CMD_BULK_READ      = 0x22U,   /* PC->STM32, data = 4 bytes [regH][regL][cntH][cntL]        */
    PROTO_CMD_I2C_PASS_WRITE = 0x30U,   /* PC->STM32, AW Firmware I2C 写指令; data = [DevId][AddrSize][AddrBytes...][DataLen][Data...] */
    PROTO_CMD_I2C_PASS_READ  = 0x31U,   /* PC->STM32, AW Firmware I2C 读指令; data = [DevId][AddrSize][AddrBytes...][ReadLen] */

    /* AW local ISP flash group (0x32~0x37) — AW86008/AW86100 本地烧录 */
    PROTO_CMD_FLASH_BEGIN      = 0x32U, /* PC->STM32, data = [addr(4 LE)][totalBytes(4 LE)]                         */
    PROTO_CMD_FLASH_DATA       = 0x33U, /* PC->STM32, data = [pktSeq(2 LE)][chunk(N)]; resp = [nextSeq(2 LE)]       */
    PROTO_CMD_FLASH_EXEC       = 0x34U, /* PC->STM32, data empty; resp = [ispStatus(1)]; 阻塞 ~5-10s                */
    PROTO_CMD_FLASH_STATUS     = 0x35U, /* PC->STM32, data empty; resp = [state(1)][rxOffset(4)][totalBytes(4)]     */
    PROTO_CMD_FLASH_CANCEL     = 0x36U, /* PC->STM32, data empty; resp empty                                        */
    PROTO_CMD_FLASH_RESET_CHIP = 0x37U, /* PC->STM32, data empty; resp = [ispStatus(1)]                             */

    /* Oscilloscope & generator control group (0x50~0x7F) */
    PROTO_CMD_START_SAMPLE     = 0x50U, /* PC->STM32, data empty                            */
    PROTO_CMD_STOP_SAMPLE      = 0x51U, /* PC->STM32, data empty                            */
    PROTO_CMD_SET_INTERVAL     = 0x52U, /* PC->STM32, data = 1 byte interval index (0~6)    */
    PROTO_CMD_SET_CHANNEL      = 0x53U, /* PC->STM32, data = 1 byte channel mask             */
    PROTO_CMD_SET_REG_MAP      = 0x54U, /* PC->STM32, data = 16 bytes (8 ch × 2 bytes)      */
    PROTO_CMD_START_LINEAR_GEN = 0x55U, /* PC->STM32, 10 bytes: addr+min+max+step+interval  */
    PROTO_CMD_START_COSINE_GEN = 0x56U, /* PC->STM32, 7+N*4 bytes: amp+off+freq+N+channels  */
    PROTO_CMD_STOP_GENERATOR   = 0x57U, /* PC->STM32, data empty                            */
    PROTO_CMD_START_SAWTOOTH_GEN = 0x58U, /* PC->STM32, 8 bytes: addr+min+max+step (every tick) */
} Proto_Cmd_t;

/*--------------------------------------------------------------------------*/
/*                           Error codes                                    */
/*--------------------------------------------------------------------------*/

typedef enum
{
    PROTO_ERR_CRC_FAIL    = 0x01U,  /* CRC check failed, seq filled 0xFF   */
    PROTO_ERR_UNKNOWN_CMD = 0x02U,  /* Command code not defined/supported   */
    PROTO_ERR_EXEC_FAIL   = 0x03U,  /* Command executed but failed (e.g. I2C) */
} Proto_ErrCode_t;

/*--------------------------------------------------------------------------*/
/*                           Frame structure                                */
/*--------------------------------------------------------------------------*/

typedef struct
{
    uint8_t seq;                        /* Frame sequence number            */
    uint8_t cmd;                        /* Command code (raw byte)          */
    uint8_t len;                        /* Data field length                */
    uint8_t data[PROTO_MAX_DATA_LEN];   /* Data payload                     */
} Proto_Frame_t;

/*--------------------------------------------------------------------------*/
/*                               API                                        */
/*--------------------------------------------------------------------------*/

void App_Protocol_Init(void);
void App_Protocol_Poll(void);
void App_Protocol_SendDebugInfo(const char *msg);
void App_Protocol_SendErrorResp(uint8_t seq, Proto_ErrCode_t err);

#endif /* __APP_PROTOCOL_H */
