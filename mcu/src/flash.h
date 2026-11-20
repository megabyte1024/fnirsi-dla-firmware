/*
 * SPDX-License-Identifier: MIT
 */

#ifndef FNIRSI_DLA_FLASH_H
#define FNIRSI_DLA_FLASH_H

#include <stdint.h>

enum FlashResult : uint8_t {
  kFlashOk = 0,
  kFlashCodeOffsetMode = 0xFB,
  kFlashBusyTimeout = 0xFF,
};

enum : uint32_t { kFlashSectorBytes = 0x1000 };

enum FlashResult flash_read(uint32_t address, uint32_t* words, uint32_t word_count);
enum FlashResult flash_erase_sector(uint32_t address);
enum FlashResult flash_write(uint32_t address, const uint32_t* words, uint32_t word_count);

#endif
