/*
 * SPDX-License-Identifier: MIT
 */

#include "system.h"

#include "ch569_regs.h"

enum : uint8_t {
  kPllDivideBy4 = 0x44,
  kClockFromPll = 0x82,
};

void clock_set_120mhz(void) {
  R8_SAFE_ACCESS_SIG = kSafeAccessKey1;
  R8_SAFE_ACCESS_SIG = kSafeAccessKey2;
  R8_CLK_PLL_DIV = kPllDivideBy4;
  R8_CLK_CFG_CTRL = kClockFromPll;
  R8_SAFE_ACCESS_SIG = 0;
}

enum ResetCause reset_cause(void) { return (enum ResetCause)(R8_RST_BOOT_STAT & kResetCauseMask); }

void software_reset(void) {
  const uint8_t control = R8_RST_WDOG_CTRL | kSoftwareResetBits;
  R8_SAFE_ACCESS_SIG = kSafeAccessKey1;
  R8_SAFE_ACCESS_SIG = kSafeAccessKey2;
  R8_RST_WDOG_CTRL = control;
  for (;;) {
  }
}

void watchdog_feed(void) { R8_WDOG_COUNT = 0; }

void watchdog_start(void) {
  R8_WDOG_COUNT = 0;
  R8_SAFE_ACCESS_SIG = kSafeAccessKey1;
  R8_SAFE_ACCESS_SIG = kSafeAccessKey2;
  R8_RST_WDOG_CTRL = kWatchdogResetEnable;
  R8_SAFE_ACCESS_SIG = 0;
}
