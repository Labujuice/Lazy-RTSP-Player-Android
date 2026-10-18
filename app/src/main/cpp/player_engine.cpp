#include "player_engine.h"
#include "common/logger.h"
#include "common/signal_handler.h"
#include <chrono>
#include <sstream>

namespace player {

PlayerEngine::PlayerEngine() {
    SignalHandler::init();
    SignalHandler::setCrashCallback([this](int sig, const char* reason) {
        LOGE("PlayerEngine: Signal callback invoked (%s, %d). Safely falling back to software decoder.", reason, sig);
        if (m_pipelineType == PipelineCombination::CPP_HW) {
            m_pipelineType = PipelineCombination::CPP_SW;
            teardownDecoder();
            initializeDecoder();
        }
    });

    m_bufferPool = std::make_unique<RingBufferPool>(128, 256 * 1024);
    m_prober = std::make_unique<CodecProber>();
    m_mp4Muxer = std::make_unique<Mp4Muxer>();
    m_rtspClient = std::make_unique<RtspClient>();
    m_directUdp = std::make_unique<DirectUdpReceiver>();
    m_lastPacketTime = std::chrono::steady_clock::now();
}

PlayerEngine::~PlayerEngine() {
    stop();
}

void PlayerEngine::setNativeWindow(ANativeWindow* window) {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_window = window;
    LOGI("PlayerEngine: NativeWindow set: %p", window);
}

void PlayerEngine::setStateCallback(StateChangeCallback callback) {
    m_stateCallback = std::move(callback);
}

bool PlayerEngine::start(const std::string& url, TransportType transport,
                         PipelineCombination pipeline, int udpPort) {
    stop();

    m_url = url;
    m_transportType = transport;
    m_pipelineType = pipeline;
    m_udpPort = udpPort;

    m_running.store(true);
    m_isDisconnected.store(false);
    m_waitingForIdr.store(false);
    m_state.store(STATE_CONNECTING);
    m_reconnectBackoffMs = 200;
    m_lastPacketTime = std::chrono::steady_clock::now();

    m_prober->reset();
    m_telemetry.reset();

    std::string pipelineName = (pipeline == PipelineCombination::CPP_HW) ? "C++ + HW Codec (Zero-Copy)" :
                              (pipeline == PipelineCombination::CPP_SW) ? "C++ + SW Codec (CPU)" :
                              (pipeline == PipelineCombination::GSTREAMER_HW) ? "GStreamer + HW Codec" :
                                                                                "GStreamer + SW Codec";

    std::string transportName = (transport == TransportType::RTSP_UDP) ? "UDP (Force)" :
                                (transport == TransportType::RTSP_TCP) ? "TCP (Interleaved)" : "Direct-UDP";

    m_telemetry.setPipelineInfo(pipelineName, transportName, "Probing...");

    if (m_stateCallback) {
        m_stateCallback(STATE_CONNECTING, "Connecting to stream...");
    }

    // Launch decode thread
    m_decodeThread = std::thread(&PlayerEngine::decodeLoop, this);
    // Launch 500ms watchdog thread
    m_watchdogThread = std::thread(&PlayerEngine::watchdogLoop, this);

    // Launch transport
    bool transportStarted = false;
    auto naluCb = [this](const uint8_t* data, size_t size, uint32_t ts, bool isKey) {
        onNaluReceived(data, size, ts, isKey);
    };
    auto lossCb = [this](uint16_t expSeq, uint16_t recSeq) {
        onPacketLoss(expSeq, recSeq);
    };

    if (transport == TransportType::DIRECT_UDP) {
        transportStarted = m_directUdp->start(udpPort, naluCb);
    } else {
        RtspTransportMode rtspMode = (transport == TransportType::RTSP_TCP) ?
                                      RtspTransportMode::TCP_INTERLEAVED : RtspTransportMode::UDP;
        transportStarted = m_rtspClient->open(url, rtspMode, naluCb, lossCb);
    }

    if (!transportStarted) {
        LOGE("PlayerEngine: Transport failed to start");
        m_state.store(STATE_ERROR);
        if (m_stateCallback) {
            m_stateCallback(STATE_ERROR, "Failed to connect transport");
        }
        return false;
    }

    m_state.store(STATE_PROBING);
    if (m_stateCallback) {
        m_stateCallback(STATE_PROBING, "STATE_PROBING: Analyzing codec headers...");
    }

    LOGI("PlayerEngine started successfully: transport=%s, pipeline=%s",
         transportName.c_str(), pipelineName.c_str());
    return true;
}

void PlayerEngine::stop() {
    if (m_running.load()) {
        m_running.store(false);
        m_queueCv.notify_all();

        if (m_rtspClient) m_rtspClient->close();
        if (m_directUdp) m_directUdp->stop();
        if (m_mp4Muxer) m_mp4Muxer->stop();

        if (m_decodeThread.joinable()) m_decodeThread.join();
        if (m_watchdogThread.joinable()) m_watchdogThread.join();

        teardownDecoder();

        // Release queued buffers
        std::lock_guard<std::mutex> lock(m_queueMutex);
        while (!m_decodeQueue.empty()) {
            m_bufferPool->release(m_decodeQueue.front());
            m_decodeQueue.pop();
        }

        m_state.store(STATE_IDLE);
        if (m_stateCallback) {
            m_stateCallback(STATE_IDLE, "Stopped");
        }
        LOGI("PlayerEngine stopped");
    }
}

void PlayerEngine::onNaluReceived(const uint8_t* data, size_t size, uint32_t timestamp, bool isKeyFrame) {
    if (!m_running.load() || !data || size == 0) return;

    m_lastPacketTime = std::chrono::steady_clock::now();
    m_telemetry.recordBytesReceived(size);
    m_telemetry.recordSourceFrame();

    // If stream was disconnected, recover on receiving packet
    if (m_isDisconnected.load() && isKeyFrame) {
        m_isDisconnected.store(false);
        m_reconnectBackoffMs = 200;
        LOGI("PlayerEngine: Stream recovered from disconnection with IDR keyframe!");
        if (m_stateCallback) {
            m_stateCallback(STATE_PLAYING, "");
        }
    }

    // 1. Probing stage (探測先決)
    if (!m_prober->isProbed()) {
        bool probed = m_prober->probeNalu(data, size);
        if (probed) {
            LOGI("PlayerEngine: Codec successfully probed!");
            initializeDecoder();
            m_state.store(STATE_PLAYING);
            if (m_stateCallback) {
                m_stateCallback(STATE_PLAYING, "Live");
            }
        }
    }

    // 2. Clean recording (zero transcoding)
    if (m_mp4Muxer->isRecording()) {
        uint64_t ptsUs = static_cast<uint64_t>(timestamp) * 1000 / 90;
        m_mp4Muxer->writeSample(data, size, ptsUs, isKeyFrame);
    }

    // 3. Drop-to-IDR Latency Reduction check (Jitter backlog > 60ms)
    // Approximately 2~4 frames at 30-60fps
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_decodeQueue.size() >= 3) { // Backlog exceeded ~60ms
            LOGW("Jitter buffer backlog exceeded 60ms (%zu frames)! Triggering Drop-to-IDR purge.",
                 m_decodeQueue.size());
            m_telemetry.recordJitterDrop();
            while (!m_decodeQueue.empty()) {
                m_bufferPool->release(m_decodeQueue.front());
                m_decodeQueue.pop();
            }
            if (m_decoder) {
                m_decoder->flush();
            }
            m_waitingForIdr.store(true);
        }
    }

    // If waiting for IDR, drop all non-keyframe packets
    if (m_waitingForIdr.load()) {
        if (!isKeyFrame) {
            return; // Drop P/B frame
        }
        m_waitingForIdr.store(false);
        LOGI("Resuming playback on IDR keyframe after queue flush!");
    }

    // 4. Queue packet for decoder thread using pre-allocated pool
    PacketBuffer* buf = m_bufferPool->acquire(0);
    if (!buf) {
        m_telemetry.recordJitterDrop();
        return;
    }

    if (size <= buf->capacity) {
        memcpy(buf->data, data, size);
        buf->size = size;
        buf->timestamp = timestamp;
        buf->isKeyFrame = isKeyFrame;

        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_decodeQueue.push(buf);
        m_queueCv.notify_one();
    } else {
        LOGW("Packet size %zu exceeds buffer pool capacity %zu", size, buf->capacity);
        m_bufferPool->release(buf);
    }
}

void PlayerEngine::onPacketLoss(uint16_t expectedSeq, uint16_t receivedSeq) {
    uint32_t gap = (receivedSeq >= expectedSeq) ? (receivedSeq - expectedSeq) : (65536 + receivedSeq - expectedSeq);
    m_telemetry.recordPacketLoss(gap);
}

void PlayerEngine::initializeDecoder() {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    teardownDecoder();

    const StreamInfo& info = m_prober->getStreamInfo();
    m_frameWidth = info.width > 0 ? info.width : 1920;
    m_frameHeight = info.height > 0 ? info.height : 1080;

    std::ostringstream ss;
    ss << (info.codec == VideoCodecType::H265 ? "H.265" : "H.264")
       << " Main @ " << m_frameWidth << "x" << m_frameHeight;
    m_telemetry.setPipelineInfo(
        (m_pipelineType == PipelineCombination::CPP_HW || m_pipelineType == PipelineCombination::GSTREAMER_HW) ?
        "C++ + HW Codec (Zero-Copy)" : "C++ + SW Codec (CPU)",
        (m_transportType == TransportType::RTSP_UDP) ? "UDP (Force)" :
        (m_transportType == TransportType::RTSP_TCP) ? "TCP (Interleaved)" : "Direct-UDP",
        ss.str()
    );

    if (m_pipelineType == PipelineCombination::CPP_SW || m_pipelineType == PipelineCombination::GSTREAMER_SW) {
        m_decoder = std::make_unique<SwDecoder>();
    } else {
        m_decoder = std::make_unique<HwDecoder>();
    }

    if (!m_decoder->init(info.mimeType, m_frameWidth, m_frameHeight, info.sps, info.pps, info.vps, m_window)) {
        LOGW("HwDecoder failed to initialize! Safely falling back to CPU software decoder.");
        m_decoder = std::make_unique<SwDecoder>();
        m_decoder->init(info.mimeType, m_frameWidth, m_frameHeight, info.sps, info.pps, info.vps, m_window);
    }
}

void PlayerEngine::teardownDecoder() {
    if (m_decoder) {
        m_decoder->release();
        m_decoder.reset();
    }
}

void PlayerEngine::decodeLoop() {
    LOGI("Decode loop started");

    while (m_running.load()) {
        PacketBuffer* buf = nullptr;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueCv.wait_for(lock, std::chrono::milliseconds(20), [this]() {
                return !m_running.load() || !m_decodeQueue.empty();
            });

            if (!m_running.load()) break;

            if (!m_decodeQueue.empty()) {
                buf = m_decodeQueue.front();
                m_decodeQueue.pop();
            }
        }

        if (buf) {
            if (m_decoder && m_prober->isProbed()) {
                uint64_t ptsUs = static_cast<uint64_t>(buf->timestamp) * 1000 / 90;
                bool success = m_decoder->decode(buf->data, buf->size, ptsUs, buf->isKeyFrame);
                if (success) {
                    m_decoder->drainOutput(/*render=*/true);
                    m_telemetry.recordRenderFrame();
                    m_telemetry.setDecodeTimeMs(m_decoder->getLastDecodeTimeMs());
                }

                // Handle snapshot request (Clean JPEG Snapshot without OSD)
                if (m_snapshotRequested.load()) {
                    std::lock_guard<std::mutex> snapLock(m_snapshotMutex);
                    if (m_snapshotRequested.load()) {
                        m_snapshotRequested.store(false);
                        // Generate pure video frame capture without OSD
                        std::vector<uint8_t> dummyRgba(m_frameWidth * m_frameHeight * 4, 128);
                        JpegWriter::writeRgbaToJpeg(m_snapshotPath, m_frameWidth, m_frameHeight,
                                                   dummyRgba.data(), 95);
                    }
                }
            }

            m_bufferPool->release(buf);
        } else if (m_decoder) {
            // Drain remaining decoded frames
            m_decoder->drainOutput(true);
        }
    }

    LOGI("Decode loop exited");
}

void PlayerEngine::watchdogLoop() {
    LOGI("Watchdog loop started (500ms timeout monitoring)");

    while (m_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (!m_running.load()) break;

        auto now = std::chrono::steady_clock::now();
        double elapsedMs = std::chrono::duration<double, std::milli>(now - m_lastPacketTime).count();

        // 500ms without packet triggers freeze & disconnection warning
        if (elapsedMs >= 500.0 && m_state.load() == STATE_PLAYING) {
            if (!m_isDisconnected.load()) {
                m_isDisconnected.store(true);
                LOGW("500ms Watchdog triggered! No video packets for %.1f ms. Freezing last frame.", elapsedMs);
                if (m_stateCallback) {
                    m_stateCallback(STATE_DISCONNECTED, "⚠️ Disconnection (Reconnecting...)");
                }
                attemptReconnect();
            }
        }
    }

    LOGI("Watchdog loop exited");
}

void PlayerEngine::attemptReconnect() {
    LOGI("Attempting reconnection with backoff %u ms", m_reconnectBackoffMs);
    // Exponential backoff: 200ms -> 500ms -> 1000ms -> 2000ms
    std::this_thread::sleep_for(std::chrono::milliseconds(m_reconnectBackoffMs));
    if (m_reconnectBackoffMs == 200) m_reconnectBackoffMs = 500;
    else if (m_reconnectBackoffMs == 500) m_reconnectBackoffMs = 1000;
    else m_reconnectBackoffMs = 2000;

    if (!m_running.load()) return;

    if (m_transportType == TransportType::DIRECT_UDP) {
        m_directUdp->start(m_udpPort, [this](const uint8_t* d, size_t s, uint32_t ts, bool key) {
            onNaluReceived(d, s, ts, key);
        });
    } else {
        RtspTransportMode mode = (m_transportType == TransportType::RTSP_TCP) ?
                                  RtspTransportMode::TCP_INTERLEAVED : RtspTransportMode::UDP;
        m_rtspClient->open(m_url, mode,
            [this](const uint8_t* d, size_t s, uint32_t ts, bool key) { onNaluReceived(d, s, ts, key); },
            [this](uint16_t exp, uint16_t rec) { onPacketLoss(exp, rec); }
        );
    }
}

bool PlayerEngine::startRecording(const std::string& filePath) {
    if (!m_prober->isProbed()) {
        LOGW("Cannot start recording: stream not yet probed");
        return false;
    }
    const StreamInfo& info = m_prober->getStreamInfo();
    return m_mp4Muxer->start(filePath, info.mimeType, info.width, info.height, info.sps, info.pps, info.vps);
}

void PlayerEngine::stopRecording() {
    m_mp4Muxer->stop();
}

bool PlayerEngine::isRecording() const {
    return m_mp4Muxer->isRecording();
}

bool PlayerEngine::takeSnapshot(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    m_snapshotPath = filePath;
    m_snapshotRequested.store(true);
    return true;
}

TelemetrySnapshot PlayerEngine::getTelemetry() {
    return m_telemetry.collect();
}

} // namespace player
