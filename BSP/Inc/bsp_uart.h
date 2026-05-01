/**
  * @file    bsp_uart.h
  * @brief   UART BSP — USART1 RX interrupt + DMA TX
  */

#ifndef __BSP_UART_H
#define __BSP_UART_H

#include "stm32f4xx.h"

/*--------------------------------------------------------------------------*/
/*                          Hardware configuration                          */
/*--------------------------------------------------------------------------*/

#define BSP_UART_PERIPH             USART1
#define BSP_UART_CLK                RCC_APB2Periph_USART1
#define BSP_UART_BAUDRATE_DEFAULT   460800U

/* TX pin: PA9, AF7 */
#define BSP_UART_TX_GPIO_PORT       GPIOA
#define BSP_UART_TX_GPIO_CLK        RCC_AHB1Periph_GPIOA
#define BSP_UART_TX_PIN             GPIO_Pin_9
#define BSP_UART_TX_PIN_SOURCE      GPIO_PinSource9
#define BSP_UART_TX_AF              GPIO_AF_USART1

/* RX pin: PA10, AF7 */
#define BSP_UART_RX_GPIO_PORT       GPIOA
#define BSP_UART_RX_GPIO_CLK        RCC_AHB1Periph_GPIOA
#define BSP_UART_RX_PIN             GPIO_Pin_10
#define BSP_UART_RX_PIN_SOURCE      GPIO_PinSource10
#define BSP_UART_RX_AF              GPIO_AF_USART1

/* DMA TX: DMA2 Stream7 Channel4 */
#define BSP_UART_DMA_PERIPH         DMA2
#define BSP_UART_DMA_CLK            RCC_AHB1Periph_DMA2
#define BSP_UART_DMA_STREAM         DMA2_Stream7
#define BSP_UART_DMA_CHANNEL        DMA_Channel_4
#define BSP_UART_DMA_IRQn           DMA2_Stream7_IRQn
#define BSP_UART_DMA_IRQ_PRIORITY   10U
/* All status flags for DMA2 Stream7 (used when clearing after disable) */
#define BSP_UART_TX_DMA_FLAGS       (DMA_FLAG_TCIF7 | DMA_FLAG_HTIF7 | \
                                     DMA_FLAG_TEIF7 | DMA_FLAG_DMEIF7 | \
                                     DMA_FLAG_FEIF7)

/* USART1 RX interrupt priority */
#define BSP_UART_IRQn               USART1_IRQn
#define BSP_UART_IRQ_PRIORITY       2U

/* Buffer sizes */
#define BSP_UART_RX_BUF_SIZE        512U
#define BSP_UART_TX_BUF_SIZE        264U    /* >= PROTO_MAX_FRAME_LEN (262) */

/* TxWait timeout: 50ms is sufficient at all supported baudrates */
#define BSP_UART_TX_WAIT_TIMEOUT_MS 50U

/*--------------------------------------------------------------------------*/
/*                               API                                        */
/*--------------------------------------------------------------------------*/

ErrorStatus BSP_UART_Init(void);
ErrorStatus BSP_UART_Transmit(const uint8_t *pData, uint16_t len);
ErrorStatus BSP_UART_SetBaudrate(uint32_t baudrate);
void        BSP_UART_TxWait(void);      /* Block until DMA TX done or timeout */
uint8_t     BSP_UART_IsTxBusy(void);   /* Non-blocking: 1 = TX in progress   */

/* Called from ISR — not for application use */
void BSP_UART_RxISR_Callback(void);
void BSP_UART_TxDmaISR_Callback(void);

/* Called by App layer to read received bytes */
uint16_t BSP_UART_RxRead(uint8_t *pBuf, uint16_t maxLen);

/* Discard all bytes currently in RX ring buffer */
void BSP_UART_RxFlush(void);

/* Get and clear RX overflow count (bytes lost when buffer was full) */
uint32_t BSP_UART_GetAndClearOverflowCount(void);

#endif /* __BSP_UART_H */
