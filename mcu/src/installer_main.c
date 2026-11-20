/*
 * SPDX-License-Identifier: MIT
 */

#include "delay.h"
#include "flash.h"
#include "fpga_link.h"
#include "installer.h"
#include "installer_image.h"
#include "settings.h"
#include "system.h"

enum : uint16_t {
  kRegisterLight = 0x42,
  kLightRed = 6,
  kLightBlue = 3,
};

enum : uint32_t {
  kBlinkMs = 500,
  kFeedMs = 100,
};

const char installer_mark[] __attribute__((used, retain)) = "megabyte DLA16-BOOTLOADER-INSTALLER";

static int read_flash(uint32_t address, uint32_t* words, uint32_t word_count) {
  return flash_read(address, words, word_count) == kFlashOk ? 0 : -1;
}

static int erase_flash(uint32_t address) { return flash_erase_sector(address) == kFlashOk ? 0 : -1; }

static int write_flash(uint32_t address, const uint32_t* words, uint32_t word_count) {
  return flash_write(address, words, word_count) == kFlashOk ? 0 : -1;
}

static void restart(void) { software_reset(); }

[[noreturn]] static void show_failure(enum InstallerResult result) {
  (void)result;
  clock_init();
  fpga_link_init();
  for (uint32_t blink = 0;; ++blink) {
    (void)fpga_link_write(kRegisterLight, (blink & 1u) != 0 ? kLightBlue : kLightRed);
    for (uint32_t waited = 0; waited < kBlinkMs; waited += kFeedMs) {
      watchdog_feed();
      delay_ms(kFeedMs);
    }
  }
}

static const struct InstallerPort kPort = {
    watchdog_feed, read_flash, erase_flash, write_flash, settings_store_boot_selection, restart, show_failure,
};

int main(void) {
  const struct InstallerImage image = {installer_boot_words, installer_boot_size, installer_boot_crc};
  clock_set_120mhz();
  show_failure(installer_run(&kPort, &image));
}
