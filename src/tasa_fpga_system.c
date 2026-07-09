#include "tasa_fpga_system.h"

tasa_status_t tasa_fpga_get_version(tasa_fpga_dev_t* dev, tasa_fpga_version_t* out) {
    if (out == NULL) return TASA_ERR_INVALID_ARG;
    return tasa_fpga_sys_read(dev, TASA_REG_MODE_SYSTEM, TASA_SYS_REG_FPGA_VER, out->raw, sizeof(out->raw));
}

tasa_status_t tasa_fpga_get_dipswitch(tasa_fpga_dev_t* dev, tasa_dipswitch_t* out) {
    if (out == NULL) return TASA_ERR_INVALID_ARG;
    return tasa_fpga_sys_read(dev, TASA_REG_MODE_SYSTEM, TASA_SYS_REG_DIPSWITCH, &out->raw, sizeof(out->raw));
}

tasa_status_t tasa_fpga_bfic_reset_write(tasa_fpga_dev_t* dev, tasa_bfic_reset_tile_t tile, uint8_t rst_bits) {
    return tasa_fpga_sys_write(dev, TASA_REG_MODE_SYSTEM, (uint8_t)tile, &rst_bits, sizeof(rst_bits));
}

tasa_status_t tasa_fpga_bfic_reset_read(tasa_fpga_dev_t* dev, tasa_bfic_reset_tile_t tile, uint8_t* rst_bits) {
    if (rst_bits == NULL) return TASA_ERR_INVALID_ARG;
    return tasa_fpga_sys_read(dev, TASA_REG_MODE_SYSTEM, (uint8_t)tile, rst_bits, sizeof(*rst_bits));
}

tasa_status_t tasa_fpga_get_calibration_addr(tasa_fpga_dev_t* dev, uint8_t* addr) {
    if (addr == NULL) return TASA_ERR_INVALID_ARG;
    return tasa_fpga_sys_read(dev, TASA_REG_MODE_SYSTEM, TASA_SYS_REG_CALIBRATION_ADDR, addr, sizeof(*addr));
}

tasa_status_t tasa_fpga_set_calibration_addr(tasa_fpga_dev_t* dev, uint8_t addr) {
    return tasa_fpga_sys_write(dev, TASA_REG_MODE_SYSTEM, TASA_SYS_REG_CALIBRATION_ADDR, &addr, sizeof(addr));
}

tasa_status_t tasa_fpga_set_beam(tasa_fpga_dev_t* dev, const tasa_beam_set_t* beam) {
    if (beam == NULL) return TASA_ERR_INVALID_ARG;

    uint8_t burst[TASA_SYS_BEAM_BURST_LEN];
    burst[0] = (uint8_t)(beam->pol_id_address >> 24u);
    burst[1] = (uint8_t)(beam->pol_id_address >> 16u);
    burst[2] = (uint8_t)(beam->pol_id_address >> 8u);
    burst[3] = (uint8_t)(beam->pol_id_address >> 0u);
    burst[4] = (uint8_t)(beam->beam_id_address >> 24u);
    burst[5] = (uint8_t)(beam->beam_id_address >> 16u);
    burst[6] = (uint8_t)(beam->beam_id_address >> 8u);
    burst[7] = (uint8_t)(beam->beam_id_address >> 0u);
    burst[8] = beam->beam_mode.raw;

    return tasa_fpga_sys_write(dev, TASA_REG_MODE_SYSTEM, TASA_SYS_REG_POL_ID_ADDR, burst, sizeof(burst));
}

tasa_status_t tasa_fpga_get_beam(tasa_fpga_dev_t* dev, tasa_beam_set_t* out) {
    if (out == NULL) return TASA_ERR_INVALID_ARG;

    uint8_t burst[TASA_SYS_BEAM_BURST_LEN] = {0};
    tasa_status_t ret = tasa_fpga_sys_read(dev, TASA_REG_MODE_SYSTEM, TASA_SYS_REG_POL_ID_ADDR, burst, sizeof(burst));
    if (ret != TASA_OK) return ret;

    out->pol_id_address =
        ((uint32_t)burst[0] << 24u) | ((uint32_t)burst[1] << 16u) | ((uint32_t)burst[2] << 8u) | (uint32_t)burst[3];
    out->beam_id_address =
        ((uint32_t)burst[4] << 24u) | ((uint32_t)burst[5] << 16u) | ((uint32_t)burst[6] << 8u) | (uint32_t)burst[7];
    out->beam_mode.raw = burst[8];

    return TASA_OK;
}

tasa_status_t tasa_fpga_beam_is_busy(tasa_fpga_dev_t* dev, bool* busy) {
    if (busy == NULL) return TASA_ERR_INVALID_ARG;

    uint8_t raw = 0;
    tasa_status_t ret = tasa_fpga_sys_read(dev, TASA_REG_MODE_SYSTEM, TASA_SYS_REG_BEAM_MODE, &raw, sizeof(raw));
    if (ret != TASA_OK) return ret;

    tasa_beam_mode_t mode;
    mode.raw = raw;
    *busy = !mode.bits.auto_mode_status;

    return TASA_OK;
}
