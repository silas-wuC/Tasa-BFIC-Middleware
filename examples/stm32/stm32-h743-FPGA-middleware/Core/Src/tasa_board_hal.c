#include "tasa_board_hal.h"

#include "main.h"
#include "spi.h"

#define BOARD_SPI_TIMEOUT_MS 100u

int board_gpio_set_mux(void* ctx, uint8_t mode_bits) {
    (void)ctx;

    /* PE7–PE12 map to MUX bit0–bit5; one HAL call per pin for readability. */
    HAL_GPIO_WritePin(MUX_SEL0_GPIO_Port, MUX_SEL0_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(MUX_SEL1_GPIO_Port, MUX_SEL1_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(MUX_SEL2_GPIO_Port, MUX_SEL2_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(MUX_SEL3_GPIO_Port, MUX_SEL3_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(MUX_SEL4_GPIO_Port, MUX_SEL4_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(MUX_SEL5_GPIO_Port, MUX_SEL5_Pin, GPIO_PIN_SET);
    // HAL_GPIO_WritePin(MUX_SEL0_GPIO_Port, MUX_SEL0_Pin, (mode_bits & 0x01u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    // HAL_GPIO_WritePin(MUX_SEL1_GPIO_Port, MUX_SEL1_Pin, (mode_bits & 0x02u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    // HAL_GPIO_WritePin(MUX_SEL2_GPIO_Port, MUX_SEL2_Pin, (mode_bits & 0x04u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    // HAL_GPIO_WritePin(MUX_SEL3_GPIO_Port, MUX_SEL3_Pin, (mode_bits & 0x08u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    // HAL_GPIO_WritePin(MUX_SEL4_GPIO_Port, MUX_SEL4_Pin, (mode_bits & 0x10u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    // HAL_GPIO_WritePin(MUX_SEL5_GPIO_Port, MUX_SEL5_Pin, (mode_bits & 0x20u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    return 0;
}

int board_spi_xfer(void* ctx, const uint8_t* tx, uint8_t* rx, size_t len) {
    SPI_HandleTypeDef* hspi = (SPI_HandleTypeDef*)ctx;
    HAL_StatusTypeDef status;

    if (hspi == NULL || tx == NULL || len == 0U) {
        return -1;
    }

    HAL_GPIO_WritePin(SPI1_CSB_GPIO_Port, SPI1_CSB_Pin, GPIO_PIN_RESET);

    if (rx != NULL) {
        status = HAL_SPI_TransmitReceive(hspi, (uint8_t*)tx, rx, len, BOARD_SPI_TIMEOUT_MS);
    } else {
        status = HAL_SPI_Transmit(hspi, (uint8_t*)tx, len, BOARD_SPI_TIMEOUT_MS);
    }

    HAL_GPIO_WritePin(SPI1_CSB_GPIO_Port, SPI1_CSB_Pin, GPIO_PIN_SET);

    return (status == HAL_OK) ? 0 : -1;
}
