#pragma once

#include <stddef.h>
#include <stdint.h>

int board_gpio_set_mux(void* ctx, uint8_t mode_bits);
int board_spi_xfer(void* ctx, const uint8_t* tx, uint8_t* rx, size_t len);
