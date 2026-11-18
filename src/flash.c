/*
 * SPDX-License-Identifier: MIT
 */

#include "flash.h"

#include "ch569_regs.h"

enum : uint32_t {
  kInternalOffset = 0x8000,
  kBusyPolls = 0x280000,
  kPageMask = 0xFF,
};

enum : uint8_t {
  kCmdPageProgram = 0x02,
  kCmdReadStatus = 0x05,
  kCmdWriteEnable = 0x06,
  kCmdFastRead = 0x0B,
  kCmdSectorErase = 0x20,
  kControlBegin = 0x07,
  kControlShift = 0x10,
  kStatusBusy = 0x01,
  kRomCodeOffset = 0x10,
  kRomWriteEnable = 0x8C,
  kRomWriteKeep = 0x73,
  kRomCfgFixedBits = 0x80,
};

static void wait_idle(void) {
  while ((R8_SPI_ROM_CR & 0x80u) != 0) {
  }
}

static void begin(uint8_t command) {
  R8_SPI_ROM_CR = 0;
  R8_SPI_ROM_CR = kControlBegin;
  R8_SPI_ROM_DATA = command;
}

static void end(void) {
  wait_idle();
  R8_SPI_ROM_CR = 0;
}

static void send(uint8_t value) {
  wait_idle();
  R8_SPI_ROM_DATA = value;
}

static uint8_t receive(void) {
  wait_idle();
  return R8_SPI_ROM_DATA;
}

static void send_address(uint32_t address) {
  send((uint8_t)(address >> 16));
  send((uint8_t)(address >> 8));
  send((uint8_t)address);
}

static void begin_after_write_enable(uint8_t command) {
  begin(kCmdWriteEnable);
  end();
  begin(command);
}

static enum FlashResult wait_ready(void) {
  end();
  for (uint32_t poll = kBusyPolls; poll > 0; --poll) {
    begin(kCmdReadStatus);
    (void)receive();
    const uint8_t status = receive();
    end();
    if ((status & kStatusBusy) == 0) return kFlashOk;
  }
  return kFlashBusyTimeout;
}

static void allow_writes(void) {
  const uint8_t config = R8_GLOB_ROM_CFG | kRomWriteEnable;
  R8_SAFE_ACCESS_SIG = kSafeAccessKey1;
  R8_SAFE_ACCESS_SIG = kSafeAccessKey2;
  R8_GLOB_ROM_CFG = config;
}

static void forbid_writes(void) {
  const uint8_t config = (R8_GLOB_ROM_CFG & kRomWriteKeep) | kRomCfgFixedBits;
  R8_SAFE_ACCESS_SIG = kSafeAccessKey1;
  R8_SAFE_ACCESS_SIG = kSafeAccessKey2;
  R8_GLOB_ROM_CFG = config;
}

enum FlashResult flash_read(uint32_t address, uint32_t* words, uint32_t word_count) {
  if ((R8_GLOB_ROM_CFG & kRomCodeOffset) != 0) return kFlashCodeOffsetMode;
  begin(kCmdFastRead);
  send_address(address + kInternalOffset);
  (void)receive();
  (void)receive();
  for (uint32_t i = 0; i < word_count; ++i) {
    (void)receive();
    (void)receive();
    (void)receive();
    (void)receive();
    words[i] = R32_SPI_ROM_DATA32;
  }
  end();
  return kFlashOk;
}

enum FlashResult flash_erase_sector(uint32_t address) {
  if ((R8_GLOB_ROM_CFG & kRomCodeOffset) != 0) return kFlashCodeOffsetMode;
  allow_writes();
  begin_after_write_enable(kCmdSectorErase);
  send_address((address & ~(kFlashSectorBytes - 1u)) + kInternalOffset);
  const enum FlashResult result = wait_ready();
  forbid_writes();
  return result;
}

enum FlashResult flash_write(uint32_t address, const uint32_t* words, uint32_t word_count) {
  if ((R8_GLOB_ROM_CFG & kRomCodeOffset) != 0) return kFlashCodeOffsetMode;
  uint32_t target = (address & ~3u) + kInternalOffset;
  enum FlashResult result = kFlashOk;
  allow_writes();
  while (word_count > 0 && result == kFlashOk) {
    begin_after_write_enable(kCmdPageProgram);
    send_address(target);
    do {
      R32_SPI_ROM_DATA32 = *words++;
      const uint8_t shift = R8_SPI_ROM_CR | kControlShift;
      for (int pulse = 0; pulse < 4; ++pulse) {
        wait_idle();
        R8_SPI_ROM_CR = shift;
      }
      target += 4;
      --word_count;
    } while (word_count > 0 && (target & kPageMask) != 0);
    result = wait_ready();
  }
  forbid_writes();
  return result;
}
