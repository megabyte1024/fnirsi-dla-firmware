/*
 * SPDX-License-Identifier: MIT
 */

#ifndef FNIRSI_DLA_SPI_FLASH_H
#define FNIRSI_DLA_SPI_FLASH_H

#include <stdint.h>

void spi_flash_prepare(void);
uint32_t spi_flash_id(void);
int spi_flash_read(uint32_t address, uint8_t* bytes, uint32_t count);
int spi_flash_erase_sector(uint32_t address);
int spi_flash_program(uint32_t address, const uint8_t* bytes, uint32_t count);

#endif
