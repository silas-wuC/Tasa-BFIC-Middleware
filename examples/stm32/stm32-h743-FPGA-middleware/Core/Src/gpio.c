/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file    gpio.c
 * @brief   This file provides code for the configuration
 *          of all used GPIO pins.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "gpio.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/*----------------------------------------------------------------------------*/
/* Configure GPIO                                                             */
/*----------------------------------------------------------------------------*/
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/** Configure pins
     PH0-OSC_IN (PH0)   ------> RCC_OSC_IN
     PH1-OSC_OUT (PH1)   ------> RCC_OSC_OUT
     PA13 (JTMS/SWDIO)   ------> DEBUG_JTMS-SWDIO
     PA14 (JTCK/SWCLK)   ------> DEBUG_JTCK-SWCLK
*/
void MX_GPIO_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* GPIO Ports Clock Enable */
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /*Configure GPIO pin Output Level */
    HAL_GPIO_WritePin(GPIOB, LED_GREEN_Pin | LED_RED_Pin, GPIO_PIN_RESET);

    /*Configure GPIO pin Output Level */
    HAL_GPIO_WritePin(
        GPIOE, MUX_SEL0_Pin | MUX_SEL1_Pin | MUX_SEL2_Pin | MUX_SEL3_Pin | MUX_SEL4_Pin | MUX_SEL5_Pin | LED_YELLOW_Pin,
        GPIO_PIN_RESET);

    /*Configure GPIO pin Output Level */
    HAL_GPIO_WritePin(GPIOD, SPI1_CSB_Pin | AIP1_4_Common6_PD2_Pin | AIP1_4_Common5_PD3_Pin | AIP1_4_Common4_PD4_Pin,
                      GPIO_PIN_RESET);

    /*Configure GPIO pin Output Level */
    HAL_GPIO_WritePin(GPIOC, AIP1_4_Common3_PC7_Pin | AIP1_4_Common2_PC8_Pin | AIP1_4_Common1_PC9_Pin, GPIO_PIN_RESET);

    /*Configure GPIO pins : LED_GREEN_Pin LED_RED_Pin */
    GPIO_InitStruct.Pin = LED_GREEN_Pin | LED_RED_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /*Configure GPIO pins : MUX_SEL0_Pin MUX_SEL1_Pin MUX_SEL2_Pin MUX_SEL3_Pin
                             MUX_SEL4_Pin MUX_SEL5_Pin LED_YELLOW_Pin */
    GPIO_InitStruct.Pin =
        MUX_SEL0_Pin | MUX_SEL1_Pin | MUX_SEL2_Pin | MUX_SEL3_Pin | MUX_SEL4_Pin | MUX_SEL5_Pin | LED_YELLOW_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

    /*Configure GPIO pins : SPI1_CSB_Pin AIP1_4_Common6_PD2_Pin AIP1_4_Common5_PD3_Pin AIP1_4_Common4_PD4_Pin */
    GPIO_InitStruct.Pin = SPI1_CSB_Pin | AIP1_4_Common6_PD2_Pin | AIP1_4_Common5_PD3_Pin | AIP1_4_Common4_PD4_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    /*Configure GPIO pins : AIP1_4_Common3_PC7_Pin AIP1_4_Common2_PC8_Pin AIP1_4_Common1_PC9_Pin */
    GPIO_InitStruct.Pin = AIP1_4_Common3_PC7_Pin | AIP1_4_Common2_PC8_Pin | AIP1_4_Common1_PC9_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

/* USER CODE BEGIN 2 */

/* USER CODE END 2 */
