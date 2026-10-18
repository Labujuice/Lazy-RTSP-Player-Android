#pragma once

#include <string>
#include <cstdint>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

namespace player {

class SocketTransport {
public:
    SocketTransport();
    ~SocketTransport();

    // Disable copy
    SocketTransport(const SocketTransport&) = delete;
    SocketTransport& operator=(const SocketTransport&) = delete;

    // TCP methods
    bool connectTcp(const std::string& host, int port, int timeoutMs = 3000);
    ssize_t sendTcp(const void* data, size_t size);
    ssize_t recvTcp(void* buffer, size_t size, int timeoutMs = 500);

    // UDP methods
    bool bindUdp(int port);
    ssize_t sendUdp(const void* data, size_t size, const std::string& host, int port);
    ssize_t recvUdp(void* buffer, size_t size, int timeoutMs = 500);

    void closeSocket();
    int getFd() const { return m_fd; }
    int getBoundPort() const { return m_boundPort; }

private:
    int m_fd = -1;
    int m_boundPort = 0;
    bool m_isUdp = false;
};

} // namespace player
