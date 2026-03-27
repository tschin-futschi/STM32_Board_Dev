/**
  * @file    stm32f4xx_it.c
  * @brief   All interrupt service routines
  */

#include "stm32f4xx_it.h"
#include "bsp_tick.h"
#include "bsp_uart.h"
#include "bsp_pmic.h"

/*--------------------------------------------------------------------------*/
/*                    Cortex-M4 Processor Exception Handlers                */
/*--------------------------------------------------------------------------*/

void NMI_Handler(void)
{
}

void HardFault_Handler(void)
{
    volatile uint32_t i;

    /* Enable GPIOF clock — RCC AHB1ENR bit 5 */
    RCC->AHB1ENR |= (1U << 5);
    /* Brief delay for clock to stabilize */
    (void)RCC->AHB1ENR;

    /* PF13 as push-pull output: MODER[27:26] = 01 */
    GPIOF->MODER &= ~(3U << (13 * 2));
    GPIOF->MODER |=  (1U << (13 * 2));
    GPIOF->OTYPER &= ~(1U << 13);

    /* Fast blink ~100ms interval — unmistakable fault indicator */
    while (1)
    {
        GPIOF->ODR ^= (1U << 13);
        for (i = 0; i < 600000; i++) {}
    }
}

void MemManage_Handler(void)
{
    while (1) {}
}

void BusFault_Handler(void)
{
    while (1) {}
}

void UsageFault_Handler(void)
{
    while (1) {}
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

void SysTick_Handler(void)
{
    BSP_Tick_Increment();
}

void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(BSP_UART_PERIPH, USART_IT_RXNE) != RESET)
    {
        BSP_UART_RxISR_Callback();
        USART_ClearITPendingBit(BSP_UART_PERIPH, USART_IT_RXNE);
    }
}

void DMA2_Stream7_IRQHandler(void)
{
    if (DMA_GetITStatus(BSP_UART_DMA_STREAM, DMA_IT_TCIF7) != RESET)
    {
        BSP_UART_TxDmaISR_Callback();
        DMA_ClearITPendingBit(BSP_UART_DMA_STREAM, DMA_IT_TCIF7);
    }
}

void EXTI4_IRQHandler(void)
{
    if (EXTI_GetITStatus(BSP_PMIC_HWEN_EXTI_LINE) != RESET)
    {
        EXTI_ClearITPendingBit(BSP_PMIC_HWEN_EXTI_LINE);

        /* Only set flag if HWEN is still high (basic debounce) */
        if (GPIO_ReadInputDataBit(BSP_PMIC_HWEN_GPIO_PORT, BSP_PMIC_HWEN_PIN) != RESET)
        {
            g_pmicHwenFlag = 1U;
        }
    }
}
