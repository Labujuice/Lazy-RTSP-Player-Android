#pragma once

#include "socket_transport.h"
#include "rtp_depacketizer.h"
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <functional>
#include <mutex>

namespace player {

enum class RtspTransportMode {
    UDP = 0,
    TCP_INTERLEAVED = 1
};

class RtspClient {
public:
    RtspClient();
    ~RtspClient();

    bool open(const std::string& url, RtspTransportMode mode, NaluCallback naluCb, LossCallback lossCb);
    void close();
    bool isConnected() const { return m_running.load(); }
    RtspTransportMode getTransportMode() const { return m_mode; }
    VideoCodecType getDetectedCodec() const { return m_depacketizer.getCodecType(); }
    uint64_t getLostPackets() const { return m_depacketizer.getLostPacketCount(); }
    uint64_t getReceivedPackets() const { return m_depacketizer.getReceivedPacketCount(); }

private:
    bool parseUrl(const std::string& url);
    bool sendRequest(const std::string& req);
    std::string receiveResponse(int timeoutMs = 3000);
    bool parseSdp(const std::string& sdp);

    void streamLoopTcp();
    void streamLoopUdp();
    void heartbeatLoop();

    std::string m_url;
    std::string m_host;
    int m_port = 554;
    std::string m_path = "/";
    RtspTransportMode m_mode = RtspTransportMode::UDP;

    SocketTransport m_tcpTransport;
    SocketTransport m_rtpUdpTransport;
    SocketTransport m_rtcpUdpTransport;
    std::mutex m_tcpMutex;

    RtpDepacketizer m_depacketizer;
    NaluCallback m_naluCallback;
    LossCallback m_lossCallback;

    int m_cseq = 1;
    std::string m_session;
    int m_sessionTimeoutSec = 55;
    std::string m_videoControlUri;

    std::atomic<bool> m_running{false};
    std::thread m_streamThread;
    std::thread m_heartbeatThread;
};

} // namespace player
