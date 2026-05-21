/**
  * @file    stm32f4xx_it.c
  * @brief   All interrupt service routines
  */

#include "stm32f4xx_it.h"
#include "bsp_tick.h"
#include "bsp_uart.h"
#include "bsp_pmic.h"
#include "bsp_tim.h"

/* HardFault LED blink delay: ~50 ms at 180 MHz with -Og.
 * Pure CPU loop because SysTick may be inconsistent during fault.
 * Frequency chosen to be distinct from startup-failure blinks (100~1600 ms). */
#define HARDFAULT_BLINK_LOOPS  300000U

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

    /* Blink at ~50ms — unmistakable fault indicator, distinct from startup blinks */
    while (1)
    {
        GPIOF->ODR ^= (1U << 13);
        for (i = 0; i < HARDFAULT_BLINK_LOOPS; i++) {}
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

void TIM6_DAC_IRQHandler(void)
{
    if (TIM_GetITStatus(BSP_SAMPLE_TIM_PERIPH, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(BSP_SAMPLE_TIM_PERIPH, TIM_IT_Update);
        BSP_SampleTim_ISR_Callback();
    }
}

void EXTI4_IRQHandler(void)
{
    if (EXTI_GetITStatus(BSP_PMIC_HWEN_EXTI_LINE) != RESET)
    {
        EXTI_ClearITPendingBit(BSP_PMIC_HWEN_EXTI_LINE);

        /* Re-sample to filter spurious edges (EMI glitch); not true debounce */
        if (GPIO_ReadInputDataBit(BSP_PMIC_HWEN_GPIO_PORT, BSP_PMIC_HWEN_PIN) != RESET)
        {
            g_pmicHwenFlag = 1U;
        }
    }
}
