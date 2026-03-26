/**
  * @file    app_protocol.c
  * @brief   Protocol application layer — frame parser & command dispatcher
  *
  * Receive path (control frame only):
  *   BSP ring buffer → ProcessByte() state machine → command dispatch
  *
  * Transmit path:
  *   SendFrame() builds frame into static buffer → BSP_UART_Transmit()
  */

#include "app_protocol.h"
#include "bsp_uart.h"
#include <stddef.h>

/*--------------------------------------------------------------------------*/
/*                         RX state machine                                 */
/*--------------------------------------------------------------------------*/

typedef enum
{
    STATE_WAIT_SOF1,    /* Waiting for 0xAA                                 */
    STATE_WAIT_SOF2,    /* Waiting for 0x55                                 */
    STATE_WAIT_SEQ,     /* Waiting for frame sequence number                */
    STATE_WAIT_CMD,     /* Waiting for command code                         */
    STATE_WAIT_LEN,     /* Waiting for data length                          */
    STATE_WAIT_DATA,    /* Receiving data bytes                             */
    STATE_WAIT_CRC_H,   /* Waiting for CRC high byte                       */
    STATE_WAIT_CRC_L,   /* Waiting for CRC low byte, then dispatch          */
} Proto_State_t;

static Proto_State_t s_state;
static Proto_Frame_t s_rxFrame;
static uint8_t       s_dataIdx;
static uint8_t       s_rxCrcHigh;
static uint16_t      s_rxCrcAccum;  /* Running CRC16 over SEQ+CMD+LEN+DATA */

/*--------------------------------------------------------------------------*/
/*                      CRC16-MODBUS (poly 0x8005)                          */
/*--------------------------------------------------------------------------*/

static uint16_t CRC16_Update(uint16_t crc, uint8_t byte)
{
    uint8_t bit;
    crc ^= (uint16_t)byte;
    for (bit = 0U; bit < 8U; bit++)
    {
        if ((crc & 0x0001U) != 0U)
        {
            crc = (crc >> 1U) ^ 0x8005U;
        }
        else
        {
            crc >>= 1U;
        }
    }
    return crc;
}

/*--------------------------------------------------------------------------*/
/*                         TX helpers                                       */
/*--------------------------------------------------------------------------*/

/**
  * @brief Build and transmit a control frame.
  * @param seq    Frame sequence number (echo back the received seq).
  * @param cmd    Command code.
  * @param pData  Pointer to data payload, NULL if len == 0.
  * @param len    Data length in bytes.
  * @retval SUCCESS / ERROR (BSP busy)
  */
static ErrorStatus SendFrame(uint8_t seq, uint8_t cmd,
                              const uint8_t *pData, uint8_t len)
{
    static uint8_t s_txBuf[PROTO_MAX_FRAME_LEN];
    uint16_t crc = 0xFFFFU;
    uint8_t  idx = 0U;
    uint8_t  i;

    /* Frame header (excluded from CRC) */
    s_txBuf[idx++] = PROTO_SOF1;
    s_txBuf[idx++] = PROTO_SOF2;

    /* CRC-covered fields: SEQ, CMD, LEN */
    s_txBuf[idx] = seq; crc = CRC16_Update(crc, seq); idx++;
    s_txBuf[idx] = cmd; crc = CRC16_Update(crc, cmd); idx++;
    s_txBuf[idx] = len; crc = CRC16_Update(crc, len); idx++;

    /* Data payload */
    for (i = 0U; i < len; i++)
    {
        s_txBuf[idx] = pData[i];
        crc = CRC16_Update(crc, pData[i]);
        idx++;
    }

    /* CRC16 big-endian */
    s_txBuf[idx++] = (uint8_t)(crc >> 8U);
    s_txBuf[idx++] = (uint8_t)(crc & 0xFFU);

    return BSP_UART_Transmit(s_txBuf, idx);
}

static void SendErrorResp(uint8_t seq, Proto_ErrCode_t err)
{
    uint8_t errByte = (uint8_t)err;
    (void)SendFrame(seq, (uint8_t)PROTO_CMD_ERROR_RESP, &errByte, 1U);
}

/*--------------------------------------------------------------------------*/
/*                        Command handlers                                  */
/*--------------------------------------------------------------------------*/

static void HandleHeartbeat(const Proto_Frame_t *pFrame)
{
    (void)SendFrame(pFrame->seq, (uint8_t)PROTO_CMD_HEARTBEAT, NULL, 0U);
}

/*--------------------------------------------------------------------------*/
/*                         RX state machine                                 */
/*--------------------------------------------------------------------------*/

static void ProcessByte(uint8_t byte)
{
    switch (s_state)
    {
        case STATE_WAIT_SOF1:
            if (byte == PROTO_SOF1)
            {
                s_state = STATE_WAIT_SOF2;
            }
            break;

        case STATE_WAIT_SOF2:
            if (byte == PROTO_SOF2)
            {
                s_state = STATE_WAIT_SEQ;
            }
            else
            {
                s_state = STATE_WAIT_SOF1;
            }
            break;

        case STATE_WAIT_SEQ:
            s_rxFrame.seq = byte;
            s_rxCrcAccum  = CRC16_Update(0xFFFFU, byte);
            s_state       = STATE_WAIT_CMD;
            break;

        case STATE_WAIT_CMD:
            s_rxFrame.cmd = byte;
            s_rxCrcAccum  = CRC16_Update(s_rxCrcAccum, byte);
            s_state       = STATE_WAIT_LEN;
            break;

        case STATE_WAIT_LEN:
            s_rxFrame.len = byte;
            s_rxCrcAccum  = CRC16_Update(s_rxCrcAccum, byte);
            s_dataIdx     = 0U;
            s_state       = (byte == 0U) ? STATE_WAIT_CRC_H : STATE_WAIT_DATA;
            break;

        case STATE_WAIT_DATA:
            s_rxFrame.data[s_dataIdx++] = byte;
            s_rxCrcAccum = CRC16_Update(s_rxCrcAccum, byte);
            if (s_dataIdx >= s_rxFrame.len)
            {
                s_state = STATE_WAIT_CRC_H;
            }
            break;

        case STATE_WAIT_CRC_H:
            s_rxCrcHigh = byte;
            s_state     = STATE_WAIT_CRC_L;
            break;

        case STATE_WAIT_CRC_L:
        {
            uint16_t rxCrc = ((uint16_t)s_rxCrcHigh << 8U) | (uint16_t)byte;

            if (rxCrc != s_rxCrcAccum)
            {
                /* SEQ is untrusted on CRC failure, fill 0xFF per spec */
                SendErrorResp(PROTO_CRC_ERR_SEQ, PROTO_ERR_CRC_FAIL);
            }
            else
            {
                switch (s_rxFrame.cmd)
                {
                    case PROTO_CMD_HEARTBEAT:
                        HandleHeartbeat(&s_rxFrame);
                        break;

                    default:
                        SendErrorResp(s_rxFrame.seq, PROTO_ERR_UNKNOWN_CMD);
                        break;
                }
            }
            s_state = STATE_WAIT_SOF1;
            break;
        }

        default:
            s_state = STATE_WAIT_SOF1;
            break;
    }
}

/*--------------------------------------------------------------------------*/
/*                            Public API                                    */
/*--------------------------------------------------------------------------*/

void App_Protocol_Init(void)
{
    s_state      = STATE_WAIT_SOF1;
    s_dataIdx    = 0U;
    s_rxCrcHigh  = 0U;
    s_rxCrcAccum = 0U;
}

void App_Protocol_Poll(void)
{
    static uint8_t s_rxBuf[BSP_UART_RX_BUF_SIZE];
    uint16_t len;
    uint16_t i;

    len = BSP_UART_RxRead(s_rxBuf, BSP_UART_RX_BUF_SIZE);
    for (i = 0U; i < len; i++)
    {
        ProcessByte(s_rxBuf[i]);
    }
}
