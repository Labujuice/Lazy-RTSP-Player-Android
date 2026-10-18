#include "mp4_muxer.h"
#include "../common/logger.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

namespace player {

Mp4Muxer::Mp4Muxer() = default;

Mp4Muxer::~Mp4Muxer() {
    stop();
}

bool Mp4Muxer::start(const std::string& outputPath, const std::string& mimeType,
                     int width, int height,
                     const std::vector<uint8_t>& sps,
                     const std::vector<uint8_t>& pps,
                     const std::vector<uint8_t>& vps) {
    stop();
    std::lock_guard<std::mutex> lock(m_mutex);

    m_filePath = outputPath;
    m_fd = open(outputPath.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (m_fd < 0) {
        LOGE("Mp4Muxer failed to open output file: %s (errno=%s)", outputPath.c_str(), strerror(errno));
        return false;
    }

    m_muxer = AMediaMuxer_new(m_fd, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);
    if (!m_muxer) {
        LOGE("AMediaMuxer_new failed");
        close(m_fd);
        m_fd = -1;
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

    m_trackIndex = AMediaMuxer_addTrack(m_muxer, format);
    AMediaFormat_delete(format);

    if (m_trackIndex < 0) {
        LOGE("AMediaMuxer_addTrack failed");
        AMediaMuxer_delete(m_muxer);
        m_muxer = nullptr;
        close(m_fd);
        m_fd = -1;
        return false;
    }

    media_status_t status = AMediaMuxer_start(m_muxer);
    if (status != AMEDIA_OK) {
        LOGE("AMediaMuxer_start failed with status %d", status);
        AMediaMuxer_delete(m_muxer);
        m_muxer = nullptr;
        close(m_fd);
        m_fd = -1;
        return false;
    }

    m_firstPtsUs = 0;
    m_recording.store(true);
    LOGI("Mp4Muxer started recording successfully to %s", outputPath.c_str());
    return true;
}

void Mp4Muxer::writeSample(const uint8_t* nalu, size_t size, uint64_t ptsUs, bool isKeyFrame) {
    if (!m_recording.load() || !m_muxer || m_trackIndex < 0 || !nalu || size == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_recording.load()) return;

    if (m_firstPtsUs == 0) {
        m_firstPtsUs = ptsUs;
    }

    uint64_t relativePtsUs = (ptsUs >= m_firstPtsUs) ? (ptsUs - m_firstPtsUs) : 0;

    AMediaCodecBufferInfo info{};
    info.offset = 0;
    info.size = static_cast<int32_t>(size);
    info.presentationTimeUs = static_cast<int64_t>(relativePtsUs);
    info.flags = isKeyFrame ? AMEDIACODEC_BUFFER_FLAG_KEY_FRAME : 0;

    AMediaMuxer_writeSampleData(m_muxer, static_cast<size_t>(m_trackIndex), nalu, &info);
}

void Mp4Muxer::stop() {
    if (m_recording.load()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_recording.store(false);
        if (m_muxer) {
            LOGI("Stopping AMediaMuxer");
            AMediaMuxer_stop(m_muxer);
            AMediaMuxer_delete(m_muxer);
            m_muxer = nullptr;
        }
        if (m_fd >= 0) {
            close(m_fd);
            m_fd = -1;
        }
        m_trackIndex = -1;
        LOGI("Mp4Muxer recording stopped: %s", m_filePath.c_str());
    }
}

} // namespace player
