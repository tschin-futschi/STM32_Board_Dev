/**
  * @file    bsp_tim.c
  * @brief   Sampling timer BSP — TIM6 periodic interrupt
  *
  * TIM6 clock = APB1 × 2 = 90 MHz
  * PSC = 89  → counter clock = 1 MHz (1 count = 1 us)
  * ARR       → interrupt period = (ARR + 1) us
  */

#include "bsp_tim.h"

/*--------------------------------------------------------------------------*/
/*                          Sampling interval table                         */
/*--------------------------------------------------------------------------*/

/* ARR = interval_us - 1 */
static const uint16_t k_arrTable[BSP_SAMPLE_TIM_IDX_MAX + 1U] =
{
    199U,   /* idx 0:  200 us,  5000 Hz */
    299U,   /* idx 1:  300 us,  3333 Hz */
    499U,   /* idx 2:  500 us,  2000 Hz */
    749U,   /* idx 3:  750 us,  1333 Hz */
    999U,   /* idx 4: 1000 us,  1000 Hz (default) */
   1499U,   /* idx 5: 1500 us,   667 Hz */
   1999U,   /* idx 6: 2000 us,   500 Hz */
};

/*--------------------------------------------------------------------------*/
/*                          Internal state                                  */
/*--------------------------------------------------------------------------*/

static volatile uint8_t s_sampleFlag;
static uint8_t s_currentIdx = BSP_SAMPLE_TIM_DEFAULT_IDX;

/*--------------------------------------------------------------------------*/
/*                          Public API                                      */
/*--------------------------------------------------------------------------*/

/**
  * @brief  Initialize TIM6 with default sampling interval (index 4 = 1000 us).
  *         Timer is NOT started — call BSP_SampleTim_Start() explicitly.
  */
ErrorStatus BSP_SampleTim_Init(void)
{
    TIM_TimeBaseInitTypeDef timInit;
    NVIC_InitTypeDef        nvicInit;

    /* Enable TIM6 clock */
    RCC_APB1PeriphClockCmd(BSP_SAMPLE_TIM_RCC_APB1, ENABLE);

    /* Configure TIM6: PSC=89 → 1 MHz, ARR = default interval */
    TIM_TimeBaseStructInit(&timInit);
    timInit.TIM_Prescaler         = BSP_SAMPLE_TIM_PSC;
    timInit.TIM_Period             = k_arrTable[BSP_SAMPLE_TIM_DEFAULT_IDX];
    timInit.TIM_CounterMode        = TIM_CounterMode_Up;
    timInit.TIM_ClockDivision      = TIM_CKD_DIV1;
    TIM_TimeBaseInit(BSP_SAMPLE_TIM_PERIPH, &timInit);

    /* Clear any pending update flag from init */
    TIM_ClearFlag(BSP_SAMPLE_TIM_PERIPH, TIM_FLAG_Update);

    /* Configure NVIC */
    nvicInit.NVIC_IRQChannel                   = BSP_SAMPLE_TIM_IRQn;
    nvicInit.NVIC_IRQChannelPreemptionPriority = BSP_SAMPLE_TIM_IRQ_PRIORITY;
    nvicInit.NVIC_IRQChannelSubPriority        = 0U;
    nvicInit.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvicInit);

    s_sampleFlag = 0U;

    return SUCCESS;
}

/**
  * @brief  Set sampling interval by index.
  * @param  idx  Interval index, 0~6.
  * @retval SUCCESS / ERROR (invalid index)
  */
ErrorStatus BSP_SampleTim_SetFreq(uint8_t idx)
{
    if (idx > BSP_SAMPLE_TIM_IDX_MAX)
    {
        return ERROR;
    }
    TIM_SetAutoreload(BSP_SAMPLE_TIM_PERIPH, k_arrTable[idx]);
    s_currentIdx = idx;
    return SUCCESS;
}

/**
  * @brief  Return current tick period in microseconds.
  */
uint16_t BSP_SampleTim_GetPeriodUs(void)
{
    return k_arrTable[s_currentIdx] + 1U;
}

/**
  * @brief  Start TIM6: enable update interrupt and counter.
  */
void BSP_SampleTim_Start(void)
{
    TIM_ClearFlag(BSP_SAMPLE_TIM_PERIPH, TIM_FLAG_Update);
    s_sampleFlag = 0U;
    TIM_ITConfig(BSP_SAMPLE_TIM_PERIPH, TIM_IT_Update, ENABLE);
    TIM_Cmd(BSP_SAMPLE_TIM_PERIPH, ENABLE);
}

/**
  * @brief  Stop TIM6: disable counter and update interrupt.
  */
void BSP_SampleTim_Stop(void)
{
    TIM_Cmd(BSP_SAMPLE_TIM_PERIPH, DISABLE);
    TIM_ITConfig(BSP_SAMPLE_TIM_PERIPH, TIM_IT_Update, DISABLE);
    TIM_ClearFlag(BSP_SAMPLE_TIM_PERIPH, TIM_FLAG_Update);
    s_sampleFlag = 0U;
}

/**
  * @brief  Return current sample flag (1 = new sample due).
  */
uint8_t BSP_SampleTim_GetFlag(void)
{
    return s_sampleFlag;
}

/**
  * @brief  Clear sample flag after sample has been processed.
  */
void BSP_SampleTim_ClearFlag(void)
{
    s_sampleFlag = 0U;
}

/**
  * @brief  Called from TIM6_DAC_IRQHandler — set sample flag only.
  */
void BSP_SampleTim_ISR_Callback(void)
{
    s_sampleFlag = 1U;
}
