#include "codec_prober.h"
#include "../common/logger.h"
#include <cstring>

namespace player {

// Helper bit reader for H.264 / H.265 SPS
class BitReader {
public:
    BitReader(const uint8_t* data, size_t size) : m_data(data), m_size(size) {}

    uint32_t readBits(int n) {
        uint32_t result = 0;
        for (int i = 0; i < n; ++i) {
            result = (result << 1) | readBit();
        }
        return result;
    }

    uint32_t readBit() {
        if (m_byteOffset >= m_size) return 0;
        uint32_t bit = (m_data[m_byteOffset] >> (7 - m_bitOffset)) & 1;
        m_bitOffset++;
        if (m_bitOffset == 8) {
            m_bitOffset = 0;
            m_byteOffset++;
            // Check emulation prevention byte 0x000003
            if (m_byteOffset + 2 < m_size &&
                m_data[m_byteOffset - 1] == 0 &&
                m_data[m_byteOffset] == 0 &&
                m_data[m_byteOffset + 1] == 3) {
                m_byteOffset++; // skip 0x03
            }
        }
        return bit;
    }

    uint32_t readExponentialGolomb() {
        int leadingZeroBits = 0;
        while (readBit() == 0 && leadingZeroBits < 32 && m_byteOffset < m_size) {
            leadingZeroBits++;
        }
        if (leadingZeroBits == 0) return 0;
        return (1 << leadingZeroBits) - 1 + readBits(leadingZeroBits);
    }

    int32_t readSignedExponentialGolomb() {
        uint32_t codeNum = readExponentialGolomb();
        if (codeNum % 2 == 0) {
            return -static_cast<int32_t>(codeNum / 2);
        } else {
            return static_cast<int32_t>((codeNum + 1) / 2);
        }
    }

private:
    const uint8_t* m_data;
    size_t m_size;
    size_t m_byteOffset = 0;
    int m_bitOffset = 0;
};

CodecProber::CodecProber() {
    reset();
}

void CodecProber::reset() {
    m_info = StreamInfo();
    m_probePacketCount = 0;
}

bool CodecProber::probeNalu(const uint8_t* nalu, size_t size) {
    if (!nalu || size < 4) return false;
    m_probePacketCount++;

    size_t i = 0;
    while (i + 3 < size) {
        size_t startCodeLen = 0;
        if (nalu[i] == 0 && nalu[i + 1] == 0) {
            if (nalu[i + 2] == 1) {
                startCodeLen = 3;
            } else if (i + 4 <= size && nalu[i + 2] == 0 && nalu[i + 3] == 1) {
                startCodeLen = 4;
            }
        }

        if (startCodeLen == 0) {
            i++;
            continue;
        }

        size_t naluStart = i + startCodeLen;
        size_t nextStart = size;
        for (size_t j = naluStart; j + 2 < size; ++j) {
            if (nalu[j] == 0 && nalu[j + 1] == 0 &&
                (nalu[j + 2] == 1 || (j + 3 < size && nalu[j + 2] == 0 && nalu[j + 3] == 1))) {
                nextStart = j;
                break;
            }
        }

        size_t naluTotalSize = nextStart - i;
        size_t payloadSize = nextStart - naluStart;

        if (payloadSize > 0) {
            uint8_t firstByte = nalu[naluStart];
            uint8_t h264Type = firstByte & 0x1F;
            uint8_t h265Type = (firstByte >> 1) & 0x3F;

            // Check H.264 SPS / PPS
            if (h264Type == 7) { // SPS
                LOGI("CodecProber: Detected H.264 SPS (size=%zu)", naluTotalSize);
                m_info.codec = VideoCodecType::H264;
                m_info.mimeType = "video/avc";
                m_info.sps.assign(nalu + i, nalu + nextStart);
                parseH264Sps(nalu + naluStart + 1, payloadSize - 1);
            } else if (h264Type == 8) { // PPS
                LOGI("CodecProber: Detected H.264 PPS (size=%zu)", naluTotalSize);
                m_info.pps.assign(nalu + i, nalu + nextStart);
            }

            // Check H.265 VPS / SPS / PPS
            if (h265Type == 32) { // VPS
                LOGI("CodecProber: Detected H.265 VPS (size=%zu)", naluTotalSize);
                m_info.codec = VideoCodecType::H265;
                m_info.mimeType = "video/hevc";
                m_info.vps.assign(nalu + i, nalu + nextStart);
            } else if (h265Type == 33) { // SPS
                LOGI("CodecProber: Detected H.265 SPS (size=%zu)", naluTotalSize);
                m_info.codec = VideoCodecType::H265;
                m_info.mimeType = "video/hevc";
                m_info.sps.assign(nalu + i, nalu + nextStart);
                parseH265Sps(nalu + naluStart + 2, payloadSize - 2);
            } else if (h265Type == 34) { // PPS
                LOGI("CodecProber: Detected H.265 PPS (size=%zu)", naluTotalSize);
                m_info.pps.assign(nalu + i, nalu + nextStart);
            }
        }

        i = nextStart;
    }

    // Probing condition satisfied: Require BOTH SPS and PPS
    if (m_info.codec == VideoCodecType::H264 && !m_info.sps.empty() && !m_info.pps.empty()) {
        m_info.isProbed = true;
    } else if (m_info.codec == VideoCodecType::H265 && !m_info.sps.empty() && !m_info.pps.empty()) {
        m_info.isProbed = true;
    } else if (m_probePacketCount >= 30) {
        if (m_info.codec == VideoCodecType::UNKNOWN) {
            m_info.codec = VideoCodecType::H264;
            m_info.mimeType = "video/avc";
        }
        if (m_info.width <= 0) m_info.width = 1280;
        if (m_info.height <= 0) m_info.height = 720;
        m_info.isProbed = true;
        LOGI("CodecProber fallback: Forced probe after 30 packets (%s %dx%d)",
             m_info.mimeType.c_str(), m_info.width, m_info.height);
    }

    return m_info.isProbed;
}

bool CodecProber::parseH264Sps(const uint8_t* data, size_t size) {
    if (size < 3) return false;
    BitReader reader(data, size);

    m_info.profile = reader.readBits(8);
    reader.readBits(8); // constraint_set_flags
    m_info.level = reader.readBits(8);

    reader.readExponentialGolomb(); // seq_parameter_set_id

    if (m_info.profile == 100 || m_info.profile == 110 ||
        m_info.profile == 122 || m_info.profile == 244 ||
        m_info.profile == 44  || m_info.profile == 83  ||
        m_info.profile == 86  || m_info.profile == 118 ||
        m_info.profile == 128) {
        
        uint32_t chromaFormat = reader.readExponentialGolomb();
        if (chromaFormat == 3) {
            reader.readBit(); // separate_colour_plane_flag
        }
        reader.readExponentialGolomb(); // bit_depth_luma_minus8
        reader.readExponentialGolomb(); // bit_depth_chroma_minus8
        reader.readBit(); // qpprime_y_zero_transform_bypass_flag
        if (reader.readBit()) { // seq_scaling_matrix_present_flag
            for (int i = 0; i < ((chromaFormat != 3) ? 8 : 12); ++i) {
                if (reader.readBit()) { // seq_scaling_list_present_flag
                    int lastScale = 8, nextScale = 8;
                    int sizeOfScalingList = (i < 6) ? 16 : 64;
                    for (int j = 0; j < sizeOfScalingList; ++j) {
                        if (nextScale != 0) {
                            int32_t deltaScale = reader.readSignedExponentialGolomb();
                            nextScale = (lastScale + deltaScale + 256) % 256;
                        }
                        lastScale = (nextScale == 0) ? lastScale : nextScale;
                    }
                }
            }
        }
    }

    reader.readExponentialGolomb(); // log2_max_frame_num_minus4
    uint32_t picOrderCntType = reader.readExponentialGolomb();
    if (picOrderCntType == 0) {
        reader.readExponentialGolomb(); // log2_max_pic_order_cnt_lsb_minus4
    } else if (picOrderCntType == 1) {
        reader.readBit(); // delta_pic_order_always_zero_flag
        reader.readSignedExponentialGolomb(); // offset_for_non_ref_pic
        reader.readSignedExponentialGolomb(); // offset_for_top_to_bottom_field
        uint32_t numRefFramesInPicOrderCntCycle = reader.readExponentialGolomb();
        for (uint32_t i = 0; i < numRefFramesInPicOrderCntCycle; ++i) {
            reader.readSignedExponentialGolomb();
        }
    }

    reader.readExponentialGolomb(); // max_num_ref_frames
    reader.readBit(); // gaps_in_frame_num_value_allowed_flag

    uint32_t picWidthInMbsMinus1 = reader.readExponentialGolomb();
    uint32_t picHeightInMapUnitsMinus1 = reader.readExponentialGolomb();
    uint32_t frameMbsOnlyFlag = reader.readBit();

    if (!frameMbsOnlyFlag) {
        reader.readBit(); // mb_adaptive_frame_field_flag
    }
    reader.readBit(); // direct_8x8_inference_flag

    uint32_t cropLeft = 0, cropRight = 0, cropTop = 0, cropBottom = 0;
    if (reader.readBit()) { // frame_cropping_flag
        cropLeft = reader.readExponentialGolomb();
        cropRight = reader.readExponentialGolomb();
        cropTop = reader.readExponentialGolomb();
        cropBottom = reader.readExponentialGolomb();
    }

    int width = (picWidthInMbsMinus1 + 1) * 16 - (cropLeft + cropRight) * 2;
    int height = (2 - frameMbsOnlyFlag) * (picHeightInMapUnitsMinus1 + 1) * 16 - (cropTop + cropBottom) * 2;

    if (width > 0 && height > 0) {
        m_info.width = width;
        m_info.height = height;
        LOGI("CodecProber H.264 parsed successfully: %dx%d (profile=%d, level=%d)",
             m_info.width, m_info.height, m_info.profile, m_info.level);
        return true;
    }

    // Default fallback resolution if parsing was incomplete
    m_info.width = 1920;
    m_info.height = 1080;
    return false;
}

bool CodecProber::parseH265Sps(const uint8_t* data, size_t size) {
    if (size < 15) return false;
    BitReader reader(data, size);

    reader.readBits(4); // sps_video_parameter_set_id
    uint32_t maxSubLayersMinus1 = reader.readBits(3);
    reader.readBit(); // sps_temporal_id_nesting_flag

    // Profile tier level (PTL)
    reader.readBits(2); // general_profile_space
    reader.readBit();  // general_tier_flag
    m_info.profile = reader.readBits(5);
    reader.readBits(32); // general_profile_compatibility_flags
    reader.readBits(48); // general_constraint_indicator_flags
    m_info.level = reader.readBits(8);

    std::vector<bool> subLayerProfilePresent(maxSubLayersMinus1);
    std::vector<bool> subLayerLevelPresent(maxSubLayersMinus1);
    for (uint32_t i = 0; i < maxSubLayersMinus1; ++i) {
        subLayerProfilePresent[i] = reader.readBit();
        subLayerLevelPresent[i] = reader.readBit();
    }
    if (maxSubLayersMinus1 > 0) {
        for (uint32_t i = maxSubLayersMinus1; i < 8; ++i) {
            reader.readBits(2);
        }
    }
    for (uint32_t i = 0; i < maxSubLayersMinus1; ++i) {
        if (subLayerProfilePresent[i]) reader.readBits(88);
        if (subLayerLevelPresent[i]) reader.readBits(8);
    }

    reader.readExponentialGolomb(); // sps_seq_parameter_set_id
    uint32_t chromaFormatIdc = reader.readExponentialGolomb();
    if (chromaFormatIdc == 3) {
        reader.readBit(); // separate_colour_plane_flag
    }

    uint32_t picWidthInLumaSamples = reader.readExponentialGolomb();
    uint32_t picHeightInLumaSamples = reader.readExponentialGolomb();

    if (reader.readBit()) { // conformance_window_flag
        reader.readExponentialGolomb(); // conf_win_left_offset
        reader.readExponentialGolomb(); // conf_win_right_offset
        reader.readExponentialGolomb(); // conf_win_top_offset
        reader.readExponentialGolomb(); // conf_win_bottom_offset
    }

    if (picWidthInLumaSamples > 0 && picHeightInLumaSamples > 0) {
        m_info.width = static_cast<int>(picWidthInLumaSamples);
        m_info.height = static_cast<int>(picHeightInLumaSamples);
        LOGI("CodecProber H.265 parsed successfully: %dx%d (profile=%d, level=%d)",
             m_info.width, m_info.height, m_info.profile, m_info.level);
        return true;
    }

    m_info.width = 1920;
    m_info.height = 1080;
    return false;
}

} // namespace player
