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
    // 1. Read app ticks from /proc/self/stat
    unsigned long utime = 0, stime = 0;
    FILE* statFile = fopen("/proc/self/stat", "r");
    if (statFile) {
        // pid (comm) state ppid pgrp session tty_nr tpgid flags minflt cminflt majflt cmajflt utime(14) stime(15)
        if (fscanf(statFile, "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*lu %*lu %*lu %*lu %lu %lu",
                   &utime, &stime) == 2) {
            // Read success
        }
        fclose(statFile);
    }

    // 2. Read total ticks from /proc/stat
    unsigned long long user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0;
    FILE* procStat = fopen("/proc/stat", "r");
    if (procStat) {
        char cpuLabel[16];
        if (fscanf(procStat, "%s %llu %llu %llu %llu %llu %llu %llu",
                   cpuLabel, &user, &nice, &system, &idle, &iowait, &irq, &softirq) == 8) {
            // Read success
        }
        fclose(procStat);
    }

    unsigned long long currentAppTicks = utime + stime;
    unsigned long long currentTotalTicks = user + nice + system + idle + iowait + irq + softirq;

    if (m_prevTotalTicks > 0 && currentTotalTicks > m_prevTotalTicks) {
        unsigned long long totalDiff = currentTotalTicks - m_prevTotalTicks;
        unsigned long long appDiff = (currentAppTicks >= m_prevAppTicks) ? (currentAppTicks - m_prevAppTicks) : 0;

        long numCores = sysconf(_SC_NPROCESSORS_ONLN);
        if (numCores <= 0) numCores = 4;

        m_lastSnapshot.cpuRateApp = (static_cast<double>(appDiff) / static_cast<double>(totalDiff)) * 100.0 * numCores;
        m_lastSnapshot.cpuRateTotal = (static_cast<double>(totalDiff - idle) / static_cast<double>(totalDiff)) * 100.0;
    }

    m_prevAppTicks = currentAppTicks;
    m_prevTotalTicks = currentTotalTicks;
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
