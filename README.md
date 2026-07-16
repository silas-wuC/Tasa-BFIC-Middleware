# Tasa-BFIC-Middleware

MCU 與 FPGA 之間的 BFIC（F6222）SPI 轉接層。F6222 driver 組好封包後，經本 middleware 切 GPIO 選路、再送 SPI 至 FPGA → 目標 BFIC。

**一句話：** 不必把 `f6222_*` 函式搬進 middleware；只要接好 `spi_xfer` 送信管道，之後照常呼叫 F6222 API 即可。

---

## 架構

```
MCU 應用程式
    │  f6222_init() / f6222_set_phase() / …
    ▼
RENESAS-F6222-Driver（third_party）  ← 組 SPI 封包，呼叫 dev->spi_xfer
    ▼
tasa_bfic_bridge.c                   ← init 時把 spi_xfer 換成 bridge
    ▼
tasa_fpga_link.c                     ← gpio_set_mux + spi_xfer
    ▼
板級 HAL（你實作）                    ← 真的拉 GPIO / SPI 腳
    ▼
FPGA → BFIC
```

| 檔案 | 職責 |
|------|------|
| `tasa_bfic_mode.c` | 路線表：0x03 = AiP A 第 3 顆，0x00 = 廣播全部… |
| `tasa_fpga_link.c` | 送信：先切 GPIO 選路，再 SPI 送出 |
| `tasa_bfic_bridge.c` | 接線：把 F6222 driver 的 `spi_xfer` 接到 link |

---

## 你要寫什麼、不用寫什麼

### 要寫（板級 HAL，各 MCU 專案不同）

```c
/* ① 切 6 根 GPIO，選要通哪顆 BFIC */
int board_gpio_set_mux(void* ctx, uint8_t mode_bits) {
    /* 例：mode_bits = 0x03 → GPIO 排出 0b000011 */
    return 0;
}

/* ② 真的 SPI 時鐘（CS 拉低、送 byte、CS 拉高） */
int board_spi_xfer(void* ctx, const uint8_t* tx, uint8_t* rx, size_t len) {
    /* HAL_SPI_TransmitReceive… */
    return 0;
}
```

### 不用寫

- 把 `f6222_init` 等函式複製進 middleware
- 為每個 `f6222_*` 建函式指標表
- 改 F6222 driver 源碼

F6222 driver 內部每次寫暫存器都會呼叫 `dev->spi_xfer`。攔住這一個指標 = 攔住所有 F6222 操作。

---

## 四步用法

```c
#include "f6222.h"
#include "tasa_bfic_bridge.h"

/* --- 第一步：板級 HAL（見上） --- */

/* --- 第二步：建立送信管道 --- */
static tasa_fpga_dev_t link = {
    .gpio_set_mux = board_gpio_set_mux,
    .spi_xfer       = board_spi_xfer,
    .ctx            = NULL,
};

/* --- 第三步：接線 + 選路線 --- */
static tasa_bfic_bridge_t bridge;
static f6222_dev_t dev;

tasa_bfic_bridge_init(&bridge, &dev, &link, TASA_BFIC_MODE_AIP_BROADCAST);

/* --- 第四步：直接呼叫 F6222 API --- */
f6222_init(&dev, chip_addr);   /* SPI 全走 middleware，chip_addr 對應硬體 ADD[4:0] */
```

`tasa_bfic_bridge_init()` 做的事：

```c
dev->spi_xfer = tasa_bfic_bridge_spi_xfer;  /* 函式指標：留空位，填真正送信的人 */
dev->ctx      = bridge;
```

之後 F6222 driver 每次要送 SPI，都會自動經 bridge → link → 你的板級 HAL。

---

## 情境範例

### 廣播初始化全部 BFIC

```c
tasa_bfic_bridge_init(&bridge, &dev, &link, TASA_BFIC_MODE_AIP_BROADCAST);  /* 0x00 */
f6222_init(&dev, chip_addr);
```

FPGA MUX 設為廣播，同一筆 SPI 寫入會送到所有 BFIC。`chip_addr` 仍須與硬體位址一致（通常各顆不同）。

### 只初始化 AiP A 第 3 顆

```c
tasa_bfic_bridge_init(&bridge, &dev, &link, TASA_BFIC_MODE_AIP_A_BF3);  /* 0x03 */
f6222_init(&dev, chip_addr);
```

### 只寫 Tile A 全部

```c
tasa_bfic_bridge_init(&bridge, &dev, &link, TASA_BFIC_MODE_AIP_BROADCAST_A);  /* 0x25 */
f6222_init(&dev, chip_addr);
```

### 換顆 BFIC

每顆一個 `bridge`（或重新 `tasa_bfic_bridge_init` 換 `mode`），driver 本身不用改。

---

## FPGA 內部控制（0x2F）

Mode 0x2F（`TASA_BFIC_MODE_FPGA_INTERNAL`）是個異類：它**不透傳 F6222 封包**，而是走 FPGA 自己的暫存器協議（Command byte + 暫存器位址 + dummy/data）。所以**不經 bridge、不用 F6222 driver**，直接用 `tasa_fpga_ctrl_*`，底層仍復用同一條 `tasa_fpga_dev_t` 送信管道（GPIO 自動切到 0x2F）。

```c
#include "tasa_fpga_ctrl.h"

/* link 就是前面建立的 tasa_fpga_dev_t（gpio_set_mux + spi_xfer） */
uint8_t ver[4] = {0};
if (tasa_fpga_ctrl_read_version(&link, ver) == TASA_OK) {
    printf("FPGA version: %u.%u.%u.%u\r\n", ver[0], ver[1], ver[2], ver[3]);
}
```

底層時序（讀 System 暫存器，`CMD = 0x80 Ctrl_FPGA | 0x10 Read | 0x08 System = 0x98`）：

| SPI  | byte0     | byte1 | byte2 | byte3…                     |
|------|-----------|-------|-------|----------------------------|
| MOSI | CMD(0x98) | 位址   | dummy | dummy…                     |
| MISO | -         | -     | -     | Data(addr), Data(addr+1)…  |

- 讀 `count` byte → 幀長 `count + 3`，收到的資料從 `rx[3]` 起算。
- 通用讀：`tasa_fpga_ctrl_read(&link, TASA_FPGA_REG_SYSTEM, addr, buf, count)`（`count ≤ 29`）。
- 目前只實作 System 讀；I2C State/Write/Result 與寫入為後續階段。

---

## Mode 選擇速查

| 你想做 | mode 常數 | 值 |
|--------|-----------|-----|
| 廣播寫全部 BFIC | `TASA_BFIC_MODE_AIP_BROADCAST` | 0x00 |
| 只寫 Tile A 全部 | `TASA_BFIC_MODE_AIP_BROADCAST_A` | 0x25 |
| 只寫 Tile B 全部 | `TASA_BFIC_MODE_AIP_BROADCAST_B` | 0x26 |
| 只寫 AiP A 第 3 顆 | `TASA_BFIC_MODE_AIP_A_BF3` | 0x03 |
| Flash A | `TASA_BFIC_MODE_FLASH_A` | 0x29 |

完整定義見 [`include/tasa_bfic_mode.h`](include/tasa_bfic_mode.h)。

---

## 逐步發生的事

假設已執行 `tasa_bfic_bridge_init(...)` 後呼叫 `f6222_init(&dev, chip_addr)`：

| 步 | 誰 | 做什麼 |
|----|-----|--------|
| 1 | 應用程式 | 叫 `f6222_init()` |
| 2 | F6222 driver | 迴圈 76 次，組「寫暫存器」封包 |
| 3 | F6222 driver | 每次呼叫 `dev->spi_xfer(...)` |
| 4 | bridge | 轉給 `tasa_fpga_mux_xfer()` |
| 5 | link | ① `gpio_set_mux` 切路線 ② `spi_xfer` 時鐘送出 |
| 6 | 板級 HAL | 真的拉 MCU GPIO 和 SPI 腳 |
| 7 | FPGA | 轉到目標 BFIC |

你沒在 middleware 裡寫 `f6222_init`，但它送的每一 byte 都經 middleware。

---

## 常見誤解

| 誤解 | 正解 |
|------|------|
| SPI 要經 middleware，所以 f6222 函式要搬進 middleware | 只搬 **送信**；**組包** 留 F6222 driver |
| 每顆 BFIC 要改 driver | 換 `mode` 即可，driver 不動 |
| middleware 還沒完成所以不能送 SPI | Phase 1 已完成；缺的是板級 `gpio_set_mux` / `spi_xfer` |

---

## 相依

- [RENESAS-F6222-Driver](third_party/RENESAS-F6222-Driver)（git submodule）
- Include path 需同時含 `include/` 與 `third_party/RENESAS-F6222-Driver/include/`

---

## FPGA Reference

Register map, interface, and protocol definitions for BFIC / FPGA:

**[FPGA Reference Spreadsheet](https://docs.google.com/spreadsheets/d/1mG5EBg3hF-dM5JgIYT1P0irBdOqtw-7Vai5JK5146gU/edit?gid=1990517554#gid=1990517554)**
