/*
 * tasa_fpga_ctrl.h — FPGA internal register access (MUX mode 0x2F).
 *
 * Unlike the BFIC passthrough modes (0x00–0x2D) which forward native F6222
 * frames to a target BFIC, mode 0x2F (TASA_BFIC_MODE_FPGA_INTERNAL) speaks the
 * FPGA Ctrl-FPGA protocol (Command byte bit 7 = 1):
 *
 *   bit 7     : 1 = Ctrl FPGA (0 = MUX passthrough)
 *   bit 4     : R/W (1 = read, 0 = write)
 *   bits 3:0  : Register Mode (System / I2C State / I2C Write / I2C Result)
 *
 * SPI Read Register (read `count` bytes starting at `addr`):
 *   index:  0       1         2        3            4 ...
 *   MOSI:   CMD(R)  Reg Addr  Dummy    Dummy        ...
 *   MISO:   -       -         Dummy    Data(Addr)   Data(Addr+1) ...
 *
 * SPI Write Register (not implemented yet):
 *   index:  0       1         2            3 ...
 *   MOSI:   CMD(W)  Reg Addr  Data(Addr)   Data(Addr+1) ...
 *
 * This layer sits beside tasa_bfic_bridge — it does NOT involve the F6222
 * driver. It reuses tasa_fpga_mux_xfer() so GPIO MUX select (0x2F) and the
 * board SPI transfer are shared with the passthrough path.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "tasa_fpga_link.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Max data bytes per Ctrl-FPGA burst (FPGA SPI Format spec). */
#define TASA_FPGA_CTRL_MAX_PAYLOAD 20u

/** Read frame overhead: CMD + Reg Addr + one dummy clock byte. */
#define TASA_FPGA_CTRL_READ_OVERHEAD 3u

/** Max SPI clock bytes for one Ctrl-FPGA read transfer. */
#define TASA_FPGA_CTRL_READ_MAX_FRAME (TASA_FPGA_CTRL_MAX_PAYLOAD + TASA_FPGA_CTRL_READ_OVERHEAD)

/** Write frame overhead: CMD + Reg Addr (no dummy). Reserved for tasa_fpga_ctrl_write. */
#define TASA_FPGA_CTRL_WRITE_OVERHEAD 2u

/** Max SPI clock bytes for one Ctrl-FPGA write transfer. Reserved for future write API. */
#define TASA_FPGA_CTRL_WRITE_MAX_FRAME (TASA_FPGA_CTRL_MAX_PAYLOAD + TASA_FPGA_CTRL_WRITE_OVERHEAD)

/* Register Mode field (Command byte bits 3:0). Only SYSTEM is implemented for
 * now; the I2C_* values are placeholders for later phases. */
typedef enum {
    TASA_FPGA_REG_SYSTEM = 0x8u,
    TASA_FPGA_REG_I2C_STATE = 0x4u,
    TASA_FPGA_REG_I2C_WRITE = 0x2u,
    TASA_FPGA_REG_I2C_RESULT = 0x1u,
} tasa_fpga_reg_mode_t;

/* Command byte (bit 7 = Ctrl FPGA, bit 4 = R/!W: 1=read, 0=write). */
#define TASA_FPGA_CTRL_CMD_CTRL_FPGA 0x80u
#define TASA_FPGA_CTRL_CMD_READ 0x10u

/** True dummy byte after CMD+Addr, giving FPGA time to react; no valid MISO data yet. */
#define TASA_FPGA_CTRL_DUMMY_BYTE 0xDDu

/** MOSI filler byte sent while clocking in read data; distinct pattern from the true dummy. */
#define TASA_FPGA_CTRL_READ_FILLER_BYTE 0x5Au

/**
 * Read `count` bytes from an FPGA internal register block over MUX mode 0x2F.
 *
 * @param dev       FPGA MUX link (same struct used by the passthrough path).
 * @param reg_mode  Register Mode selector (Command byte bits 3:0).
 * @param addr      Starting register address.
 * @param data      Output buffer, receives `count` bytes.
 * @param count     Byte count; 1..TASA_FPGA_CTRL_MAX_PAYLOAD.
 * @return          TASA_OK on success, negative tasa_status_t on error.
 */
tasa_status_t tasa_fpga_ctrl_read(tasa_fpga_dev_t* dev, tasa_fpga_reg_mode_t reg_mode, uint8_t addr, uint8_t* data,
                                  size_t count);

/** System register block: FPGA firmware version starting address. */
#define TASA_FPGA_CTRL_VERSION_ADDR 0x00u

/** FPGA firmware version byte count (Major / Minor / Patch / Pre-Release). */
#define TASA_FPGA_CTRL_VERSION_LEN 4u

/**
 * Read the 4-byte FPGA firmware version over MUX mode 0x2F (System register block).
 *
 * Reads TASA_FPGA_CTRL_VERSION_LEN bytes starting at TASA_FPGA_CTRL_VERSION_ADDR
 * via TASA_FPGA_REG_SYSTEM. Output layout:
 *   version[0] — Major
 *   version[1] — Minor
 *   version[2] — Patch
 *   version[3] — Pre-Release
 *
 * @param dev     FPGA MUX link (same struct used by the passthrough path).
 * @param version Output buffer; must hold TASA_FPGA_CTRL_VERSION_LEN bytes.
 * @return        TASA_OK on success, negative tasa_status_t on error.
 */
tasa_status_t tasa_fpga_ctrl_read_version(tasa_fpga_dev_t* dev, uint8_t version[TASA_FPGA_CTRL_VERSION_LEN]);

/** System register block: DIP switch status address. */
#define TASA_FPGA_CTRL_DIP_SWITCH_ADDR 0x04u

/** DIP switch status byte count. */
#define TASA_FPGA_CTRL_DIP_SWITCH_LEN 1u

/**
 * Read the 1-byte DIP switch status over MUX mode 0x2F (System register block).
 *
 * Reads TASA_FPGA_CTRL_DIP_SWITCH_LEN byte at TASA_FPGA_CTRL_DIP_SWITCH_ADDR
 * via TASA_FPGA_REG_SYSTEM (Register Mode System, 0x08). The register packs two
 * fields in one byte (read-only, default 0x00):
 *   status[7:4] — HW Version [3:0]
 *   status[3:0] — Switch [3:0] (DIP switch positions)
 *
 * Example decode:
 *   hw_version    = (status >> 4) & 0x0Fu;
 *   switch_status = status & 0x0Fu;
 *
 * @param dev    FPGA MUX link (same struct used by the passthrough path).
 * @param status Output byte; receives the raw DIPSwitch status register value.
 * @return       TASA_OK on success, negative tasa_status_t on error.
 */
tasa_status_t tasa_fpga_ctrl_read_dip_switch_status(tasa_fpga_dev_t* dev, uint8_t* status);

#ifdef __cplusplus
}
#endif
