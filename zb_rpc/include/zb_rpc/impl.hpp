#pragma once

#include "zb_rpc/log.hpp"
#include "zb_rpc/meta.hpp"
#include "zb_rpc/types.hpp"

#include <cerrno>
#include <cstring>
#include <unistd.h>

namespace zb_rpc {

inline bool write_all(int fd, const void *buf, std::size_t n)
{
    auto p = static_cast<const uint8_t *>(buf);
    while (n) {
        ssize_t w = ::write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            ZB_RPC_LOG("rpc", "write: %s", std::strerror(errno));
            return false;
        }
        if (w == 0) {
            ZB_RPC_LOG("rpc", "write: peer closed");
            return false;
        }
        p += w;
        n -= static_cast<std::size_t>(w);
    }
    return true;
}

inline bool read_all(int fd, void *buf, std::size_t n)
{
    auto p = static_cast<uint8_t *>(buf);
    while (n) {
        ssize_t r = ::read(fd, p, n);
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            ZB_RPC_LOG("rpc", "read: %s", std::strerror(errno));
            return false;
        }
        if (r == 0) {
            ZB_RPC_LOG("rpc", "read: peer closed");
            return false;
        }
        p += r;
        n -= static_cast<std::size_t>(r);
    }
    return true;
}

class RpcEngine {
public:
    void attach(int fd) { fd_ = fd; }
    int fd() const { return fd_; }

    template <typename T>
    bool send(api_id id, const T *t)
    {
        static_assert(rpc_memcpy_safe<T>());
        RpcHeader head{id, sizeof(T)};
        ZB_RPC_LOG("rpc", "send %s size=%u", rpc_api_name(id), head.size);
        rpc_dump("rpc", *t);
        if (!write_all(fd_, &head, sizeof(head))) {
            return false;
        }
        return write_all(fd_, t, sizeof(T));
    }

    bool send(api_id id)
    {
        RpcHeader head{id, 0};
        ZB_RPC_LOG("rpc", "send %s size=0", rpc_api_name(id));
        return write_all(fd_, &head, sizeof(head));
    }

    RpcHeader get_header()
    {
        RpcHeader head{};
        if (!read_all(fd_, &head, sizeof(head))) {
            return RpcHeader{api_id::ERROR, 0};
        }
        ZB_RPC_LOG("rpc", "recv header %s size=%u", rpc_api_name(head.id), head.size);
        return head;
    }

    template <typename T>
    T get_payload(api_id id, const RpcHeader &head)
    {
        static_assert(rpc_memcpy_safe<T>());
        T value{};
        if (head.id != id || head.size != sizeof(T)) {
            ZB_RPC_LOG("rpc", "unexpected header %s vs %s or size %u vs %zu", rpc_api_name(head.id),
                       rpc_api_name(id), head.size, sizeof(T));
            return {};
        }
        if (!read_all(fd_, &value, sizeof(T))) {
            return {};
        }
        rpc_dump("rpc", value);
        return value;
    }

private:
    int fd_{-1};
};

} // namespace zb_rpc
