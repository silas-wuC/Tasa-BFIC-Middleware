/*
 * tasa_bfic_bridge.h — adapts RENESAS-F6222-Driver onto the FPGA MUX link.
 *
 * Implements f6222_dev_t.spi_xfer by tunnelling native BFIC frames through
 * tasa_fpga_mux_xfer(), so existing f6222_* API calls work unmodified.
 */

#pragma once

#include <stdint.h>

#include "f6222.h"
#include "tasa_bfic_mode.h"
#include "tasa_fpga_link.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    tasa_fpga_dev_t* link;
    tasa_bfic_mux_mode_t mode;
} tasa_bfic_bridge_t;

tasa_status_t tasa_bfic_bridge_init(tasa_bfic_bridge_t* bridge, f6222_dev_t* dev, tasa_fpga_dev_t* link,
                                    tasa_bfic_mux_mode_t mode);

int tasa_bfic_bridge_spi_xfer(void* ctx, const uint8_t* tx, uint8_t* rx, size_t len);

#ifdef __cplusplus
}
#endif
