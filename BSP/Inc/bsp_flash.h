/**
  * @file    bsp_flash.h
  * @brief   On-chip FLASH BSP — sector erase + word-aligned program + read
  *
  * Used by App/Src/app_flashstore.c to manage the 896KB FLASH file storage
  * region (Sector 5-11 on STM32F429ZGT6, addresses 0x08020000 ~ 0x08100000).
  *
  * Threading: not thread-safe. Caller guarantees no concurrent use.
  * Cache: data region (0x08020000+) is not cached by ART (ART only caches
  *        instruction fetches from the firmware region), so reads remain
  *        coherent after writes without any cache invalidation.
  * Blocking: EraseSectors blocks ~1s per 128KB sector while CPU is stalled
  *           on its own Flash bank (single-bank chip). Interrupts deferred.
  */

#ifndef __BSP_FLASH_H
#define __BSP_FLASH_H

#include "stm32f4xx.h"

/*--------------------------------------------------------------------------*/
/*                              Status codes                                */
/*--------------------------------------------------------------------------*/

typedef enum {
    BSP_FLASH_OK          = 0,  /* Operation succeeded                   */
    BSP_FLASH_ERR_RANGE   = 1,  /* Sector number / length out of range   */
    BSP_FLASH_ERR_ALIGN   = 2,  /* Target address not 4-byte aligned     */
    BSP_FLASH_ERR_ERASE   = 3,  /* FLASH_EraseSector returned non-OK     */
    BSP_FLASH_ERR_PROGRAM = 4   /* FLASH_ProgramWord returned non-OK     */
} BSP_Flash_Status;

/*--------------------------------------------------------------------------*/
/*                       Writable region bounds                            */
/*--------------------------------------------------------------------------*/

/* Only the data region (Sector 5-11) may be erased/programmed. Sectors 0-4
 * hold the running firmware + vector table; writing/erasing them would brick
 * the board, so the API rejects any target below SECTOR_MIN / START_ADDR. */
#define BSP_FLASH_DATA_START_ADDR   0x08020000U   /* Sector 5 start            */
#define BSP_FLASH_DATA_END_ADDR     0x08100000U   /* Sector 12 start (exclusive) */
#define BSP_FLASH_SECTOR_MIN        5U            /* Lowest erasable sector    */
#define BSP_FLASH_SECTOR_MAX        11U           /* Highest sector on F429ZG  */

/*--------------------------------------------------------------------------*/
/*                                  API                                     */
/*--------------------------------------------------------------------------*/

/**
  * @brief  Erase a range of FLASH sectors (inclusive on both ends).
  * @param  startSector  First sector to erase (0..11 on STM32F429ZGT6)
  * @param  endSector    Last sector to erase (>= startSector, <= 11)
  * @retval BSP_FLASH_OK / BSP_FLASH_ERR_RANGE / BSP_FLASH_ERR_ERASE
  *
  * Internal: Unlock -> ClearFlag -> EraseSector loop -> Lock.
  * Voltage range: VoltageRange_3 (2.7V~3.6V) for word-level access speed.
  */
BSP_Flash_Status BSP_Flash_EraseSectors(uint8_t startSector, uint8_t endSector);

/**
  * @brief  Program a byte buffer into FLASH at the given address.
  * @param  addr  Target address; must be 4-byte aligned
  * @param  data  Pointer to source buffer; non-NULL
  * @param  len   Number of bytes to write; > 0. If not a multiple of 4,
  *               the last partial word is padded with 0xFF (caller must
  *               account for this when computing CRC or readback length).
  * @retval BSP_FLASH_OK / BSP_FLASH_ERR_RANGE / BSP_FLASH_ERR_ALIGN /
  *         BSP_FLASH_ERR_PROGRAM
  *
  * Internal: Unlock -> ClearFlag -> ProgramWord loop -> Lock.
  * Caller must ensure target range was erased (FLASH only allows 1->0 bit
  * transitions; programming over non-0xFF bytes is silently rejected and
  * will fail this function).
  */
BSP_Flash_Status BSP_Flash_ProgramBytes(uint32_t addr, const uint8_t *data, uint32_t len);

/**
  * @brief  Read FLASH content as a plain memcpy.
  * @param  addr  Source address (within FLASH-mapped range)
  * @param  out   Destination buffer; non-NULL
  * @param  len   Number of bytes to read
  *
  * FLASH is memory-mapped; no special access pattern needed.
  * This function is provided for symmetry with the Program/Erase pair.
  */
void BSP_Flash_ReadBytes(uint32_t addr, uint8_t *out, uint32_t len);

#endif /* __BSP_FLASH_H */
