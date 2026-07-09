/*
 * tasa_fpga_i2c.h — Ctrl-FPGA I2C bridge (I2C State / I2C Write / I2C
 * Result register blocks, Register_Mode = 0x4 / 0x2 / 0x1).
 *
 * Lets the MCU drive an I2C device that only the FPGA has a physical bus
 * to (per the Register Table sheet's example, a PMIC at 7-bit address
 * 0x14). The FPGA does the actual I2C bit-banging/peripheral work; the MCU
 * just stages the transfer over SPI and polls for completion.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tasa_fpga_link.h"

#ifdef __cplusplus
extern "C" {
#endif

/* I2C State block (Register_Mode = 0x4) register addresses */
#define TASA_I2C_REG_SLV_DIR 0x00u  /* bit7 RnW (1=read/0=write), bits[6:0] = 7-bit slave addr */
#define TASA_I2C_REG_REG_BASE 0x01u /* target device's internal register start address */
#define TASA_I2C_REG_LEN_N 0x02u    /* transfer byte count N */
#define TASA_I2C_REG_START 0x03u    /* write bit0=1 to launch; self-clears once consumed */
#define TASA_I2C_REG_STATE 0x04u    /* bit0 DONE, bit1 NACK */

#define TASA_I2C_SLV_DIR_READ_BIT (1u << 7u)
#define TASA_I2C_SLV_DIR_ADDR_MASK 0x7Fu

#define TASA_I2C_STATE_DONE_BIT (1u << 0u)
#define TASA_I2C_STATE_NACK_BIT (1u << 1u)

#define TASA_I2C_START_BIT (1u << 0u)

/* WDATA[0..255] (Register_Mode = 0x2) / RDATA[0..255] (Register_Mode = 0x1).
 * Capped at 255 (not 256) since LEN_N is a single byte. */
#define TASA_I2C_MAX_LEN 255u

/** Default polling budget for tasa_fpga_i2c_write()/read() completion. */
#define TASA_I2C_POLL_MAX 1000u

/**
 * tasa_fpga_i2c_write() — write `len` bytes to a 7-bit I2C slave's internal
 * registers starting at `reg_base`, via the FPGA's I2C bridge.
 *
 * Blocks polling TASA_I2C_REG_STATE up to `poll_max` times (each poll is one
 * SPI transaction; there is no wall-clock delay between polls — insert your
 * own if the I2C device is slow).
 *
 * @return TASA_OK, TASA_ERR_NACK if the slave NACK'd, TASA_ERR_TIMEOUT if
 *         DONE never set within poll_max iterations, or an argument/SPI error.
 */
tasa_status_t tasa_fpga_i2c_write(tasa_fpga_dev_t* dev, uint8_t slave_addr7, uint8_t reg_base, const uint8_t* data,
                                  size_t len, uint32_t poll_max);

/**
 * tasa_fpga_i2c_read() — read `len` bytes from a 7-bit I2C slave's internal
 * registers starting at `reg_base`, via the FPGA's I2C bridge.
 */
tasa_status_t tasa_fpga_i2c_read(tasa_fpga_dev_t* dev, uint8_t slave_addr7, uint8_t reg_base, uint8_t* data, size_t len,
                                 uint32_t poll_max);

#ifdef __cplusplus
}
#endif
