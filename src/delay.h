/*
 * SPDX-License-Identifier: MIT
 */

#ifndef FNIRSI_DLA_DELAY_H
#define FNIRSI_DLA_DELAY_H

#include <stdint.h>

void clock_init(void);
uint32_t clock_now_ms(void);
void delay_us(uint32_t microseconds);
void delay_ms(uint32_t milliseconds);

#endif
