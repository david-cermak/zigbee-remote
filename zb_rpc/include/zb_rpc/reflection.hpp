#pragma once

#include "zb_rpc/log.hpp"
#include "zb_rpc/protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <meta>
#include <type_traits>

namespace zb_rpc {

template <typename T>
consteval bool wire_safe()
{
    constexpr auto type = ^^T;
    if constexpr (std::is_void_v<T> || std::is_same_v<T, EmptyReq>) {
        return true;
    } else if constexpr (std::is_same_v<T, uint8_t> || std::is_same_v<T, char> ||
                         std::is_same_v<T, le16> || std::is_same_v<T, le32>) {
        return true;
    } else if constexpr (std::meta::is_array_type(type)) {
        using Element = [:std::meta::remove_all_extents(type):];
        return wire_safe<Element>();
    } else if constexpr (std::meta::is_class_type(type)) {
        constexpr auto context = std::meta::access_context::current();
        template for (constexpr auto member : [:std::meta::reflect_constant_array(
                          std::meta::nonstatic_data_members_of(type, context)):]) {
            using Member = [:std::meta::type_of(member):];
            if constexpr (!wire_safe<Member>()) {
                return false;
            }
        }
        return true;
    }
    return false;
}

inline const char *api_name(api_id id)
{
    template for (constexpr auto enumerator : [:std::meta::reflect_constant_array(
                      std::meta::enumerators_of(^^api_id)):]) {
        if (id == [:enumerator:]) {
            return std::meta::identifier_of(enumerator).data();
        }
    }
    return "unknown";
}

struct DumpBuffer {
    char data[384]{};
    std::size_t size{};

    void append(const char *format, auto... values)
    {
        if (size >= sizeof(data) - 1) {
            return;
        }
        int written = std::snprintf(data + size, sizeof(data) - size, format, values...);
        if (written > 0) {
            size += static_cast<std::size_t>(written);
            if (size >= sizeof(data)) {
                size = sizeof(data) - 1;
            }
        }
    }
};

template <typename T>
void dump_value(DumpBuffer &buffer, const T &value);

template <typename T, std::size_t Size>
void dump_array(DumpBuffer &buffer, const T (&values)[Size])
{
    if constexpr (std::is_same_v<T, char>) {
        buffer.append("\"");
        for (std::size_t i = 0; i < Size && values[i] != '\0'; ++i) {
            buffer.append("%c", values[i]);
        }
        buffer.append("\"");
    } else {
        buffer.append("[");
        for (std::size_t i = 0; i < Size; ++i) {
            if (i != 0) {
                buffer.append(", ");
            }
            dump_value(buffer, values[i]);
        }
        buffer.append("]");
    }
}

template <typename T>
void dump_value(DumpBuffer &buffer, const T &value)
{
    constexpr auto type = ^^T;
    if constexpr (std::is_same_v<T, EmptyReq>) {
        buffer.append("{}");
    } else if constexpr (std::is_same_v<T, le32>) {
        buffer.append("0x%08lx", static_cast<unsigned long>(from_le32(value)));
    } else if constexpr (std::is_same_v<T, le16>) {
        buffer.append("0x%04x", static_cast<unsigned>(from_le16(value)));
    } else if constexpr (std::is_same_v<T, uint8_t>) {
        buffer.append("%u", static_cast<unsigned>(value));
    } else if constexpr (std::is_same_v<T, char>) {
        buffer.append("'%c'", value);
    } else if constexpr (std::meta::is_array_type(type)) {
        dump_array(buffer, value);
    } else if constexpr (std::meta::is_class_type(type)) {
        constexpr auto context = std::meta::access_context::current();
        buffer.append("%s { ", std::meta::identifier_of(type).data());
        bool first = true;
        template for (constexpr auto member : [:std::meta::reflect_constant_array(
                          std::meta::nonstatic_data_members_of(type, context)):]) {
            if (!first) {
                buffer.append(", ");
            }
            first = false;
            buffer.append("%s=", std::meta::identifier_of(member).data());
            dump_value(buffer, value.[:member:]);
        }
        buffer.append(" }");
    }
}

template <typename T>
void dump(const char *tag, const T &value)
{
    static_assert(wire_safe<T>());
    DumpBuffer buffer;
    dump_value(buffer, value);
    ZB_RPC_LOG(tag, "%s", buffer.data);
}

} // namespace zb_rpc
