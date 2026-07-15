# STM32H743ZIT6U 從零建立專案：CubeMX 定腳 + VSCode 開發

> 目標讀者:完全新手,看不懂算我輸。
> 目標流程:**只用 STM32CubeMX 產生初始化程式碼與腳位定義**,之後所有開發(編輯、編譯、燒錄、除錯)**都在 VSCode 完成**,別特麼再兩頭跑。
> 參照範本:`RENESAS-F6222-Driver/examples/stm32/f6222-spi-demo`(已驗證可跑的 H743 範例,照著抄準沒錯)。

---

## 0. 為什麼是這個流程

CubeMX 的價值只有一個:**把腳位、時脈、外設參數,轉成一堆 `HAL_xxx_Init()` 初始化 C 程式碼**。手寫這些暫存器設定又臭又長又容易錯,純屬自虐,能讓工具幹的活兒別自己上手。

但 CubeMX 內建的 IDE(STM32CubeIDE)肥、慢、Eclipse-based,用起來膈應人。所以策略是:

- **CubeMX**:只當「腳位/外設 → 程式碼」的產生器。輸出成 **Makefile 專案**(不是 IDE 專案)。
- **VSCode + STM32 for VSCode 擴充**:吃那個 Makefile,負責 build / flash / debug。日常寫 code、改 code 全在這,別瞎折騰別的工具。

改腳位時回 CubeMX 重新產生;寫邏輯時待在 VSCode。兩邊靠 `.ioc` 檔和 Makefile 對接,互不打架(CubeMX 會保留 `USER CODE` 區塊,這點做得還算厚道)。

---

## 1. 前置安裝(一次性,裝完別再問我為啥又要裝)

| 工具 | 用途 | 備註 |
|------|------|------|
| **STM32CubeMX** | 腳位/程式碼產生器 | 需 Java。獨立安裝，不需整包 CubeIDE |
| **STM32CubeCLT** | 命令列工具鏈（arm-none-eabi-gcc、OpenOCD、ST-Link GDB server、make） | 範本裝在 `/opt/st/stm32cubeclt_1.21.0`。這是 VSCode build/flash 的後端 |
| **VSCode** | 編輯器 | — |
| **VSCode 擴充：STM32 for VSCode** (`bmd.stm32-for-vscode`) | 讀 Makefile、一鍵 build/flash | 核心 |
| **VSCode 擴充：Cortex-Debug** (`marus25.cortex-debug`) | 晶片除錯（斷點、暫存器） | 除錯用 |
| **VSCode 擴充：clangd** 或 **C/C++** (`ms-vscode.cpptools`) | IntelliSense / 跳轉 | 範本兩者都留設定檔 |

CubeMX 和 CubeCLT 從 ST 官網下載(需註冊帳號,麻煩但躲不掉)。CLT 是「CubeMX toolchain」的獨立版,比整包 CubeIDE 小很多,別傻乎乎裝了整包肥雞。

> 確認 CLT 路徑:`ls /opt/st/stm32cubeclt_*`。之後 VSCode 設定要用到 `.../GNU-tools-for-STM32/bin`,記好了別到時候路徑填錯乾瞪眼。

---

## 2. CubeMX 建立專案

### 2.1 選晶片

1. 開 STM32CubeMX → **File → New Project**。
2. 右上搜尋框輸入 **`STM32H743ZIT`**。列表通常跳出**兩列**,別瞎選:

   | 料號 | 差別 | 選？ |
   |------|------|------|
   | `STM32H743ZIT6` | Tray 出貨 | ✅ 選這個 |
   | `STM32H743ZIT6TR` | Tape & Reel(打帶捲盤,量產貼片機用) | ❌ |

   兩者晶片 die、腳位、記憶體、Reference(都是 `STM32H743ZITx`)**完全相同**,差別只在出貨包裝,別自己嚇自己以為選錯要出大事。CubeMX 只認 Reference,選哪個產出程式碼一模一樣。你的 `STM32H743ZIT6U`(`U` 也只是包裝/等級變體)同屬這顆,選第一列 `STM32H743ZIT6` 即可。

3. 選中 `STM32H743ZIT6`(封裝 **LQFP144**)→ 按 **Start Project** → 進 **Pinout & Configuration** 畫面。

> 對照範本 `.ioc`:`Mcu.CPN=STM32H743ZIT6`、`Mcu.Package=LQFP144`、`Mcu.Name=STM32H743ZITx`。

### 2.2 時脈來源(RCC)

左側 **System Core → RCC**:

- **High Speed Clock (HSE)** → 選 **Crystal/Ceramic Resonator**(若你的板子有外部晶振)。
  - 這會把 `PH0/PH1` 設成 `RCC_OSC_IN` / `RCC_OSC_OUT`。
- 若板子沒晶振,HSE 選 `Disable`,改用內部 HSI(可跑但精度差;SPI/UART 一般夠用,別要求太高)。

> 範本用 HSE Crystal,但 Clock Configuration 沒拉倍頻,SYSCLK 停在 64 MHz(見 §2.5)。

### 2.3 除錯介面(Debug/SWD)—— 這步必做,不做等著鎖死晶片哭去吧

> ⚠️ CubeMX **6.17** 起,Debug 從 SYS 移到獨立分類 **`Trace and Debug`**(舊版教學寫在 SYS → Debug,新版找不到很正常,別自己瞎找瞎懷疑人生)。

1. 左側 **Trace and Debug** 分類 → 展開 → 點 **DEBUG**。
2. **Mode** 下拉 → 選 **`Serial Wire`**(SWD)。
3. 自動配腳 `PA13 = SWDIO`、`PA14 = SWCLK`。

**Timebase Source** 仍在 **System Core → SYS**,保留 **SysTick** 即可(該頁會顯示 `Warning: This peripheral has no parameters to be configured`,別慌,正常現象)。

> ⚠️ 若不開 SWD,第一次燒錄後 ST-Link 可能再也連不上晶片(要靠拉 BOOT0 或 connect-under-reset 硬救回來,折騰得很)。新手務必開 Serial Wire,這步偷懶等著自己收拾爛攤子。

### 2.4 外設腳位(照你的板子接線,別照抄別人的板子)

以範本的 F6222 SPI demo 為例,設了這些。**你依自己硬體改**,操作方式一樣:

**SPI1(4-wire SPI 主控)**

左側 **Connectivity → SPI1**:

- **Mode** → `Full-Duplex Master`。
- 自動配腳:`PA5 = SPI1_SCK`、`PA6 = SPI1_MISO`、`PA7 = SPI1_MOSI`（本 repo 範本 `stm32-h743-FPGA-middleware`；F6222 demo 若用 PB5 見下方 CN7 表 SB 說明）。
- Parameter Settings:
  - `Data Size` = 8 Bits
  - `Prescaler` 調到你要的鮑率(範本 `/128` → ≈1 MBit/s,因為 SYSCLK 只有 64 MHz)
  - CPOL/CPHA 依 slave 需求設(F6222 上升沿採樣 → Mode 0)
  - `NSS` → **`Software NSS`**(片選不由 SPI 硬體管,改由 GPIO 手動拉)

**NUCLEO-H743ZI2：SPI1 對 CN7（Zio / Arduino 接頭）針腳**

SPI1 四線全在 **CN7**（ST Zio，Arduino Uno V3 數位腳 D10–D13 那一排）。對照 ST **UM2407** Table 18：

| STM32 腳 | Arduino 名 | 訊號 | **CN7 針腳** | 備註 |
|---------|-----------|------|-------------|------|
| PA5 | D13 | SPI1_SCK | **pin 10** | — |
| PA6 | D12 | SPI1_MISO | **pin 12** | — |
| PA7 | D11 | SPI1_MOSI | **pin 14** | 須 **SB33 OFF、SB35 ON**（D11 接 PA7） |
| PB5 | D11 | SPI1_MOSI（替代） | **pin 14** | 出廠預設 **SB33 ON、SB35 OFF**（D11 接 PB5）；F6222 demo 用此配置 |
| PD14 | D10 | SPI1_CSB（軟體片選） | **pin 16** | GPIO_Output，非 SPI 硬體 NSS |

> 針腳查自 UM2407 Table 18（CN7 Zio connector pinout）。**MOSI 同一物理針 CN7 pin 14**，PA7 與 PB5 二選一，靠板背 SB33/SB35 跳線決定，焊對腳名但 SB 不對 MOSI 照樣啞火。
>
> GND 可取 CN7 **pin 8**（或同排任一 GND）。3.3 V 邏輯，勿當 5 V tolerance 用。

**SPI 片選 CSB(軟體 GPIO,必設)**

SPI1 用 `SPI_NSS_SOFT`,**CSB 不接 SPI 硬體 NSS 腳**,須另設一隻 GPIO 當片選:

1. 右邊晶片圖點 **`PD14`** → 選 **`GPIO_Output`**。
2. 下方 **GPIO** 設定頁:
   - `GPIO output level` = **`High`**(開機 CS 釋放,不選中)
   - **User Label** = `F6222_CSB`(或 `SPI_CSB`,產生 `#define` 供程式用)
3. 傳輸時軟體拉低 CSB → `HAL_SPI_TransmitReceive()` → 拉高 CSB(見 `f6222_spi_stm32.c`)。

> 範本固定 **PD14 = SPI CSB**。板子若接別腳,改 GPIO 即可,SPI SCK/MISO/MOSI 不動。

**USART3(printf / log 用)**

左側 **Connectivity → USART3** → Mode `Asynchronous`。

> ⚠️ USART3_TX/RX 在 H743 有**多組可選腳**(PB10/PB11、PD8/PD9、PC10/PC11…)。CubeMX 選 Async 後會**自動挑第一個可用腳**(新版常落在 **PB10=TX / PB11=RX**),不一定跟範本的 **PD8/PD9** 相同,別懵。
>
> **腳位以你板子實際接線為準,不要照抄範本,抄錯了自己debug到懷疑人生。** 若板子 UART 接在別的腳:先在晶片圖點目前被佔的腳 → 選 `Reset_State` 解除,再點目標腳選 `USART3_TX` / `USART3_RX`(點目標 TX 腳時 CubeMX 也會自動把舊腳讓開,這點還算貼心)。

**GPIO(片選 CSB、RST、MODE、LED 等)**

直接在右邊晶片圖上點腳位 → 選 `GPIO_Output`(或 Input)。範本設了:

| 腳 | 用途（範例） | 備註 |
|----|------|------|
| PB0 | GPIO_Output | — |
| PB7 | GPIO_Output | — |
| PB14 | GPIO_Output | — |
| **PD14** | **SPI CSB(片選)** | `GPIO_Output`,User Label `F6222_CSB`;初始 `PinState = SET`(高電平=未選中) |

設 GPIO 時可在下方 **GPIO** 設定頁改:初始電平、上/下拉、輸出速度、**User Label**(給腳位取名,例如 `F6222_CSB`,產生的程式碼會用這名字當 `#define`,這功能是真牛逼,一定要用)。

**NUCLEO-H743ZI2 板載 3 顆 User LED(固定腳位,直接設 GPIO_Output)**

板上已焊 3 顆 user LED,接線固定(不能改腳,想改也白搭),CubeMX 直接在晶片圖點這 3 隻設 `GPIO_Output` 即可:

| LED | 顏色 | STM32 腳 | 建議 User Label | 備註 |
|-----|------|---------|-----------------|------|
| LD1 | 綠 (Green) | PB0 | `LED_GREEN` | 高電平亮（active-high）|
| LD2 | 黃 (Yellow) | PE1 | `LED_YELLOW` | 高電平亮 |
| LD3 | 紅 (Red) | PB14 | `LED_RED` | 高電平亮 |

> 3 顆都是 **active-high**:`GPIO_PIN_SET` 亮、`GPIO_PIN_RESET` 滅。初始電平設 `Reset`(開機不亮)即可。
>
> 腳位查自 ST **UM2407**(NUCLEO-H743ZI2 User Manual)§6.5 LEDs。⚠️ PB0(LD1)與範本 GPIO 表的 PB0 用途衝突——實際用板載 LED 時,這隻歸 LD1,別再拿去當其他 CSB/GPIO 用,搶著用兩邊都得崩。PB14(LD3)同理與範本 PB14 衝突,擇一使用,別貪心兩個都想要。

**GPIO MUX 選擇線(6-bit)建議腳位:`PE7`–`PE12`**

MUX 用 GPIO 輸出選通道(見 [stm32-spi-primer.md](stm32-spi-primer.md) 的 `gpio_set_mux`)。推薦這連續 6 隻,**在 NUCLEO-H743ZI2 上全部落在同一個 Zio 接頭 CN10**,選這幾隻是有講究的,不是瞎湊的:

| STM32 腳 | 建議 User Label | MUX bit | **針腳** | 該腳預設功能(參考) |
|---------|-----------------|---------|---------|------------------|
| PE7 | `MUX_SEL0` | bit0 | **CN10 pin16** | TIM1_ETR |
| PE8 | `MUX_SEL1` | bit1 | **CN10 pin18** | TIM1_CH1N |
| PE9 | `MUX_SEL2` | bit2 | **CN10 pin22** | TIM1_CH1 |
| PE10 | `MUX_SEL3` | bit3 | **CN10 pin20** | TIM1_CH2N |
| PE11 | `MUX_SEL4` | bit4 | **CN10 pin24** | TIM1_CH2 |
| PE12 | `MUX_SEL5` | bit5 | **CN10 pin26** | TIM1_CH3N |

> 針腳位置查自 ST **UM2407**(NUCLEO-H743ZI2 User Manual)Table 21 CN10 Zio Connector。CN10 是板子上的 ST Zio 接頭之一(Arduino 相容擴充腳)。
>
> ⚠️ **接線照 STM32 腳名對,不要照 pin 號順序,這坑不少人栽過**:CN10 上 PE9=pin22、PE10=pin20 是**對調**的(pin20 在 pin22 前面)。焊 `MUX_SEL2`(PE9) 要接 pin22、`MUX_SEL3`(PE10) 接 pin20,焊反了電路通但邏輯全亂,查半天都查不出來,氣死人。
>
> 6 隻全在 CN10 且都是偶數 pin(同一排),排線集中好接。GND 可取 CN10 上任一 GND pin(如 pin 8)。

**選這 6 隻的理由**(不是瞎選的,有道理):

1. **全在同一 port(GPIOE)** → 一次寫一個暫存器設完 6 隻,不用跨 port瞎折騰。
2. **bit 位置連續(7~12)** → MUX 值直接映射,一行寫完(見下方 code)。
3. **不撞現有腳** — 避開 SWD(PA13/14)、HSE(PH0/1)、SPI1(PA5/PA6/PA7 或 PB5、PD14)、USART3(PB10/11)。
4. **非啟動/振盪腳** — 不碰 BOOT0、PC13-15(RTC 弱驅動),碰了容易出詭異bug。
5. **LQFP144 上實體相鄰** → PCB 走線好拉,佈線的兄弟會感謝你。

在 CubeMX 逐一點 PE7~PE12 設 `GPIO_Output`,User Label 依上表命名,別偷懶不命名到時候看代碼一頭霧水。設定 GPIO 值的程式碼(寫在 `USER CODE` 區塊):

```c
// 設 6-bit MUX 值 (0–63)，PE7 對應 bit0
// 讀改寫版（易懂）
GPIOE->ODR = (GPIOE->ODR & ~(0x3Fu << 7)) | ((uint32_t)(mux & 0x3F) << 7);

// 原子版（BSRR，不需先讀，不會被中斷打斷）
GPIOE->BSRR = ((uint32_t)(~mux & 0x3F) << (7 + 16)) | ((uint32_t)(mux & 0x3F) << 7);
```

> MUX 若不需 6-bit,砍尾巴即可。例如 4-bit(16 通道)只用 PE7~PE10,把 mask `0x3F` 改 `0x0F`。

### 2.5 Clock Configuration(時脈樹)

上方分頁 **Clock Configuration**。

- 想跑滿速:把 HSE 餵進 PLL,拉 `SYSCLK` 到 400/480 MHz(H743 上限)。CubeMX 會自動算分頻/倍頻,紅色代表超規、黃色代表建議調整,別無視警示硬幹。
- 範本沒拉,維持 **64 MHz**(HSI 等級),對 SPI/UART demo 已足夠。**新手先別碰,能跑再優化,別剛學會走路就想飛。**

### 2.6 Project Manager —— 決定輸出成 VSCode 能吃的格式(這步是重中之重,搞錯前面全白幹)

上方分頁 **Project Manager**:

**Project 頁**

| 欄位 | 設定 | 說明 |
|------|------|------|
| Project Name | 例如 `stm32-h743-app` | 會變成資料夾名 |
| Project Location | 你的工作目錄 | — |
| **Toolchain / IDE** | **`Makefile`** | ⭐ **重點,別特麼手滑選錯**。不是選 STM32CubeIDE。選 Makefile 才能給 VSCode 擴充吃 |
| Linker Settings | 預設 Heap `0x200` / Stack `0x400` | 夠用,別瞎調 |

**Code Generator 頁**

- ✅ **Generate peripheral initialization as a pair of '.c/.h' files per peripheral**(可選,讓 spi.c/gpio.c 分檔,較整齊,強迫症福音)
- ✅ **Keep User Code when re-generating**(⭐ 必勾,不勾你寫的東西下次重新產生直接被吃掉,血本無歸)
- Copy only necessary library files(省空間,別堆一堆沒用的垃圾)

### 2.7 產生程式碼

右上 **GENERATE CODE**。完成後資料夾長這樣(對照範本):

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

`main.c` 裡會有 `MX_GPIO_Init()`、`MX_SPI1_Init()`、`MX_USART3_UART_Init()`,以及 `/* USER CODE BEGIN */ ... /* USER CODE END */` 區塊——**你的程式寫在這些區塊裡**,重新產生時不會被蓋掉,這是唯一的救命符,記牢了。

---

## 3. 接上 VSCode

### 3.1 開專案

VSCode **File → Open Folder** → 開剛產生的 `stm32-h743-app/`(含 `Makefile` 那層當 workspace root)。

裝好 §1 那三個擴充。首次開啟,「STM32 for VSCode」擴充會偵測到 Makefile,可能問你要不要產生設定檔,讓它產就完事了,別自己瞎手動。

### 3.2 `.vscode/` 設定檔(照範本補齊)

擴充會自動生大部分。以下四個檔對照範本,重點是**工具鏈路徑要對到你機器的 CubeCLT**,路徑填錯全套白搭:

**`.vscode/settings.json`** —— 指向 CubeCLT 工具鏈

```json
{
    "stm32-for-vscode.openOCDPath": false,
    "cortex-debug.armToolchainPath": "/opt/st/stm32cubeclt_1.21.0/GNU-tools-for-STM32/bin"
}
```

> 版本號 `1.21.0` 換成你實際裝的,別照抄範本數字。用 `ls /opt/st/` 查。

**`.vscode/tasks.json`** —— build / flash 任務(擴充指令包好了,直接抄不用自己寫)

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

**`.vscode/launch.json`** —— Cortex-Debug 除錯(用 OpenOCD)

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

**`openocd.cfg`**(放專案根,不是 `.vscode/`,別放錯地方)—— 告訴 OpenOCD 用哪個燒錄器和晶片

```tcl
# ST-Link 燒錄器
source [find interface/stlink.cfg]
# H7 目標
source [find target/stm32h7x.cfg]
```

**`.vscode/c_cpp_properties.json`** —— IntelliSense(C/C++ 擴充用)

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

> 想要更準的跳轉/補全,用 **clangd** + `compile_commands.json`(見 §5),別將就著用爛體驗。

---

## 4. Build / Flash / Debug 三步驟

### Build

- 側邊欄 STM32 擴充圖示 → **Build**,或 `Ctrl+Shift+B` 選 `Build STM`。
- 擴充實際跑的是 CubeMX 產的 Makefile(`make -j`)。輸出 `.elf` / `.hex` / `.bin` 到 `build/`。
- 首次會看到 HAL 一堆檔在編,別慌,正常。結尾有 `--print-memory-usage` 印 FLASH/RAM 用量。

### Flash(燒錄)

- 板子用 ST-Link 接電腦(Nucleo 板內建,自製板接 SWD 4 線:SWDIO/SWCLK/GND/3V3)。
- STM32 擴充 → **Flash**。底層 = OpenOCD 透過 ST-Link 寫 FLASH。
- 成功會看到 `Programming Started` → `Verified OK`,看不到這兩行就別瞎高興。

### Debug(除錯)

- 按 `F5`(跑 `launch.json` 的 `Debug STM32`)。
- 會先 build → 起 OpenOCD GDB server → 停在 `main()`。
- 可下斷點、看變數、看**外設暫存器**(Cortex-Debug 的 registers / peripherals 視窗),這功能是真香。

---

## 5. IntelliSense 加強(可選但強烈建議,別偷懶不裝):clangd + compile_commands.json

C/C++ 擴充對 HAL 巨集有時解析不完整,拉胯得很。改用 **clangd** 體驗最好,但它需要 `compile_commands.json`(每個 .c 的確切編譯指令)。

產生方式(在專案根,需 `bear` 或 `compiledb`):

```bash
# 方法 A：bear 包住 make
make clean && bear -- make -j

# 方法 B：compiledb
make clean && compiledb make -j
```

產出的 `compile_commands.json` 讓 clangd 精準知道每個檔的 include 路徑與巨集定義,跳轉/補全/錯誤標示全對,爽度直接拉滿。

> 範本專案根與 example 都放了 `compile_commands.json`,就是這用途。改腳位重新產生程式碼後,若加了新原始檔,重跑一次上面指令刷新即可,別忘了刷新不然又是一堆紅字嚇自己。

---

## 6. 之後的開發節奏

```
要改腳位 / 加外設 ──► 開 .ioc（CubeMX）──► 改 ──► GENERATE CODE ──► 回 VSCode
                                                                     │
寫邏輯 / 改邏輯 ──────────────────────────► 在 USER CODE 區塊寫 ◄─────┘
                                                                     │
                                        VSCode: Build → Flash → Debug
```

**鐵則,給老子刻腦子裡**

1. **只在 `/* USER CODE BEGIN */ ... /* USER CODE END */` 之間寫程式**,其餘讓 CubeMX 管。跨出去的改動,下次 GENERATE 直接被吃掉,別事後才哭。
2. 自己的驅動/中介層(如 F6222 driver)放獨立資料夾,在 CubeMX **Makefile 的 `C_SOURCES` / `C_INCLUDES`** 手動加,或用 STM32-for-VSCode 的 `STM32-for-VSCode.config.yaml` 的 `sourceFiles` / `includeDirectories` 加(見範本 repo 根目錄那份 yaml)。
3. `.ioc` 一定要進版控,別偷懶不提交。它是腳位設定的唯一真相來源,丟了等於瞎折騰重來。
4. `build/` 加進 `.gitignore`,不進版控,別把一堆編譯垃圾塞進倉庫膈應人。

---

## 7. 對接本專案(Tasa-BFIC-Middleware / F6222 driver)

這份 STM32 專案是 host 端。要把 F6222 driver 掛進來:

- driver 的 SPI 底層在 `platform/stm32/src/f6222_spi_stm32.c`,它呼叫 HAL 的 `HAL_SPI_TransmitReceive()` 等。
- 把 CubeMX 的 `hspi1` handle 傳給 driver 的初始化,**CSB 用 PD14**(User Label `F6222_CSB`),RST/MODE 用其餘 GPIO 腳(記得在 CubeMX 給它們 User Label,別偷懶不然代碼裡看得一頭霧水)。
- 詳細接法見 [f6222-integration-guide.md](f6222-integration-guide.md)、SPI 概念見 [stm32-spi-primer.md](stm32-spi-primer.md)。

---

## 附錄:範本專案關鍵設定速查(H743 已驗證,照著抄不會錯)

| 項目 | 值 |
|------|-----|
| MCU / 封裝 | STM32H743ZITx / LQFP144 |
| Toolchain | Makefile |
| Debug | Serial Wire（PA13/PA14）|
| SPI1 | Master, PA5=SCK / PA6=MISO / PA7=MOSI（或 PB5=MOSI）, 8-bit, NSS=Software；CN7：**pin10** SCK / **pin12** MISO / **pin14** MOSI |
| SPI CSB | **PD14**（GPIO_Output,User Label `SPI1_CSB` 或 `F6222_CSB`,初始高=未選中）；CN7 **pin 16**（D10）|
| USART3 | Async；範本 PD8=TX / PD9=RX（新版預設 PB10/PB11，依板子接線改）|
| GPIO Out | PB0, PB7, PB14 |
| GPIO MUX (建議) | PE7–PE12（6-bit，bit 7~12，同 port GPIOE）；NUCLEO-H743ZI2 全在 CN10：PE7=p16 PE8=p18 PE9=p22 PE10=p20 PE11=p24 PE12=p26 |
| Clock | HSE Crystal，SYSCLK 64 MHz（未拉 PLL）|
| CLT 路徑 | `/opt/st/stm32cubeclt_1.21.0/GNU-tools-for-STM32/bin` |
| 燒錄 | OpenOCD + ST-Link，`target/stm32h7x.cfg` |
