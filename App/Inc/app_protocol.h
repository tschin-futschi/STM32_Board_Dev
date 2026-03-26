/**
  * @file    app_protocol.h
  * @brief   Protocol application layer — control frame parser & dispatcher
  *
  * Frame format (protocol.MD v1.3):
  *   Control:     [0xAA][0x55][SEQ][CMD][LEN][DATA n][CRC16_H][CRC16_L]
  *   Data stream: [0xBB][MASK][DATA n][XOR]  — TX only, defined here for reference
  *
  * CRC16-MODBUS: poly=0x8005, init=0xFFFF, LSB-first, result big-endian
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
    PROTO_CMD_HEARTBEAT  = 0x00U,   /* Bidirectional, data empty            */
    PROTO_CMD_ERROR_RESP = 0x01U,   /* STM32->PC, data = 1 byte error code  */
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
/*                     Command handler function pointer                     */
/*--------------------------------------------------------------------------*/

typedef void (*Proto_CmdHandler_t)(const Proto_Frame_t *pFrame);

/*--------------------------------------------------------------------------*/
/*                               API                                        */
/*--------------------------------------------------------------------------*/

void App_Protocol_Init(void);
void App_Protocol_Poll(void);

#endif /* __APP_PROTOCOL_H */
