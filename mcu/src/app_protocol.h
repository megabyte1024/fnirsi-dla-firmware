/*
 * SPDX-License-Identifier: MIT
 */

#ifndef FNIRSI_DLA_APP_PROTOCOL_H
#define FNIRSI_DLA_APP_PROTOCOL_H

#include <stdint.h>

struct FpgaQueue;
struct FpgaRunner;

#define APP_VERSION_MAJOR 0u
#define APP_VERSION_MINOR 36u
#define APP_REPLY_BYTES 64u
#define APP_VENDOR_REPLY_BYTES 24u
#define APP_OWN_MARK "megabyte"
#define APP_OWN_MARK_AT 9u
#define APP_BUSY_AT 17u
#define APP_CMD_STOP_FEEDING_WATCHDOG 0xF0u
#define APP_CMD_FPGA_WRITE 0xA0u
#define APP_CMD_FPGA_READ 0xA1u
#define APP_CMD_FPGA_PULSE_PB23 0xA2u
#define APP_CMD_STREAM_HALT 0xA3u
#define APP_FPGA_BAD_LENGTH 0xFFu
#define APP_WATCHDOG_GRACE_MS 10000u
#define APP_USB3_WAIT_MS 4000u

struct AppFpgaReply {
  uint32_t word;
  uint16_t length0;
  uint16_t length1;
  uint8_t status;
};

struct AppStreamState {
  uint32_t halts;
  uint64_t halt_position;
  uint8_t fault;
  uint64_t fault_at;
};

struct AppHardware {
  uint8_t (*boot_selection)(void);
  int (*set_boot_selection)(uint8_t selection);
  void (*reset)(void);
  uint8_t model;
  int (*fpga_write)(uint16_t reg, uint16_t value);
  int (*fpga_read)(uint16_t reg, struct AppFpgaReply* reply);
  void (*fpga_pulse_pb23)(void);
  struct FpgaQueue* fpga_queue;
  uint8_t (*usb_generation)(void);
  void (*stop_data)(void);
  void (*stream_state)(struct AppStreamState* state);
  struct FpgaRunner* fpga_runner;
  void (*fault_no_usb3)(void);
};

struct AppState {
  uint8_t stop_feeding_watchdog;
  uint8_t rate_index;
  int16_t centivolts;
  uint8_t usb_was_configured;
  uint8_t usb3_link_was_up;
  uint32_t starts_refused;
};

uint16_t app_handle_command(struct AppState* state, const struct AppHardware* hardware, const uint8_t* command,
                            uint16_t length, uint8_t* reply);

int app_watchdog_should_feed(struct AppState* state, uint32_t uptime_ms, int usb_configured);
int app_usb3_should_give_up(struct AppState* state, uint32_t uptime_ms, int usb3_link_up);
void app_usb_reset(const struct AppHardware* hardware);
uint16_t app_vendor_request(const struct AppHardware* hardware, uint8_t request, uint16_t max_length,
                            uint8_t* reply);

#endif
