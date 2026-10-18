#include "telemetry_collector.h"
#include "../common/logger.h"
#include <cstdio>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <sstream>

namespace player {

TelemetryCollector::TelemetryCollector() {
    m_pipeline = "C++ + HW Codec (Zero-Copy)";
    m_transport = "RTSP/UDP (Force)";
    m_codecRes = "Probing...";
    m_lastSampleTime = std::chrono::steady_clock::now();
    reset();
}

void TelemetryCollector::reset() {
    m_bytesAccumulator.store(0);
    m_sourceFrameAccumulator.store(0);
    m_renderFrameAccumulator.store(0);
    m_droppedJitter.store(0);
    m_droppedLoss.store(0);
    m_decodeTimeMs.store(0.0);
    m_lastSampleTime = std::chrono::steady_clock::now();
    m_prevAppNs = 0;
    m_prevMonoNs = 0;
    m_prevIdleTicks = 0;
    m_prevTotalTicks = 0;
}

void TelemetryCollector::recordBytesReceived(size_t bytes) {
    m_bytesAccumulator.fetch_add(bytes);
}

void TelemetryCollector::recordSourceFrame() {
    m_sourceFrameAccumulator.fetch_add(1);
}

void TelemetryCollector::recordRenderFrame() {
    m_renderFrameAccumulator.fetch_add(1);
}

void TelemetryCollector::recordJitterDrop() {
    m_droppedJitter.fetch_add(1);
}

void TelemetryCollector::recordPacketLoss(uint32_t count) {
    m_droppedLoss.fetch_add(count);
}

void TelemetryCollector::setDecodeTimeMs(double ms) {
    m_decodeTimeMs.store(ms);
}

void TelemetryCollector::setPipelineInfo(const std::string& pipeline, const std::string& transport, const std::string& codecRes) {
    m_pipeline = pipeline;
    m_transport = transport;
    m_codecRes = codecRes;
}

TelemetrySnapshot TelemetryCollector::collect() {
    auto now = std::chrono::steady_clock::now();
    double elapsedSec = std::chrono::duration<double>(now - m_lastSampleTime).count();

    if (elapsedSec >= 0.5) { // Update rates roughly every 500ms~1000ms
        uint64_t bytes = m_bytesAccumulator.exchange(0);
        uint32_t sourceFrames = m_sourceFrameAccumulator.exchange(0);
        uint32_t renderFrames = m_renderFrameAccumulator.exchange(0);

        m_lastSnapshot.bitrateKbps = (bytes * 8.0) / (elapsedSec * 1000.0);
        m_lastSnapshot.renderFps = renderFrames / elapsedSec;
        m_lastSnapshot.sourceFps = static_cast<int>(sourceFrames / elapsedSec);

        updateCpuStats();
        updateMemoryStats();
        updateGpuStats();

        m_lastSampleTime = now;
    }

    m_lastSnapshot.pipelineCombination = m_pipeline;
    m_lastSnapshot.transportProtocol = m_transport;
    m_lastSnapshot.codecResolution = m_codecRes;
    m_lastSnapshot.decodeTimeMs = m_decodeTimeMs.load();
    m_lastSnapshot.droppedFramesJitter = m_droppedJitter.load();
    m_lastSnapshot.droppedFramesLoss = m_droppedLoss.load();

    return m_lastSnapshot;
}

void TelemetryCollector::updateCpuStats() {
    // 1. High-resolution App CPU measurement via CLOCK_PROCESS_CPUTIME_ID and CLOCK_MONOTONIC
    struct timespec tsProcess{}, tsMonotonic{};
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &tsProcess);
    clock_gettime(CLOCK_MONOTONIC, &tsMonotonic);

    uint64_t currentAppNs = static_cast<uint64_t>(tsProcess.tv_sec) * 1000000000ULL + tsProcess.tv_nsec;
    uint64_t currentMonoNs = static_cast<uint64_t>(tsMonotonic.tv_sec) * 1000000000ULL + tsMonotonic.tv_nsec;

    long numCores = sysconf(_SC_NPROCESSORS_ONLN);
    if (numCores <= 0) numCores = 4;

    if (m_prevMonoNs > 0 && currentMonoNs > m_prevMonoNs) {
        uint64_t deltaMono = currentMonoNs - m_prevMonoNs;
        uint64_t deltaApp = (currentAppNs >= m_prevAppNs) ? (currentAppNs - m_prevAppNs) : 0;

        // Normalized App CPU usage across all cores (0.0% ~ 100.0%)
        double appRate = (static_cast<double>(deltaApp) / static_cast<double>(deltaMono * numCores)) * 100.0;
        if (appRate > 100.0) appRate = 100.0;
        m_lastSnapshot.cpuRateApp = appRate;
    }

    m_prevAppNs = currentAppNs;
    m_prevMonoNs = currentMonoNs;

    // 2. Read total ticks from /proc/stat if accessible (root or older Android)
    unsigned long long user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0;
    FILE* procStat = fopen("/proc/stat", "r");
    if (procStat) {
        char cpuLabel[16];
        if (fscanf(procStat, "%s %llu %llu %llu %llu %llu %llu %llu",
                   cpuLabel, &user, &nice, &system, &idle, &iowait, &irq, &softirq) == 8) {
            unsigned long long currentTotalTicks = user + nice + system + idle + iowait + irq + softirq;
            if (m_prevTotalTicks > 0 && currentTotalTicks > m_prevTotalTicks) {
                unsigned long long totalDiff = currentTotalTicks - m_prevTotalTicks;
                unsigned long long idleDiff = (idle >= m_prevIdleTicks) ? (idle - m_prevIdleTicks) : 0;
                double totalRate = (static_cast<double>(totalDiff - idleDiff) / static_cast<double>(totalDiff)) * 100.0;
                m_lastSnapshot.cpuRateTotal = totalRate;
            }
            m_prevIdleTicks = idle;
            m_prevTotalTicks = currentTotalTicks;
        }
        fclose(procStat);
    } else {
        // /proc/stat is protected by SELinux on modern Android (API 26+)
        m_lastSnapshot.cpuRateTotal = -1.0;
    }
}

void TelemetryCollector::updateMemoryStats() {
    FILE* statusFile = fopen("/proc/self/status", "r");
    if (!statusFile) return;

    char line[256];
    double rssKb = 0;
    while (fgets(line, sizeof(line), statusFile)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line + 6, "%lf", &rssKb);
            break;
        }
    }
    fclose(statusFile);

    m_lastSnapshot.ramRssMb = rssKb / 1024.0;
    m_lastSnapshot.ramPssMb = (rssKb * 0.85) / 1024.0; // Approximation of PSS
}

void TelemetryCollector::updateGpuStats() {
    // Attempt reading Adreno / Mali sysfs if accessible
    const char* gpuPaths[] = {
        "/sys/class/kgsl/kgsl-3d0/gpuclk",
        "/sys/class/kgsl/kgsl-3d0/gpubusy",
        "/sys/class/devfreq/1c00000.qcom,kgsl-3d0/cur_freq",
        "/sys/class/devfreq/gpufreq/cur_freq",
        nullptr
    };

    bool found = false;
    for (int i = 0; gpuPaths[i] != nullptr; ++i) {
        FILE* fp = fopen(gpuPaths[i], "r");
        if (fp) {
            char val[64];
            if (fgets(val, sizeof(val), fp)) {
                long freq = atol(val);
                if (freq > 1000000) freq /= 1000000; // Hz to MHz
                else if (freq > 1000) freq /= 1000;
                m_lastSnapshot.gpuState = std::to_string(freq) + " MHz";
                found = true;
            }
            fclose(fp);
            if (found) break;
        }
    }

    if (!found) {
        m_lastSnapshot.gpuState = "N/A (Protected)";
    }
}

} // namespace player
