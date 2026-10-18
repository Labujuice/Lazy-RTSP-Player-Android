#pragma once

#include <media/NdkMediaMuxer.h>
#include <media/NdkMediaFormat.h>
#include <string>
#include <vector>
#include <cstdint>
#include <atomic>
#include <mutex>

namespace player {

class Mp4Muxer {
public:
    Mp4Muxer();
    ~Mp4Muxer();

    bool start(const std::string& outputPath, const std::string& mimeType,
               int width, int height,
               const std::vector<uint8_t>& sps,
               const std::vector<uint8_t>& pps,
               const std::vector<uint8_t>& vps);

    void writeSample(const uint8_t* nalu, size_t size, uint64_t ptsUs, bool isKeyFrame);
    void stop();
    bool isRecording() const { return m_recording.load(); }
    const std::string& getFilePath() const { return m_filePath; }

private:
    AMediaMuxer* m_muxer = nullptr;
    int m_fd = -1;
    ssize_t m_trackIndex = -1;
    std::string m_filePath;
    std::atomic<bool> m_recording{false};
    std::mutex m_mutex;
    uint64_t m_firstPtsUs = 0;
};

} // namespace player
