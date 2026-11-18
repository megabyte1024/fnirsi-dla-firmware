/*
 * SPDX-License-Identifier: MIT
 */

#ifndef FNIRSI_DLA_USB3_H
#define FNIRSI_DLA_USB3_H

#include <stdint.h>

typedef uint16_t (*Usb3CommandHandler)(const uint8_t* command, uint16_t length, uint8_t* reply);
typedef uint16_t (*Usb3VendorHandler)(uint8_t request, uint16_t max_length, uint8_t* reply);
typedef void (*Usb3SampleDone)(uint8_t packets_left);

void usb3_init(Usb3CommandHandler handler, Usb3VendorHandler vendor);
void usb3_interrupt(void);
void usb3_link_interrupt(void);
int usb3_configured(void);
int usb3_link_up(void);
int usb3_take_reset(void);
void usb3_stop(void);
void usb3_sample_send(const uint8_t* data, uint8_t packets, uint16_t last_bytes);
void usb3_sample_reset(Usb3SampleDone done);
int usb3_sample_armed(void);
void usb3_sample_nudge(void);
void usb3_sample_state(uint32_t* values);
void usb3_read_registers(uint32_t* values);

#endif
