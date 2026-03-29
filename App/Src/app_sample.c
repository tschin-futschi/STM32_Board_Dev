/**
  * @file    app_sample.c
  * @brief   Oscilloscope sampling — channel config, TIM6 trigger, stream frame TX
  */

#include "app_sample.h"
#include "app_motor.h"
#include "bsp_tim.h"
#include "bsp_uart.h"

/*--------------------------------------------------------------------------*/
/*                          Internal state                                  */
/*--------------------------------------------------------------------------*/

static uint8_t  s_sampling;
static uint8_t  s_channelMask;
static uint16_t s_channelRegMap[APP_SAMPLE_NUM_CHANNELS];

/* Stream frame TX buffer: [0xBB][mask][LEN][data up to 16 bytes][XOR] */
#define STREAM_FRAME_MAX_LEN    (1U + 1U + 1U + (APP_SAMPLE_NUM_CHANNELS * 2U) + 1U)
static uint8_t s_streamBuf[STREAM_FRAME_MAX_LEN];

/*--------------------------------------------------------------------------*/
/*                          Internal helpers                                */
/*--------------------------------------------------------------------------*/

/**
  * @brief  Build and transmit one data stream frame for the current sample.
  * @param  effectiveMask  Bitmask of channels to include (valid reg + enabled).
  */
static void SendStreamFrame(uint8_t effectiveMask)
{
    uint8_t  idx = 0U;
    uint8_t  ch;
    uint8_t  xor = 0U;
    uint8_t  len = 0U;
    uint16_t val;
    uint8_t  dataStart;
    uint8_t  i;

    /* Count active channels for LEN field */
    for (ch = 0U; ch < APP_SAMPLE_NUM_CHANNELS; ch++)
    {
        if ((effectiveMask & (uint8_t)(1U << ch)) != 0U)
        {
            len += 2U;
        }
    }

    s_streamBuf[idx++] = 0xBBU;         /* Stream frame SOF */
    s_streamBuf[idx++] = effectiveMask;
    s_streamBuf[idx++] = len;
    dataStart = idx;

    /* Fill channel data (low channel first) */
    for (ch = 0U; ch < APP_SAMPLE_NUM_CHANNELS; ch++)
    {
        if ((effectiveMask & (uint8_t)(1U << ch)) != 0U)
        {
            if (App_Motor_ReadReg(s_channelRegMap[ch], &val) != SUCCESS)
            {
                val = 0U;   /* Send 0 on read failure, keep stream alive */
            }
            s_streamBuf[idx++] = (uint8_t)(val >> 8U);
            s_streamBuf[idx++] = (uint8_t)(val & 0xFFU);
        }
    }

    /* XOR: effectiveMask XOR LEN XOR all data bytes (protocol.MD v1.4) */
    xor = effectiveMask ^ len;
    for (i = dataStart; i < idx; i++)
    {
        xor ^= s_streamBuf[i];
    }
    s_streamBuf[idx++] = xor;

    (void)BSP_UART_Transmit(s_streamBuf, idx);
}

/*--------------------------------------------------------------------------*/
/*                          Public API                                      */
/*--------------------------------------------------------------------------*/

/**
  * @brief  Initialize sampling state to defaults. Call once before main loop.
  */
void App_Sample_Init(void)
{
    uint8_t i;

    s_sampling      = 0U;
    s_channelMask   = 0x01U;                    /* Default: channel 0 only  */

    for (i = 0U; i < APP_SAMPLE_NUM_CHANNELS; i++)
    {
        s_channelRegMap[i] = APP_SAMPLE_REG_UNMAPPED;
    }

    BSP_SampleTim_Init();
}

/**
  * @brief  Poll sampling — call from main loop every iteration.
  *         Sends one stream frame per TIM6 interrupt tick.
  */
void App_Sample_Poll(void)
{
    uint8_t effectiveMask;

    if (s_sampling == 0U)               { return; }
    if (BSP_SampleTim_GetFlag() == 0U)  { return; }

    BSP_SampleTim_ClearFlag();

    effectiveMask = App_Sample_GetEffectiveMask();
    if (effectiveMask == 0U)
    {
        /* No valid channels — stop silently */
        App_Sample_Stop();
        return;
    }

    SendStreamFrame(effectiveMask);
}

/**
  * @brief  Start sampling. Fails if no channel has a valid register mapping.
  * @retval SUCCESS / ERROR
  */
ErrorStatus App_Sample_Start(void)
{
    if (App_Sample_GetEffectiveMask() == 0U)
    {
        return ERROR;
    }

    s_sampling = 1U;
    BSP_SampleTim_Start();
    return SUCCESS;
}

/**
  * @brief  Stop sampling immediately. Clears TIM6 and sample flag.
  */
void App_Sample_Stop(void)
{
    BSP_SampleTim_Stop();
    s_sampling = 0U;
}

/**
  * @brief  Set sampling interval by index (0~7).
  * @retval SUCCESS / ERROR (invalid index)
  */
ErrorStatus App_Sample_SetInterval(uint8_t idx)
{
    return BSP_SampleTim_SetFreq(idx);
}

/**
  * @brief  Set active channel mask.
  * @retval SUCCESS / ERROR (mask == 0)
  */
ErrorStatus App_Sample_SetChannelMask(uint8_t mask)
{
    if (mask == 0U)
    {
        return ERROR;
    }
    s_channelMask = mask;
    return SUCCESS;
}

/**
  * @brief  Set channel-to-register mapping from 16-byte payload.
  *         pData layout: [ch0_H][ch0_L][ch1_H][ch1_L]...[ch7_H][ch7_L]
  */
void App_Sample_SetRegMap(const uint8_t *pData)
{
    uint8_t i;
    for (i = 0U; i < APP_SAMPLE_NUM_CHANNELS; i++)
    {
        s_channelRegMap[i] = ((uint16_t)pData[i * 2U] << 8U)
                             | (uint16_t)pData[i * 2U + 1U];
    }
}

/**
  * @brief  Return 1 if sampling is active, 0 otherwise.
  */
uint8_t App_Sample_IsActive(void)
{
    return s_sampling;
}

/**
  * @brief  Return bitmask of channels that are both enabled and have a valid
  *         register mapping (reg != 0xFFFF).
  */
uint8_t App_Sample_GetEffectiveMask(void)
{
    uint8_t mask = 0U;
    uint8_t ch;

    for (ch = 0U; ch < APP_SAMPLE_NUM_CHANNELS; ch++)
    {
        if (((s_channelMask & (uint8_t)(1U << ch)) != 0U) &&
            (s_channelRegMap[ch] != APP_SAMPLE_REG_UNMAPPED))
        {
            mask |= (uint8_t)(1U << ch);
        }
    }
    return mask;
}
