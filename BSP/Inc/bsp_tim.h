/**
  * @file    bsp_tim.h
  * @brief   Sampling timer BSP — TIM6, APB1×2 = 90 MHz
  *
  * PSC = 89  → counter clock = 1 MHz
  * ARR       → overflow period = (ARR + 1) us
  * ISR name  → TIM6_DAC_IRQHandler
  */

#ifndef __BSP_TIM_H
#define __BSP_TIM_H

#include "stm32f4xx.h"

/*--------------------------------------------------------------------------*/
/*                          Hardware configuration                          */
/*--------------------------------------------------------------------------*/

#define BSP_SAMPLE_TIM_PERIPH           TIM6
#define BSP_SAMPLE_TIM_RCC_APB1         RCC_APB1Periph_TIM6
#define BSP_SAMPLE_TIM_IRQn             TIM6_DAC_IRQn
#define BSP_SAMPLE_TIM_IRQ_PRIORITY     7U

/* PSC = 89: 90 MHz / 90 = 1 MHz counter clock, 1 count = 1 us */
#define BSP_SAMPLE_TIM_PSC              89U

/* Default sampling interval index (1000 us = 1 kHz) */
#define BSP_SAMPLE_TIM_DEFAULT_IDX      4U

/* Maximum sampling interval index (7 options: 0~6) */
#define BSP_SAMPLE_TIM_IDX_MAX          6U

/*--------------------------------------------------------------------------*/
/*                       Sampling interval table                            */
/*--------------------------------------------------------------------------*/

/*
 * ARR values for each index (ARR = interval_us - 1):
 *   idx 0:  200 us → ARR =  199,  5000 Hz
 *   idx 1:  300 us → ARR =  299,  3333 Hz
 *   idx 2:  500 us → ARR =  499,  2000 Hz
 *   idx 3:  750 us → ARR =  749,  1333 Hz
 *   idx 4: 1000 us → ARR =  999,  1000 Hz (default)
 *   idx 5: 1500 us → ARR = 1499,   667 Hz
 *   idx 6: 2000 us → ARR = 1999,   500 Hz
 */

/*--------------------------------------------------------------------------*/
/*                               API                                        */
/*--------------------------------------------------------------------------*/

ErrorStatus BSP_SampleTim_Init(void);
ErrorStatus BSP_SampleTim_SetFreq(uint8_t idx);
void        BSP_SampleTim_Start(void);
void        BSP_SampleTim_Stop(void);

uint8_t     BSP_SampleTim_GetFlag(void);
void        BSP_SampleTim_ClearFlag(void);
uint16_t    BSP_SampleTim_GetPeriodUs(void);

/* Called from ISR only */
void        BSP_SampleTim_ISR_Callback(void);

#endif /* __BSP_TIM_H */
