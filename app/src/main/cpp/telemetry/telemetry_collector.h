#pragma once

#include <string>
#include <cstdint>
#include <chrono>
#include <atomic>

namespace player {

struct TelemetrySnapshot {
    std::string pipelineCombination;
    std::string transportProtocol;
    std::string codecResolution;
    double bitrateKbps = 0.0;
    double renderFps = 0.0;
    int sourceFps = 0;
    double decodeTimeMs = 0.0;
    uint64_t droppedFramesJitter = 0;
    uint64_t droppedFramesLoss = 0;
    double cpuRateApp = 0.0;
    double cpuRateTotal = 0.0;
    double ramPssMb = 0.0;
    double ramRssMb = 0.0;
    std::string gpuState;
};

class TelemetryCollector {
public:
    TelemetryCollector();
    ~TelemetryCollector() = default;

    void recordBytesReceived(size_t bytes);
    void recordSourceFrame();
    void recordRenderFrame();
    void recordJitterDrop();
    void recordPacketLoss(uint32_t count);
    void setDecodeTimeMs(double ms);

    void setPipelineInfo(const std::string& pipeline, const std::string& transport, const std::string& codecRes);

    TelemetrySnapshot collect();
    void reset();

private:
    void updateCpuStats();
    void updateMemoryStats();
    void updateGpuStats();

    std::string m_pipeline;
    std::string m_transport;
    std::string m_codecRes;

    std::atomic<uint64_t> m_bytesAccumulator{0};
    std::atomic<uint32_t> m_sourceFrameAccumulator{0};
    std::atomic<uint32_t> m_renderFrameAccumulator{0};
    std::atomic<uint64_t> m_droppedJitter{0};
    std::atomic<uint64_t> m_droppedLoss{0};
    std::atomic<double> m_decodeTimeMs{0.0};

    std::chrono::steady_clock::time_point m_lastSampleTime;
    TelemetrySnapshot m_lastSnapshot;

    // CPU measurement state
    uint64_t m_prevAppNs = 0;
    uint64_t m_prevMonoNs = 0;
    unsigned long long m_prevIdleTicks = 0;
    unsigned long long m_prevTotalTicks = 0;
};

} // namespace player
