/**
  * @file    app_sample.c
  * @brief   Oscilloscope sampling & wave generator
  *
  * Sampling and generator share TIM6 as a common tick source.
  * I2C reads and generator writes execute in the TIM6 ISR for precise timing.
  * Data is stored in a double buffer; main loop only handles UART transmission.
  * TIM6 runs whenever at least one consumer is active; stops when both are idle.
  */

#include "app_sample.h"
#include "app_motor.h"
#include "app_protocol.h"
#include "bsp_tim.h"
#include "bsp_uart.h"

#include <math.h>

/*--------------------------------------------------------------------------*/
/*                       Sampling internal state                           */
/*--------------------------------------------------------------------------*/

#define I2C_FAIL_WARN_THRESHOLD   10U   /* consecutive all-fail ticks → 0x06 warning  */
#define I2C_FAIL_STOP_THRESHOLD   50U   /* consecutive all-fail ticks → auto-stop      */

static volatile uint8_t  s_sampling;
static volatile uint8_t  s_channelMask;
static volatile uint16_t s_channelRegMap[APP_SAMPLE_NUM_CHANNELS];
static uint16_t s_i2cFailCount;            /* consecutive all-channel I2C failure count */
static uint8_t  s_i2cWarnSent;             /* 1 = warning 0x06 already sent            */

/* Stream frame TX buffer: [0xBB][mask][LEN][data up to 16 bytes][XOR] */
#define STREAM_FRAME_MAX_LEN    (1U + 1U + 1U + (APP_SAMPLE_NUM_CHANNELS * 2U) + 1U)
static uint8_t s_streamBuf[STREAM_FRAME_MAX_LEN];

/* Double buffer for ISR → main loop data transfer */
static volatile uint16_t s_isrBuf[2][APP_SAMPLE_NUM_CHANNELS];
static volatile uint8_t  s_isrWriteIdx;        /* which buffer ISR writes to      */
static volatile uint8_t  s_isrDataReady;       /* 1 = new sample data available   */
static volatile uint8_t  s_isrEffectiveMask;   /* channel mask captured in ISR    */
static volatile uint8_t  s_isrFailCount;       /* I2C failures in last ISR sample */
static volatile uint8_t  s_isrTotalCount;      /* total channels in last ISR sample */

/*--------------------------------------------------------------------------*/
/*                      Generator internal state                           */
/*--------------------------------------------------------------------------*/

typedef enum { GEN_NONE, GEN_LINEAR, GEN_COSINE, GEN_SAWTOOTH } GenMode_t;

static volatile GenMode_t s_genMode;
static volatile uint16_t  s_genDivider;      /* tick divider: how many ticks per write   */
static volatile uint16_t  s_genCounter;      /* current tick counter                     */

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
static uint8_t  s_cosChCount;
static uint16_t s_cosAddr[APP_GEN_COS_MAX_CH];
static int16_t  s_cosPhaseX10[APP_GEN_COS_MAX_CH];
static float    s_cosPhaseAccum;    /* base phase accumulator (radians)         */
static float    s_cosDPhase;        /* phase increment per tick (radians)       */
static uint16_t s_cosFreqX100;     /* saved freq (Hz×100) for divider recalc   */

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

static void Generator_DoSawtoothWrite(void)
{
    /* Write current value */
    (void)App_Motor_WriteReg(s_linAddr, (uint16_t)s_linCurrent);

    /* Advance — same triangle logic as linear */
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
    static const float k_twoPi = 6.28318530f;
    uint8_t i;

    for (i = 0U; i < s_cosChCount; i++)
    {
        float phaseRad = (float)s_cosPhaseX10[i] * (3.14159265f / 1800.0f);
        float rawValue = (float)s_cosOffset +
                         (float)s_cosAmplitude *
                         cosf(s_cosPhaseAccum + phaseRad);

        /* Clamp to int16 range */
        int32_t clamped;
        if (rawValue > 32767.0f)        { clamped = 32767; }
        else if (rawValue < -32768.0f)  { clamped = -32768; }
        else                            { clamped = (int32_t)rawValue; }

        (void)App_Motor_WriteReg(s_cosAddr[i], (uint16_t)(int16_t)clamped);
    }

    /* Advance phase, wrap at 2π to prevent float precision loss */
    s_cosPhaseAccum += s_cosDPhase;
    if (s_cosPhaseAccum >= k_twoPi)
    {
        s_cosPhaseAccum -= k_twoPi;
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
    else if (s_genMode == GEN_SAWTOOTH)
    {
        Generator_DoSawtoothWrite();
    }
}

/*--------------------------------------------------------------------------*/
/*                     ISR callback — time-critical sampling               */
/*--------------------------------------------------------------------------*/

/**
  * @brief  Called from TIM6 ISR. Performs generator write and sampling I2C
  *         reads at hardware-precise timing. Data is stored in double buffer
  *         for main loop to send via UART.
  */
static void SampleTimerISR(void)
{
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
        uint8_t ch;
        uint8_t bufIdx    = s_isrWriteIdx;
        uint8_t failCnt   = 0U;
        uint8_t totalCnt  = 0U;
        uint8_t mask      = 0U;
        uint16_t val;

        /* Compute effective mask (channelMask & mapped channels) */
        for (ch = 0U; ch < APP_SAMPLE_NUM_CHANNELS; ch++)
        {
            if (((s_channelMask & (uint8_t)(1U << ch)) != 0U) &&
                (s_channelRegMap[ch] != APP_SAMPLE_REG_UNMAPPED))
            {
                mask |= (uint8_t)(1U << ch);
            }
        }

        /* Read each active channel via I2C */
        for (ch = 0U; ch < APP_SAMPLE_NUM_CHANNELS; ch++)
        {
            if ((mask & (uint8_t)(1U << ch)) != 0U)
            {
                totalCnt++;
                if (App_Motor_ReadReg(s_channelRegMap[ch], &val) != SUCCESS)
                {
                    val = 0U;
                    failCnt++;
                }
                s_isrBuf[bufIdx][ch] = val;
            }
        }

        s_isrEffectiveMask = mask;
        s_isrFailCount     = failCnt;
        s_isrTotalCount    = totalCnt;
        s_isrWriteIdx      = bufIdx ^ 1U;   /* swap buffer for next ISR */
        s_isrDataReady     = 1U;
    }
}

/*--------------------------------------------------------------------------*/
/*                       Sampling internal helpers                         */
/*--------------------------------------------------------------------------*/

/**
  * @brief  Build and transmit one data stream frame from ISR buffer.
  * @param  effectiveMask  Bitmask of channels included.
  * @param  pData          Pointer to ISR sample buffer (one row).
  * @param  failCount      Number of I2C failures in this sample.
  * @param  totalCount     Total active channels in this sample.
  */
static void SendStreamFrame(uint8_t effectiveMask, const volatile uint16_t *pData,
                             uint8_t failCount, uint8_t totalCount)
{
    uint8_t idx = 0U;
    uint8_t ch;
    uint8_t xorVal = 0U;
    uint8_t len = 0U;
    uint8_t dataStart;
    uint8_t i;

    /* Count active channels for LEN field */
    for (ch = 0U; ch < APP_SAMPLE_NUM_CHANNELS; ch++)
    {
        if ((effectiveMask & (uint8_t)(1U << ch)) != 0U)
        {
            len += 2U;
        }
    }

    s_streamBuf[idx++] = 0xBBU;
    s_streamBuf[idx++] = effectiveMask;
    s_streamBuf[idx++] = len;
    dataStart = idx;

    /* Fill channel data from ISR buffer */
    for (ch = 0U; ch < APP_SAMPLE_NUM_CHANNELS; ch++)
    {
        if ((effectiveMask & (uint8_t)(1U << ch)) != 0U)
        {
            uint16_t val = pData[ch];
            s_streamBuf[idx++] = (uint8_t)(val >> 8U);
            s_streamBuf[idx++] = (uint8_t)(val & 0xFFU);
        }
    }

    /* I2C fault detection: track consecutive all-channel failures */
    if (failCount >= totalCount)
    {
        s_i2cFailCount++;
        if (s_i2cFailCount >= I2C_FAIL_STOP_THRESHOLD)
        {
            App_Protocol_SendErrorResp(0x00U, PROTO_ERR_EXEC_FAIL);
            App_Protocol_SendDebugInfo("I2C bus stuck, sampling auto-stopped");
            App_Sample_Stop();
            return;
        }
        if ((s_i2cFailCount >= I2C_FAIL_WARN_THRESHOLD) && (s_i2cWarnSent == 0U))
        {
            App_Protocol_SendDebugInfo("I2C bus stuck, sampling degraded");
            s_i2cWarnSent = 1U;
        }
    }
    else
    {
        s_i2cFailCount = 0U;
        s_i2cWarnSent  = 0U;
    }

    /* XOR checksum */
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

    s_sampling     = 0U;
    s_channelMask  = 0x01U;
    s_i2cFailCount = 0U;
    s_i2cWarnSent  = 0U;

    for (i = 0U; i < APP_SAMPLE_NUM_CHANNELS; i++)
    {
        s_channelRegMap[i] = APP_SAMPLE_REG_UNMAPPED;
    }

    /* ISR double buffer state */
    s_isrWriteIdx     = 0U;
    s_isrDataReady    = 0U;
    s_isrEffectiveMask = 0U;
    s_isrFailCount    = 0U;
    s_isrTotalCount   = 0U;

    /* Generator state */
    s_genMode    = GEN_NONE;
    s_genDivider = 1U;
    s_genCounter = 0U;

    BSP_SampleTim_Init();
    BSP_SampleTim_SetCallback(SampleTimerISR);
}

/**
  * @brief  Poll — called from main loop every iteration.
  *         ISR does I2C reads at precise timing; Poll sends buffered data via UART.
  */
void App_Sample_Poll(void)
{
    uint8_t  mask;
    uint8_t  readIdx;
    uint8_t  failCnt;
    uint8_t  totalCnt;

    /* TX busy: keep dataReady=1 so data is not lost; retry next loop */
    if (BSP_UART_IsTxBusy() != 0U) { return; }

    /* Check if ISR has new data */
    if (s_isrDataReady == 0U)
    {
        return;
    }

    /* Snapshot ISR outputs and clear flag (atomic w.r.t. TIM6 ISR) */
    __disable_irq();
    mask     = s_isrEffectiveMask;
    failCnt  = s_isrFailCount;
    totalCnt = s_isrTotalCount;
    readIdx  = s_isrWriteIdx ^ 1U;    /* ISR already swapped; read the other buffer */
    s_isrDataReady = 0U;
    __enable_irq();

    if (mask == 0U)
    {
        App_Sample_Stop();
        return;
    }

    SendStreamFrame(mask, s_isrBuf[readIdx], failCnt, totalCnt);
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

    if (result == SUCCESS)
    {
        if (s_genMode == GEN_LINEAR)
        {
            s_genDivider = Generator_CalcDivider(s_linIntervalMs);
        }
        else if (s_genMode == GEN_COSINE)
        {
            uint32_t periodUs = (uint32_t)BSP_SampleTim_GetPeriodUs();
            float freqHz = (float)s_cosFreqX100 * 0.01f;
            float dtSec  = (float)periodUs * 0.000001f;
            s_cosDPhase  = 6.28318530f * freqHz * dtSec;
        }
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
    __disable_irq();
    for (i = 0U; i < APP_SAMPLE_NUM_CHANNELS; i++)
    {
        s_channelRegMap[i] = ((uint16_t)pData[i * 2U] << 8U)
                             | (uint16_t)pData[i * 2U + 1U];
    }
    __enable_irq();
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

ErrorStatus App_Generator_StartSawtooth(uint16_t addr, int16_t min, int16_t max,
                                        int16_t step)
{
    /* Auto-stop previous generator */
    s_genMode = GEN_NONE;

    s_linAddr      = addr;
    s_linMin       = min;
    s_linMax       = max;
    s_linStep      = step;
    s_linCurrent   = min;
    s_linAscending = 1U;

    /* Every tick — no divider */
    s_genDivider = 1U;
    s_genCounter = 0U;
    s_genMode    = GEN_SAWTOOTH;

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
    s_cosChCount   = channelCount;
    s_cosFreqX100  = freqX100;

    for (i = 0U; i < channelCount; i++)
    {
        s_cosAddr[i]     = addrs[i];
        s_cosPhaseX10[i] = phaseX10[i];
    }

    /* Pre-calculate phase increment per tick: dPhase = 2πf × Δt */
    {
        uint32_t periodUs = (uint32_t)BSP_SampleTim_GetPeriodUs();
        float freqHz      = (float)freqX100 * 0.01f;
        float dtSec       = (float)periodUs * 0.000001f;
        s_cosDPhase       = 6.28318530f * freqHz * dtSec;
    }
    s_cosPhaseAccum = 0.0f;

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

uint8_t App_Generator_GetWriteChannelCount(void)
{
    if (s_genMode == GEN_LINEAR)   { return 1U; }
    if (s_genMode == GEN_SAWTOOTH) { return 1U; }
    if (s_genMode == GEN_COSINE)   { return s_cosChCount; }
    return 0U;
}
