# STM32 SPI 結構科普（完全新手向）

## 1. SPI 本質是什麼

SPI = Serial Peripheral Interface，晶片對晶片的同步序列匯流排。角色分 Master（主控，通常是 MCU）和 Slave（從屬，如 Flash、感測器、你這邊的 FPGA）。

四條線：

| 線 | 全名 | 作用 |
|---|---|---|
| SCK | Serial Clock | Master 出時脈，決定傳輸節奏 |
| MOSI | Master Out Slave In | Master→Slave 資料 |
| MISO | Master In Slave Out | Slave→Master 資料 |
| NSS/CS | Chip Select | 選中哪個 Slave（低電平有效居多） |

一個 Master 可接多個 Slave，靠不同 CS 腳位切換——這就是 `gpio_set_mux` 在幹的事（用 GPIO 選 MUX 通道，決定這次 SPI 傳輸走哪個裝置/模式）。

## 2. STM32 硬體上怎麼實現

STM32 晶片內建 SPI 外設（SPI1、SPI2、SPI3…），每個是一組暫存器（register），位址映射（memory-mapped）到某個記憶體區塊。核心暫存器：

- `CR1`/`CR2`：控制暫存器，設 Master/Slave、資料寬度（8bit/16bit）、時脈極性(CPOL)/相位(CPHA)、鮑率分頻(baud rate prescaler)、First bit（MSB/LSB）
- `SR`：狀態暫存器，查 TXE（發送緩衝空）、RXNE（接收緩衝有資料）、BSY（忙碌中）
- `DR`：資料暫存器，寫入 = 發送，讀取 = 接收

直接操作暫存器很繁瑣，所以 ST 官方提供 **HAL 函式庫**幫你包一層。

## 3. HAL 的 SPI 結構 `SPI_HandleTypeDef`

這是你在 STM32 CubeMX/HAL 專案裡會看到的核心結構，長這樣（概念版）：

```c
typedef struct {
    SPI_TypeDef *Instance;       // 指向哪個 SPI 外設 (SPI1/SPI2/SPI3 的暫存器基底位址)
    SPI_InitTypeDef Init;        // 初始化參數
    // 內部還有 DMA handle、狀態機、lock 等，先不用管
} SPI_HandleTypeDef;
```

`Init` 裡面重要欄位：

| 欄位 | 意義 | 常見選擇 |
|---|---|---|
| `Mode` | Master 或 Slave | `SPI_MODE_MASTER` |
| `Direction` | 全雙工/半雙工/只收/只發 | `SPI_DIRECTION_2LINES`（全雙工） |
| `DataSize` | 每次傳幾 bit | `SPI_DATASIZE_8BIT` |
| `CLKPolarity` | 空閒時 SCK 是高還低 (CPOL) | 依 Slave 規格 |
| `CLKPhase` | 第一還第二個邊緣取樣 (CPHA) | 依 Slave 規格 |
| `NSS` | CS 由硬體管還是軟體(GPIO)手動拉 | 一般用 `SPI_NSS_SOFT`，自己拉 GPIO |
| `BaudRatePrescaler` | 分頻，決定 SCK 頻率 | 依 Slave 能承受的最高速 |
| `FirstBit` | 先傳 MSB 還 LSB | `SPI_FIRSTBIT_MSB` 居多 |

CPOL/CPHA 合起來就是常聽到的 **SPI Mode 0~3**，兩邊（Master 跟 Slave 晶片）設定要對齊，不對就收不到正確資料。

## 4. 使用流程（CubeMX 產生版，最常見）

1. **CubeMX 勾選** 某個 SPI 外設 → 選 Mode（Full-Duplex Master）→ 設 CPOL/CPHA/BaudRate → Generate Code。
2. CubeMX 自動生成：
   - GPIO 初始化（把 SCK/MOSI/MISO 腳位切到 AF 模式 = Alternate Function，讓腳位交給 SPI 外設控制而非當一般 GPIO）
   - `MX_SPI1_Init()`：填好 `SPI_HandleTypeDef hspi1` 並呼叫 `HAL_SPI_Init(&hspi1)`
3. **CS 腳位自己拉**（若用 `SPI_NSS_SOFT`）：
   ```c
   HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_RESET); // 選中 Slave
   HAL_SPI_TransmitReceive(&hspi1, tx_buf, rx_buf, len, timeout_ms);
   HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_SET);   // 取消選中
   ```
4. 常用 API：
   - `HAL_SPI_Transmit(&hspi1, tx, len, timeout)` 只發
   - `HAL_SPI_Receive(&hspi1, rx, len, timeout)` 只收
   - `HAL_SPI_TransmitReceive(&hspi1, tx, rx, len, timeout)` 全雙工同時收發（SPI 硬體本質就是邊發邊收）

## 5. 對照 Tasa-BFIC-Middleware 的抽象層

`tasa_fpga_link.c` 是一層**硬體抽象**（HAL abstraction），沒有直接碰 STM32 暫存器或 `SPI_HandleTypeDef`，而是透過 `dev->gpio_set_mux` 和 `dev->spi_xfer` 兩個 function pointer 讓上層邏輯跟平台脫鉤。

真正的 STM32 實作（driver 層）大概長這樣：

```c
static int stm32_spi_xfer(void* ctx, const uint8_t* tx, uint8_t* rx, size_t len) {
    SPI_HandleTypeDef* hspi = (SPI_HandleTypeDef*)ctx;
    HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_RESET);
    HAL_StatusTypeDef st = HAL_SPI_TransmitReceive(hspi, (uint8_t*)tx, rx, len, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_SET);
    return (st == HAL_OK) ? 0 : -1;
}
```

然後把這函式指標塞進 `tasa_fpga_dev_t.spi_xfer`，`gpio_set_mux` 同理接到 `HAL_GPIO_WritePin`。這樣中介層（middleware）不用知道自己跑在 STM32 還是別的平台。
