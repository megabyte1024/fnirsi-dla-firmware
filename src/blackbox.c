/*
 * SPDX-License-Identifier: MIT
 */

#include "blackbox.h"

#define BLACKBOX_MAGIC 0xB1ACB0C5u
#define TRAP_CAUSE 0xEEu

struct Blackbox {
  uint32_t magic;
  uint32_t count;
  uint32_t codes[BLACKBOX_ENTRIES];
  uint32_t values[BLACKBOX_ENTRIES];
};

static struct Blackbox storage __attribute__((section(".dmadata")));

static struct Blackbox* box(void) { return &storage; }

void blackbox_clear(void) {
  box()->magic = BLACKBOX_MAGIC;
  box()->count = 0;
}

void blackbox_log(uint32_t code, uint32_t value) {
  struct Blackbox* b = box();
  if (b->magic != BLACKBOX_MAGIC) blackbox_clear();
  b->codes[b->count % BLACKBOX_ENTRIES] = code;
  b->values[b->count % BLACKBOX_ENTRIES] = value;
  ++b->count;
}

void blackbox_repeat(uint32_t code) {
  struct Blackbox* b = box();
  if (b->magic == BLACKBOX_MAGIC && b->count > 0 && b->codes[(b->count - 1u) % BLACKBOX_ENTRIES] == code) {
    ++b->values[(b->count - 1u) % BLACKBOX_ENTRIES];
    return;
  }
  blackbox_log(code, 1);
}

uint32_t blackbox_count(void) { return box()->magic == BLACKBOX_MAGIC ? box()->count : 0; }

void blackbox_entry(uint32_t index, uint32_t* code, uint32_t* value) {
  *code = box()->codes[index % BLACKBOX_ENTRIES];
  *value = box()->values[index % BLACKBOX_ENTRIES];
}

__attribute__((aligned(4))) static void trap(void) {
  uint32_t cause;
  uint32_t pc;
  uint32_t bad;
  __asm__ volatile("csrr %0, mcause" : "=r"(cause));
  __asm__ volatile("csrr %0, mepc" : "=r"(pc));
  __asm__ volatile("csrr %0, mtval" : "=r"(bad));
  blackbox_log(TRAP_CAUSE, cause);
  blackbox_log(TRAP_CAUSE + 1u, pc);
  blackbox_log(TRAP_CAUSE + 2u, bad);
  for (;;) {
  }
}

void blackbox_catch_traps(void) { __asm__ volatile("csrw mtvec, %0" : : "r"(trap)); }
