# Current Version: 1.4.0

## [1.4.0] - 2026-10-18
### Added
- **OSD 遙測開關按鈕**：在頂部控制列新增專屬 `OSD` 按鈕，支援點擊即時切換 OSD 遙測 HUD，並以顏色高亮（藍色/暗灰）指示啟用狀態。
- **G2G 延遲量測毫秒碼表 HUD**：新增專用高對比毫秒數位碼表（`00:00:00:000`），以 ms 為最小單位即時滾動，專為高速相機 Glass-to-Glass (G2G) 端到端延遲評測量身打造；支援頂部 `TIMER` 按鈕獨立 Toggle 控制與翡翠綠高亮。
- **Android 14 SELinux 相容 CPU 監控**：改採 POSIX 標準高精度行程時脈 `CLOCK_PROCESS_CPUTIME_ID` 搭配 `CLOCK_MONOTONIC`，徹底解決 Android 8+ 及 Android 14 SELinux 封鎖非 Root 應用讀取 `/proc/stat` 導致 CPU Usage 永久歸零（`0.0% / 0.0%`）之問題，精準量測 App CPU 即時附載。
- **RTSP Keep-Alive 心跳保活機制**：於 `RtspClient` 實作週期性 `GET_PARAMETER` 心跳執行緒，自動解析 SETUP 回應之 `timeout=` 參數並以 20s 週期發送保活封包，根除伺服器 55 秒 session timeout 導致的中斷問題。
- **本地 RTP/RTCP 埠隨機化防衝撞**：導入奈秒時間戳種子之隨機偶數 Port 配對機制，解決重連或快速重啟時與前一連線未完全釋放的 UDP Port 碰撞問題。

### Fixed
- **多 Slice 綠屏修復**：於 `RtpDepacketizer` 實作 Access Unit (AU) 跨 Slice 封包組裝聚合機制，確保完整視訊影格單元餵入 `AMediaCodec`，徹底消除多切片編碼下的滿版綠屏與破圖。
- **解碼隊列等待優化**：放寬硬體解碼器輸入緩衝區等待逾時至 10ms，防止晶片瞬時忙碌時錯誤丟幀；並於解碼迴圈中無條件呼叫 `drainOutput(true)`，保證輸出影格即時無延遲渲染。
- **Socket EOF 緊密迴圈修復**：修正 `SocketTransport::recvTcp` 遇到對端關閉時回傳 0 造成 `poll()` 密集空轉之問題，正確回傳 -1 觸發斷線狀態。
- **斷線看門狗持續重連**：重構 `PlayerEngine::watchdogLoop`，500ms 無封包即凍結最後一幀，並在背景以指數退避（`200ms -> 500ms -> 1s -> 2s`）持續重試連線；恢復期間嚴格過濾非關鍵幀，直至 IDR 抵達才恢復渲染，完全符合 PRD 規範。

---

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
