/*
 * tasa_fpga_system.h — Ctrl-FPGA "System" register block (Register_Mode =
 * TASA_REG_MODE_SYSTEM, address 0x00-0x12 per "Register Table" sheet).
 *
 * Covers FPGA/HW identification, per-tile BFIC reset, and the high-level
 * "auto beam set" sequence (Pol ID + Beam ID + Beam mode), which lets the
 * FPGA sequence the low-level BFIC SPI writes itself instead of the MCU
 * driving f6222_* calls one register at a time.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tasa_fpga_link.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Register addresses within the System block (Register_Mode = 0x8) */
#define TASA_SYS_REG_FPGA_VER 0x00u     /* 4 bytes: Major, Minor, Patch, Pre-Release */
#define TASA_SYS_REG_DIPSWITCH 0x04u    /* 1 byte */
#define TASA_SYS_REG_POL_ID_ADDR 0x05u  /* 4 bytes, MSB first ([31:24]..[7:0]) */
#define TASA_SYS_REG_BEAM_ID_ADDR 0x09u /* 4 bytes, MSB first */
#define TASA_SYS_REG_BEAM_MODE 0x0Du    /* 1 byte */
#define TASA_SYS_REG_BFIC_RESET_A 0x0Eu /* 1 byte, default 0x1F */
#define TASA_SYS_REG_BFIC_RESET_B 0x0Fu
#define TASA_SYS_REG_BFIC_RESET_C 0x10u
#define TASA_SYS_REG_BFIC_RESET_D 0x11u
#define TASA_SYS_REG_CALIBRATION_ADDR 0x12u /* 1 byte */

/* One "set beam" burst spans Pol ID(4) + Beam ID(4) + Beam mode(1) = 9 bytes
 * starting at TASA_SYS_REG_POL_ID_ADDR (matches legacy "SET_BEAM_ALL"). */
#define TASA_SYS_BEAM_BURST_LEN 9u

typedef union {
    uint8_t raw[4];
    struct __attribute__((packed)) {
        uint8_t major;
        uint8_t minor;
        uint8_t patch;
        uint8_t pre_release;
    } bits;
} tasa_fpga_version_t;

typedef union {
    uint8_t raw;
    struct __attribute__((packed)) {
        uint8_t switch_pos : 4; /* Switch[3:0] */
        uint8_t hw_version : 4; /* HW Version[3:0] */
    } bits;
} tasa_dipswitch_t;

/*
 * Beam mode bitfield. Field order/names follow the datasheet-verified
 * legacy layout (CDP-Eureka-MCU BEAM_MODE_st); the "SPI Format"/"Register
 * Table" sheet's own polarity wording for bits 3-4 is self-contradictory
 * ("0x0: 1 is busy" / "0x0: 1 is Start") — treat those two as unconfirmed
 * until checked against real FPGA behavior.
 */
typedef union {
    uint8_t raw;
    struct __attribute__((packed)) {
        uint8_t tx_or_rx : 1;           /* 0 = RX, 1 = TX */
        uint8_t linear_or_circular : 1; /* 0 = Circular, 1 = Linear */
        uint8_t phase : 1;              /* RX Linear only: 0 = 0deg, 1 = 90deg */
        uint8_t auto_mode_status : 1;   /* busy/done, polarity unconfirmed (see above) */
        uint8_t start_set_beam : 1;     /* start trigger, polarity unconfirmed (see above) */
        uint8_t reserved : 3;
    } bits;
} tasa_beam_mode_t;

typedef struct __attribute__((packed)) {
    uint32_t pol_id_address;
    uint32_t beam_id_address;
    tasa_beam_mode_t beam_mode;
} tasa_beam_set_t;

typedef enum {
    TASA_BFIC_RESET_TILE_A = TASA_SYS_REG_BFIC_RESET_A,
    TASA_BFIC_RESET_TILE_B = TASA_SYS_REG_BFIC_RESET_B,
    TASA_BFIC_RESET_TILE_C = TASA_SYS_REG_BFIC_RESET_C,
    TASA_BFIC_RESET_TILE_D = TASA_SYS_REG_BFIC_RESET_D,
} tasa_bfic_reset_tile_t;

/* BFIC Reset A-D: bits[3:0] = RST1-RST4, bit4 = RST5 ("TX AiP Center" only).
 * Default 0x1F. Polarity (1 = held in reset vs. 1 = released) is not
 * stated in protocol spec v0.01 — confirm against FPGA/BFIC bring-up. */
#define TASA_BFIC_RESET_RST1 (1u << 0u)
#define TASA_BFIC_RESET_RST2 (1u << 1u)
#define TASA_BFIC_RESET_RST3 (1u << 2u)
#define TASA_BFIC_RESET_RST4 (1u << 3u)
#define TASA_BFIC_RESET_RST5 (1u << 4u)

tasa_status_t tasa_fpga_get_version(tasa_fpga_dev_t* dev, tasa_fpga_version_t* out);
tasa_status_t tasa_fpga_get_dipswitch(tasa_fpga_dev_t* dev, tasa_dipswitch_t* out);

/* Per-tile BFIC reset (see tasa_bfic_reset_tile_t and TASA_BFIC_RESET_RST*). */
tasa_status_t tasa_fpga_bfic_reset_write(tasa_fpga_dev_t* dev, tasa_bfic_reset_tile_t tile, uint8_t rst_bits);
tasa_status_t tasa_fpga_bfic_reset_read(tasa_fpga_dev_t* dev, tasa_bfic_reset_tile_t tile, uint8_t* rst_bits);

tasa_status_t tasa_fpga_get_calibration_addr(tasa_fpga_dev_t* dev, uint8_t* addr);
tasa_status_t tasa_fpga_set_calibration_addr(tasa_fpga_dev_t* dev, uint8_t addr);

/**
 * tasa_fpga_set_beam() — write Pol ID + Beam ID + Beam mode in one 9-byte
 * burst, letting the FPGA drive the actual BFIC SPI writes for the
 * selected beam/polarization. Does not wait for completion — poll
 * tasa_fpga_beam_is_busy() (with your own delay) before issuing another
 * beam-set request.
 */
tasa_status_t tasa_fpga_set_beam(tasa_fpga_dev_t* dev, const tasa_beam_set_t* beam);

/** tasa_fpga_get_beam() — read back the current Pol ID / Beam ID / Beam mode. */
tasa_status_t tasa_fpga_get_beam(tasa_fpga_dev_t* dev, tasa_beam_set_t* out);

/**
 * tasa_fpga_beam_is_busy() — read Beam mode and report auto_mode_status.
 * See the polarity caveat on tasa_beam_mode_t.bits.auto_mode_status.
 */
tasa_status_t tasa_fpga_beam_is_busy(tasa_fpga_dev_t* dev, bool* busy);

#ifdef __cplusplus
}
#endif
