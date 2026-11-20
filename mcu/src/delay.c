/*
 * SPDX-License-Identifier: MIT
 */

#include "delay.h"

#include "ch569_regs.h"
#include "ticks.h"

enum : uint32_t {
  kSysTickTicksPerMicrosecond = 120 / 8,
  kSysTickTicksPerMillisecond = kSysTickTicksPerMicrosecond * 1000,
  kSysTickEnableAndClear = 0x101,
  kSysTickCompareNever = 0xFFFFFFFFu,
};

static uint32_t last_ticks;
static uint32_t pending_ticks;
static uint32_t now_ms;

void clock_init(void) {
  R32_STK_CTLR = 0;
  R32_STK_CMPLR = kSysTickCompareNever;
  R32_STK_CMPHR = kSysTickCompareNever;
  R32_STK_CTLR = kSysTickEnableAndClear;
  last_ticks = R32_STK_CNTL;
  pending_ticks = 0;
  now_ms = 0;
}

uint32_t clock_now_ms(void) {
  const uint32_t ticks = R32_STK_CNTL;
  pending_ticks += systick_elapsed(last_ticks, ticks);
  last_ticks = ticks;
  now_ms += pending_ticks / kSysTickTicksPerMillisecond;
  pending_ticks %= kSysTickTicksPerMillisecond;
  return now_ms;
}

void delay_us(uint32_t microseconds) {
  const uint32_t start = R32_STK_CNTL;
  const uint32_t ticks = microseconds * kSysTickTicksPerMicrosecond;
  while (systick_elapsed(start, R32_STK_CNTL) < ticks) {
  }
}

void delay_ms(uint32_t milliseconds) {
  while (milliseconds-- > 0) delay_us(1000);
}
