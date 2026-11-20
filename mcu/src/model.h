/*
 * SPDX-License-Identifier: MIT
 */

#ifndef FNIRSI_DLA_MODEL_H
#define FNIRSI_DLA_MODEL_H

#include <stdint.h>

#define MODEL_FACTORY_WORD_ADDRESS 0x6F018u

uint8_t model_from_factory_word(uint32_t word);
void model_keep_over_reset(uint8_t model);
uint8_t model_kept_over_reset(void);

#endif
