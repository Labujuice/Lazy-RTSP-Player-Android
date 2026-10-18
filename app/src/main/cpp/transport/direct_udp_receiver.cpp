#include "direct_udp_receiver.h"
#include "../common/logger.h"
#include <chrono>
#include <cstring>

namespace player {

DirectUdpReceiver::DirectUdpReceiver() {
    m_streamBuffer.reserve(1024 * 1024);
}

DirectUdpReceiver::~DirectUdpReceiver() {
    stop();
}

bool DirectUdpReceiver::start(int port, NaluCallback callback) {
    stop();

    m_port = port;
    m_callback = std::move(callback);

    if (!m_transport.bindUdp(m_port)) {
        LOGE("DirectUdpReceiver failed to bind port %d", m_port);
        return false;
    }

    m_streamBuffer.clear();
    m_frameCounter = 0;
    m_running.store(true);
    m_thread = std::thread(&DirectUdpReceiver::receiveLoop, this);

    LOGI("DirectUdpReceiver started successfully on port %d", m_port);
    return true;
}

void DirectUdpReceiver::stop() {
    if (m_running.load()) {
        m_running.store(false);
        m_transport.closeSocket();
        if (m_thread.joinable()) {
            m_thread.join();
        }
        LOGI("DirectUdpReceiver stopped");
    }
}

void DirectUdpReceiver::receiveLoop() {
    std::vector<uint8_t> recvBuf(65536);

    while (m_running.load()) {
        ssize_t n = m_transport.recvUdp(recvBuf.data(), recvBuf.size(), 100);
        if (n > 0) {
            processChunk(recvBuf.data(), static_cast<size_t>(n));
        } else if (n < 0 && m_running.load()) {
            LOGW("DirectUdpReceiver recv error");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

void DirectUdpReceiver::processChunk(const uint8_t* data, size_t size) {
    if (!data || size == 0) return;

    m_streamBuffer.insert(m_streamBuffer.end(), data, data + size);

    // Search for NAL start codes (0x00000001 or 0x000001)
    size_t startPos = 0;
    std::vector<size_t> naluStarts;

    for (size_t i = 0; i + 3 < m_streamBuffer.size(); ++i) {
        if (m_streamBuffer[i] == 0 && m_streamBuffer[i + 1] == 0) {
            if (m_streamBuffer[i + 2] == 1) {
                // 3-byte start code
                naluStarts.push_back(i);
            } else if (i + 3 < m_streamBuffer.size() && m_streamBuffer[i + 2] == 0 && m_streamBuffer[i + 3] == 1) {
                // 4-byte start code
                naluStarts.push_back(i);
                i += 1;
            }
        }
    }

    if (naluStarts.size() >= 2) {
        for (size_t k = 0; k + 1 < naluStarts.size(); ++k) {
            size_t naluStart = naluStarts[k];
            size_t naluEnd = naluStarts[k + 1];
            size_t naluLen = naluEnd - naluStart;

            const uint8_t* naluPtr = m_streamBuffer.data() + naluStart;

            // Determine if keyframe
            bool isKey = false;
            size_t headerOffset = 0;
            if (naluPtr[2] == 1) headerOffset = 3;
            else if (naluPtr[3] == 1) headerOffset = 4;

            if (naluLen > headerOffset) {
                uint8_t h264Type = naluPtr[headerOffset] & 0x1F;
                uint8_t h265Type = (naluPtr[headerOffset] >> 1) & 0x3F;
                if (h264Type == 5 || h264Type == 7 || h264Type == 8 ||
                    (h265Type >= 16 && h265Type <= 21) || (h265Type >= 32 && h265Type <= 34)) {
                    isKey = true;
                }
            }

            uint32_t ts = m_frameCounter++ * 3000; // ~30/60fps 90kHz clock ticks
            if (m_callback) {
                m_callback(naluPtr, naluLen, ts, isKey);
            }
            startPos = naluEnd;
        }

        // Erase emitted NALUs from stream buffer
        if (startPos > 0) {
            m_streamBuffer.erase(m_streamBuffer.begin(), m_streamBuffer.begin() + startPos);
        }
    }

    // Guard against runaway buffer if no start code is found
    if (m_streamBuffer.size() > 4 * 1024 * 1024) {
        LOGW("DirectUdpReceiver stream buffer exceeded 4MB without valid NALU, trimming");
        m_streamBuffer.erase(m_streamBuffer.begin(), m_streamBuffer.begin() + 2 * 1024 * 1024);
    }
}

} // namespace player
