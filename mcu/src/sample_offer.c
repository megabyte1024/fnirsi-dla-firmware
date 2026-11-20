/*
 * SPDX-License-Identifier: MIT
 */

#include "sample_offer.h"

#include "data_path.h"

void sample_offer_arm(struct SampleOffer* offer, uint8_t packets, uint32_t control_at_arm) {
  offer->packets = packets;
  offer->control = control_at_arm;
}

uint8_t sample_offer_done(struct SampleOffer* offer, uint32_t control_now) {
  const uint8_t left = data_packets_left(offer->packets, offer->control, control_now);
  offer->packets = 0;
  return left;
}

void sample_offer_clear(struct SampleOffer* offer, SampleOfferGiveBack give_back) {
  const uint8_t left = offer->packets;
  offer->packets = 0;
  if (left != 0 && give_back != 0) give_back(left);
}

int sample_offer_armed(const struct SampleOffer* offer) { return offer->packets != 0; }
