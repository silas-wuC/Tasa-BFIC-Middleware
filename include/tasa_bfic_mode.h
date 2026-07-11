/*
 * tasa_bfic_mode.h — BFIC MUX mode definitions (GPIO 6-bit field, 0x00–0x2F).
 *
 * Each mode value drives 6 MUX-select GPIOs (one bit per pin) to route the
 * MCU SPI bus to the target BFIC device.  TX/RX share the same hex value;
 * BF9 entries (0x09, 0x12, 0x1B, 0x24) are TX-only per the mode table.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TASA_BFIC_MODE_AIP_BROADCAST = 0x00u,

    TASA_BFIC_MODE_AIP_A_BF1 = 0x01u,
    TASA_BFIC_MODE_AIP_A_BF2 = 0x02u,
    TASA_BFIC_MODE_AIP_A_BF3 = 0x03u,
    TASA_BFIC_MODE_AIP_A_BF4 = 0x04u,
    TASA_BFIC_MODE_AIP_A_BF5 = 0x05u,
    TASA_BFIC_MODE_AIP_A_BF6 = 0x06u,
    TASA_BFIC_MODE_AIP_A_BF7 = 0x07u,
    TASA_BFIC_MODE_AIP_A_BF8 = 0x08u,
    TASA_BFIC_MODE_AIP_A_BF9 = 0x09u,

    TASA_BFIC_MODE_AIP_B_BF1 = 0x0Au,
    TASA_BFIC_MODE_AIP_B_BF2 = 0x0Bu,
    TASA_BFIC_MODE_AIP_B_BF3 = 0x0Cu,
    TASA_BFIC_MODE_AIP_B_BF4 = 0x0Du,
    TASA_BFIC_MODE_AIP_B_BF5 = 0x0Eu,
    TASA_BFIC_MODE_AIP_B_BF6 = 0x0Fu,
    TASA_BFIC_MODE_AIP_B_BF7 = 0x10u,
    TASA_BFIC_MODE_AIP_B_BF8 = 0x11u,
    TASA_BFIC_MODE_AIP_B_BF9 = 0x12u,

    TASA_BFIC_MODE_AIP_C_BF1 = 0x13u,
    TASA_BFIC_MODE_AIP_C_BF2 = 0x14u,
    TASA_BFIC_MODE_AIP_C_BF3 = 0x15u,
    TASA_BFIC_MODE_AIP_C_BF4 = 0x16u,
    TASA_BFIC_MODE_AIP_C_BF5 = 0x17u,
    TASA_BFIC_MODE_AIP_C_BF6 = 0x18u,
    TASA_BFIC_MODE_AIP_C_BF7 = 0x19u,
    TASA_BFIC_MODE_AIP_C_BF8 = 0x1Au,
    TASA_BFIC_MODE_AIP_C_BF9 = 0x1Bu,

    TASA_BFIC_MODE_AIP_D_BF1 = 0x1Cu,
    TASA_BFIC_MODE_AIP_D_BF2 = 0x1Du,
    TASA_BFIC_MODE_AIP_D_BF3 = 0x1Eu,
    TASA_BFIC_MODE_AIP_D_BF4 = 0x1Fu,
    TASA_BFIC_MODE_AIP_D_BF5 = 0x20u,
    TASA_BFIC_MODE_AIP_D_BF6 = 0x21u,
    TASA_BFIC_MODE_AIP_D_BF7 = 0x22u,
    TASA_BFIC_MODE_AIP_D_BF8 = 0x23u,
    TASA_BFIC_MODE_AIP_D_BF9 = 0x24u,

    TASA_BFIC_MODE_AIP_BROADCAST_A = 0x25u,
    TASA_BFIC_MODE_AIP_BROADCAST_B = 0x26u,
    TASA_BFIC_MODE_AIP_BROADCAST_C = 0x27u,
    TASA_BFIC_MODE_AIP_BROADCAST_D = 0x28u,

    TASA_BFIC_MODE_FLASH_A = 0x29u,
    TASA_BFIC_MODE_FLASH_B = 0x2Au,
    TASA_BFIC_MODE_FLASH_C = 0x2Bu,
    TASA_BFIC_MODE_FLASH_D = 0x2Cu,
    TASA_BFIC_MODE_FLASH_BOOT = 0x2Du,

    TASA_BFIC_MODE_RESERVED = 0x2Eu,
    TASA_BFIC_MODE_FPGA_INTERNAL = 0x2Fu,
} tasa_bfic_mux_mode_t;

#define TASA_BFIC_MODE_MIN 0x00u
#define TASA_BFIC_MODE_MAX 0x2Fu
#define TASA_BFIC_MODE_MASK 0x3Fu

typedef enum {
    TASA_BFIC_TILE_A = 0,
    TASA_BFIC_TILE_B = 1,
    TASA_BFIC_TILE_C = 2,
    TASA_BFIC_TILE_D = 3,
} tasa_bfic_tile_t;

typedef enum {
    TASA_BFIC_DIR_RX = 0,
    TASA_BFIC_DIR_TX = 1,
} tasa_bfic_dir_t;

static inline uint8_t tasa_bfic_mode_raw(tasa_bfic_mux_mode_t mode) { return (uint8_t)mode; }

static inline bool tasa_bfic_mode_is_valid(tasa_bfic_mux_mode_t mode) { return (uint8_t)mode <= TASA_BFIC_MODE_MAX; }

static inline uint8_t tasa_bfic_mode_gpio_bits(tasa_bfic_mux_mode_t mode) {
    return (uint8_t)mode & TASA_BFIC_MODE_MASK;
}

static inline bool tasa_bfic_mode_is_aip(tasa_bfic_mux_mode_t mode) {
    uint8_t m = (uint8_t)mode;
    return m <= 0x24u || (m >= 0x25u && m <= 0x28u);
}

static inline bool tasa_bfic_mode_is_flash(tasa_bfic_mux_mode_t mode) {
    uint8_t m = (uint8_t)mode;
    return m >= 0x29u && m <= 0x2Du;
}

tasa_bfic_mux_mode_t tasa_bfic_mode_from_tile_bf(tasa_bfic_tile_t tile, uint8_t bf);
tasa_bfic_mux_mode_t tasa_bfic_mode_tile_broadcast(tasa_bfic_tile_t tile);
bool tasa_bfic_mode_valid_for_dir(tasa_bfic_mux_mode_t mode, tasa_bfic_dir_t dir);
bool tasa_bfic_mode_is_tx_only_bf9(tasa_bfic_mux_mode_t mode);
const char* tasa_bfic_mode_name(tasa_bfic_mux_mode_t mode);

#ifdef __cplusplus
}
#endif
