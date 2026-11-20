/*
 * SPDX-License-Identifier: MIT
 */

#include "usb3_descriptors.h"

#define LOW(value) ((uint8_t)((value) & 0xFFu))
#define HIGH(value) ((uint8_t)((value) >> 8))
#define VENDOR_ID 0x1A86u
#define PRODUCT_ID 0x5537u
#define DEVICE_RELEASE 0x0100u
#define USB_RELEASE_3_0 0x0300u
#define VENDOR_CLASS 0xFFu
#define BULK 0x02u
#define PACKET_BYTES 1024u
#define BULK_ENDPOINT(address, max_burst)                                                                          \
  0x07, 0x05, (address), BULK, LOW(PACKET_BYTES), HIGH(PACKET_BYTES), 0x00, 0x06, 0x30, (max_burst), 0x00, 0x00, 0x00

const uint8_t usb3_device_descriptor[USB3_DEVICE_DESCRIPTOR_BYTES] = {
    0x12, 0x01, LOW(USB_RELEASE_3_0), HIGH(USB_RELEASE_3_0), VENDOR_CLASS, 0x80, 0x55, 0x09,
    LOW(VENDOR_ID), HIGH(VENDOR_ID), LOW(PRODUCT_ID), HIGH(PRODUCT_ID), LOW(DEVICE_RELEASE), HIGH(DEVICE_RELEASE),
    0x01, 0x02, 0x03, 0x01,
};

const uint8_t usb3_config_descriptor[USB3_CONFIG_DESCRIPTOR_BYTES] = {
    0x09, 0x02, LOW(USB3_CONFIG_DESCRIPTOR_BYTES), HIGH(USB3_CONFIG_DESCRIPTOR_BYTES), 0x01, 0x01, 0x00, 0x80, 0x64,
    0x09, 0x04, 0x00, 0x00, 0x0E, VENDOR_CLASS, VENDOR_CLASS, VENDOR_CLASS, 0x00,
    BULK_ENDPOINT(0x81, 3), BULK_ENDPOINT(0x01, 2), BULK_ENDPOINT(0x82, 3), BULK_ENDPOINT(0x02, 2),
    BULK_ENDPOINT(0x83, 2), BULK_ENDPOINT(0x03, 3), BULK_ENDPOINT(0x84, 2), BULK_ENDPOINT(0x04, 3),
    BULK_ENDPOINT(0x85, 2), BULK_ENDPOINT(0x05, 3), BULK_ENDPOINT(0x86, 2), BULK_ENDPOINT(0x06, 3),
    BULK_ENDPOINT(0x87, 2), BULK_ENDPOINT(0x07, 3),
};

const uint8_t usb3_bos_descriptor[USB3_BOS_DESCRIPTOR_BYTES] = {
    0x05, 0x0F, LOW(USB3_BOS_DESCRIPTOR_BYTES), HIGH(USB3_BOS_DESCRIPTOR_BYTES), 0x02,
    0x07, 0x10, 0x02, 0x06, 0x00, 0x00, 0x00,
    0x0A, 0x10, 0x03, 0x00, 0x0E, 0x00, 0x01, 0x0A, 0xFF, 0x07,
};

static const uint8_t kLanguages[] = {0x04, 0x03, 0x09, 0x04};
static const uint8_t kManufacturer[] = {0x08, 0x03, 'W', 0, 'C', 0, 'H', 0};
static const uint8_t kProduct[] = {0x26, 0x03, 'W', 0, 'C', 0, 'H', 0, ' ', 0, 'U', 0, 'S', 0, 'B', 0, '3', 0, '.', 0,
                                   '0', 0, ' ', 0, 'D', 0, 'E', 0, 'V', 0, 'I', 0, 'C', 0, 'E', 0, ' ', 0};
static const uint8_t kSerial[] = {0x16, 0x03, '0', 0, '1', 0, '2', 0, '3', 0, '4', 0,
                                  '5', 0, '6', 0, '7', 0, '8', 0, '9', 0};

const uint8_t* const usb3_string_descriptors[USB3_STRING_DESCRIPTORS] = {kLanguages, kManufacturer, kProduct, kSerial};
