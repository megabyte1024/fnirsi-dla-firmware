/*
 * SPDX-License-Identifier: MIT
 */

#include "settings.h"

#include "flash.h"

enum : uint32_t {
  kSettingsAddress = 0x66000,
  kSettingsWords = 0x68 / 4,
  kBootSelectionWord = 1,
  kBootSelectionMask = 0xFF,
  kBootloaderNext = 1,
};

static uint8_t boot_selection(const uint32_t* block) { return (uint8_t)(block[kBootSelectionWord] & kBootSelectionMask); }

struct SettingsReport settings_set_boot_selection(uint8_t selection) {
  struct SettingsReport report = {kSettingsWritten, kFlashOk, 0, 0};
  uint32_t block[kSettingsWords];
  report.flash_status = flash_read(kSettingsAddress, block, kSettingsWords);
  if (report.flash_status != kFlashOk) {
    report.result = kSettingsReadFailed;
    return report;
  }
  report.boot_selection_before = boot_selection(block);
  report.boot_selection_after = report.boot_selection_before;
  if (report.boot_selection_before == selection) {
    report.result = kSettingsAlreadySet;
    return report;
  }
  block[kBootSelectionWord] = (block[kBootSelectionWord] & ~kBootSelectionMask) | selection;
  report.flash_status = flash_erase_sector(kSettingsAddress);
  if (report.flash_status != kFlashOk) {
    report.result = kSettingsEraseFailed;
    return report;
  }
  report.flash_status = flash_write(kSettingsAddress, block, kSettingsWords);
  if (report.flash_status != kFlashOk) {
    report.result = kSettingsWriteFailed;
    return report;
  }
  uint32_t check[kSettingsWords];
  report.flash_status = flash_read(kSettingsAddress, check, kSettingsWords);
  report.boot_selection_after = boot_selection(check);
  for (uint32_t i = 0; i < kSettingsWords; ++i) {
    if (report.flash_status != kFlashOk || check[i] != block[i]) {
      report.result = kSettingsVerifyFailed;
      return report;
    }
  }
  return report;
}

struct SettingsReport settings_select_bootloader_next(void) { return settings_set_boot_selection(kBootloaderNext); }

uint8_t settings_boot_selection(void) {
  uint32_t block[kBootSelectionWord + 1];
  if (flash_read(kSettingsAddress, block, kBootSelectionWord + 1) != kFlashOk) return kBootloaderNext;
  return boot_selection(block);
}

int settings_store_boot_selection(uint8_t selection) {
  const struct SettingsReport report = settings_set_boot_selection(selection);
  return report.result == kSettingsWritten || report.result == kSettingsAlreadySet ? 0 : -1;
}
