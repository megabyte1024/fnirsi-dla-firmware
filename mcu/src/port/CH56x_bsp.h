/*
 * SPDX-License-Identifier: MIT
 */

#ifndef FNIRSI_DLA_PORT_CH56X_BSP_H
#define FNIRSI_DLA_PORT_CH56X_BSP_H

#include <stdint.h>

#include "CH56xSFR.h"

#define bsp_disable_interrupt() ((void)0)
#define bsp_enable_interrupt() ((void)0)

void bsp_wait_us_delay(uint32_t us);
void bsp_wait_ms_delay(uint32_t ms);

#endif
