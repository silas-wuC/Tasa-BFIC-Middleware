#include "tasa_fpga_ctrl.h"

#include <string.h>

#include "tasa_bfic_mode.h"

tasa_status_t tasa_fpga_ctrl_read(tasa_fpga_dev_t* dev, tasa_fpga_reg_mode_t reg_mode, uint8_t addr, uint8_t* data,
                                  size_t count) {
    if (dev == NULL || data == NULL || count == 0u) {
        return TASA_ERR_INVALID_ARG;
    }
    if (count > TASA_FPGA_CTRL_MAX_PAYLOAD) {
        return TASA_ERR_INVALID_ARG;
    }

    size_t len = count + TASA_FPGA_CTRL_READ_OVERHEAD;

    uint8_t tx[TASA_FPGA_CTRL_READ_MAX_FRAME] = {0};
    uint8_t rx[TASA_FPGA_CTRL_READ_MAX_FRAME] = {0};

    tx[0] = (uint8_t)(TASA_FPGA_CTRL_CMD_CTRL_FPGA | TASA_FPGA_CTRL_CMD_READ | (uint8_t)reg_mode);
    tx[1] = addr;
    /* tx[2..] stay 0 as dummy clock bytes. */

    tasa_status_t st = tasa_fpga_mux_xfer(dev, TASA_BFIC_MODE_FPGA_INTERNAL, tx, rx, len);
    if (st != TASA_OK) {
        return st;
    }

    memcpy(data, &rx[TASA_FPGA_CTRL_READ_OVERHEAD], count);
    return TASA_OK;
}

tasa_status_t tasa_fpga_ctrl_read_version(tasa_fpga_dev_t* dev, uint8_t version[4]) {
    return tasa_fpga_ctrl_read(dev, TASA_FPGA_REG_SYSTEM, 0x00u, version, 4u);
}
