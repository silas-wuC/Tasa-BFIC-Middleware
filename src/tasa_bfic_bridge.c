#include "tasa_bfic_bridge.h"

int tasa_bfic_bridge_spi_xfer(void* ctx, const uint8_t* tx, uint8_t* rx, size_t len) {
    tasa_bfic_bridge_t* bridge = (tasa_bfic_bridge_t*)ctx;

    if (bridge == NULL || bridge->link == NULL) {
        return -1;
    }

    return (tasa_fpga_mux_xfer(bridge->link, bridge->mode, tx, rx, len) == TASA_OK) ? 0 : -1;
}

tasa_status_t tasa_bfic_bridge_init(tasa_bfic_bridge_t* bridge, f6222_dev_t* dev, tasa_fpga_dev_t* link,
                                    tasa_bfic_mux_mode_t mode) {
    if (bridge == NULL || dev == NULL || link == NULL) {
        return TASA_ERR_INVALID_ARG;
    }
    if (!tasa_bfic_mode_is_valid(mode)) {
        return TASA_ERR_INVALID_ARG;
    }

    bridge->link = link;
    bridge->mode = mode;

    dev->spi_xfer = tasa_bfic_bridge_spi_xfer;
    dev->ctx = bridge;

    return TASA_OK;
}
