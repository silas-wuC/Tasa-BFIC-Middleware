#include "tasa_board_hal.h"

#include "main.h"
#include "spi.h"

#define BOARD_SPI_TIMEOUT_MS 100u

int board_gpio_set_mux(void* ctx, uint8_t mode_bits) {
    (void)ctx;

    /* PE7–PE12 map to MUX bit0–bit5; atomic BSRR write avoids read-modify-write races. */
    GPIOE->BSRR = ((uint32_t)(~mode_bits & 0x3Fu) << (7 + 16)) | ((uint32_t)(mode_bits & 0x3Fu) << 7);
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
