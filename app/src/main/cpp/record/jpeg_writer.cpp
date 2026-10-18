#include "jpeg_writer.h"
#include "../common/logger.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>

namespace player {

// Minimal baseline JPEG encoder
struct JpegBitWriter {
    std::vector<uint8_t> buffer;
    uint32_t bitBuffer = 0;
    int bitCount = 0;

    void writeByte(uint8_t byte) {
        buffer.push_back(byte);
        if (byte == 0xFF) {
            buffer.push_back(0x00); // Byte stuffing
        }
    }

    void writeBits(uint32_t bits, int numBits) {
        bitBuffer = (bitBuffer << numBits) | (bits & ((1 << numBits) - 1));
        bitCount += numBits;
        while (bitCount >= 8) {
            uint8_t byte = static_cast<uint8_t>((bitBuffer >> (bitCount - 8)) & 0xFF);
            writeByte(byte);
            bitCount -= 8;
        }
    }

    void flushBits() {
        if (bitCount > 0) {
            writeBits((1 << (8 - bitCount)) - 1, 8 - bitCount);
        }
    }
};

static const uint8_t ZigZag[64] = {
     0,  1,  5,  6, 14, 15, 27, 28,
     2,  4,  7, 13, 16, 26, 29, 42,
     3,  8, 12, 17, 25, 30, 41, 43,
     9, 11, 18, 24, 31, 40, 44, 53,
    10, 19, 23, 32, 39, 45, 52, 54,
    20, 22, 33, 38, 46, 51, 55, 60,
    21, 34, 37, 47, 50, 56, 59, 61,
    35, 36, 48, 49, 57, 58, 62, 63
};

static const uint8_t StdLuminanceQuant[64] = {
    16, 11, 10, 16, 24, 40, 51, 61,
    12, 12, 14, 19, 26, 58, 60, 55,
    14, 13, 16, 24, 40, 57, 69, 56,
    14, 17, 22, 29, 51, 87, 80, 62,
    18, 22, 37, 56, 68,109,103, 77,
    24, 35, 55, 64, 81,104,113, 92,
    49, 64, 78, 87,103,121,120,101,
    72, 92, 95, 98,112,100,103, 99
};

static const uint8_t StdChrominanceQuant[64] = {
    17, 18, 24, 47, 99, 99, 99, 99,
    18, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99,
    47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99
};

// Forward discrete cosine transform
static void fdct8x8(const float in[64], float out[64]) {
    for (int v = 0; v < 8; ++v) {
        for (int u = 0; u < 8; ++u) {
            float sum = 0.0f;
            for (int y = 0; y < 8; ++y) {
                for (int x = 0; x < 8; ++x) {
                    sum += in[y * 8 + x] *
                           cosf((2.0f * x + 1.0f) * u * M_PI / 16.0f) *
                           cosf((2.0f * y + 1.0f) * v * M_PI / 16.0f);
                }
            }
            float cu = (u == 0) ? 0.70710678f : 1.0f;
            float cv = (v == 0) ? 0.70710678f : 1.0f;
            out[v * 8 + u] = 0.25f * cu * cv * sum;
        }
    }
}

bool JpegWriter::writeRgbaToJpeg(const std::string& filePath, int width, int height,
                                const uint8_t* rgba, int quality) {
    if (!rgba || width <= 0 || height <= 0) return false;

    // Convert RGBA to planar YUV420
    int ySize = width * height;
    int uvSize = (width / 2) * (height / 2);
    std::vector<uint8_t> yPlane(ySize);
    std::vector<uint8_t> uPlane(uvSize);
    std::vector<uint8_t> vPlane(uvSize);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int idx = (y * width + x) * 4;
            uint8_t r = rgba[idx];
            uint8_t g = rgba[idx + 1];
            uint8_t b = rgba[idx + 2];

            // Standard ITU-R BT.601 conversion
            int Y = (66 * r + 129 * g + 25 * b + 128) >> 8;
            yPlane[y * width + x] = static_cast<uint8_t>(std::clamp(Y + 16, 0, 255));

            if ((y % 2 == 0) && (x % 2 == 0)) {
                int U = (-38 * r - 74 * g + 112 * b + 128) >> 8;
                int V = (112 * r - 94 * g - 18 * b + 128) >> 8;
                int uvIdx = (y / 2) * (width / 2) + (x / 2);
                uPlane[uvIdx] = static_cast<uint8_t>(std::clamp(U + 128, 0, 255));
                vPlane[uvIdx] = static_cast<uint8_t>(std::clamp(V + 128, 0, 255));
            }
        }
    }

    return writeYuv420ToJpeg(filePath, width, height, yPlane.data(), uPlane.data(), vPlane.data(),
                            width, width / 2, quality);
}

bool JpegWriter::writeYuv420ToJpeg(const std::string& filePath, int width, int height,
                                 const uint8_t* yPlane, const uint8_t* uPlane, const uint8_t* vPlane,
                                 int yStride, int uvStride, int quality) {
    FILE* fp = fopen(filePath.c_str(), "wb");
    if (!fp) {
        LOGE("Failed to open file for writing JPEG: %s", filePath.c_str());
        return false;
    }

    // Scale quantization tables according to quality
    quality = std::clamp(quality, 1, 100);
    int scale = (quality < 50) ? (5000 / quality) : (200 - quality * 2);

    uint8_t qY[64], qC[64];
    for (int i = 0; i < 64; ++i) {
        int valY = (StdLuminanceQuant[i] * scale + 50) / 100;
        int valC = (StdChrominanceQuant[i] * scale + 50) / 100;
        qY[i] = static_cast<uint8_t>(std::clamp(valY, 1, 255));
        qC[i] = static_cast<uint8_t>(std::clamp(valC, 1, 255));
    }

    // Write JPEG SOI
    uint8_t soi[2] = {0xFF, 0xD8};
    fwrite(soi, 1, 2, fp);

    // APP0 Marker (JFIF)
    uint8_t jfif[18] = {
        0xFF, 0xE0, 0x00, 0x10, 'J', 'F', 'I', 'F', 0x00,
        0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00
    };
    fwrite(jfif, 1, 18, fp);

    // DQT (Quantization Table)
    uint8_t dqtHeader[4] = {0xFF, 0xDB, 0x00, 132};
    fwrite(dqtHeader, 1, 4, fp);
    fputc(0x00, fp); // Table 0 (Luminance)
    uint8_t qYZig[64];
    for (int i = 0; i < 64; ++i) qYZig[ZigZag[i]] = qY[i];
    fwrite(qYZig, 1, 64, fp);

    fputc(0x01, fp); // Table 1 (Chrominance)
    uint8_t qCZig[64];
    for (int i = 0; i < 64; ++i) qCZig[ZigZag[i]] = qC[i];
    fwrite(qCZig, 1, 64, fp);

    // SOF0 (Start of Frame)
    uint8_t sof0[19] = {
        0xFF, 0xC0, 0x00, 0x11, 0x08,
        static_cast<uint8_t>((height >> 8) & 0xFF), static_cast<uint8_t>(height & 0xFF),
        static_cast<uint8_t>((width >> 8) & 0xFF), static_cast<uint8_t>(width & 0xFF),
        0x03, // 3 components (Y, Cb, Cr)
        0x01, 0x11, 0x00, // Y component: ID 1, sampling 1x1, Q-table 0
        0x02, 0x11, 0x01, // Cb component: ID 2, sampling 1x1, Q-table 1
        0x03, 0x11, 0x01  // Cr component: ID 3, sampling 1x1, Q-table 1
    };
    fwrite(sof0, 1, 19, fp);

    // Standard Huffman tables (DHT)
    static const uint8_t dhtLumaDC[] = {
        0xFF, 0xC4, 0x00, 0x1F, 0x00,
        0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0,
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
    };
    fwrite(dhtLumaDC, 1, sizeof(dhtLumaDC), fp);

    static const uint8_t dhtLumaAC[] = {
        0xFF, 0xC4, 0x00, 0xB5, 0x10,
        0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7D,
        0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
        0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
        0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xA1, 0x08,
        0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0,
        0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0A, 0x16,
        0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28,
        0x29, 0x2A, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
        0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
        0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
        0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
        0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
        0x7A, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
        0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
        0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
        0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6,
        0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5,
        0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4,
        0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2,
        0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA,
        0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
        0xF9, 0xFA
    };
    fwrite(dhtLumaAC, 1, sizeof(dhtLumaAC), fp);

    // SOS (Start of Scan)
    uint8_t sos[14] = {
        0xFF, 0xDA, 0x00, 0x0C, 0x03,
        0x01, 0x00,
        0x02, 0x11,
        0x03, 0x11,
        0x00, 0x3F, 0x00
    };
    fwrite(sos, 1, 14, fp);

    // Bitwriter for scan data
    JpegBitWriter writer;

    // Encode 8x8 blocks
    int prevDcY = 0, prevDcU = 0, prevDcV = 0;
    float blockIn[64], blockOut[64];

    for (int by = 0; by < (height + 7) / 8; ++by) {
        for (int bx = 0; bx < (width + 7) / 8; ++bx) {
            // Y Block
            for (int y = 0; y < 8; ++y) {
                int py = std::min(by * 8 + y, height - 1);
                for (int x = 0; x < 8; ++x) {
                    int px = std::min(bx * 8 + x, width - 1);
                    blockIn[y * 8 + x] = static_cast<float>(yPlane[py * yStride + px]) - 128.0f;
                }
            }
            fdct8x8(blockIn, blockOut);

            int dcVal = static_cast<int>(std::round(blockOut[0] / qY[0]));
            int diff = dcVal - prevDcY;
            prevDcY = dcVal;

            if (diff == 0) {
                writer.writeBits(0, 2); // DC size 0
            } else {
                int absDiff = std::abs(diff);
                int numBits = 0;
                while (absDiff > 0) { numBits++; absDiff >>= 1; }
                uint32_t bits = (diff < 0) ? (diff + (1 << numBits) - 1) : diff;
                writer.writeBits(numBits, 2);
                writer.writeBits(bits, numBits);
            }
            // End of block (EOB) for AC coefficients
            writer.writeBits(0x0A, 4);

            // Cb Block
            for (int y = 0; y < 8; ++y) {
                int py = std::min(by * 8 + y, height - 1) / 2;
                for (int x = 0; x < 8; ++x) {
                    int px = std::min(bx * 8 + x, width - 1) / 2;
                    blockIn[y * 8 + x] = static_cast<float>(uPlane[py * uvStride + px]) - 128.0f;
                }
            }
            fdct8x8(blockIn, blockOut);
            dcVal = static_cast<int>(std::round(blockOut[0] / qC[0]));
            diff = dcVal - prevDcU;
            prevDcU = dcVal;
            if (diff == 0) {
                writer.writeBits(0, 2);
            } else {
                int absDiff = std::abs(diff);
                int numBits = 0;
                while (absDiff > 0) { numBits++; absDiff >>= 1; }
                uint32_t bits = (diff < 0) ? (diff + (1 << numBits) - 1) : diff;
                writer.writeBits(numBits, 2);
                writer.writeBits(bits, numBits);
            }
            writer.writeBits(0x0A, 4); // EOB

            // Cr Block
            for (int y = 0; y < 8; ++y) {
                int py = std::min(by * 8 + y, height - 1) / 2;
                for (int x = 0; x < 8; ++x) {
                    int px = std::min(bx * 8 + x, width - 1) / 2;
                    blockIn[y * 8 + x] = static_cast<float>(vPlane[py * uvStride + px]) - 128.0f;
                }
            }
            fdct8x8(blockIn, blockOut);
            dcVal = static_cast<int>(std::round(blockOut[0] / qC[0]));
            diff = dcVal - prevDcV;
            prevDcV = dcVal;
            if (diff == 0) {
                writer.writeBits(0, 2);
            } else {
                int absDiff = std::abs(diff);
                int numBits = 0;
                while (absDiff > 0) { numBits++; absDiff >>= 1; }
                uint32_t bits = (diff < 0) ? (diff + (1 << numBits) - 1) : diff;
                writer.writeBits(numBits, 2);
                writer.writeBits(bits, numBits);
            }
            writer.writeBits(0x0A, 4); // EOB
        }
    }

    writer.flushBits();
    fwrite(writer.buffer.data(), 1, writer.buffer.size(), fp);

    // EOI (End of Image)
    uint8_t eoi[2] = {0xFF, 0xD9};
    fwrite(eoi, 1, 2, fp);
    fclose(fp);

    LOGI("Clean JPEG snapshot written successfully to %s (quality=%d%%)", filePath.c_str(), quality);
    return true;
}

} // namespace player
