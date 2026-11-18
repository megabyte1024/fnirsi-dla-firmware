/*
 * SPDX-License-Identifier: MIT
 */

#include "boot_protocol.h"
#include "flash.h"
#include "settings.h"
#include "spi_flash.h"
#include "system.h"
#include "usb2.h"

static int read_flash(uint32_t address, uint32_t* words, uint32_t word_count) {
  return flash_read(address, words, word_count) == kFlashOk ? 0 : -1;
}

static int erase_flash(uint32_t address) { return flash_erase_sector(address) == kFlashOk ? 0 : -1; }

static int write_flash(uint32_t address, const uint32_t* words, uint32_t word_count) {
  return flash_write(address, words, word_count) == kFlashOk ? 0 : -1;
}

static void restart(void) { software_reset(); }

static uint8_t cause(void) { return (uint8_t)reset_cause(); }

static const struct BootHardware kHardware = {
    read_flash,     erase_flash,       write_flash,  settings_boot_selection, settings_store_boot_selection,
    restart,        cause,             spi_flash_prepare,                     spi_flash_id,
    spi_flash_read, spi_flash_erase_sector, spi_flash_program,
};

static struct BootUpdate update;

static uint16_t handle(const uint8_t* command, uint16_t length, uint8_t* reply) {
  return boot_handle_command(&update, &kHardware, command, length, reply);
}

int main(void) {
  clock_set_120mhz();
  if (boot_should_start_application(&kHardware)) {
    if (boot_application_wants_watchdog(&kHardware)) watchdog_start();
    const uintptr_t entry = BOOT_APP_ADDRESS;
    ((void (*)(void))entry)();
  }
  usb2_init();
  for (;;) usb2_poll(handle);
}
