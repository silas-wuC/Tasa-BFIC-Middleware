/*
 * tasa_fpga_link.h — MCU-to-FPGA MUX passthrough transport.
 *
 * Phase 1 exposes only MUX passthrough (Ctrl_FPGA=0): prepend a 1-byte
 * command (mode in bits[6:1], dummy bit0) to a native BFIC SPI frame and
 * forward it through the MCU SPI peripheral to the FPGA.
 *
 * Platform-agnostic: caller supplies spi_xfer() (CSB + clock).
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "tasa_bfic_mode.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TASA_FPGA_MUX_MAX_DATA 32u

typedef enum {
    TASA_OK = 0,
    TASA_ERR_INVALID_ARG = -1,
    TASA_ERR_SPI = -2,
} tasa_status_t;

typedef struct {
    int (*spi_xfer)(void* ctx, const uint8_t* tx, uint8_t* rx, size_t len);
    void* ctx;
} tasa_fpga_dev_t;

tasa_status_t tasa_fpga_mux_xfer(tasa_fpga_dev_t* dev, tasa_bfic_mux_mode_t mode, const uint8_t* tx, uint8_t* rx,
                                 size_t len);

#ifdef __cplusplus
}
#endif
