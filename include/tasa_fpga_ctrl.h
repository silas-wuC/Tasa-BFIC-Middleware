/*
 * tasa_fpga_ctrl.h — FPGA internal register access (MUX mode 0x2F).
 *
 * Unlike the BFIC passthrough modes (0x00–0x2D) which forward native F6222
 * frames to a target BFIC, mode 0x2F (TASA_BFIC_MODE_FPGA_INTERNAL) speaks the
 * FPGA's own register protocol: a Command byte (Ctrl_FPGA | R/W | Register
 * Mode), a register address, then dummy/data bytes.
 *
 * This layer sits beside tasa_bfic_bridge — it does NOT involve the F6222
 * driver. It reuses tasa_fpga_mux_xfer() so GPIO MUX select (0x2F) and the
 * board SPI transfer are shared with the passthrough path.
 *
 * SPI Read framing (read `count` bytes starting at `addr`):
 *   index:  0     1      2      3           4             ...
 *   MOSI:   CMD   addr   dummy  dummy       dummy         ...
 *   MISO:   x     x      x      Data(addr)  Data(addr+1)  ...
 * Frame length = count + 3; received data starts at rx[3].
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "tasa_bfic_mode.h"
#include "tasa_fpga_link.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}
#endif
