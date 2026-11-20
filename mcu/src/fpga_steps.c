/*
 * SPDX-License-Identifier: MIT
 */

#include "fpga_steps.h"

#define REG_CONTROL_0B 0x0Bu
#define REG_LIGHT 0x42u
#define REG_SETTINGS_PULSE 0x5Bu
#define REG_THRESHOLD_PERIOD_LOW 0x36u
#define THRESHOLD_PERIOD 1000u
#define THRESHOLD_PULSE_POSITIVE 100u
#define THRESHOLD_PULSE_NOT_POSITIVE 25u
#define STOP_PB23_PULSE_MS 100u
#define PWM_PORTS 4u
#define PWM_PORT_FIRST 0x10u
#define PWM_STOP_ALL 0x14u
#define PWM_START_BYTES 9u
#define PWM_FULL_DUTY 100u
#define PWM_16_MHZ 16000000u
#define PWM_16_MHZ_TICKS 13u
#define PWM_19_MHZ 19000000u
#define PWM_19_MHZ_TICKS 11u
#define NANOSECONDS_PER_SECOND 1000000000u
#define THRESHOLD_STEPS 8u
#define START_PB23_PULSE_MS 50u
#define REG_RATE 0x0Au
#define RATE_INDEX_FASTEST 14u
#define REG_CHANNEL_COUNT 0x19u
#define REG_CHANNEL_ORDER_FIRST 0x1Au
#define CHANNEL_SLOTS 32u
#define REG_STAGE_FIRST 0x11u
#define STAGE_REGISTERS 8u
#define STAGES 16u
#define STAGE_DONT_CARE 0x5555u
#define REG_STAGE_STROBE 0x0Eu
#define REG_STAGES_DONE 0x0Du
#define STAGE_STEPS (STAGE_REGISTERS + 2u)
#define FPGA_LIGHT_CAPTURING 3u

#define WRITE(reg, value) {FPGA_STEP_WRITE, (reg), (value)}
#define WAIT(ms) {FPGA_STEP_WAIT_MS, 0u, (ms)}
#define PB23(level) {FPGA_STEP_PB23, 0u, (level)}
#define DATA_START {FPGA_STEP_DATA_START, 0u, 0u}
#define COUNT(steps) ((uint16_t)(sizeof(steps) / sizeof((steps)[0])))

static uint16_t free_steps(const struct FpgaQueue* queue) { return (uint16_t)(FPGA_QUEUE_STEPS - queue->count); }

static void copy_step(struct FpgaStep* to, const struct FpgaStep* from) {
  to->kind = from->kind;
  to->reg = from->reg;
  to->value = from->value;
}

static void push_unchecked(struct FpgaQueue* queue, const struct FpgaStep* step) {
  copy_step(&queue->steps[(queue->head + queue->count) % FPGA_QUEUE_STEPS], step);
  ++queue->count;
}

static int push_all(struct FpgaQueue* queue, const struct FpgaStep* steps, uint16_t count) {
  uint16_t i;
  if (free_steps(queue) < count) return -1;
  for (i = 0; i < count; ++i) push_unchecked(queue, &steps[i]);
  return 0;
}

int fpga_queue_push(struct FpgaQueue* queue, uint8_t kind, uint16_t reg, uint16_t value) {
  const struct FpgaStep step = {kind, reg, value};
  return push_all(queue, &step, 1);
}

int fpga_queue_pop(struct FpgaQueue* queue, struct FpgaStep* step) {
  if (queue->count == 0) return -1;
  copy_step(step, &queue->steps[queue->head]);
  queue->head = (uint16_t)((queue->head + 1u) % FPGA_QUEUE_STEPS);
  --queue->count;
  return 0;
}

uint32_t fpga_threshold_duty(int16_t centivolts) {
  const int32_t offset = centivolts > 0 ? 9900 : 2439;
  const int32_t numerator = ((200 - (int32_t)centivolts) * 15 + offset) * 5;
  return numerator < 0 ? 0u : (uint32_t)numerator / 66u;
}

int fpga_queue_startup(struct FpgaQueue* queue) {
  static const struct FpgaStep kSteps[] = {
      WAIT(1000),          WAIT(1000),          WAIT(1000),          WRITE(REG_CONTROL_0B, 1), WRITE(0x43, 0),
      WRITE(0x2C, 0),      WRITE(0x2B, 0x800),  WRITE(0x2A, 2),      WRITE(0x0A, 0),           WRITE(0x44, 1),
      WRITE(0x19, 0),      WRITE(REG_CONTROL_0B, 0), WAIT(500),      WRITE(REG_LIGHT, FPGA_LIGHT_RED),
  };
  return push_all(queue, kSteps, COUNT(kSteps));
}

int fpga_queue_stop(struct FpgaQueue* queue) {
  static const struct FpgaStep kSteps[] = {
      PB23(1),          WAIT(STOP_PB23_PULSE_MS), PB23(0),
      WRITE(0x07, 0x101), WRITE(0x08, 1),         WRITE(0x41, 0),
      WRITE(0x07, 0x10),  WRITE(REG_CONTROL_0B, 1), WRITE(REG_LIGHT, FPGA_LIGHT_GREEN),
  };
  return push_all(queue, kSteps, COUNT(kSteps));
}

int fpga_queue_exit(struct FpgaQueue* queue) { return fpga_queue_light(queue, FPGA_LIGHT_RED); }

int fpga_queue_light(struct FpgaQueue* queue, uint16_t value) {
  return fpga_queue_push(queue, FPGA_STEP_WRITE, REG_LIGHT, value);
}

static uint16_t add_write(struct FpgaStep* steps, uint16_t count, uint16_t reg, uint16_t value) {
  steps[count].kind = FPGA_STEP_WRITE;
  steps[count].reg = reg;
  steps[count].value = value;
  return (uint16_t)(count + 1u);
}

static uint16_t add_threshold(struct FpgaStep* steps, uint16_t count, int16_t centivolts) {
  const uint32_t duty = fpga_threshold_duty(centivolts);
  count = add_write(steps, count, REG_THRESHOLD_PERIOD_LOW, THRESHOLD_PERIOD);
  count = add_write(steps, count, 0x37, 0);
  count = add_write(steps, count, 0x38, (uint16_t)(duty & 0xFFFFu));
  count = add_write(steps, count, 0x39, (uint16_t)(duty >> 16));
  count = add_write(steps, count, 0x3A, 100);
  count = add_write(steps, count, 0x3B, 0);
  count = add_write(steps, count, 0x3C,
                    (uint16_t)(centivolts > 0 ? THRESHOLD_PULSE_POSITIVE : THRESHOLD_PULSE_NOT_POSITIVE));
  return add_write(steps, count, 0x3D, 0);
}

int fpga_queue_threshold(struct FpgaQueue* queue, int16_t centivolts) {
  struct FpgaStep steps[2u + THRESHOLD_STEPS];
  uint16_t count = add_write(steps, 0, REG_SETTINGS_PULSE, 1);
  count = add_write(steps, count, REG_SETTINGS_PULSE, 0);
  count = add_threshold(steps, count, centivolts);
  return push_all(queue, steps, count);
}

uint32_t fpga_pwm_period_ticks(uint32_t frequency_hz) {
  uint32_t nanoseconds;
  if (frequency_hz == 0) return 0;
  if (frequency_hz == PWM_16_MHZ) return PWM_16_MHZ_TICKS;
  if (frequency_hz == PWM_19_MHZ) return PWM_19_MHZ_TICKS;
  nanoseconds = NANOSECONDS_PER_SECOND / frequency_hz;
  return (2u * nanoseconds + 5u) / 10u;
}

static uint32_t pwm_duty_ticks(uint32_t period_ticks, uint32_t duty_percent) {
  const uint32_t duty = duty_percent > PWM_FULL_DUTY ? PWM_FULL_DUTY : duty_percent;
  return (uint32_t)(((uint64_t)duty * period_ticks + PWM_FULL_DUTY / 2u) / PWM_FULL_DUTY);
}

static uint32_t little_endian32(const uint8_t* bytes) {
  return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static const uint16_t kPwmRegisters[PWM_PORTS][4] = {
    {0x2E, 0x2F, 0x30, 0x31},
    {0x32, 0x33, 0x34, 0x35},
    {0x5C, 0x5D, 0x5E, 0x60},
    {0x61, 0x62, 0x63, 0x64},
};

static uint16_t add_pair(struct FpgaStep* steps, uint16_t count, const uint16_t* registers, uint32_t value) {
  count = add_write(steps, count, registers[0], (uint16_t)(value & 0xFFFFu));
  return add_write(steps, count, registers[1], (uint16_t)(value >> 16));
}

int fpga_queue_pwm(struct FpgaQueue* queue, const uint8_t* payload, uint16_t length) {
  struct FpgaStep steps[3 + 2 * PWM_PORTS];
  uint16_t count = 0;
  const uint8_t port = length > 0 ? payload[0] : 0u;
  const int known_port = port >= PWM_PORT_FIRST && port < PWM_PORT_FIRST + PWM_PORTS;
  count = add_write(steps, count, REG_SETTINGS_PULSE, 1);
  count = add_write(steps, count, REG_SETTINGS_PULSE, 0);
  if (length == 1 && known_port) {
    count = add_pair(steps, count, &kPwmRegisters[port - PWM_PORT_FIRST][2], 0);
  } else if (length == 1 && port == PWM_STOP_ALL) {
    uint16_t i;
    count = add_write(steps, count, REG_SETTINGS_PULSE, 1);
    for (i = 0; i < PWM_PORTS; ++i) count = add_pair(steps, count, &kPwmRegisters[i][2], 0);
  } else if (length >= PWM_START_BYTES && known_port) {
    const uint16_t* registers = kPwmRegisters[port - PWM_PORT_FIRST];
    const uint32_t period = fpga_pwm_period_ticks(little_endian32(payload + 1));
    count = add_pair(steps, count, &registers[0], period);
    count = add_pair(steps, count, &registers[2], pwm_duty_ticks(period, little_endian32(payload + 5)));
  }
  return push_all(queue, steps, count);
}

static const struct FpgaStep kStartBegin[] = {
    PB23(1),
    WAIT(START_PB23_PULSE_MS),
    PB23(0),
    WRITE(0x07, 0x101),
    WRITE(0x08, 1),
    WRITE(0x41, 0),
    WRITE(REG_SETTINGS_PULSE, 1),
    WRITE(REG_SETTINGS_PULSE, 0),
    WRITE(0x07, 0),
    WRITE(0x08, 0),
    WRITE(0x41, 1),
};

static const struct FpgaStep kStartAfterThreshold[] = {
    WRITE(REG_CONTROL_0B, 1), WRITE(0x06, 0), WRITE(0x43, 0), WRITE(0x2C, 0), WRITE(0x2B, 0), WRITE(0x2A, 0),
};

static const struct FpgaStep kStartAfterRate[] = {
    WRITE(0x58, 3),
    WRITE(0x59, 0),
    WRITE(0x44, 1),
};

static const struct FpgaStep kStartAfterChannels[] = {
    WRITE(0x51, 0),    WRITE(0x50, 0), WRITE(0x55, 0), WRITE(0x56, 0xFFFE), WRITE(0x57, 0xFFFF), WRITE(0x0D, 1),
    WRITE(0x0E, 0),    WRITE(0x45, 1), WRITE(0x0C, 1), WRITE(0x0F, 0),      WRITE(0x09, 0),      WRITE(0x10, 1),
};

static const struct FpgaStep kStartEnd[] = {
    WRITE(REG_LIGHT, FPGA_LIGHT_CAPTURING),
    DATA_START,
    WRITE(0x97, 1),
    WRITE(REG_CONTROL_0B, 0),
};

#define CAPTURE_START_STEPS                                                                                        \
  (COUNT(kStartBegin) + THRESHOLD_STEPS + COUNT(kStartAfterThreshold) + 1u + COUNT(kStartAfterRate) + 1u +        \
   CHANNEL_SLOTS / 2u + COUNT(kStartAfterChannels) + STAGES * STAGE_STEPS + COUNT(kStartEnd))

static void put(struct FpgaQueue* queue, uint8_t kind, uint16_t reg, uint16_t value) {
  const struct FpgaStep step = {kind, reg, value};
  push_unchecked(queue, &step);
}

static void put_all(struct FpgaQueue* queue, const struct FpgaStep* steps, uint16_t count) {
  uint16_t i;
  for (i = 0; i < count; ++i) push_unchecked(queue, &steps[i]);
}

static uint16_t channel_count_code(uint16_t mask) {
  uint16_t count = 0;
  uint16_t bits;
  for (bits = mask; bits != 0; bits = (uint16_t)(bits & (bits - 1u))) ++count;
  if (count == 1) return 0x50;
  if (count <= 2) return 0x00;
  if (count <= 4) return 0x10;
  if (count <= 8) return 0x20;
  return 0x30;
}

static int channel_selected(uint16_t mask, uint8_t channel) {
  return ((uint32_t)mask >> channel & 1u) != 0;
}

static void put_channel_order(struct FpgaQueue* queue, uint16_t mask) {
  uint8_t order[CHANNEL_SLOTS];
  uint8_t filled = 0;
  uint8_t channel;
  uint16_t i;
  for (channel = 0; channel < CHANNEL_SLOTS; ++channel) {
    if (channel_selected(mask, channel)) order[filled++] = channel;
  }
  for (channel = 0; channel < CHANNEL_SLOTS; ++channel) {
    if (!channel_selected(mask, channel)) order[filled++] = channel;
  }
  for (i = 0; i < CHANNEL_SLOTS / 2u; ++i) {
    put(queue, FPGA_STEP_WRITE, (uint16_t)(REG_CHANNEL_ORDER_FIRST + i),
        (uint16_t)(order[2u * i] | (order[2u * i + 1u] << 8)));
  }
}

static void put_stages(struct FpgaQueue* queue) {
  uint16_t stage;
  uint16_t reg;
  for (stage = 0; stage < STAGES; ++stage) {
    for (reg = 0; reg < STAGE_REGISTERS; ++reg) {
      put(queue, FPGA_STEP_WRITE, (uint16_t)(REG_STAGE_FIRST + reg), STAGE_DONT_CARE);
    }
    put(queue, FPGA_STEP_WRITE, REG_STAGE_STROBE, 1);
    put(queue, FPGA_STEP_WRITE, stage + 1u < STAGES ? REG_STAGE_STROBE : REG_STAGES_DONE, 0);
  }
}

int fpga_queue_capture_start(struct FpgaQueue* queue, uint8_t rate_index, int16_t centivolts,
                             uint16_t channel_mask) {
  struct FpgaStep threshold[THRESHOLD_STEPS];
  if (free_steps(queue) < CAPTURE_START_STEPS) return -1;
  add_threshold(threshold, 0, centivolts);
  put_all(queue, kStartBegin, COUNT(kStartBegin));
  put_all(queue, threshold, THRESHOLD_STEPS);
  put_all(queue, kStartAfterThreshold, COUNT(kStartAfterThreshold));
  put(queue, FPGA_STEP_WRITE, REG_RATE,
      rate_index <= RATE_INDEX_FASTEST ? (uint16_t)(RATE_INDEX_FASTEST - rate_index) : 0u);
  put_all(queue, kStartAfterRate, COUNT(kStartAfterRate));
  put(queue, FPGA_STEP_WRITE, REG_CHANNEL_COUNT, channel_count_code(channel_mask));
  put_channel_order(queue, channel_mask);
  put_all(queue, kStartAfterChannels, COUNT(kStartAfterChannels));
  put_stages(queue);
  put_all(queue, kStartEnd, COUNT(kStartEnd));
  return 0;
}

void fpga_runner_poll(struct FpgaRunner* runner, struct FpgaQueue* queue, const struct FpgaPort* port) {
  struct FpgaStep step;
  if (runner->waiting) {
    if ((uint32_t)(port->now_ms() - runner->wait_start_ms) < runner->wait_ms) return;
    runner->waiting = 0;
  }
  while (fpga_queue_pop(queue, &step) == 0) {
    switch (step.kind) {
      case FPGA_STEP_WRITE:
        port->write(step.reg, step.value);
        break;
      case FPGA_STEP_PB23:
        port->set_pb23((uint8_t)step.value);
        break;
      case FPGA_STEP_DATA_START:
        port->start_data();
        break;
      default:
        runner->wait_start_ms = port->now_ms();
        runner->wait_ms = step.value;
        runner->waiting = 1;
        return;
    }
  }
}
