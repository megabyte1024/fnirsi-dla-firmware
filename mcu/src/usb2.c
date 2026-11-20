/*
 * SPDX-License-Identifier: MIT
 */

#include "usb2.h"

#include "ch569_regs.h"

#define R8_USB_CTRL CH569_REG8(0x40009000u)
#define R8_USB_INT_EN CH569_REG8(0x40009002u)
#define R8_USB_DEV_AD CH569_REG8(0x40009003u)
#define R8_USB_INT_FG CH569_REG8(0x4000900Au)
#define R8_USB_INT_ST CH569_REG8(0x4000900Bu)
#define R16_USB_RX_LEN CH569_REG16(0x4000900Cu)
#define R8_UEP2_3_MOD CH569_REG8(0x40009011u)
#define R32_UEP0_RT_DMA CH569_REG32(0x40009014u)
#define R32_UEP2_RX_DMA CH569_REG32(0x4000901Cu)
#define R32_UEP2_TX_DMA CH569_REG32(0x40009038u)
#define R16_UEP0_MAX_LEN CH569_REG16(0x40009050u)
#define R16_UEP2_MAX_LEN CH569_REG16(0x40009058u)
#define R16_UEP0_T_LEN CH569_REG16(0x40009070u)
#define R8_UEP0_TX_CTRL CH569_REG8(0x40009072u)
#define R8_UEP0_RX_CTRL CH569_REG8(0x40009073u)
#define R16_UEP2_T_LEN CH569_REG16(0x40009078u)
#define R8_UEP2_TX_CTRL CH569_REG8(0x4000907Au)
#define R8_UEP2_RX_CTRL CH569_REG8(0x4000907Bu)

enum : uint8_t {
  kHighSpeedDevice = 0x20 | 0x10 | 0x08 | 0x01,
  kInterruptSources = 0x20 | 0x02 | 0x01,
  kFlagSetup = 0x20,
  kFlagTransfer = 0x02,
  kFlagBusReset = 0x01,
  kStatusToggleOk = 0x40,
  kEp2Enable = 0x08 | 0x04,
  kResAck = 0x00,
  kResNak = 0x02,
  kResStall = 0x03,
  kResMask = 0x03,
  kToggle1 = 0x08,
  kTokenOut = 0,
  kTokenIn = 2,
  kEp0Size = 64,
};

enum : uint8_t {
  kReqGetStatus = 0,
  kReqClearFeature = 1,
  kReqSetAddress = 5,
  kReqGetDescriptor = 6,
  kReqGetConfiguration = 8,
  kReqSetConfiguration = 9,
  kDescDevice = 1,
  kDescConfiguration = 2,
  kDescString = 3,
  kDescQualifier = 6,
  kTypeMask = 0x60,
};

static const uint8_t kDeviceDescriptor[] = {18, 1, 0x00, 0x02, 0xFF, 0x00, 0x00, kEp0Size, 0x86, 0x1A, 0x37, 0x55,
                                            0x00, 0x01, 0, 0, 0, 1};
static const uint8_t kQualifierDescriptor[] = {10, 6, 0x00, 0x02, 0xFF, 0x00, 0x00, kEp0Size, 1, 0};
static const uint8_t kConfigurationDescriptor[] = {
    9, 2, 39, 0, 1, 1, 0, 0x80, 50,
    9, 4, 0, 0, 3, 0xFF, 0x00, 0x00, 0,
    7, 5, 0x81, 2, 0x00, 0x02, 0,
    7, 5, 0x02, 2, 0x00, 0x02, 0,
    7, 5, 0x82, 2, 0x00, 0x02, 0,
};
static const uint8_t kLanguageDescriptor[] = {4, 3, 0x09, 0x04};

static uint8_t ep0_buffer[kEp0Size] __attribute__((section(".dmadata"), aligned(16)));
static uint8_t ep2_rx_buffer[kUsb2CommandPacketBytes] __attribute__((section(".dmadata"), aligned(16)));
static uint8_t ep2_tx_buffer[kUsb2CommandPacketBytes] __attribute__((section(".dmadata"), aligned(16)));
static uint8_t pending_address;
static uint8_t configuration;

static void reset_endpoints(void) {
  R8_USB_DEV_AD = 0;
  pending_address = 0;
  configuration = 0;
  R16_UEP0_T_LEN = 0;
  R8_UEP0_TX_CTRL = kResNak;
  R8_UEP0_RX_CTRL = kResAck;
  R16_UEP2_T_LEN = 0;
  R8_UEP2_TX_CTRL = kResNak;
  R8_UEP2_RX_CTRL = kResAck;
}

void usb2_init(void) {
  R8_USB_CTRL = 0;
  R8_USB_CTRL = kHighSpeedDevice;
  R8_USB_INT_EN = kInterruptSources;
  R8_UEP2_3_MOD = kEp2Enable;
  R16_UEP0_MAX_LEN = kEp0Size;
  R16_UEP2_MAX_LEN = kUsb2CommandPacketBytes;
  R32_UEP0_RT_DMA = (uint32_t)(uintptr_t)ep0_buffer;
  R32_UEP2_RX_DMA = (uint32_t)(uintptr_t)ep2_rx_buffer;
  R32_UEP2_TX_DMA = (uint32_t)(uintptr_t)ep2_tx_buffer;
  reset_endpoints();
}

static int32_t answer(const uint8_t* data, uint16_t length, uint16_t requested) {
  const uint16_t sent = length < requested ? length : requested;
  for (uint16_t i = 0; i < sent; ++i) ep0_buffer[i] = data[i];
  return sent;
}

static int32_t standard_request(void) {
  const uint8_t request = ep0_buffer[1];
  const uint8_t value_low = ep0_buffer[2];
  const uint8_t value_high = ep0_buffer[3];
  const uint8_t index_low = ep0_buffer[4];
  const uint16_t requested = (uint16_t)(ep0_buffer[6] | (ep0_buffer[7] << 8));
  static const uint8_t zero_status[2] = {0, 0};
  switch (request) {
    case kReqGetDescriptor:
      if (value_high == kDescDevice) return answer(kDeviceDescriptor, sizeof kDeviceDescriptor, requested);
      if (value_high == kDescConfiguration) {
        return answer(kConfigurationDescriptor, sizeof kConfigurationDescriptor, requested);
      }
      if (value_high == kDescQualifier) return answer(kQualifierDescriptor, sizeof kQualifierDescriptor, requested);
      if (value_high == kDescString && value_low == 0) {
        return answer(kLanguageDescriptor, sizeof kLanguageDescriptor, requested);
      }
      return -1;
    case kReqSetAddress:
      pending_address = value_low;
      return 0;
    case kReqSetConfiguration:
      configuration = value_low;
      return 0;
    case kReqGetConfiguration:
      return answer(&configuration, 1, requested);
    case kReqGetStatus:
      return answer(zero_status, 2, requested);
    case kReqClearFeature:
      if ((index_low & 0x7F) == 2) {
        if ((index_low & 0x80) != 0) {
          R8_UEP2_TX_CTRL = kResNak;
        } else {
          R8_UEP2_RX_CTRL = kResAck;
        }
      }
      return 0;
    default:
      return -1;
  }
}

static void on_setup(void) {
  const int32_t length = (ep0_buffer[0] & kTypeMask) == 0 ? standard_request() : -1;
  if (length < 0) {
    R16_UEP0_T_LEN = 0;
    R8_UEP0_TX_CTRL = kResStall;
    R8_UEP0_RX_CTRL = kResStall;
    return;
  }
  R16_UEP0_T_LEN = (uint16_t)length;
  R8_UEP0_TX_CTRL = kResAck | kToggle1;
  R8_UEP0_RX_CTRL = kResAck | kToggle1;
}

static void on_ep0(uint8_t token) {
  if (token == kTokenIn && pending_address != 0) {
    R8_USB_DEV_AD = pending_address;
    pending_address = 0;
  }
  R16_UEP0_T_LEN = 0;
  R8_UEP0_TX_CTRL = kResNak;
  R8_UEP0_RX_CTRL = kResAck | kToggle1;
}

static void on_ep2(uint8_t token, uint8_t status, Usb2CommandHandler handler) {
  if (token == kTokenOut) {
    if ((status & kStatusToggleOk) == 0) return;
    R8_UEP2_RX_CTRL ^= kToggle1;
    const uint16_t reply = handler(ep2_rx_buffer, R16_USB_RX_LEN, ep2_tx_buffer);
    if (reply != 0) {
      R16_UEP2_T_LEN = reply;
      R8_UEP2_TX_CTRL = (uint8_t)((R8_UEP2_TX_CTRL & ~kResMask) | kResAck);
    }
  } else if (token == kTokenIn) {
    R8_UEP2_TX_CTRL = (uint8_t)(((R8_UEP2_TX_CTRL ^ kToggle1) & ~kResMask) | kResNak);
  }
}

void usb2_poll(Usb2CommandHandler handler) {
  const uint8_t flags = R8_USB_INT_FG;
  if ((flags & kFlagBusReset) != 0) {
    reset_endpoints();
    R8_USB_INT_FG = kFlagBusReset;
  }
  if ((flags & kFlagSetup) != 0) {
    on_setup();
    R8_USB_INT_FG = kFlagSetup;
  }
  if ((flags & kFlagTransfer) != 0) {
    const uint8_t status = R8_USB_INT_ST;
    const uint8_t endpoint = status & 0x0F;
    const uint8_t token = (status >> 4) & 0x03;
    if (endpoint == 0) on_ep0(token);
    if (endpoint == 2) on_ep2(token, status, handler);
    R8_USB_INT_FG = kFlagTransfer;
  }
}

int usb2_configured(void) { return configuration != 0; }
