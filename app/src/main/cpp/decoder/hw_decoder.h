#pragma once

#include "video_decoder.h"
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <atomic>
#include <chrono>

namespace player {

class HwDecoder : public VideoDecoder {
public:
    HwDecoder();
    ~HwDecoder() override;

    bool init(const std::string& mimeType, int width, int height,
              const std::vector<uint8_t>& sps,
              const std::vector<uint8_t>& pps,
              const std::vector<uint8_t>& vps,
              ANativeWindow* window) override;

    bool decode(const uint8_t* nalu, size_t size, uint64_t ptsUs, bool isKeyFrame) override;
    int drainOutput(bool render = true) override;
    void flush() override;
    void release() override;

    const char* getName() const override { return "AMediaCodec (Hardware Zero-Copy)"; }
    double getLastDecodeTimeMs() const override { return m_lastDecodeTimeMs.load(); }
    uint64_t getRenderedFrameCount() const override { return m_renderedFrames.load(); }
    uint64_t getDroppedFrameCount() const override { return m_droppedFrames.load(); }

private:
    AMediaCodec* m_codec = nullptr;
    ANativeWindow* m_window = nullptr;
    bool m_isConfigured = false;

    std::atomic<double> m_lastDecodeTimeMs{0.0};
    std::atomic<uint64_t> m_renderedFrames{0};
    std::atomic<uint64_t> m_droppedFrames{0};
    std::chrono::steady_clock::time_point m_lastInputTime;
};

} // namespace player
