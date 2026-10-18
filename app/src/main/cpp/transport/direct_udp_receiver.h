#pragma once

#include "socket_transport.h"
#include "rtp_depacketizer.h"
#include <thread>
#include <atomic>
#include <vector>
#include <functional>

namespace player {

class DirectUdpReceiver {
public:
    DirectUdpReceiver();
    ~DirectUdpReceiver();

    bool start(int port, NaluCallback callback);
    void stop();
    bool isRunning() const { return m_running.load(); }
    int getPort() const { return m_port; }

private:
    void receiveLoop();
    void processChunk(const uint8_t* data, size_t size);

    SocketTransport m_transport;
    int m_port = 5600;
    std::atomic<bool> m_running{false};
    std::thread m_thread;
    NaluCallback m_callback;

    std::vector<uint8_t> m_streamBuffer;
    uint32_t m_frameCounter = 0;
};

} // namespace player
