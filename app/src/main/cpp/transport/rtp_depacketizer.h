#pragma once

#include <cstdint>
#include <vector>
#include <functional>
#include <string>

namespace player {

enum class VideoCodecType {
    UNKNOWN = 0,
    H264,
    H265,
    AV1
};

using NaluCallback = std::function<void(const uint8_t* data, size_t size, uint32_t timestamp, bool isKeyFrame)>;
using LossCallback = std::function<void(uint16_t expectedSeq, uint16_t receivedSeq)>;

class RtpDepacketizer {
public:
    explicit RtpDepacketizer(VideoCodecType codec = VideoCodecType::H264);
    ~RtpDepacketizer() = default;

    void setCodecType(VideoCodecType codec) { m_codec = codec; }
    VideoCodecType getCodecType() const { return m_codec; }

    void setNaluCallback(NaluCallback cb) { m_naluCallback = std::move(cb); }
    void setLossCallback(LossCallback cb) { m_lossCallback = std::move(cb); }

    bool processRtpPacket(const uint8_t* packet, size_t size);
    void reset();

    uint64_t getLostPacketCount() const { return m_lostPackets; }
    uint64_t getReceivedPacketCount() const { return m_receivedPackets; }

private:
    void processH264(const uint8_t* payload, size_t payloadSize, uint32_t timestamp, bool marker);
    void processH265(const uint8_t* payload, size_t payloadSize, uint32_t timestamp, bool marker);

    VideoCodecType m_codec = VideoCodecType::H264;
    NaluCallback m_naluCallback;
    LossCallback m_lossCallback;

    std::vector<uint8_t> m_assemblyBuffer;
    bool m_isAssembling = false;
    uint32_t m_currentTimestamp = 0;

    bool m_hasLastSeq = false;
    uint16_t m_lastSeq = 0;
    uint64_t m_receivedPackets = 0;
    uint64_t m_lostPackets = 0;
};

} // namespace player
