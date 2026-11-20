/*
 * SPDX-License-Identifier: MIT
 */

#ifndef FNIRSI_DLA_SAMPLE_OFFER_H
#define FNIRSI_DLA_SAMPLE_OFFER_H

#include <stdint.h>

struct SampleOffer {
  uint8_t packets;
  uint32_t control;
};

typedef void (*SampleOfferGiveBack)(uint8_t packets_left);

void sample_offer_arm(struct SampleOffer* offer, uint8_t packets, uint32_t control_at_arm);
uint8_t sample_offer_done(struct SampleOffer* offer, uint32_t control_now);
void sample_offer_clear(struct SampleOffer* offer, SampleOfferGiveBack give_back);
int sample_offer_armed(const struct SampleOffer* offer);

#endif
