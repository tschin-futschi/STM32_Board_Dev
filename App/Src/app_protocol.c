/**
  * @file    app_protocol.c
  * @brief   Protocol application layer — echo mode for Stage 2 validation
  */

#include "app_protocol.h"
#include "bsp_uart.h"

#define PROTO_ECHO_BUF_SIZE     BSP_UART_RX_BUF_SIZE

void App_Protocol_Init(void)
{
    /* Reserved for future protocol initialization */
}

void App_Protocol_Poll(void)
{
    static uint8_t s_echoBuf[PROTO_ECHO_BUF_SIZE];
    uint16_t len;

    len = BSP_UART_RxRead(s_echoBuf, PROTO_ECHO_BUF_SIZE);

    if (len > 0U)
    {
        /* Echo back — retry handled next poll if TX busy */
        BSP_UART_Transmit(s_echoBuf, len);
    }
}
