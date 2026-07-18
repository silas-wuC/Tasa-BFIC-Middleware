# 等 FPGA「做完」的正确姿势：poll_max 到底烂在哪，从裸机到 RTOS 全套替代方案

> 針對 `[tasa_fpga_ctrl_set_beam()](../src/tasa_fpga_ctrl.c)` 裡那個 `poll_max` 迴圈計數輪詢。
> 從新手一眼看懂，到資深工程師的事件驅動 + RTOS 玩法，全給你排成一條階梯，附好壞與難度對比。
> **本項目示例板 STM32H743 現在是裸機，但以後大概率會上 RTOS**，所以 RTOS 那一整套原語這篇一次性給你鋪全，別到時候現抓瞎。

---

## 0. 先搞明白：现在这套 `poll_max` 是干嘛的，坑在哪

現在的代碼長這樣（`[src/tasa_fpga_ctrl.c:156](../src/tasa_fpga_ctrl.c#L156-L169)`）：

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

翻譯成人話：**設完 beam 之後，瘋狂讀 FPGA 的 bit 3 狀態位，讀到「done」就返回，讀滿** `poll_max` **次還沒 done 就報 timeout。** `poll_max == 0` 表示「老子讀到天荒地老也不放棄」。

臥槽，這寫法能跑，但一堆坑，挨個給你點出來：


| #   | 坑                       | 為啥膈應                                                                                                                                                                                                                   |
| --- | ----------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | **數的是圈數，不是時間**          | `poll_max` 是迴圈次數，不是毫秒。真實等待時長 = `poll_max` × 一次 SPI 讀暫存器的耗時。而這耗時取決於 SPI 時鐘頻率、幀長、CPU 主頻、總線有沒有人搶。換塊板子、SPI 從 10MHz 調到 1MHz，同樣 `poll_max=1000` 可能是 5ms 也可能是 500ms。**不可預測、不可移植**。頭文件自己都招了：`bounds SPI reads, not wall time`。 |
| 2   | **純忙等（busy-wait）**      | 兩圈之間沒有任何喘息，CPU 100% 空轉狂讀，SPI 總線被無意義的 poll 打滿，功耗拉滿。單核裸機上，這段時間整個系統就是塊板磚。                                                                                                                                                 |
| 3   | `poll_max == 0` **是顆雷** | FPGA 要是掛了 / done 位永遠不來，這就是個**死循環**，直接把系統焊死，看門狗不咬你就等著重啟吧。                                                                                                                                                               |
| 4   | **純阻塞**                 | 函數不返回，調用者啥也幹不了。裸機 superloop 裡其他任務全餓死，UART 掉包、按鍵沒反應。RTOS 裡雖然只阻自己這個任務，但忙等會霸著 CPU 不讓出，把 RTOS 的調度優勢直接作廢。                                                                                                                 |
| 5   | **策略和機制混在一起還甩鍋給調用者**    | 「該等多久」這種策略問題，被塞成一個玄學數字 `poll_max`，每個調用點自己猜。今天猜 1000，明天上量產發現不夠，改 5000，屎山越堆越高。                                                                                                                                           |


**一句話：**`poll_max` **不是「錯」，是「原始」。** 它是「連時間都拿不到」時的兜底寫法。下面這條階梯，就是帶你從這個泥坑一級一級往上爬，一直爬到 RTOS 事件驅動的天花板。

---



## 階梯總覽（先看這張，細節在後面）


| Lv  | 方案                   | 一句話原理               | 難度  | CPU 佔用 | SPI 總線 | 時間可預測 | 可移植     | 額外依賴           | 阻塞?        |
| --- | -------------------- | ------------------- | --- | ------ | ------ | ----- | ------- | -------------- | ---------- |
| 0   | **現狀：圈數輪詢**          | 讀滿 N 次就放棄           | 🟢  | 爆滿     | 爆滿     | ❌     | ✅       | 無              | 是          |
| 1   | **圈數 + 固定延時**        | 每圈之間睡 1ms           | 🟢  | 高      | 低      | 半吊子   | ⚠️      | delay 回調       | 是          |
| 2   | **時間戳超時** ⭐          | 用 tick 算真實 deadline | 🟡  | 高      | 中      | ✅     | ✅       | tick 回調        | 是          |
| 3   | **超時 + 退避節流**        | 間隔由短到長遞增            | 🟠  | 中      | 低      | ✅     | ✅       | tick + delay   | 是          |
| 4   | **非阻塞狀態機**           | 拆成 start / poll 兩半  | 🟠  | 低      | 低      | ✅     | ✅       | 調用者主循環         | **否**      |
| 5   | **GPIO done 腳 + 中斷** | done 邊沿觸發中斷         | 🔴  | 極低     | 零      | ✅     | ⚠️硬體    | FPGA 引腳 + EXTI | 可選         |
| 6   | **RTOS 阻塞等待（全家桶）**   | 阻塞讓出 CPU，事件叫醒       | 🔴  | 極低     | 零      | ✅     | ⚠️需 RTOS | RTOS + 中斷      | 是(只阻自己)    |
| 7   | **DMA + 全事件驅動**      | 整條鏈路零忙等零阻塞          | ⚫   | 極低     | 零      | ✅     | ⚠️硬體+框架 | DMA + 中斷 + 狀態機 | 否          |


難度圖例：🟢 新手 / 🟡 入門 / 🟠 中級 / 🔴 高級 / ⚫ 資深（過度設計預警）

**Lv6 是這篇的重頭戲**——它不是一個方案，是「上了 RTOS 之後，你手裡多出來的一整箱工具」。下面會拆成 6a~6f 逐個講。

---



## ⚠️ 動手前必讀：這層根本沒有「時間」，也沒有「操作系統」

在往下抄代碼之前，先記住一個**架構硬約束**：

`tasa_fpga_dev_t`（`[include/tasa_fpga_link.h:32](../include/tasa_fpga_link.h#L32-L44)`）目前只有兩個函數指針：

```c
typedef struct {
    int (*gpio_set_mux)(void* ctx, uint8_t mode_bits);
    int (*spi_xfer)(void* ctx, const uint8_t* tx, uint8_t* rx, size_t len);
    void* ctx;
} tasa_fpga_dev_t;
```

**沒有** `get_tick`**，沒有** `delay`**，更沒有任何 RTOS 的影子。** 這就是為啥當初只能用「數圈數」這種爛招——它連當前時間都拿不到。

所以 Lv1 往上，凡是要碰「真實時間」的方案，第一步都得**給 dev 注入時間能力**。但這是個平台無關的 middleware，**絕對不能**在這層直接 `#include "stm32h7xx_hal.h"` 然後調 `HAL_GetTick()`，更**不能** `#include "FreeRTOS.h"`——那就把整個 middleware 焊死在某個平台/某個 RTOS 上了，別的地方直接沒法用。

正確做法：**照著現有的回調風格，再加幾個函數指針**，讓板級 HAL（或 RTOS 適配層）自己填。這是一條貫穿 Lv2~Lv7 的地基，先看清楚：

```c
typedef struct {
    int (*gpio_set_mux)(void* ctx, uint8_t mode_bits);
    int (*spi_xfer)(void* ctx, const uint8_t* tx, uint8_t* rx, size_t len);

    /* 新增：平台注入「時間」能力，middleware 本身不碰任何 HAL / RTOS */
    uint32_t (*get_tick_ms)(void* ctx);            /* 返回單調遞增的毫秒 tick，給 Lv2/Lv3 */
    void     (*delay_ms)(void* ctx, uint32_t ms);  /* 睡一會兒；裸機=忙等，RTOS=vTaskDelay */

    /* 更進一步（Lv6）：平台注入「阻塞等一個事件」的能力。
     * middleware 只說「我要等 beam done 這個事件、最多等 timeout_ms」，
     * 至於底下是信號量、任務通知還是事件組，平台自己決定。 */
    int (*wait_event)(void* ctx, uint32_t timeout_ms);  /* 0=事件到, <0=超時/錯誤 */
    void (*signal_event)(void* ctx);                    /* 中斷/回調裡調，喚醒等待者 */

    void* ctx;
} tasa_fpga_dev_t;
```

裸機那邊，`wait_event` 就退化成「`__WFI` + 查 flag + 超時」；上了 RTOS，`wait_event` 就是 `xSemaphoreTake` / `ulTaskNotifyTake` / `osSemaphoreAcquire`，`signal_event` 就是對應的 `...FromISR` 版本。**middleware 一行都不用改，換平台只換回調實現。** 記住原則：`middleware 只定義「需要什麼能力」，平台負責「怎麼實現」`。

> **老哥點評**：`wait_event`/`signal_event` 這對回調是為 RTOS 預留的「抽象插座」。你現在裸機可以先不填（用 Lv2~Lv5），但接口先留好，等哪天上了 RTOS，適配層一填，middleware 白嫖 RTOS 的阻塞能力，爽得很。這叫**面向未來設計，不叫過度設計**——區別在於你只加了兩個指針，沒寫一行 RTOS 代碼。

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


| ✅ 好處        | ❌ 壞處                                                                        |
| ----------- | --------------------------------------------------------------------------- |
| 改動一行，新手都能看懂 | `poll_max` 還是圈數，只是現在 ≈ 毫秒，仍然在猜                                              |
| SPI 總線不再被打滿 | **裸機的** `HAL_Delay` **本身就是 SysTick 忙等**，CPU 並沒真省下來（想真省電得 `__WFI()` 或上 RTOS） |
| 移植代價低       | 固定 1ms 間隔：beam 若 50µs 就好了，你白等近 1ms；響應和開銷沒法兼顧                                |


> **老哥點評**：這是「花五分鐘讓現狀不那麼丟人」的方案。能救急，但別停在這，它只是給屎山噴了瓶香水。**注意**：一旦上了 RTOS，這裡的 `delay_ms` 就該映射成 `vTaskDelay`（會讓出 CPU），而不是忙等——同一行代碼，RTOS 下才真省電。

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

**資深細節（別漏）**：`(uint32_t)(now - start)` 這個無符號減法是關鍵。`HAL_GetTick()` 是 32 位毫秒計數，約 49.7 天就回繞到 0。用無符號減法，即使跨越回繞點結果也對——**別手賤寫成** `now >= start + timeout_ms`**，那玩意兒一溢出就當場暴斃**。


| ✅ 好處                                 | ❌ 壞處                                         |
| ------------------------------------ | -------------------------------------------- |
| **超時是真·毫秒**，換板子/換 SPI 速率都不變，可預測、可移植  | 需要給 dev 加 `get_tick_ms` 回調（見上面架構章節）          |
| 調用方寫 `timeout_ms=50` 一眼就懂啥意思，不用猜玄學數字 | 還是忙等輪詢（沒加 delay 的話），CPU 仍在轉——可疊加 Lv1 的 delay |
| 徹底幹掉 `poll_max==0` 死循環雷（超時是強制的）      | 仍是阻塞式，不解決「阻塞整個系統」的問題                         |


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


| ✅ 好處                      | ❌ 壞處                               |
| ------------------------- | ---------------------------------- |
| 快的 beam 幾乎零延遲抓到，慢的也不燒 CPU | 代碼複雜度上來了，要調退避曲線（起始間隔、倍率、封頂）        |
| 總線/功耗開銷比固定間隔更低            | 同時要 `get_tick_ms` + `delay_ms` 倆回調 |
| 兼顧響應速度和資源佔用               | 還是阻塞式                              |


> **老哥點評**：Lv2 的親兒子，適合「beam 完成時間波動大、又在乎功耗」的場景。沒這需求就別瞎折騰，Lv2 足夠。上了 RTOS 後，這個退避輪詢最適合塞進一個**低優先級專職輪詢任務**裡（見 Lv6f），把醜活隔離掉。

---



## Lv 4 — 非阻塞狀態機（🟠 中級：裸機 superloop 的救星）

**思路**：前面全是「函數不返回、死等」。Lv4 直接把它**拆成兩半**：

- `set_beam_start()`：配置 + 觸發 Set Beam，**立刻返回**，不等。
- 調用者在自己的主循環（superloop）裡，**每圈順手查一下** done，同時自己管超時。

好消息是——**項目早埋好伏筆了**。`[tasa_fpga_ctrl_beam_is_done()](../include/tasa_fpga_ctrl.h#L341-L353)` 的注釋白紙黑字寫著：

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


| ✅ 好處                      | ❌ 壞處                                    |
| ------------------------- | --------------------------------------- |
| **不阻塞系統**，UART/按鍵/其他任務照常跑 | 調用者要自己維護狀態（`beam_pending` 這類），心智負擔轉移出去了 |
| 純軟件，不要額外硬體                | 響應延遲取決於你 superloop 一圈多長                 |
| 完美適配裸機 superloop / 協作式調度  | 多個並發 beam 請求要自己排隊管理                     |


> **老哥點評**：**沒 RTOS 又不想被一次 beam 卡死全家，這就是正解。** 阻塞 vs 非阻塞是嵌入式的分水嶺，跨過這道坎你就從「調庫的」變「懂架構的」了。項目 API 都給你留好門了，順水推舟的事。這個 `set_beam_start` 一旦有了，它也是 Lv6/Lv7 的地基——RTOS 任務和 DMA 狀態機都得先能「觸發後立刻返回」。

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


| ✅ 好處                                   | ❌ 壞處                                      |
| -------------------------------------- | ----------------------------------------- |
| **零輪詢、零 SPI 總線浪費**，CPU 能 `__WFI` 真睡覺省電 | **強依賴硬體**：FPGA 得引出 done 腳、板子得布線、MCU 得留中斷腳 |
| 響應快到微秒級，done 一來立刻知道                    | 中斷處理要小心：`volatile`、去抖、中斷優先級、共享數據競態        |
| 是 Lv6/Lv7 事件驅動的基礎                      | 仍建議留超時兜底（信號線/FPGA 掛了咋辦），純中斷不設防會吊死         |


> **老哥點評**：這是「輪詢」和「事件驅動」的分界線。前提是**硬體得配合**——沒有 done 腳，這套就是空中樓閣，趁早跟硬體的人把這根線的事兒敲定。有這根線，你就從「一直問」升級成「等通知」，境界不一樣了。**這根 done 腳同時也是 Lv6 RTOS 方案的最佳搭檔**——中斷 give 信號量、喚醒阻塞任務，一氣呵成。

---



## Lv 6 — RTOS 阻塞等待（🔴 高級：多任務系統的整箱工具）

> ⚠️ **本項目示例板 STM32H743 目前是裸機，沒有 RTOS。這一級是「往上走、上了 RTOS 之後」的完整玩法。** 你要問的「以後大概率上 RTOS」，答案全在這一章。先鋪概念，再給各 RTOS 的具體代碼。

上了 RTOS，Lv5 那個「置 flag + `__WFI` 空轉」就能升級成**真正的任務阻塞**：調用任務阻塞在某個同步對象上，**期間 CPU 完全讓給別的任務**；EXTI 中斷（或一個專職輪詢任務）在 done 時發信號把它叫醒。超時交給 RTOS 原生支持，一個參數搞定。

但 RTOS 給你的**不止「信號量」一種**。等一個「beam done 事件」，你有一整箱工具，各有各的脾氣。下面 6a~6f 逐個過。

### 先認清底層機制：所有 RTOS 都在幹同一件事

不管 FreeRTOS、Zephyr、ThreadX 還是 RT-Thread，等事件的本質都是三步：

1. **任務調用一個「阻塞獲取」原語**，帶超時 → RTOS 把這任務從就緒隊列摘掉，掛到等待隊列，**調度器切去跑別的任務**（CPU 一點不浪費）。
2. **中斷 / 另一個任務調用對應的「釋放」原語**（`...FromISR` 版本從中斷裡調）→ RTOS 把等待的任務放回就緒隊列。
3. 若被喚醒的任務優先級更高 → **中斷返回時立刻切過去**（`portYIELD_FROM_ISR`），響應是微秒級的。

超時沒到、事件也沒來，任務就一直睡著，`tickless idle` 下 MCU 甚至能深度睡眠。**這就是 RTOS 相比裸機忙等的碾壓性優勢。**


### RTOS 同步原語全家桶——等一個 beam done，用哪個？

| 原語                              | 適合場景                                       | 用在 beam 等待上的評價                                                                 |
| ------------------------------- | ------------------------------------------ | ---------------------------------------------------------------------------- |
| **二值信號量** (binary semaphore)    | 中斷 → 單個任務的「一次性通知」                          | 最直觀的入門選擇。ISR give，任務 take。但有「丟信號/重複給」的邊角問題，多數場景已被下面的任務通知取代。                    |
| **計數信號量** (counting semaphore)  | 多次事件排隊、資源池計數                              | beam 是「一次一個」，計數用不太上。除非你要緩衝多個 done 事件。                                          |
| **互斥量** (mutex)                 | 保護共享資源（如 SPI 總線本身）                        | **不是用來等 done 的**，是用來保證「同一時刻只有一個任務在操作 FPGA/SPI」。beam 場景常和信號量搭配用（見 6d 優先級反轉）。 |
| **直接任務通知** (task notification)  | 中斷 → 特定任務的輕量通知（FreeRTOS 招牌）              | ⭐ **FreeRTOS 下的首選**：比信號量快 ~45%、省一個對象的 RAM。ISR `vTaskNotifyGiveFromISR`，任務 `ulTaskNotifyTake`。 |
| **事件組 / 事件標誌** (event group)    | 等「多個條件的組合」（done AND 校准好 AND ...）          | beam 只等一個 done，用事件組是殺雞用牛刀。但若你要「等 done **或** 錯誤中斷 **或** 取消請求」，事件組的「等多個位」就香了。 |
| **隊列 / 消息隊列** (queue)           | 中斷 → 任務傳「事件 + 數據」                         | 想連 done 帶「哪個 beam、狀態碼」一起傳給任務，用隊列。純通知就沒必要，隊列更重。                                 |
| **軟件定時器** (software timer)      | 超時兜底、周期性重試                               | 配合非阻塞觸發：起個 one-shot 定時器做超時，done 中斷來了就取消它。純事件驅動風格，不佔任務。                          |
| **條件變量** (condvar，Zephyr/POSIX) | 「等某個謂詞成立」+ 互斥量                            | POSIX/Zephyr 風格。beam 場景一般用不到這麼重的抽象。                                            |

**一句話選型**：FreeRTOS 用 **直接任務通知**（6a），要等多個條件用 **事件組**（6b），要隔離輪詢醜活用 **專職輪詢任務 + 信號量**（6f）。其餘按需。

---

### Lv 6a — 直接任務通知 / 二值信號量（FreeRTOS 首選）

最經典的「中斷喚醒阻塞任務」。先給二值信號量的版本（最好懂），再給任務通知（最優）。

```c
/* ── 版本一：二值信號量（入門，好理解）── */
SemaphoreHandle_t beam_done_sem;   /* xSemaphoreCreateBinary() 初始化 */

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == FPGA_DONE_Pin) {
        BaseType_t hp_woken = pdFALSE;
        xSemaphoreGiveFromISR(beam_done_sem, &hp_woken);
        portYIELD_FROM_ISR(hp_woken);   /* 若喚醒了更高優先級任務，立刻切過去 */
    }
}

/* 業務任務裡：阻塞等，CPU 讓給別人 */
tasa_fpga_ctrl_set_beam_start(&link, dir, polar, phase);
if (xSemaphoreTake(beam_done_sem, pdMS_TO_TICKS(BEAM_TIMEOUT_MS)) == pdTRUE) {
    /* done：beam 設好了 */
} else {
    /* 逾時：RTOS 原生超時，乾淨俐落，不用自己數 tick */
}
```

```c
/* ── 版本二：直接任務通知（FreeRTOS 官方推薦，更快更省）── */
static TaskHandle_t beam_task;   /* 記下要通知的任務句柄 */

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == FPGA_DONE_Pin) {
        BaseType_t hp_woken = pdFALSE;
        vTaskNotifyGiveFromISR(beam_task, &hp_woken);   /* 不用單獨建信號量對象 */
        portYIELD_FROM_ISR(hp_woken);
    }
}

/* 業務任務裡 */
beam_task = xTaskGetCurrentTaskHandle();
tasa_fpga_ctrl_set_beam_start(&link, dir, polar, phase);
if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(BEAM_TIMEOUT_MS)) > 0) {
    /* done */
} else {
    /* timeout */
}
```

> **老哥點評**：FreeRTOS 手冊自己都說 task notification 比信號量**快約 45%、少佔一個對象的 RAM**。缺點是「一個任務同一時刻只有一個通知值」，多對多場景不好使——但 beam done 這種「中斷通知固定一個任務」的場景，它就是最優解。**能用任務通知就別建信號量。**

---

### Lv 6b — 事件組 / 事件標誌（等多個條件的組合）

當你要等的不只是「done」，而是「done **或** 錯誤 **或** 取消」——或者「done **且** 校准完成」——事件組（event group）就派上用場了。它是一組位（bit flags），任務可以「等其中任意一位」或「等全部位」。

```c
EventGroupHandle_t beam_events;   /* xEventGroupCreate() */
#define EV_BEAM_DONE   (1u << 0)
#define EV_BEAM_ERROR  (1u << 1)
#define EV_BEAM_CANCEL (1u << 2)

/* done 中斷 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == FPGA_DONE_Pin) {
        BaseType_t hp_woken = pdFALSE;
        xEventGroupSetBitsFromISR(beam_events, EV_BEAM_DONE, &hp_woken);
        portYIELD_FROM_ISR(hp_woken);
    }
}

/* 業務任務：等「done 或 error 或 cancel」任意一個 */
EventBits_t bits = xEventGroupWaitBits(
    beam_events, EV_BEAM_DONE | EV_BEAM_ERROR | EV_BEAM_CANCEL,
    pdTRUE /* 讀後清零 */, pdFALSE /* 任意一個滿足即返回 */,
    pdMS_TO_TICKS(BEAM_TIMEOUT_MS));

if (bits & EV_BEAM_DONE)  { /* 正常完成 */ }
else if (bits & EV_BEAM_ERROR)  { /* FPGA 報錯了 */ }
else if (bits & EV_BEAM_CANCEL) { /* 上層取消了這次 beam */ }
else { /* timeout：一個位都沒等到 */ }
```

> **老哥點評**：beam 只等一個 done，用事件組屬於過度設計。但一旦你的系統要「同時等多個異步事件、誰先來聽誰的」——比如 done 腳、錯誤中斷、看門狗取消——事件組的「等多位」能力就無可替代，比開一堆信號量再輪流查優雅太多。

---

### Lv 6c — 隊列 / 消息傳遞（連事件帶數據一起搬）

信號量/通知只能傳「事情發生了」，傳不了「發生了啥」。要把「哪個 beam、完成狀態、耗時多少」一起交給處理任務，用**隊列**。這也是「生產者-消費者」模型的標配。

```c
typedef struct { uint8_t beam_id; tasa_status_t status; uint32_t elapsed_ms; } beam_evt_t;
QueueHandle_t beam_q;   /* xQueueCreate(len, sizeof(beam_evt_t)) */

/* done 中斷：把事件塞進隊列 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == FPGA_DONE_Pin) {
        BaseType_t hp_woken = pdFALSE;
        beam_evt_t e = { .beam_id = g_cur_beam, .status = TASA_OK, .elapsed_ms = 0 };
        xQueueSendFromISR(beam_q, &e, &hp_woken);
        portYIELD_FROM_ISR(hp_woken);
    }
}

/* 消費任務：阻塞收，帶超時 */
beam_evt_t e;
if (xQueueReceive(beam_q, &e, pdMS_TO_TICKS(BEAM_TIMEOUT_MS)) == pdTRUE) {
    handle_beam_result(&e);   /* 拿到完整信息 */
} else {
    /* timeout */
}
```

> **老哥點評**：純「等一下 done」用隊列偏重。但如果 beam 事件要排隊處理、要帶上下文數據、要多生產者匯聚到一個消費任務——隊列是最自然的架構。FreeRTOS 的 **stream buffer / message buffer**（單生產單消費專用，更輕）也可以了解下。

---

### Lv 6d — 互斥量：別忘了保護 SPI 總線本身

前面講的都是「怎麼等 done」。但上了 RTOS 多任務，冒出個新問題：**萬一兩個任務同時要操作 FPGA/SPI 怎麼辦？** 一個任務正在跑 beam 的讀-改-寫序列，另一個任務插進來動了同一條 SPI 總線，數據就串了。這時候需要**互斥量（mutex）**把整個 beam 操作序列鎖起來。

```c
SemaphoreHandle_t spi_mutex;   /* xSemaphoreCreateMutex()，注意用 Mutex 不是 Binary */

tasa_status_t app_set_beam_safe(...) {
    xSemaphoreTake(spi_mutex, portMAX_DELAY);   /* 拿鎖，獨佔 SPI */
    tasa_status_t st = tasa_fpga_ctrl_set_beam(&link, ...);
    xSemaphoreGive(spi_mutex);                  /* 放鎖 */
    return st;
}
```

**關鍵坑——優先級反轉（priority inversion）**：低優先級任務拿著 SPI 鎖，高優先級任務要這把鎖只能乾等，中優先級任務又把低優先級的擠掉不讓它跑完釋放鎖——高優先級任務被中優先級變相卡死。**FreeRTOS 的 mutex 帶「優先級繼承」**（拿鎖的低優先級任務臨時被提到等鎖者的優先級）能緩解，**但二值信號量沒有這機制**，所以「保護資源用 mutex，事件通知用信號量/通知」——別混用。

> **老哥點評**：這是純裸機根本不用操心、一上多任務就跳出來咬你的坑。beam 序列是「讀-改-寫 + 等 done」的複合操作，天然需要原子性保護。middleware 這層**不該自己上鎖**（它不知道有沒有 RTOS），鎖應該加在應用層調用點——又一個「機制與策略分離」的例子。

---

### Lv 6e — 軟件定時器做超時兜底（純事件驅動風格）

不想讓任何任務阻塞等超時？用**軟件定時器**（software timer）：觸發 beam 時起一個 one-shot 定時器，done 中斷來了就取消它；定時器要是先到了，就在定時器回調裡宣告 timeout。全程沒有任務阻塞在等待上，最貼合「全事件驅動」哲學，也是通往 Lv7 的過渡。

```c
TimerHandle_t beam_timeout_timer;   /* xTimerCreate(..., pdFALSE /*one-shot*/, ...) */

void app_request_beam(void) {
    tasa_fpga_ctrl_set_beam_start(&link, dir, polar, phase);
    xTimerChangePeriod(beam_timeout_timer, pdMS_TO_TICKS(BEAM_TIMEOUT_MS), 0);  /* 啟動 */
}

/* done 中斷：取消超時定時器 + 通知 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == FPGA_DONE_Pin) {
        BaseType_t hp_woken = pdFALSE;
        xTimerStopFromISR(beam_timeout_timer, &hp_woken);
        /* ... 通知處理任務 done ... */
        portYIELD_FROM_ISR(hp_woken);
    }
}

/* 定時器回調（在 timer service task 上下文）：先到 = 超時 */
void beam_timeout_cb(TimerHandle_t t) { on_beam_timeout(); }
```

> **老哥點評**：軟件定時器跑在 FreeRTOS 的 timer service task 上，回調裡**別幹重活、別阻塞**，否則拖累所有定時器。適合「觸發後徹底撒手，成敗都用回調通知」的異步架構。

---

### Lv 6f — 沒有 done 硬體腳？專職輪詢任務隔離醜活

如果 FPGA **沒引出 done 腳**（Lv5 的前提不成立），RTOS 下也有優雅解：起一個**低優先級專職輪詢任務**，它內部用 Lv3 的退避去查 SPI 狀態位，查到 done 就給信號量 / 發任務通知；業務任務照樣**優雅阻塞在信號量上**，完全不知道底下是輪詢。**「輪詢的醜活」被隔離進一個低優先級任務**，高優先級業務任務該幹嘛幹嘛。

```c
/* 低優先級輪詢任務：醜活關這兒 */
void beam_poll_task(void* arg) {
    for (;;) {
        xSemaphoreTake(beam_start_sem, portMAX_DELAY);   /* 等「有 beam 要跟蹤」的信號 */
        uint32_t interval = 0;
        for (;;) {
            bool done = false;
            if (tasa_fpga_ctrl_beam_is_done(&link, &done) == TASA_OK && done) {
                xSemaphoreGive(beam_done_sem);           /* 通知業務任務：好了 */
                break;
            }
            interval = (interval == 0) ? 1 : (interval < 8 ? interval << 1 : 8);
            vTaskDelay(pdMS_TO_TICKS(interval));         /* 退避 + 讓出 CPU */
        }
    }
}
```

> **老哥點評**：這是「有 RTOS、但硬體沒 done 腳」的標準解法——輪詢無法避免，但可以把它**關進小黑屋**（低優先級任務），別讓它污染業務邏輯，也別讓它霸佔 CPU（`vTaskDelay` 會讓出）。等哪天硬體加了 done 腳，把這任務換成 Lv5 的中斷即可，業務層代碼一行不動。

---

### 各主流 RTOS 的原語對照（換 RTOS 時查這張）

以後上哪個 RTOS 還沒定？把 middleware 的 `wait_event`/`signal_event` 回調映射到對應原語就行，這張表給你打好底：

| 能力            | **FreeRTOS**                  | **CMSIS-RTOS2**（抽象層）      | **Zephyr**                     | **ThreadX**                 | **RT-Thread**            |
| ------------- | ----------------------------- | ------------------------- | ------------------------------ | --------------------------- | ------------------------ |
| 二值信號量         | `xSemaphoreTake/GiveFromISR`  | `osSemaphoreAcquire/Release` | `k_sem_take/give`              | `tx_semaphore_get/put`      | `rt_sem_take/release`    |
| 直接任務通知 ⭐      | `ulTaskNotifyTake` / `vTaskNotifyGiveFromISR` | `osThreadFlagsWait/Set`   | `k_poll` / thread wakeup       | `tx_thread_wait`（事件標誌代替）    | 無直接對應（用信號量）              |
| 事件組/標誌        | `xEventGroupWaitBits`         | `osEventFlagsWait/Set`    | `k_event_wait` / `k_poll`      | `tx_event_flags_get/set`    | `rt_event_recv/send`     |
| 消息隊列          | `xQueueReceive/SendFromISR`   | `osMessageQueueGet/Put`   | `k_msgq_get/put`               | `tx_queue_receive/send`     | `rt_mq_recv/send`        |
| 互斥量(帶優先級繼承)   | `xSemaphoreCreateMutex`       | `osMutexAcquire/Release`  | `k_mutex_lock/unlock`          | `tx_mutex_get/put`          | `rt_mutex_take/release`  |
| 軟件定時器         | `xTimerStart` / `...FromISR`  | `osTimerStart`            | `k_timer_start`                | `tx_timer_activate`         | `rt_timer_start`         |
| 超時單位          | tick（`pdMS_TO_TICKS`）         | ms（`osWaitForever`=阻塞）    | `K_MSEC()` / `K_FOREVER`       | tick（`TX_WAIT_FOREVER`）     | tick（`RT_WAITING_FOREVER`）|

> **老哥建議**：想**一次寫死、換 RTOS 不用改業務代碼**，就選 **CMSIS-RTOS2** 這層 ARM 官方抽象——底下墊 FreeRTOS 也行、墊 RTX5 也行，API 一套。STM32CubeMX 生成的 RTOS 代碼默認就是 CMSIS-RTOS2 包在 FreeRTOS 上，跟本項目的 STM32 生態最搭。要極致性能/最省 RAM，就直接裸調 FreeRTOS 的 task notification。


| ✅ Lv6 整體好處                       | ❌ Lv6 整體壞處                            |
| ------------------------------- | ------------------------------------ |
| 阻塞期間 CPU 100% 讓給其他任務，多任務系統的標準解 | **得先有 RTOS**，裸機項目為這一個功能上 RTOS 是本末倒置 |
| 超時是 RTOS 原生的，代碼乾淨，`tickless` 下能深睡省電 | 信號量/通知/事件組/優先級反轉一堆概念，得懂，寫錯就是玄學死鎖    |
| 工具箱豐富：等一個 / 等多個 / 帶數據 / 保護資源全都有 | 中斷與任務的同步要嚴謹，`...FromISR` 版本別調錯       |


> **老哥總評**：項目一旦上了 RTOS，就**別在任務裡忙等**，那是糟蹋 RTOS。首選 **task notification（6a）**，等多條件用 **事件組（6b）**，沒 done 腳就 **輪詢任務隔離（6f）**，別忘了 **mutex 保護 SPI 總線（6d）**。純裸機就別為這一個功能硬上 RTOS，殺雞用牛刀。

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

配合 Lv5 的 done 中斷腳，甚至連「反覆讀狀態」都省了：觸發完直接等 done 中斷，中斷裡跑完成回調。**整個 beam 流程 CPU 幾乎零介入。** 上了 RTOS 的話，DMA 完成中斷還能直接 give 信號量喚醒任務（Lv6 + Lv7 合體），是高頻切 beam 場景的終極形態。


| ✅ 好處                      | ❌ 壞處                                                   |
| ------------------------- | ------------------------------------------------------ |
| CPU 佔用幾乎為零，吞吐/併發拉滿        | **複雜度爆炸**：DMA 配置、cache 一致性（H7 的 D-Cache 坑到你哭）、中斷嵌套、狀態機 |
| 最優雅的事件驅動架構，適合高頻大量 beam 切換 | 難寫更難調，出 bug 全是偶發的玄學問題                                  |
| 與現代 SoC 的低功耗/高性能設計哲學契合    | **對「偶爾設一次 beam」的場景就是純過度設計**                            |


> **老哥點評**：這是「炫技天花板」，也是**過度設計的重災區**。你要是一秒切幾百次 beam、或者做通用高性能框架，值得。要只是偶爾設個 beam 等它好——**打住，回去用 Lv2 或 Lv4**，別給自己刨坑。STM32H7 的 DMA + D-Cache 一致性能坑死人（DMA 緩衝區記得放進 non-cacheable MPU 區域，或老老實實 `SCB_CleanInvalidateDCache`），沒金剛鑽別攬這瓷器活。

---



## 那到底選哪个？（TL;DR 选型指南）

別糾結，對號入座：


| 你的處境                            | 直接上                              |
| ------------------------------- | ------------------------------- |
| **就想改一處、立刻不丟人**                 | **Lv2 時間戳超時** ⭐（性價比之王，先上這個）     |
| 在乎功耗、beam 完成時間忽快忽慢              | Lv2 + Lv3 退避                    |
| 裸機 superloop，不想被一次 beam 卡死全家    | **Lv4 非阻塞狀態機**（項目 API 已備好）      |
| FPGA 有 done 引腳（或能加）             | Lv5 GPIO 中斷                     |
| **已經/即將上 RTOS，等單個 done**        | **Lv6a 任務通知/信號量 + 中斷喚醒**        |
| RTOS，要同時等 done/錯誤/取消 多個事件       | Lv6b 事件組                        |
| RTOS，事件要帶數據、多生產者匯聚              | Lv6c 隊列                         |
| RTOS，多任務搶 SPI 總線                | Lv6d 互斥量（別忘了！配優先級繼承）            |
| RTOS，但 FPGA 沒 done 腳            | Lv6f 低優先級輪詢任務隔離醜活               |
| 高頻切 beam / 做通用高性能框架             | Lv7 DMA 事件驅動（想清楚再上，別過度設計）       |


**通用鐵律**：

1. **先把「圈數」換成「時間」**（Lv0 → Lv2），這是所有人的第一步，投入最小收益最大。
2. **超時永遠要有**，`poll_max == 0` 那種「無限等」的雷，一個都別留。裸機用時間戳，RTOS 用原生超時參數。
3. **能不阻塞就不阻塞**（Lv4 起），這是嵌入式從「能跑」到「能打」的分水嶺。
4. **輪詢升級成中斷**（Lv5 起）要硬體配合，趁早跟硬體的人把 done 引腳的事敲死——它同時是 Lv6/Lv7 的地基。
5. **上了 RTOS 就別在任務裡忙等**（Lv6），選對原語：等一個用任務通知、等多個用事件組、保護資源用互斥量。忙等會把 RTOS 的調度優勢作廢。
6. **別為了炫技上 Lv7**，過度設計比 `poll_max` 更害人——那至少還簡單。

**給未來上 RTOS 的一條總綱**：中間件這層**永遠別直接碰 RTOS 頭文件**。把「等一個事件」「發一個信號」抽象成 `wait_event`/`signal_event` 回調（見架構章節），裸機填 `__WFI`+flag，RTOS 填任務通知，換 RTOS 只換適配層。這樣 Lv2 的時間戳、Lv6 的阻塞、Lv7 的 DMA，全都能在同一套 middleware 上按平台自由切換。**這才是這個平台無關 middleware 的正確活法。**

---

*相關文件：*`[src/tasa_fpga_ctrl.c](../src/tasa_fpga_ctrl.c)` *·* `[include/tasa_fpga_ctrl.h](../include/tasa_fpga_ctrl.h)` *·* `[include/tasa_fpga_link.h](../include/tasa_fpga_link.h)`
