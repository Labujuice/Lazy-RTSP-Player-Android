#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <android/native_window.h>

namespace player {

class VideoDecoder {
public:
    virtual ~VideoDecoder() = default;

    virtual bool init(const std::string& mimeType, int width, int height,
                      const std::vector<uint8_t>& sps,
                      const std::vector<uint8_t>& pps,
                      const std::vector<uint8_t>& vps,
                      ANativeWindow* window) = 0;

    virtual bool decode(const uint8_t* nalu, size_t size, uint64_t ptsUs, bool isKeyFrame) = 0;
    virtual void drainOutput(bool render = true) = 0;
    virtual void flush() = 0;
    virtual void release() = 0;

    virtual const char* getName() const = 0;
    virtual double getLastDecodeTimeMs() const = 0;
    virtual uint64_t getRenderedFrameCount() const = 0;
    virtual uint64_t getDroppedFrameCount() const = 0;
};

} // namespace player
