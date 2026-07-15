/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
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
#include "main.h"

#include "gpio.h"
#include "spi.h"
#include "usart.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "f6222.h"
#include "tasa_bfic_bridge.h"
#include "tasa_board_hal.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static tasa_fpga_dev_t fpga_link = {
    .gpio_set_mux = board_gpio_set_mux,
    .spi_xfer = board_spi_xfer,
    .ctx = &hspi1,
};
static tasa_bfic_bridge_t bfic_bridge;
static f6222_dev_t f6222_dev;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

int __io_putchar(int ch) {
    uint8_t c = (uint8_t)ch;
    HAL_UART_Transmit(&huart3, &c, 1, HAL_MAX_DELAY);
    return ch;
}

/*
 * Cool LED effect: software-PWM "flowing rainbow wave".
 *
 * No hardware timer/PWM channel is wired to the on-board LEDs, so brightness is
 * faked with a software PWM: within each ~1 ms PWM window every LED is turned on
 * for a fraction of the window proportional to its target brightness (0..255).
 *
 * Each LED rides a sine-shaped breathing curve, but the three curves are phase
 * shifted 120 deg apart, so the light appears to "flow" across GREEN -> YELLOW
 * -> RED and back. All timing is derived from HAL_GetTick(), so the loop never
 * blocks.
 */

/* 256-entry quarter-symmetric sine LUT, scaled 0..255 (breathing brightness). */
static const uint8_t kSineLut[256] = {
    128, 131, 134, 137, 140, 143, 146, 149, 152, 156, 159, 162, 165, 168, 171, 174, 176, 179, 182, 185, 188, 191,
    193, 196, 199, 201, 204, 206, 209, 211, 213, 216, 218, 220, 222, 224, 226, 228, 230, 232, 234, 236, 237, 239,
    240, 242, 243, 245, 246, 247, 248, 249, 250, 251, 252, 252, 253, 254, 254, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 254, 254, 253, 252, 252, 251, 250, 249, 248, 247, 246, 245, 243, 242, 240, 239, 237, 236,
    234, 232, 230, 228, 226, 224, 222, 220, 218, 216, 213, 211, 209, 206, 204, 201, 199, 196, 193, 191, 188, 185,
    182, 179, 176, 174, 171, 168, 165, 162, 159, 156, 152, 149, 146, 143, 140, 137, 134, 131, 128, 124, 121, 118,
    115, 112, 109, 106, 103, 99,  96,  93,  90,  87,  84,  81,  79,  76,  73,  70,  67,  64,  62,  59,  56,  54,
    51,  49,  46,  44,  42,  39,  37,  35,  33,  31,  29,  27,  25,  23,  21,  19,  18,  16,  15,  13,  12,  10,
    9,   8,   7,   6,   5,   4,   3,   3,   2,   1,   1,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    1,   1,   2,   3,   3,   4,   5,   6,   7,   8,   9,   10,  12,  13,  15,  16,  18,  19,  21,  23,  25,  27,
    29,  31,  33,  35,  37,  39,  42,  44,  46,  49,  51,  54,  56,  59,  62,  64,  67,  70,  73,  76,  79,  81,
    84,  87,  90,  93,  96,  99,  103, 106, 109, 112, 115, 118, 121, 124};

/* One software-PWM window in HAL ticks (1 tick = 1 ms). */
#define PWM_WINDOW_MS 8u
/* How fast the wave advances one LUT step (smaller = faster breathing). */
#define WAVE_STEP_MS 6u
/* 120 deg phase offset between the three LEDs (256 entries / 3). */
#define PHASE_OFFSET 85u

typedef struct {
    GPIO_TypeDef* port;
    uint16_t pin;
    uint8_t phase; /* starting offset into the sine LUT */
} led_channel_t;

static const led_channel_t kLeds[] = {
    {LED_GREEN_GPIO_Port, LED_GREEN_Pin, 0u * PHASE_OFFSET},
    {LED_YELLOW_GPIO_Port, LED_YELLOW_Pin, 1u * PHASE_OFFSET},
    {LED_RED_GPIO_Port, LED_RED_Pin, 2u * PHASE_OFFSET},
};
#define LED_COUNT (sizeof(kLeds) / sizeof(kLeds[0]))

/*
 * Render one software-PWM window for all LEDs, using each LED's current
 * brightness. Blocks for exactly PWM_WINDOW_MS to keep the duty cycle honest.
 */
static void LED_RenderWindow(const uint8_t brightness[LED_COUNT]) {
    uint32_t window_start = HAL_GetTick();
    uint32_t elapsed;

    do {
        elapsed = HAL_GetTick() - window_start;
        /* Position inside the window scaled to 0..255. */
        uint8_t slot = (uint8_t)((elapsed * 255u) / PWM_WINDOW_MS);

        for (uint32_t i = 0; i < LED_COUNT; i++) {
            GPIO_PinState state = (slot < brightness[i]) ? GPIO_PIN_SET : GPIO_PIN_RESET;
            HAL_GPIO_WritePin(kLeds[i].port, kLeds[i].pin, state);
        }
    } while (elapsed < PWM_WINDOW_MS);
}

/*
 * Advance the breathing wave and drive one PWM window. Call repeatedly from the
 * main loop; it is self-timed and never spins idle for long.
 */
static void LED_WaveTick(void) {
    static uint32_t last_step = 0;
    static uint8_t wave_pos = 0;
    uint8_t brightness[LED_COUNT];

    if ((HAL_GetTick() - last_step) >= WAVE_STEP_MS) {
        last_step = HAL_GetTick();
        wave_pos++; /* wraps naturally at 256 */
    }

    for (uint32_t i = 0; i < LED_COUNT; i++) {
        brightness[i] = kSineLut[(uint8_t)(wave_pos + kLeds[i].phase)];
    }

    LED_RenderWindow(brightness);
}

/* USER CODE END 0 */

/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void) {
    /* USER CODE BEGIN 1 */

    /* USER CODE END 1 */

    /* MPU Configuration--------------------------------------------------------*/
    MPU_Config();

    /* MCU Configuration--------------------------------------------------------*/

    /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
    HAL_Init();

    /* USER CODE BEGIN Init */

    /* USER CODE END Init */

    /* Configure the system clock */
    SystemClock_Config();

    /* USER CODE BEGIN SysInit */

    /* USER CODE END SysInit */

    /* Initialize all configured peripherals */
    MX_GPIO_Init();
    MX_SPI1_Init();
    MX_USART3_UART_Init();
    /* USER CODE BEGIN 2 */
    if (tasa_bfic_bridge_init(&bfic_bridge, &f6222_dev, &fpga_link, TASA_BFIC_MODE_FPGA_INTERNAL) != TASA_OK) {
        Error_Handler();
    }
    printf("tasa_bfic_bridge_init succ\r\n");
    if (f6222_init_global(&f6222_dev) != F6222_OK) {
        Error_Handler();
    }
    printf("f6222_init_global succ\r\n");
    /* USER CODE END 2 */

    /* Infinite loop */
    /* USER CODE BEGIN WHILE */
    while (1) {
        /* USER CODE END WHILE */

        /* USER CODE BEGIN 3 */
        LED_WaveTick();
    }
    /* USER CODE END 3 */
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /** Supply configuration update enable
     */
    HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

    /** Configure the main internal regulator output voltage
     */
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {
    }

    /** Initializes the RCC Oscillators according to the specified parameters
     * in the RCC_OscInitTypeDef structure.
     */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM = 4;
    RCC_OscInitStruct.PLL.PLLN = 9;
    RCC_OscInitStruct.PLL.PLLP = 2;
    RCC_OscInitStruct.PLL.PLLQ = 2;
    RCC_OscInitStruct.PLL.PLLR = 2;
    RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
    RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOMEDIUM;
    RCC_OscInitStruct.PLL.PLLFRACN = 3072;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    /** Initializes the CPU, AHB and APB buses clocks
     */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 |
                                  RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV1;
    RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK) {
        Error_Handler();
    }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/* MPU Configuration */

void MPU_Config(void) {
    MPU_Region_InitTypeDef MPU_InitStruct = {0};

    /* Disables the MPU */
    HAL_MPU_Disable();

    /** Initializes and configures the Region and the memory to be protected
     */
    MPU_InitStruct.Enable = MPU_REGION_ENABLE;
    MPU_InitStruct.Number = MPU_REGION_NUMBER0;
    MPU_InitStruct.BaseAddress = 0x0;
    MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
    MPU_InitStruct.SubRegionDisable = 0x87;
    MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
    MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
    MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
    MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
    MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
    MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

    HAL_MPU_ConfigRegion(&MPU_InitStruct);
    /* Enables the MPU */
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void) {
    /* USER CODE BEGIN Error_Handler_Debug */
    /* User can add his own implementation to report the HAL error return state */
    __disable_irq();
    while (1) {
    }
    /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
 * @brief  Reports the name of the source file and the source line number
 *         where the assert_param error has occurred.
 * @param  file: pointer to the source file name
 * @param  line: assert_param error line source number
 * @retval None
 */
void assert_failed(uint8_t* file, uint32_t line) {
    /* USER CODE BEGIN 6 */
    /* User can add his own implementation to report the file name and line number,
       ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
    /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
