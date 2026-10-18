#include "socket_transport.h"
#include "../common/logger.h"
#include <netdb.h>
#include <cstring>
#include <cerrno>

namespace player {

SocketTransport::SocketTransport() : m_fd(-1), m_boundPort(0), m_isUdp(false) {}

SocketTransport::~SocketTransport() {
    closeSocket();
}

void SocketTransport::closeSocket() {
    if (m_fd >= 0) {
        close(m_fd);
        m_fd = -1;
    }
    m_boundPort = 0;
}

bool SocketTransport::connectTcp(const std::string& host, int port, int timeoutMs) {
    closeSocket();
    m_isUdp = false;

    struct addrinfo hints{}, *res = nullptr;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    std::string portStr = std::to_string(port);
    if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0 || !res) {
        LOGE("Failed to resolve host: %s", host.c_str());
        return false;
    }

    m_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (m_fd < 0) {
        LOGE("Failed to create TCP socket: %s", strerror(errno));
        freeaddrinfo(res);
        return false;
    }

    // Set non-blocking
    int flags = fcntl(m_fd, F_GETFL, 0);
    fcntl(m_fd, F_SETFL, flags | O_NONBLOCK);

    int ret = connect(m_fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);

    if (ret < 0 && errno != EINPROGRESS) {
        LOGE("TCP connect failed: %s", strerror(errno));
        closeSocket();
        return false;
    }

    if (ret != 0) {
        struct pollfd pfd{};
        pfd.fd = m_fd;
        pfd.events = POLLOUT;
        int pollRet = poll(&pfd, 1, timeoutMs);
        if (pollRet <= 0) {
            LOGE("TCP connect timeout (%d ms) or poll error", timeoutMs);
            closeSocket();
            return false;
        }

        int so_error = 0;
        socklen_t len = sizeof(so_error);
        getsockopt(m_fd, SOL_SOCKET, SO_ERROR, &so_error, &len);
        if (so_error != 0) {
            LOGE("TCP socket error after connect: %s", strerror(so_error));
            closeSocket();
            return false;
        }
    }

    // Set socket receive buffer size
    int rcvbuf = 1024 * 1024;
    setsockopt(m_fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    LOGI("TCP connected to %s:%d successfully", host.c_str(), port);
    return true;
}

ssize_t SocketTransport::sendTcp(const void* data, size_t size) {
    if (m_fd < 0) return -1;
    return send(m_fd, data, size, MSG_NOSIGNAL);
}

ssize_t SocketTransport::recvTcp(void* buffer, size_t size, int timeoutMs) {
    if (m_fd < 0) return -1;

    if (timeoutMs > 0) {
        struct pollfd pfd{};
        pfd.fd = m_fd;
        pfd.events = POLLIN;
        int pollRet = poll(&pfd, 1, timeoutMs);
        if (pollRet < 0) return -1;
        if (pollRet == 0) return 0; // timeout
    }

    ssize_t ret = recv(m_fd, buffer, size, 0);
    if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return 0;
    }
    return ret;
}

bool SocketTransport::bindUdp(int port) {
    closeSocket();
    m_isUdp = true;

    m_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (m_fd < 0) {
        LOGE("Failed to create UDP socket: %s", strerror(errno));
        return false;
    }

    // Reuse address and port
    int opt = 1;
    setsockopt(m_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
    setsockopt(m_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

    // Increase buffer size to prevent packet loss under high bitrate
    int rcvbuf = 2 * 1024 * 1024;
    setsockopt(m_fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(m_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOGE("Failed to bind UDP port %d: %s", port, strerror(errno));
        closeSocket();
        return false;
    }

    // Query actual bound port if requested port was 0
    socklen_t addrLen = sizeof(addr);
    if (getsockname(m_fd, (struct sockaddr*)&addr, &addrLen) == 0) {
        m_boundPort = ntohs(addr.sin_port);
    } else {
        m_boundPort = port;
    }

    // Set non-blocking
    int flags = fcntl(m_fd, F_GETFL, 0);
    fcntl(m_fd, F_SETFL, flags | O_NONBLOCK);

    LOGI("UDP socket bound to port %d", m_boundPort);
    return true;
}

ssize_t SocketTransport::sendUdp(const void* data, size_t size, const std::string& host, int port) {
    if (m_fd < 0) return -1;
    struct sockaddr_in destAddr{};
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &destAddr.sin_addr);

    return sendto(m_fd, data, size, 0, (struct sockaddr*)&destAddr, sizeof(destAddr));
}

ssize_t SocketTransport::recvUdp(void* buffer, size_t size, int timeoutMs) {
    if (m_fd < 0) return -1;

    if (timeoutMs > 0) {
        struct pollfd pfd{};
        pfd.fd = m_fd;
        pfd.events = POLLIN;
        int pollRet = poll(&pfd, 1, timeoutMs);
        if (pollRet < 0) return -1;
        if (pollRet == 0) return 0; // timeout
    }

    ssize_t ret = recvfrom(m_fd, buffer, size, 0, nullptr, nullptr);
    if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return 0;
    }
    return ret;
}

} // namespace player
