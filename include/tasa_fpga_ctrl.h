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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tasa_bfic_mode.h"
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
/* Write has no separate CMD macro: the R/!W bit (bit 4) cleared == write, so the
 * write command byte is simply (TASA_FPGA_CTRL_CMD_CTRL_FPGA | reg_mode). */

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

/**
 * Write `count` bytes to an FPGA internal register block over MUX mode 0x2F.
 *
 * Mirrors tasa_fpga_ctrl_read but drives the write frame: the R/!W bit (bit 4)
 * is cleared, there is no dummy byte, and MISO is ignored.
 *
 * @param dev       FPGA MUX link (same struct used by the passthrough path).
 * @param reg_mode  Register Mode selector (Command byte bits 3:0).
 * @param addr      Starting register address.
 * @param data      Input buffer; `count` bytes are written from here.
 * @param count     Byte count; 1..TASA_FPGA_CTRL_MAX_PAYLOAD.
 * @return          TASA_OK on success, negative tasa_status_t on error.
 */
tasa_status_t tasa_fpga_ctrl_write(tasa_fpga_dev_t* dev, tasa_fpga_reg_mode_t reg_mode, uint8_t addr,
                                   const uint8_t* data, size_t count);

/** System register block: FPGA firmware version starting address. */
#define TASA_FPGA_CTRL_VERSION_ADDR 0x00u

/** FPGA firmware version byte count (Major / Minor / Patch / Pre-Release). */
#define TASA_FPGA_CTRL_VERSION_LEN 4u

/**
 * Read the 4-byte FPGA firmware version over MUX mode 0x2F (System register block).
 *
 * Reads TASA_FPGA_CTRL_VERSION_LEN bytes starting at TASA_FPGA_CTRL_VERSION_ADDR
 * via TASA_FPGA_REG_SYSTEM (Register Mode System, 0x08). Registers are
 * read-only (R/W = R). Output layout:
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
 * via TASA_FPGA_REG_SYSTEM (Register Mode System, 0x08). Register is
 * read-only (R/W = R, default 0x00). The register packs two fields in one byte:
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

/** System register block: Pol ID starting address (32-bit, MSB @0x05). */
#define TASA_FPGA_CTRL_POL_ID_ADDR 0x05u

/** Pol ID byte count (32-bit, big-endian across 0x05..0x08). */
#define TASA_FPGA_CTRL_POL_ID_LEN 4u

/**
 * Read the 4-byte Pol ID over MUX mode 0x2F (System register block, R/W).
 *
 * Reads TASA_FPGA_CTRL_POL_ID_LEN bytes starting at TASA_FPGA_CTRL_POL_ID_ADDR
 * via TASA_FPGA_REG_SYSTEM. Big-endian, MSB first:
 *   pol_id[0] — [31:24] @0x05
 *   pol_id[1] — [23:16] @0x06
 *   pol_id[2] — [15:8]  @0x07
 *   pol_id[3] — [7:0]   @0x08
 * The caller assembles the 32-bit scalar; this layer returns raw bytes, like
 * tasa_fpga_ctrl_read_version.
 *
 * Example decode:
 *   id = ((uint32_t)pol_id[0] << 24) | ((uint32_t)pol_id[1] << 16) |
 *        ((uint32_t)pol_id[2] << 8) | pol_id[3];
 *
 * @param dev    FPGA MUX link (same struct used by the passthrough path).
 * @param pol_id Output buffer; must hold TASA_FPGA_CTRL_POL_ID_LEN bytes.
 * @return       TASA_OK on success, negative tasa_status_t on error.
 */
tasa_status_t tasa_fpga_ctrl_read_pol_id(tasa_fpga_dev_t* dev, uint8_t pol_id[TASA_FPGA_CTRL_POL_ID_LEN]);

/**
 * Write the 4-byte Pol ID over MUX mode 0x2F (System register block, R/W).
 *
 * Big-endian, MSB first:
 *   pol_id[0] — [31:24] @0x05
 *   pol_id[1] — [23:16] @0x06
 *   pol_id[2] — [15:8]  @0x07
 *   pol_id[3] — [7:0]   @0x08
 * Caller supplies the 32-bit value already split into big-endian bytes.
 *
 * Example encode:
 *   pol_id[0] = (uint8_t)(id >> 24);
 *   pol_id[1] = (uint8_t)(id >> 16);
 *   pol_id[2] = (uint8_t)(id >> 8);
 *   pol_id[3] = (uint8_t)(id);
 *
 * @param dev    FPGA MUX link (same struct used by the passthrough path).
 * @param pol_id Input buffer; TASA_FPGA_CTRL_POL_ID_LEN bytes, MSB first.
 * @return       TASA_OK on success, negative tasa_status_t on error.
 */
tasa_status_t tasa_fpga_ctrl_write_pol_id(tasa_fpga_dev_t* dev, const uint8_t pol_id[TASA_FPGA_CTRL_POL_ID_LEN]);

/** System register block: Beam ID starting address (32-bit, MSB @0x09). */
#define TASA_FPGA_CTRL_BEAM_ID_ADDR 0x09u

/** Beam ID byte count (32-bit, big-endian across 0x09..0x0C). */
#define TASA_FPGA_CTRL_BEAM_ID_LEN 4u

/**
 * Read the 4-byte Beam ID over MUX mode 0x2F (System register block, R/W).
 *
 * Reads TASA_FPGA_CTRL_BEAM_ID_LEN bytes starting at TASA_FPGA_CTRL_BEAM_ID_ADDR
 * via TASA_FPGA_REG_SYSTEM. Big-endian, MSB first:
 *   beam_id[0] — [31:24] @0x09
 *   beam_id[1] — [23:16] @0x0A
 *   beam_id[2] — [15:8]  @0x0B
 *   beam_id[3] — [7:0]   @0x0C
 * The caller assembles the 32-bit scalar; this layer returns raw bytes, like
 * tasa_fpga_ctrl_read_pol_id.
 *
 * Example decode:
 *   id = ((uint32_t)beam_id[0] << 24) | ((uint32_t)beam_id[1] << 16) |
 *        ((uint32_t)beam_id[2] << 8) | beam_id[3];
 *
 * @param dev     FPGA MUX link (same struct used by the passthrough path).
 * @param beam_id Output buffer; must hold TASA_FPGA_CTRL_BEAM_ID_LEN bytes.
 * @return        TASA_OK on success, negative tasa_status_t on error.
 */
tasa_status_t tasa_fpga_ctrl_read_beam_id(tasa_fpga_dev_t* dev, uint8_t beam_id[TASA_FPGA_CTRL_BEAM_ID_LEN]);

/**
 * Write the 4-byte Beam ID over MUX mode 0x2F (System register block, R/W).
 *
 * Big-endian, MSB first:
 *   beam_id[0] — [31:24] @0x09
 *   beam_id[1] — [23:16] @0x0A
 *   beam_id[2] — [15:8]  @0x0B
 *   beam_id[3] — [7:0]   @0x0C
 * Caller supplies the 32-bit value already split into big-endian bytes.
 *
 * Example encode:
 *   beam_id[0] = (uint8_t)(id >> 24);
 *   beam_id[1] = (uint8_t)(id >> 16);
 *   beam_id[2] = (uint8_t)(id >> 8);
 *   beam_id[3] = (uint8_t)(id);
 *
 * @param dev     FPGA MUX link (same struct used by the passthrough path).
 * @param beam_id Input buffer; TASA_FPGA_CTRL_BEAM_ID_LEN bytes, MSB first.
 * @return        TASA_OK on success, negative tasa_status_t on error.
 */
tasa_status_t tasa_fpga_ctrl_write_beam_id(tasa_fpga_dev_t* dev, const uint8_t beam_id[TASA_FPGA_CTRL_BEAM_ID_LEN]);

/** System register block: Beam mode control/status register address. */
#define TASA_FPGA_CTRL_BEAM_MODE_ADDR 0x0Du

/** Beam mode register byte count (single-byte, packs control + status bits). */
#define TASA_FPGA_CTRL_BEAM_MODE_LEN 1u

/*
 * Beam mode register (0x0D) bitfield masks. R/W, Self Clear = N.
 * Bits are decoded/encoded via these masks (read-modify-write); we deliberately
 * do NOT wrap this register in a bitfield struct — bitfield bit order is
 * implementation-defined and does not portably match the wire/FPGA layout.
 *
 *   bit 0  TX/RX selection      : 0 = RX mode,      1 = TX mode
 *   bit 1  Linear/Circular       : 0 = Circular,     1 = Linear
 *   bit 2  Phase select (RX Lin) : 0 = 0 phase,      1 = 90 phase (+32 % 64)
 *   bit 3  Auto mode status      : 1 = busy,         0 = done   (READ-ONLY)
 *   bit 4  Set Beam              : 1 = Start,        0 = none   (trigger)
 */
#define TASA_FPGA_CTRL_BEAM_MODE_TX_RX_MASK 0x01u
#define TASA_FPGA_CTRL_BEAM_MODE_LIN_CIR_MASK 0x02u
#define TASA_FPGA_CTRL_BEAM_MODE_PHASE_MASK 0x04u
#define TASA_FPGA_CTRL_BEAM_MODE_AUTO_STAT_MASK 0x08u
#define TASA_FPGA_CTRL_BEAM_MODE_SET_BEAM_MASK 0x10u

/*
 * Set Beam (bit 4) trigger style. The FPGA spec does not document whether the
 * trigger is edge- or level-sensitive, and Self Clear = N means the bit is not
 * auto-cleared. Default (0) = level trigger: write bit 4 = 1 and leave it.
 * Define this to 1 for edge trigger: write 1, then write 0 to re-arm.
 */
#ifndef TASA_FPGA_CTRL_BEAM_SET_EDGE_TRIGGER
#define TASA_FPGA_CTRL_BEAM_SET_EDGE_TRIGGER 0
#endif

/*
 * Semantic beam-mode field values. Enum values match the raw bit values so a
 * field can be OR'd straight into its mask position (0 = clear, 1 = set).
 * TX/RX direction reuses the existing tasa_bfic_dir_t {RX = 0, TX = 1} from
 * tasa_bfic_mode.h rather than defining a new type.
 */

/** Beam mode bit 1: polarization selection. */
typedef enum {
    TASA_BEAM_CIRCULAR = 0,
    TASA_BEAM_LINEAR = 1,
} tasa_beam_polar_t;

/** Beam mode bit 2: RX-Linear phase selection. */
typedef enum {
    TASA_BEAM_PHASE_0 = 0,
    TASA_BEAM_PHASE_90 = 1,
} tasa_beam_phase_t;

/**
 * Read the 1-byte Beam mode register (0x0D) over MUX mode 0x2F (System block).
 *
 * Returns the raw register byte; decode fields with the BEAM_MODE bit masks.
 * This is the low-level accessor — for a full configure-and-trigger sequence
 * use tasa_fpga_ctrl_set_beam.
 *
 * @param dev  FPGA MUX link (same struct used by the passthrough path).
 * @param mode Output byte; receives the raw Beam mode register value.
 * @return     TASA_OK on success, negative tasa_status_t on error.
 */
tasa_status_t tasa_fpga_ctrl_read_beam_mode(tasa_fpga_dev_t* dev, uint8_t* mode);

/**
 * Write the 1-byte Beam mode register (0x0D) over MUX mode 0x2F (System block).
 *
 * Writes the raw register byte as given; the caller is responsible for
 * assembling the bits (typically read-modify-write via the BEAM_MODE masks)
 * so control bits are not clobbered. bit 3 (Auto mode status) is read-only on
 * the FPGA side; any value written to it is ignored.
 *
 * @param dev  FPGA MUX link (same struct used by the passthrough path).
 * @param mode Raw Beam mode register value to write.
 * @return     TASA_OK on success, negative tasa_status_t on error.
 */
tasa_status_t tasa_fpga_ctrl_write_beam_mode(tasa_fpga_dev_t* dev, uint8_t mode);

/**
 * Query the Auto mode status bit (0x0D bit 3) to see if beam setting finished.
 *
 * Reads the Beam mode register and decodes bit 3. Note the inverted logic:
 * bit 3 = 1 means busy, bit 3 = 0 means done, so *done is true when the bit
 * is clear. Useful for non-blocking callers that poll on their own; the
 * blocking tasa_fpga_ctrl_set_beam waits on this condition internally.
 *
 * @param dev  FPGA MUX link (same struct used by the passthrough path).
 * @param done Output flag; set to true when the FPGA reports done (bit 3 = 0).
 * @return     TASA_OK on success, negative tasa_status_t on error.
 */
tasa_status_t tasa_fpga_ctrl_beam_is_done(tasa_fpga_dev_t* dev, bool* done);

/**
 * Configure and trigger a beam, then block until the FPGA reports done.
 *
 * Performs a read-modify-write on 0x0D so unrelated bits are preserved: sets
 * the TX/RX (bit 0), Linear/Circular (bit 1) and Phase (bit 2) fields from the
 * arguments, then pulses Set Beam (bit 4). The trigger style follows
 * TASA_FPGA_CTRL_BEAM_SET_EDGE_TRIGGER (default level: write 1 and leave it).
 * Finally it polls Auto mode status (bit 3) against a real wall-clock deadline
 * (via dev->get_tick_ms), returning TASA_ERR_TIMEOUT if the FPGA never reports
 * done within `timeout_ms`. Unlike a loop-count bound, this is portable across
 * boards/SPI clock speeds and always terminates.
 *
 * @param dev        FPGA MUX link (same struct used by the passthrough path).
 * @param dir        TX/RX direction (tasa_bfic_dir_t: RX = 0, TX = 1).
 * @param polar      Polarization mode (Circular / Linear).
 * @param phase      RX-Linear phase selection (0 / 90).
 * @param timeout_ms Max wall-clock time to wait for done, in milliseconds.
 * @return           TASA_OK on success, TASA_ERR_TIMEOUT if done never asserts
 *                   within timeout_ms, or a negative tasa_status_t from the
 *                   underlying transfer.
 */
tasa_status_t tasa_fpga_ctrl_set_beam(tasa_fpga_dev_t* dev, tasa_bfic_dir_t dir, tasa_beam_polar_t polar,
                                      tasa_beam_phase_t phase, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
