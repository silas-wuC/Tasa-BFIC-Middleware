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

tasa_status_t tasa_fpga_ctrl_read_pol_id(tasa_fpga_dev_t* dev, uint8_t pol_id[TASA_FPGA_CTRL_POL_ID_LEN]) {
    return tasa_fpga_ctrl_read(dev, TASA_FPGA_REG_SYSTEM, TASA_FPGA_CTRL_POL_ID_ADDR, pol_id,
                               TASA_FPGA_CTRL_POL_ID_LEN);
}

tasa_status_t tasa_fpga_ctrl_write_pol_id(tasa_fpga_dev_t* dev, const uint8_t pol_id[TASA_FPGA_CTRL_POL_ID_LEN]) {
    return tasa_fpga_ctrl_write(dev, TASA_FPGA_REG_SYSTEM, TASA_FPGA_CTRL_POL_ID_ADDR, pol_id,
                                TASA_FPGA_CTRL_POL_ID_LEN);
}

tasa_status_t tasa_fpga_ctrl_read_beam_id(tasa_fpga_dev_t* dev, uint8_t beam_id[TASA_FPGA_CTRL_BEAM_ID_LEN]) {
    return tasa_fpga_ctrl_read(dev, TASA_FPGA_REG_SYSTEM, TASA_FPGA_CTRL_BEAM_ID_ADDR, beam_id,
                               TASA_FPGA_CTRL_BEAM_ID_LEN);
}

tasa_status_t tasa_fpga_ctrl_write_beam_id(tasa_fpga_dev_t* dev, const uint8_t beam_id[TASA_FPGA_CTRL_BEAM_ID_LEN]) {
    return tasa_fpga_ctrl_write(dev, TASA_FPGA_REG_SYSTEM, TASA_FPGA_CTRL_BEAM_ID_ADDR, beam_id,
                                TASA_FPGA_CTRL_BEAM_ID_LEN);
}

tasa_status_t tasa_fpga_ctrl_read_beam_mode(tasa_fpga_dev_t* dev, uint8_t* mode) {
    return tasa_fpga_ctrl_read(dev, TASA_FPGA_REG_SYSTEM, TASA_FPGA_CTRL_BEAM_MODE_ADDR, mode,
                               TASA_FPGA_CTRL_BEAM_MODE_LEN);
}

tasa_status_t tasa_fpga_ctrl_write_beam_mode(tasa_fpga_dev_t* dev, uint8_t beam_mode_reg) {
    return tasa_fpga_ctrl_write(dev, TASA_FPGA_REG_SYSTEM, TASA_FPGA_CTRL_BEAM_MODE_ADDR, &beam_mode_reg,
                                TASA_FPGA_CTRL_BEAM_MODE_LEN);
}

tasa_status_t tasa_fpga_ctrl_beam_is_done(tasa_fpga_dev_t* dev, bool* done) {
    if (done == NULL) {
        return TASA_ERR_INVALID_ARG;
    }

    uint8_t mode = 0;
    tasa_status_t st = tasa_fpga_ctrl_read_beam_mode(dev, &mode);
    if (st != TASA_OK) {
        return st;
    }

    /* bit 3 inverted: 1 = busy, 0 = done. */
    *done = ((mode & TASA_FPGA_CTRL_BEAM_MODE_AUTO_STAT_MASK) == 0u);
    return TASA_OK;
}

tasa_status_t tasa_fpga_ctrl_set_beam(tasa_fpga_dev_t* dev, tasa_bfic_dir_t dir, tasa_beam_polar_t polar,
                                      tasa_beam_phase_t phase, uint32_t timeout_ms) {
    /* 1. Read current register (read-modify-write; keep unrelated bits). */
    uint8_t mode = 0;
    tasa_status_t st = tasa_fpga_ctrl_read_beam_mode(dev, &mode);
    if (st != TASA_OK) {
        return st;
    }

    /* 2. Set only the config bits (0/1/2); clear Set Beam so we can pulse it. */
    mode &= (uint8_t)~(TASA_FPGA_CTRL_BEAM_MODE_TX_RX_MASK | TASA_FPGA_CTRL_BEAM_MODE_LIN_CIR_MASK |
                       TASA_FPGA_CTRL_BEAM_MODE_PHASE_MASK | TASA_FPGA_CTRL_BEAM_MODE_SET_BEAM_MASK);
    if (dir == TASA_BFIC_DIR_TX) {
        mode |= TASA_FPGA_CTRL_BEAM_MODE_TX_RX_MASK;
    }
    if (polar == TASA_BEAM_LINEAR) {
        mode |= TASA_FPGA_CTRL_BEAM_MODE_LIN_CIR_MASK;
    }
    if (phase == TASA_BEAM_PHASE_90) {
        mode |= TASA_FPGA_CTRL_BEAM_MODE_PHASE_MASK;
    }
    st = tasa_fpga_ctrl_write_beam_mode(dev, mode);
    if (st != TASA_OK) {
        return st;
    }

    /* 3. Pulse Set Beam (bit 4 = 1) to start. */
    st = tasa_fpga_ctrl_write_beam_mode(dev, (uint8_t)(mode | TASA_FPGA_CTRL_BEAM_MODE_SET_BEAM_MASK));
    if (st != TASA_OK) {
        return st;
    }

#if TASA_FPGA_CTRL_BEAM_SET_EDGE_TRIGGER
    /* 4. Edge trigger: drop Set Beam back to 0 to re-arm for next time. */
    st = tasa_fpga_ctrl_write_beam_mode(dev, mode);
    if (st != TASA_OK) {
        return st;
    }
#endif

    /* 5. Block on Auto mode status (bit 3) until done or the deadline passes. */
    uint32_t start = dev->get_tick_ms(dev->ctx);
    for (;;) {
        bool done = false;
        st = tasa_fpga_ctrl_beam_is_done(dev, &done);
        if (st != TASA_OK) {
            return st;
        }
        if (done) {
            return TASA_OK;
        }
        /* Unsigned wrap-around subtraction: stays correct even if the tick
         * counter has rolled over since `start`. */
        if ((uint32_t)(dev->get_tick_ms(dev->ctx) - start) >= timeout_ms) {
            return TASA_ERR_TIMEOUT;
        }
        /* Throttle: without this the loop busy-polls flat out, saturating
         * the SPI bus and pegging the CPU on bare metal. */
        dev->delay_ms(dev->ctx, 1);
    }
}
