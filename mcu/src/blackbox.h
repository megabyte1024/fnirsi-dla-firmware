/*
 * SPDX-License-Identifier: MIT
 */

#ifndef FNIRSI_DLA_BLACKBOX_H
#define FNIRSI_DLA_BLACKBOX_H

#include <stdint.h>

#define BLACKBOX_ENTRIES 256u

void blackbox_log(uint32_t code, uint32_t value);
void blackbox_clear(void);
void blackbox_repeat(uint32_t code);
uint32_t blackbox_count(void);
void blackbox_entry(uint32_t index, uint32_t* code, uint32_t* value);
void blackbox_catch_traps(void);

#endif
