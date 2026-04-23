/**
  * @file    app_sample.c
  * @brief   Oscilloscope sampling & wave generator
  *
  * Sampling and generator share TIM6 as a common tick source.
  * Each tick: generator write first (if running), then sampling read (if running).
  * TIM6 runs whenever at least one consumer is active; stops when both are idle.
  */

#include "app_sample.h"
#include "app_motor.h"
#include "bsp_tim.h"
#include "bsp_tick.h"
#include "bsp_uart.h"

#include <math.h>

/*--------------------------------------------------------------------------*/
/*                       Sampling internal state                           */
/*--------------------------------------------------------------------------*/

static uint8_t  s_sampling;
static uint8_t  s_channelMask;
static uint16_t s_channelRegMap[APP_SAMPLE_NUM_CHANNELS];

/* Stream frame TX buffer: [0xBB][mask][LEN][data up to 16 bytes][XOR] */
#define STREAM_FRAME_MAX_LEN    (1U + 1U + 1U + (APP_SAMPLE_NUM_CHANNELS * 2U) + 1U)
static uint8_t s_streamBuf[STREAM_FRAME_MAX_LEN];

/*--------------------------------------------------------------------------*/
/*                      Generator internal state                           */
/*--------------------------------------------------------------------------*/

typedef enum { GEN_NONE, GEN_LINEAR, GEN_COSINE } GenMode_t;

static GenMode_t s_genMode;
static uint16_t  s_genDivider;      /* tick divider: how many ticks per write   */
static uint16_t  s_genCounter;      /* current tick counter                     */

/* Linear mode */
static uint16_t s_linAddr;
static int16_t  s_linMin;
static int16_t  s_linMax;
static int16_t  s_linStep;
static int16_t  s_linCurrent;
static uint8_t  s_linAscending;
static uint16_t s_linIntervalMs;    /* saved for divider recalculation          */

/* Cosine mode */
static int16_t  s_cosAmplitude;
static int16_t  s_cosOffset;
static uint16_t s_cosFreqX100;
static uint8_t  s_cosChCount;
static uint16_t s_cosAddr[APP_GEN_COS_MAX_CH];
static int16_t  s_cosPhaseX10[APP_GEN_COS_MAX_CH];
static uint32_t s_cosStartTick;     /* ms time base from BSP_GetTick()         */

/*--------------------------------------------------------------------------*/
/*                   Shared TIM6 start / stop                              */
/*--------------------------------------------------------------------------*/

/**
  * @brief  Ensure TIM6 is running. Safe to call when already running.
  */
static void EnsureTimRunning(void)
{
    BSP_SampleTim_Start();
}

/**
  * @brief  Stop TIM6 only when no consumer needs it.
  *         Call after clearing your own "active" flag.
  */
static void StopTimIfIdle(void)
{
    if ((s_sampling == 0U) && (s_genMode == GEN_NONE))
    {
        BSP_SampleTim_Stop();
    }
}

/*--------------------------------------------------------------------------*/
/*                      Generator internal helpers                         */
/*--------------------------------------------------------------------------*/

/**
  * @brief  Calculate tick divider from intervalMs and current TIM6 period.
  *         Uses rounding to nearest integer for best approximation.
  *         Result >= 1 (at least one write per tick).
  */
static uint16_t Generator_CalcDivider(uint16_t intervalMs)
{
    uint32_t periodUs   = (uint32_t)BSP_SampleTim_GetPeriodUs();
    uint32_t intervalUs = (uint32_t)intervalMs * 1000U;
    uint16_t divider    = (uint16_t)((intervalUs + periodUs / 2U) / periodUs);
    return (divider < 1U) ? 1U : divider;
}

static void Generator_DoLinearWrite(void)
{
    /* Write current value */
    (void)App_Motor_WriteReg(s_linAddr, (uint16_t)s_linCurrent);

    /* Advance to next value */
    if (s_linAscending != 0U)
    {
        int32_t next = (int32_t)s_linCurrent + (int32_t)s_linStep;
        if (next > (int32_t)s_linMax)
        {
            s_linCurrent   = s_linMax;
            s_linAscending = 0U;
        }
        else
        {
            s_linCurrent = (int16_t)next;
        }
    }
    else
    {
        int32_t next = (int32_t)s_linCurrent - (int32_t)s_linStep;
        if (next < (int32_t)s_linMin)
        {
            s_linCurrent   = s_linMin;
            s_linAscending = 1U;
        }
        else
        {
            s_linCurrent = (int16_t)next;
        }
    }
}

static void Generator_DoCosineWrite(void)
{
    uint32_t elapsedMs = BSP_GetTick() - s_cosStartTick;
    float    tSec      = (float)elapsedMs * 0.001f;
    float    freqHz    = (float)s_cosFreqX100 * 0.01f;
    uint8_t  i;

    for (i = 0U; i < s_cosChCount; i++)
    {
        float phaseRad = (float)s_cosPhaseX10[i] * 0.1f * 3.14159265f / 180.0f;
        float rawValue = (float)s_cosOffset +
                         (float)s_cosAmplitude *
                         cosf(6.28318530f * freqHz * tSec + phaseRad);

        /* Clamp to int16 range */
        int32_t clamped;
        if (rawValue > 32767.0f)        { clamped = 32767; }
        else if (rawValue < -32768.0f)  { clamped = -32768; }
        else                            { clamped = (int32_t)rawValue; }

        (void)App_Motor_WriteReg(s_cosAddr[i], (uint16_t)(int16_t)clamped);
    }
}

static void Generator_DoWrite(void)
{
    if (s_genMode == GEN_LINEAR)
    {
        Generator_DoLinearWrite();
    }
    else if (s_genMode == GEN_COSINE)
    {
        Generator_DoCosineWrite();
    }
}

/*--------------------------------------------------------------------------*/
/*                       Sampling internal helpers                         */
/*--------------------------------------------------------------------------*/

/**
  * @brief  Build and transmit one data stream frame for the current sample.
  * @param  effectiveMask  Bitmask of channels to include.
  */
static void SendStreamFrame(uint8_t effectiveMask)
{
    uint8_t  idx = 0U;
    uint8_t  ch;
    uint8_t  xorVal = 0U;
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

    /* XOR: effectiveMask XOR LEN XOR all data bytes */
    xorVal = effectiveMask ^ len;
    for (i = dataStart; i < idx; i++)
    {
        xorVal ^= s_streamBuf[i];
    }
    s_streamBuf[idx++] = xorVal;

    (void)BSP_UART_Transmit(s_streamBuf, idx);
}

/*--------------------------------------------------------------------------*/
/*                       Sampling public API                               */
/*--------------------------------------------------------------------------*/

/**
  * @brief  Initialize sampling and generator state. Call once before main loop.
  */
void App_Sample_Init(void)
{
    uint8_t i;

    s_sampling    = 0U;
    s_channelMask = 0x01U;

    for (i = 0U; i < APP_SAMPLE_NUM_CHANNELS; i++)
    {
        s_channelRegMap[i] = APP_SAMPLE_REG_UNMAPPED;
    }

    /* Generator state */
    s_genMode    = GEN_NONE;
    s_genDivider = 1U;
    s_genCounter = 0U;

    BSP_SampleTim_Init();
}

/**
  * @brief  Poll — called from main loop every iteration.
  *         Each TIM6 tick: generator write (phase 1) then sampling read (phase 2).
  */
void App_Sample_Poll(void)
{
    /* Nothing to do if both idle */
    if ((s_sampling == 0U) && (s_genMode == GEN_NONE))
    {
        return;
    }

    if (BSP_SampleTim_GetFlag() == 0U) { return; }
    BSP_SampleTim_ClearFlag();

    /* ---- Phase 1: Generator write (if running) ---- */
    if (s_genMode != GEN_NONE)
    {
        s_genCounter++;
        if (s_genCounter >= s_genDivider)
        {
            s_genCounter = 0U;
            Generator_DoWrite();
        }
    }

    /* ---- Phase 2: Sampling read (if running) ---- */
    if (s_sampling != 0U)
    {
        uint8_t effectiveMask;

        /* Skip frame if TX still busy */
        if (BSP_UART_IsTxBusy() != 0U) { return; }

        effectiveMask = App_Sample_GetEffectiveMask();
        if (effectiveMask == 0U)
        {
            App_Sample_Stop();
            return;
        }

        SendStreamFrame(effectiveMask);
    }
}

/**
  * @brief  Start sampling. Fails if no channel has a valid register mapping.
  */
ErrorStatus App_Sample_Start(void)
{
    if (App_Sample_GetEffectiveMask() == 0U)
    {
        return ERROR;
    }

    s_sampling = 1U;
    EnsureTimRunning();
    return SUCCESS;
}

/**
  * @brief  Stop sampling. Generator (if running) is not affected.
  */
void App_Sample_Stop(void)
{
    s_sampling = 0U;
    StopTimIfIdle();
}

/**
  * @brief  Set sampling interval by index (0~6).
  *         If linear generator is running, recalculates its tick divider.
  */
ErrorStatus App_Sample_SetInterval(uint8_t idx)
{
    ErrorStatus result = BSP_SampleTim_SetFreq(idx);

    if ((result == SUCCESS) && (s_genMode == GEN_LINEAR))
    {
        s_genDivider = Generator_CalcDivider(s_linIntervalMs);
    }

    return result;
}

ErrorStatus App_Sample_SetChannelMask(uint8_t mask)
{
    if (mask == 0U)
    {
        return ERROR;
    }
    s_channelMask = mask;
    return SUCCESS;
}

void App_Sample_SetRegMap(const uint8_t *pData)
{
    uint8_t i;
    for (i = 0U; i < APP_SAMPLE_NUM_CHANNELS; i++)
    {
        s_channelRegMap[i] = ((uint16_t)pData[i * 2U] << 8U)
                             | (uint16_t)pData[i * 2U + 1U];
    }
}

uint8_t App_Sample_IsActive(void)
{
    return s_sampling;
}

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

/*--------------------------------------------------------------------------*/
/*                       Generator public API                              */
/*--------------------------------------------------------------------------*/

ErrorStatus App_Generator_StartLinear(uint16_t addr, int16_t min, int16_t max,
                                      int16_t step, uint16_t intervalMs)
{
    /* Auto-stop previous generator */
    s_genMode = GEN_NONE;

    s_linAddr       = addr;
    s_linMin        = min;
    s_linMax        = max;
    s_linStep       = step;
    s_linCurrent    = min;
    s_linAscending  = 1U;
    s_linIntervalMs = intervalMs;

    s_genDivider = Generator_CalcDivider(intervalMs);
    s_genCounter = 0U;
    s_genMode    = GEN_LINEAR;

    EnsureTimRunning();
    return SUCCESS;
}

ErrorStatus App_Generator_StartCosine(int16_t amplitude, int16_t offset,
                                      uint16_t freqX100, uint8_t channelCount,
                                      const uint16_t *addrs, const int16_t *phaseX10)
{
    uint8_t i;

    /* Auto-stop previous generator */
    s_genMode = GEN_NONE;

    s_cosAmplitude = amplitude;
    s_cosOffset    = offset;
    s_cosFreqX100  = freqX100;
    s_cosChCount   = channelCount;

    for (i = 0U; i < channelCount; i++)
    {
        s_cosAddr[i]     = addrs[i];
        s_cosPhaseX10[i] = phaseX10[i];
    }

    s_cosStartTick = BSP_GetTick();

    /* Cosine writes every tick */
    s_genDivider = 1U;
    s_genCounter = 0U;
    s_genMode    = GEN_COSINE;

    EnsureTimRunning();
    return SUCCESS;
}

void App_Generator_Stop(void)
{
    s_genMode = GEN_NONE;
    StopTimIfIdle();
}

uint8_t App_Generator_IsRunning(void)
{
    return (s_genMode != GEN_NONE) ? 1U : 0U;
}
