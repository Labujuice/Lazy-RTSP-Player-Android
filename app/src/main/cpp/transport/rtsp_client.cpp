#include "rtsp_client.h"
#include "../common/logger.h"
#include <sstream>
#include <algorithm>
#include <cstring>
#include <cctype>

namespace player {

RtspClient::RtspClient() = default;

RtspClient::~RtspClient() {
    close();
}

bool RtspClient::parseUrl(const std::string& url) {
    m_url = url;
    // Expected: rtsp://host[:port][/path]
    std::string prefix = "rtsp://";
    if (url.rfind(prefix, 0) != 0) {
        LOGE("Invalid RTSP URL (must start with rtsp://): %s", url.c_str());
        return false;
    }

    std::string rest = url.substr(prefix.length());
    // Strip user:pass if present
    size_t atPos = rest.find('@');
    if (atPos != std::string::npos) {
        rest = rest.substr(atPos + 1);
    }

    size_t slashPos = rest.find('/');
    std::string hostPort;
    if (slashPos != std::string::npos) {
        hostPort = rest.substr(0, slashPos);
        m_path = rest.substr(slashPos);
    } else {
        hostPort = rest;
        m_path = "/";
    }

    size_t colonPos = hostPort.find(':');
    if (colonPos != std::string::npos) {
        m_host = hostPort.substr(0, colonPos);
        m_port = std::stoi(hostPort.substr(colonPos + 1));
    } else {
        m_host = hostPort;
        m_port = 554;
    }

    LOGI("Parsed RTSP URL: host=%s, port=%d, path=%s", m_host.c_str(), m_port, m_path.c_str());
    return true;
}

bool RtspClient::sendRequest(const std::string& req) {
    LOGV("RTSP >>> \n%s", req.c_str());
    ssize_t sent = m_tcpTransport.sendTcp(req.data(), req.size());
    return sent == static_cast<ssize_t>(req.size());
}

std::string RtspClient::receiveResponse(int timeoutMs) {
    std::string resp;
    char buf[1024];
    int elapsed = 0;
    while (elapsed < timeoutMs) {
        ssize_t n = m_tcpTransport.recvTcp(buf, sizeof(buf) - 1, 100);
        if (n > 0) {
            buf[n] = '\0';
            resp.append(buf, n);
            if (resp.find("\r\n\r\n") != std::string::npos) {
                // If Content-Length present, read body
                size_t clPos = resp.find("Content-Length: ");
                if (clPos == std::string::npos) {
                    clPos = resp.find("content-length: ");
                }
                if (clPos != std::string::npos) {
                    size_t endLine = resp.find("\r\n", clPos);
                    int cl = std::stoi(resp.substr(clPos + 16, endLine - (clPos + 16)));
                    size_t headerEnd = resp.find("\r\n\r\n") + 4;
                    while (resp.length() - headerEnd < static_cast<size_t>(cl) && elapsed < timeoutMs) {
                        ssize_t bodyN = m_tcpTransport.recvTcp(buf, sizeof(buf) - 1, 100);
                        if (bodyN > 0) {
                            resp.append(buf, bodyN);
                        }
                        elapsed += 100;
                    }
                }
                break;
            }
        }
        elapsed += 100;
    }
    LOGV("RTSP <<< \n%s", resp.c_str());
    return resp;
}

bool RtspClient::parseSdp(const std::string& sdp) {
    // Check video codec
    std::string lowerSdp = sdp;
    std::transform(lowerSdp.begin(), lowerSdp.end(), lowerSdp.begin(), ::tolower);

    if (lowerSdp.find("h265") != std::string::npos || lowerSdp.find("hevc") != std::string::npos) {
        m_depacketizer.setCodecType(VideoCodecType::H265);
        LOGI("SDP parsed: Detected H.265 / HEVC stream");
    } else {
        m_depacketizer.setCodecType(VideoCodecType::H264);
        LOGI("SDP parsed: Detected H.264 stream");
    }

    // Extract control uri
    size_t videoPos = sdp.find("m=video");
    if (videoPos != std::string::npos) {
        size_t controlPos = sdp.find("a=control:", videoPos);
        if (controlPos != std::string::npos) {
            size_t endLine = sdp.find("\r\n", controlPos);
            std::string ctrl = sdp.substr(controlPos + 10, endLine - (controlPos + 10));
            if (ctrl.find("rtsp://") == 0) {
                m_videoControlUri = ctrl;
            } else if (ctrl == "*") {
                m_videoControlUri = m_url;
            } else {
                if (!m_url.empty() && m_url.back() == '/') {
                    m_videoControlUri = m_url + ctrl;
                } else {
                    m_videoControlUri = m_url + "/" + ctrl;
                }
            }
        }
    }

    if (m_videoControlUri.empty()) {
        m_videoControlUri = m_url;
    }
    LOGI("RTSP Video Track Control URI: %s", m_videoControlUri.c_str());
    return true;
}

bool RtspClient::open(const std::string& url, RtspTransportMode mode, NaluCallback naluCb, LossCallback lossCb) {
    close();

    if (!parseUrl(url)) return false;

    m_mode = mode;
    m_naluCallback = std::move(naluCb);
    m_lossCallback = std::move(lossCb);
    m_depacketizer.setNaluCallback(m_naluCallback);
    m_depacketizer.setLossCallback(m_lossCallback);
    m_depacketizer.reset();
    m_cseq = 1;

    if (!m_tcpTransport.connectTcp(m_host, m_port, 3000)) {
        LOGE("Failed to connect to RTSP server %s:%d", m_host.c_str(), m_port);
        return false;
    }

    // 1. OPTIONS
    std::ostringstream ss;
    ss << "OPTIONS " << m_url << " RTSP/1.0\r\n"
       << "CSeq: " << m_cseq++ << "\r\n"
       << "User-Agent: UltraLowLatencyRTSPPlayer/1.3\r\n\r\n";
    if (!sendRequest(ss.str())) return false;
    std::string resp = receiveResponse();
    if (resp.find("200 OK") == std::string::npos) {
        LOGW("OPTIONS response did not contain 200 OK");
    }

    // 2. DESCRIBE
    ss.str("");
    ss.clear();
    ss << "DESCRIBE " << m_url << " RTSP/1.0\r\n"
       << "CSeq: " << m_cseq++ << "\r\n"
       << "Accept: application/sdp\r\n"
       << "User-Agent: UltraLowLatencyRTSPPlayer/1.3\r\n\r\n";
    if (!sendRequest(ss.str())) return false;
    resp = receiveResponse();
    if (resp.find("200 OK") == std::string::npos) {
        LOGE("DESCRIBE failed: %s", resp.c_str());
        return false;
    }
    parseSdp(resp);

    // 3. SETUP
    int clientRtpPort = 50000 + (rand() % 1000) * 2;
    int clientRtcpPort = clientRtpPort + 1;

    ss.str("");
    ss.clear();
    ss << "SETUP " << m_videoControlUri << " RTSP/1.0\r\n"
       << "CSeq: " << m_cseq++ << "\r\n";

    if (m_mode == RtspTransportMode::UDP) {
        if (!m_rtpUdpTransport.bindUdp(clientRtpPort) || !m_rtcpUdpTransport.bindUdp(clientRtcpPort)) {
            LOGE("Failed to bind local RTP/RTCP UDP ports %d/%d", clientRtpPort, clientRtcpPort);
            return false;
        }
        ss << "Transport: RTP/AVP;unicast;client_port=" << clientRtpPort << "-" << clientRtcpPort << "\r\n";
    } else {
        ss << "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n";
    }
    ss << "User-Agent: UltraLowLatencyRTSPPlayer/1.3\r\n\r\n";

    if (!sendRequest(ss.str())) return false;
    resp = receiveResponse();
    if (resp.find("200 OK") == std::string::npos) {
        LOGE("SETUP failed: %s", resp.c_str());
        return false;
    }

    // Extract Session
    size_t sessPos = resp.find("Session: ");
    if (sessPos == std::string::npos) sessPos = resp.find("session: ");
    if (sessPos != std::string::npos) {
        size_t endSess = resp.find_first_of(";\r\n", sessPos + 9);
        m_session = resp.substr(sessPos + 9, endSess - (sessPos + 9));
        LOGI("RTSP Session ID: %s", m_session.c_str());
    }

    // 4. PLAY
    ss.str("");
    ss.clear();
    ss << "PLAY " << m_url << " RTSP/1.0\r\n"
       << "CSeq: " << m_cseq++ << "\r\n";
    if (!m_session.empty()) {
        ss << "Session: " << m_session << "\r\n";
    }
    ss << "Range: npt=0.000-\r\n"
       << "User-Agent: UltraLowLatencyRTSPPlayer/1.3\r\n\r\n";

    if (!sendRequest(ss.str())) return false;
    resp = receiveResponse();
    if (resp.find("200 OK") == std::string::npos) {
        LOGE("PLAY failed: %s", resp.c_str());
        return false;
    }

    LOGI("RTSP Play command accepted, launching stream receiver loop (mode=%s)",
         (m_mode == RtspTransportMode::UDP ? "UDP" : "TCP Interleaved"));

    m_running.store(true);
    if (m_mode == RtspTransportMode::TCP_INTERLEAVED) {
        m_streamThread = std::thread(&RtspClient::streamLoopTcp, this);
    } else {
        m_streamThread = std::thread(&RtspClient::streamLoopUdp, this);
    }

    return true;
}

void RtspClient::close() {
    if (m_running.load()) {
        m_running.store(false);

        // Send TEARDOWN
        if (!m_session.empty() && m_tcpTransport.getFd() >= 0) {
            std::ostringstream ss;
            ss << "TEARDOWN " << m_url << " RTSP/1.0\r\n"
               << "CSeq: " << m_cseq++ << "\r\n"
               << "Session: " << m_session << "\r\n"
               << "User-Agent: UltraLowLatencyRTSPPlayer/1.3\r\n\r\n";
            sendRequest(ss.str());
        }

        m_tcpTransport.closeSocket();
        m_rtpUdpTransport.closeSocket();
        m_rtcpUdpTransport.closeSocket();

        if (m_streamThread.joinable()) {
            m_streamThread.join();
        }
        LOGI("RtspClient closed");
    }
}

void RtspClient::streamLoopTcp() {
    std::vector<uint8_t> buffer(65536);
    while (m_running.load()) {
        // Read interleaved frame: '$' <channel: 1> <length: 2>
        uint8_t magic = 0;
        ssize_t n = m_tcpTransport.recvTcp(&magic, 1, 100);
        if (n <= 0) continue;

        if (magic == '$') {
            uint8_t header[3];
            size_t got = 0;
            while (got < 3 && m_running.load()) {
                ssize_t r = m_tcpTransport.recvTcp(header + got, 3 - got, 100);
                if (r > 0) got += r;
            }
            if (got != 3) continue;

            uint8_t channel = header[0];
            uint16_t length = (static_cast<uint16_t>(header[1]) << 8) | header[2];

            if (buffer.size() < length) {
                buffer.resize(length);
            }

            size_t payloadRead = 0;
            while (payloadRead < length && m_running.load()) {
                ssize_t r = m_tcpTransport.recvTcp(buffer.data() + payloadRead, length - payloadRead, 100);
                if (r > 0) payloadRead += r;
            }

            if (payloadRead == length && channel == 0) {
                // Video RTP packet
                m_depacketizer.processRtpPacket(buffer.data(), length);
            }
        }
    }
}

void RtspClient::streamLoopUdp() {
    std::vector<uint8_t> rtpBuf(65536);
    while (m_running.load()) {
        ssize_t n = m_rtpUdpTransport.recvUdp(rtpBuf.data(), rtpBuf.size(), 100);
        if (n > 0) {
            m_depacketizer.processRtpPacket(rtpBuf.data(), static_cast<size_t>(n));
        }
    }
}

} // namespace player
