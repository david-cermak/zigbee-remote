#pragma once

#include "zb_rpc/engine.hpp"

namespace zb_rpc {

// L1: explicit (Desc, ^^fn) entry for fold-based dispatch.
template <typename Desc, auto Function>
struct handler {
    using desc = Desc;
    static constexpr auto function = Function;
};

template <typename... Handlers>
invoke_result dispatch(Engine &engine, const FrameHeader &header)
{
    invoke_result result = invoke_result::no_match;
    (
        [&]<typename H>() {
            if (result == invoke_result::no_match) {
                result = invoke_one<typename H::desc, H::function>(engine, header);
            }
        }.template operator()<Handlers>(),
        ...);
    return result;
}

} // namespace zb_rpc
