# F6222 與 Middleware 串接指南

說白了就是講清楚 RENESAS-F6222-Driver 怎麼靠 Tasa-BFIC-Middleware 這層膠水串到 FPGA / BFIC 上,每層誰幹啥、數據咋流、怎麼動手接、出了錯去哪兒查——都在這兒了,給老子看仔細了。

> 聽好了:driver 名字叫 **F6222**(third_party/RENESAS-F6222-Driver),不是 F6522,別特麼看錯,看錯了自己debug到崩潰別怪別人。

---

## 1. 為啥要搞個中介層

F6222 driver 這貨自己就一根筋,只知道「呼叫 `dev->spi_xfer` 甩個 SPI 封包出去」,別的啥都不知道,傻乎乎的:

- 這包要送去哪顆 BFIC 它不知道(板上可能焊了好幾顆,得靠 FPGA MUX 選路)
- 板子上 GPIO / SPI 腳位到底咋接的它也不知道

Middleware 幹的活兒就是在 driver 調用 `spi_xfer` 那一瞬間,偷偷塞進去一句「先切 GPIO 選路,再送 SPI」的邏輯,driver 那邊代碼一行都不用動,這波操作屬實牛逼。

---

## 2. 分層跟檔案對應關係

```
應用程式
    │  f6222_init() / f6222_set_phase() / f6222_local_reg_write() ...
    ▼
third_party/RENESAS-F6222-Driver          ← 組 SPI 封包（暫存器位址+資料）
    │  呼叫 dev->spi_xfer(dev->ctx, tx, rx, len)
    ▼
src/tasa_bfic_bridge.c                    ← 攔截點：把 spi_xfer 轉呼 tasa_fpga_mux_xfer
    ▼
src/tasa_fpga_link.c                      ← ① gpio_set_mux(選路) ② spi_xfer(送信)
    ▼
板級 HAL（你要實作，STM32/其他 MCU 皆可）  ← 真正拉 GPIO 腳、跑 SPI 時脈
    ▼
FPGA → 目標 BFIC
```

| 檔案 | 型別/函式 | 職責 |
|---|---|---|
| `third_party/RENESAS-F6222-Driver/include/f6222.h` | `f6222_dev_t` | driver 的裝置結構，含 `spi_xfer` function pointer + `ctx` |
| `include/tasa_bfic_mode.h` | `tasa_bfic_mux_mode_t` | MUX 路線表（0x00–0x2F），對應要打到哪顆 BFIC / 哪個 tile |
| `include/tasa_fpga_link.h` | `tasa_fpga_dev_t` | 送信管道結構，含 `gpio_set_mux` + `spi_xfer` 兩個 function pointer |
| `src/tasa_fpga_link.c` | `tasa_fpga_mux_xfer()` | 真正動作：先切 GPIO，再送 SPI |
| `include/tasa_bfic_bridge.h` / `src/tasa_bfic_bridge.c` | `tasa_bfic_bridge_t`, `tasa_bfic_bridge_init()` | 接線膠水：把 F6222 driver 的 `spi_xfer` 換成 bridge 提供的版本 |

---

## 3. 關鍵數據結構

### 3.1 F6222 driver 那端(third_party 的東西,別手賤去改,改了出事自己扛)

```c
typedef struct {
    int (*spi_xfer)(void* ctx, const uint8_t* tx, uint8_t* rx, size_t len);
    void* ctx;   /* 原封不動傳給 spi_xfer 的第一個參數 */
} f6222_dev_t;
```

driver 內部所有暫存器讀寫(`f6222_local_reg_write`、`f6222_local_reg_read`……這些)兜兜轉轉最後都得走 `dev->spi_xfer(dev->ctx, tx, rx, len)` 這一條道,躲都躲不掉。

### 3.2 Middleware 的送信管道

```c
typedef struct {
    int (*gpio_set_mux)(void* ctx, uint8_t mode_bits);  /* 切 6-bit MUX 路線 */
    int (*spi_xfer)(void* ctx, const uint8_t* tx, uint8_t* rx, size_t len);
    void* ctx;
} tasa_fpga_dev_t;
```

### 3.3 Bridge(純粹就是個膠水層,沒啥花活)

```c
typedef struct {
    tasa_fpga_dev_t* link;
    tasa_bfic_mux_mode_t mode;   /* 這個 bridge 固定送去哪個路線 */
} tasa_bfic_bridge_t;
```

`tasa_bfic_bridge_init()` 幹的事兒就兩行,簡單粗暴,別想太多:

```c
dev->spi_xfer = tasa_bfic_bridge_spi_xfer;  /* 蓋掉 driver 原本空的 spi_xfer */
dev->ctx      = bridge;                     /* driver 呼叫時第一參數變成 bridge 指標 */
```

這麼一改,之後 driver 每次調 `dev->spi_xfer(dev->ctx, tx, rx, len)`,背地裡實際跑的其實是這一坨:

```c
tasa_bfic_bridge_spi_xfer(bridge, tx, rx, len)
    → tasa_fpga_mux_xfer(bridge->link, bridge->mode, tx, rx, len)
        → link->gpio_set_mux(link->ctx, mode_bits)   /* 先切路線 */
        → link->spi_xfer(link->ctx, tx, rx, len)     /* 再送 SPI */
```

---

## 4. 串接步驟(輪到你上場幹活了,別磨磨唧唧)

### Step 1 — 實作板級 HAL

就兩個函式,簽名釘死了不能改,內容看你平台自己寫,別瞎改簽名不然全套接口都得跟著崩:

```c
/* 切 6 根 GPIO，決定這次 SPI 打到哪顆 BFIC / 哪個 tile */
int board_gpio_set_mux(void* ctx, uint8_t mode_bits) {
    /* 例如 mode_bits = 0x03 → 依序把 6 個 bit 拉到對應 GPIO pin */
    HAL_GPIO_WritePin(MUX0_Port, MUX0_Pin, (mode_bits & 0x01) ? SET : RESET);
    /* ... bit1~bit5 依序處理 ... */
    return 0;
}

/* 真正的 SPI 時脈：CS 拉低 → 收發 → CS 拉高 */
int board_spi_xfer(void* ctx, const uint8_t* tx, uint8_t* rx, size_t len) {
    HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_RESET);
    HAL_StatusTypeDef st = HAL_SPI_TransmitReceive((SPI_HandleTypeDef*)ctx,
                                                    (uint8_t*)tx, rx, len, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_SET);
    return (st == HAL_OK) ? 0 : -1;
}

/* 單調遞增毫秒 tick，供 set_beam 的超時等待使用 */
uint32_t board_get_tick_ms(void* ctx) {
    return HAL_GetTick();
}
```

STM32 SPI 週邊背景知識見 [stm32-spi-primer.md](stm32-spi-primer.md)。

### Step 2 — 建立送信管道 `tasa_fpga_dev_t`

```c
static tasa_fpga_dev_t link = {
    .gpio_set_mux = board_gpio_set_mux,
    .spi_xfer     = board_spi_xfer,
    .get_tick_ms  = board_get_tick_ms,
    .ctx          = &hspi1,   /* 你的 SPI handle，會傳回 board_spi_xfer 的 ctx */
};
```

### Step 3 — 接線：初始化 bridge，選定路線

```c
static tasa_bfic_bridge_t bridge;
static f6222_dev_t dev;

tasa_status_t rc = tasa_bfic_bridge_init(&bridge, &dev, &link, TASA_BFIC_MODE_AIP_A_BF3);
if (rc != TASA_OK) {
    /* TASA_ERR_INVALID_ARG：bridge/dev/link 為 NULL，或 mode 超出 0x00–0x2F */
}
```

### Step 4 — 照常呼叫 F6222 API,別自己整活加東西

```c
f6222_status_t st = f6222_init(&dev, chip_addr);  /* chip_addr 對應硬體 ADD[4:0]，與路線無關 */
```

`f6222_init()` 內部跑 76 次暫存器寫入 + ready/scratch test,每一次都自動經過 bridge → link → 板級 HAL → FPGA → 目標 BFIC,呼叫端完全無感,爽的就是這點——你根本不用操心底層那攤事兒。

---

## 5. Mode(路線)選擇,選錯了打到別的BFIC別怪middleware

`tasa_bfic_mux_mode_t`（`include/tasa_bfic_mode.h`）決定這次 SPI 打到哪裡，6-bit GPIO 欄位：

| 用途 | 常數 | 值 |
|---|---|---|
| 廣播全部 BFIC | `TASA_BFIC_MODE_AIP_BROADCAST` | 0x00 |
| Tile A 第 N 顆（N=1~9） | `TASA_BFIC_MODE_AIP_A_BF{N}` | 0x01–0x09 |
| Tile B 第 N 顆 | `TASA_BFIC_MODE_AIP_B_BF{N}` | 0x0A–0x12 |
| Tile C 第 N 顆 | `TASA_BFIC_MODE_AIP_C_BF{N}` | 0x13–0x1B |
| Tile D 第 N 顆 | `TASA_BFIC_MODE_AIP_D_BF{N}` | 0x1C–0x24 |
| 廣播 Tile A/B/C/D 全部 | `TASA_BFIC_MODE_AIP_BROADCAST_{A,B,C,D}` | 0x25–0x28 |
| Flash A/B/C/D | `TASA_BFIC_MODE_FLASH_{A,B,C,D}` | 0x29–0x2C |
| Flash Boot | `TASA_BFIC_MODE_FLASH_BOOT` | 0x2D |
| 內部 FPGA 控制（不可用於 TX） | `TASA_BFIC_MODE_FPGA_INTERNAL` | 0x2F |

輔助函式（`tasa_bfic_mode.c`）：

- `tasa_bfic_mode_from_tile_bf(tile, bf)`：用 tile + bf 編號查 mode，bf=0 回傳該 tile 廣播 mode
- `tasa_bfic_mode_valid_for_dir(mode, dir)`：檢查 mode 對 TX/RX 方向是否合法（BF9 系列為 TX-only，`TASA_BFIC_MODE_FPGA_INTERNAL` 不可做資料傳輸）
- `tasa_bfic_mode_name(mode)`：debug 用，回傳可讀字串

**換顆 BFIC**:每顆配一個獨立 `tasa_bfic_bridge_t`,或對同一個 bridge 重新呼叫 `tasa_bfic_bridge_init()` 換 mode。driver 端程式碼一行都不用動,省心。

---

## 6. 完整流程追蹤（以 `f6222_init` 為例）

| 步驟 | 執行者 | 動作 |
|---|---|---|
| 1 | 應用程式 | `tasa_bfic_bridge_init(&bridge, &dev, &link, mode)` |
| 2 | 應用程式 | `f6222_init(&dev, chip_addr)` |
| 3 | F6222 driver | `f6222_wait_ready` → `f6222_scratch_test` → 76 次暫存器寫入 → 16 channel disable |
| 4 | F6222 driver | 每次都呼叫 `dev->spi_xfer(dev->ctx, tx, rx, len)` |
| 5 | bridge | `tasa_bfic_bridge_spi_xfer` 轉呼 `tasa_fpga_mux_xfer(bridge->link, bridge->mode, tx, rx, len)` |
| 6 | link | 先 `gpio_set_mux(mode_bits)` 切路線，再 `spi_xfer(tx, rx, len)` 送出 |
| 7 | 板級 HAL | 真的拉 GPIO、跑 SPI 時脈 |
| 8 | FPGA | 依 MUX 選路轉給目標 BFIC |

---

## 7. 錯誤碼對照

`tasa_status_t`（`tasa_fpga_link.h`）：

| 值 | 意義 | 常見成因 |
|---|---|---|
| `TASA_OK` (0) | 成功 | |
| `TASA_ERR_INVALID_ARG` (-1) | 參數錯誤 | `tasa_fpga_mux_xfer`：`dev`/`tx` 為 NULL、`len` 為 0、mode 不合法；`tasa_fpga_ctrl_read`：`count` 為 0 或超過 `TASA_FPGA_CTRL_MAX_PAYLOAD`(18) |
| `TASA_ERR_GPIO` (-2 對應來源) | `gpio_set_mux` 回傳負值 | 板級 HAL GPIO 操作失敗 |
| `TASA_ERR_SPI` | `spi_xfer` 回傳負值 | 板級 HAL SPI 傳輸失敗（如 HAL_SPI_TransmitReceive timeout） |

`f6222_status_t`(driver 端,`f6222.h`)額外處理 silicon ID 不符、scratch pattern 不符、ready timeout 等 protocol 層錯誤,跟 middleware 層是分開的兩組錯誤碼,出錯時兩邊都得查,別偷懶只看一邊就下結論。

---

## 8. 常見誤解(踩坑重災區,看仔細了)

| 誤解 | 正解 |
|---|---|
| SPI 要經 middleware，所以要把 f6222 函式搬進 middleware | 不用，只接 `spi_xfer` 這一個管道，組包邏輯留在 F6222 driver |
| 每顆 BFIC 要各自改一份 driver | 不用改 driver，只要換 `mode` 或多開一個 `bridge` |
| Middleware 沒做完不能用 | Phase 1（MUX passthrough）已完成，缺的只是板級 `gpio_set_mux` / `spi_xfer` 實作 |
| `chip_addr` 跟 `mode` 是同一件事 | 不是,別搞混了。`mode` 決定 FPGA MUX 路線(走哪個 SPI 通道),`chip_addr` 是 BFIC 硬體位址(ADD[4:0]),兩者都要對,缺一不可,搞錯一個就等著抓瞎 |

---

## 9. 相依與參考

- Driver：[third_party/RENESAS-F6222-Driver](../third_party/RENESAS-F6222-Driver)（git submodule）
- Include path 需同時含 `include/` 與 `third_party/RENESAS-F6222-Driver/include/`
- STM32 SPI 背景知識：[stm32-spi-primer.md](stm32-spi-primer.md)
- FPGA / BFIC register map：[FPGA Reference Spreadsheet](https://docs.google.com/spreadsheets/d/1mG5EBg3hF-dM5JgIYT1P0irBdOqtw-7Vai5JK5146gU/edit?gid=1990517554#gid=1990517554)
