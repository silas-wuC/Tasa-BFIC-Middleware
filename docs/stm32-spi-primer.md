# STM32 SPI 結構掃盲(純新手向,別怕)

## 1. SPI 到底是個啥玩意兒

SPI = Serial Peripheral Interface,說白了就是芯片跟芯片之間嘮嗑用的同步串行總線。角色分倆:Master(主控,一般就是 MCU)和 Slave(從設備,比如 Flash、傳感器、還有你這邊的 FPGA)。

四根線,記住就行,別特麼記混了:

| 線 | 全名 | 幹啥的 |
|---|---|---|
| SCK | Serial Clock | Master 出時鐘,節奏它說了算 |
| MOSI | Master Out Slave In | Master→Slave 發數據 |
| MISO | Master In Slave Out | Slave→Master 回數據 |
| NSS/CS | Chip Select | 選中哪個 Slave(基本都是低電平有效) |

一個 Master 底下能掛一堆 Slave,靠不同的 CS 引腳來回切——這就是 `gpio_set_mux` 幹的活兒(拿 GPIO 去選 MUX 通道,決定這回 SPI 傳輸走哪個設備/哪個模式)。

## 2. STM32 硬件上咋實現的

STM32 芯片裡頭自帶 SPI 外設(SPI1、SPI2、SPI3……),每個都是一坨寄存器(register),地址映射(memory-mapped)到某塊內存區。核心的幾個寄存器,搞不清楚別瞎猜:

- `CR1`/`CR2`:控制寄存器,設 Master/Slave、數據寬度(8bit/16bit)、時鐘極性(CPOL)/相位(CPHA)、波特率分頻(baud rate prescaler)、先發哪位(MSB/LSB)
- `SR`:狀態寄存器,查 TXE(發送緩衝空了)、RXNE(接收緩衝來貨了)、BSY(正忙著呢)
- `DR`:數據寄存器,寫進去=發出去,讀出來=收到的

直接懟寄存器那叫一個磨磨唧唧,所以 ST 官方給你包了一層 **HAL 庫**,省心,別再手擼寄存器把自己搞崩潰。

## 3. HAL 的 SPI 結構 `SPI_HandleTypeDef`

這就是你在 STM32 CubeMX/HAL 項目裡天天見的核心結構,長這德行(概念版):

```c
typedef struct {
    SPI_TypeDef *Instance;       // 指向哪個 SPI 外設 (SPI1/SPI2/SPI3 的寄存器基地址)
    SPI_InitTypeDef Init;        // 初始化參數
    // 裡頭還有 DMA handle、狀態機、lock 啥的,先別管
} SPI_HandleTypeDef;
```

`Init` 裡頭幾個要緊的字段,別看漏了哪個就等著抓瞎:

| 字段 | 啥意思 | 一般咋選 |
|---|---|---|
| `Mode` | Master 還是 Slave | `SPI_MODE_MASTER` |
| `Direction` | 全雙工/半雙工/只收/只發 | `SPI_DIRECTION_2LINES`(全雙工) |
| `DataSize` | 一次傳幾位 | `SPI_DATASIZE_8BIT` |
| `CLKPolarity` | 空閒時 SCK 是高還是低 (CPOL) | 看 Slave 規格書 |
| `CLKPhase` | 第一還是第二個邊沿採樣 (CPHA) | 看 Slave 規格書 |
| `NSS` | CS 讓硬件管還是軟件(GPIO)自己拉 | 一般用 `SPI_NSS_SOFT`,自己拉 GPIO |
| `BaudRatePrescaler` | 分頻,決定 SCK 頻率 | 看 Slave 能扛多快 |
| `FirstBit` | 先發 MSB 還是 LSB | 絕大多數 `SPI_FIRSTBIT_MSB` |

CPOL/CPHA 倆湊一塊兒就是你常聽說的 **SPI Mode 0~3**,兩邊(Master 跟 Slave 芯片)得對齊,對不上就收一堆亂碼,到時候debug到懷疑人生別怪沒提醒你。

## 4. 咋用(CubeMX 生成版,最常見)

1. **CubeMX 裡勾上** 某個 SPI 外設 → 選 Mode(Full-Duplex Master)→ 設 CPOL/CPHA/BaudRate → Generate Code。
2. CubeMX 自動給你生:
   - GPIO 初始化(把 SCK/MOSI/MISO 引腳切到 AF 模式 = Alternate Function,讓引腳歸 SPI 外設管,別當普通 GPIO 使,這步搞錯了等著抓瞎)
   - `MX_SPI1_Init()`:填好 `SPI_HandleTypeDef hspi1` 再調 `HAL_SPI_Init(&hspi1)`
3. **CS 引腳自己拉**(要是用了 `SPI_NSS_SOFT`):
   ```c
   HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_RESET); // 選中 Slave
   HAL_SPI_TransmitReceive(&hspi1, tx_buf, rx_buf, len, timeout_ms);
   HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_SET);   // 鬆開
   ```
4. 常用 API:
   - `HAL_SPI_Transmit(&hspi1, tx, len, timeout)` 只發
   - `HAL_SPI_Receive(&hspi1, rx, len, timeout)` 只收
   - `HAL_SPI_TransmitReceive(&hspi1, tx, rx, len, timeout)` 全雙工邊發邊收(SPI 硬件本質上就是收發同時進行)

## 5. 對上 Tasa-BFIC-Middleware 的抽象層

`tasa_fpga_link.c` 就是一層**硬件抽象**(HAL abstraction),它壓根不碰 STM32 寄存器,也不碰 `SPI_HandleTypeDef`,而是拿 `dev->gpio_set_mux` 和 `dev->spi_xfer` 倆函數指針,把上層邏輯跟具體平台徹底解耦,牛逼就牛逼在這兒。

真正的 STM32 實現(driver 層)大概長這樣:

```c
static int stm32_spi_xfer(void* ctx, const uint8_t* tx, uint8_t* rx, size_t len) {
    SPI_HandleTypeDef* hspi = (SPI_HandleTypeDef*)ctx;
    HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_RESET);
    HAL_StatusTypeDef st = HAL_SPI_TransmitReceive(hspi, (uint8_t*)tx, rx, len, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_SET);
    return (st == HAL_OK) ? 0 : -1;
}
```

然後把這函數指針塞進 `tasa_fpga_dev_t.spi_xfer`,`gpio_set_mux` 一個道理,接到 `HAL_GPIO_WritePin` 上。這麼一搞,中間層(middleware)根本不用操心自己跑在 STM32 上還是別的平台上,別瞎折騰去改中間層的代碼。
