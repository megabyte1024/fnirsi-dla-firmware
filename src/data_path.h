/*
 * SPDX-License-Identifier: MIT
 */

#ifndef FNIRSI_DLA_DATA_PATH_H
#define FNIRSI_DLA_DATA_PATH_H

#include <stdint.h>

#define DATA_RING_BYTES 49152u
#define DATA_WINDOW_BYTES 8192u
#define DATA_BLOCK_BYTES 4096u
#define DATA_PACKET_BYTES 1024u
#define DATA_TRANSFER_PACKETS 4u
#define DATA_RESUME_PENDING_BYTES 32768u
#define DATA_START_RESTARTS 8u
#define DATA_REPEATS_IN_A_ROW 4u
#define DATA_LATE_BLOCK_MS 200u
#define DATA_LATE_NONE (-1)
#define DATA_RX_RESTART 2
#define DATA_BLACKBOX_LATE_BLOCK 0x18u

#define DATA_FAULT_NONE 0u
#define DATA_FAULT_RECEIVE 1u
#define DATA_FAULT_FIFO_OVERFLOW 2u
#define DATA_FAULT_LOST_BLOCK 3u
#define DATA_FAULT_OVERWRITE 4u
#define DATA_FAULT_PACKETS 5u
#define DATA_FAULT_NO_USB3 6u

struct DataPath {
  uint32_t window;
  uint8_t block;
  uint8_t paused;
  uint8_t in_flight;
  uint8_t fault;
  uint8_t stopped;
  uint8_t restarts;
  uint8_t repeats;
  uint8_t repeat_waiting;
  uint32_t repeat_at_ms;
  uint32_t pending;
  uint32_t send;
  uint32_t transfer_bytes;
  uint8_t transfer_packets;
  uint64_t received;
  uint64_t taken;
  uint64_t fault_at;
  uint32_t halts;
  uint64_t halt_position;
};

struct DataRx {
  uint32_t offset0;
  uint32_t offset1;
};

struct DataTransfer {
  uint32_t offset;
  uint8_t packets;
  uint16_t last_bytes;
};

void data_path_start(struct DataPath* path, uint32_t* rx0, uint32_t* rx1);
int data_path_block(struct DataPath* path, int status_ok, struct DataRx* rx);
void data_path_fault(struct DataPath* path, uint8_t fault);
void data_path_stop(struct DataPath* path);
void data_path_halt(struct DataPath* path);
int data_path_repeat(struct DataPath* path, uint32_t now_ms);
int data_path_late_block(struct DataPath* path, uint32_t now_ms, struct DataRx* rx);
void data_path_link(struct DataPath* path, int usb3_configured);
int data_path_resume(struct DataPath* path);
int data_path_next(struct DataPath* path, struct DataTransfer* transfer);
int data_path_after(const struct DataPath* path, struct DataTransfer* transfer);
void data_path_taken(struct DataPath* path, uint8_t packets_left);
uint8_t data_packets_left(uint8_t offered, uint32_t tx_control_at_offer, uint32_t tx_control_now);

#endif
