/*
 * SPDX-License-Identifier: MIT
 */

#include "data_link.h"

#include <stdalign.h>
#include <stdint.h>

#include "ch569_regs.h"
#include "app_protocol.h"
#include "data_path.h"
#include "delay.h"
#include "fpga_link.h"
#include "interrupts.h"
#include "usb3.h"
#include "blackbox.h"

#define FLAG_TRANSMIT_DONE 0x01u
#define FLAG_RECEIVE_DONE 0x02u
#define FLAG_FIFO_OVERFLOW 0x04u
#define STATUS_ERRORS 0x06u
#define STATUS_SEQUENCE_MISMATCH 0x04u
#define RX_SEQUENCE_MASK 0x0Fu
#define RX_TOGGLE 0x10u

static alignas(16) uint8_t ring[DATA_RING_BYTES] __attribute__((section(".dmadata")));

static struct DataPath path;
static uint8_t running;
static uint8_t last_sequence;
static uint32_t started_ms;
static uint32_t blocks;
static uint32_t status_errors;
static uint32_t fifo_overflows;
static uint32_t sequence_mismatches;
static uint32_t pauses;
static uint32_t resumes;
static uint8_t first_bad_status;
static uint16_t last_length0;
static uint16_t last_length1;
static uint8_t pb22;
static uint32_t transfers;
static uint32_t partial_takes;
static uint32_t repeats_dropped;
static uint8_t streamed_window;
static uint8_t fast_ready;
static uint32_t fast_address;
static uint8_t fault_logged;

uint8_t* data_link_ring(void) { return ring; }

static void set_pb22(uint8_t level) {
  if (level == pb22) return;
  pb22 = level;
  if (level) ++pauses;
  fpga_link_set_pb22(level);
}

static void reset_receive_sequence(void) {
  if ((R8_HSPI_RX_SC & RX_TOGGLE) != 0) R8_HSPI_RX_SC = RX_TOGGLE;
  R8_HSPI_RX_SC = 0;
}

static void plan_fast(void) {
  struct DataTransfer after;
  fast_ready = (uint8_t)data_path_after(&path, &after);
  if (fast_ready) fast_address = (uint32_t)(uintptr_t)ring + after.offset;
}

static void offer_next(void) {
  struct DataTransfer transfer;
  if (data_path_next(&path, &transfer)) {
    ++transfers;
    usb3_sample_send(ring + transfer.offset, transfer.packets, transfer.last_bytes);
    plan_fast();
  }
}

int data_link_fast(uint32_t* address) {
  if (!fast_ready || path.stopped || path.fault != DATA_FAULT_NONE) return 0;
  *address = fast_address;
  return 1;
}

void data_link_fast_taken(void) {
  struct DataTransfer transfer;
  fast_ready = 0;
  data_path_taken(&path, 0);
  if (data_path_next(&path, &transfer)) {
    ++transfers;
    plan_fast();
  }
  if (path.fault != DATA_FAULT_NONE || !running) return;
  data_path_link(&path, usb3_configured());
  if (data_path_resume(&path)) ++resumes;
  set_pb22(path.paused);
}

static void advance(void) {
  if (path.fault != DATA_FAULT_NONE && !fault_logged) {
    fault_logged = 1;
    blackbox_log(0x50, path.fault);
    blackbox_log(0x51, (uint32_t)path.fault_at);
    blackbox_log(0x52, first_bad_status);
  }
  data_path_link(&path, usb3_configured());
  if (data_path_resume(&path)) ++resumes;
  offer_next();
  set_pb22(path.paused);
}

static void sample_done(uint8_t packets_left) {
  if (packets_left != 0) ++partial_takes;
  data_path_taken(&path, packets_left);
  offer_next();
  advance();
}

static uint32_t address_of(uint32_t offset) { return (uint32_t)(uintptr_t)ring + offset; }

void data_link_start(void) {
  streamed_window = 0;
  fast_ready = 0;
  uint32_t rx0;
  uint32_t rx1;
  data_path_start(&path, &rx0, &rx1);
  R32_HSPI_RX_ADDR0 = address_of(rx0);
  R32_HSPI_RX_ADDR1 = address_of(rx1);
  reset_receive_sequence();
  R8_HSPI_INT_FLAG = FLAG_RECEIVE_DONE | FLAG_FIFO_OVERFLOW;
  last_sequence = (uint8_t)(R8_HSPI_RX_SC & RX_SEQUENCE_MASK);
  started_ms = clock_now_ms();
  blocks = 0;
  status_errors = 0;
  fifo_overflows = 0;
  sequence_mismatches = 0;
  pauses = 0;
  resumes = 0;
  first_bad_status = 0;
  transfers = 0;
  partial_takes = 0;
  repeats_dropped = 0;
  fault_logged = 0;
  usb3_sample_reset(sample_done);
  pb22 = 1;
  set_pb22(0);
  running = 1;
  interrupts_enable_hspi();
}

static void receive(void) {
  struct DataRx rx;
  const uint8_t status = R8_HSPI_RTX_STATUS;
  const uint8_t sequence = (uint8_t)(R8_HSPI_RX_SC & RX_SEQUENCE_MASK);
  R8_HSPI_INT_FLAG = FLAG_RECEIVE_DONE;
  ++blocks;
  last_length0 = R16_HSPI_RX_LEN0;
  last_length1 = R16_HSPI_RX_LEN1;
  if ((status & STATUS_ERRORS) == STATUS_SEQUENCE_MISMATCH && sequence == last_sequence &&
      data_path_repeat(&path, clock_now_ms())) {
    const uint8_t toggle = (uint8_t)(R8_HSPI_RX_SC & RX_TOGGLE);
    const uint8_t wanted = path.block != 0 ? RX_TOGGLE : 0u;
    set_pb22(path.paused);
    R8_HSPI_RX_SC = (uint8_t)((toggle != wanted ? RX_TOGGLE : 0u) | last_sequence);
    ++repeats_dropped;
    return;
  }
  if (sequence != (uint8_t)((last_sequence + 1u) & RX_SEQUENCE_MASK)) ++sequence_mismatches;
  last_sequence = sequence;
  if ((status & STATUS_ERRORS) != 0) {
    ++status_errors;
    if (first_bad_status == 0) first_bad_status = status;
  }
  if (path.block == 1) streamed_window = !pb22;
  {
    const int result = data_path_block(&path, (status & STATUS_ERRORS) == 0, &rx);
    if (result == 0) {
      if (path.block == 1 && !path.paused) {
        R32_HSPI_RX_ADDR0 = address_of((path.window + DATA_WINDOW_BYTES) % DATA_RING_BYTES);
      }
      return;
    }
    if (result != 1) streamed_window = 0;
  }
  if (streamed_window) {
    R32_HSPI_RX_ADDR1 = address_of(rx.offset1);
    reset_receive_sequence();
    last_sequence = (uint8_t)(R8_HSPI_RX_SC & RX_SEQUENCE_MASK);
    return;
  }
  R32_HSPI_RX_ADDR0 = address_of(rx.offset0);
  R32_HSPI_RX_ADDR1 = address_of(rx.offset1);
  reset_receive_sequence();
  last_sequence = (uint8_t)(R8_HSPI_RX_SC & RX_SEQUENCE_MASK);
  if (data_path_resume(&path)) {
    ++resumes;
    set_pb22(path.paused);
  }
}

void data_link_interrupt(void) {
  if ((R8_HSPI_INT_FLAG & FLAG_TRANSMIT_DONE) != 0) R8_HSPI_INT_FLAG = FLAG_TRANSMIT_DONE;
  if (!running) {
    R8_HSPI_INT_FLAG = FLAG_RECEIVE_DONE | FLAG_FIFO_OVERFLOW;
    return;
  }
  if ((R8_HSPI_INT_FLAG & FLAG_FIFO_OVERFLOW) != 0) {
    R8_HSPI_INT_FLAG = FLAG_FIFO_OVERFLOW;
    ++fifo_overflows;
    data_path_fault(&path, DATA_FAULT_FIFO_OVERFLOW);
  }
  if ((R8_HSPI_INT_FLAG & FLAG_RECEIVE_DONE) != 0) receive();
  advance();
}

static void put32(uint8_t* bytes, uint32_t value) {
  bytes[0] = (uint8_t)value;
  bytes[1] = (uint8_t)(value >> 8);
  bytes[2] = (uint8_t)(value >> 16);
  bytes[3] = (uint8_t)(value >> 24);
}

void data_link_status(uint8_t* bytes) {
  put32(bytes, (uint32_t)path.received);
  put32(bytes + 4, (uint32_t)(path.received >> 32));
  put32(bytes + 8, (uint32_t)path.taken);
  put32(bytes + 12, blocks);
  put32(bytes + 16, status_errors);
  put32(bytes + 20, repeats_dropped);
  put32(bytes + 24, partial_takes);
  put32(bytes + 28, pauses);
  put32(bytes + 32, resumes);
  put32(bytes + 36, started_ms);
  put32(bytes + 40, transfers);
  put32(bytes + 44, (uint32_t)path.fault_at);
  bytes[48] = path.fault;
  bytes[49] = first_bad_status;
  bytes[50] = (uint8_t)(running | path.stopped << 1 | path.restarts << 4);
  bytes[51] = pb22;
  bytes[52] = (uint8_t)last_length0;
  bytes[53] = (uint8_t)(last_length0 >> 8);
  bytes[54] = (uint8_t)last_length1;
  bytes[55] = (uint8_t)(last_length1 >> 8);
  bytes[56] = last_sequence;
  bytes[57] = R8_HSPI_RX_SC;
  bytes[58] = R8_HSPI_INT_FLAG;
  bytes[59] = R8_HSPI_RTX_STATUS;
}

void data_link_fault_no_usb3(void) {
  uint32_t rx0;
  uint32_t rx1;
  data_path_start(&path, &rx0, &rx1);
  data_path_link(&path, 0);
  blackbox_log(0x50, path.fault);
}

void data_link_stop(void) {
  if (!running) return;
  data_path_stop(&path);
  set_pb22(path.paused);
}

void data_link_halt(void) {
  if (running) data_path_halt(&path);
}

void data_link_stream_state(struct AppStreamState* state) {
  state->halts = path.halts;
  state->halt_position = path.halt_position;
  state->fault = path.fault;
  state->fault_at = path.fault_at;
}

uint32_t data_link_transfers(void) { return transfers; }

void data_link_poll(uint32_t now_ms) {
  struct DataRx rx;
  int late;
  if (!running) return;
  late = data_path_late_block(&path, now_ms, &rx);
  if (late == DATA_LATE_NONE) return;
  if (late == 1) {
    R32_HSPI_RX_ADDR0 = address_of(rx.offset0);
    R32_HSPI_RX_ADDR1 = address_of(rx.offset1);
    reset_receive_sequence();
    last_sequence = (uint8_t)(R8_HSPI_RX_SC & RX_SEQUENCE_MASK);
  }
  advance();
}

int data_link_streaming(void) { return running && !path.stopped && path.fault == DATA_FAULT_NONE; }
