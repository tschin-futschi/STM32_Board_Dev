/**
  * @file    app_sample.h
  * @brief   Oscilloscope sampling — channel config, TIM6 trigger, stream frame TX
  *
  * Data stream frame format (protocol.MD v1.4):
  *   [0xBB][effective_mask][LEN][ch_data...][XOR]
  *   LEN = number of active channels × 2
  *   ch_data: int16 big-endian per channel, ordered from lowest to highest channel
  *   XOR: effective_mask XOR LEN XOR all data bytes
  *
  * Defaults:
  *   Interval index : 5  (1000 us, 1 kHz)
  *   Channel mask   : 0x01 (channel 0 only)
  *   Reg map        : all 0xFFFF (unmapped)
  */

#ifndef __APP_SAMPLE_H
#define __APP_SAMPLE_H

#include "stm32f4xx.h"

/*--------------------------------------------------------------------------*/
/*                            Constants                                     */
/*--------------------------------------------------------------------------*/

#define APP_SAMPLE_NUM_CHANNELS     8U      /* Total channels supported     */
#define APP_SAMPLE_REG_UNMAPPED     0xFFFFU /* Sentinel: channel not mapped */
#define APP_SAMPLE_REG_MAP_LEN      16U     /* 8 channels × 2 bytes each    */

/*--------------------------------------------------------------------------*/
/*                               API                                        */
/*--------------------------------------------------------------------------*/

void        App_Sample_Init(void);
void        App_Sample_Poll(void);

ErrorStatus App_Sample_Start(void);
void        App_Sample_Stop(void);

ErrorStatus App_Sample_SetInterval(uint8_t idx);
ErrorStatus App_Sample_SetChannelMask(uint8_t mask);
void        App_Sample_SetRegMap(const uint8_t *pData);

uint8_t     App_Sample_IsActive(void);
uint8_t     App_Sample_GetEffectiveMask(void);

#endif /* __APP_SAMPLE_H */
