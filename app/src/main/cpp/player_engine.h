#pragma once

#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <android/native_window.h>

#include "common/ring_buffer_pool.h"
#include "transport/rtsp_client.h"
#include "transport/direct_udp_receiver.h"
#include "prober/codec_prober.h"
#include "decoder/video_decoder.h"
#include "decoder/hw_decoder.h"
#include "decoder/sw_decoder.h"
#include "record/mp4_muxer.h"
#include "record/jpeg_writer.h"
#include "telemetry/telemetry_collector.h"

namespace player {

enum class PipelineCombination {
    CPP_HW = 0,
    CPP_SW = 1,
    GSTREAMER_HW = 2,
    GSTREAMER_SW = 3
};

enum class TransportType {
    RTSP_UDP = 0,
    RTSP_TCP = 1,
    DIRECT_UDP = 2
};

enum ConnectionState {
    STATE_IDLE = 0,
    STATE_CONNECTING = 1,
    STATE_PROBING = 2,
    STATE_PLAYING = 3,
    STATE_DISCONNECTED = 4,
    STATE_ERROR = 5
};

using StateChangeCallback = std::function<void(int state, const std::string& message)>;

class PlayerEngine {
public:
    PlayerEngine();
    ~PlayerEngine();

    void setNativeWindow(ANativeWindow* window);
    void setStateCallback(StateChangeCallback callback);

    bool start(const std::string& url, TransportType transport,
               PipelineCombination pipeline, int udpPort = 5600);
    void stop();

    bool startRecording(const std::string& filePath);
    void stopRecording();
    bool isRecording() const;

    bool takeSnapshot(const std::string& filePath);

    TelemetrySnapshot getTelemetry();
    bool isPlaying() const { return m_state.load() == STATE_PLAYING; }

private:
    void onNaluReceived(const uint8_t* data, size_t size, uint32_t timestamp, bool isKeyFrame);
    void onPacketLoss(uint16_t expectedSeq, uint16_t receivedSeq);

    void decodeLoop();
    void watchdogLoop();
    void attemptReconnect();

    void initializeDecoder();
    void teardownDecoder();

    ANativeWindow* m_window = nullptr;
    StateChangeCallback m_stateCallback;

    std::string m_url;
    TransportType m_transportType = TransportType::RTSP_UDP;
    PipelineCombination m_pipelineType = PipelineCombination::CPP_HW;
    int m_udpPort = 5600;

    std::atomic<int> m_state{STATE_IDLE};
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_isDisconnected{false};
    std::atomic<bool> m_waitingForIdr{false};

    std::unique_ptr<RtspClient> m_rtspClient;
    std::unique_ptr<DirectUdpReceiver> m_directUdp;
    std::unique_ptr<CodecProber> m_prober;
    std::unique_ptr<VideoDecoder> m_decoder;
    std::unique_ptr<Mp4Muxer> m_mp4Muxer;
    std::unique_ptr<RingBufferPool> m_bufferPool;
    TelemetryCollector m_telemetry;

    // Queue of packets for decode thread (limited to 0~1 frame)
    std::queue<PacketBuffer*> m_decodeQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCv;

    std::thread m_decodeThread;
    std::thread m_watchdogThread;

    std::chrono::steady_clock::time_point m_lastPacketTime;
    uint32_t m_reconnectBackoffMs = 200;

    // Snapshot request state
    std::atomic<bool> m_snapshotRequested{false};
    std::string m_snapshotPath;
    std::mutex m_snapshotMutex;

    // Last keyframe / frame copy for freeze and snapshot
    std::vector<uint8_t> m_lastFrameBuffer;
    int m_frameWidth = 1920;
    int m_frameHeight = 1080;
};

} // namespace player
