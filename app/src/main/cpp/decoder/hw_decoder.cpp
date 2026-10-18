#include "hw_decoder.h"
#include "../common/logger.h"
#include <cstring>

namespace player {

HwDecoder::HwDecoder() = default;

HwDecoder::~HwDecoder() {
    release();
}

bool HwDecoder::init(const std::string& mimeType, int width, int height,
                     const std::vector<uint8_t>& sps,
                     const std::vector<uint8_t>& pps,
                     const std::vector<uint8_t>& vps,
                     ANativeWindow* window) {
    release();

    if (!window) {
        LOGE("HwDecoder init failed: ANativeWindow is null");
        return false;
    }
    m_window = window;

    LOGI("HwDecoder initializing for %s (%dx%d)", mimeType.c_str(), width, height);
    m_codec = AMediaCodec_createDecoderByType(mimeType.c_str());
    if (!m_codec) {
        LOGE("AMediaCodec_createDecoderByType failed for %s", mimeType.c_str());
        return false;
    }

    AMediaFormat* format = AMediaFormat_new();
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, mimeType.c_str());
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, width > 0 ? width : 1920);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, height > 0 ? height : 1080);

    // Ultra low latency config flags
    AMediaFormat_setInt32(format, "low-latency", 1);
    AMediaFormat_setInt32(format, "vendor.qcom-ext-dec-low-latency.enable", 1);
    AMediaFormat_setInt32(format, "vendor.hisi-ext-dec-low-latency.enable", 1);

    if (mimeType == "video/avc") {
        if (!sps.empty()) {
            AMediaFormat_setBuffer(format, "csd-0", sps.data(), sps.size());
        }
        if (!pps.empty()) {
            AMediaFormat_setBuffer(format, "csd-1", pps.data(), pps.size());
        }
    } else if (mimeType == "video/hevc") {
        // For HEVC, csd-0 often contains VPS + SPS + PPS
        std::vector<uint8_t> csd0;
        if (!vps.empty()) csd0.insert(csd0.end(), vps.begin(), vps.end());
        if (!sps.empty()) csd0.insert(csd0.end(), sps.begin(), sps.end());
        if (!pps.empty()) csd0.insert(csd0.end(), pps.begin(), pps.end());
        if (!csd0.empty()) {
            AMediaFormat_setBuffer(format, "csd-0", csd0.data(), csd0.size());
        }
    }

    // Configure with ANativeWindow for Zero-Copy hardware decoding
    media_status_t status = AMediaCodec_configure(m_codec, format, m_window, nullptr, 0);
    AMediaFormat_delete(format);

    if (status != AMEDIA_OK) {
        LOGE("AMediaCodec_configure failed with status: %d", status);
        AMediaCodec_delete(m_codec);
        m_codec = nullptr;
        return false;
    }

    status = AMediaCodec_start(m_codec);
    if (status != AMEDIA_OK) {
        LOGE("AMediaCodec_start failed with status: %d", status);
        AMediaCodec_delete(m_codec);
        m_codec = nullptr;
        return false;
    }

    m_isConfigured = true;
    LOGI("HwDecoder started successfully with Zero-Copy to ANativeWindow");
    return true;
}

bool HwDecoder::decode(const uint8_t* nalu, size_t size, uint64_t ptsUs, bool /*isKeyFrame*/) {
    if (!m_codec || !m_isConfigured || !nalu || size == 0) {
        return false;
    }

    m_lastInputTime = std::chrono::steady_clock::now();

    // Zero-timeout dequeue for maximum throughput and minimum latency
    ssize_t inIdx = AMediaCodec_dequeueInputBuffer(m_codec, 0);
    if (inIdx < 0) {
        // Decoder input queue is momentarily full, drain output first
        drainOutput(true);
        inIdx = AMediaCodec_dequeueInputBuffer(m_codec, 1000); // 1ms wait
    }

    if (inIdx >= 0) {
        size_t capacity = 0;
        uint8_t* buf = AMediaCodec_getInputBuffer(m_codec, inIdx, &capacity);
        if (buf && capacity >= size) {
            memcpy(buf, nalu, size);
            uint32_t flags = 0;
            AMediaCodec_queueInputBuffer(m_codec, inIdx, 0, size, ptsUs, flags);
            return true;
        } else {
            LOGW("Input buffer capacity (%zu) smaller than NALU (%zu)", capacity, size);
            AMediaCodec_queueInputBuffer(m_codec, inIdx, 0, 0, ptsUs, 0);
        }
    } else {
        m_droppedFrames++;
    }

    return false;
}

int HwDecoder::drainOutput(bool render) {
    if (!m_codec || !m_isConfigured) return 0;

    AMediaCodecBufferInfo info;
    int tryCount = 0;
    int rendered = 0;
    while (true) {
        int timeoutUs = (tryCount == 0) ? 2000 : 0;
        ssize_t outIdx = AMediaCodec_dequeueOutputBuffer(m_codec, &info, timeoutUs);
        tryCount++;
        if (outIdx >= 0) {
            auto now = std::chrono::steady_clock::now();
            double decodeMs = std::chrono::duration<double, std::milli>(now - m_lastInputTime).count();
            // Moving average for decode time
            double currentAvg = m_lastDecodeTimeMs.load();
            if (currentAvg <= 0.0) {
                m_lastDecodeTimeMs.store(decodeMs);
            } else {
                m_lastDecodeTimeMs.store(0.8 * currentAvg + 0.2 * decodeMs);
            }

            // Zero-copy render directly to ANativeWindow
            media_status_t status = AMediaCodec_releaseOutputBuffer(m_codec, outIdx, render);
            if (status != AMEDIA_OK) {
                LOGW("AMediaCodec_releaseOutputBuffer failed with status: %d", status);
            }
            m_renderedFrames++;
            if (render) {
                rendered++;
            }
        } else if (outIdx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            AMediaFormat* newFormat = AMediaCodec_getOutputFormat(m_codec);
            LOGI("AMediaCodec output format changed: %s", AMediaFormat_toString(newFormat));
            AMediaFormat_delete(newFormat);
        } else if (outIdx == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
            LOGD("AMediaCodec output buffers changed");
        } else {
            // No more buffers ready
            break;
        }
    }
    return rendered;
}

void HwDecoder::flush() {
    if (m_codec && m_isConfigured) {
        LOGI("Flushing AMediaCodec hardware decoder");
        media_status_t status = AMediaCodec_flush(m_codec);
        if (status != AMEDIA_OK) {
            LOGW("AMediaCodec_flush returned: %d", status);
        }
    }
}

void HwDecoder::release() {
    if (m_codec) {
        LOGI("Releasing AMediaCodec hardware decoder");
        if (m_isConfigured) {
            AMediaCodec_stop(m_codec);
            m_isConfigured = false;
        }
        AMediaCodec_delete(m_codec);
        m_codec = nullptr;
    }
    m_window = nullptr;
}

} // namespace player
