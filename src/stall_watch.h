/*
 * SPDX-License-Identifier: MIT
 */

#ifndef FNIRSI_DLA_STALL_WATCH_H
#define FNIRSI_DLA_STALL_WATCH_H

#include <stdint.h>

#define STALL_WATCH_MS 20u

struct StallWatch {
  uint32_t transfers;
  uint32_t since_ms;
};

int stall_watch_due(struct StallWatch* watch, int armed, uint32_t transfers, uint32_t now_ms);

#endif
