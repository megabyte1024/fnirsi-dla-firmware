/*
 * SPDX-License-Identifier: MIT
 */

#ifndef FNIRSI_DLA_INSTALLER_H
#define FNIRSI_DLA_INSTALLER_H

#include <stdint.h>

#define INSTALLER_SECTOR_BYTES 0x1000u
#define INSTALLER_PAGE_BYTES 256u
#define INSTALLER_ERASED_WORD 0xF7F9BF11u
#define INSTALLER_START_MARKER 0x43535442u
#define INSTALLER_ATTEMPTS 3u
#define INSTALLER_BOOTLOADER_NEXT 1u

enum InstallerResult {
  INSTALLER_WRITTEN = 0,
  INSTALLER_ALREADY_IN_PLACE = 1,
  INSTALLER_IMAGE_EMPTY = 2,
  INSTALLER_IMAGE_TOO_LONG = 3,
  INSTALLER_IMAGE_NOT_WORDS = 4,
  INSTALLER_IMAGE_CRC_WRONG = 5,
  INSTALLER_IMAGE_NOT_BOOTLOADER = 6,
  INSTALLER_READ_FAILED = 7,
  INSTALLER_WRITE_FAILED = 8,
  INSTALLER_SELECTION_FAILED = 9,
  INSTALLER_IMAGE_VALID = 10,
};

struct InstallerImage {
  const uint32_t* words;
  uint32_t size;
  uint32_t crc;
};

struct InstallerPort {
  void (*feed_watchdog)(void);
  int (*read)(uint32_t address, uint32_t* words, uint32_t word_count);
  int (*erase_sector)(uint32_t address);
  int (*write)(uint32_t address, const uint32_t* words, uint32_t word_count);
  int (*store_boot_selection)(uint8_t selection);
  void (*reset)(void);
  void (*report_failure)(enum InstallerResult result);
};

uint32_t installer_crc32(const uint32_t* words, uint32_t size);
enum InstallerResult installer_check_image(const struct InstallerImage* image);
enum InstallerResult installer_run(const struct InstallerPort* port, const struct InstallerImage* image);

#endif
