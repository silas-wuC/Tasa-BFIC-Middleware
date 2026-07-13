# STM32H743ZIT6U 從零建立專案：CubeMX 定腳 + VSCode 開發

> 目標讀者：完全新手。
> 目標流程：**只用 STM32CubeMX 產生初始化程式碼與腳位定義**，之後所有開發（編輯、編譯、燒錄、除錯）**都在 VSCode 完成**。
> 參照範本：`RENESAS-F6222-Driver/examples/stm32/f6222-spi-demo`（已驗證可跑的 H743 範例）。

---

## 0. 為什麼是這個流程

CubeMX 的價值只有一個：**把腳位、時脈、外設參數，轉成一堆 `HAL_xxx_Init()` 初始化 C 程式碼**。手寫這些暫存器設定又臭又長又容易錯。

但 CubeMX 內建的 IDE（STM32CubeIDE）肥、慢、Eclipse-based。所以策略是：

- **CubeMX**：只當「腳位/外設 → 程式碼」的產生器。輸出成 **Makefile 專案**（不是 IDE 專案）。
- **VSCode + STM32 for VSCode 擴充**：吃那個 Makefile，負責 build / flash / debug。日常寫 code、改 code 全在這。

改腳位時回 CubeMX 重新產生；寫邏輯時待在 VSCode。兩邊靠 `.ioc` 檔和 Makefile 對接，互不打架（CubeMX 會保留 `USER CODE` 區塊）。

---

## 1. 前置安裝（一次性）

| 工具 | 用途 | 備註 |
|------|------|------|
| **STM32CubeMX** | 腳位/程式碼產生器 | 需 Java。獨立安裝，不需整包 CubeIDE |
| **STM32CubeCLT** | 命令列工具鏈（arm-none-eabi-gcc、OpenOCD、ST-Link GDB server、make） | 範本裝在 `/opt/st/stm32cubeclt_1.21.0`。這是 VSCode build/flash 的後端 |
| **VSCode** | 編輯器 | — |
| **VSCode 擴充：STM32 for VSCode** (`bmd.stm32-for-vscode`) | 讀 Makefile、一鍵 build/flash | 核心 |
| **VSCode 擴充：Cortex-Debug** (`marus25.cortex-debug`) | 晶片除錯（斷點、暫存器） | 除錯用 |
| **VSCode 擴充：clangd** 或 **C/C++** (`ms-vscode.cpptools`) | IntelliSense / 跳轉 | 範本兩者都留設定檔 |

CubeMX 和 CubeCLT 從 ST 官網下載（需註冊帳號）。CLT 是「CubeMX toolchain」的獨立版，比整包 CubeIDE 小很多。

> 確認 CLT 路徑：`ls /opt/st/stm32cubeclt_*` 。之後 VSCode 設定要用到 `.../GNU-tools-for-STM32/bin`。

---

## 2. CubeMX 建立專案

### 2.1 選晶片

1. 開 STM32CubeMX → **File → New Project**。
2. 右上搜尋框輸入完整料號 **`STM32H743ZIT6`**（你的 `STM32H743ZIT6U` 的 `U` 只是出貨/包裝變體，晶片 die、腳位、記憶體全同 → CubeMX 就選 `STM32H743ZITx`，封裝 **LQFP144**）。
3. 雙擊選中 → 進 **Pinout & Configuration** 畫面。

> 對照範本 `.ioc`：`Mcu.CPN=STM32H743ZIT6`、`Mcu.Package=LQFP144`。

### 2.2 時脈來源（RCC）

左側 **System Core → RCC**：

- **High Speed Clock (HSE)** → 選 **Crystal/Ceramic Resonator**（若你的板子有外部晶振）。
  - 這會把 `PH0/PH1` 設成 `RCC_OSC_IN` / `RCC_OSC_OUT`。
- 若板子沒晶振，HSE 選 `Disable`，改用內部 HSI（可跑但精度差；SPI/UART 一般夠用）。

> 範本用 HSE Crystal，但 Clock Configuration 沒拉倍頻，SYSCLK 停在 64 MHz（見 §2.5）。

### 2.3 除錯介面（SYS）—— 必做，否則燒一次就鎖死

左側 **System Core → SYS**：

- **Debug** → 選 **Serial Wire**（SWD）。
  - 設定 `PA13 = SWDIO`、`PA14 = SWCLK`。
- **Timebase Source** → 保留 **SysTick**。

> ⚠️ 若不開 SWD，第一次燒錄後 ST-Link 可能無法再連上晶片（要靠拉 BOOT0 或 connect-under-reset 救）。新手務必開 Serial Wire。

### 2.4 外設腳位（照你的板子接線）

以範本的 F6222 SPI demo 為例，設了這些。**你依自己硬體改**，操作方式一樣：

**SPI1（4-wire SPI 主控）**

左側 **Connectivity → SPI1**：

- **Mode** → `Full-Duplex Master`。
- 自動配腳：`PA5 = SPI1_SCK`、`PA6 = SPI1_MISO`、`PB5 = SPI1_MOSI`。
- Parameter Settings：
  - `Data Size` = 8 Bits
  - `Prescaler` 調到你要的鮑率（範本 `/128` → ≈1 MBit/s，因為 SYSCLK 只有 64 MHz）
  - CPOL/CPHA 依 slave 需求設（F6222 上升沿採樣 → Mode 0）

**USART3（printf / log 用）**

左側 **Connectivity → USART3** → Mode `Asynchronous` → `PD8 = TX`、`PD9 = RX`。

**GPIO（片選 CSB、RST、MODE、LED 等）**

直接在右邊晶片圖上點腳位 → 選 `GPIO_Output`（或 Input）。範本設了：

| 腳 | 用途（範例） | 備註 |
|----|------|------|
| PB0 | GPIO_Output | — |
| PB7 | GPIO_Output | — |
| PB14 | GPIO_Output | — |
| PD14 | GPIO_Output | 初始 `PinState = SET`（高電平開機）|

設 GPIO 時可在下方 **GPIO** 設定頁改：初始電平、上/下拉、輸出速度、**User Label**（給腳位取名，例如 `F6222_CSB`，產生的程式碼會用這名字當 `#define`，超好用）。

### 2.5 Clock Configuration（時脈樹）

上方分頁 **Clock Configuration**。

- 想跑滿速：把 HSE 餵進 PLL，拉 `SYSCLK` 到 400/480 MHz（H743 上限）。CubeMX 會自動算分頻/倍頻，紅色代表超規、黃色代表建議調整。
- 範本沒拉，維持 **64 MHz**（HSI 等級），對 SPI/UART demo 已足夠。**新手先別碰，能跑再優化。**

### 2.6 Project Manager —— 決定輸出成 VSCode 能吃的格式（最關鍵一步）

上方分頁 **Project Manager**：

**Project 頁**

| 欄位 | 設定 | 說明 |
|------|------|------|
| Project Name | 例如 `stm32-h743-app` | 會變成資料夾名 |
| Project Location | 你的工作目錄 | — |
| **Toolchain / IDE** | **`Makefile`** | ⭐ **重點**。不是選 STM32CubeIDE。選 Makefile 才能給 VSCode 擴充吃 |
| Linker Settings | 預設 Heap `0x200` / Stack `0x400` | 夠用 |

**Code Generator 頁**

- ✅ **Generate peripheral initialization as a pair of '.c/.h' files per peripheral**（可選，讓 spi.c/gpio.c 分檔，較整齊）
- ✅ **Keep User Code when re-generating**（⭐ 必勾，保住你在 `USER CODE` 區塊寫的東西）
- Copy only necessary library files（省空間）

### 2.7 產生程式碼

右上 **GENERATE CODE**。完成後資料夾長這樣（對照範本）：

```
stm32-h743-app/
├── stm32-h743-app.ioc        # CubeMX 專案檔，改腳位就開這個
├── Makefile                  # ⭐ CubeMX 產的，VSCode 擴充靠它
├── STM32H743XX_FLASH.ld      # linker script（記憶體佈局）
├── startup_stm32h743xx.s     # 啟動組語
├── Core/
│   ├── Inc/                  # main.h, stm32h7xx_hal_conf.h, *_it.h
│   └── Src/                  # main.c, stm32h7xx_it.c, hal_msp.c, system_*.c ...
└── Drivers/
    ├── STM32H7xx_HAL_Driver/ # HAL 函式庫原始碼
    └── CMSIS/                # ARM 核心定義
```

`main.c` 裡會有 `MX_GPIO_Init()`、`MX_SPI1_Init()`、`MX_USART3_UART_Init()`，以及 `/* USER CODE BEGIN */ ... /* USER CODE END */` 區塊——**你的程式寫在這些區塊裡**，重新產生時不會被蓋掉。

---

## 3. 接上 VSCode

### 3.1 開專案

VSCode **File → Open Folder** → 開剛產生的 `stm32-h743-app/`（含 `Makefile` 那層當 workspace root）。

裝好 §1 那三個擴充。首次開啟，「STM32 for VSCode」擴充會偵測到 Makefile，可能問你要不要產生設定檔，讓它產。

### 3.2 `.vscode/` 設定檔（照範本補齊）

擴充會自動生大部分。以下四個檔對照範本，重點是**工具鏈路徑要對到你機器的 CubeCLT**：

**`.vscode/settings.json`** —— 指向 CubeCLT 工具鏈

```json
{
    "stm32-for-vscode.openOCDPath": false,
    "cortex-debug.armToolchainPath": "/opt/st/stm32cubeclt_1.21.0/GNU-tools-for-STM32/bin"
}
```

> 版本號 `1.21.0` 換成你實際裝的。用 `ls /opt/st/` 查。

**`.vscode/tasks.json`** —— build / flash 任務（擴充指令包好了）

```json
{
    "version": "2.0.0",
    "tasks": [
        { "label": "Build STM",       "type": "process", "command": "${command:stm32-for-vscode.build}",      "options": {"cwd": "${workspaceRoot}"}, "group": {"kind": "build", "isDefault": true}, "problemMatcher": ["$gcc"] },
        { "label": "Build Clean STM",  "type": "process", "command": "${command:stm32-for-vscode.cleanBuild}", "options": {"cwd": "${workspaceRoot}"}, "group": {"kind": "build", "isDefault": true}, "problemMatcher": ["$gcc"] },
        { "label": "Flash STM",        "type": "process", "command": "${command:stm32-for-vscode.flash}",      "options": {"cwd": "${workspaceRoot}"}, "group": {"kind": "build", "isDefault": true}, "problemMatcher": ["$gcc"] }
    ]
}
```

**`.vscode/launch.json`** —— Cortex-Debug 除錯（用 OpenOCD）

```json
{
    "configurations": [
        {
            "showDevDebugOutput": "parsed",
            "cwd": "${workspaceRoot}",
            "executable": "./build/debug/.elf",
            "name": "Debug STM32",
            "request": "launch",
            "type": "cortex-debug",
            "servertype": "openocd",
            "preLaunchTask": "Build STM",
            "device": "STM32H743ZITx",
            "configFiles": ["openocd.cfg"]
        }
    ]
}
```

**`openocd.cfg`**（放專案根，不是 `.vscode/`）—— 告訴 OpenOCD 用哪個燒錄器和晶片

```tcl
# ST-Link 燒錄器
source [find interface/stlink.cfg]
# H7 目標
source [find target/stm32h7x.cfg]
```

**`.vscode/c_cpp_properties.json`** —— IntelliSense（C/C++ 擴充用）

```json
{
  "configurations": [
    {
      "name": "STM32",
      "includePath": ["${workspaceFolder}/Core/Inc/**", "${workspaceFolder}/Drivers/**"],
      "defines": ["STM32H743xx", "USE_HAL_DRIVER"],
      "compilerPath": "/opt/st/stm32cubeclt_1.21.0/GNU-tools-for-STM32/bin/arm-none-eabi-gcc",
      "cStandard": "c11",
      "intelliSenseMode": "linux-gcc-arm"
    }
  ],
  "version": 4
}
```

> 想要更準的跳轉/補全，用 **clangd** + `compile_commands.json`（見 §5）。

---

## 4. Build / Flash / Debug 三步驟

### Build

- 側邊欄 STM32 擴充圖示 → **Build**，或 `Ctrl+Shift+B` 選 `Build STM`。
- 擴充實際跑的是 CubeMX 產的 Makefile（`make -j`）。輸出 `.elf` / `.hex` / `.bin` 到 `build/`。
- 首次會看到 HAL 一堆檔在編，正常。結尾有 `--print-memory-usage` 印 FLASH/RAM 用量。

### Flash（燒錄）

- 板子用 ST-Link 接電腦（Nucleo 板內建，自製板接 SWD 4 線：SWDIO/SWCLK/GND/3V3）。
- STM32 擴充 → **Flash**。底層 = OpenOCD 透過 ST-Link 寫 FLASH。
- 成功會看到 `Programming Started` → `Verified OK`。

### Debug（除錯）

- 按 `F5`（跑 `launch.json` 的 `Debug STM32`）。
- 會先 build → 起 OpenOCD GDB server → 停在 `main()`。
- 可下斷點、看變數、看**外設暫存器**（Cortex-Debug 的 registers / peripherals 視窗）。

---

## 5. IntelliSense 加強（可選但強烈建議）：clangd + compile_commands.json

C/C++ 擴充對 HAL 巨集有時解析不完整。改用 **clangd** 體驗最好，但它需要 `compile_commands.json`（每個 .c 的確切編譯指令）。

產生方式（在專案根，需 `bear` 或 `compiledb`）：

```bash
# 方法 A：bear 包住 make
make clean && bear -- make -j

# 方法 B：compiledb
make clean && compiledb make -j
```

產出的 `compile_commands.json` 讓 clangd 精準知道每個檔的 include 路徑與巨集定義，跳轉/補全/錯誤標示全對。

> 範本專案根與 example 都放了 `compile_commands.json`，就是這用途。改腳位重新產生程式碼後，若加了新原始檔，重跑一次上面指令刷新即可。

---

## 6. 之後的開發節奏

```
要改腳位 / 加外設 ──► 開 .ioc（CubeMX）──► 改 ──► GENERATE CODE ──► 回 VSCode
                                                                     │
寫邏輯 / 改邏輯 ──────────────────────────► 在 USER CODE 區塊寫 ◄─────┘
                                                                     │
                                        VSCode: Build → Flash → Debug
```

**鐵則**

1. **只在 `/* USER CODE BEGIN */ ... /* USER CODE END */` 之間寫程式**，其餘讓 CubeMX 管。跨出去的改動，下次 GENERATE 會被吃掉。
2. 自己的驅動/中介層（如 F6222 driver）放獨立資料夾，在 CubeMX **Makefile 的 `C_SOURCES` / `C_INCLUDES`** 手動加，或用 STM32-for-VSCode 的 `STM32-for-VSCode.config.yaml` 的 `sourceFiles` / `includeDirectories` 加（見範本 repo 根目錄那份 yaml）。
3. `.ioc` 一定要進版控。它是腳位設定的唯一真相來源。
4. `build/` 加進 `.gitignore`，不進版控。

---

## 7. 對接本專案（Tasa-BFIC-Middleware / F6222 driver）

這份 STM32 專案是 host 端。要把 F6222 driver 掛進來：

- driver 的 SPI 底層在 `platform/stm32/src/f6222_spi_stm32.c`，它呼叫 HAL 的 `HAL_SPI_TransmitReceive()` 等。
- 把 CubeMX 的 `hspi1` handle 傳給 driver 的初始化，CSB/RST/MODE 用 CubeMX 設的 GPIO 腳（記得在 CubeMX 給它們 User Label）。
- 詳細接法見 [f6222-integration-guide.md](f6222-integration-guide.md)、SPI 概念見 [stm32-spi-primer.md](stm32-spi-primer.md)。

---

## 附錄：範本專案關鍵設定速查（H743 已驗證）

| 項目 | 值 |
|------|-----|
| MCU / 封裝 | STM32H743ZITx / LQFP144 |
| Toolchain | Makefile |
| Debug | Serial Wire（PA13/PA14）|
| SPI1 | Master, PA5=SCK / PA6=MISO / PB5=MOSI, 8-bit |
| USART3 | Async, PD8=TX / PD9=RX |
| GPIO Out | PB0, PB7, PB14, PD14(初始高) |
| Clock | HSE Crystal，SYSCLK 64 MHz（未拉 PLL）|
| CLT 路徑 | `/opt/st/stm32cubeclt_1.21.0/GNU-tools-for-STM32/bin` |
| 燒錄 | OpenOCD + ST-Link，`target/stm32h7x.cfg` |
