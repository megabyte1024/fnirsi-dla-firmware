/*
 * SPDX-License-Identifier: MIT
 */

#ifndef FNIRSI_DLA_FPGA_LINK_H
#define FNIRSI_DLA_FPGA_LINK_H

#include <stdint.h>

enum FpgaLinkResult : int8_t {
  kFpgaLinkOk = 0,
  kFpgaLinkSendTimeout = -1,
  kFpgaLinkReplyTimeout = -2,
};

struct FpgaLinkReply {
  uint32_t word;
  uint16_t length0;
  uint16_t length1;
  uint8_t status;
};

void fpga_link_init(void);
void fpga_link_pulse_pb23(void);
void fpga_link_set_pb23(uint8_t level);
void fpga_link_set_pb22(uint8_t level);
void fpga_link_set_pb24(uint8_t level);
void fpga_link_set_hspi_enable(uint8_t enable);
void fpga_link_read_pins(uint32_t* port_a, uint32_t* port_b, uint32_t* direction_a, uint32_t* direction_b);
int fpga_link_set_control_output(uint8_t pin, uint8_t level);
enum FpgaLinkResult fpga_link_write(uint16_t reg, uint16_t value);
enum FpgaLinkResult fpga_link_read(uint16_t reg, struct FpgaLinkReply* reply);

#endif
