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
