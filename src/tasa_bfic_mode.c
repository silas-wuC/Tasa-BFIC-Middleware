#include "tasa_bfic_mode.h"

static const uint8_t k_tile_bf_base[4] = {0x01u, 0x0Au, 0x13u, 0x1Cu};
static const uint8_t k_tile_broadcast[4] = {0x25u, 0x26u, 0x27u, 0x28u};

static bool tile_is_valid(tasa_bfic_tile_t tile) { return tile <= TASA_BFIC_TILE_D; }

tasa_bfic_mux_mode_t tasa_bfic_mode_from_tile_bf(tasa_bfic_tile_t tile, uint8_t bf) {
    if (!tile_is_valid(tile)) {
        return TASA_BFIC_MODE_RESERVED;
    }

    if (bf == 0u) {
        return tasa_bfic_mode_tile_broadcast(tile);
    }

    if (bf >= 1u && bf <= 8u) {
        return (tasa_bfic_mux_mode_t)(k_tile_bf_base[tile] + (bf - 1u));
    }

    if (bf == 9u) {
        return (tasa_bfic_mux_mode_t)(k_tile_bf_base[tile] + 8u);
    }

    return TASA_BFIC_MODE_RESERVED;
}

tasa_bfic_mux_mode_t tasa_bfic_mode_tile_broadcast(tasa_bfic_tile_t tile) {
    if (!tile_is_valid(tile)) {
        return TASA_BFIC_MODE_RESERVED;
    }

    return (tasa_bfic_mux_mode_t)k_tile_broadcast[tile];
}

bool tasa_bfic_mode_is_tx_only_bf9(tasa_bfic_mux_mode_t mode) {
    switch (mode) {
        case TASA_BFIC_MODE_AIP_A_BF9:
        case TASA_BFIC_MODE_AIP_B_BF9:
        case TASA_BFIC_MODE_AIP_C_BF9:
        case TASA_BFIC_MODE_AIP_D_BF9:
            return true;
        default:
            return false;
    }
}

bool tasa_bfic_mode_valid_for_dir(tasa_bfic_mux_mode_t mode, tasa_bfic_dir_t dir) {
    if (!tasa_bfic_mode_is_valid(mode) || mode == TASA_BFIC_MODE_RESERVED) {
        return false;
    }

    if (dir == TASA_BFIC_DIR_TX) {
        return mode != TASA_BFIC_MODE_FPGA_INTERNAL;
    }

    if (mode == TASA_BFIC_MODE_FPGA_INTERNAL) {
        return false;
    }

    return !tasa_bfic_mode_is_tx_only_bf9(mode);
}

const char* tasa_bfic_mode_name(tasa_bfic_mux_mode_t mode) {
    switch (mode) {
        case TASA_BFIC_MODE_AIP_BROADCAST:
            return "AiP Broadcast";
        case TASA_BFIC_MODE_AIP_A_BF1:
            return "AiP A BF1";
        case TASA_BFIC_MODE_AIP_A_BF2:
            return "AiP A BF2";
        case TASA_BFIC_MODE_AIP_A_BF3:
            return "AiP A BF3";
        case TASA_BFIC_MODE_AIP_A_BF4:
            return "AiP A BF4";
        case TASA_BFIC_MODE_AIP_A_BF5:
            return "AiP A BF5";
        case TASA_BFIC_MODE_AIP_A_BF6:
            return "AiP A BF6";
        case TASA_BFIC_MODE_AIP_A_BF7:
            return "AiP A BF7";
        case TASA_BFIC_MODE_AIP_A_BF8:
            return "AiP A BF8";
        case TASA_BFIC_MODE_AIP_A_BF9:
            return "AiP A BF9";
        case TASA_BFIC_MODE_AIP_B_BF1:
            return "AiP B BF1";
        case TASA_BFIC_MODE_AIP_B_BF2:
            return "AiP B BF2";
        case TASA_BFIC_MODE_AIP_B_BF3:
            return "AiP B BF3";
        case TASA_BFIC_MODE_AIP_B_BF4:
            return "AiP B BF4";
        case TASA_BFIC_MODE_AIP_B_BF5:
            return "AiP B BF5";
        case TASA_BFIC_MODE_AIP_B_BF6:
            return "AiP B BF6";
        case TASA_BFIC_MODE_AIP_B_BF7:
            return "AiP B BF7";
        case TASA_BFIC_MODE_AIP_B_BF8:
            return "AiP B BF8";
        case TASA_BFIC_MODE_AIP_B_BF9:
            return "AiP B BF9";
        case TASA_BFIC_MODE_AIP_C_BF1:
            return "AiP C BF1";
        case TASA_BFIC_MODE_AIP_C_BF2:
            return "AiP C BF2";
        case TASA_BFIC_MODE_AIP_C_BF3:
            return "AiP C BF3";
        case TASA_BFIC_MODE_AIP_C_BF4:
            return "AiP C BF4";
        case TASA_BFIC_MODE_AIP_C_BF5:
            return "AiP C BF5";
        case TASA_BFIC_MODE_AIP_C_BF6:
            return "AiP C BF6";
        case TASA_BFIC_MODE_AIP_C_BF7:
            return "AiP C BF7";
        case TASA_BFIC_MODE_AIP_C_BF8:
            return "AiP C BF8";
        case TASA_BFIC_MODE_AIP_C_BF9:
            return "AiP C BF9";
        case TASA_BFIC_MODE_AIP_D_BF1:
            return "AiP D BF1";
        case TASA_BFIC_MODE_AIP_D_BF2:
            return "AiP D BF2";
        case TASA_BFIC_MODE_AIP_D_BF3:
            return "AiP D BF3";
        case TASA_BFIC_MODE_AIP_D_BF4:
            return "AiP D BF4";
        case TASA_BFIC_MODE_AIP_D_BF5:
            return "AiP D BF5";
        case TASA_BFIC_MODE_AIP_D_BF6:
            return "AiP D BF6";
        case TASA_BFIC_MODE_AIP_D_BF7:
            return "AiP D BF7";
        case TASA_BFIC_MODE_AIP_D_BF8:
            return "AiP D BF8";
        case TASA_BFIC_MODE_AIP_D_BF9:
            return "AiP D BF9";
        case TASA_BFIC_MODE_AIP_BROADCAST_A:
            return "AiP Broadcast A";
        case TASA_BFIC_MODE_AIP_BROADCAST_B:
            return "AiP Broadcast B";
        case TASA_BFIC_MODE_AIP_BROADCAST_C:
            return "AiP Broadcast C";
        case TASA_BFIC_MODE_AIP_BROADCAST_D:
            return "AiP Broadcast D";
        case TASA_BFIC_MODE_FLASH_A:
            return "Flash A";
        case TASA_BFIC_MODE_FLASH_B:
            return "Flash B";
        case TASA_BFIC_MODE_FLASH_C:
            return "Flash C";
        case TASA_BFIC_MODE_FLASH_D:
            return "Flash D";
        case TASA_BFIC_MODE_FLASH_BOOT:
            return "Flash Boot";
        case TASA_BFIC_MODE_RESERVED:
            return "Reserved";
        case TASA_BFIC_MODE_FPGA_INTERNAL:
            return "Internal FPGA Control";
        default:
            return "?";
    }
}
