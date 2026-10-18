#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace player {

class JpegWriter {
public:
    static bool writeRgbaToJpeg(const std::string& filePath, int width, int height,
                                const uint8_t* rgba, int quality = 95);

    static bool writeYuv420ToJpeg(const std::string& filePath, int width, int height,
                                 const uint8_t* yPlane, const uint8_t* uPlane, const uint8_t* vPlane,
                                 int yStride, int uvStride, int quality = 95);
};

} // namespace player
