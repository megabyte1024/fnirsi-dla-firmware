/*
 * SPDX-License-Identifier: MIT
 */

#ifndef FNIRSI_DLA_SETTINGS_H
#define FNIRSI_DLA_SETTINGS_H

#include <stdint.h>

enum SettingsResult : uint8_t {
  kSettingsWritten = 0,
  kSettingsAlreadySet = 1,
  kSettingsReadFailed = 2,
  kSettingsEraseFailed = 3,
  kSettingsWriteFailed = 4,
  kSettingsVerifyFailed = 5,
};

struct SettingsReport {
  enum SettingsResult result;
  uint8_t flash_status;
  uint8_t boot_selection_before;
  uint8_t boot_selection_after;
};

struct SettingsReport settings_set_boot_selection(uint8_t selection);
struct SettingsReport settings_select_bootloader_next(void);
uint8_t settings_boot_selection(void);
int settings_store_boot_selection(uint8_t selection);

#endif
