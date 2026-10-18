# UltraLowLatencyRTSPPlayer 實做計畫 (Implementation Plan)

本專案旨在為 Android (手機 / 平板 / 電視盒 Android TV) 開發一款端到端延遲 $\le$ 100ms 之單路極限低延遲 RTSP / Direct-UDP 視訊串流播放器 (FPV / 工業操控版)。

---

## 一、 系統架構規劃與技術選型

```
┌────────────────────────────────────────────────────────┐
│             應用展示層 (Kotlin / Jetpack)               │
│  - 跨終端 UI / D-pad 焦點 / DataStore URL 歷史紀錄      │
│  - OSD HUD 懸浮層 / 手勢與遙控器快捷鍵監聽              │
└───────────────────────────┬────────────────────────────┘
                            │ JNI 控制介面 (Control & Telemetry Only)
┌───────────────────────────┴────────────────────────────┐
│             核心多媒體引擎層 (Modern C++ NDK)           │
│  ┌──────────────────────────────────────────────────┐  │
│  │ 1. 網路傳輸端 (RTSP-UDP / RTSP-TCP / Direct-UDP) │  │
│  └────────────────────────┬─────────────────────────┘  │
│                           ▼                            │
│  ┌──────────────────────────────────────────────────┐  │
│  │ 2. 編碼探測器 (Codec Prober: SPS/PPS/VPS 解析)    │  │
│  └──────┬───────────────────────────────────┬───────┘  │
│         │ 原始編碼串流 (Raw NALU)           │          │
│         ▼                                   ▼          │
│  ┌──────────────┐                 ┌─────────────────┐  │
│  │ 3. MP4 Muxer │ (純淨錄影)      │ 4. 解碼決策模組 │  │
│  └──────────────┘                 └────────┬────────┘  │
│                                            │           │
│         ┌──────────────────────────────────┴────────┐  │
│         ▼                                           ▼  │
│  ┌──────────────┐                         ┌──────────┐ │
│  │ NDK 硬體解碼 │                         │ CPU 軟解 │ │
│  │ (AMediaCodec)│                         │(Fallback)│ │
│  └──────┬───────┘                         └────┬─────┘ │
│         │ Zero-Copy (Direct to Surface)        │ PBO   │
│         ▼                                      ▼       │
│  ┌──────────────────────────────────────────────────┐  │
│  │ 5. 渲染與截圖 (ANativeWindow / Clean JPEG Capture)│  │
│  └──────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────┘
```

### 1.1 核心架構與各層職責
1. **底層 C++ NDK 核心 (Modern C++17)**:
   - **POSIX Signal Handler**: 攔截 SIGSEGV / SIGBUS / SIGABRT，確保硬體解碼驅動異常時不閃退，能拋出 JNI 例外或安全切換軟解。
   - **Pre-allocated Ring Buffer Pool**: 預配置環形緩衝池，杜絕運作期間頻繁 `malloc`/`free` 與記憶體碎片化。
   - **Network Ingestion (拉流端)**:
     - `RTSP over UDP`: 標準 RTSP 1.0 握手 + UDP 接收 RTP/RTCP。
     - `RTSP over TCP`: RTSP Interleaved 模式，TCP 通道多工傳輸。
     - `Direct UDP Raw Stream`: 直接監聽 UDP 埠（預設 5600），以 `0x00000001` Start Code 解析純 NALU。
     - 支援 H.264 (RFC 6184 / FU-A) 與 H.265 (RFC 7798 / FU) RTP 解包。
   - **Codec Prober (探測先決)**:
     - 探測前數個 NALU：H.264 (SPS/PPS), H.265 (VPS/SPS/PPS), AV1 (Sequence Header)。
     - 動態解析寬高與編碼格式，查詢晶片支援後才實例化解碼器。
   - **Drop-to-IDR 追時機制**:
     - 緩衝佇列長度嚴格限制為 0~1 幀。
     - 積壓 > 60ms 立即 flush 解碼器並丟棄後續 P/B 幀，直達下一個 IDR 關鍵幀。
   - **500ms Watchdog 畫面凍結與自動重連**:
     - 500ms 無封包即鎖定最後一幀不刷黑屏，回調 Kotlin 顯示 `⚠️ Disconnection (Reconnecting...)`。
     - 背景執行指數退避重連 (200ms -> 500ms -> 1s -> 2s)。
   - **NDK AMediaCodec Zero-Copy 硬體解碼**:
     - 透過 `ANativeWindow_fromSurface` 直接綁定 `AMediaCodec`，解碼完直接輸出至螢幕，JVM/CPU 零記憶體拷貝。
   - **純淨無損錄影 (Clean MP4 Muxer)**:
     - 透過 NDK `AMediaMuxer`，解封裝後 Raw NALU 直接寫入 MP4，零二次轉碼，絕不包含 OSD 水印。
   - **純淨無損截圖 (Clean JPEG Screenshot)**:
     - 直接截取視訊解碼原始像素並以 libjpeg/壓縮演算法輸出 95% 高畫質 `.jpg` 至 `Pictures/FPV_Snapshots`，不包含 UI/OSD。
   - **全維度 Telemetry 效能遙測**:
     - 收集 Bitrate、FPS (Render/Source)、Decode Time、Drop Count、App CPU %、Total CPU %、RAM (PSS/RSS)、GPU 狀態。

2. **Kotlin 展示與控制層**:
   - **跨端支援**: Phone / Tablet / Android TV (D-pad 焦點移動與按鍵監聽)。
   - **UI 佈局**:
     - 底層全螢幕 `SurfaceView`。
     - 右上角非侵入式 OSD HUD（半透明黑底、Monospace 字體、雙指點擊或遙控器 Menu 鍵開關）。
     - 頂部中央 Amber 琥珀色斷線警告 `⚠️ Disconnection (Reconnecting...)`。
     - 頂部 URL 列與 LRU 歷史下拉選單（最多 5 筆，冷啟動自動載入上次 URL 並播放）。
     - 錄影與截圖控制按鈕。
     - 設定對話框（長按 1.5 秒或選單開啟）：切換 2x2 管線與傳輸模式。

3. **建置與版本控管**:
   - `VerChangeLog.md` 作為版號 Single Source of Truth，Gradle 動態解析 `versionName` 與 `versionCode`。
   - 包含簽章設定 (Keystore)，產生具有有效憑證的 Debug APK 與 Release APK。
   - 提供標準 `Dockerfile` 支援容器化編譯。

---

## 二、 實做階段與執行計畫 (Milestones)

### Phase 1: 專案基礎骨架與建置系統初始化
- [x] 建立 Gradle Android 專案結構 (`settings.gradle.kts`, `build.gradle.kts`, `app/build.gradle.kts`, `gradle.properties`)。
- [x] 下載/配置 Gradle Wrapper (8.x)。
- [x] 建立 `VerChangeLog.md`，並在 `app/build.gradle.kts` 中配置版本自動解析邏輯。
- [x] 建立簽名金鑰庫 (`keystore`)，設定 Debug 與 Release 簽章憑證。
- [x] 建立 `Dockerfile` (Ubuntu 22.04, OpenJDK 17, Android SDK 34, NDK, CMake) 及建置腳本。
- [x] Git Commit 紀錄 Phase 1。

### Phase 2: C++ NDK 核心多媒體引擎實做
- [x] 配置 `CMakeLists.txt`，連結 `mediandk`, `android`, `log`, `jnigraphics`。
- [x] 實做 POSIX Signal Handler 與 Crash 防護機制。
- [x] 實做 Pre-allocated Ring Buffer Pool 記憶體管理池。
- [x] 實做網路傳輸層：
  - `SocketTransport`: TCP / UDP 通訊封裝與非阻塞 I/O。
  - `RTSPClient`: RTSP 1.0 (OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN, Interleaved TCP / UDP RTP)。
  - `DirectUdpReceiver`: Direct UDP Raw NALU 監聽與 Start Code 分割器。
  - `RtpDepacketizer`: H.264 (RFC 6184) 與 H.265 (RFC 7798) RTP 解包 (Single NAL, FU-A/FU, STAP-A)。
- [x] 實做 Codec Prober (SPS/PPS/VPS 解析，識別編碼與寬高，探測先決)。
- [x] 實做 NDK `AMediaCodec` Zero-Copy 硬體解碼器與 `ANativeWindow` 渲染。
- [x] 實做 Drop-to-IDR 追時演算法（60ms 佇列積壓觸發清空緩衝與 IDR 尋找）。
- [x] 實做 500ms Watchdog 畫面凍結與指數退避自動重連。
- [x] 實做純淨 MP4 錄影 (`AMediaMuxer` Raw NALU 直灌) 與純淨 JPEG 截圖輸出。
- [x] 實做系統效能監控數據採集（CPU /proc/stat, RAM, FPS, Bitrate, Decode Time）。
- [x] Git Commit 紀錄 Phase 2。

### Phase 3: JNI 介面與橋接層實做
- [x] 撰寫 C++ JNI 橋接函數 (`NativePlayerBridge`)：控制與狀態回調。
- [x] 撰寫 Kotlin `NativePlayer` 類別，封裝 JNI 呼叫與回調監聽。
- [x] Git Commit 紀錄 Phase 3。

### Phase 4: Kotlin 應用展示層與 UI / TV 適配
- [x] 實做 `DataStoreManager`：管理最近 5 筆 LRU 歷史 URL 及冷啟動自動載入。
- [x] 實做 `MainActivity` UI：
  - `SurfaceView` 與 `SurfaceHolder.Callback`。
  - 右上角非侵入式 OSD HUD（即時顯示 Telemetry 指標，雙指/Menu 鍵開關）。
  - 頂部 Amber 斷線警告標籤 `⚠️ Disconnection (Reconnecting...)`。
  - 串流 URL 輸入與歷史卡片/下拉選單。
  - 錄影與截圖按鈕。
  - 管線與傳輸協定設定對話框（2x2 模組化選擇）。
  - Android TV D-pad 焦點適配與遙控器鍵位處理。
- [x] 配置 AndroidManifest.xml (權限、TV Leanback 支援、硬體加速)。
- [x] Git Commit 紀錄 Phase 4。

### Phase 5: 編譯驗證與產出簽章 Debug / Release APK
- [x] 執行 Gradle 編譯驗證。
- [x] 產出 `app-debug.apk`。
- [x] 產出 `app-release.apk` (帶憑證簽名，ProGuard/R8 混淆與 Strip)。
- [x] 驗證 APK 憑證簽名與版號 (versionName, versionCode)。
- [x] Git Commit 紀錄 Phase 5。

### Phase 6: 渲染問題排查、根因修復與實機驗證 (Render Bug Investigation & Real-device Verification)
- [x] **實機 Logcat 追蹤與綠屏/掉幀診斷**：
  - 發現硬體解碼輸出呈現純綠屏 (YUV 0,0,0) 且 Render FPS 異常偏低 (7.7 fps)。
- [x] **根因定位與修復 (Root Causes & Solutions)**：
  1. *SPS/PPS 雙重探測時序*：原 Codec Prober 在收到 SPS 後立即返回 `isProbed=true`，導致 `AMediaCodec_configure` 初始化時 `csd-1` (PPS) 為空，硬體解碼器缺少 PPS 無法解碼 Slice 輸出綠屏。修復：強制要求同時探測到 SPS 與 PPS 且支援單一緩衝區多 NALU 解析後才實例化解碼器。
  2. *RTP Multi-Slice Access Unit 組合*：串流每幀由 19 個 RTP 封包與多個 Slice 組成（相同 RTP timestamp，最後一個封包 Marker=1）。原代碼將各 Slice 單獨發送至 `AMediaCodec_queueInputBuffer`，造成晶片解碼器 Macroblock 缺失與 `present fence has not fired` 警示。修復：在 `RtpDepacketizer` 實做 Access Unit 完整組合緩衝區，於 Marker=1 或 Timestamp 變化時一次性送入完整幀。
  3. *Drop-to-IDR 追時門檻優化*：避免網路抖動或微小等待時間造成單幀誤觸發 Drop-to-IDR，限制必須在積壓幀數 $\ge$ 3 且 waitMs > 60ms 時才啟動追時，並優先在積壓佇列中直接搜尋最新 IDR 關鍵幀跳躍恢復播放，避免 5 秒黑屏凍結。
- [x] **實機驗證通過 (MiTV-AFMU0 Android 14)**：
  - 視訊畫面色彩鮮明、字體時鐘清晰 (1280x720 @ 30fps)。
  - OSD HUD 顯示 `RENDER FPS: 31.0 fps (Src: 31)`，零延遲同步。
  - 純淨 JPEG 截圖與純淨 MP4 錄影功能正常運作並通過驗證。
- [x] Git Commit 紀錄 Phase 6 (`e77dcf6`)。
