/*
 * SPDX-License-Identifier: MIT
 */

#ifndef FNIRSI_DLA_FPGA_STEPS_H
#define FNIRSI_DLA_FPGA_STEPS_H

#include <stdint.h>

#define FPGA_QUEUE_STEPS 512u
#define FPGA_STEP_WRITE 0u
#define FPGA_STEP_PB23 1u
#define FPGA_STEP_WAIT_MS 2u
#define FPGA_STEP_DATA_START 3u
#define FPGA_LIGHT_RED 6u
#define FPGA_LIGHT_GREEN 5u

struct FpgaStep {
  uint8_t kind;
  uint16_t reg;
  uint16_t value;
};

struct FpgaQueue {
  struct FpgaStep steps[FPGA_QUEUE_STEPS];
  uint16_t head;
  uint16_t count;
};

struct FpgaPort {
  int (*write)(uint16_t reg, uint16_t value);
  void (*set_pb23)(uint8_t level);
  uint32_t (*now_ms)(void);
  void (*start_data)(void);
};

struct FpgaRunner {
  uint32_t wait_start_ms;
  uint32_t wait_ms;
  uint8_t waiting;
};

int fpga_queue_push(struct FpgaQueue* queue, uint8_t kind, uint16_t reg, uint16_t value);
int fpga_queue_pop(struct FpgaQueue* queue, struct FpgaStep* step);
uint32_t fpga_threshold_duty(int16_t centivolts);

int fpga_queue_startup(struct FpgaQueue* queue);
int fpga_queue_stop(struct FpgaQueue* queue);
int fpga_queue_exit(struct FpgaQueue* queue);
int fpga_queue_light(struct FpgaQueue* queue, uint16_t value);
int fpga_queue_threshold(struct FpgaQueue* queue, int16_t centivolts);
int fpga_queue_pwm(struct FpgaQueue* queue, const uint8_t* payload, uint16_t length);
uint32_t fpga_pwm_period_ticks(uint32_t frequency_hz);
int fpga_queue_capture_start(struct FpgaQueue* queue, uint8_t rate_index, int16_t centivolts,
                             uint16_t channel_mask);

void fpga_runner_poll(struct FpgaRunner* runner, struct FpgaQueue* queue, const struct FpgaPort* port);

#endif
