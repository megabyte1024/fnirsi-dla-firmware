/*
 * SPDX-License-Identifier: MIT
 */

#ifndef FNIRSI_DLA_BOOT_PROTOCOL_H
#define FNIRSI_DLA_BOOT_PROTOCOL_H

#include <stdint.h>

#define BOOT_APP_ADDRESS 0x1000u
#define BOOT_APP_LIMIT 0x65000u
#define BOOT_SECTOR_BYTES 0x1000u
#define BOOT_ERASED_WORD 0xF7F9BF11u
#define BOOT_REPLY_BYTES 64u
#define BOOT_MAX_CHUNK_BYTES 508u
#define BOOT_MARKER_ADDRESS 0x1004u
#define BOOT_APP_MARKER 0x43535442u
#define BOOT_RESET_CAUSE_WATCHDOG 2u
#define BOOT_PROTOCOL_MARK 1u
#define BOOT_FPGA_AREA_BYTES 0x100000u
#define BOOT_FPGA_HOLD_BYTES 0x1000u
#define BOOT_FPGA_PAGE_BYTES 256u
#define BOOT_FPGA_CRC_MAX_BYTES 0x10000u
#define BOOT_FPGA_FLASH_BYTES 0x1000000u
#define BOOT_OPEN_NONE 0u
#define BOOT_OPEN_MCU 1u
#define BOOT_OPEN_FPGA 2u

struct BootHardware {
  int (*read)(uint32_t address, uint32_t* words, uint32_t word_count);
  int (*erase_sector)(uint32_t address);
  int (*write)(uint32_t address, const uint32_t* words, uint32_t word_count);
  uint8_t (*boot_selection)(void);
  int (*set_boot_selection)(uint8_t selection);
  void (*reset)(void);
  uint8_t (*reset_cause)(void);
  void (*flash_bus_prepare)(void);
  uint32_t (*fpga_id)(void);
  int (*fpga_read)(uint32_t address, uint8_t* bytes, uint32_t count);
  int (*fpga_erase)(uint32_t address);
  int (*fpga_program)(uint32_t address, const uint8_t* bytes, uint32_t count);
};

struct BootUpdate {
  uint32_t image_size;
  uint32_t received;
  uint32_t first_word;
  uint8_t open;
  uint8_t bus_ready;
  uint8_t hold[BOOT_FPGA_HOLD_BYTES];
};

int boot_application_complete(const struct BootHardware* hardware);
int boot_should_start_application(const struct BootHardware* hardware);
int boot_application_wants_watchdog(const struct BootHardware* hardware);
uint16_t boot_handle_command(struct BootUpdate* update, const struct BootHardware* hardware, const uint8_t* command,
                             uint16_t length, uint8_t* reply);

#endif
