#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include "../transport/rtp_depacketizer.h"

namespace player {

struct StreamInfo {
    VideoCodecType codec = VideoCodecType::UNKNOWN;
    std::string mimeType;
    int width = 0;
    int height = 0;
    int profile = 0;
    int level = 0;
    std::vector<uint8_t> sps;
    std::vector<uint8_t> pps;
    std::vector<uint8_t> vps;
    bool isProbed = false;
};

class CodecProber {
public:
    CodecProber();
    ~CodecProber() = default;

    void reset();
    bool probeNalu(const uint8_t* nalu, size_t size);
    bool isProbed() const { return m_info.isProbed; }
    const StreamInfo& getStreamInfo() const { return m_info; }

private:
    bool parseH264Sps(const uint8_t* data, size_t size);
    bool parseH265Sps(const uint8_t* data, size_t size);

    StreamInfo m_info;
    int m_probePacketCount = 0;
};

} // namespace player
