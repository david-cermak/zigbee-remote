#pragma once

#include "zb_rpc/log.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>

namespace zb_rpc {

inline int rpc_listen(const char *host, uint16_t port)
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        ZB_RPC_LOG("sock", "socket: %s", std::strerror(errno));
        return -1;
    }
    int yes = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        ZB_RPC_LOG("sock", "inet_pton(%s) failed", host);
        ::close(fd);
        return -1;
    }
    if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        ZB_RPC_LOG("sock", "bind: %s", std::strerror(errno));
        ::close(fd);
        return -1;
    }
    if (::listen(fd, 1) < 0) {
        ZB_RPC_LOG("sock", "listen: %s", std::strerror(errno));
        ::close(fd);
        return -1;
    }
    ZB_RPC_LOG("sock", "listening on %s:%u", host, port);
    return fd;
}

inline int rpc_accept(int listen_fd)
{
    int fd = ::accept(listen_fd, nullptr, nullptr);
    if (fd < 0) {
        ZB_RPC_LOG("sock", "accept: %s", std::strerror(errno));
    }
    return fd;
}

inline int rpc_connect(const char *host, uint16_t port)
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        ZB_RPC_LOG("sock", "socket: %s", std::strerror(errno));
        return -1;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        ZB_RPC_LOG("sock", "inet_pton(%s) failed", host);
        ::close(fd);
        return -1;
    }
    if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        ZB_RPC_LOG("sock", "connect: %s", std::strerror(errno));
        ::close(fd);
        return -1;
    }
    return fd;
}

} // namespace zb_rpc
