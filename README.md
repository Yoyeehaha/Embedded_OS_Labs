# Embedded_OS_Labs

本 repository 彙整了嵌入式作業系統課程的各項實驗專案，主要以 STM32F407VG 開發版為硬體平台，並使用 STM32CubeIDE 1.17.0 進行開發、編譯與維護。專案內容涵蓋 FreeRTOS 任務設計、同步機制、外部中斷、檔案系統整合與周邊控制等主題，目標是建立完整的嵌入式系統實作與觀察基礎。

## 硬體與開發環境

- 開發板：STM32F407VG
- 開發工具：STM32CubeIDE 1.17.0
- 作業系統主題：FreeRTOS 任務排程、同步機制、外部中斷、檔案系統與音訊播放

## 專案結構

- `Code_space/`：所有 Lab 的主要開發內容，每個 Lab 各自包含 CubeIDE 專案、`Core/`、`Drivers/`、`FreeRTOS/` 等原始碼與建置設定。
- `Code_space/Lab_1` ~ `Code_space/Lab_5`：對應各次實驗的完整專案資料夾。
- `Lab_Materials/`：課程講義、實驗參考資料與相關素材。
- `Lab_Reports/`：實驗報告、整理文件與撰寫內容。

## 各 Lab 內容

### Lab 1 - LED 與按鍵控制

使用 FreeRTOS 建立按鍵與 LED 任務，透過 queue 傳遞訊息控制 LED 模式切換。短按可切換顯示模式，長按可暫停或恢復 LED 任務。

### Lab 2 - Task 排程與監控

透過多個 FreeRTOS 任務控制不同 LED 閃爍週期，並加入 task monitor 與延遲任務，觀察任務排程、優先權與系統執行情況。

### Lab 3 - 外部中斷與 Semaphore 同步

以感測器中斷觸發任務處理流程，搭配 semaphore 做 ISR 與任務間同步，並控制紅、綠、橘 LED 的行為，示範中斷驅動設計。

### Lab 4 - 任務建立、刪除與 FreeRTOS 資源觀察

建立多個測試任務並觀察 LED 與 FreeRTOS 的資源使用狀態，包含任務刪除與 free list 輸出，用來理解系統記憶體與任務管理行為。

### Lab 5 - FATFS、音訊播放與按鍵事件控制

整合 FATFS、UART log、音訊播放與按鍵事件辨識，支援單擊、雙擊與長按等操作模式，並可進行播放/暫停、切歌與音量控制，同時將操作記錄寫入 log 檔。

## 期末專案

期末專案請參考：[Embedded_FinalProject](https://github.com/timchen1015/Embedded_FinalProject)

此專案主要使用 OV7670 影像模組、ILI9341 LCD 與 STM32F407VG 開發版實作駕駛疲勞偵測系統，並延伸整合影像擷取、LCD 顯示與即時判斷流程，作為課程期末整合應用。

## 備註

- 各 Lab 皆以 STM32F407VG 平台為基礎，並使用 CubeIDE 產生的專案結構。
- 若要重新建置專案，請以對應 Lab 目錄中的 `.ioc` 檔案開啟 STM32CubeIDE。