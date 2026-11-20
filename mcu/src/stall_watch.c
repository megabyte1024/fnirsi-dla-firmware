/*
 * SPDX-License-Identifier: MIT
 */

#include "stall_watch.h"

int stall_watch_due(struct StallWatch* watch, int armed, uint32_t transfers, uint32_t now_ms) {
  if (!armed || transfers != watch->transfers) {
    watch->transfers = transfers;
    watch->since_ms = now_ms;
    return 0;
  }
  if ((uint32_t)(now_ms - watch->since_ms) < STALL_WATCH_MS) return 0;
  watch->since_ms = now_ms;
  return 1;
}
