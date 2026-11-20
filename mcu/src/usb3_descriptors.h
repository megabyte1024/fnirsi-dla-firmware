/*
 * SPDX-License-Identifier: MIT
 */

#ifndef FNIRSI_DLA_USB3_DESCRIPTORS_H
#define FNIRSI_DLA_USB3_DESCRIPTORS_H

#include <stdint.h>

#define USB3_DEVICE_DESCRIPTOR_BYTES 18u
#define USB3_CONFIG_DESCRIPTOR_BYTES 200u
#define USB3_BOS_DESCRIPTOR_BYTES 22u
#define USB3_STRING_DESCRIPTORS 4u

extern const uint8_t usb3_device_descriptor[USB3_DEVICE_DESCRIPTOR_BYTES];
extern const uint8_t usb3_config_descriptor[USB3_CONFIG_DESCRIPTOR_BYTES];
extern const uint8_t usb3_bos_descriptor[USB3_BOS_DESCRIPTOR_BYTES];
extern const uint8_t* const usb3_string_descriptors[USB3_STRING_DESCRIPTORS];

#endif
