#include "tasa_fpga_link.h"

tasa_status_t tasa_fpga_sys_write(tasa_fpga_dev_t* dev, uint8_t reg_mode, uint8_t reg_addr, const uint8_t* data,
                                  size_t len) {
    if (dev == NULL || dev->spi_xfer == NULL || data == NULL) return TASA_ERR_INVALID_ARG;
    if (len == 0u || len > TASA_FPGA_SYS_MAX_DATA) return TASA_ERR_INVALID_ARG;

    uint8_t tx[2u + TASA_FPGA_SYS_MAX_DATA];
    size_t i;
    int ret;

    tx[0] = TASA_CMD_CTRL_FPGA_BIT | (reg_mode & TASA_CMD_SYS_REG_MODE_MASK);
    tx[1] = reg_addr;
    for (i = 0; i < len; i++) tx[2u + i] = data[i];

    ret = dev->spi_xfer(dev->ctx, tx, NULL, 2u + len);
    if (ret < 0) return TASA_ERR_SPI;

    return TASA_OK;
}

tasa_status_t tasa_fpga_sys_read(tasa_fpga_dev_t* dev, uint8_t reg_mode, uint8_t reg_addr, uint8_t* data,
                                 size_t len) {
    if (dev == NULL || dev->spi_xfer == NULL || data == NULL) return TASA_ERR_INVALID_ARG;
    if (len == 0u || len > TASA_FPGA_SYS_MAX_DATA) return TASA_ERR_INVALID_ARG;

    /* MOSI: CMD, RegAddr, Dummy, -, -, ...  MISO: -, -, Dummy, Data(Addr), ... */
    uint8_t tx[3u + TASA_FPGA_SYS_MAX_DATA] = {0};
    uint8_t rx[3u + TASA_FPGA_SYS_MAX_DATA] = {0};
    size_t i;
    int ret;

    tx[0] = TASA_CMD_CTRL_FPGA_BIT | TASA_CMD_READ_BIT | (reg_mode & TASA_CMD_SYS_REG_MODE_MASK);
    tx[1] = reg_addr;
    tx[2] = 0x00u; /* dummy */

    ret = dev->spi_xfer(dev->ctx, tx, rx, 3u + len);
    if (ret < 0) return TASA_ERR_SPI;

    for (i = 0; i < len; i++) data[i] = rx[3u + i];

    return TASA_OK;
}

tasa_status_t tasa_fpga_mux_xfer(tasa_fpga_dev_t* dev, uint8_t mux_channel, const uint8_t* tx, uint8_t* rx,
                                 size_t len) {
    if (dev == NULL || dev->spi_xfer == NULL || tx == NULL) return TASA_ERR_INVALID_ARG;
    if (len == 0u || len > TASA_FPGA_MUX_MAX_DATA) return TASA_ERR_INVALID_ARG;

    uint8_t tx_buf[1u + TASA_FPGA_MUX_MAX_DATA];
    uint8_t rx_buf[1u + TASA_FPGA_MUX_MAX_DATA];
    size_t i;
    int ret;

    tx_buf[0] = (uint8_t)((mux_channel & TASA_CMD_MUX_CHANNEL_MASK) << TASA_CMD_MUX_CHANNEL_SHIFT);
    for (i = 0; i < len; i++) tx_buf[1u + i] = tx[i];

    ret = dev->spi_xfer(dev->ctx, tx_buf, (rx != NULL) ? rx_buf : NULL, 1u + len);
    if (ret < 0) return TASA_ERR_SPI;

    if (rx != NULL) {
        for (i = 0; i < len; i++) rx[i] = rx_buf[1u + i];
    }

    return TASA_OK;
}
