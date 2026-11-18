/*
 * SPDX-License-Identifier: MIT
 */

#include "interrupts.h"

#include <stdint.h>

enum : uint32_t { kVectoredMode = 1u, kMachineInterruptEnable = 8u, kHspiInterrupt = 28u };

#define PFIC_IENR0 (*(volatile uint32_t*)0xE000E100u)

extern const uint32_t interrupt_vectors[];

void interrupts_init(void) {
  const uint32_t vectors = (uint32_t)(uintptr_t)interrupt_vectors | kVectoredMode;
  __asm__ volatile("csrw mtvec, %0" : : "r"(vectors));
}

void interrupts_on(void) { __asm__ volatile("csrs mstatus, %0" : : "r"(kMachineInterruptEnable) : "memory"); }

void interrupts_off(void) { __asm__ volatile("csrc mstatus, %0" : : "r"(kMachineInterruptEnable) : "memory"); }

void interrupts_enable_hspi(void) { PFIC_IENR0 = 1u << kHspiInterrupt; }
