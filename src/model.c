/*
 * SPDX-License-Identifier: MIT
 */

#include "model.h"

#if !defined(MODEL_REG8)
#define MODEL_REG8(address) (*(volatile uint8_t*)(address))
#endif

#define R8_GLOB_RESET_KEEP MODEL_REG8(0x40001007u)

enum {
  kFactoryByteUnwritten = 0x00,
  kFactoryByteErased = 0xFF,
};

uint8_t model_from_factory_word(uint32_t word) {
  const uint8_t byte18 = (uint8_t)word;
  const uint8_t byte19 = (uint8_t)(word >> 8);
  return byte19 == kFactoryByteUnwritten || byte19 == kFactoryByteErased ? byte18 : byte19;
}

void model_keep_over_reset(uint8_t model) { R8_GLOB_RESET_KEEP = model; }

uint8_t model_kept_over_reset(void) { return R8_GLOB_RESET_KEEP; }
