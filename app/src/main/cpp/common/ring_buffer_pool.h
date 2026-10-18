#pragma once

#include <vector>
#include <mutex>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <atomic>
#include "logger.h"

namespace player {

struct PacketBuffer {
    uint8_t* data = nullptr;
    size_t capacity = 0;
    size_t size = 0;
    uint32_t timestamp = 0;
    uint16_t sequenceNumber = 0;
    bool isKeyFrame = false;
    int poolIndex = -1;

    void reset() {
        size = 0;
        timestamp = 0;
        sequenceNumber = 0;
        isKeyFrame = false;
    }
};

class RingBufferPool {
public:
    explicit RingBufferPool(size_t poolSize = 128, size_t bufferCapacity = 256 * 1024)
        : m_poolSize(poolSize), m_bufferCapacity(bufferCapacity) {
        
        m_rawMemory.resize(m_poolSize * m_bufferCapacity);
        m_buffers.resize(m_poolSize);
        m_freeIndices.reserve(m_poolSize);

        for (size_t i = 0; i < m_poolSize; ++i) {
            m_buffers[i].data = m_rawMemory.data() + (i * m_bufferCapacity);
            m_buffers[i].capacity = m_bufferCapacity;
            m_buffers[i].size = 0;
            m_buffers[i].poolIndex = static_cast<int>(i);
            m_freeIndices.push_back(i);
        }
        LOGI("RingBufferPool initialized: %zu buffers, %zu KB each, total memory: %zu MB",
             m_poolSize, m_bufferCapacity / 1024, (m_poolSize * m_bufferCapacity) / (1024 * 1024));
    }

    ~RingBufferPool() = default;

    // Disallow copy
    RingBufferPool(const RingBufferPool&) = delete;
    RingBufferPool& operator=(const RingBufferPool&) = delete;

    PacketBuffer* acquire(int timeoutMs = 20) {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_freeIndices.empty()) {
            if (timeoutMs > 0) {
                if (!m_cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this]() {
                    return !m_freeIndices.empty();
                })) {
                    LOGW("RingBufferPool acquire timed out, pool exhausted!");
                    return nullptr;
                }
            } else {
                return nullptr;
            }
        }

        size_t index = m_freeIndices.back();
        m_freeIndices.pop_back();
        PacketBuffer* buf = &m_buffers[index];
        buf->reset();
        return buf;
    }

    void release(PacketBuffer* buf) {
        if (!buf || buf->poolIndex < 0 || static_cast<size_t>(buf->poolIndex) >= m_poolSize) {
            return;
        }

        std::unique_lock<std::mutex> lock(m_mutex);
        m_freeIndices.push_back(buf->poolIndex);
        m_cv.notify_one();
    }

    size_t getAvailableCount() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_freeIndices.size();
    }

    size_t getTotalCount() const {
        return m_poolSize;
    }

private:
    size_t m_poolSize;
    size_t m_bufferCapacity;
    std::vector<uint8_t> m_rawMemory;
    std::vector<PacketBuffer> m_buffers;
    std::vector<size_t> m_freeIndices;
    std::mutex m_mutex;
    std::condition_variable m_cv;
};

} // namespace player
