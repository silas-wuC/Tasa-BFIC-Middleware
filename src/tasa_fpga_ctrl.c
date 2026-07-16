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
    /* tx[2] is the true dummy byte; tx[3..] are MOSI filler bytes while reading data. */
    tx[2] = TASA_FPGA_CTRL_DUMMY_BYTE;
    memset(&tx[TASA_FPGA_CTRL_READ_OVERHEAD], TASA_FPGA_CTRL_READ_FILLER_BYTE, count);

    tasa_status_t st = tasa_fpga_mux_xfer(dev, TASA_BFIC_MODE_FPGA_INTERNAL, tx, rx, len);
    if (st != TASA_OK) {
        return st;
    }

    memcpy(data, &rx[TASA_FPGA_CTRL_READ_OVERHEAD], count);
    return TASA_OK;
}

tasa_status_t tasa_fpga_ctrl_write(tasa_fpga_dev_t* dev, tasa_fpga_reg_mode_t reg_mode, uint8_t addr,
                                   const uint8_t* data, size_t count) {
    if (dev == NULL || data == NULL || count == 0u) {
        return TASA_ERR_INVALID_ARG;
    }
    if (count > TASA_FPGA_CTRL_MAX_PAYLOAD) {
        return TASA_ERR_INVALID_ARG;
    }

    size_t len = count + TASA_FPGA_CTRL_WRITE_OVERHEAD;

    uint8_t tx[TASA_FPGA_CTRL_WRITE_MAX_FRAME] = {0};

    /* Write CMD: Ctrl-FPGA set, R/!W bit clear (bit 4 = 0 => write). No dummy
     * byte; data starts right after CMD + Reg Addr. */
    tx[0] = (uint8_t)(TASA_FPGA_CTRL_CMD_CTRL_FPGA | (uint8_t)reg_mode);
    tx[1] = addr;
    memcpy(&tx[TASA_FPGA_CTRL_WRITE_OVERHEAD], data, count);

    /* rx = NULL: no MISO needed for a write; the board HAL then uses a plain
     * SPI transmit instead of a full-duplex transfer. */
    return tasa_fpga_mux_xfer(dev, TASA_BFIC_MODE_FPGA_INTERNAL, tx, NULL, len);
}

tasa_status_t tasa_fpga_ctrl_read_version(tasa_fpga_dev_t* dev, uint8_t version[TASA_FPGA_CTRL_VERSION_LEN]) {
    return tasa_fpga_ctrl_read(dev, TASA_FPGA_REG_SYSTEM, TASA_FPGA_CTRL_VERSION_ADDR, version,
                               TASA_FPGA_CTRL_VERSION_LEN);
}

tasa_status_t tasa_fpga_ctrl_read_dip_switch_status(tasa_fpga_dev_t* dev, uint8_t* status) {
    return tasa_fpga_ctrl_read(dev, TASA_FPGA_REG_SYSTEM, TASA_FPGA_CTRL_DIP_SWITCH_ADDR, status,
                               TASA_FPGA_CTRL_DIP_SWITCH_LEN);
}
