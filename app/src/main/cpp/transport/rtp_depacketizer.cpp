#include "rtp_depacketizer.h"
#include "../common/logger.h"
#include <cstring>

namespace player {

static const uint8_t START_CODE[4] = {0x00, 0x00, 0x00, 0x01};

RtpDepacketizer::RtpDepacketizer(VideoCodecType codec) : m_codec(codec) {
    m_assemblyBuffer.reserve(512 * 1024);
}

void RtpDepacketizer::reset() {
    m_assemblyBuffer.clear();
    m_isAssembling = false;
    m_auBuffer.clear();
    m_auHasData = false;
    m_auIsKeyFrame = false;
    m_hasLastSeq = false;
    m_lastSeq = 0;
    m_receivedPackets = 0;
    m_lostPackets = 0;
}

bool RtpDepacketizer::processRtpPacket(const uint8_t* packet, size_t size) {
    if (!packet || size < 12) {
        return false;
    }

    // RTP Header:
    // byte 0: V(2), P(1), X(1), CC(4)
    uint8_t v = (packet[0] >> 6) & 0x03;
    if (v != 2) {
        // Not RTP version 2
        return false;
    }

    uint8_t p = (packet[0] >> 5) & 0x01;
    uint8_t x = (packet[0] >> 4) & 0x01;
    uint8_t cc = packet[0] & 0x0F;

    bool marker = (packet[1] >> 7) & 0x01;
    // uint8_t pt = packet[1] & 0x7F;

    uint16_t seq = (static_cast<uint16_t>(packet[2]) << 8) | packet[3];
    uint32_t timestamp = (static_cast<uint32_t>(packet[4]) << 24) |
                         (static_cast<uint32_t>(packet[5]) << 16) |
                         (static_cast<uint32_t>(packet[6]) << 8) |
                         packet[7];

    // Sequence tracking and loss detection
    m_receivedPackets++;
    if (m_hasLastSeq) {
        uint16_t expectedSeq = m_lastSeq + 1;
        if (seq != expectedSeq) {
            uint16_t gap = seq - expectedSeq;
            m_lostPackets += gap;
            LOGW("RTP packet loss detected! Expected seq=%u, received=%u (lost %u packets)",
                 expectedSeq, seq, gap);
            if (m_lossCallback) {
                m_lossCallback(expectedSeq, seq);
            }
        }
    }
    m_lastSeq = seq;
    m_hasLastSeq = true;

    size_t headerLen = 12 + (cc * 4);
    if (size < headerLen) {
        return false;
    }

    if (x) {
        if (size < headerLen + 4) return false;
        uint16_t extLen = (static_cast<uint16_t>(packet[headerLen + 2]) << 8) | packet[headerLen + 3];
        headerLen += 4 + (extLen * 4);
        if (size < headerLen) return false;
    }

    size_t payloadSize = size - headerLen;
    if (p) {
        // Padding at end
        if (payloadSize == 0) return false;
        uint8_t padLen = packet[size - 1];
        if (padLen > payloadSize) return false;
        payloadSize -= padLen;
    }

    if (payloadSize == 0) {
        return true;
    }

    const uint8_t* payload = packet + headerLen;

    if (m_codec == VideoCodecType::H264) {
        processH264(payload, payloadSize, timestamp, marker);
    } else if (m_codec == VideoCodecType::H265) {
        processH265(payload, payloadSize, timestamp, marker);
    }

    return true;
}

void RtpDepacketizer::appendNaluToAu(const uint8_t* nalu, size_t size, uint32_t timestamp, bool isKey, bool marker) {
    if (!nalu || size == 0) return;

    // If timestamp changed, flush the previous Access Unit first
    if (m_auHasData && timestamp != m_auTimestamp) {
        flushCurrentAu();
    }

    m_auBuffer.insert(m_auBuffer.end(), START_CODE, START_CODE + 4);
    m_auBuffer.insert(m_auBuffer.end(), nalu, nalu + size);
    m_auTimestamp = timestamp;
    m_auHasData = true;
    if (isKey) {
        m_auIsKeyFrame = true;
    }

    if (marker) {
        flushCurrentAu();
    }
}

void RtpDepacketizer::flushCurrentAu() {
    if (m_auHasData && m_naluCallback) {
        m_naluCallback(m_auBuffer.data(), m_auBuffer.size(), m_auTimestamp, m_auIsKeyFrame);
    }
    m_auBuffer.clear();
    m_auHasData = false;
    m_auIsKeyFrame = false;
}

void RtpDepacketizer::processH264(const uint8_t* payload, size_t payloadSize, uint32_t timestamp, bool marker) {
    uint8_t nalType = payload[0] & 0x1F;

    if (nalType >= 1 && nalType <= 23) {
        // Single NAL unit packet
        bool isKey = (nalType == 5 || nalType == 7 || nalType == 8);
        appendNaluToAu(payload, payloadSize, timestamp, isKey, marker);
    } else if (nalType == 28) {
        // FU-A fragmentation
        if (payloadSize < 2) return;
        uint8_t fuIndicator = payload[0];
        uint8_t fuHeader = payload[1];
        bool startBit = (fuHeader & 0x80) != 0;
        bool endBit = (fuHeader & 0x40) != 0;
        uint8_t innerType = fuHeader & 0x1F;
        bool isKey = (innerType == 5);

        if (startBit) {
            m_isAssembling = true;
            m_currentTimestamp = timestamp;
            m_assemblyBuffer.clear();
            uint8_t reconstructedHeader = (fuIndicator & 0xE0) | innerType;
            m_assemblyBuffer.push_back(reconstructedHeader);
            m_assemblyBuffer.insert(m_assemblyBuffer.end(), payload + 2, payload + payloadSize);
        } else if (m_isAssembling) {
            m_assemblyBuffer.insert(m_assemblyBuffer.end(), payload + 2, payload + payloadSize);
        }

        if (endBit && m_isAssembling) {
            m_isAssembling = false;
            appendNaluToAu(m_assemblyBuffer.data(), m_assemblyBuffer.size(), m_currentTimestamp, isKey, marker);
        }
    } else if (nalType == 24) {
        // STAP-A aggregation packet
        size_t offset = 1;
        while (offset + 2 <= payloadSize) {
            uint16_t naluSize = (static_cast<uint16_t>(payload[offset]) << 8) | payload[offset + 1];
            offset += 2;
            if (offset + naluSize > payloadSize) break;

            uint8_t subType = payload[offset] & 0x1F;
            bool isKey = (subType == 5 || subType == 7 || subType == 8);
            bool isLastInPacket = (offset + naluSize == payloadSize);

            appendNaluToAu(payload + offset, naluSize, timestamp, isKey, isLastInPacket && marker);
            offset += naluSize;
        }
    }
}

void RtpDepacketizer::processH265(const uint8_t* payload, size_t payloadSize, uint32_t timestamp, bool marker) {
    if (payloadSize < 2) return;
    uint8_t nalType = (payload[0] >> 1) & 0x3F;

    if (nalType <= 47) {
        // Single NAL unit packet
        bool isKey = (nalType >= 16 && nalType <= 21) || (nalType >= 32 && nalType <= 34);
        appendNaluToAu(payload, payloadSize, timestamp, isKey, marker);
    } else if (nalType == 49) {
        // Fragmentation Unit (FU)
        if (payloadSize < 3) return;
        uint8_t fuHeader = payload[2];
        bool startBit = (fuHeader & 0x80) != 0;
        bool endBit = (fuHeader & 0x40) != 0;
        uint8_t innerType = fuHeader & 0x3F;
        bool isKey = (innerType >= 16 && innerType <= 21);

        if (startBit) {
            m_isAssembling = true;
            m_currentTimestamp = timestamp;
            m_assemblyBuffer.clear();

            // Reconstruct 2-byte NAL header
            uint8_t byte0 = (payload[0] & 0x81) | ((innerType & 0x3F) << 1);
            uint8_t byte1 = payload[1];
            m_assemblyBuffer.push_back(byte0);
            m_assemblyBuffer.push_back(byte1);
            m_assemblyBuffer.insert(m_assemblyBuffer.end(), payload + 3, payload + payloadSize);
        } else if (m_isAssembling) {
            m_assemblyBuffer.insert(m_assemblyBuffer.end(), payload + 3, payload + payloadSize);
        }

        if (endBit && m_isAssembling) {
            m_isAssembling = false;
            appendNaluToAu(m_assemblyBuffer.data(), m_assemblyBuffer.size(), m_currentTimestamp, isKey, marker);
        }
    } else if (nalType == 48) {
        // AP (Aggregation Packet)
        size_t offset = 2;
        while (offset + 2 <= payloadSize) {
            uint16_t naluSize = (static_cast<uint16_t>(payload[offset]) << 8) | payload[offset + 1];
            offset += 2;
            if (offset + naluSize > payloadSize) break;

            uint8_t subType = (payload[offset] >> 1) & 0x3F;
            bool isKey = (subType >= 16 && subType <= 21) || (subType >= 32 && subType <= 34);
            bool isLastInPacket = (offset + naluSize == payloadSize);

            appendNaluToAu(payload + offset, naluSize, timestamp, isKey, isLastInPacket && marker);
            offset += naluSize;
        }
    }
}

} // namespace player
