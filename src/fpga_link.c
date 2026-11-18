/*
 * SPDX-License-Identifier: MIT
 */

#include "fpga_link.h"

#include "ch569_regs.h"
#include "delay.h"

enum : uint32_t {
  kPortAOutputs = (1u << 9) | (1u << 10) | (1u << 11) | (1u << 21),
  kPortAStrongDrive = 1u << 11,
  kPortAPulledDown = (1u << 6) | (1u << 18) | (1u << 19) | (1u << 23),
  kPortAFloatingInputs = 0x1Fu | (1u << 6) | (1u << 16) | (1u << 17) | (1u << 20) | (1u << 22),
  kPortBDataInputs = 0x003FFFFFu,
  kPinPb22 = 1u << 22,
  kPinPb23 = 1u << 23,
  kPinPb24 = 1u << 24,
  kPortBControlOutputs = kPinPb22 | kPinPb23 | kPinPb24,
  kRegisterShift = 16,
  kUserFieldMask = 0x03,
  kUserFieldRead = 0x01,
  kPb23PulseMs = 100,
  kSendPolls = 1000,
  kSendPollUs = 10,
  kReplyPolls = 100,
  kReplyPollMs = 1,
  kRxBufferBytes = 4096,
};

enum : uint8_t {
  kCfgKeep = 0xF2,
  kCfgUpMode = 0x01,
  kCfg32Bit = 0x08,
  kCfgTxToggle = 0x20,
  kCfgRxToggle = 0x40,
  kCfgHardwareAck = 0x80,
  kAuxTxClockInvert = 0x01,
  kAuxAckAfterDelay = 0x04,
  kAuxDualDmaRequest = 0x20,
  kAuxAckDelayMask = 0x18,
  kCtrlEnable = 0x01,
  kCtrlDma = 0x02,
  kCtrlSoftwareSend = 0x04,
  kCtrlClearMask = 0x18,
  kSequenceToggle = 0x10,
  kFlagTransmitDone = 0x01,
  kFlagReceiveDone = 0x02,
  kFlagFifoOverflow = 0x04,
  kFlagAll = 0x0F,
  kStatusCrcError = 0x02,
  kStatusSequenceMismatch = 0x04,
};

static volatile uint32_t tx_word __attribute__((section(".dmadata"), aligned(16)));
static volatile uint32_t rx_buffer[kRxBufferBytes / 4] __attribute__((section(".dmadata"), aligned(16)));

static void clear_sequence(volatile uint8_t* sequence) {
  if ((*sequence & kSequenceToggle) != 0) *sequence = kSequenceToggle;
  *sequence = 0;
}

static void init_pins(void) {
  R32_PB_SMT &= ~kPortBControlOutputs;
  R32_PB_DRV |= kPortBControlOutputs;
  R32_PB_PD &= ~kPortBControlOutputs;
  R32_PB_DIR |= kPortBControlOutputs;
  R32_PB_OUT |= kPinPb24;
  R32_PB_CLR = kPinPb22 | kPinPb23;

  R32_PA_DIR |= kPortAOutputs;
  R32_PA_DRV |= kPortAStrongDrive;
  R32_PA_PU &= ~kPortAOutputs;
  R32_PA_PD &= ~kPortAOutputs;
  R32_PA_DIR &= ~kPortAPulledDown;
  R32_PA_PU &= ~kPortAPulledDown;
  R32_PA_PD |= kPortAPulledDown;
  R32_PA_DIR &= ~kPortAFloatingInputs;
  R32_PA_PU &= ~kPortAFloatingInputs;
  R32_PA_PD &= ~kPortAFloatingInputs;
  R32_PB_DIR &= ~kPortBDataInputs;
  R32_PB_PU &= ~kPortBDataInputs;
  R32_PB_PD &= ~kPortBDataInputs;
}

void fpga_link_init(void) {
  init_pins();
  R8_HSPI_CFG = (uint8_t)((R8_HSPI_CFG & kCfgKeep) | kCfgUpMode | kCfg32Bit | kCfgHardwareAck | kCfgTxToggle |
                          kCfgRxToggle);
  R8_HSPI_AUX = (uint8_t)((R8_HSPI_AUX | kAuxDualDmaRequest | kAuxTxClockInvert | kAuxAckAfterDelay) &
                          ~kAuxAckDelayMask);
  R8_HSPI_CTRL &= (uint8_t)~kCtrlClearMask;
  R32_HSPI_TX_ADDR0 = (uint32_t)(uintptr_t)&tx_word;
  R32_HSPI_TX_ADDR1 = (uint32_t)(uintptr_t)&tx_word;
  R32_HSPI_RX_ADDR0 = (uint32_t)(uintptr_t)rx_buffer;
  R32_HSPI_RX_ADDR1 = (uint32_t)(uintptr_t)rx_buffer;
  R16_HSPI_DMA_LEN0 = kRxBufferBytes - 1;
  R16_HSPI_DMA_LEN1 = kRxBufferBytes - 1;
  clear_sequence(&R8_HSPI_RX_SC);
  clear_sequence(&R8_HSPI_TX_SC);
  R8_HSPI_CTRL |= kCtrlEnable | kCtrlDma;
  R32_HSPI_UDF0 = 0;
  R32_HSPI_UDF1 = 0;
  R8_HSPI_INT_EN |= kFlagTransmitDone | kFlagReceiveDone | kFlagFifoOverflow;
  R8_HSPI_INT_FLAG = kFlagAll;
}

void fpga_link_set_pb23(uint8_t level) {
  if (level != 0) {
    R32_PB_OUT |= kPinPb23;
  } else {
    R32_PB_CLR = kPinPb23;
  }
}

void fpga_link_set_pb22(uint8_t level) {
  if (level != 0) {
    R32_PB_OUT |= kPinPb22;
  } else {
    R32_PB_CLR = kPinPb22;
  }
}

void fpga_link_pulse_pb23(void) {
  fpga_link_set_pb23(0);
  fpga_link_set_pb23(1);
  delay_ms(kPb23PulseMs);
  fpga_link_set_pb23(0);
}

static void set_user_field(uint32_t field) {
  R32_HSPI_UDF0 = (R32_HSPI_UDF0 & ~kUserFieldMask) | field;
  R32_HSPI_UDF1 = (R32_HSPI_UDF1 & ~kUserFieldMask) | field;
}

static enum FpgaLinkResult send_word(uint32_t word) {
  tx_word = word;
  R16_HSPI_DMA_LEN0 = sizeof word - 1;
  R16_HSPI_DMA_LEN1 = sizeof word - 1;
  R8_HSPI_INT_FLAG = kFlagTransmitDone;
  R8_HSPI_CTRL |= kCtrlSoftwareSend;
  for (uint32_t poll = 0; poll < kSendPolls; ++poll) {
    if ((R8_HSPI_INT_FLAG & kFlagTransmitDone) != 0) {
      R8_HSPI_INT_FLAG = kFlagTransmitDone;
      return kFpgaLinkOk;
    }
    delay_us(kSendPollUs);
  }
  return kFpgaLinkSendTimeout;
}

enum FpgaLinkResult fpga_link_write(uint16_t reg, uint16_t value) {
  set_user_field(0);
  return send_word(((uint32_t)reg << kRegisterShift) | value);
}

enum FpgaLinkResult fpga_link_read(uint16_t reg, struct FpgaLinkReply* reply) {
  set_user_field(kUserFieldRead);
  R32_HSPI_RX_ADDR0 = (uint32_t)(uintptr_t)rx_buffer;
  R32_HSPI_RX_ADDR1 = (uint32_t)(uintptr_t)rx_buffer;
  rx_buffer[0] = 0;
  const enum FpgaLinkResult sent = send_word((uint32_t)reg << kRegisterShift);
  if (sent != kFpgaLinkOk) return sent;
  for (uint32_t poll = 0; poll < kReplyPolls; ++poll) {
    if ((R8_HSPI_INT_FLAG & kFlagReceiveDone) != 0) {
      R8_HSPI_INT_FLAG = kFlagReceiveDone;
      reply->status = R8_HSPI_RTX_STATUS;
      reply->length0 = R16_HSPI_RX_LEN0;
      reply->length1 = R16_HSPI_RX_LEN1;
      reply->word = rx_buffer[0];
      if ((reply->status & (kStatusCrcError | kStatusSequenceMismatch)) != 0) clear_sequence(&R8_HSPI_RX_SC);
      return kFpgaLinkOk;
    }
    delay_ms(kReplyPollMs);
  }
  return kFpgaLinkReplyTimeout;
}
