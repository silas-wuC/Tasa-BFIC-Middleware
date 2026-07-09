#include "tasa_fpga_i2c.h"

static tasa_status_t stage_transfer(tasa_fpga_dev_t* dev, uint8_t slv_dir, uint8_t reg_base, uint8_t len) {
    tasa_status_t ret;

    ret = tasa_fpga_sys_write(dev, TASA_REG_MODE_I2C_STATE, TASA_I2C_REG_SLV_DIR, &slv_dir, sizeof(slv_dir));
    if (ret != TASA_OK) return ret;

    ret = tasa_fpga_sys_write(dev, TASA_REG_MODE_I2C_STATE, TASA_I2C_REG_REG_BASE, &reg_base, sizeof(reg_base));
    if (ret != TASA_OK) return ret;

    ret = tasa_fpga_sys_write(dev, TASA_REG_MODE_I2C_STATE, TASA_I2C_REG_LEN_N, &len, sizeof(len));
    if (ret != TASA_OK) return ret;

    uint8_t start = TASA_I2C_START_BIT;
    return tasa_fpga_sys_write(dev, TASA_REG_MODE_I2C_STATE, TASA_I2C_REG_START, &start, sizeof(start));
}

static tasa_status_t wait_done(tasa_fpga_dev_t* dev, uint32_t poll_max) {
    uint32_t i;

    for (i = 0; i < poll_max; i++) {
        uint8_t state = 0;
        tasa_status_t ret = tasa_fpga_sys_read(dev, TASA_REG_MODE_I2C_STATE, TASA_I2C_REG_STATE, &state, 1u);
        if (ret != TASA_OK) return ret;

        if (state & TASA_I2C_STATE_NACK_BIT) return TASA_ERR_NACK;
        if (state & TASA_I2C_STATE_DONE_BIT) return TASA_OK;
    }

    return TASA_ERR_TIMEOUT;
}

tasa_status_t tasa_fpga_i2c_write(tasa_fpga_dev_t* dev, uint8_t slave_addr7, uint8_t reg_base, const uint8_t* data,
                                  size_t len, uint32_t poll_max) {
    if (data == NULL || len == 0u || len > TASA_I2C_MAX_LEN) return TASA_ERR_INVALID_ARG;

    tasa_status_t ret = tasa_fpga_sys_write(dev, TASA_REG_MODE_I2C_WRITE, 0x00u, data, len);
    if (ret != TASA_OK) return ret;

    ret = stage_transfer(dev, (uint8_t)(slave_addr7 & TASA_I2C_SLV_DIR_ADDR_MASK), reg_base, (uint8_t)len);
    if (ret != TASA_OK) return ret;

    return wait_done(dev, poll_max);
}

tasa_status_t tasa_fpga_i2c_read(tasa_fpga_dev_t* dev, uint8_t slave_addr7, uint8_t reg_base, uint8_t* data, size_t len,
                                 uint32_t poll_max) {
    if (data == NULL || len == 0u || len > TASA_I2C_MAX_LEN) return TASA_ERR_INVALID_ARG;

    uint8_t slv_dir = (uint8_t)((slave_addr7 & TASA_I2C_SLV_DIR_ADDR_MASK) | TASA_I2C_SLV_DIR_READ_BIT);
    tasa_status_t ret = stage_transfer(dev, slv_dir, reg_base, (uint8_t)len);
    if (ret != TASA_OK) return ret;

    ret = wait_done(dev, poll_max);
    if (ret != TASA_OK) return ret;

    return tasa_fpga_sys_read(dev, TASA_REG_MODE_I2C_RESULT, 0x00u, data, len);
}
