#include "sw_decoder.h"
#include "../common/logger.h"
#include <cstring>

namespace player {

SwDecoder::SwDecoder() = default;

SwDecoder::~SwDecoder() {
    release();
}

bool SwDecoder::init(const std::string& mimeType, int width, int height,
                     const std::vector<uint8_t>& sps,
                     const std::vector<uint8_t>& pps,
                     const std::vector<uint8_t>& vps,
                     ANativeWindow* window) {
    release();

    if (!window) {
        LOGE("SwDecoder init failed: ANativeWindow is null");
        return false;
    }
    m_window = window;

    LOGI("SwDecoder initializing for %s (%dx%d)", mimeType.c_str(), width, height);

    // Attempt to open standard Android software decoder first
    const char* swCodecNamesH264[] = {"c2.android.avc.decoder", "OMX.google.h264.decoder", nullptr};
    const char* swCodecNamesH265[] = {"c2.android.hevc.decoder", "OMX.google.hevc.decoder", nullptr};

    const char** candidates = (mimeType == "video/hevc") ? swCodecNamesH265 : swCodecNamesH264;
    for (int i = 0; candidates[i] != nullptr; ++i) {
        m_codec = AMediaCodec_createCodecByName(candidates[i]);
        if (m_codec) {
            LOGI("Found CPU software codec: %s", candidates[i]);
            break;
        }
    }

    if (!m_codec) {
        LOGW("Specific CPU software codec name not found, falling back to AMediaCodec_createDecoderByType");
        m_codec = AMediaCodec_createDecoderByType(mimeType.c_str());
    }

    if (!m_codec) {
        LOGE("Failed to create software codec for %s", mimeType.c_str());
        return false;
    }

    AMediaFormat* format = AMediaFormat_new();
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, mimeType.c_str());
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, width > 0 ? width : 1920);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, height > 0 ? height : 1080);

    if (mimeType == "video/avc") {
        if (!sps.empty()) AMediaFormat_setBuffer(format, "csd-0", sps.data(), sps.size());
        if (!pps.empty()) AMediaFormat_setBuffer(format, "csd-1", pps.data(), pps.size());
    } else if (mimeType == "video/hevc") {
        std::vector<uint8_t> csd0;
        if (!vps.empty()) csd0.insert(csd0.end(), vps.begin(), vps.end());
        if (!sps.empty()) csd0.insert(csd0.end(), sps.begin(), sps.end());
        if (!pps.empty()) csd0.insert(csd0.end(), pps.begin(), pps.end());
        if (!csd0.empty()) AMediaFormat_setBuffer(format, "csd-0", csd0.data(), csd0.size());
    }

    media_status_t status = AMediaCodec_configure(m_codec, format, m_window, nullptr, 0);
    AMediaFormat_delete(format);

    if (status != AMEDIA_OK) {
        LOGE("SwDecoder AMediaCodec_configure failed: %d", status);
        AMediaCodec_delete(m_codec);
        m_codec = nullptr;
        return false;
    }

    status = AMediaCodec_start(m_codec);
    if (status != AMEDIA_OK) {
        LOGE("SwDecoder AMediaCodec_start failed: %d", status);
        AMediaCodec_delete(m_codec);
        m_codec = nullptr;
        return false;
    }

    m_isConfigured = true;
    LOGI("SwDecoder started successfully");
    return true;
}

bool SwDecoder::decode(const uint8_t* nalu, size_t size, uint64_t ptsUs, bool /*isKeyFrame*/) {
    if (!m_codec || !m_isConfigured || !nalu || size == 0) return false;

    m_lastInputTime = std::chrono::steady_clock::now();

    ssize_t inIdx = AMediaCodec_dequeueInputBuffer(m_codec, 0);
    if (inIdx < 0) {
        drainOutput(true);
        inIdx = AMediaCodec_dequeueInputBuffer(m_codec, 1000);
    }

    if (inIdx >= 0) {
        size_t capacity = 0;
        uint8_t* buf = AMediaCodec_getInputBuffer(m_codec, inIdx, &capacity);
        if (buf && capacity >= size) {
            memcpy(buf, nalu, size);
            AMediaCodec_queueInputBuffer(m_codec, inIdx, 0, size, ptsUs, 0);
            return true;
        } else {
            AMediaCodec_queueInputBuffer(m_codec, inIdx, 0, 0, ptsUs, 0);
        }
    } else {
        m_droppedFrames++;
    }

    return false;
}

void SwDecoder::drainOutput(bool render) {
    if (!m_codec || !m_isConfigured) return;

    AMediaCodecBufferInfo info;
    while (true) {
        ssize_t outIdx = AMediaCodec_dequeueOutputBuffer(m_codec, &info, 0);
        if (outIdx >= 0) {
            auto now = std::chrono::steady_clock::now();
            double decodeMs = std::chrono::duration<double, std::milli>(now - m_lastInputTime).count();
            double currentAvg = m_lastDecodeTimeMs.load();
            if (currentAvg <= 0.0) {
                m_lastDecodeTimeMs.store(decodeMs);
            } else {
                m_lastDecodeTimeMs.store(0.8 * currentAvg + 0.2 * decodeMs);
            }

            AMediaCodec_releaseOutputBuffer(m_codec, outIdx, render);
            m_renderedFrames++;
        } else if (outIdx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            AMediaFormat* newFormat = AMediaCodec_getOutputFormat(m_codec);
            LOGI("SwDecoder format changed: %s", AMediaFormat_toString(newFormat));
            AMediaFormat_delete(newFormat);
        } else {
            break;
        }
    }
}

void SwDecoder::flush() {
    if (m_codec && m_isConfigured) {
        AMediaCodec_flush(m_codec);
    }
}

void SwDecoder::release() {
    if (m_codec) {
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
