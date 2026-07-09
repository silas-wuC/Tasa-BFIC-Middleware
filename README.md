# Tasa-BFIC-Middleware

---

MCU↔FPGA中間層。MCU 不直觸 BFIC（F6222），凡涉 BFIC 之 SPI 皆經 FPGA 轉發。

## FPGA Reference

Register map, interface, and protocol definitions for BFIC / FPGA:

**[FPGA Reference Spreadsheet](https://docs.google.com/spreadsheets/d/1mG5EBg3hF-dM5JgIYT1P0irBdOqtw-7Vai5JK5146gU/edit?gid=1990517554#gid=1990517554)**

## 分層

```
應用層（波束控制邏輯）
   │
   ├─ f6222.h API（third_party/RENESAS-F6222-Driver，不改）
   │     └─ tasa_bfic_bridge.c（實作 spi_xfer callback）
   │           └─ tasa_fpga_link.c：tasa_fpga_mux_xfer()
   │
   └─ tasa_fpga_system.h（Beam/Pol ID、BFIC Reset、版本）
   └─ tasa_fpga_i2c.h（PMIC 等 I2C 裝置橋接）
         └─ tasa_fpga_link.c：tasa_fpga_sys_write/read()
               └─ MCU SPI HAL（呼叫者提供，未含於本庫）── FPGA ── BFIC 陣列
```

## 檔案

| 檔案 | 職責 |
|------|------|
| `include/tasa_fpga_link.h`, `src/tasa_fpga_link.c` | Command byte 封裝；Ctrl-FPGA 暫存器存取（`tasa_fpga_sys_write/read`）；MUX 直通（`tasa_fpga_mux_xfer`） |
| `include/tasa_bfic_bridge.h`, `src/tasa_bfic_bridge.c` | 接通 F6222 driver 之 `f6222_dev_t.spi_xfer`，令既有 `f6222_*` API 原封不動運作 |
| `include/tasa_fpga_system.h`, `src/tasa_fpga_system.c` | System 暫存器塊：版本、DIPSwitch、BFIC Reset（A-D 四區）、Pol/Beam ID、Beam mode 自動設定 |
| `include/tasa_fpga_i2c.h`, `src/tasa_fpga_i2c.c` | I2C State/Write/Result 暫存器塊橋接，供存取 FPGA 側 I2C 裝置（如 PMIC，位址 0x14） |
| `third_party/RENESAS-F6222-Driver` | git submodule，BFIC 原生 SPI frame 組裝，平台無關 |

## 協定摘要（v0.01）

Command byte 首位元：`Bit7=1` 進 Ctrl-FPGA 暫存器空間（`Register_Mode` 獨熱碼選 System/I2C State/I2C Write/I2C Result 四區之一，`Bit4` 為 R/W̄）；`Bit7=0` 為 MUX 直通，`Bit[4:1]` 選通道，其後直接接續 BFIC 原生 SPI frame。

## 待確認事項（非阻塞，供對照 FPGA 端）

- MUX 直通側之通道定址：v0.01 表格空白，本庫假設為單一 4-bit 通道選擇；若需覆蓋 4 個 AiP tile（A/B/C/D）× 9 顆 BF，恐超出 4-bit 範圍，需與 FPGA 端另議定址機制。
- `Beam mode` 暫存器 Bit3（`Auto_mode_status`）與 Bit4（`Start_Set_Beam`）之極性，原表文字自相矛盾，`tasa_fpga_system.h` 內已註記，需以實機驗證。
- `BFIC Reset A-D` 各位元 1=解除重置或 1=保持重置，原表未載明，預設值 `0x1F`。

## 建置

無正式建置系統；`compile_commands.json` 供 clangd 語法檢查用，非量產工具鏈。實際專案應將本庫與 `third_party/RENESAS-F6222-Driver` 一併納入 MCU 端建置（如 STM32CubeIDE）。
