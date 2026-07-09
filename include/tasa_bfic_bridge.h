/*
 * tasa_bfic_bridge.h — adapts RENESAS-F6222-Driver onto the FPGA link.
 *
 * The F6222 driver (third_party/RENESAS-F6222-Driver) already assembles
 * every native BFIC SPI frame; it only needs one callback wired up
 * (f6222_dev_t.spi_xfer). This bridge implements that callback by
 * tunnelling the frame through tasa_fpga_mux_xfer(), so all existing
 * f6222_* API calls (f6222_local_reg_write, f6222_set_phase, f6222_init,
 * ...) work unmodified — the MCU still "talks BFIC", it just never
 * touches a physical BFIC SPI bus itself.
 */

#pragma once

#include <stdint.h>

#include "f6222.h"
#include "tasa_fpga_link.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * tasa_bfic_bridge_t — binds one FPGA link + mux channel to an f6222_dev_t.
 *
 * One instance per physical BFIC (or per broadcast group) you want to
 * address; each carries its own mux_channel so several bridges can share
 * the same tasa_fpga_dev_t (and therefore the same MCU SPI peripheral).
 */
typedef struct {
    tasa_fpga_dev_t* link; /* MCU-to-FPGA transport, shared across bridges */
    uint8_t mux_channel;   /* MUX Register_Mode target select, see tasa_fpga_link.h */
} tasa_bfic_bridge_t;

/**
 * tasa_bfic_bridge_init() — bind a bridge and fill in an f6222_dev_t ready
 * to hand to any f6222_* API.
 *
 * @param bridge       Storage owned by the caller; must outlive dev's use.
 * @param dev          Filled with spi_xfer=tasa_bfic_bridge_spi_xfer, ctx=bridge.
 * @param link         Already-initialized MCU-to-FPGA transport.
 * @param mux_channel  Target BFIC/mux channel (see TASA_CMD_MUX_CHANNEL_MASK).
 */
tasa_status_t tasa_bfic_bridge_init(tasa_bfic_bridge_t* bridge, f6222_dev_t* dev, tasa_fpga_dev_t* link,
                                    uint8_t mux_channel);

/**
 * tasa_bfic_bridge_spi_xfer() — f6222_dev_t.spi_xfer implementation.
 *
 * Exposed directly in case callers want to assign it to f6222_dev_t.spi_xfer
 * themselves instead of going through tasa_bfic_bridge_init().
 */
int tasa_bfic_bridge_spi_xfer(void* ctx, const uint8_t* tx, uint8_t* rx, size_t len);

#ifdef __cplusplus
}
#endif
