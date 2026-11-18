/*
 * SPDX-License-Identifier: MIT
 */

#ifndef FNIRSI_DLA_DATA_LINK_H
#define FNIRSI_DLA_DATA_LINK_H

#include <stdint.h>

#define DATA_LINK_STATUS_BYTES 60u

void data_link_start(void);
void data_link_fault_no_usb3(void);
void data_link_interrupt(void);
void data_link_stop(void);
uint32_t data_link_transfers(void);
int data_link_streaming(void);
void data_link_halt(void);
int data_link_fast(uint32_t* address);
void data_link_fast_taken(void);
void data_link_poll(uint32_t now_ms);
struct AppStreamState;
void data_link_stream_state(struct AppStreamState* state);
void data_link_status(uint8_t* bytes);
uint8_t* data_link_ring(void);

#endif
