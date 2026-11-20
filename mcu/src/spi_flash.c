/*
 * SPDX-License-Identifier: MIT
 */

#include "spi_flash.h"

#if !defined(SPI_FLASH_REG8)
#define SPI_FLASH_REG8(address) (*(volatile uint8_t*)(address))
#define SPI_FLASH_REG32(address) (*(volatile uint32_t*)(address))
#endif

#define R32_PA_DIR SPI_FLASH_REG32(0x40001040u)
#define R32_PA_OUT SPI_FLASH_REG32(0x40001048u)
#define R32_PA_CLR SPI_FLASH_REG32(0x4000104Cu)
#define R8_SPI0_CTRL_MOD SPI_FLASH_REG8(0x40004000u)
#define R8_SPI0_CTRL_CFG SPI_FLASH_REG8(0x40004001u)
#define R8_SPI0_CLOCK_DIV SPI_FLASH_REG8(0x40004003u)
#define R8_SPI0_BUFFER SPI_FLASH_REG8(0x40004004u)
#define R8_SPI0_INT_FLAG SPI_FLASH_REG8(0x40004006u)

#define PIN_CHIP_SELECT (1u << 12)
#define PIN_CLOCK (1u << 13)
#define PIN_DATA_OUT (1u << 14)
#define ERASE_POLLS 600000u
#define PROGRAM_POLLS 6000u

enum {
  kClockDivider = 8,
  kModeClear = 0x02,
  kModeMaster = 0x60,
  kCfgAutoClear = 0x10,
  kFlagFree = 0x40,
  kCmdPageProgram = 0x02,
  kCmdRead = 0x03,
  kCmdReadStatus = 0x05,
  kCmdWriteEnable = 0x06,
  kCmdSectorErase = 0x20,
  kCmdJedecId = 0x9F,
  kStatusBusy = 0x01,
  kIdle = 0xFF,
};

static uint8_t exchange(uint8_t value) {
  R8_SPI0_BUFFER = value;
  while ((R8_SPI0_INT_FLAG & kFlagFree) == 0) {
  }
  return R8_SPI0_BUFFER;
}

static void chip_select(void) { R32_PA_CLR = PIN_CHIP_SELECT; }

static void chip_release(void) { R32_PA_OUT |= PIN_CHIP_SELECT; }

void spi_flash_prepare(void) {
  R32_PA_OUT |= PIN_CHIP_SELECT;
  R32_PA_DIR |= PIN_CHIP_SELECT | PIN_CLOCK | PIN_DATA_OUT;
  R8_SPI0_CLOCK_DIV = kClockDivider;
  R8_SPI0_CTRL_MOD = kModeClear;
  R8_SPI0_CTRL_MOD = kModeMaster;
  R8_SPI0_CTRL_CFG = kCfgAutoClear;
}

static int run(uint8_t op, uint32_t address, uint8_t* bytes, uint32_t count, uint32_t polls) {
  int result = 0;
  if (polls != 0) {
    chip_select();
    (void)exchange(kCmdWriteEnable);
    chip_release();
  }
  chip_select();
  (void)exchange(op);
  if (op != kCmdJedecId) {
    (void)exchange((uint8_t)(address >> 16));
    (void)exchange((uint8_t)(address >> 8));
    (void)exchange((uint8_t)address);
  }
  for (; count > 0; --count, ++bytes) {
    const uint8_t in = exchange(*bytes);
    if (op != kCmdPageProgram) *bytes = in;
  }
  chip_release();
  if (polls != 0) {
    result = -1;
    chip_select();
    (void)exchange(kCmdReadStatus);
    for (; polls > 0; --polls) {
      if ((exchange(kIdle) & kStatusBusy) == 0) {
        result = 0;
        break;
      }
    }
    chip_release();
  }
  return result;
}

uint32_t spi_flash_id(void) {
  uint8_t id[3] = {kIdle, kIdle, kIdle};
  (void)run(kCmdJedecId, 0, id, 3, 0);
  return ((uint32_t)id[0] << 16) | ((uint32_t)id[1] << 8) | id[2];
}

int spi_flash_read(uint32_t address, uint8_t* bytes, uint32_t count) { return run(kCmdRead, address, bytes, count, 0); }

int spi_flash_erase_sector(uint32_t address) { return run(kCmdSectorErase, address, 0, 0, ERASE_POLLS); }

int spi_flash_program(uint32_t address, const uint8_t* bytes, uint32_t count) {
  return run(kCmdPageProgram, address, (uint8_t*)bytes, count, PROGRAM_POLLS);
}
