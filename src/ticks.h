/*
 * SPDX-License-Identifier: MIT
 */

#ifndef FNIRSI_DLA_TICKS_H
#define FNIRSI_DLA_TICKS_H

#include <stdint.h>

static inline uint32_t systick_elapsed(uint32_t earlier, uint32_t later) { return earlier - later; }

#endif
