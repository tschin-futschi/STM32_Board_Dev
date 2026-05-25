/**
  * @file    app_flashstore.h
  * @brief   FLASH file storage — single-slot overwrite model on Sector 5-11
  *
  * Storage layout (896KB region, [0x08020000, 0x08100000)):
  *
  *   0x08020000 ---------------------------------- Sector 5 start
  *               metadata (16 bytes):
  *                 [magic(4 LE)] [size(4 LE)] [crc32(4 LE)] [reserved(4)]
  *   0x08020010 ----------------------------------
  *               file data (up to 917488 bytes contiguous)
  *   0x08100000 ---------------------------------- Sector 12 start (exclusive)
  *
  * Semantics: single-slot. WriteBegin erases the entire region (Sector 5-11)
  * and starts a fresh session; WriteEnd commits metadata atomically (only
  * after CRC check passes). A failed/interrupted write leaves no metadata
  * magic, so the next Init/ReadBegin reports the slot as Empty -- no partial
  * file is ever exposed to readers.
  *
  * Downloaded bytes equal the original file 1:1 (no header/footer/padding
  * is included in the dump), so the user can rename .txt back to the
  * original extension and use the file directly.
  *
  * Protocol mapping: see app_protocol.c handlers for 0x39 ~ 0x3E commands.
  * Status codes here match the FlashStoreStatus enum defined in
  * MotorDev/src/protocol/motor_protocol.h on the PC side -- keep both in
  * sync when adding/removing values.
  */

#ifndef __APP_FLASHSTORE_H
#define __APP_FLASHSTORE_H

#include "stm32f4xx.h"

/*--------------------------------------------------------------------------*/
/*                            Region constants                              */
/*--------------------------------------------------------------------------*/

#define FS_REGION_START_ADDR    0x08020000U          /* Sector 5 start    */
#define FS_REGION_END_ADDR      0x08100000U          /* Sector 12 start   */
#define FS_REGION_TOTAL_BYTES   (896U * 1024U)       /* 917504            */

#define FS_SECTOR_FIRST         5U                   /* Erase Sector 5    */
#define FS_SECTOR_LAST          11U                  /*  through Sector 11 */

#define FS_META_ADDR            FS_REGION_START_ADDR /* 0x08020000        */
#define FS_META_SIZE            16U                  /* 4*uint32_t        */
#define FS_DATA_ADDR            (FS_REGION_START_ADDR + FS_META_SIZE) /* 0x08020010 */
#define FS_MAX_FILE_BYTES       (FS_REGION_TOTAL_BYTES - FS_META_SIZE) /* 917488 */

#define FS_META_MAGIC           0xA5C3E18FU          /* Sentinel value    */

#define FS_CHUNK_MAX_BYTES      252U                 /* Aligned with 0x3A/0x3D payload limit */

/*--------------------------------------------------------------------------*/
/*                              Status codes                                */
/*                                                                          */
/*   These values are sent on the wire (response payload of 0x39/0x3B/      */
/*   0x3C, plus 0x01 ErrorResponse for 0x3A/0x3D failures). They MUST       */
/*   match FlashStoreStatus in MotorDev/src/protocol/motor_protocol.h.      */
/*--------------------------------------------------------------------------*/

typedef enum {
    FS_OK           = 0x00U,  /* Operation succeeded                         */
    FS_EMPTY        = 0x01U,  /* metadata magic == 0xFFFFFFFF (erased)       */
    FS_CORRUPT      = 0x02U,  /* metadata magic invalid (not 0xFFFFFFFF      */
                              /*   and not FS_META_MAGIC)                    */
    FS_WRITE_FAILED = 0x03U,  /* BSP_Flash erase/program returned error      */
    FS_CRC_MISMATCH = 0x04U,  /* WriteEnd: PC-computed CRC != local accum    */
    FS_BUSY         = 0x05U,  /* (reserved for future use; not raised here)  */
    FS_OUT_OF_RANGE = 0x06U,  /* totalBytes/len/pktSeq out of valid range    */
    FS_SEQ_ERROR    = 0x07U   /* pktSeq != expected (gap or duplicate) or    */
                              /*   WriteEnd called with offset != totalBytes */
                              /*   or WriteData/End called without WriteBegin */
} FsStatus;

/*--------------------------------------------------------------------------*/
/*                                  API                                     */
/*--------------------------------------------------------------------------*/

/**
  * @brief  Initialize the FLASH file store at boot.
  * @retval FS_OK      Valid file present (metadata magic matches, size sane)
  * @retval FS_EMPTY   Region is erased (magic = 0xFFFFFFFF)
  * @retval FS_CORRUPT Metadata magic invalid OR size > FS_MAX_FILE_BYTES
  *
  * Pure read; does not modify FLASH. Call once during App init.
  */
FsStatus App_FlashStore_Init(void);

/**
  * @brief  Start a write session.
  * @param  totalBytes  Final file size in bytes; must be in [1, FS_MAX_FILE_BYTES]
  * @retval FS_OK            Region erased, session armed
  * @retval FS_OUT_OF_RANGE  totalBytes out of bounds
  * @retval FS_WRITE_FAILED  BSP_Flash_EraseSectors hardware error
  *
  * Blocks ~3-7 seconds while erasing Sector 5-11. CPU stalls on its own
  * FLASH bank; the caller (app_protocol.c handler) should ensure no
  * tight-deadline work runs in parallel. Heartbeat must already have been
  * paused by the PC side before sending 0x39 BEGIN.
  *
  * Any previously active session is silently overwritten -- the model is
  * single-slot, repeated BEGINs are allowed.
  */
FsStatus App_FlashStore_WriteBegin(uint32_t totalBytes);

/**
  * @brief  Write one data chunk during an active session.
  * @param  pktSeq      0-based packet sequence (must equal previous + 1, or 0 for first packet)
  * @param  chunk       Source buffer (non-NULL)
  * @param  len         Chunk size in bytes; must be in [1, FS_CHUNK_MAX_BYTES]
  *                       and writeOffset + len <= totalBytes
  * @param  nextSeqOut  [out] Next expected pktSeq on success (= pktSeq + 1)
  * @retval FS_OK            Chunk programmed, CRC accumulator advanced
  * @retval FS_SEQ_ERROR     No active session, or pktSeq mismatch
  * @retval FS_OUT_OF_RANGE  len out of range or would overflow totalBytes
  * @retval FS_WRITE_FAILED  BSP_Flash_ProgramBytes hardware error
  */
FsStatus App_FlashStore_WriteData(uint16_t pktSeq,
                                  const uint8_t *chunk,
                                  uint16_t len,
                                  uint16_t *nextSeqOut);

/**
  * @brief  Finish a write session, validate CRC, and commit metadata.
  * @param  expectedCrc32  CRC32 (IEEE 802.3) of the entire file, computed by PC
  * @retval FS_OK             CRC matches, metadata written, session closed
  * @retval FS_SEQ_ERROR      No active session OR bytes received != totalBytes
  * @retval FS_CRC_MISMATCH   CRC differs; session closed and file discarded
  *                           (metadata not written, slot remains empty)
  * @retval FS_WRITE_FAILED   Metadata program error
  *
  * Metadata is written ONLY after CRC verification, so a CRC mismatch
  * leaves the slot as Empty (magic = 0xFFFFFFFF still). Same for any
  * mid-session crash: no metadata = no file.
  */
FsStatus App_FlashStore_WriteEnd(uint32_t expectedCrc32);

/**
  * @brief  Read metadata to start a read session.
  * @param  sizeOut    [out] File size in bytes; 0 if not OK
  * @param  crc32Out   [out] File CRC32; 0 if not OK
  * @retval FS_OK       Valid file present; sizeOut/crc32Out populated
  * @retval FS_EMPTY    Slot empty
  * @retval FS_CORRUPT  Metadata invalid (magic mismatch or size out of range)
  *
  * Stateless: no session bookkeeping. Caller can call ReadData repeatedly
  * after a successful ReadBegin without re-checking state on every call.
  */
FsStatus App_FlashStore_ReadBegin(uint32_t *sizeOut, uint32_t *crc32Out);

/**
  * @brief  Read one data chunk by packet sequence.
  * @param  pktSeq         0-based packet sequence (offset = pktSeq * FS_CHUNK_MAX_BYTES)
  * @param  out            Destination buffer (non-NULL)
  * @param  maxLen         Capacity of `out`; recommended >= FS_CHUNK_MAX_BYTES
  * @param  actualLenOut   [out] Bytes actually written to `out`
  *                          (may be < FS_CHUNK_MAX_BYTES for the last packet)
  * @retval FS_OK            Bytes read successfully
  * @retval FS_EMPTY         No file present
  * @retval FS_CORRUPT       Metadata invalid
  * @retval FS_OUT_OF_RANGE  pktSeq is beyond end-of-file
  */
FsStatus App_FlashStore_ReadData(uint16_t pktSeq,
                                 uint8_t *out,
                                 uint16_t maxLen,
                                 uint16_t *actualLenOut);

/**
  * @brief  Report region capacity and current usage.
  * @param  totalCapacity  [out] FS_MAX_FILE_BYTES (constant 917488)
  * @param  usedSize       [out] Current file size in bytes, or 0 if slot empty/corrupt
  *
  * Always succeeds (no failure mode). Used by 0x3E FLASH_STORE_INFO handler.
  */
void App_FlashStore_GetInfo(uint32_t *totalCapacity, uint32_t *usedSize);

#endif /* __APP_FLASHSTORE_H */
