#include "tasa_fpga_link.h"

tasa_status_t tasa_fpga_mux_xfer(tasa_fpga_dev_t* dev, tasa_bfic_mux_mode_t mode, const uint8_t* tx, uint8_t* rx,
                                 size_t len) {
    if (dev == NULL || dev->gpio_set_mux == NULL || dev->spi_xfer == NULL || tx == NULL) {
        return TASA_ERR_INVALID_ARG;
    }
    if (len == 0u || len > TASA_FPGA_MUX_MAX_DATA) {
        return TASA_ERR_INVALID_ARG;
    }
    if (!tasa_bfic_mode_is_valid(mode)) {
        return TASA_ERR_INVALID_ARG;
    }

    int ret = dev->gpio_set_mux(dev->ctx, tasa_bfic_mode_gpio_bits(mode));
    if (ret < 0) {
        return TASA_ERR_GPIO;
    }

    ret = dev->spi_xfer(dev->ctx, tx, rx, len);
    if (ret < 0) {
        return TASA_ERR_SPI;
    }

    return TASA_OK;
}
