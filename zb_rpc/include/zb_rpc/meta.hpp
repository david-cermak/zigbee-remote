#pragma once

#include "zb_rpc/log.hpp"
#include "zb_rpc/types.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <meta>
#include <string_view>
#include <type_traits>

namespace zb_rpc {

template <typename T>
consteval bool rpc_memcpy_safe()
{
    constexpr auto t = ^^T;
    if constexpr (std::meta::is_pointer_type(t) || std::meta::is_member_pointer_type(t)) {
        return false;
    } else if constexpr (std::meta::is_scalar_type(t) || std::meta::is_enum_type(t)) {
        return true;
    } else if constexpr (std::meta::is_array_type(t)) {
        using E = [: std::meta::remove_all_extents(t) :];
        return rpc_memcpy_safe<E>();
    } else if constexpr (std::meta::is_class_type(t) || std::meta::is_union_type(t)) {
        if constexpr (std::meta::is_union_type(t)) {
            return false;
        }
        constexpr auto ctx = std::meta::access_context::current();
        template for (constexpr auto m : [: std::meta::reflect_constant_array(
                         std::meta::nonstatic_data_members_of(t, ctx)) :]) {
            using MT = [: std::meta::type_of(m) :];
            if constexpr (!rpc_memcpy_safe<MT>()) {
                return false;
            }
        }
        return true;
    }
    return false;
}

inline const char *rpc_api_name(api_id id)
{
    template for (constexpr auto e : [: std::meta::reflect_constant_array(
                     std::meta::enumerators_of(^^api_id)) :]) {
        if (id == [:e:]) {
            return std::meta::identifier_of(e).data();
        }
    }
    return "?";
}

struct DumpBuf {
    char data[768]{};
    std::size_t n{0};

    void append(const char *fmt, auto... args)
    {
        if (n >= sizeof(data) - 1) {
            return;
        }
        int w = std::snprintf(data + n, sizeof(data) - n, fmt, args...);
        if (w > 0) {
            n += static_cast<std::size_t>(w);
            if (n >= sizeof(data)) {
                n = sizeof(data) - 1;
            }
        }
    }
};

template <typename T>
void dump_value(DumpBuf &b, const T &obj);

template <typename T, std::size_t N>
void dump_array(DumpBuf &b, const T (&arr)[N])
{
    if constexpr (std::is_same_v<T, char>) {
        b.append("\"");
        for (std::size_t i = 0; i < N && arr[i] != '\0'; ++i) {
            b.append("%c", arr[i]);
        }
        b.append("\"");
    } else if constexpr (std::is_same_v<T, uint8_t> && N == 6) {
        for (std::size_t i = 0; i < N; ++i) {
            b.append(i ? ":%02x" : "%02x", static_cast<unsigned>(arr[i]));
        }
    } else {
        b.append("[");
        for (std::size_t i = 0; i < N; ++i) {
            if (i) {
                b.append(", ");
            }
            dump_value(b, arr[i]);
        }
        b.append("]");
    }
}

template <typename T>
void dump_value(DumpBuf &b, const T &obj)
{
    constexpr auto t = ^^T;
    if constexpr (std::meta::is_array_type(t)) {
        dump_array(b, obj);
    } else if constexpr (std::meta::is_enum_type(t)) {
        b.append("%d", static_cast<int>(obj));
    } else if constexpr (std::is_same_v<T, uint8_t>) {
        b.append("0x%02x", static_cast<unsigned>(obj));
    } else if constexpr (std::is_same_v<T, int8_t>) {
        b.append("%d", static_cast<int>(obj));
    } else if constexpr (std::is_same_v<T, uint16_t>) {
        b.append("0x%04x", static_cast<unsigned>(obj));
    } else if constexpr (std::is_same_v<T, int16_t>) {
        b.append("%d", static_cast<int>(obj));
    } else if constexpr (std::is_same_v<T, uint32_t>) {
        b.append("%u", static_cast<unsigned>(obj));
    } else if constexpr (std::is_same_v<T, int32_t>) {
        b.append("%d", static_cast<int>(obj));
    } else if constexpr (std::meta::is_class_type(t)) {
        constexpr auto ctx = std::meta::access_context::current();
        b.append("%s { ", std::meta::identifier_of(t).data());
        bool first = true;
        template for (constexpr auto m : [: std::meta::reflect_constant_array(
                         std::meta::nonstatic_data_members_of(t, ctx)) :]) {
            if (!first) {
                b.append(", ");
            }
            first = false;
            b.append("%s=", std::meta::identifier_of(m).data());
            dump_value(b, obj.[:m:]);
        }
        b.append(" }");
    } else {
        b.append("?");
    }
}

template <typename T>
void rpc_dump(const char *tag, const T &obj)
{
    DumpBuf b;
    dump_value(b, obj);
    ZB_RPC_LOG(tag, "%s", b.data);
}

} // namespace zb_rpc
