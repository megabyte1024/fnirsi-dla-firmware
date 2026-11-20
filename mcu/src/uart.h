/*
 * SPDX-License-Identifier: MIT
 */

#ifndef FNIRSI_DLA_UART_H
#define FNIRSI_DLA_UART_H

#include <stdint.h>

void uart_init_115200(void);
void uart_write(const char* text);
void uart_write_hex(uint32_t value, unsigned digits);
void uart_flush(void);

#endif
