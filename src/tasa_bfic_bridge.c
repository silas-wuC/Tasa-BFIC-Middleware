#include "tasa_bfic_bridge.h"

int tasa_bfic_bridge_spi_xfer(void* ctx, const uint8_t* tx, uint8_t* rx, size_t len) {
    tasa_bfic_bridge_t* bridge = (tasa_bfic_bridge_t*)ctx;

    if (bridge == NULL || bridge->link == NULL) return -1;

    return (tasa_fpga_mux_xfer(bridge->link, bridge->mux_channel, tx, rx, len) == TASA_OK) ? 0 : -1;
}

tasa_status_t tasa_bfic_bridge_init(tasa_bfic_bridge_t* bridge, f6222_dev_t* dev, tasa_fpga_dev_t* link,
                                    uint8_t mux_channel) {
    if (bridge == NULL || dev == NULL || link == NULL) return TASA_ERR_INVALID_ARG;

    bridge->link = link;
    bridge->mux_channel = mux_channel;

    dev->spi_xfer = tasa_bfic_bridge_spi_xfer;
    dev->ctx = bridge;

    return TASA_OK;
}
