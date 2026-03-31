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
#include "bsp_i2c2.h"
#include "app_motor.h"
#include "app_sample.h"
#include <stddef.h>
#include <string.h>

/*--------------------------------------------------------------------------*/
/*                         Baudrate lookup table                            */
/*--------------------------------------------------------------------------*/

/* Indexed by 0x00~0x07, matches protocol.MD section 4.2 */
static const uint32_t k_baudrateTable[8U] =
{
      9600U,  /* 0x00 */
     19200U,  /* 0x01 */
     38400U,  /* 0x02 */
     57600U,  /* 0x03 */
    115200U,  /* 0x04 (default) */
    230400U,  /* 0x05 */
    460800U,  /* 0x06 */
    921600U,  /* 0x07 */
};

#define PROTO_BAUDRATE_IDX_MAX  7U
#define PROTO_MOTOR_ADDR_MAX    0x7FU

/* Bulk read packet sizing */
#define BULK_MAX_DATA_PER_PKT   252U                            /* Max data bytes per packet (126 regs × 2)          */
#define BULK_PKT_HEADER_LEN     2U                              /* [packet_seq(1)][total_packets(1)]                 */
#define BULK_REGS_PER_PKT       (BULK_MAX_DATA_PER_PKT / 2U)   /* Max registers per packet                         */
#define BULK_MAX_TOTAL_REGS     (255U * BULK_REGS_PER_PKT)     /* Max total regs: 255 pkts × 126 regs/pkt = 32130   */

/* Compile-time guard: TX frame must fit inside BSP DMA buffer */
_Static_assert(PROTO_MAX_FRAME_LEN <= BSP_UART_TX_BUF_SIZE,
               "PROTO_MAX_FRAME_LEN exceeds BSP_UART_TX_BUF_SIZE");

/* Compile-time guard: bulk packet data length must fit in uint8_t */
_Static_assert((BULK_PKT_HEADER_LEN + BULK_MAX_DATA_PER_PKT) <= 255U,
               "Bulk packet dataLen overflows uint8_t");

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
static uint16_t      s_rxCrcAccum;          /* Running CRC16 over SEQ+CMD+LEN+DATA */
static uint8_t       s_rxBuf[BSP_UART_RX_BUF_SIZE];

static const uint16_t k_crc16Init = 0xFFFFU; /* CRC16-MODBUS initial value  */

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
    uint16_t crc = k_crc16Init;
    uint16_t idx = 0U;
    uint8_t  i;

    s_txBuf[idx++] = PROTO_SOF1;
    s_txBuf[idx++] = PROTO_SOF2;

    s_txBuf[idx] = seq; crc = CRC16_Update(crc, seq); idx++;
    s_txBuf[idx] = cmd; crc = CRC16_Update(crc, cmd); idx++;
    s_txBuf[idx] = len; crc = CRC16_Update(crc, len); idx++;

    for (i = 0U; i < len; i++)
    {
        s_txBuf[idx] = pData[i];
        crc = CRC16_Update(crc, pData[i]);
        idx++;
    }

    /* CRC16 big-endian (protocol spec: high byte first) */
    s_txBuf[idx++] = (uint8_t)(crc >> 8U);
    s_txBuf[idx++] = (uint8_t)(crc & 0xFFU);

    BSP_UART_TxWait();
    return BSP_UART_Transmit(s_txBuf, idx);
}

static void SendErrorResp(uint8_t seq, Proto_ErrCode_t err)
{
    uint8_t errByte = (uint8_t)err;
    (void)SendFrame(seq, (uint8_t)PROTO_CMD_ERROR_RESP, &errByte, 1U);
}

/* Send a 0x06 debug info frame (ASCII string, max PROTO_MAX_DATA_LEN bytes) */
static void SendDebugInfo(const char *msg)
{
    uint8_t len = (uint8_t)strlen(msg);
    (void)SendFrame(0x00U, (uint8_t)PROTO_CMD_DEBUG_INFO,
                    (const uint8_t *)msg, len);
}

/*--------------------------------------------------------------------------*/
/*                   Command handlers & dispatcher                          */
/*--------------------------------------------------------------------------*/

/*
 * Probe a 7-bit I2C address on the motor bus by attempting a 1-byte read.
 * Returns SUCCESS if device ACKs, ERROR otherwise.
 */
static ErrorStatus ProbeMotorIc(uint8_t addr)
{
    uint8_t dummy;
    return BSP_I2C2_ReadReg(addr, 0x00U, &dummy, 1U);
}

static void HandleHeartbeat(const Proto_Frame_t *pFrame)
{
    (void)SendFrame(pFrame->seq, (uint8_t)PROTO_CMD_HEARTBEAT, NULL, 0U);
}

/* 0x02 — Set motor IC I2C address */
static void HandleSetMotorAddr(const Proto_Frame_t *pFrame)
{
    uint8_t addr;

    if (pFrame->len != 1U)
    {
        SendErrorResp(pFrame->seq, PROTO_ERR_EXEC_FAIL);
        return;
    }

    addr = pFrame->data[0];

    /* Validate: must be non-zero and within 7-bit range */
    if ((addr == 0x00U) || (addr > PROTO_MOTOR_ADDR_MAX))
    {
        SendErrorResp(pFrame->seq, PROTO_ERR_EXEC_FAIL);
        return;
    }

    /* Verify device ACKs on I2C bus */
    if (ProbeMotorIc(addr) != SUCCESS)
    {
        SendErrorResp(pFrame->seq, PROTO_ERR_EXEC_FAIL);
        return;
    }

    g_motorIcAddr = addr;
    (void)SendFrame(pFrame->seq, (uint8_t)PROTO_CMD_SET_MOTOR_ADDR, NULL, 0U);
}

/* 0x03 — Set UART baudrate */
static void HandleSetBaudrate(const Proto_Frame_t *pFrame)
{
    uint8_t idx;

    if (pFrame->len != 1U)
    {
        SendErrorResp(pFrame->seq, PROTO_ERR_EXEC_FAIL);
        return;
    }

    idx = pFrame->data[0];

    if (idx > PROTO_BAUDRATE_IDX_MAX)
    {
        SendErrorResp(pFrame->seq, PROTO_ERR_EXEC_FAIL);
        return;
    }

    /* Echo with current (old) baudrate first, then wait for DMA to finish */
    (void)SendFrame(pFrame->seq, (uint8_t)PROTO_CMD_SET_BAUDRATE, NULL, 0U);
    BSP_UART_TxWait();
    BSP_UART_SetBaudrate(k_baudrateTable[idx]);
    /* Discard any garbage bytes received during baudrate transition */
    BSP_UART_RxFlush();
    s_state = STATE_WAIT_SOF1;
}

/* 0x04 — System reset */
static void HandleReset(const Proto_Frame_t *pFrame)
{
    /* Echo before reset, wait for DMA to finish so PC receives the ACK */
    (void)SendFrame(pFrame->seq, (uint8_t)PROTO_CMD_RESET, NULL, 0U);
    BSP_UART_TxWait();
    NVIC_SystemReset();
}

/* 0x05 — Motor IC ping (ACK test) */
static void HandleMotorPing(const Proto_Frame_t *pFrame)
{
    if (g_motorIcAddr == 0x00U)
    {
        SendErrorResp(pFrame->seq, PROTO_ERR_EXEC_FAIL);
        return;
    }

    if (ProbeMotorIc(g_motorIcAddr) != SUCCESS)
    {
        SendErrorResp(pFrame->seq, PROTO_ERR_EXEC_FAIL);
        return;
    }

    (void)SendFrame(pFrame->seq, (uint8_t)PROTO_CMD_MOTOR_PING, NULL, 0U);
}

/* 0x20 — Read single register */
static void HandleReadReg(const Proto_Frame_t *pFrame)
{
    uint16_t reg;
    uint16_t val;
    uint8_t  resp[2];

    if (pFrame->len != 2U)
    {
        SendErrorResp(pFrame->seq, PROTO_ERR_EXEC_FAIL);
        return;
    }

    reg = ((uint16_t)pFrame->data[0] << 8U) | (uint16_t)pFrame->data[1];

    if (App_Motor_ReadReg(reg, &val) != SUCCESS)
    {
        SendErrorResp(pFrame->seq, PROTO_ERR_EXEC_FAIL);
        return;
    }

    /* Return 2 bytes big-endian */
    resp[0] = (uint8_t)(val >> 8U);
    resp[1] = (uint8_t)(val & 0xFFU);
    (void)SendFrame(pFrame->seq, (uint8_t)PROTO_CMD_READ_REG, resp, 2U);
}

/* 0x21 — Write single register */
static void HandleWriteReg(const Proto_Frame_t *pFrame)
{
    uint16_t reg;
    uint16_t val;

    if (pFrame->len != 4U)
    {
        SendErrorResp(pFrame->seq, PROTO_ERR_EXEC_FAIL);
        return;
    }

    /* data[0..1] = reg (big-endian), data[2..3] = value (big-endian) */
    reg = ((uint16_t)pFrame->data[0] << 8U) | (uint16_t)pFrame->data[1];
    val = ((uint16_t)pFrame->data[2] << 8U) | (uint16_t)pFrame->data[3];

    if (App_Motor_WriteReg(reg, val) != SUCCESS)
    {
        SendErrorResp(pFrame->seq, PROTO_ERR_EXEC_FAIL);
        return;
    }

    (void)SendFrame(pFrame->seq, (uint8_t)PROTO_CMD_WRITE_REG, NULL, 0U);
}

/* 0x22 — Bulk read registers (multi-packet response) */
static void HandleBulkRead(const Proto_Frame_t *pFrame)
{
    static uint8_t s_pktBuf[BULK_PKT_HEADER_LEN + BULK_MAX_DATA_PER_PKT];

    uint16_t startReg;
    uint16_t totalRegs;
    uint8_t  totalPkts;
    uint8_t  pktIdx;
    uint16_t regIdx;
    uint16_t val;
    uint8_t  dataLen;
    uint8_t  regsThisPkt;
    uint8_t  i;

    if (pFrame->len != 4U)
    {
        SendErrorResp(pFrame->seq, PROTO_ERR_EXEC_FAIL);
        return;
    }

    /* data[0..1] = startReg (big-endian), data[2..3] = totalRegs (big-endian) */
    startReg  = ((uint16_t)pFrame->data[0] << 8U) | (uint16_t)pFrame->data[1];
    totalRegs = ((uint16_t)pFrame->data[2] << 8U) | (uint16_t)pFrame->data[3];

    if ((totalRegs == 0U) || (totalRegs > BULK_MAX_TOTAL_REGS))
    {
        SendErrorResp(pFrame->seq, PROTO_ERR_EXEC_FAIL);
        return;
    }

    /* Calculate total packets (ceiling division) */
    totalPkts = (uint8_t)((totalRegs + BULK_REGS_PER_PKT - 1U) / BULK_REGS_PER_PKT);

    /* Reject bulk read while sampling is active */
    if (App_Sample_IsActive() != 0U)
    {
        SendErrorResp(pFrame->seq, PROTO_ERR_EXEC_FAIL);
        return;
    }

    regIdx = 0U;
    for (pktIdx = 0U; pktIdx < totalPkts; pktIdx++)
    {
        regsThisPkt = (uint8_t)(((totalRegs - regIdx) > (uint16_t)BULK_REGS_PER_PKT)
                                 ? BULK_REGS_PER_PKT
                                 : (uint8_t)(totalRegs - regIdx));
        dataLen = BULK_PKT_HEADER_LEN + (regsThisPkt * 2U);

        /* Build packet payload: [pktIdx][totalPkts][reg data...] */
        s_pktBuf[0] = pktIdx;
        s_pktBuf[1] = totalPkts;

        for (i = 0U; i < regsThisPkt; i++)
        {
            if (App_Motor_ReadReg((uint16_t)(startReg + (regIdx + i) * 2U), &val) != SUCCESS)
            {
                SendErrorResp(pFrame->seq, PROTO_ERR_EXEC_FAIL);
                return;
            }
            s_pktBuf[BULK_PKT_HEADER_LEN + (i * 2U)]      = (uint8_t)(val >> 8U);
            s_pktBuf[BULK_PKT_HEADER_LEN + (i * 2U) + 1U] = (uint8_t)(val & 0xFFU);
        }

        /* Wait for previous TX to complete, then send this packet */
        BSP_UART_TxWait();
        (void)SendFrame(pFrame->seq, (uint8_t)PROTO_CMD_BULK_READ,
                        s_pktBuf, dataLen);

        regIdx += regsThisPkt;
    }
}

/* 0x50 — Start sampling */
static void HandleStartSample(const Proto_Frame_t *pFrame)
{
    if (App_Sample_Start() != SUCCESS)
    {
        SendErrorResp(pFrame->seq, PROTO_ERR_EXEC_FAIL);
        SendDebugInfo("Start failed: all channel reg maps are 0xFFFF");
        return;
    }
    (void)SendFrame(pFrame->seq, (uint8_t)PROTO_CMD_START_SAMPLE, NULL, 0U);
}

/* 0x51 — Stop sampling */
static void HandleStopSample(const Proto_Frame_t *pFrame)
{
    App_Sample_Stop();
    (void)SendFrame(pFrame->seq, (uint8_t)PROTO_CMD_STOP_SAMPLE, NULL, 0U);
}

/* 0x52 — Set sampling interval */
static void HandleSetInterval(const Proto_Frame_t *pFrame)
{
    if (pFrame->len != 1U)
    {
        SendErrorResp(pFrame->seq, PROTO_ERR_EXEC_FAIL);
        return;
    }
    if (App_Sample_SetInterval(pFrame->data[0]) != SUCCESS)
    {
        SendErrorResp(pFrame->seq, PROTO_ERR_EXEC_FAIL);
        return;
    }
    (void)SendFrame(pFrame->seq, (uint8_t)PROTO_CMD_SET_INTERVAL, NULL, 0U);
}

/* 0x53 — Set sampling channel mask */
static void HandleSetChannel(const Proto_Frame_t *pFrame)
{
    if (pFrame->len != 1U)
    {
        SendErrorResp(pFrame->seq, PROTO_ERR_EXEC_FAIL);
        return;
    }
    if (App_Sample_SetChannelMask(pFrame->data[0]) != SUCCESS)
    {
        SendErrorResp(pFrame->seq, PROTO_ERR_EXEC_FAIL);
        return;
    }
    (void)SendFrame(pFrame->seq, (uint8_t)PROTO_CMD_SET_CHANNEL, NULL, 0U);
}

/* 0x54 — Set channel register mapping */
static void HandleSetRegMap(const Proto_Frame_t *pFrame)
{
    if (pFrame->len != APP_SAMPLE_REG_MAP_LEN)
    {
        SendErrorResp(pFrame->seq, PROTO_ERR_EXEC_FAIL);
        return;
    }
    App_Sample_SetRegMap(pFrame->data);
    (void)SendFrame(pFrame->seq, (uint8_t)PROTO_CMD_SET_REG_MAP, NULL, 0U);
}

static void DispatchFrame(const Proto_Frame_t *pFrame)
{
    switch (pFrame->cmd)
    {
        case PROTO_CMD_HEARTBEAT:
            HandleHeartbeat(pFrame);
            break;
        case PROTO_CMD_SET_MOTOR_ADDR:
            HandleSetMotorAddr(pFrame);
            break;
        case PROTO_CMD_SET_BAUDRATE:
            HandleSetBaudrate(pFrame);
            break;
        case PROTO_CMD_RESET:
            HandleReset(pFrame);
            break;
        case PROTO_CMD_MOTOR_PING:
            HandleMotorPing(pFrame);
            break;
        case PROTO_CMD_READ_REG:
            HandleReadReg(pFrame);
            break;
        case PROTO_CMD_WRITE_REG:
            HandleWriteReg(pFrame);
            break;
        case PROTO_CMD_BULK_READ:
            HandleBulkRead(pFrame);
            break;
        case PROTO_CMD_START_SAMPLE:
            HandleStartSample(pFrame);
            break;
        case PROTO_CMD_STOP_SAMPLE:
            HandleStopSample(pFrame);
            break;
        case PROTO_CMD_SET_INTERVAL:
            HandleSetInterval(pFrame);
            break;
        case PROTO_CMD_SET_CHANNEL:
            HandleSetChannel(pFrame);
            break;
        case PROTO_CMD_SET_REG_MAP:
            HandleSetRegMap(pFrame);
            break;
        default:
            SendErrorResp(pFrame->seq, PROTO_ERR_UNKNOWN_CMD);
            break;
    }
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
            else if (byte == PROTO_SOF1)
            {
                /* 0xAA may be the start of a new frame — stay in SOF2 */
                s_state = STATE_WAIT_SOF2;
            }
            else
            {
                s_state = STATE_WAIT_SOF1;
            }
            break;

        case STATE_WAIT_SEQ:
            s_rxFrame.seq = byte;
            s_rxCrcAccum  = CRC16_Update(k_crc16Init, byte);
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
                SendErrorResp(PROTO_CRC_ERR_SEQ, PROTO_ERR_CRC_FAIL);
            else
                DispatchFrame(&s_rxFrame);
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
    uint16_t len;
    uint16_t i;

    len = BSP_UART_RxRead(s_rxBuf, BSP_UART_RX_BUF_SIZE);

    for (i = 0U; i < len; i++)
    {
        ProcessByte(s_rxBuf[i]);
    }
}
