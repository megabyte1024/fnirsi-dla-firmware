/*
 * SPDX-License-Identifier: MIT
 */

#include "boot_protocol.h"

#define COMMAND_MARK 0x0Au
#define CMD_DEVICE_DATA 0x10u
#define CMD_FLUSH 0x0Bu
#define CMD_VERSION 0x81u
#define CMD_IMAGE_START 0x82u
#define CMD_IMAGE_CHUNK 0x83u
#define CMD_FPGA_START 0x85u
#define CMD_FPGA_CHUNK 0x86u
#define CMD_BOOT_SELECT 0x89u
#define CMD_RESET 0x90u
#define CMD_FPGA_CRC 0xB2u
#define BOOTLOADER_NEXT 1u
#define ACCEPTED 1u
#define REFUSED 0u
#define MIN_IMAGE_BYTES 8u
#define CRC_COMMAND_BYTES 14u
#define NO_FLASH_ID_LOW 0u
#define NO_FLASH_ID_HIGH 0xFFFFFFu

int boot_application_complete(const struct BootHardware* hardware) {
  uint32_t word = BOOT_ERASED_WORD;
  return hardware->read(BOOT_APP_ADDRESS, &word, 1) == 0 && word != BOOT_ERASED_WORD && word != 0xFFFFFFFFu;
}

int boot_should_start_application(const struct BootHardware* hardware) {
  return hardware->reset_cause() != BOOT_RESET_CAUSE_WATCHDOG && hardware->boot_selection() != BOOTLOADER_NEXT &&
         boot_application_complete(hardware);
}

int boot_application_wants_watchdog(const struct BootHardware* hardware) {
  uint32_t marker = 0;
  return hardware->read(BOOT_MARKER_ADDRESS, &marker, 1) == 0 && marker == BOOT_APP_MARKER;
}

static uint32_t read_u32(const uint8_t* bytes) {
  return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void put_u32(uint8_t* bytes, uint32_t value) {
  bytes[0] = (uint8_t)value;
  bytes[1] = (uint8_t)(value >> 8);
  bytes[2] = (uint8_t)(value >> 16);
  bytes[3] = (uint8_t)(value >> 24);
}

static void prepare_flash_bus(struct BootUpdate* update, const struct BootHardware* hardware) {
  if (!update->bus_ready) {
    hardware->flash_bus_prepare();
    update->bus_ready = 1;
  }
}

static uint8_t start_image(struct BootUpdate* update, const struct BootHardware* hardware, uint32_t size,
                           uint8_t kind) {
  update->open = BOOT_OPEN_NONE;
  update->received = 0;
  if (size < MIN_IMAGE_BYTES ||
      size > (kind == BOOT_OPEN_FPGA ? BOOT_FPGA_AREA_BYTES : BOOT_APP_LIMIT - BOOT_APP_ADDRESS)) {
    return REFUSED;
  }
  if (kind == BOOT_OPEN_FPGA) {
    uint32_t id;
    prepare_flash_bus(update, hardware);
    id = hardware->fpga_id();
    if (id == NO_FLASH_ID_LOW || id == NO_FLASH_ID_HIGH) return REFUSED;
  }
  update->image_size = size;
  update->open = kind;
  return ACCEPTED;
}

static int program_page(const struct BootHardware* hardware, uint32_t address, const uint8_t* data, uint32_t count) {
  uint8_t check[BOOT_FPGA_PAGE_BYTES];
  uint32_t i;
  if (hardware->fpga_program(address, data, count) != 0 || hardware->fpga_read(address, check, count) != 0) return -1;
  for (i = 0; i < count; ++i) {
    if (check[i] != data[i]) return -1;
  }
  return 0;
}

static int program_held_sector(const struct BootUpdate* update, const struct BootHardware* hardware, uint32_t size) {
  const uint32_t held = size < BOOT_FPGA_HOLD_BYTES ? size : BOOT_FPGA_HOLD_BYTES;
  uint32_t page = (held - 1u) & ~(BOOT_FPGA_PAGE_BYTES - 1u);
  for (;;) {
    const uint32_t count = held - page < BOOT_FPGA_PAGE_BYTES ? held - page : BOOT_FPGA_PAGE_BYTES;
    if (program_page(hardware, page, update->hold + page, count) != 0) return -1;
    if (page == 0) return 0;
    page -= BOOT_FPGA_PAGE_BYTES;
  }
}

static uint8_t write_fpga_chunk(struct BootUpdate* update, const struct BootHardware* hardware, const uint8_t* data,
                                uint32_t length) {
  const uint32_t offset = update->received;
  const uint32_t size = update->image_size;
  const uint32_t next = offset + length;
  uint32_t i;
  if (update->open != BOOT_OPEN_FPGA) return REFUSED;
  update->open = BOOT_OPEN_NONE;
  if (length == 0 || length > BOOT_FPGA_PAGE_BYTES || next > size || (length != BOOT_FPGA_PAGE_BYTES && next != size)) {
    return REFUSED;
  }
  if ((offset % BOOT_SECTOR_BYTES) == 0 && hardware->fpga_erase(offset) != 0) return REFUSED;
  if (offset < BOOT_FPGA_HOLD_BYTES) {
    for (i = 0; i < length; ++i) update->hold[offset + i] = data[i];
  } else if (program_page(hardware, offset, data, length) != 0) {
    return REFUSED;
  }
  if (next == size && program_held_sector(update, hardware, size) != 0) return REFUSED;
  update->received = next;
  if (next != size) update->open = BOOT_OPEN_FPGA;
  return ACCEPTED;
}

static uint8_t crc_range(const struct BootHardware* hardware, uint32_t address, uint32_t count, uint32_t* crc) {
  uint8_t buffer[BOOT_FPGA_PAGE_BYTES];
  uint32_t value = ~*crc;
  if (count > BOOT_FPGA_CRC_MAX_BYTES || address > BOOT_FPGA_FLASH_BYTES || count > BOOT_FPGA_FLASH_BYTES - address) {
    return REFUSED;
  }
  while (count > 0) {
    const uint32_t piece = count < BOOT_FPGA_PAGE_BYTES ? count : BOOT_FPGA_PAGE_BYTES;
    uint32_t i;
    if (hardware->fpga_read(address, buffer, piece) != 0) return REFUSED;
    for (i = 0; i < piece; ++i) {
      uint32_t bit;
      value ^= buffer[i];
      for (bit = 0; bit < 8u; ++bit) value = (value >> 1) ^ (0xEDB88320u & (0u - (value & 1u)));
    }
    address += piece;
    count -= piece;
  }
  *crc = ~value;
  return ACCEPTED;
}

static uint8_t write_chunk(struct BootUpdate* update, const struct BootHardware* hardware, const uint8_t* data,
                           uint32_t length) {
  uint32_t words[BOOT_MAX_CHUNK_BYTES / 4u];
  const uint32_t word_count = (length + 3u) / 4u;
  const uint32_t address = BOOT_APP_ADDRESS + update->received;
  uint32_t skip = 0;
  uint32_t i;
  if (update->open != BOOT_OPEN_MCU || length == 0 || length > BOOT_MAX_CHUNK_BYTES ||
      update->received + length > update->image_size || (update->received & 3u) != 0) {
    return REFUSED;
  }
  for (i = 0; i < word_count; ++i) {
    uint32_t word = 0xFFFFFFFFu;
    uint32_t b;
    for (b = 0; b < 4u && i * 4u + b < length; ++b) {
      word &= ~(0xFFu << (b * 8u));
      word |= (uint32_t)data[i * 4u + b] << (b * 8u);
    }
    words[i] = word;
  }
  if ((update->received % BOOT_SECTOR_BYTES) == 0 && hardware->erase_sector(address) != 0) return REFUSED;
  if (update->received == 0) {
    update->first_word = words[0];
    skip = 1;
  }
  if (word_count > skip && hardware->write(address + skip * 4u, words + skip, word_count - skip) != 0) return REFUSED;
  update->received += length;
  if (update->received == update->image_size) {
    update->open = BOOT_OPEN_NONE;
    if (hardware->write(BOOT_APP_ADDRESS, &update->first_word, 1) != 0) return REFUSED;
  }
  return ACCEPTED;
}

static uint16_t reply_status(uint8_t* reply, uint8_t command, uint8_t status) {
  reply[0] = COMMAND_MARK;
  reply[1] = command;
  reply[2] = status;
  return BOOT_REPLY_BYTES;
}

static uint16_t reply_crc(struct BootUpdate* update, const struct BootHardware* hardware, const uint8_t* command,
                          uint8_t* reply) {
  uint32_t crc = read_u32(command + 10);
  uint32_t id;
  prepare_flash_bus(update, hardware);
  reply_status(reply, CMD_FPGA_CRC, crc_range(hardware, read_u32(command + 2), read_u32(command + 6), &crc));
  put_u32(reply + 4, crc);
  id = hardware->fpga_id();
  reply[8] = (uint8_t)(id >> 16);
  reply[9] = (uint8_t)(id >> 8);
  reply[10] = (uint8_t)id;
  put_u32(reply + 12, update->received);
  reply[15] = update->open;
  return BOOT_REPLY_BYTES;
}

uint16_t boot_handle_command(struct BootUpdate* update, const struct BootHardware* hardware, const uint8_t* command,
                             uint16_t length, uint8_t* reply) {
  uint16_t i;
  if (length < 2 || command[0] != COMMAND_MARK) return 0;
  for (i = 0; i < BOOT_REPLY_BYTES; ++i) reply[i] = 0;
  switch (command[1]) {
    case CMD_VERSION:
      reply_status(reply, CMD_VERSION, 1);
      reply[3] = hardware->boot_selection() == BOOTLOADER_NEXT ? 0x62u : 0x61u;
      reply[6] = 1;
      reply[9] = BOOT_PROTOCOL_MARK;
      return BOOT_REPLY_BYTES;
    case CMD_FLUSH:
      for (i = 0; i < BOOT_REPLY_BYTES; ++i) reply[i] = 0xFF;
      return BOOT_REPLY_BYTES;
    case CMD_DEVICE_DATA:
      return BOOT_REPLY_BYTES;
    case CMD_IMAGE_START:
    case CMD_FPGA_START:
      if (length < 6) return 0;
      return reply_status(reply, command[1],
                          start_image(update, hardware, read_u32(command + 2),
                                      command[1] == CMD_FPGA_START ? BOOT_OPEN_FPGA : BOOT_OPEN_MCU));
    case CMD_IMAGE_CHUNK:
    case CMD_FPGA_CHUNK: {
      uint32_t data_length = 0;
      if (length >= 4) {
        data_length = (uint32_t)command[2] | ((uint32_t)command[3] << 8);
        if (data_length > (uint32_t)length - 4u) data_length = 0;
      }
      return reply_status(reply, command[1],
                          command[1] == CMD_FPGA_CHUNK ? write_fpga_chunk(update, hardware, command + 4, data_length)
                                                       : write_chunk(update, hardware, command + 4, data_length));
    }
    case CMD_FPGA_CRC:
      if (length < CRC_COMMAND_BYTES) return 0;
      return reply_crc(update, hardware, command, reply);
    case CMD_BOOT_SELECT: {
      const uint8_t selection = (uint8_t)(length > 2 && command[2] != 0 ? BOOTLOADER_NEXT : 0u);
      return reply_status(reply, CMD_BOOT_SELECT, hardware->set_boot_selection(selection) == 0 ? ACCEPTED : REFUSED);
    }
    case CMD_RESET:
      hardware->reset();
      return 0;
    default:
      return 0;
  }
}
