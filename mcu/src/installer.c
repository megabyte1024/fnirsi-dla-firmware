/*
 * SPDX-License-Identifier: MIT
 */

#include "installer.h"

#define PAGE_WORDS (INSTALLER_PAGE_BYTES / 4u)
#define JAL_OPCODE_AND_RD_MASK 0xFFFu
#define JAL_TO_X0 0x06Fu
#define FIRST_CODE_OFFSET 8u
#define CRC32_POLYNOMIAL 0xEDB88320u
#define COMPARE_SAME 0
#define COMPARE_DIFFERENT 1
#define COMPARE_READ_FAILED 2

uint32_t installer_crc32(const uint32_t* words, uint32_t size) {
  uint32_t crc = 0xFFFFFFFFu;
  uint32_t i;
  for (i = 0; i < size; ++i) {
    uint32_t bit;
    crc ^= (words[i / 4u] >> (8u * (i % 4u))) & 0xFFu;
    for (bit = 0; bit < 8u; ++bit) crc = (crc >> 1) ^ (CRC32_POLYNOMIAL & (0u - (crc & 1u)));
  }
  return ~crc;
}

static uint32_t jal_target(uint32_t word) {
  return (((word >> 21) & 0x3FFu) << 1) | (((word >> 20) & 1u) << 11) | (((word >> 12) & 0xFFu) << 12) |
         (((word >> 31) & 1u) << 20);
}

enum InstallerResult installer_check_image(const struct InstallerImage* image) {
  uint32_t target;
  if (image->size == 0) return INSTALLER_IMAGE_EMPTY;
  if (image->size > INSTALLER_SECTOR_BYTES) return INSTALLER_IMAGE_TOO_LONG;
  if ((image->size & 3u) != 0) return INSTALLER_IMAGE_NOT_WORDS;
  if (installer_crc32(image->words, image->size) != image->crc) return INSTALLER_IMAGE_CRC_WRONG;
  if (image->size < FIRST_CODE_OFFSET || (image->words[0] & JAL_OPCODE_AND_RD_MASK) != JAL_TO_X0 ||
      image->words[1] != INSTALLER_START_MARKER) {
    return INSTALLER_IMAGE_NOT_BOOTLOADER;
  }
  target = jal_target(image->words[0]);
  if (target < FIRST_CODE_OFFSET || target >= image->size) return INSTALLER_IMAGE_NOT_BOOTLOADER;
  return INSTALLER_IMAGE_VALID;
}

static uint32_t page_words(uint32_t size, uint32_t address) {
  const uint32_t remaining = size - address;
  return (remaining < INSTALLER_PAGE_BYTES ? remaining : INSTALLER_PAGE_BYTES) / 4u;
}

static int compare(const struct InstallerPort* port, const struct InstallerImage* image, uint32_t end) {
  uint32_t buffer[PAGE_WORDS];
  uint32_t address;
  int same = 1;
  port->feed_watchdog();
  for (address = 0; address < end; address += INSTALLER_PAGE_BYTES) {
    const uint32_t count = page_words(end, address);
    uint32_t i;
    if (port->read(address, buffer, count) != 0) return COMPARE_READ_FAILED;
    for (i = 0; i < count; ++i) {
      const uint32_t at = address / 4u + i;
      if (buffer[i] != (at < image->size / 4u ? image->words[at] : INSTALLER_ERASED_WORD)) same = 0;
    }
  }
  return same ? COMPARE_SAME : COMPARE_DIFFERENT;
}

static int erase_and_program(const struct InstallerPort* port, const struct InstallerImage* image) {
  uint32_t address;
  port->feed_watchdog();
  if (port->erase_sector(0) != 0) return -1;
  for (address = 0; address < image->size; address += INSTALLER_PAGE_BYTES) {
    port->feed_watchdog();
    if (port->write(address, image->words + address / 4u, page_words(image->size, address)) != 0) return -1;
  }
  return 0;
}

static enum InstallerResult refuse_before_erase(const struct InstallerPort* port, enum InstallerResult result) {
  port->feed_watchdog();
  (void)port->store_boot_selection(INSTALLER_BOOTLOADER_NEXT);
  port->report_failure(result);
  return result;
}

enum InstallerResult installer_run(const struct InstallerPort* port, const struct InstallerImage* image) {
  enum InstallerResult result;
  uint32_t attempt;
  int found;
  port->feed_watchdog();
  result = installer_check_image(image);
  if (result != INSTALLER_IMAGE_VALID) return refuse_before_erase(port, result);
  found = compare(port, image, image->size);
  if (found == COMPARE_READ_FAILED) return refuse_before_erase(port, INSTALLER_READ_FAILED);
  result = INSTALLER_ALREADY_IN_PLACE;
  if (found != COMPARE_SAME) {
    result = INSTALLER_WRITE_FAILED;
    for (attempt = 0; attempt < INSTALLER_ATTEMPTS && result != INSTALLER_WRITTEN; ++attempt) {
      if (erase_and_program(port, image) == 0 && compare(port, image, INSTALLER_SECTOR_BYTES) == COMPARE_SAME) {
        result = INSTALLER_WRITTEN;
      }
    }
    if (result != INSTALLER_WRITTEN) {
      port->report_failure(result);
      return result;
    }
  }
  port->feed_watchdog();
  if (port->store_boot_selection(INSTALLER_BOOTLOADER_NEXT) != 0) {
    port->report_failure(INSTALLER_SELECTION_FAILED);
    return INSTALLER_SELECTION_FAILED;
  }
  port->feed_watchdog();
  port->reset();
  return result;
}
