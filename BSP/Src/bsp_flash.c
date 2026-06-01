/**
  * @file    bsp_flash.c
  * @brief   On-chip FLASH BSP implementation
  *
  * StdPeriph FLASH driver wrappers: each public API performs its own
  * Unlock/Lock pairing so callers never manage the lock state.
  */

#include "bsp_flash.h"

#include <string.h>

/* STM32F429ZGT6 has Sector 0..11 in single bank.
 * Map plain index 0..11 to the StdPeriph macro values (which encode the
 * SNB field of FLASH_CR and are NOT simply 0..11). */
static const uint16_t s_sectorMap[12] = {
    FLASH_Sector_0,  FLASH_Sector_1,  FLASH_Sector_2,  FLASH_Sector_3,
    FLASH_Sector_4,  FLASH_Sector_5,  FLASH_Sector_6,  FLASH_Sector_7,
    FLASH_Sector_8,  FLASH_Sector_9,  FLASH_Sector_10, FLASH_Sector_11
};

/* All status flags worth clearing before a fresh op */
#define BSP_FLASH_ALL_FLAGS (FLASH_FLAG_EOP    | FLASH_FLAG_OPERR  | \
                             FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR | \
                             FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR)

/*--------------------------------------------------------------------------*/

BSP_Flash_Status BSP_Flash_EraseSectors(uint8_t startSector, uint8_t endSector)
{
    if ((startSector < BSP_FLASH_SECTOR_MIN) ||
        (startSector > endSector) ||
        (endSector > BSP_FLASH_SECTOR_MAX)) {
        return BSP_FLASH_ERR_RANGE;
    }

    FLASH_Unlock();
    FLASH_ClearFlag(BSP_FLASH_ALL_FLAGS);

    for (uint8_t s = startSector; s <= endSector; s++) {
        FLASH_Status st = FLASH_EraseSector(s_sectorMap[s], VoltageRange_3);
        if (st != FLASH_COMPLETE) {
            FLASH_Lock();
            return BSP_FLASH_ERR_ERASE;
        }
    }

    FLASH_Lock();
    return BSP_FLASH_OK;
}

/*--------------------------------------------------------------------------*/

BSP_Flash_Status BSP_Flash_ProgramBytes(uint32_t addr, const uint8_t *data, uint32_t len)
{
    if ((data == NULL) || (len == 0U)) {
        return BSP_FLASH_ERR_RANGE;
    }
    /* Reject any target outside the data region (protect firmware sectors).
     * Use subtraction (len > END - addr) so addr+len cannot overflow. */
    if ((addr < BSP_FLASH_DATA_START_ADDR) ||
        (addr >= BSP_FLASH_DATA_END_ADDR)  ||
        (len  > (BSP_FLASH_DATA_END_ADDR - addr))) {
        return BSP_FLASH_ERR_RANGE;
    }
    if ((addr & 0x3U) != 0U) {
        return BSP_FLASH_ERR_ALIGN;
    }

    FLASH_Unlock();
    FLASH_ClearFlag(BSP_FLASH_ALL_FLAGS);

    uint32_t i = 0U;

    /* Whole-word phase */
    while ((i + 4U) <= len) {
        uint32_t word = ((uint32_t)data[i])               |
                        ((uint32_t)data[i + 1U] << 8U)    |
                        ((uint32_t)data[i + 2U] << 16U)   |
                        ((uint32_t)data[i + 3U] << 24U);
        FLASH_Status st = FLASH_ProgramWord(addr + i, word);
        if (st != FLASH_COMPLETE) {
            FLASH_Lock();
            return BSP_FLASH_ERR_PROGRAM;
        }
        i += 4U;
    }

    /* Tail phase: pad partial word with 0xFF (erased-state value) */
    if (i < len) {
        uint8_t tail[4] = { 0xFFU, 0xFFU, 0xFFU, 0xFFU };
        for (uint32_t j = 0U; j < (len - i); j++) {
            tail[j] = data[i + j];
        }
        uint32_t word = ((uint32_t)tail[0])         |
                        ((uint32_t)tail[1] << 8U)   |
                        ((uint32_t)tail[2] << 16U)  |
                        ((uint32_t)tail[3] << 24U);
        FLASH_Status st = FLASH_ProgramWord(addr + i, word);
        if (st != FLASH_COMPLETE) {
            FLASH_Lock();
            return BSP_FLASH_ERR_PROGRAM;
        }
    }

    FLASH_Lock();
    return BSP_FLASH_OK;
}

/*--------------------------------------------------------------------------*/

void BSP_Flash_ReadBytes(uint32_t addr, uint8_t *out, uint32_t len)
{
    memcpy(out, (const void *)addr, len);
}
