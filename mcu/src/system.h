/*
 * SPDX-License-Identifier: MIT
 */

#ifndef FNIRSI_DLA_SYSTEM_H
#define FNIRSI_DLA_SYSTEM_H

#include <stdint.h>

enum ResetCause : uint8_t {
  kResetCauseSoftware = 0,
  kResetCausePowerOn = 1,
  kResetCauseWatchdog = 2,
  kResetCausePin = 3,
};

void clock_set_120mhz(void);
enum ResetCause reset_cause(void);
[[noreturn]] void software_reset(void);
void watchdog_start(void);
void watchdog_feed(void);

#endif
