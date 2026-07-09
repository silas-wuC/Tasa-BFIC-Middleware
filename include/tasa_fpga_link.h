/*
 * tasa_fpga_link.h — MCU-to-FPGA SPI transport layer.
 *
 * Implements the command-byte envelope defined in "TASA_MCU to FPGA SPI
 * Protocol.xlsx" (sheet "SPI Format" / "Register Table"). The MCU never
 * talks to the BFIC (F6222) directly: every transaction goes out over the
 * MCU's own SPI peripheral to the FPGA, which either services it locally
 * (Ctrl-FPGA register space: System / I2C State / I2C Write / I2C Result)
 * or forwards it verbatim to a physical BFIC over its own SPI mux
 * (MUX passthrough — see tasa_fpga_mux_xfer() and tasa_bfic_bridge.h).
 *
 * Platform-agnostic: no OS, no stdlib, no SPI implementation. The caller
 * supplies a single spi_xfer() callback (mirrors RENESAS-F6222-Driver's
 * f6222_dev_t so the same HAL glue can serve both).
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════════
 * Command Byte (SPI Format sheet, "Command (1 Byte)")
 * ═══════════════════════════════════════════════════════════════
 *
 *   Bit7   Ctrl_FPGA   1 = System / Ctrl-FPGA register space
 *                      0 = MUX passthrough to a physical BFIC
 *
 *   System side (Ctrl_FPGA = 1):
 *     Bit4      R / W\u0304   1 = Read, 0 = Write
 *     Bit[3:0]  Register_Mode (one-hot; see TASA_REG_MODE_*)
 *
 *   MUX side (Ctrl_FPGA = 0):
 *     Bit[4:1]  Register_Mode — mux/target-channel select
 *     Bit0      Dummy
 */
#define TASA_CMD_CTRL_FPGA_BIT (1u << 7u)
#define TASA_CMD_READ_BIT (1u << 4u)

#define TASA_CMD_SYS_REG_MODE_MASK 0x0Fu

#define TASA_CMD_MUX_CHANNEL_SHIFT 1u
#define TASA_CMD_MUX_CHANNEL_MASK 0x0Fu /* 4-bit channel field, bits [4:1] */

/* Register_Mode one-hot values, Ctrl_FPGA=1 side (Register Table sheet) */
#define TASA_REG_MODE_SYSTEM 0x8u     /* FPGA/HW version, Pol/Beam ID, Beam mode, BFIC Reset */
#define TASA_REG_MODE_I2C_STATE 0x4u  /* SLV_DIR / REG_BASE / LEN_N / START / STATE */
#define TASA_REG_MODE_I2C_WRITE 0x2u  /* WDATA[0..255] */
#define TASA_REG_MODE_I2C_RESULT 0x1u /* RDATA[0..255] */

/* Largest single System-side burst this link supports (I2C Write/Result
 * blocks span the full 0x00-0xFF address space). */
#define TASA_FPGA_SYS_MAX_DATA 256u

/* Largest MUX-passthrough native BFIC frame forwarded in one CS cycle
 * (F6222 local/global write frames are 4-5 bytes; leaves headroom for
 * FBS follow-up bytes). */
#define TASA_FPGA_MUX_MAX_DATA 32u

typedef enum {
    TASA_OK = 0,
    TASA_ERR_INVALID_ARG = -1,
    TASA_ERR_SPI = -2,
    TASA_ERR_TIMEOUT = -3,
    TASA_ERR_NACK = -4,
} tasa_status_t;

/**
 * tasa_fpga_dev_t — hardware abstraction for the MCU-side SPI peripheral
 * wired to the FPGA (CSB + SCLK/SDI/SDO), one instance per physical link.
 */
typedef struct {
    /**
     * SPI transfer to the FPGA.
     *
     * @param ctx  Opaque pointer from tasa_fpga_dev_t.ctx.
     * @param tx   Bytes to clock out on MOSI — always non-NULL.
     * @param rx   Buffer for MISO bytes; pass NULL when read-back not needed.
     * @param len  Number of bytes in the transaction.
     * @return     0 on success, negative value on error.
     *
     * Caller drives CSB low before the first clock and high after the last.
     */
    int (*spi_xfer)(void* ctx, const uint8_t* tx, uint8_t* rx, size_t len);

    void* ctx; /* passed verbatim as the first arg to spi_xfer */
} tasa_fpga_dev_t;

/* ── Ctrl-FPGA register access (System / I2C State / I2C Write / I2C Result) ── */

/**
 * tasa_fpga_sys_write() — SPI Write Register: CMD, RegAddr, Data(Addr..Addr+n-1).
 *
 * @param reg_mode  One of TASA_REG_MODE_* (selects the Ctrl-FPGA register block).
 * @param reg_addr  8-bit register address within that block.
 * @param data      Bytes to write, data[0] lands at reg_addr.
 * @param len       Number of bytes, 1..TASA_FPGA_SYS_MAX_DATA.
 */
tasa_status_t tasa_fpga_sys_write(tasa_fpga_dev_t* dev, uint8_t reg_mode, uint8_t reg_addr, const uint8_t* data,
                                  size_t len);

/**
 * tasa_fpga_sys_read() — SPI Read Register: CMD, RegAddr, Dummy, then burst read.
 *
 * @param data  Receives len bytes starting at reg_addr.
 */
tasa_status_t tasa_fpga_sys_read(tasa_fpga_dev_t* dev, uint8_t reg_mode, uint8_t reg_addr, uint8_t* data, size_t len);

/* ── MUX passthrough (raw BFIC SPI frame forwarding) ── */

/**
 * tasa_fpga_mux_xfer() — prepend a 1-byte MUX command (Ctrl_FPGA=0, channel in
 * bits[4:1]) to an already-assembled native BFIC SPI frame and forward it
 * through the FPGA's SPI mux to the selected physical BFIC.
 *
 * `tx`/`rx`/`len` are the exact bytes the F6222 driver's spi_xfer() callback
 * receives — see tasa_bfic_bridge.h, which wires this function in directly.
 *
 * NOTE: the MUX-side register map is not finalized in protocol spec v0.01
 * (blank in "SPI Format" sheet rows 14-23); this implementation assumes the
 * FPGA forwards the payload byte-for-byte with a fixed 1-byte offset. Confirm
 * against the FPGA team before relying on this for anything but bring-up.
 *
 * @param mux_channel  4-bit target selector (0 = broadcast, per legacy
 *                     precedent in CDP-Eureka-MCU's AIP_SPI channel field).
 */
tasa_status_t tasa_fpga_mux_xfer(tasa_fpga_dev_t* dev, uint8_t mux_channel, const uint8_t* tx, uint8_t* rx, size_t len);

#ifdef __cplusplus
}
#endif
