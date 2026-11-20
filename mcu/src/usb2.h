/*
 * SPDX-License-Identifier: MIT
 */

#ifndef FNIRSI_DLA_USB2_H
#define FNIRSI_DLA_USB2_H

#include <stdint.h>

enum : uint16_t { kUsb2CommandPacketBytes = 512 };

typedef uint16_t (*Usb2CommandHandler)(const uint8_t* command, uint16_t length, uint8_t* reply);

void usb2_init(void);
void usb2_poll(Usb2CommandHandler handler);
int usb2_configured(void);

#endif
