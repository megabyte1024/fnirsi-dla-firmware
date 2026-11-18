/*
 * SPDX-License-Identifier: MIT
 */

#include "usb3.h"

#include <stdalign.h>

#include "CH56x_common.h"
#include "blackbox.h"
#include "data_link.h"
#include "data_path.h"
#include "delay.h"
#include "sample_offer.h"
#include "usb3_descriptors.h"
#include "wch-ch56x-lib/usb/usb30.h"
#include "wch-ch56x-lib/usb/usb30_utils.h"
#include "wch-ch56x-lib/usb/usb_device.h"

#define PACKET_BYTES 1024u
#define CONTROL_PACKET_BYTES 512u
#define COMMAND_BURST 3u
#define REPLY_BURST 4u
#define SAMPLE_BURST 4u
#define REPLY_BYTES 64u
#define ENDPOINTS 16u
#define STRING_INDEXES 256u
#define SAMPLE_ENDPOINT 1u
#define COMMAND_ENDPOINT 2u
#define USB2_OFF_SETTLE_MS 100u
#define TRANSFER_DONE 1u

static alignas(16) uint8_t control_buffer[CONTROL_PACKET_BYTES] __attribute__((section(".dmadata")));
static alignas(16) uint8_t command_buffer[PACKET_BYTES * COMMAND_BURST] __attribute__((section(".dmadata")));
static alignas(16) uint8_t reply_buffer[REPLY_BYTES] __attribute__((section(".dmadata")));

static const USB_DEVICE_CONFIG* configs[1];
static const USB_STRING_DESCR* strings[STRING_INDEXES];
static const uint8_t kEmptyString[2] = {0x02, 0x03};
static Usb3CommandHandler command_handler;
static Usb3VendorHandler vendor_handler;
static uint8_t vendor_reply[64];
static Usb3SampleDone sample_done;
static volatile uint8_t reset_seen;
static struct SampleOffer offer;

void bsp_wait_us_delay(uint32_t us) { delay_us(us); }

void bsp_wait_ms_delay(uint32_t ms) { delay_ms(ms); }

static uint16_t vendor_request(USB_SETUP* request, uint8_t** buffer) {
  enum { kVendorDeviceIn = 0xC0 };
  uint16_t length;
  if (request->bRequestType != kVendorDeviceIn) return USB_DESCR_UNSUPPORTED;
  length = vendor_handler(request->bRequest, request->wLength < sizeof vendor_reply ? request->wLength
                                                                                     : (uint16_t)sizeof vendor_reply,
                          vendor_reply);
  if (length == 0) return USB_DESCR_UNSUPPORTED;
  *buffer = vendor_reply;
  return length;
}

static void no_tx_complete(TRANSACTION_STATUS status) { (void)status; }

static uint8_t no_rx(uint8_t* const data, uint16_t size) {
  (void)data;
  (void)size;
  return ENDP_STATE_ACK;
}

static uint8_t command_received(uint8_t* const data, uint16_t size) {
  const uint16_t reply_length = command_handler(data, size, reply_buffer);
  if (reply_length > 0) endp_tx_set_new_buffer(&usb_device_0, COMMAND_ENDPOINT, reply_buffer, reply_length);
  return ENDP_STATE_ACK;
}

static void set_endpoint(volatile USB_ENDPOINT* endpoint, uint8_t* buffer, uint16_t packet_bytes, uint8_t burst,
                         uint8_t state) {
  endpoint->buffer = buffer;
  endpoint->max_packet_size = packet_bytes;
  endpoint->max_burst = burst;
  endpoint->max_packet_size_with_burst = (uint16_t)(packet_bytes * burst);
  endpoint->state = state;
}

static void set_descriptors(void) {
  uint16_t i;
  configs[0] = (const USB_DEVICE_CONFIG*)usb3_config_descriptor;
  for (i = 0; i < STRING_INDEXES; ++i) {
    strings[i] = (const USB_STRING_DESCR*)(i < USB3_STRING_DESCRIPTORS ? usb3_string_descriptors[i] : kEmptyString);
  }
  usb_device_set_usb3_device_descriptor(&usb_device_0, (const USB_DEV_DESCR*)usb3_device_descriptor);
  usb_device_set_usb3_config_descriptors(&usb_device_0, configs);
  usb_device_set_bos_descriptor(&usb_device_0, (const USB_BOS_DESCR*)usb3_bos_descriptor);
  usb_device_set_string_descriptors(&usb_device_0, strings);
}

static void set_endpoints(void) {
  uint16_t i;
  volatile usb_endpoints_t* endpoints = &usb_device_0.endpoints;
  endpoints->endp0_user_handled_control_request = vendor_request;
  for (i = 0; i < ENDPOINTS; ++i) {
    endpoints->tx_complete[i] = no_tx_complete;
    endpoints->rx_callback[i] = no_rx;
  }
  endpoints->rx_callback[COMMAND_ENDPOINT] = command_received;
  set_endpoint(&endpoints->rx[0], control_buffer, CONTROL_PACKET_BYTES, 1, ENDP_STATE_ACK);
  set_endpoint(&endpoints->rx[COMMAND_ENDPOINT], command_buffer, PACKET_BYTES, COMMAND_BURST, ENDP_STATE_ACK);
  set_endpoint(&endpoints->tx[COMMAND_ENDPOINT], reply_buffer, PACKET_BYTES, REPLY_BURST, ENDP_STATE_NAK);
  set_endpoint(&endpoints->tx[SAMPLE_ENDPOINT], data_link_ring(), PACKET_BYTES, SAMPLE_BURST, ENDP_STATE_NAK);
  usb_device_set_endpoint_mask(&usb_device_0, ENDPOINT_1_TX | ENDPOINT_2_RX | ENDPOINT_2_TX);
}

void usb3_init(Usb3CommandHandler handler, Usb3VendorHandler vendor) {
  command_handler = handler;
  vendor_handler = vendor;
  set_descriptors();
  set_endpoints();
  blackbox_log(1, 0);
  R32_USB_CONTROL = 0;
  blackbox_log(2, 0);
  delay_ms(USB2_OFF_SETTLE_MS);
  blackbox_log(3, USBSS->LINK_STATUS);
  usb30_device_init(false);
  blackbox_log(4, USBSS->LINK_STATUS);
}

bool usb30_take_ep_in(uint8_t endp_num) {
  uint8_t left;
  uint32_t address;
  if (endp_num != SAMPLE_ENDPOINT) return false;
  {
    const uint32_t control = USBSS->UEP1_TX_CTRL;
    if (((control >> 16) & 0x1Fu) == 0 && sample_offer_armed(&offer) && data_link_fast(&address)) {
      const uint32_t sequence = control & 0x03e00000u;
      USBSS->UEP1_TX_DMA = address;
      USBSS->UEP1_TX_CTRL = sequence | (1u << 28) | ((uint32_t)ACK << 26) | ((uint32_t)DATA_TRANSFER_PACKETS << 16) |
                            DATA_PACKET_BYTES;
      usb30_send_erdy(SAMPLE_ENDPOINT | IN, DATA_TRANSFER_PACKETS);
      sample_offer_arm(&offer, DATA_TRANSFER_PACKETS, sequence);
      data_link_fast_taken();
      USBSS->USB_STATUS = TRANSFER_DONE;
      return true;
    }
  }
  usb30_in_clear_interrupt(SAMPLE_ENDPOINT);
  left = sample_offer_done(&offer, USBSS->UEP1_TX_CTRL);
  if (sample_done != NULL) sample_done(left);
  USBSS->USB_STATUS = TRANSFER_DONE;
  return true;
}

static void reset_command_endpoints(uint8_t endpoint_address) {
  if (endpoint_address == COMMAND_ENDPOINT) {
    usb30_out_clear_interrupt_all(COMMAND_ENDPOINT);
    *usb30_get_rx_endpoint_total_length(COMMAND_ENDPOINT) = 0;
    *usb30_get_rx_endpoint_prev_set_max_burst(COMMAND_ENDPOINT) = COMMAND_BURST;
    USBSS->UEP2_RX_DMA = (uint32_t)(uintptr_t)command_buffer;
    usb30_out_set(COMMAND_ENDPOINT, ACK, COMMAND_BURST);
    usb30_send_erdy(COMMAND_ENDPOINT, COMMAND_BURST);
  } else if (endpoint_address == (COMMAND_ENDPOINT | IN)) {
    usb30_in_clear_interrupt_all(COMMAND_ENDPOINT);
    USBSS->UEP2_TX_DMA = (uint32_t)(uintptr_t)reply_buffer;
    usb30_in_set(COMMAND_ENDPOINT, DISABLE, NRDY, 0, 0);
    usb_device_0.endpoints.tx[COMMAND_ENDPOINT].state = ENDP_STATE_NAK;
  }
}

void usb30_clear_endpoint_halt(uint8_t endpoint_address) {
  blackbox_log(0x43, endpoint_address);
  reset_command_endpoints(endpoint_address);
  if (endpoint_address != (SAMPLE_ENDPOINT | IN)) return;
  data_link_halt();
  usb30_in_clear_interrupt_all(SAMPLE_ENDPOINT);
  sample_offer_clear(&offer, sample_done);
}

void usb3_interrupt(void) { USBSS_IRQHandler(); }

void usb3_link_interrupt(void) {
  const uint32_t flags = USBSS->LINK_INT_FLAG;
  blackbox_log(0x10, flags);
  LINK_IRQHandler();
  if ((flags & (HOT_RESET_FLAG | WARM_RESET_FLAG)) != 0) reset_seen = 1;
  blackbox_log(0x11, USBSS->LINK_STATUS);
  if ((flags & LINK_RDY_FLAG) != 0 && sample_offer_armed(&offer)) {
    const uint8_t left = (uint8_t)((USBSS->UEP1_TX_CTRL >> 16) & 0x1Fu);
    if (left != 0) {
      usb30_send_erdy(SAMPLE_ENDPOINT | IN, left);
      blackbox_log(0x16, left);
    }
  }
}

int usb3_configured(void) { return usb_device_0.state == CONFIGURED; }

int usb3_link_up(void) { return usb_device_0.speed == USB30_SUPERSPEED; }

void usb3_stop(void) { usb30_device_deinit(); }

void usb3_sample_send(const uint8_t* data, uint8_t packets, uint16_t last_bytes) {
  USBSS->UEP1_TX_DMA = (uint32_t)(uintptr_t)data;
  usb30_in_clear_interrupt(SAMPLE_ENDPOINT);
  sample_offer_arm(&offer, packets, USBSS->UEP1_TX_CTRL);
  usb30_in_set(SAMPLE_ENDPOINT, ENABLE, ACK, packets, last_bytes);
  usb30_send_erdy(SAMPLE_ENDPOINT | IN, packets);
}

int usb3_take_reset(void) {
  const int seen = reset_seen;
  reset_seen = 0;
  return seen;
}

int usb3_sample_armed(void) { return sample_offer_armed(&offer); }

void usb3_sample_nudge(void) {
  const uint8_t left = (uint8_t)((USBSS->UEP1_TX_CTRL >> 16) & 0x1Fu);
  if (!sample_offer_armed(&offer) || left == 0) return;
  usb30_send_erdy(SAMPLE_ENDPOINT | IN, left);
  blackbox_log(0x17, left);
}

void usb3_sample_reset(Usb3SampleDone done) {
  sample_done = done;
  sample_offer_arm(&offer, 0, 0);
  usb30_in_clear_interrupt(SAMPLE_ENDPOINT);
  usb30_in_set(SAMPLE_ENDPOINT, DISABLE, NRDY, 0, 0);
}

void usb3_read_registers(uint32_t* values) {
  values[0] = USBSS->LINK_STATUS;
  values[1] = USBSS->LINK_CFG;
  values[2] = USBSS->LINK_CTRL;
  values[3] = USBSS->USB_STATUS;
}

void usb3_sample_state(uint32_t* values) {
  values[0] = USBSS->UEP1_TX_CTRL;
  values[1] = USBSS->UEP1_TX_DMA;
  values[2] = USBSS->USB_STATUS;
  values[3] = USBSS->LINK_STATUS;
}
