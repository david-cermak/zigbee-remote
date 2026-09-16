#pragma once

#include "zb_rpc/protocol.hpp"
#include "zb_rpc/reflection.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <unistd.h>

namespace zb_rpc {

inline bool write_all(int fd, const void *data, std::size_t size)
{
    const auto *bytes = static_cast<const uint8_t *>(data);
    while (size > 0) {
        ssize_t written = ::write(fd, bytes, size);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            ZB_RPC_LOG("rpc", "write failed: errno %d", errno);
            return false;
        }
        bytes += written;
        size -= static_cast<std::size_t>(written);
    }
    return true;
}

inline bool read_all(int fd, void *data, std::size_t size)
{
    auto *bytes = static_cast<uint8_t *>(data);
    while (size > 0) {
        ssize_t received = ::read(fd, bytes, size);
        if (received < 0 && errno == EINTR) {
            continue;
        }
        if (received <= 0) {
            if (received < 0) {
                ZB_RPC_LOG("rpc", "read failed: errno %d", errno);
            }
            return false;
        }
        bytes += received;
        size -= static_cast<std::size_t>(received);
    }
    return true;
}

class Engine {
public:
    explicit Engine(int fd) : fd_(fd) {}

    bool receive_header(FrameHeader &header)
    {
        last_error_ = error_code::none;
        WireHeader wire{};
        if (!read_all(fd_, &wire, sizeof(wire))) {
            return false;
        }
        if (from_le32(wire.magic) != k_magic) {
            ZB_RPC_LOG("rpc", "bad frame magic");
            last_error_ = error_code::bad_magic;
            return false;
        }

        header.id = static_cast<api_id>(from_le32(wire.id));
        header.size = from_le32(wire.size);
        ZB_RPC_LOG("rpc", "recv %s size=%lu", api_name(header.id),
                   static_cast<unsigned long>(header.size));
        if (header.size > k_max_payload) {
            ZB_RPC_LOG("rpc", "payload exceeds %lu bytes",
                       static_cast<unsigned long>(k_max_payload));
            last_error_ = error_code::oversized_payload;
            return false;
        }
        return true;
    }

    template <typename T>
    bool receive_payload(const FrameHeader &header, T &payload)
    {
        static_assert(wire_safe<T>());
        if constexpr (std::is_same_v<T, EmptyReq>) {
            if (header.size != 0) {
                last_error_ = error_code::payload_mismatch;
                return false;
            }
            payload = {};
            return true;
        } else {
            static_assert(sizeof(T) <= k_max_payload);
            if (header.size != sizeof(T)) {
                ZB_RPC_LOG("rpc", "%s payload size %lu, expected %lu", api_name(header.id),
                           static_cast<unsigned long>(header.size),
                           static_cast<unsigned long>(sizeof(T)));
                last_error_ = error_code::payload_mismatch;
                return false;
            }
            if (!read_all(fd_, &payload, sizeof(payload))) {
                return false;
            }
            dump("rpc", payload);
            return true;
        }
    }

    bool send(api_id id)
    {
        WireHeader header{
            .magic = to_le32(k_magic),
            .id = to_le32(static_cast<uint32_t>(id)),
            .size = to_le32(0),
        };
        ZB_RPC_LOG("rpc", "send %s size=0", api_name(id));
        return write_all(fd_, &header, sizeof(header));
    }

    template <typename T>
    bool send(api_id id, const T &payload)
    {
        static_assert(wire_safe<T>());
        if constexpr (std::is_same_v<T, EmptyReq>) {
            return send(id);
        } else {
            static_assert(sizeof(T) <= k_max_payload);
            WireHeader header{
                .magic = to_le32(k_magic),
                .id = to_le32(static_cast<uint32_t>(id)),
                .size = to_le32(sizeof(T)),
            };
            ZB_RPC_LOG("rpc", "send %s size=%lu", api_name(id),
                       static_cast<unsigned long>(sizeof(T)));
            dump("rpc", payload);
            return write_all(fd_, &header, sizeof(header)) &&
                   write_all(fd_, &payload, sizeof(payload));
        }
    }

    bool send_error(error_code code)
    {
        return send(api_id::error, ErrorResp{.code = to_le32(static_cast<uint32_t>(code))});
    }

    bool discard_payload(uint32_t size)
    {
        uint8_t buffer[32];
        while (size > 0) {
            std::size_t chunk = size < sizeof(buffer) ? size : sizeof(buffer);
            if (!read_all(fd_, buffer, chunk)) {
                return false;
            }
            size -= static_cast<uint32_t>(chunk);
        }
        return true;
    }

    error_code last_error() const { return last_error_; }

private:
    int fd_;
    error_code last_error_{error_code::none};
};

template <typename Desc>
bool call(Engine &engine, typename Desc::resp_type &response)
    requires std::is_same_v<typename Desc::req_type, EmptyReq>
{
    if (!engine.send(Desc::id)) {
        return false;
    }
    FrameHeader header{};
    if (!engine.receive_header(header)) {
        return false;
    }
    if (header.id == api_id::error) {
        ErrorResp error{};
        if (engine.receive_payload(header, error)) {
            ZB_RPC_LOG("rpc", "server error code=%lu",
                       static_cast<unsigned long>(from_le32(error.code)));
        }
        return false;
    }
    if (header.id != Desc::id) {
        ZB_RPC_LOG("rpc", "unexpected response %s", api_name(header.id));
        engine.discard_payload(header.size);
        return false;
    }
    return engine.receive_payload(header, response);
}

template <typename Desc>
bool call(Engine &engine, const typename Desc::req_type &request,
          typename Desc::resp_type &response)
    requires(!std::is_same_v<typename Desc::req_type, EmptyReq>)
{
    if (!engine.send(Desc::id, request)) {
        return false;
    }
    FrameHeader header{};
    if (!engine.receive_header(header)) {
        return false;
    }
    if (header.id == api_id::error) {
        ErrorResp error{};
        if (engine.receive_payload(header, error)) {
            ZB_RPC_LOG("rpc", "server error code=%lu",
                       static_cast<unsigned long>(from_le32(error.code)));
        }
        return false;
    }
    if (header.id != Desc::id) {
        ZB_RPC_LOG("rpc", "unexpected response %s", api_name(header.id));
        engine.discard_payload(header.size);
        return false;
    }
    return engine.receive_payload(header, response);
}

enum class invoke_result {
    no_match,
    handled,
    failed,
};

template <typename Desc, auto Function>
invoke_result invoke_one(Engine &engine, const FrameHeader &header)
{
    if (header.id != Desc::id) {
        return invoke_result::no_match;
    }

    using Request = typename Desc::req_type;
    using Response = typename Desc::resp_type;
    Request request{};
    if (!engine.receive_payload(header, request)) {
        if (engine.last_error() != error_code::none) {
            engine.send_error(engine.last_error());
        }
        return invoke_result::failed;
    }

    Response response{};
    if constexpr (std::is_same_v<Request, EmptyReq>) {
        response = [:Function:]();
    } else {
        response = [:Function:](request);
    }
    return engine.send(Desc::id, response) ? invoke_result::handled
                                           : invoke_result::failed;
}

} // namespace zb_rpc
