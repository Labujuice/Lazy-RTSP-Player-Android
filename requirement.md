# 產品需求規格書 (PRD)：Android 跨端單路極低延遲視訊串流播放器 (FPV / 工業操控版)

| 文件版本 | 專案名稱 | 適用平台 | 核心指標 | 文件狀態 |
| :--- | :--- | :--- | :--- | :--- |
| **v1.3 (Release Target)** | UltraLowLatencyRTSPPlayer | Android 7.0+ (手機 / 平板 / 電視盒) | 端到端延遲 $\le$ 100ms | 需求定案 (Ready for Architecture) |

---

## 一、 產品概述 (Executive Summary)

### 1.1 背景與問題陳述 (Problem Statement)
在無人機第一人稱視角（FPV）即時操控、機器人自主導引及各類工業遠端搖控應用中，操作端對畫面延遲與時效性有極度苛刻的要求（端到端延遲須控制在 100ms 以內）。現行 Android 多媒體播放解決方案（如 Google ExoPlayer、Ijkplayer、VLC）大多基於常規影音播放場景設計，預設具備 500ms 至 2000ms 的平滑抗抖緩衝（Jitter Buffer），在網路抖動或丟包時極易產生慢動作播放、畫面滯後與延遲累積，無法滿足即時閉環操控需求。
此外，業界傳統採用之 GStreamer 框架雖然功能強大，但在導入 Android 時往往引入巨大的 APK 體積包袱，且其內部管線傳輸與上下文切換可能帶來不可忽視的延遲與記憶體拷貝開銷。因此，亟需一套具備可量化 Benchmark 評估能力、支援模組化解耦、可剪裁且極度專注於單路低延遲傳輸的專業級播放核心。

### 1.2 產品願景與定位 (Product Vision)
本產品為一款專為 Android 手機、平板與 Android TV/電視盒所設計的**單路極限低延遲視訊管線引擎與效能遙測儀**。
產品具備可解耦之 2 × 2 模組化管線（支援自研 C++ 與 GStreamer 雙拉流引擎，以及硬體與軟體雙解碼器），並支援多種傳輸層通訊模式（RTSP over UDP、RTSP over TCP、Direct UDP Raw NALU）。全系統嚴格遵守「先探測編碼格式、再實例化解碼器」的安全時序，並內建非侵入式全維度 OSD 遙測 HUD、純淨無損 MP4 錄影、純淨 JPEG 截圖，以及跨開發環境的 Docker 容器化建置流水線與版本演進自動化管理機制。

### 1.3 成功衡量指標 (Success Metrics / KPIs)
1. **極限端到端延遲 (Latency SLA)**：自研 C++ RTP Client + NDK 硬解管線在穩定局域網/低干擾 Wi-Fi 下，玻璃到玻璃（Glass-to-Glass）延遲 $\le$ 100ms。
2. **零記憶體拷貝 (Zero-Copy Verification)**：自研硬解管線下，視訊封包經 NDK 解析後直接寫入 `AMediaCodec` 輸入緩衝，解碼後直接藉由 `ANativeWindow` 渲染至 SurfaceView，JVM/CPU 記憶體拷貝次數為 0 次。
3. **無人化追時能力 (Drop-to-IDR)**：當佇列因網路瞬斷累積超過 60ms，系統於 150ms 內完成緩衝清空並跳至最新 IDR/關鍵幀，徹底消除延遲累積。
4. **純淨無損擷取影響度**：啟用錄影與截圖時，管線影格渲染延遲增加 $\le$ 2ms，CPU 增量 $\le$ 2%。
5. **安裝包體積 (APK Footprint)**：純自研 C++ 輕量版 (Slim Flavor) APK 增量 $\le$ 15MB。

---

## 二、 規格邊界定義 (Scope Definition)

### 2.1 In-Scope (本次規格承諾交付)
* **單路專注視訊解碼**：資源全數集中於單一畫面，確保解碼線程與渲染排程優先權最高。
* **模組化 2 × 2 管線矩陣**：
  * 拉流端（Ingestion）：自研 C++ 引擎 vs. GStreamer 官方管線。
  * 解碼端（Decoder）：Android NDK `AMediaCodec` 硬體解碼 vs. FFmpeg `libavcodec` CPU 軟體解碼。
* **傳輸層協議彈性配置**：
  * RTSP over UDP（強制 UDP 接收 RTP）。
  * RTSP over TCP（Interleaved 交錯傳輸模式）。
  * Direct UDP Raw Stream（監聽指定 Port，接收無 RTP/RTSP 封裝之純 NALU 串流）。
* **探測先決架構（Probe-First Pipeline）**：連線後先擷取 NALU Header/SPS/PPS/VPS 解析格式，比對晶片硬體支援能力後才實例化對應解碼器。
* **極低延遲追時機制**：0~1 幀無緩存設計；積壓 > 60ms 觸發清空緩衝並尋找下一個關鍵幀恢復。
* **斷線 500ms 畫面凍結與自動重連**：連續 500ms 無封包即凍結保留最後一幀，顯示 "Disconnection" 警告並於背景以退避演算法重試。
* **非侵入式 OSD 效能與系統資源儀表板**：可隨時開關，提供管線配置、Bitrate、FPS、解碼耗時、丟幀數、CPU 使用率、RAM 佔用、GPU 狀態監控。
* **純淨無損錄影與截圖**：
  * 錄影：Demux 後直接透過輕量級 Muxer 封裝為標準 `.mp4`（無二次編碼、無 OSD 疊加）。
  * 截圖：解碼後原始畫格即時輸出為標準 `.jpg`（無 OSD 疊加）。
* **URL 狀態記憶與 LRU 歷史紀錄**：冷啟動自動載入上次離開的 URL，並儲存最近最多 5 筆不重複歷史 URL。
* **跨三端通用架構**：以 Kotlin 開發通用 Android View/Compose，以標準 `SurfaceView` + NDK `ANativeWindow` 相容手機、平板與電視盒（支援 D-pad 導航）。
* **工程交付標準**：Dockerfile 容器化建置環境（產出 Debug/Release APK）與 `VerChangeLog.md` 語意化版本管理。

### 2.2 Out-of-Scope (明確排除項目)
* **多分割畫面 (Multi-View / Grid)**：排除 2x2 或 3x3 多路同時播放功能。
* **攝影機/雲台控制 (ROI / POI / PTZ)**：嚴格不支援 PTZ 鏡頭轉向、ROI 感興趣區域動態編碼配置、POI 標記或 ONVIF 控制指令。
* **音訊管線與音畫同步 (Audio Pipeline / AV Sync)**：全面排除音訊解碼、重採樣及音訊時鐘同步機制（純 Video-Only 模式），以避免音訊佇列阻礙視訊極限時效。

---

## 三、 系統架構與技術語言規範 (Architecture & Tech Stack)

### 3.1 語言分層原則
* **應用與展示層 (Kotlin)**：負責 Android 生命週期管理、Surface 生命週期回調、D-pad 焦點控制、URL 歷史持久化（Jetpack DataStore）、非侵入式 HUD 疊加繪製與使用者操作互動。
* **底層核心引擎 (Modern C++ / NDK)**：負責 Socket 監聽（UDP/TCP）、RTP 解包、Direct UDP NALU 切分、Codec Prober、`AMediaCodec` / `libavcodec` 調度、Zero-Copy 渲染至 `ANativeWindow`、MP4 封裝與系統資源指標讀取。
* **跨層邊界 (JNI)**：僅傳遞控制指令（Start, Stop, SwitchPipeline, Record, Capture）與每秒彙整之效能遙測數據 Struct，**嚴禁**將高頻視訊封包或原始像素陣列跨 JNI 傳遞至 JVM。


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
│  │ (AMediaCodec)│                         │(libavcod)│ │
│  └──────┬───────┘                         └────┬─────┘ │
│         │ Zero-Copy                            │ PBO   │
│         ▼                                      ▼       │
│  ┌──────────────────────────────────────────────────┐  │
│  │ 5. 渲染與截圖 (ANativeWindow / Clean JPEG Capture)│  │
│  └──────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────┘

```

---

## 四、 核心功能詳細規格 (Functional Specifications)

### 4.1 模組化 2 × 2 管線矩陣 (F-01)
* **規格定義**：拉流端與解碼端完全解耦，支援 4 種組合：
  1. **組合 1 (自研 C++ + 硬解 - 預設)**：RTP 封包經 C++ Socket 接收解包後直接寫入 NDK `AMediaCodec` 輸入緩衝區，解碼完成直接送往 `ANativeWindow` 進行硬體渲染（Zero-Copy）。
  2. **組合 2 (自研 C++ + 軟解)**：NALU 寫入 `libavcodec` 解碼為 YUV420P，經由 C++ OpenGL ES 著色器或 AHardwareBuffer 渲染至 Surface。
  3. **組合 3 (GStreamer + 硬解)**：使用 GStreamer 官方外掛構建 pipeline：`rtspsrc ! rtph26xdepay ! h26xparse ! amcviddec ! glimagesink`。
  4. **組合 4 (GStreamer + 軟解)**：使用 GStreamer 軟解外掛：`rtspsrc ! rtph26xdepay ! h26xparse ! avdec_h26x ! videoconvert ! autovideosink`。
* **動態切換原則**：切換時必須遵循安全釋放時序：`Stop Pipeline` $\rightarrow$ `Flush & Release Codec` $\rightarrow$ `Release Window` $\rightarrow$ `Re-initialize Target Pipeline`，防止晶片硬體通道洩漏。

### 4.2 傳輸層協議配置 (F-02)
* **傳輸模式選擇**：
  1. **RTSP over UDP (預設)**：RTSP 控制連線建立後，指定 UDP Port 接收 RTP/RTCP。允許少量封包丟失，杜絕 TCP 重傳機制帶來的延遲放大。
  2. **RTSP over TCP (交錯傳輸 Interleaved)**：RTP/RTCP 封包封裝在 TCP 控制通道傳輸，適用於嚴格阻擋 UDP 流量之企業網路。
  3. **Direct UDP (Raw Stream)**：無 RTSP 信令握手、無 RTP 標頭包裝。直接監聽自定義 UDP 連接埠（預設 5600），以 `0x00000001` Start Code 分割 NALU，直接送入解碼管線。適配特種無人機圖傳板（如 OpenIPC、WFB-ng）。

### 4.3 探測先決之編碼決策機制 (F-03)
* **核心原則**：嚴禁在未識別實際編碼格式前預先初始化解碼器。
* **運作時序**：
  1. **連線建立**：網路通道開啟，系統處於 `STATE_PROBING`。
  2. **封包監聽**：分析收到的前 1~5 個數據包：
     * H.264：搜尋 NAL Type 7 (SPS)、Type 8 (PPS)。
     * H.265：搜尋 NAL Type 32 (VPS)、Type 33 (SPS)、Type 34 (PPS)。
     * AV1：搜尋 OBU Type 1 (Sequence Header)。
  3. **硬體能力適配**：
     * 調用 Android NDK / MediaCodecList 比對目標設備是否具備該格式之硬體解碼器（如 `video/avc`、`video/hevc`、`video/av01`）。
     * 若選擇硬解且晶片支援 $\rightarrow$ 呼叫 `AMediaCodec_createDecoderByType()`。
     * 若選擇硬解但晶片回報不支援（例如特定平價晶片不支援 HEVC 10-bit 或 AV1） $\rightarrow$ 系統自動安全降級為 `libavcodec` 軟體解碼，並於 OSD 顯示降級警示。

### 4.4 極致防積壓與追時演算法 (F-04)
* **緩衝深度**：Jitter Buffer 限制為 **0 ~ 1 幀**（嚴禁多幀抗抖動緩存）。
* **追時機制 (Drop-to-IDR)**：
  * 當接收緩衝區積壓時間超過 **60ms**（約 2~4 幀視訊時長）時，立即啟動追時。
  * 調用 `AMediaCodec_flush` 清空解碼器輸入/輸出緩衝。
  * 拋棄後續所有抵達之 P 幀與 B 幀，直到接收到下一個完整 SPS/PPS/VPS + IDR 關鍵幀，立即送入解碼恢復渲染，保證操作畫面永遠處於最新時間戳。

### 4.5 網路中斷 500ms 畫面凍結與復原機制 (F-05)
* **超時判斷**：核心設定 500ms 數據守護定時器（Watchdog）。若連續 **500ms** 未收到任何有效視訊封包：
  1. **畫面凍結 (Freeze Last Frame)**：立即停止 Surface 清空與繪製，鎖定中斷前最後一幀完整畫面，絕對禁止黑屏或花屏。
  2. **警告提示**：畫面頂部中央淡入醒目琥珀色半透明警示標籤：`⚠️ Disconnection (Reconnecting...)`。
* **背景自動復原**：背景啟動指數退避重連（間隔：200ms $\rightarrow$ 500ms $\rightarrow$ 1s $\rightarrow$ 2s 循環）。串流重新連通且接收到首個關鍵幀後，自動清除警告標籤並恢復實時渲染。

### 4.6 純淨無損錄影與截圖 (F-06)
* **核心限制**：產出之多媒體檔案必須為純淨視訊畫面，**嚴格禁止壓上任何 OSD 數據、HUD 儀表板或 UI 元素**。
* **錄影規格 (Clean MP4 Muxing)**：
  * **零二次編碼 (Zero Transcoding)**：在 Demuxer 解析出 Raw NALU 後，直接分流寫入封裝器，不經過解碼與重新編碼程序，零 CPU 算力損耗、零延遲干擾。
  * **容器格式**：透過 C++ NDK 呼叫輕量化 MP4 Muxer（`AMediaMuxer`），封裝為標準 `.mp4` 檔案。
  * **時間戳校正**：嚴格寫入來源 RTP/PTS 時間戳，確保輸出檔案播放流暢無快進/慢動作現象。
* **截圖規格 (Clean JPEG Screenshot)**：
  * **純淨畫格捕捉**：
    * 軟解管線：直接由 `AVFrame` 讀取當幀 YUV420P 原始緩衝。
    * 硬解管線：於渲染管線底層直接讀取解碼後之畫格像素，或透過綁定之離屏 FBO 截取純淨視訊幀。
  * **輸出格式**：壓縮為高畫質 `.jpg`（Quality 95%），儲存至標準外部儲存目錄（`Movies/FPV_Records` 與 `Pictures/FPV_Snapshots`）。

### 4.7 非侵入式全維度 OSD 效能遙測儀表板 (F-07)
* **顯示設計**：
  * 預設配置於畫面右上角，佔用螢幕面積小於 12%，背景採用 40% 半透明黑底（`#66000000`），文字採用等寬字體（Monospace）。
  * 螢幕中央 70% 區域保持 100% 完全穿透，確保不遮擋操控視野與十字準星。
* **開關控制 (Toggle)**：
  * 手機/平板：雙指點擊螢幕任意位置切換「顯示 / 隱藏」。
  * 電視盒 (TV)：按下遙控器 `Menu` 鍵或長按 `OK` 鍵快速切換。
* **監控維度與系統讀取規範**：

| 監控群組 | 顯示欄位 | 單位與格式 | 數據採集來源 | 跨端異常與相容性處理 |
| :--- | :--- | :--- | :--- | :--- |
| **管線狀態** | 當前管線組合 | 字串（如 `C++ + HW Codec`） | 讀取當前啟動之模組狀態 | 靜態常數顯示 |
| | 傳輸協議 | 字串（`UDP (Force)` / `TCP` / `Direct-UDP`）| 讀取 Socket 狀態 | 靜態常數顯示 |
| | 編碼格式與解析度 | 字串（如 `H.265 Main @ 1920x1080`） | Codec Prober 及 MediaFormat 輸出 | 未探測完成前顯示 `Probing...` |
| **串流傳輸** | 即時位元速率 (Bitrate) | `kbps` / `Mbps` | 統計最近 1 秒內 RTP/UDP Payload 總位元組數 | 每秒計算更新 |
| | 渲染幀率 (Render FPS) | `fps`（保留一位小數） | 統計每秒送往 `ANativeWindow` 渲染之次數 | 精度達 0.1 fps |
| | 來源幀率 (Source FPS) | `fps`（整數） | 統計每秒抵達之完整視訊幀計數 | 依據 NALU 時戳換算 |
| **延遲與流暢** | 解碼耗時 (Decode Time) | `ms` | 記錄封包進解碼器至解碼完成傳回之時間差 | 移動平均值，精確至 0.1ms |
| | 丟幀累計 (Dropped Frames) | `幀數 (Jitter: X / Loss: Y)` | 累計追時主動 Drop 數與 RTP 序號斷裂數 | 累加計數 |
| **系統負載** | CPU 使用率 (CPU Rate) | 百分比 `%`（App % / Total %） | 解析 `/proc/self/stat` 與 `/proc/stat` 時間片 | 全 Android 平台相容 |
| | RAM 佔用 (Memory) | `MB` (PSS / RSS) | 讀取 `Debug.MemoryInfo` 或 `/proc/self/status` | 全 Android 平台相容 |
| | GPU 負載與頻率 | 百分比 `%` / `MHz` | 探測晶片節點（Adreno / Mali sysfs） | 若受系統 SELinux 權限阻擋，顯示 `N/A (Protected)`，絕不引發崩潰 |

### 4.8 URL 狀態記憶與 LRU 歷史管理 (F-08)
* **冷啟動自動載入**：
  * 使用 Jetpack DataStore Preferences 持久化儲存 `last_active_url`。
  * App 啟動完成後，若存在有效之 `last_active_url`，自動觸發連線與播放，無須手動再次點擊。
* **LRU 歷史佇列管理**：
  * 容量上限：嚴格限制為 **5 筆** 不重複之 URL。
  * 佇列演算法：
    * 新連線成功之 URL 自動提升至佇列頂端（Index 0）。
    * 若 URL 已存在於歷史列表中，移動至頂端，不產生重複項目。
    * 若佇列已滿 5 筆且有新 URL 加入，自動淘汰最久未使用的最後一筆（Index 4）。
* **跨端操作**：
  * 手機/平板：點擊輸入框彈出下拉選單，提供一鍵填入與單項刪除。
  * 電視盒 (TV)：焦點移至輸入框時，下方展示水平卡片列表，透過 D-pad 方向鍵即可快速選取歷史串流。

### 4.9 跨三端通用互動適配 (F-09)
* **核心視訊容器**：使用 Android 標準 `SurfaceView`，透過 `SurfaceHolder.Callback` 監聽生命週期，並將 Surface 傳遞至 NDK 轉為 `ANativeWindow`。
* **手機 / 平板**：
  * 支援全螢幕縮放適配（Fit / CenterCrop）。
  * 雙指輕觸：切換 OSD 顯示狀態。
  * 長按 1.5 秒：喚出管線與傳輸協議設定對話框。
* **電視盒 (Android TV)**：
  * 全介面元素具備 `android:focusable="true"`，支援 D-pad 焦點移動與反白。
  * 遙控器鍵位對應：`OK` 鍵開關控制選單、`Menu` 鍵切換 OSD、`Back` 鍵雙擊確認退出。

---

## 五、 非功能性需求 (NFR)

### 5.1 穩定性與防崩潰隔離 (Crash Isolation)
* NDK 層必須註冊 POSIX 信號處理器（Signal Handler，攔截 SIGSEGV、SIGBUS、SIGABRT）。
* 當特定硬體解碼驅動因損壞或不相容之 NALU 引發底層崩潰時，JNI 層必須攔截並拋出安全異常，自動釋放崩潰實例並無縫切換至軟解，嚴禁導致 App 閃退。

### 5.2 記憶體管理與零碎片化
* RTP 封包與 UDP 數據接收禁止在運行期間頻繁動態配置（`malloc` / `free` / `new`）。
* 啟動時必須預先配置固定大小的環形緩衝池（Ring Buffer Pool），封包消費完畢立即歸還指針，杜絕記憶體碎片化與記憶體洩漏。

---

## 六、 交付與建置管理規格 (Build & Version Management)

### 6.1 Docker 容器化編譯環境
專案根目錄必須提供標準 `Dockerfile`，固定 Android SDK、NDK 及 CMake 版本，所有正式產出之 APK 必須透過產出之 Docker 映像檔編譯生成。

#### 1. Dockerfile 規格
```dockerfile
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV ANDROID_SDK_ROOT=/opt/android-sdk
ENV ANDROID_NDK_VERSION=26.1.10909125
ENV CMAKE_VERSION=3.22.1

# 安裝基礎依賴與 OpenJDK 17
RUN apt-get update && apt-get install -y \
    openjdk-17-jdk \
    curl \
    unzip \
    git \
    build-essential \
    ninja-build \
    && rm -rf /var/lib/apt/lists/*

# 安裝 Android Command-line Tools
WORKDIR /opt
RUN mkdir -p ${ANDROID_SDK_ROOT}/cmdline-tools && \
    curl -o cmdline-tools.zip [https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip](https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip) && \
    unzip cmdline-tools.zip -d ${ANDROID_SDK_ROOT}/cmdline-tools && \
    mv ${ANDROID_SDK_ROOT}/cmdline-tools/cmdline-tools ${ANDROID_SDK_ROOT}/cmdline-tools/latest && \
    rm cmdline-tools.zip

ENV PATH=${PATH}:${ANDROID_SDK_ROOT}/cmdline-tools/latest/bin:${ANDROID_SDK_ROOT}/platform-tools

# 安裝 Platform、NDK 與 CMake
RUN yes | sdkmanager --licenses && \
    sdkmanager "platform-tools" \
               "platforms;android-34" \
               "build-tools;34.0.0" \
               "ndk;${ANDROID_NDK_VERSION}" \
               "cmake;${CMAKE_VERSION}"

WORKDIR /workspace
CMD ["/bin/bash"]

```

#### 2. 編譯映像檔與執行命令

* **建立建置映像檔**：
```bash
docker build -t rtsp-player-builder:v1.0 .

```


* **容器化編譯指令**：
* **Debug 產物**：
```bash
docker run --rm -v $(pwd):/workspace rtsp-player-builder:v1.0 ./gradlew assembleDebug

```


* **Release 產物**：
```bash
docker run --rm -v $(pwd):/workspace rtsp-player-builder:v1.0 ./gradlew assembleRelease

```





#### 3. Debug 與 Release 模式規格差異

| 項目 | Debug Build | Release Build |
| --- | --- | --- |
| **日誌層級** | `LOG_LEVEL_VERBOSE`：輸出詳細 NALU Header、RTP Sequence、Socket I/O 與 Jitter 追蹤 | `LOG_LEVEL_ERROR`：關閉所有 Info/Verbose 訊息，僅輸出致命錯誤 |
| **符號表與除錯** | 保留 NDK 完整 Debug Symbols，支援 ASan（AddressSanitizer）記憶體排查 | 執行 `strip` 移除全部除錯符號表 |
| **代碼壓縮** | 關閉 ProGuard / R8，便於崩潰堆疊比對 | 啟用 R8 進行極致程式碼壓縮與混淆 |
| **OSD 預設行為** | 開機連線成功後**預設展開** OSD 儀表板 | 開機連線成功後**預設隱藏** OSD 儀表板 |
| **產物路徑** | `app/build/outputs/apk/debug/app-debug.apk` | `app/build/outputs/apk/release/app-release.apk` |

---

### 6.2 版本控管與 `VerChangeLog.md` 規範

專案根目錄必須維護 `VerChangeLog.md`，並作為專案版號的**單一真實來源（Single Source of Truth）**。

#### 1. 管理原則

* 檔案第一行必須是目前的正式版本號（格式：`# Current Version: X.Y.Z`）。
* `app/build.gradle.kts` 必須透過自動化腳本解析該檔案第一行的版號，自動映射生成 `versionName` 與計算 `versionCode`。
* 歷史變更紀錄由新至舊（倒序）排列，遵循 Keep a Changelog 格式。

#### 2. `VerChangeLog.md` 規格範本

```markdown
# Current Version: 1.3.0

## [1.3.0] - 2026-10-18
### Added
- 新增 Direct UDP Raw Stream（無封裝純 NALU 串流）接收支援。
- 新增零二次編碼純淨 MP4 錄影功能（不疊加 OSD 水印）。
- 新增純淨 JPEG 截圖功能（不疊加 OSD 水印）。
- 新增最近 5 組 URL 歷史紀錄管理與冷啟動自動載入功能。
- 新增 Dockerfile 容器化編譯環境與自動化建置腳本。

### Changed
- 重構解碼管線初始化時序：改為探測編碼格式（Codec Prober）確認後才動態實例化硬解或軟解。
- OSD 效能 HUD 移至螢幕角落並加入半透明背景，提供快捷開關以消除中心視覺遮擋。

---

## [1.2.0] - 2026-10-17
### Added
- 新增全維度 OSD 效能監控 HUD（即時 FPS、Bitrate、解碼耗時、CPU、RAM、GPU 狀態）。
- 新增 500ms 斷線凍結最後一幀畫面與 "Disconnection" 警告機制。

---

## [1.1.0] - 2026-10-16
### Fixed
- 修正硬體解碼器在特定電視盒晶片因實例未釋放引發的崩潰問題。
- 加入 JNI 層 Signal 攔截與軟解安全降級防護。

---

## [1.0.0] - 2026-10-15
### Added
- 初版發布：支援 Android 手機/平板/電視盒通用 SurfaceView 渲染。
- 支援 GStreamer vs 自研 C++ 雙拉流引擎。
- 支援 NDK AMediaCodec 硬解 vs FFmpeg 軟解切換。
- 支援 RTSP over UDP 與 RTSP over TCP 切換。

```

#### 3. Gradle 自動解析腳本規格 (`build.gradle.kts`)

```kotlin
val verChangeLogFile = rootProject.file("VerChangeLog.md")
val currentVersion = if (verChangeLogFile.exists()) {
    verChangeLogFile.useLines { lines ->
        val firstLine = lines.firstOrNull() ?: "# Current Version: 1.0.0"
        firstLine.replace("# Current Version:", "").trim()
    }
} else {
    "1.0.0"
}

// 根據 SemVer (X.Y.Z) 自動生成整數 versionCode: X * 10000 + Y * 100 + Z
val versionParts = currentVersion.split(".")
val computedVersionCode = if (versionParts.size == 3) {
    versionParts[0].toInt() * 10000 + versionParts[1].toInt() * 100 + versionParts[2].toInt()
} else {
    10000
}

android {
    defaultConfig {
        versionCode = computedVersionCode
        versionName = currentVersion
    }
}

```

---

## 七、 驗收測試案例 (Acceptance Criteria)

### AC-1：探測先決與硬解 Zero-Copy 驗收

* **Given** 選擇「自研 C++ 拉流」+「硬體解碼」+「強制 UDP」，輸入 1080p 60fps H.265 RTSP 串流。
* **When** 點擊連線並透過雙機同框拍攝毫秒計時器。
* **Then**
1. 連線初始時 OSD 顯示 `Probing...`，於 3 幀內識別出 `H.265` 並成功掛載硬體解碼器。
2. 玻璃到玻璃（Glass-to-Glass）端到端延遲 $\le$ 100ms。
3. 解碼耗時穩定於 3ms ~ 6ms，且 CPU 使用率低於 15%。



### AC-2：Direct UDP Raw Stream 接收驗收

* **Given** 發射端透過 UDP 對本機指定連接埠持續發送無封裝之 H.264 Raw NALU 數據包。
* **When** 播放器選擇「Direct-UDP」傳輸模式並啟動監聽。
* **Then** Codec Prober 能正確識別 `0x00000001` Start Code 與 SPS/PPS，啟動解碼器並流暢渲染，延遲 $\le$ 100ms。

### AC-3：純淨無損錄影與截圖驗收

* **Given** 正在播放串流，畫面上方開啟並顯示 OSD 效能 HUD。
* **When**
1. 點擊截圖按鈕生成 `.jpg`。
2. 點擊錄影按鈕錄製 30 秒生成 `.mp4`。


* **Then**
1. 檢視儲存之 `.jpg` 與 `.mp4`，畫面均為 100% 純淨原始影像，**嚴禁出現任何 OSD 文字或 UI 水印**。
2. 使用 MediaInfo 檢測 `.mp4`，其編碼格式、解析度與幀率與來源串流完全一致，無二次轉碼特徵。



### AC-4：斷線凍結與自動復原驗收

* **Given** 處於正常播放狀態。
* **When** 人為拔除網路線或中斷發射端。
* **Then**
1. 播放器在 500ms 內停止畫面更新，保留中斷前最後一幀畫面（無黑屏、無花屏）。
2. 畫面頂部淡入顯示 `⚠️ Disconnection (Reconnecting...)` 標籤。
3. 發射端重新連通後，系統自動清除警告並恢復實時播放。



### AC-5：URL 記憶與 LRU 歷史管理驗收

* **Given** 使用者曾依序連線過 6 組相異之有效串流 URL。
* **When** 殺死 App 進程並重新冷啟動，隨後點擊 URL 輸入框。
* **Then**
1. App 自動載入第 6 組 URL 並立即發起連線播放。
2. 歷史列表中精確保留最近的 5 組 URL，最舊之第 1 組 URL 已被自動淘汰。



### AC-6：Docker 容器化建置與版號連動驗收

* **Given** 修改 `VerChangeLog.md` 第一行為 `# Current Version: 2.0.1`。
* **When** 執行 `docker run ... ./gradlew assembleRelease`。
* **Then**
1. Docker 容器內順利編譯完成，於 `outputs/apk/release/` 產出無 Debug Symbol 的 Release APK。
2. 檢查產出之 APK Manifest，其 `versionName` 自動設定為 `2.0.1`，`versionCode` 自動設定為 `20001`。





<FollowUp label="要針對這份 PRD 接續產出系統架構設計書（Architecture Design Document）嗎？" query="請依據這份 PRD，產出系統架構設計書（Architecture Design Document），包含 C++ NDK 類別圖、JNI 介面規格及線程模型設計。"/>

