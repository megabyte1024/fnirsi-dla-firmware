/*
 * SPDX-License-Identifier: MIT
 */

#include "app_protocol.h"

#include <string.h>

#include "fpga_steps.h"

#define COMMAND_MARK 0x0Au
#define CMD_DEVICE_DATA 0x10u
#define CMD_FLUSH 0x0Bu
#define CMD_VERSION 0x81u
#define CMD_BOOT_SELECT 0x89u
#define CMD_RESET 0x90u
#define BOOTLOADER_NEXT 1u
#define MODEL_DL32 0xFAu
#define MODEL_DL16 0xFCu
#define CMD_STATUS_LIGHT 0x87u
#define FRAME_MARK_OFFSET 8u
#define FRAME_HEADER_BYTES 12u
#define FRAME_END 0x0Bu
#define FRAME_PARAMETERS 0x11u
#define FRAME_START 0x12u
#define FRAME_STOP 0x15u
#define PARAMETERS_RATE_INDEX 9u
#define PARAMETERS_THRESHOLD 10u
#define PARAMETERS_BYTES 12u
#define START_CHANNEL_MASK 1u
#define START_BYTES 3u
#define FRAME_PWM 0x17u
#define FRAME_EXIT 0x18u
#define FRAME_THRESHOLD 0x19u
#define DEVICE_DATA_BYTES 19u
#define DEVICE_DATA_GENERATION 7u
#define DEVICE_DATA_NAME 13u

static uint16_t device_data(const struct AppHardware* hardware, uint8_t* reply) {
  static const uint8_t kTemplate[DEVICE_DATA_BYTES] = {0x0A, 0x02, 0x0D, 0x00, 0xFF, 0x00, 0x01, 0x03, 0x20, 0x01,
                                                       0x00, 0x01, 0x00, 'D',  'L',  '1',  '6',  0x00, 0x0B};
  uint16_t i;
  if (hardware->model != MODEL_DL16 && hardware->model != MODEL_DL32) return APP_REPLY_BYTES;
  for (i = 0; i < DEVICE_DATA_BYTES; ++i) reply[i] = kTemplate[i];
  reply[DEVICE_DATA_GENERATION] = hardware->usb_generation();
  if (hardware->model == MODEL_DL32) {
    reply[DEVICE_DATA_NAME + 2] = '3';
    reply[DEVICE_DATA_NAME + 3] = '2';
  }
  return APP_REPLY_BYTES;
}

static uint16_t reply_status(uint8_t* reply, uint8_t command, uint8_t status) {
  reply[0] = COMMAND_MARK;
  reply[1] = command;
  reply[2] = status;
  return APP_REPLY_BYTES;
}

static void put_little_endian(uint8_t* bytes, uint64_t value, uint8_t count) {
  uint8_t i;
  for (i = 0; i < count; ++i) bytes[i] = (uint8_t)(value >> (8u * i));
}

static uint16_t stream_halt(const struct AppHardware* hardware, uint8_t* reply) {
  struct AppStreamState stream = {0, 0, 0, 0};
  hardware->stream_state(&stream);
  reply_status(reply, APP_CMD_STREAM_HALT, 1);
  reply[3] = stream.fault;
  put_little_endian(reply + 4, stream.halts, 4);
  put_little_endian(reply + 8, stream.halt_position, 8);
  put_little_endian(reply + 16, stream.fault_at, 8);
  return APP_REPLY_BYTES;
}

uint16_t app_vendor_request(const struct AppHardware* hardware, uint8_t request, uint16_t max_length,
                            uint8_t* reply) {
  uint8_t full[APP_REPLY_BYTES];
  const uint16_t length = max_length < APP_VENDOR_REPLY_BYTES ? max_length : (uint16_t)APP_VENDOR_REPLY_BYTES;
  if (request != APP_CMD_STREAM_HALT || length == 0) return 0;
  stream_halt(hardware, full);
  memcpy(reply, full, length);
  return length;
}

static uint16_t little_endian16(const uint8_t* bytes) { return (uint16_t)(bytes[0] | (bytes[1] << 8)); }

static uint16_t reply_fpga(uint8_t* reply, uint8_t command, int result) {
  reply_status(reply, command, result == 0 ? 1u : 0u);
  reply[3] = (uint8_t)(result < 0 ? -result : 0);
  return APP_REPLY_BYTES;
}

static uint16_t fpga_write(const struct AppHardware* hardware, const uint8_t* command, uint16_t length,
                           uint8_t* reply) {
  if (length < 6) {
    reply_status(reply, APP_CMD_FPGA_WRITE, 0);
    reply[3] = APP_FPGA_BAD_LENGTH;
    return APP_REPLY_BYTES;
  }
  return reply_fpga(reply, APP_CMD_FPGA_WRITE,
                    hardware->fpga_write(little_endian16(command + 2), little_endian16(command + 4)));
}

static uint16_t fpga_read(const struct AppHardware* hardware, const uint8_t* command, uint16_t length,
                          uint8_t* reply) {
  struct AppFpgaReply answer = {0, 0, 0, 0};
  int result;
  if (length < 4) {
    reply_status(reply, APP_CMD_FPGA_READ, 0);
    reply[3] = APP_FPGA_BAD_LENGTH;
    return APP_REPLY_BYTES;
  }
  result = hardware->fpga_read(little_endian16(command + 2), &answer);
  reply_fpga(reply, APP_CMD_FPGA_READ, result);
  if (result != 0) return APP_REPLY_BYTES;
  reply[4] = (uint8_t)answer.word;
  reply[5] = (uint8_t)(answer.word >> 8);
  reply[6] = (uint8_t)(answer.word >> 16);
  reply[7] = (uint8_t)(answer.word >> 24);
  reply[8] = answer.status;
  reply[9] = (uint8_t)answer.length0;
  reply[10] = (uint8_t)(answer.length0 >> 8);
  reply[11] = (uint8_t)answer.length1;
  reply[12] = (uint8_t)(answer.length1 >> 8);
  return APP_REPLY_BYTES;
}

static int is_frame(const uint8_t* command, uint16_t length) {
  return length > FRAME_HEADER_BYTES && command[0] == 0 && command[7] == 0 && command[FRAME_MARK_OFFSET] == COMMAND_MARK;
}

static void handle_frame(struct AppState* state, const struct AppHardware* hardware, const uint8_t* packet,
                         uint16_t length) {
  const uint16_t length_field = little_endian16(packet + 10);
  const uint8_t* payload = packet + FRAME_HEADER_BYTES;
  uint16_t payload_length;
  if (length_field < 2 || (uint32_t)FRAME_HEADER_BYTES + length_field - 2u >= length) return;
  payload_length = (uint16_t)(length_field - 2u);
  if (payload[payload_length] != FRAME_END) return;
  switch (packet[9]) {
    case FRAME_PARAMETERS:
      if (payload_length >= PARAMETERS_BYTES) {
        state->rate_index = payload[PARAMETERS_RATE_INDEX];
        state->centivolts = (int16_t)little_endian16(payload + PARAMETERS_THRESHOLD);
      }
      break;
    case FRAME_START:
      if (payload_length >= START_BYTES) {
        if (hardware->usb_generation() != 3u) {
          hardware->fault_no_usb3();
          ++state->starts_refused;
        } else if (fpga_queue_capture_start(hardware->fpga_queue, state->rate_index, state->centivolts,
                                            little_endian16(payload + START_CHANNEL_MASK)) != 0) {
          ++state->starts_refused;
        }
      }
      break;
    case FRAME_STOP:
      hardware->stop_data();
      fpga_queue_stop(hardware->fpga_queue);
      break;
    case FRAME_EXIT:
      fpga_queue_exit(hardware->fpga_queue);
      break;
    case FRAME_THRESHOLD:
      if (payload_length >= 2) fpga_queue_threshold(hardware->fpga_queue, (int16_t)little_endian16(payload));
      break;
    case FRAME_PWM:
      fpga_queue_pwm(hardware->fpga_queue, payload, payload_length);
      break;
    default:
      break;
  }
}

uint16_t app_handle_command(struct AppState* state, const struct AppHardware* hardware, const uint8_t* command,
                            uint16_t length, uint8_t* reply) {
  uint16_t i;
  if (is_frame(command, length)) {
    handle_frame(state, hardware, command, length);
    return 0;
  }
  if (length < 2 || command[0] != COMMAND_MARK) return 0;
  for (i = 0; i < APP_REPLY_BYTES; ++i) reply[i] = 0;
  switch (command[1]) {
    case CMD_VERSION:
      reply_status(reply, CMD_VERSION, 1);
      reply[3] = hardware->boot_selection() == BOOTLOADER_NEXT ? 0x62u : 0x61u;
      reply[4] = APP_VERSION_MAJOR;
      reply[5] = APP_VERSION_MINOR;
      reply[6] = 1;
      reply[8] = (uint8_t)(hardware->model == MODEL_DL16 || hardware->model == MODEL_DL32 ? 1u : 0u);
      memcpy(reply + APP_OWN_MARK_AT, APP_OWN_MARK, sizeof APP_OWN_MARK - 1u);
      reply[APP_BUSY_AT] = (uint8_t)(hardware->fpga_queue->count != 0u || hardware->fpga_runner->waiting ? 1u : 0u);
      return APP_REPLY_BYTES;
    case CMD_FLUSH:
      for (i = 0; i < APP_REPLY_BYTES; ++i) reply[i] = 0xFF;
      return APP_REPLY_BYTES;
    case CMD_DEVICE_DATA:
      return device_data(hardware, reply);
    case CMD_BOOT_SELECT: {
      const uint8_t selection = (uint8_t)(length > 2 && command[2] != 0 ? BOOTLOADER_NEXT : 0u);
      return reply_status(reply, CMD_BOOT_SELECT, hardware->set_boot_selection(selection) == 0 ? 1u : 0u);
    }
    case CMD_RESET:
      hardware->reset();
      return 0;
    case APP_CMD_STOP_FEEDING_WATCHDOG:
      state->stop_feeding_watchdog = 1;
      return reply_status(reply, APP_CMD_STOP_FEEDING_WATCHDOG, 1);
    case CMD_STATUS_LIGHT:
      if (length > 2 && command[2] == 1) fpga_queue_light(hardware->fpga_queue, FPGA_LIGHT_GREEN);
      return 0;
    case APP_CMD_FPGA_WRITE:
      return fpga_write(hardware, command, length, reply);
    case APP_CMD_FPGA_READ:
      return fpga_read(hardware, command, length, reply);
    case APP_CMD_STREAM_HALT:
      return stream_halt(hardware, reply);
    case APP_CMD_FPGA_PULSE_PB23:
      hardware->fpga_pulse_pb23();
      return reply_status(reply, APP_CMD_FPGA_PULSE_PB23, 1);
    default:
      return 0;
  }
}

int app_watchdog_should_feed(struct AppState* state, uint32_t uptime_ms, int usb_configured) {
  if (usb_configured) state->usb_was_configured = 1;
  if (state->stop_feeding_watchdog) return 0;
  return state->usb_was_configured || uptime_ms < APP_WATCHDOG_GRACE_MS;
}

void app_usb_reset(const struct AppHardware* hardware) {
  hardware->stop_data();
  fpga_queue_stop(hardware->fpga_queue);
}

int app_usb3_should_give_up(struct AppState* state, uint32_t uptime_ms, int usb3_link_up) {
  if (usb3_link_up) state->usb3_link_was_up = 1;
  return !state->usb3_link_was_up && uptime_ms >= APP_USB3_WAIT_MS;
}
