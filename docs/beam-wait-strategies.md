# 等 FPGA「做完」的正确姿势：poll_max 到底烂在哪，以及 8 种替代方案

> 針對 [`tasa_fpga_ctrl_set_beam()`](../src/tasa_fpga_ctrl.c) 裡那個 `poll_max` 迴圈計數輪詢。
> 從新手一眼看懂，到資深工程師的事件驅動玩法，全給你排成一條階梯，附好壞與難度對比。

---

## 0. 先搞明白：現在這套 `poll_max` 是幹嘛的，坑在哪

現在的代碼長這樣（[`src/tasa_fpga_ctrl.c:156`](../src/tasa_fpga_ctrl.c#L156-L169)）：

```c
/* 5. Block on Auto mode status (bit 3) until done or poll budget runs out. */
for (unsigned int i = 0; poll_max == 0u || i < poll_max; i++) {
    bool done = false;
    st = tasa_fpga_ctrl_beam_is_done(dev, &done);   // 每圈一次 SPI 讀暫存器
    if (st != TASA_OK) {
        return st;
    }
    if (done) {
        return TASA_OK;
    }
}
return TASA_ERR_TIMEOUT;
```

翻譯成人話：**設完 beam 之後，瘋狂讀 FPGA 的 bit 3 狀態位，讀到「done」就返回，讀滿 `poll_max` 次還沒 done 就報 timeout。** `poll_max == 0` 表示「老子讀到天荒地老也不放棄」。

臥槽，這寫法能跑，但一堆坑，挨個給你點出來：

| # | 坑 | 為啥膈應 |
|---|-----|---------|
| 1 | **數的是圈數，不是時間** | `poll_max` 是迴圈次數，不是毫秒。真實等待時長 = `poll_max` × 一次 SPI 讀暫存器的耗時。而這耗時取決於 SPI 時鐘頻率、幀長、CPU 主頻、總線有沒有人搶。換塊板子、SPI 從 10MHz 調到 1MHz，同樣 `poll_max=1000` 可能是 5ms 也可能是 500ms。**不可預測、不可移植**。頭文件自己都招了：`bounds SPI reads, not wall time`。 |
| 2 | **純忙等（busy-wait）** | 兩圈之間沒有任何喘息，CPU 100% 空轉狂讀，SPI 總線被無意義的 poll 打滿，功耗拉滿。單核裸機上，這段時間整個系統就是塊板磚。 |
| 3 | **`poll_max == 0` 是顆雷** | FPGA 要是掛了 / done 位永遠不來，這就是個**死循環**，直接把系統焊死，看門狗不咬你就等著重啟吧。 |
| 4 | **純阻塞** | 函數不返回，調用者啥也幹不了。裸機 superloop 裡其他任務全餓死，UART 掉包、按鍵沒反應。 |
| 5 | **策略和機制混在一起還甩鍋給調用者** | 「該等多久」這種策略問題，被塞成一個玄學數字 `poll_max`，每個調用點自己猜。今天猜 1000，明天上量產發現不夠，改 5000，屎山越堆越高。 |

**一句話：`poll_max` 不是「錯」，是「原始」。** 它是「連時間都拿不到」時的兜底寫法。下面這條階梯，就是帶你從這個泥坑一級一級往上爬。

---

## 階梯總覽（先看這張，細節在後面）

| Lv | 方案 | 一句話原理 | 難度 | CPU 佔用 | SPI 總線 | 時間可預測 | 可移植 | 額外依賴 | 阻塞? |
|----|------|-----------|:----:|:-------:|:-------:|:--------:|:------:|---------|:-----:|
| 0 | **現狀：圈數輪詢** | 讀滿 N 次就放棄 | 🟢 | 爆滿 | 爆滿 | ❌ | ✅ | 無 | 是 |
| 1 | **圈數 + 固定延時** | 每圈之間睡 1ms | 🟢 | 高 | 低 | 半吊子 | ⚠️ | delay 回調 | 是 |
| 2 | **時間戳超時** ⭐ | 用 tick 算真實 deadline | 🟡 | 高 | 中 | ✅ | ✅ | tick 回調 | 是 |
| 3 | **超時 + 退避節流** | 間隔由短到長遞增 | 🟠 | 中 | 低 | ✅ | ✅ | tick + delay | 是 |
| 4 | **非阻塞狀態機** | 拆成 start / poll 兩半 | 🟠 | 低 | 低 | ✅ | ✅ | 調用者主循環 | **否** |
| 5 | **GPIO done 腳 + 中斷** | done 邊沿觸發中斷 | 🔴 | 極低 | 零 | ✅ | ⚠️硬體 | FPGA 引腳 + EXTI | 可選 |
| 6 | **RTOS 信號量 + 中斷喚醒** | 阻塞讓出 CPU，中斷叫醒 | 🔴 | 極低 | 零 | ✅ | ⚠️需 RTOS | RTOS + 中斷 | 是(只阻自己) |
| 7 | **DMA + 全事件驅動** | 整條鏈路零忙等零阻塞 | ⚫ | 極低 | 零 | ✅ | ⚠️硬體+框架 | DMA + 中斷 + 狀態機 | 否 |

難度圖例：🟢 新手 / 🟡 入門 / 🟠 中級 / 🔴 高級 / ⚫ 資深（過度設計預警）

---

## ⚠️ 動手前必讀：這層根本沒有「時間」

在往下抄代碼之前，先記住一個**架構硬約束**：

`tasa_fpga_dev_t`（[`include/tasa_fpga_link.h:32`](../include/tasa_fpga_link.h#L32-L44)）目前只有兩個函數指針：

```c
typedef struct {
    int (*gpio_set_mux)(void* ctx, uint8_t mode_bits);
    int (*spi_xfer)(void* ctx, const uint8_t* tx, uint8_t* rx, size_t len);
    void* ctx;
} tasa_fpga_dev_t;
```

**沒有 `get_tick`，沒有 `delay`。** 這就是為啥當初只能用「數圈數」這種爛招——它連當前時間都拿不到。

所以 Lv1 往上，凡是要碰「真實時間」的方案，第一步都得**給 dev 注入時間能力**。但這是個平台無關的 middleware，**絕對不能**在這層直接 `#include "stm32h7xx_hal.h"` 然後調 `HAL_GetTick()`——那就把整個 middleware 焊死在 STM32 上了，別的平台直接沒法用。

正確做法：**照著現有的回調風格，再加一兩個函數指針**，讓板級 HAL 自己填：

```c
typedef struct {
    int (*gpio_set_mux)(void* ctx, uint8_t mode_bits);
    int (*spi_xfer)(void* ctx, const uint8_t* tx, uint8_t* rx, size_t len);

    /* 新增：平台注入「時間」能力，middleware 本身不碰任何 HAL */
    uint32_t (*get_tick_ms)(void* ctx);       /* 返回單調遞增的毫秒 tick，給 Lv2/Lv3 */
    void     (*delay_ms)(void* ctx, uint32_t ms);  /* 可選：睡一會兒，給 Lv1/Lv3 */

    void* ctx;
} tasa_fpga_dev_t;
```

STM32 那邊填起來就一行：

```c
static uint32_t stm32_get_tick_ms(void* ctx) { (void)ctx; return HAL_GetTick(); }
static void     stm32_delay_ms(void* ctx, uint32_t ms) { (void)ctx; HAL_Delay(ms); }
```

**這個決策是 Lv2~Lv7 的地基，先想清楚再動手。** 記住原則：`middleware 只定義「需要什麼能力」，平台負責「怎麼實現」`。

---

## Lv 1 — 圈數輪詢 + 固定延時（🟢 新手：改動最小）

**思路**：別再讓 CPU 空轉狂讀了，每圈之間睡一小會兒（比如 1ms）。這樣圈數 × 間隔 ≈ 大致的等待時間，SPI 總線也不會被打滿。

```c
for (unsigned int i = 0; i < poll_max; i++) {
    bool done = false;
    st = tasa_fpga_ctrl_beam_is_done(dev, &done);
    if (st != TASA_OK) {
        return st;
    }
    if (done) {
        return TASA_OK;
    }
    dev->delay_ms(dev->ctx, 1);   /* ← 唯一的改動：每圈睡 1ms，別霸著總線 */
}
return TASA_ERR_TIMEOUT;
```

| ✅ 好處 | ❌ 壞處 |
|--------|--------|
| 改動一行，新手都能看懂 | `poll_max` 還是圈數，只是現在 ≈ 毫秒，仍然在猜 |
| SPI 總線不再被打滿 | **裸機的 `HAL_Delay` 本身就是 SysTick 忙等**，CPU 並沒真省下來（想真省電得 `__WFI()` 或上 RTOS） |
| 移植代價低 | 固定 1ms 間隔：beam 若 50µs 就好了，你白等近 1ms；響應和開銷沒法兼顧 |

> **老哥點評**：這是「花五分鐘讓現狀不那麼丟人」的方案。能救急，但別停在這，它只是給屎山噴了瓶香水。

---

## Lv 2 — 時間戳超時（🟡 入門：**性價比之王，強烈建議先上這個**）⭐

**思路**：別數圈數了，數**時間**。開跑前記個時間戳，每圈拿當前 tick 一減，超過 `timeout_ms` 就 timeout。這是工業界最標準的超時寫法，STM32 HAL 自己也是這麼幹的（看 `HAL_SPI_TransmitReceive` 的 `Timeout` 參數）。

把 API 從 `poll_max`（圈數）換成 `timeout_ms`（真實毫秒）：

```c
tasa_status_t tasa_fpga_ctrl_set_beam(tasa_fpga_dev_t* dev, tasa_bfic_dir_t dir,
                                      tasa_beam_polar_t polar, tasa_beam_phase_t phase,
                                      uint32_t timeout_ms) {
    /* ...前面配置 + 觸發 Set Beam 的步驟不變... */

    uint32_t start = dev->get_tick_ms(dev->ctx);
    for (;;) {
        bool done = false;
        st = tasa_fpga_ctrl_beam_is_done(dev, &done);
        if (st != TASA_OK) {
            return st;
        }
        if (done) {
            return TASA_OK;
        }
        /* 無符號回繞減法：即使 tick 溢出翻轉，只要 timeout 不接近 2^31 ms 就恆正確 */
        if ((uint32_t)(dev->get_tick_ms(dev->ctx) - start) >= timeout_ms) {
            return TASA_ERR_TIMEOUT;
        }
    }
}
```

**資深細節（別漏）**：`(uint32_t)(now - start)` 這個無符號減法是關鍵。`HAL_GetTick()` 是 32 位毫秒計數，約 49.7 天就回繞到 0。用無符號減法，即使跨越回繞點結果也對——**別手賤寫成 `now >= start + timeout_ms`，那玩意兒一溢出就當場暴斃**。

| ✅ 好處 | ❌ 壞處 |
|--------|--------|
| **超時是真·毫秒**，換板子/換 SPI 速率都不變，可預測、可移植 | 需要給 dev 加 `get_tick_ms` 回調（見上面架構章節） |
| 調用方寫 `timeout_ms=50` 一眼就懂啥意思，不用猜玄學數字 | 還是忙等輪詢（沒加 delay 的話），CPU 仍在轉——可疊加 Lv1 的 delay |
| 徹底幹掉 `poll_max==0` 死循環雷（超時是強制的） | 仍是阻塞式，不解決「阻塞整個系統」的問題 |

> **老哥點評**：**要是你只想改一個地方就收工，就改這兒。** 從「圈數」升到「時間戳超時」是投入產出比最高的一步，其他花活兒都是錦上添花。90% 的場景，Lv2（或 Lv2+Lv1 的 delay）就夠用了。

---

## Lv 3 — 超時 + 退避節流（🟠 中級：又快又省）

**思路**：Lv1 的固定間隔太死板——beam 通常很快就好，偶爾慢。那就**一開始密集查（響應快），越查不到間隔越大（省開銷）**，這叫指數退避（exponential backoff）。快的情況幾乎零延遲抓到，慢的情況也不會把總線和 CPU 榨乾。

```c
uint32_t start    = dev->get_tick_ms(dev->ctx);
uint32_t interval = 0;   /* 首圈不睡，先搏一把「秒完成」 */
for (;;) {
    bool done = false;
    st = tasa_fpga_ctrl_beam_is_done(dev, &done);
    if (st != TASA_OK) {
        return st;
    }
    if (done) {
        return TASA_OK;
    }
    if ((uint32_t)(dev->get_tick_ms(dev->ctx) - start) >= timeout_ms) {
        return TASA_ERR_TIMEOUT;
    }
    if (interval != 0u) {
        dev->delay_ms(dev->ctx, interval);
    }
    if (interval < 8u) {              /* 0 → 1 → 2 → 4 → 8ms 封頂 */
        interval = (interval == 0u) ? 1u : (interval << 1);
    }
}
```

| ✅ 好處 | ❌ 壞處 |
|--------|--------|
| 快的 beam 幾乎零延遲抓到，慢的也不燒 CPU | 代碼複雜度上來了，要調退避曲線（起始間隔、倍率、封頂） |
| 總線/功耗開銷比固定間隔更低 | 同時要 `get_tick_ms` + `delay_ms` 倆回調 |
| 兼顧響應速度和資源佔用 | 還是阻塞式 |

> **老哥點評**：Lv2 的親兒子，適合「beam 完成時間波動大、又在乎功耗」的場景。沒這需求就別瞎折騰，Lv2 足夠。

---

## Lv 4 — 非阻塞狀態機（🟠 中級：裸機 superloop 的救星）

**思路**：前面全是「函數不返回、死等」。Lv4 直接把它**拆成兩半**：

- `set_beam_start()`：配置 + 觸發 Set Beam，**立刻返回**，不等。
- 調用者在自己的主循環（superloop）裡，**每圈順手查一下** done，同時自己管超時。

好消息是——**項目早埋好伏筆了**。[`tasa_fpga_ctrl_beam_is_done()`](../include/tasa_fpga_ctrl.h#L341-L353) 的注釋白紙黑字寫著：

> *Useful for non-blocking callers that poll on their own.*

拆分後的 API 大概長這樣：

```c
/* 只配置 + 觸發，不等，立刻滾回來 */
tasa_status_t tasa_fpga_ctrl_set_beam_start(tasa_fpga_dev_t* dev, tasa_bfic_dir_t dir,
                                            tasa_beam_polar_t polar, tasa_beam_phase_t phase);
```

調用者主循環裡自己管狀態和超時，一點不阻塞別人：

```c
/* ── 應用層：一個都不阻塞的超級循環 ── */
static bool     beam_pending = false;
static uint32_t beam_start_ms;

void app_request_beam(void) {
    tasa_fpga_ctrl_set_beam_start(&link, dir, polar, phase);
    beam_start_ms = HAL_GetTick();
    beam_pending  = true;
}

void app_super_loop(void) {
    for (;;) {
        if (beam_pending) {
            bool done = false;
            if (tasa_fpga_ctrl_beam_is_done(&link, &done) == TASA_OK && done) {
                beam_pending = false;
                on_beam_ok();
            } else if (HAL_GetTick() - beam_start_ms >= BEAM_TIMEOUT_MS) {
                beam_pending = false;
                on_beam_timeout();
            }
        }
        service_uart();      /* ← 這些活兒再也不會被 beam 等待餓死 */
        service_buttons();
        service_whatever();
    }
}
```

| ✅ 好處 | ❌ 壞處 |
|--------|--------|
| **不阻塞系統**，UART/按鍵/其他任務照常跑 | 調用者要自己維護狀態（`beam_pending` 這類），心智負擔轉移出去了 |
| 純軟件，不要額外硬體 | 響應延遲取決於你 superloop 一圈多長 |
| 完美適配裸機 superloop / 協作式調度 | 多個並發 beam 請求要自己排隊管理 |

> **老哥點評**：**沒 RTOS 又不想被一次 beam 卡死全家，這就是正解。** 阻塞 vs 非阻塞是嵌入式的分水嶺，跨過這道坎你就從「調庫的」變「懂架構的」了。項目 API 都給你留好門了，順水推舟的事。

---

## Lv 5 — GPIO done 腳 + 外部中斷（🔴 高級：徹底告別輪詢）

**思路**：前面再花哨，本質都是「反覆問 FPGA 好了沒」。Lv5 換個玩法——**讓 FPGA 好了主動拍你肩膀**。

前提：**FPGA 得把「done」狀態引出一根 GPIO**（硬體要支持，得跟 FPGA/硬體的人對線）。這根腳接到 MCU 的一個外部中斷腳（STM32 的 EXTI）。beam 一完成，done 腳跳變 → 觸發中斷 → 中斷裡置個 flag / 給信號量。**從此不用輪詢，零總線佔用，響應是微秒級的。**

```c
/* ── 中斷服務例程（STM32 HAL EXTI 回調）── */
volatile bool g_beam_done = false;

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == FPGA_DONE_Pin) {
        g_beam_done = true;    /* 只置旗標，重活別在中斷裡幹 */
    }
}

/* ── 觸發後，睡到中斷叫醒（配合超時）── */
tasa_fpga_ctrl_set_beam_start(&link, dir, polar, phase);
g_beam_done   = false;
uint32_t start = HAL_GetTick();
while (!g_beam_done) {
    if (HAL_GetTick() - start >= BEAM_TIMEOUT_MS) { /* 超時兜底，別把中斷當萬能 */
        break;
    }
    __WFI();   /* Wait For Interrupt：CPU 真·睡覺，中斷來了自動醒，省電 */
}
```

| ✅ 好處 | ❌ 壞處 |
|--------|--------|
| **零輪詢、零 SPI 總線浪費**，CPU 能 `__WFI` 真睡覺省電 | **強依賴硬體**：FPGA 得引出 done 腳、板子得布線、MCU 得留中斷腳 |
| 響應快到微秒級，done 一來立刻知道 | 中斷處理要小心：`volatile`、去抖、中斷優先級、共享數據競態 |
| 是 Lv6/Lv7 事件驅動的基礎 | 仍建議留超時兜底（信號線/FPGA 掛了咋辦），純中斷不設防會吊死 |

> **老哥點評**：這是「輪詢」和「事件驅動」的分界線。前提是**硬體得配合**——沒有 done 腳，這套就是空中樓閣，趁早跟硬體的人把這根線的事兒敲定。有這根線，你就從「一直問」升級成「等通知」，境界不一樣了。

---

## Lv 6 — RTOS 信號量 + 中斷喚醒（🔴 高級：多任務系統標配）

**思路**：上了 RTOS（FreeRTOS / Zephyr / ThreadX 之類），Lv5 的「置 flag + `__WFI` 空轉」就能升級成**真正的任務阻塞**：調用任務阻塞在信號量上，**期間 CPU 完全讓給別的任務**；EXTI 中斷（或一個專職輪詢任務）在 done 時 give 信號量把它叫醒。超時交給 RTOS 原生支持，一個參數搞定。

> ⚠️ 注意：本項目示例板 STM32H743 目前是**裸機**，沒有 RTOS。這一級是「往上走、上了 RTOS 之後」的玩法。

```c
/* ── 初始化：建個二值信號量 ── */
SemaphoreHandle_t beam_done_sem;   /* xSemaphoreCreateBinary() */

/* ── 中斷裡：從 ISR 版本 give，並按需觸發任務切換 ── */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == FPGA_DONE_Pin) {
        BaseType_t hp_woken = pdFALSE;
        xSemaphoreGiveFromISR(beam_done_sem, &hp_woken);
        portYIELD_FROM_ISR(hp_woken);   /* 若喚醒了更高優先級任務，立刻切過去 */
    }
}

/* ── 業務任務裡：阻塞等，CPU 讓給別人 ── */
tasa_fpga_ctrl_set_beam_start(&link, dir, polar, phase);
if (xSemaphoreTake(beam_done_sem, pdMS_TO_TICKS(BEAM_TIMEOUT_MS)) == pdTRUE) {
    /* done：beam 設好了 */
} else {
    /* 逾時：RTOS 原生超時，乾淨俐落，不用自己數 tick */
}
```

沒 done 硬體腳也能玩：起一個**專職輪詢任務**，它內部用 Lv3 的退避去查，查到就 give 信號量——這樣「輪詢的醜活」被隔離在一個低優先級任務裡，業務任務照樣優雅阻塞、不知情。

| ✅ 好處 | ❌ 壞處 |
|--------|--------|
| 阻塞期間 CPU 100% 讓給其他任務，多任務系統的標準解 | **得先有 RTOS**，裸機項目為這個上 RTOS 是本末倒置 |
| 超時是 RTOS 原生的，代碼乾淨 | 信號量/任務通知/優先級反轉這些 RTOS 概念，得懂 |
| `task notification` 還能比信號量更輕更快 | 中斷與任務的同步要嚴謹，寫錯了就是玄學死鎖 |

> **老哥點評**：項目已經在 RTOS 上了，就該這麼幹，別在任務裡忙等，那是糟蹋 RTOS。純裸機就別為這一個功能硬上 RTOS，殺雞用牛刀。`FreeRTOS` 裡 **task notification** 通常比二值信號量更香，值得查一下。

---

## Lv 7 — DMA + 全事件驅動（⚫ 資深：優雅的極致，也是過度設計的深淵）

**思路**：把最後一塊忙等也幹掉——連 **SPI 傳輸本身都不阻塞 CPU**。用 DMA 搬 SPI 數據，傳輸完成觸發中斷，整條鏈路由一個狀態機驅動，全程零忙等、零阻塞、回調式完成通知。

流程大概是：

```
set_beam_start()
   └─► HAL_SPI_TransmitReceive_DMA()      // DMA 搬數據，CPU 立刻脫身去幹別的
          └─► HAL_SPI_TxRxCpltCallback()  // 傳輸完成中斷 → 狀態機推進
                 └─► 讀回 bit 3 狀態
                        ├─ busy → 掛個定時器，過會兒再發起下一次 DMA 讀
                        └─ done → 調用用戶註冊的完成回調 on_beam_done()
```

配合 Lv5 的 done 中斷腳，甚至連「反覆讀狀態」都省了：觸發完直接等 done 中斷，中斷裡跑完成回調。**整個 beam 流程 CPU 幾乎零介入。**

| ✅ 好處 | ❌ 壞處 |
|--------|--------|
| CPU 佔用幾乎為零，吞吐/併發拉滿 | **複雜度爆炸**：DMA 配置、cache 一致性（H7 的 D-Cache 坑到你哭）、中斷嵌套、狀態機 |
| 最優雅的事件驅動架構，適合高頻大量 beam 切換 | 難寫更難調，出 bug 全是偶發的玄學問題 |
| 與現代 SoC 的低功耗/高性能設計哲學契合 | **對「偶爾設一次 beam」的場景就是純過度設計** |

> **老哥點評**：這是「炫技天花板」，也是**過度設計的重災區**。你要是一秒切幾百次 beam、或者做通用高性能框架，值得。要只是偶爾設個 beam 等它好——**打住，回去用 Lv2 或 Lv4**，別給自己刨坑。STM32H7 的 DMA + D-Cache 一致性能坑死人，沒金剛鑽別攬這瓷器活。

---

## 那到底選哪個？（TL;DR 選型指南）

別糾結，對號入座：

| 你的處境 | 直接上 |
|---------|--------|
| **就想改一處、立刻不丟人** | **Lv2 時間戳超時** ⭐（性價比之王，先上這個） |
| 在乎功耗、beam 完成時間忽快忽慢 | Lv2 + Lv3 退避 |
| 裸機 superloop，不想被一次 beam 卡死全家 | **Lv4 非阻塞狀態機**（項目 API 已備好） |
| FPGA 有 done 引腳（或能加） | Lv5 GPIO 中斷 |
| 已經在跑 RTOS | Lv6 信號量/任務通知 + 中斷喚醒 |
| 高頻切 beam / 做通用高性能框架 | Lv7 DMA 事件驅動（想清楚再上，別過度設計） |

**通用鐵律**：

1. **先把「圈數」換成「時間」**（Lv0 → Lv2），這是所有人的第一步，投入最小收益最大。
2. **超時永遠要有**，`poll_max == 0` 那種「無限等」的雷，一個都別留。
3. **能不阻塞就不阻塞**（Lv4 起），這是嵌入式從「能跑」到「能打」的分水嶺。
4. **輪詢升級成中斷**（Lv5 起）要硬體配合，趁早跟硬體的人把 done 引腳的事敲死。
5. **別為了炫技上 Lv7**，過度設計比 `poll_max` 更害人——那至少還簡單。

---

## 附：實現任何一級之前，先把「時間能力」注入進去

再強調一遍地基（Lv2 以上都要）：這是**平台無關 middleware**，`middleware 只定義需要什麼能力，平台負責怎麼實現`。給 [`tasa_fpga_dev_t`](../include/tasa_fpga_link.h#L32) 加回調，別在這層 `#include` 任何 HAL：

```c
/* include/tasa_fpga_link.h */
typedef struct {
    int (*gpio_set_mux)(void* ctx, uint8_t mode_bits);
    int (*spi_xfer)(void* ctx, const uint8_t* tx, uint8_t* rx, size_t len);
    uint32_t (*get_tick_ms)(void* ctx);            /* Lv2/Lv3 需要 */
    void     (*delay_ms)(void* ctx, uint32_t ms);  /* Lv1/Lv3 需要（可選） */
    void* ctx;
} tasa_fpga_dev_t;
```

板級（STM32 示例）填充：

```c
static uint32_t stm32_get_tick_ms(void* ctx) { (void)ctx; return HAL_GetTick(); }
static void     stm32_delay_ms(void* ctx, uint32_t ms) { (void)ctx; HAL_Delay(ms); }

static tasa_fpga_dev_t link = {
    .gpio_set_mux = board_gpio_set_mux,
    .spi_xfer     = board_spi_xfer,
    .get_tick_ms  = stm32_get_tick_ms,
    .delay_ms     = stm32_delay_ms,
    .ctx          = NULL,
};
```

這樣一來，middleware 該用時間就用回調，該睡就讓平台睡，自己一行 HAL 代碼都不碰——換到別的 MCU、換到 Linux、換到單元測試（塞個假 tick）全都能跑。**這才是這個 middleware 平台無關設計的正確延續。**

---

*相關文件：[`src/tasa_fpga_ctrl.c`](../src/tasa_fpga_ctrl.c) · [`include/tasa_fpga_ctrl.h`](../include/tasa_fpga_ctrl.h) · [`include/tasa_fpga_link.h`](../include/tasa_fpga_link.h)*
