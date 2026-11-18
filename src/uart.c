/*
 * SPDX-License-Identifier: MIT
 */

#include "uart.h"

#include "ch569_regs.h"

enum : uint32_t {
  kSystemClockHz = 120000000u,
  kBaud = 115200u,
  kUartDivisor = (kSystemClockHz * 2u / 16u * 10u / kBaud + 5u) / 10u,
  kPinRxd1 = 1u << 7,
  kPinTxd1 = 1u << 8,
};

enum : uint8_t {
  kPreDivider = 1,
  kFifoOnClearTrigger = 0xC7,
  kEightBitsNoParity = 0x03,
  kTxdPinEnable = 0x40,
};

void uart_init_115200(void) {
  R8_UART1_DIV = kPreDivider;
  R16_UART1_DL = (uint16_t)kUartDivisor;
  R8_UART1_FCR = kFifoOnClearTrigger;
  R8_UART1_LCR = kEightBitsNoParity;
  R8_UART1_IER = kTxdPinEnable;
  R32_PA_SMT |= kPinRxd1 | kPinTxd1;
  R32_PA_DIR |= kPinTxd1;
}

static void uart_put(char c) {
  while ((R8_UART1_LSR & kUartTxFifoEmpty) == 0) {
  }
  R8_UART1_THR = (uint8_t)c;
}

void uart_write(const char* text) {
  while (*text != '\0') uart_put(*text++);
}

void uart_write_hex(uint32_t value, unsigned digits) {
  for (unsigned i = digits; i > 0; --i) {
    const unsigned nibble = (value >> ((i - 1u) * 4u)) & 0xFu;
    uart_put((char)(nibble < 10u ? '0' + nibble : 'A' + nibble - 10u));
  }
}

void uart_flush(void) {
  while ((R8_UART1_LSR & kUartTxAllEmpty) == 0) {
  }
}
