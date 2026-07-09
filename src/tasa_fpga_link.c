#include "tasa_fpga_link.h"

tasa_status_t tasa_fpga_mux_xfer(tasa_fpga_dev_t* dev, tasa_bfic_mux_mode_t mode, const uint8_t* tx, uint8_t* rx,
                                 size_t len) {
    if (dev == NULL || dev->spi_xfer == NULL || tx == NULL) {
        return TASA_ERR_INVALID_ARG;
    }
    if (len == 0u || len > TASA_FPGA_MUX_MAX_DATA) {
        return TASA_ERR_INVALID_ARG;
    }
    if (!tasa_bfic_mode_is_valid(mode)) {
        return TASA_ERR_INVALID_ARG;
    }

    uint8_t tx_buf[1u + TASA_FPGA_MUX_MAX_DATA];
    uint8_t rx_buf[1u + TASA_FPGA_MUX_MAX_DATA];
    size_t i;
    int ret;

    tx_buf[0] = tasa_bfic_mode_to_cmd(mode);
    for (i = 0; i < len; i++) {
        tx_buf[1u + i] = tx[i];
    }

    ret = dev->spi_xfer(dev->ctx, tx_buf, (rx != NULL) ? rx_buf : NULL, 1u + len);
    if (ret < 0) {
        return TASA_ERR_SPI;
    }

    if (rx != NULL) {
        for (i = 0; i < len; i++) {
            rx[i] = rx_buf[1u + i];
        }
    }

    return TASA_OK;
}
