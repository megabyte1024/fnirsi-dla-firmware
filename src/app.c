/*
 * SPDX-License-Identifier: MIT
 */

#include "app_protocol.h"
#include "ch569_regs.h"
#include "blackbox.h"
#include "data_link.h"
#include "delay.h"
#include "flash.h"
#include "interrupts.h"
#include "fpga_link.h"
#include "fpga_steps.h"
#include "model.h"
#include "settings.h"
#include "stall_watch.h"
#include "system.h"
#include "uart.h"
#include "usb2.h"
#include "usb3.h"

static volatile uint8_t use_usb3 = 0;

static uint8_t usb_generation(void) { return use_usb3 ? 3u : 2u; }

enum : uint8_t { kTestModeResetKeep = 16 };

static uint8_t model_kept_at_start;

static void restart(void) {
  uart_write("app: reset\r\n");
  uart_flush();
  software_reset();
}

static void log_register(const char* what, uint16_t reg, uint32_t value, int result) {
  uart_write(what);
  uart_write_hex(reg, 4);
  uart_write(" ");
  uart_write_hex(value, 8);
  if (result == 0) {
    uart_write(" ok\r\n");
    return;
  }
  uart_write(" failed ");
  uart_write_hex((uint32_t)-result, 2);
  uart_write("\r\n");
}

static int fpga_write(uint16_t reg, uint16_t value) {
  const int result = fpga_link_write(reg, value);
  log_register("fpga w ", reg, value, result);
  return result;
}

static int fpga_read(uint16_t reg, struct AppFpgaReply* reply) {
  struct FpgaLinkReply answer = {0, 0, 0, 0};
  const int result = fpga_link_read(reg, &answer);
  reply->word = answer.word;
  reply->length0 = answer.length0;
  reply->length1 = answer.length1;
  reply->status = answer.status;
  log_register("fpga r ", reg, answer.word, result);
  return result;
}

static void fpga_pulse_pb23(void) {
  fpga_link_pulse_pb23();
  uart_write("fpga pb23 pulse\r\n");
}

static int queued_write(uint16_t reg, uint16_t value) {
  const int result = fpga_link_write(reg, value);
  if (result != 0) log_register("fpga w ", reg, value, result);
  return result;
}

static void start_data(void) { data_link_start(); }

static struct FpgaQueue fpga_queue;
static struct FpgaRunner fpga_runner;
static const struct FpgaPort fpga_port = {queued_write, fpga_link_set_pb23, clock_now_ms, start_data};

static struct AppHardware hardware = {
    .boot_selection = settings_boot_selection,
    .set_boot_selection = settings_store_boot_selection,
    .reset = restart,
    .model = 0,
    .fpga_write = fpga_write,
    .fpga_read = fpga_read,
    .fpga_pulse_pb23 = fpga_pulse_pb23,
    .fpga_queue = &fpga_queue,
    .usb_generation = usb_generation,
    .stop_data = data_link_stop,
    .stream_state = data_link_stream_state,
    .fpga_runner = &fpga_runner,
    .fault_no_usb3 = data_link_fault_no_usb3,
};
static struct AppState state;
static struct StallWatch stall_watch;


static void put32(uint8_t* bytes, uint32_t value) {
  bytes[0] = (uint8_t)value;
  bytes[1] = (uint8_t)(value >> 8);
  bytes[2] = (uint8_t)(value >> 16);
  bytes[3] = (uint8_t)(value >> 24);
}

static uint16_t usb3_test(const uint8_t* command, uint16_t length, uint8_t* reply) {
  uint32_t values[4] = {0, 0, 0, 0};
  uint16_t i;
  const uint8_t mode = length > 2 ? command[2] : 0;
  for (i = 0; i < APP_REPLY_BYTES; ++i) reply[i] = 0;
  reply[0] = 0x0A;
  reply[1] = 0xA5;
  reply[2] = mode;
  if (mode == 1) usb3_read_registers(values);
  if (mode == 6) {
    const uint32_t first = length > 4 ? (uint32_t)(command[3] | (command[4] << 8)) : 0u;
    reply[4] = (uint8_t)blackbox_count();
    reply[5] = (uint8_t)(blackbox_count() >> 8);
    reply[6] = (uint8_t)(blackbox_count() >> 16);
    reply[7] = (uint8_t)(blackbox_count() >> 24);
    for (i = 0; i < 7; ++i) {
      uint32_t code = 0;
      uint32_t value = 0;
      blackbox_entry(first + i, &code, &value);
      put32(reply + 8 + 8 * i, code);
      put32(reply + 12 + 8 * i, value);
    }
    return APP_REPLY_BYTES;
  }
  if (mode == kTestModeResetKeep) {
    reply[4] = model_kept_at_start;
    reply[5] = model_kept_over_reset();
    reply[6] = (uint8_t)reset_cause();
    reply[7] = hardware.model;
    return APP_REPLY_BYTES;
  }
  if (mode == 7) blackbox_clear();
  if (mode == 10) usb3_sample_state(values);
  if (mode == 9) {
    put32(reply + 4, R32_STK_CTLR);
    put32(reply + 8, R32_STK_CNTL);
    put32(reply + 12, R32_STK_CNTH);
    put32(reply + 16, R32_STK_CNTFG);
    return APP_REPLY_BYTES;
  }
  if (mode == 13) {
    put32(reply + 4, fpga_queue.count);
    put32(reply + 8, fpga_runner.waiting);
    put32(reply + 12, fpga_runner.wait_ms);
    put32(reply + 16, fpga_runner.wait_start_ms);
    put32(reply + 20, state.starts_refused);
    return APP_REPLY_BYTES;
  }
  if (mode == 8) {
    data_link_status(reply + 4);
    return APP_REPLY_BYTES;
  }
  for (i = 0; i < 4; ++i) put32(reply + 4 + 4 * i, values[i]);
  return APP_REPLY_BYTES;
}

static uint16_t handle(const uint8_t* command, uint16_t length, uint8_t* reply) {
  if (length >= 2 && command[0] == 0x0A && command[1] == 0xA5) return usb3_test(command, length, reply);
  return app_handle_command(&state, &hardware, command, length, reply);
}

static uint16_t vendor(uint8_t request, uint16_t max_length, uint8_t* reply) {
  return app_vendor_request(&hardware, request, max_length, reply);
}

static uint8_t read_model(void) {
  uint32_t word = 0;
  return flash_read(MODEL_FACTORY_WORD_ADDRESS, &word, 1) == kFlashOk ? model_from_factory_word(word) : 0;
}

int main(void) {
  clock_set_120mhz();
  uart_init_115200();
  hardware.model = read_model();
  model_kept_at_start = model_kept_over_reset();
  model_keep_over_reset(hardware.model);
  uart_write("\r\nfnirsi-dla app ");
  uart_write_hex(APP_VERSION_MAJOR, 1);
  uart_write(".");
  uart_write_hex(APP_VERSION_MINOR, 1);
  uart_write(", cause ");
  uart_write_hex(reset_cause(), 1);
  uart_write(", model ");
  uart_write_hex(hardware.model, 2);
  uart_write("\r\n");
  blackbox_log(0x5B, reset_cause());
  clock_init();
  fpga_link_init();
  fpga_queue_startup(&fpga_queue);
  interrupts_init();
  use_usb3 = 1;
  usb3_init(handle, vendor);
  interrupts_on();
  uint8_t reported = 0;
  for (;;) {
    const uint32_t now_ms = clock_now_ms();
    if (use_usb3 && app_usb3_should_give_up(&state, now_ms, usb3_link_up())) {
      uart_write("app: no USB 3 link, USB 2 for commands\r\n");
      interrupts_off();
      usb3_stop();
      use_usb3 = 0;
      usb2_init();
      interrupts_on();
    }
    if (!use_usb3) usb2_poll(handle);
    interrupts_off();
    if (use_usb3 && usb3_take_reset()) app_usb_reset(&hardware);
    fpga_runner_poll(&fpga_runner, &fpga_queue, &fpga_port);
    if (use_usb3) data_link_poll(now_ms);
    if (use_usb3 && stall_watch_due(&stall_watch, data_link_streaming() && usb3_sample_armed(), data_link_transfers(), now_ms)) {
      usb3_sample_nudge();
    }
    interrupts_on();
    if (app_watchdog_should_feed(&state, now_ms, use_usb3 ? usb3_configured() : usb2_configured())) {
      watchdog_feed();
    } else if (!reported) {
      uart_write("app: watchdog no longer fed\r\n");
      reported = 1;
    }
  }
}
