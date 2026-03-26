/**
  * @file    bsp_uart.c
  * @brief   UART BSP — USART1 RX interrupt + DMA TX
  */

#include "bsp_uart.h"
#include <stddef.h>

/*--------------------------------------------------------------------------*/
/*                          Private variables                               */
/*--------------------------------------------------------------------------*/

/* RX ring buffer */
static uint8_t  s_rxBuf[BSP_UART_RX_BUF_SIZE];
static volatile uint16_t s_rxHead = 0U;   /* written by ISR */
static volatile uint16_t s_rxTail = 0U;   /* read  by main  */

/* TX DMA buffer — must be in SRAM1 (static, not CCM) */
static uint8_t  s_txBuf[BSP_UART_TX_BUF_SIZE];
static volatile uint8_t s_txDone = 1U;    /* 1 = idle, 0 = busy */

/*--------------------------------------------------------------------------*/
/*                          Private helpers                                 */
/*--------------------------------------------------------------------------*/

static void uart_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    RCC_AHB1PeriphClockCmd(BSP_UART_TX_GPIO_CLK | BSP_UART_RX_GPIO_CLK, ENABLE);

    /* TX: PA9 */
    gpio.GPIO_Pin   = BSP_UART_TX_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_AF;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(BSP_UART_TX_GPIO_PORT, &gpio);
    GPIO_PinAFConfig(BSP_UART_TX_GPIO_PORT, BSP_UART_TX_PIN_SOURCE, BSP_UART_TX_AF);

    /* RX: PA10 */
    gpio.GPIO_Pin  = BSP_UART_RX_PIN;
    GPIO_Init(BSP_UART_RX_GPIO_PORT, &gpio);
    GPIO_PinAFConfig(BSP_UART_RX_GPIO_PORT, BSP_UART_RX_PIN_SOURCE, BSP_UART_RX_AF);
}

static void uart_periph_init(uint32_t baudrate)
{
    USART_InitTypeDef usart = {0};

    RCC_APB2PeriphClockCmd(BSP_UART_CLK, ENABLE);

    usart.USART_BaudRate            = baudrate;
    usart.USART_WordLength          = USART_WordLength_8b;
    usart.USART_StopBits            = USART_StopBits_1;
    usart.USART_Parity              = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(BSP_UART_PERIPH, &usart);

    /* Enable RXNE interrupt */
    USART_ITConfig(BSP_UART_PERIPH, USART_IT_RXNE, ENABLE);

    /* Enable DMA TX request */
    USART_DMACmd(BSP_UART_PERIPH, USART_DMAReq_Tx, ENABLE);

    USART_Cmd(BSP_UART_PERIPH, ENABLE);
}

static void uart_nvic_init(void)
{
    NVIC_InitTypeDef nvic = {0};

    /* USART1 RX */
    nvic.NVIC_IRQChannel                   = BSP_UART_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = BSP_UART_IRQ_PRIORITY;
    nvic.NVIC_IRQChannelSubPriority        = 0U;
    nvic.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic);

    /* DMA2 Stream7 TX complete */
    nvic.NVIC_IRQChannel                   = BSP_UART_DMA_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = BSP_UART_IRQ_PRIORITY;
    nvic.NVIC_IRQChannelSubPriority        = 0U;
    nvic.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic);
}

static void uart_dma_init(void)
{
    DMA_InitTypeDef dma = {0};

    RCC_AHB1PeriphClockCmd(BSP_UART_DMA_CLK, ENABLE);

    DMA_DeInit(BSP_UART_DMA_STREAM);

    dma.DMA_Channel            = BSP_UART_DMA_CHANNEL;
    dma.DMA_PeripheralBaseAddr = (uint32_t)(&BSP_UART_PERIPH->DR);
    dma.DMA_Memory0BaseAddr    = (uint32_t)s_txBuf;
    dma.DMA_DIR                = DMA_DIR_MemoryToPeripheral;
    dma.DMA_BufferSize         = 1U;
    dma.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    dma.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
    dma.DMA_Mode               = DMA_Mode_Normal;
    dma.DMA_Priority           = DMA_Priority_Low;
    dma.DMA_FIFOMode           = DMA_FIFOMode_Disable;
    DMA_Init(BSP_UART_DMA_STREAM, &dma);

    /* Enable Transfer Complete interrupt */
    DMA_ITConfig(BSP_UART_DMA_STREAM, DMA_IT_TC, ENABLE);
}

/*--------------------------------------------------------------------------*/
/*                          Public API                                      */
/*--------------------------------------------------------------------------*/

ErrorStatus BSP_UART_Init(void)
{
    uart_gpio_init();
    uart_dma_init();
    uart_periph_init(BSP_UART_BAUDRATE_DEFAULT);
    uart_nvic_init();
    return SUCCESS;
}

ErrorStatus BSP_UART_Transmit(const uint8_t *pData, uint16_t len)
{
    if (pData == NULL || len == 0U || len > BSP_UART_TX_BUF_SIZE)
    {
        return ERROR;
    }

    /* Check if previous TX is still in progress */
    if (s_txDone == 0U)
    {
        return ERROR;   /* BUSY — caller retries next poll */
    }

    s_txDone = 0U;

    /* Copy to DMA buffer */
    uint16_t i;
    for (i = 0U; i < len; i++)
    {
        s_txBuf[i] = pData[i];
    }

    /* Reconfigure DMA transfer size and start */
    DMA_Cmd(BSP_UART_DMA_STREAM, DISABLE);
    BSP_UART_DMA_STREAM->NDTR = len;
    DMA_Cmd(BSP_UART_DMA_STREAM, ENABLE);

    return SUCCESS;
}

ErrorStatus BSP_UART_SetBaudrate(uint32_t baudrate)
{
    USART_InitTypeDef usart = {0};

    usart.USART_BaudRate            = baudrate;
    usart.USART_WordLength          = USART_WordLength_8b;
    usart.USART_StopBits            = USART_StopBits_1;
    usart.USART_Parity              = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;

    USART_Cmd(BSP_UART_PERIPH, DISABLE);
    USART_Init(BSP_UART_PERIPH, &usart);
    USART_Cmd(BSP_UART_PERIPH, ENABLE);

    return SUCCESS;
}

/*--------------------------------------------------------------------------*/
/*                          ISR callbacks                                   */
/*--------------------------------------------------------------------------*/

void BSP_UART_RxISR_Callback(void)
{
    uint8_t  byte    = (uint8_t)USART_ReceiveData(BSP_UART_PERIPH);
    uint16_t nextHead = (s_rxHead + 1U) % BSP_UART_RX_BUF_SIZE;

    if (nextHead != s_rxTail)   /* not full — discard if full */
    {
        s_rxBuf[s_rxHead] = byte;
        s_rxHead = nextHead;
    }
}

void BSP_UART_TxDmaISR_Callback(void)
{
    s_txDone = 1U;
}

/*--------------------------------------------------------------------------*/
/*                          RX read (App layer)                             */
/*--------------------------------------------------------------------------*/

uint16_t BSP_UART_RxRead(uint8_t *pBuf, uint16_t maxLen)
{
    uint16_t count = 0U;

    while (count < maxLen)
    {
        uint16_t head;

        __disable_irq();
        head = s_rxHead;
        __enable_irq();

        if (s_rxTail == head)
        {
            break;  /* buffer empty */
        }

        pBuf[count] = s_rxBuf[s_rxTail];
        s_rxTail = (s_rxTail + 1U) % BSP_UART_RX_BUF_SIZE;
        count++;
    }

    return count;
}
