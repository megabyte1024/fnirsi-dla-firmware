/*
 * SPDX-License-Identifier: MIT
 */

#include "data_path.h"

#include "blackbox.h"

#define SEQUENCE_SHIFT 21u
#define SEQUENCE_MASK 0x1Fu

static uint32_t wrap(uint32_t offset) { return offset % DATA_RING_BYTES; }

static void set_fault(struct DataPath* path, uint8_t fault) {
  if (path->stopped || path->fault != DATA_FAULT_NONE) return;
  path->fault = fault;
  path->fault_at = path->received;
  path->paused = 1;
}

void data_path_start(struct DataPath* path, uint32_t* rx0, uint32_t* rx1) {
  path->window = 0;
  path->block = 0;
  path->paused = 0;
  path->in_flight = 0;
  path->fault = DATA_FAULT_NONE;
  path->stopped = 0;
  path->restarts = 0;
  path->repeats = 0;
  path->repeat_waiting = 0;
  path->pending = 0;
  path->send = 0;
  path->transfer_bytes = 0;
  path->transfer_packets = 0;
  path->received = 0;
  path->taken = 0;
  path->fault_at = 0;
  path->halts = 0;
  path->halt_position = 0;
  *rx0 = 0;
  *rx1 = DATA_BLOCK_BYTES;
}

int data_path_block(struct DataPath* path, int status_ok, struct DataRx* rx) {
  const uint32_t landing = wrap(path->window + path->block * DATA_BLOCK_BYTES);
  if (path->stopped || path->fault != DATA_FAULT_NONE) return 0;
  if (!status_ok) {
    if (path->received == 0 && path->restarts < DATA_START_RESTARTS) {
      ++path->restarts;
      rx->offset0 = path->window;
      rx->offset1 = path->window + DATA_BLOCK_BYTES;
      return DATA_RX_RESTART;
    }
    set_fault(path, DATA_FAULT_RECEIVE);
    return 0;
  }
  if (wrap(landing + DATA_RING_BYTES - path->send) < path->pending) {
    set_fault(path, DATA_FAULT_OVERWRITE);
    return 0;
  }
  path->received += DATA_BLOCK_BYTES;
  path->repeats = 0;
  path->repeat_waiting = 0;
  if (path->block == 0) {
    path->block = 1;
    path->paused = (uint8_t)(path->pending > DATA_RESUME_PENDING_BYTES - DATA_WINDOW_BYTES);
    return 0;
  }
  path->block = 0;
  path->pending += DATA_WINDOW_BYTES;
  path->window = wrap(path->window + DATA_WINDOW_BYTES);
  rx->offset0 = path->window;
  rx->offset1 = path->window + DATA_BLOCK_BYTES;
  return 1;
}

void data_path_fault(struct DataPath* path, uint8_t fault) { set_fault(path, fault); }

int data_path_resume(struct DataPath* path) {
  if (path->stopped || path->fault != DATA_FAULT_NONE || !path->paused || path->block != 0) return 0;
  if (path->pending > DATA_RESUME_PENDING_BYTES) return 0;
  path->paused = 0;
  return 1;
}

int data_path_next(struct DataPath* path, struct DataTransfer* transfer) {
  uint32_t bytes = path->pending;
  if (path->stopped || path->fault != DATA_FAULT_NONE || path->in_flight || bytes == 0) return 0;
  if (bytes > DATA_TRANSFER_PACKETS * DATA_PACKET_BYTES) bytes = DATA_TRANSFER_PACKETS * DATA_PACKET_BYTES;
  if (bytes > DATA_RING_BYTES - path->send) bytes = DATA_RING_BYTES - path->send;
  transfer->offset = path->send;
  transfer->packets = (uint8_t)((bytes + DATA_PACKET_BYTES - 1u) / DATA_PACKET_BYTES);
  transfer->last_bytes = (uint16_t)(bytes - (transfer->packets - 1u) * DATA_PACKET_BYTES);
  path->transfer_bytes = bytes;
  path->transfer_packets = transfer->packets;
  path->in_flight = 1;
  return 1;
}

int data_path_after(const struct DataPath* path, struct DataTransfer* transfer) {
  const uint32_t full = DATA_TRANSFER_PACKETS * DATA_PACKET_BYTES;
  uint32_t offset;
  if (path->stopped || path->fault != DATA_FAULT_NONE || !path->in_flight) return 0;
  if (path->pending < path->transfer_bytes + full) return 0;
  offset = wrap(path->send + path->transfer_bytes);
  if (offset + full > DATA_RING_BYTES) return 0;
  transfer->offset = offset;
  transfer->packets = DATA_TRANSFER_PACKETS;
  transfer->last_bytes = DATA_PACKET_BYTES;
  return 1;
}

void data_path_taken(struct DataPath* path, uint8_t packets_left) {
  uint32_t bytes;
  if (!path->in_flight || packets_left > path->transfer_packets) {
    path->in_flight = 0;
    set_fault(path, DATA_FAULT_PACKETS);
    return;
  }
  bytes = (uint32_t)(path->transfer_packets - packets_left) * DATA_PACKET_BYTES;
  if (bytes > path->transfer_bytes) bytes = path->transfer_bytes;
  path->send = wrap(path->send + bytes);
  path->pending -= bytes;
  path->taken += bytes;
  path->in_flight = 0;
}

uint8_t data_packets_left(uint8_t offered, uint32_t tx_control_at_offer, uint32_t tx_control_now) {
  const uint32_t sent = ((tx_control_now >> SEQUENCE_SHIFT) - (tx_control_at_offer >> SEQUENCE_SHIFT)) & SEQUENCE_MASK;
  return sent >= offered ? 0u : (uint8_t)(offered - sent);
}

void data_path_stop(struct DataPath* path) {
  path->stopped = 1;
  path->paused = 0;
}

void data_path_link(struct DataPath* path, int usb3_configured) {
  if (!usb3_configured) set_fault(path, DATA_FAULT_NO_USB3);
}

void data_path_halt(struct DataPath* path) {
  ++path->halts;
  path->halt_position = path->taken;
}

int data_path_late_block(struct DataPath* path, uint32_t now_ms, struct DataRx* rx) {
  int accepted;
  if (!path->repeat_waiting || path->stopped || path->fault != DATA_FAULT_NONE) return DATA_LATE_NONE;
  if (path->block != 1) return DATA_LATE_NONE;
  if ((uint32_t)(now_ms - path->repeat_at_ms) < DATA_LATE_BLOCK_MS) return DATA_LATE_NONE;
  accepted = data_path_block(path, 1, rx);
  if (accepted == 1) blackbox_log(DATA_BLACKBOX_LATE_BLOCK, (uint32_t)path->received);
  return accepted;
}

int data_path_repeat(struct DataPath* path, uint32_t now_ms) {
  if (path->stopped || path->fault != DATA_FAULT_NONE) return 0;
  if (++path->repeats > DATA_REPEATS_IN_A_ROW) {
    set_fault(path, DATA_FAULT_RECEIVE);
    return 0;
  }
  path->repeat_waiting = 1;
  path->repeat_at_ms = now_ms;
  if (path->block == 1) path->paused = 1;
  return 1;
}
