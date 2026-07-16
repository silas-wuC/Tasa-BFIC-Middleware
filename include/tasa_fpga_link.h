/*
 * tasa_fpga_link.h — MCU-to-FPGA MUX passthrough transport.
 *
 * Phase 1 MUX passthrough (Ctrl_FPGA=0): set 6 GPIO pins (one per mode bit)
 * to select the BFIC route, then forward the native SPI frame unchanged.
 *
 * Platform-agnostic: caller supplies gpio_set_mux() and spi_xfer().
 *
 * tasa_fpga_mux_xfer() does not cap frame length; the caller (F6222 driver or
 * board HAL) owns sizing. Ctrl-FPGA burst limits are in tasa_fpga_ctrl.h.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "tasa_bfic_mode.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TASA_OK = 0,
    TASA_ERR_INVALID_ARG = -1,
    TASA_ERR_SPI = -2,
    TASA_ERR_GPIO = -3,
} tasa_status_t;

typedef struct {
    /**
     * Drive MUX select GPIOs before each SPI transfer.
     *
     * @param ctx       Opaque pointer from tasa_fpga_dev_t.ctx.
     * @param mode_bits 6-bit route selector (mode & TASA_BFIC_MODE_MASK).
     *                    Bit i maps to one GPIO pin; pin assignment is platform-specific.
     * @return          0 on success, negative value on error.
     */
    int (*gpio_set_mux)(void* ctx, uint8_t mode_bits);
    int (*spi_xfer)(void* ctx, const uint8_t* tx, uint8_t* rx, size_t len);
    void* ctx;
} tasa_fpga_dev_t;

tasa_status_t tasa_fpga_mux_xfer(tasa_fpga_dev_t* dev, tasa_bfic_mux_mode_t mode, const uint8_t* tx, uint8_t* rx,
                                 size_t len);

#ifdef __cplusplus
}
#endif
