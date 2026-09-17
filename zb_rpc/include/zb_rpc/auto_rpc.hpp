#pragma once

#include "zb_rpc/engine.hpp"

#include <cstddef>
#include <meta>
#include <string>
#include <string_view>
#include <type_traits>

namespace zb_rpc {

consteval api_id id_for_name(std::string_view name)
{
    template for (constexpr auto enumerator : [:std::meta::reflect_constant_array(
                      std::meta::enumerators_of(^^api_id)):]) {
        if (std::meta::identifier_of(enumerator) == name) {
            return [:enumerator:];
        }
    }
    throw "handler name has no matching api_id enumerator";
}

consteval bool is_rpc_handler(std::meta::info member)
{
    return std::meta::is_function(member) && std::meta::has_identifier(member) &&
           !std::meta::is_special_member_function(member) &&
           !std::meta::is_constructor(member) && !std::meta::is_destructor(member) &&
           std::meta::parameters_of(member).size() == 1;
}

template <std::meta::info Function>
struct fn_desc {
    static constexpr api_id id = id_for_name(std::meta::identifier_of(Function));
    using req_type = [:std::meta::type_of(std::meta::parameters_of(Function)[0]):];
    using resp_type = [:std::meta::return_type_of(Function):];

    static_assert(wire_safe<req_type>());
    static_assert(wire_safe<resp_type>());
    static_assert(std::is_same_v<req_type, EmptyReq> || sizeof(req_type) <= k_max_payload);
    static_assert(sizeof(resp_type) <= k_max_payload);
};

template <std::meta::info Function>
bool rpc_call_fn(Engine &engine, typename fn_desc<Function>::resp_type &response)
    requires std::is_same_v<typename fn_desc<Function>::req_type, EmptyReq>
{
    return call<fn_desc<Function>>(engine, response);
}

template <std::meta::info Function>
bool rpc_call_fn(Engine &engine, const typename fn_desc<Function>::req_type &request,
                 typename fn_desc<Function>::resp_type &response)
    requires(!std::is_same_v<typename fn_desc<Function>::req_type, EmptyReq>)
{
    return call<fn_desc<Function>>(engine, request, response);
}

template <std::meta::info Namespace>
invoke_result dispatch_ns(Engine &engine, const FrameHeader &header)
{
    constexpr auto context = std::meta::access_context::current();
    template for (constexpr auto member : [:std::meta::reflect_constant_array(
                      std::meta::members_of(Namespace, context)):]) {
        if constexpr (is_rpc_handler(member)) {
            constexpr api_id id = id_for_name(std::meta::identifier_of(member));
            if (header.id == id) {
                using Request = [:std::meta::type_of(std::meta::parameters_of(member)[0]):];
                using Response = [:std::meta::return_type_of(member):];
                static_assert(wire_safe<Request>());
                static_assert(wire_safe<Response>());

                Request request{};
                if (!engine.receive_payload(header, request)) {
                    if (engine.last_error() != error_code::none) {
                        engine.send_error(engine.last_error());
                    }
                    return invoke_result::failed;
                }
                Response response = [:member:](request);
                return engine.send(id, response) ? invoke_result::handled
                                                 : invoke_result::failed;
            }
        }
    }
    return invoke_result::no_match;
}

namespace detail {

consteval void append_u32(std::string &out, unsigned value)
{
    char tmp[16];
    int i = 0;
    if (value == 0) {
        tmp[i++] = '0';
    } else {
        while (value > 0) {
            tmp[i++] = static_cast<char>('0' + (value % 10U));
            value /= 10U;
        }
    }
    while (i > 0) {
        out += tmp[--i];
    }
}

consteval void append_catalog_line(std::string &out, std::meta::info member)
{
    const api_id id = id_for_name(std::meta::identifier_of(member));
    out += "  ";
    out += std::meta::identifier_of(member);
    out += " id=";
    append_u32(out, static_cast<unsigned>(id));
    out += " req=";
    out += std::meta::display_string_of(std::meta::type_of(std::meta::parameters_of(member)[0]));
    out += " resp=";
    out += std::meta::display_string_of(std::meta::return_type_of(member));
    out += '\n';
}

} // namespace detail

template <std::meta::info Namespace>
consteval std::string catalog_dump()
{
    std::string out = "rpc catalog:\n";
    constexpr auto context = std::meta::access_context::current();
    std::size_t count = 0;
    template for (constexpr auto member : [:std::meta::reflect_constant_array(
                      std::meta::members_of(Namespace, context)):]) {
        if constexpr (is_rpc_handler(member)) {
            using Request = [:std::meta::type_of(std::meta::parameters_of(member)[0]):];
            using Response = [:std::meta::return_type_of(member):];
            static_assert(wire_safe<Request>());
            static_assert(wire_safe<Response>());
            (void)id_for_name(std::meta::identifier_of(member));
            detail::append_catalog_line(out, member);
            ++count;
        }
    }
    if (count == 0) {
        throw "rpc namespace has no handlers";
    }
    return out;
}

template <std::meta::info Namespace>
consteval bool verify_catalog()
{
    (void)catalog_dump<Namespace>();
    return true;
}

template <std::meta::info Namespace>
inline constexpr auto k_catalog_text = std::define_static_string(catalog_dump<Namespace>());

} // namespace zb_rpc
