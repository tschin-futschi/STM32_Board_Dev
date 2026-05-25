/**
  * @file    app_flashstore.c
  * @brief   FLASH file storage implementation
  *
  * CRC32 algorithm: table-driven, poly 0xEDB88320 (IEEE 802.3),
  * init 0xFFFFFFFF, final XOR 0xFFFFFFFF.
  * MUST stay byte-identical with PC side firmware_parser.cpp crc32Compute.
  */

#include "app_flashstore.h"
#include "bsp_flash.h"

#include <string.h>

/*--------------------------------------------------------------------------*/
/*                       Persistent metadata layout                         */
/*                       (16 bytes at FS_META_ADDR)                         */
/*--------------------------------------------------------------------------*/

typedef struct {
    uint32_t magic;     /* FS_META_MAGIC if a valid file is present       */
    uint32_t size;      /* File size in bytes; must be <= FS_MAX_FILE_BYTES */
    uint32_t crc32;     /* CRC32 of the data region (size bytes)          */
    uint32_t reserved;  /* 0xFFFFFFFF; reserved for future schema bits    */
} FsMetadata;

/*--------------------------------------------------------------------------*/
/*                            CRC32 (table-driven)                          */
/*--------------------------------------------------------------------------*/

static uint32_t s_crcTable[256];
static uint8_t  s_crcTableReady = 0U;

static void crc32_init_table(void)
{
    if (s_crcTableReady) {
        return;
    }
    for (uint32_t i = 0U; i < 256U; i++) {
        uint32_t c = i;
        for (uint8_t j = 0U; j < 8U; j++) {
            c = (c & 1U) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
        }
        s_crcTable[i] = c;
    }
    s_crcTableReady = 1U;
}

/* Update running CRC with `len` bytes from `data`.
 * `crc` must be the running state (init: 0xFFFFFFFF; final caller does ~). */
static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0U; i < len; i++) {
        crc = s_crcTable[(crc ^ data[i]) & 0xFFU] ^ (crc >> 8);
    }
    return crc;
}

/*--------------------------------------------------------------------------*/
/*                            Session state                                 */
/*                                                                          */
/*   All static; valid only while a Write session is active.                */
/*--------------------------------------------------------------------------*/

static uint8_t  s_writeActive    = 0U;
static uint32_t s_expectedTotal  = 0U;
static uint32_t s_writeOffset    = 0U;
static uint16_t s_nextPktSeq     = 0U;  /* Next expected pktSeq (0..N)      */
static uint32_t s_writeCrcAccum  = 0U;  /* Running CRC, pre-final-XOR       */

/*--------------------------------------------------------------------------*/
/*                       Internal helpers                                   */
/*--------------------------------------------------------------------------*/

static void read_metadata(FsMetadata *out)
{
    BSP_Flash_ReadBytes(FS_META_ADDR, (uint8_t *)out, FS_META_SIZE);
}

/* Returns FS_OK / FS_EMPTY / FS_CORRUPT based on metadata sanity.
 * On FS_OK, populates *sizeOut and *crc32Out (if non-NULL).
 * On non-OK, sets *sizeOut = 0 and *crc32Out = 0 (if non-NULL). */
static FsStatus check_metadata(uint32_t *sizeOut, uint32_t *crc32Out)
{
    FsMetadata meta;
    read_metadata(&meta);

    if (sizeOut  != NULL) { *sizeOut  = 0U; }
    if (crc32Out != NULL) { *crc32Out = 0U; }

    if (meta.magic == 0xFFFFFFFFU) {
        return FS_EMPTY;
    }
    if (meta.magic != FS_META_MAGIC) {
        return FS_CORRUPT;
    }
    if ((meta.size == 0U) || (meta.size > FS_MAX_FILE_BYTES)) {
        return FS_CORRUPT;
    }

    if (sizeOut  != NULL) { *sizeOut  = meta.size;  }
    if (crc32Out != NULL) { *crc32Out = meta.crc32; }
    return FS_OK;
}

/*--------------------------------------------------------------------------*/
/*                                  API                                     */
/*--------------------------------------------------------------------------*/

FsStatus App_FlashStore_Init(void)
{
    crc32_init_table();
    s_writeActive   = 0U;
    s_expectedTotal = 0U;
    s_writeOffset   = 0U;
    s_nextPktSeq    = 0U;
    s_writeCrcAccum = 0U;

    return check_metadata(NULL, NULL);
}

/*--------------------------------------------------------------------------*/

FsStatus App_FlashStore_WriteBegin(uint32_t totalBytes)
{
    if ((totalBytes == 0U) || (totalBytes > FS_MAX_FILE_BYTES)) {
        return FS_OUT_OF_RANGE;
    }

    /* Erase entire region (Sector 5..11). ~3-7 seconds blocking. */
    BSP_Flash_Status est = BSP_Flash_EraseSectors(FS_SECTOR_FIRST, FS_SECTOR_LAST);
    if (est != BSP_FLASH_OK) {
        s_writeActive = 0U;
        return FS_WRITE_FAILED;
    }

    s_writeActive   = 1U;
    s_expectedTotal = totalBytes;
    s_writeOffset   = 0U;
    s_nextPktSeq    = 0U;
    s_writeCrcAccum = 0xFFFFFFFFU;   /* CRC init */
    return FS_OK;
}

/*--------------------------------------------------------------------------*/

FsStatus App_FlashStore_WriteData(uint16_t pktSeq,
                                  const uint8_t *chunk,
                                  uint16_t len,
                                  uint16_t *nextSeqOut)
{
    if (!s_writeActive) {
        return FS_SEQ_ERROR;
    }
    if (pktSeq != s_nextPktSeq) {
        return FS_SEQ_ERROR;
    }
    if ((chunk == NULL) || (len == 0U) || (len > FS_CHUNK_MAX_BYTES)) {
        return FS_OUT_OF_RANGE;
    }
    if ((uint32_t)s_writeOffset + (uint32_t)len > s_expectedTotal) {
        return FS_OUT_OF_RANGE;
    }

    BSP_Flash_Status pst = BSP_Flash_ProgramBytes(FS_DATA_ADDR + s_writeOffset,
                                                   chunk, (uint32_t)len);
    if (pst != BSP_FLASH_OK) {
        s_writeActive = 0U;   /* abort session */
        return FS_WRITE_FAILED;
    }

    s_writeCrcAccum = crc32_update(s_writeCrcAccum, chunk, (uint32_t)len);
    s_writeOffset  += len;
    s_nextPktSeq   += 1U;

    if (nextSeqOut != NULL) {
        *nextSeqOut = s_nextPktSeq;
    }
    return FS_OK;
}

/*--------------------------------------------------------------------------*/

FsStatus App_FlashStore_WriteEnd(uint32_t expectedCrc32)
{
    if (!s_writeActive) {
        return FS_SEQ_ERROR;
    }
    if (s_writeOffset != s_expectedTotal) {
        s_writeActive = 0U;
        return FS_SEQ_ERROR;
    }

    uint32_t finalCrc = ~s_writeCrcAccum;
    if (finalCrc != expectedCrc32) {
        s_writeActive = 0U;
        /* No metadata written -> slot stays Empty for any future reader */
        return FS_CRC_MISMATCH;
    }

    /* Commit metadata. Sector 5 was erased by WriteBegin, so this is
     * writing into all-0xFF cells -> guaranteed clean. */
    FsMetadata meta;
    meta.magic    = FS_META_MAGIC;
    meta.size     = s_expectedTotal;
    meta.crc32    = finalCrc;
    meta.reserved = 0xFFFFFFFFU;

    BSP_Flash_Status pst = BSP_Flash_ProgramBytes(FS_META_ADDR,
                                                   (const uint8_t *)&meta,
                                                   FS_META_SIZE);
    s_writeActive = 0U;   /* Session always closes after End */
    if (pst != BSP_FLASH_OK) {
        return FS_WRITE_FAILED;
    }
    return FS_OK;
}

/*--------------------------------------------------------------------------*/

FsStatus App_FlashStore_ReadBegin(uint32_t *sizeOut, uint32_t *crc32Out)
{
    return check_metadata(sizeOut, crc32Out);
}

/*--------------------------------------------------------------------------*/

FsStatus App_FlashStore_Wipe(void)
{
    /* Same erase as WriteBegin, but caller does not follow up with WRITE_DATA.
     * Sequence matters: erase first (may fail), then clear session state only
     * on success, so a failed wipe leaves the in-RAM session untouched. */
    BSP_Flash_Status est = BSP_Flash_EraseSectors(FS_SECTOR_FIRST, FS_SECTOR_LAST);
    if (est != BSP_FLASH_OK) {
        return FS_WRITE_FAILED;
    }

    s_writeActive   = 0U;
    s_expectedTotal = 0U;
    s_writeOffset   = 0U;
    s_nextPktSeq    = 0U;
    s_writeCrcAccum = 0U;
    return FS_OK;
}

/*--------------------------------------------------------------------------*/

FsStatus App_FlashStore_ReadData(uint16_t pktSeq,
                                 uint8_t *out,
                                 uint16_t maxLen,
                                 uint16_t *actualLenOut)
{
    if ((out == NULL) || (maxLen == 0U) || (actualLenOut == NULL)) {
        return FS_OUT_OF_RANGE;
    }

    uint32_t size = 0U;
    FsStatus st = check_metadata(&size, NULL);
    if (st != FS_OK) {
        *actualLenOut = 0U;
        return st;
    }

    uint32_t offset = (uint32_t)pktSeq * FS_CHUNK_MAX_BYTES;
    if (offset >= size) {
        *actualLenOut = 0U;
        return FS_OUT_OF_RANGE;
    }

    uint32_t remaining = size - offset;
    uint32_t chunkLen  = (remaining > FS_CHUNK_MAX_BYTES) ? FS_CHUNK_MAX_BYTES : remaining;
    if (chunkLen > maxLen) {
        chunkLen = maxLen;
    }

    BSP_Flash_ReadBytes(FS_DATA_ADDR + offset, out, chunkLen);
    *actualLenOut = (uint16_t)chunkLen;
    return FS_OK;
}

/*--------------------------------------------------------------------------*/

void App_FlashStore_GetInfo(uint32_t *totalCapacity, uint32_t *usedSize)
{
    if (totalCapacity != NULL) {
        *totalCapacity = FS_MAX_FILE_BYTES;
    }
    if (usedSize != NULL) {
        uint32_t size = 0U;
        FsStatus st = check_metadata(&size, NULL);
        *usedSize = (st == FS_OK) ? size : 0U;
    }
}
