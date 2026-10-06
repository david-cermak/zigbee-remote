# Automating RPC further with C++26 reflection

**Status:** design notes only — no implementation yet.  
**Proven on:** GCC 16.2.0 (`-std=c++26 -freflection`), local toolchain via `~/local/export.sh`.  
**Context:** compare `zb_rpc` (host simulator) with `zb_rpc_shared` (endian-safe production headers).

---

## 1. What is still hand-written today

| Artifact | Simulator (`zb_rpc`) | Shared / ESP (`zb_rpc_shared` + `rpc_server.cpp`) |
| --- | --- | --- |
| Wire / host types | Host `uint16_t` / `int32_t` in payloads | Explicit `le16` / `le32` + wrappers |
| Catalog | `catalog.hpp` — one `rpc_desc<Id,Req,Resp>` alias per API | Same idea in `protocol.hpp` `desc::*` |
| Server dispatch | `\|\|`-chain of `invoke_one<Desc, ^^fn>` | Same chain, plus endian adapters in each `handle_*` |
| Client calls | `rpc_call<desc::…>` | `call<desc::…>` |
| Names / dump | `rpc_api_name`, `rpc_dump` | `api_name`, `wire_safe`, recursive `dump` |

Reflection already covers **policy** (`wire_safe`) and **observation** (`api_name`, `dump`).  
The remaining grind is the **catalog ↔ handler ↔ dispatch** triangle: three lists that must stay in lockstep.

The slide deck already flagged this:

> Reflection does **not** auto-generate this loop (yet) — explicit is reviewable.

This note asks: *how far can we go without becoming a mini-IDL?*

---

## 2. The attractive idea — collect functions, then reflect

```cpp
// zb_api_handlers.inc  (declarations only — wire types)
U16Resp get_panid();
I32Resp open_network(OpenNetworkReq req);
zb_addr_t find_sensor(FindSensorReq req);
// …

struct RpcHandlers {
#include "zb_api_handlers.inc"
};

// later: implementations
U16Resp RpcHandlers::get_panid() { /* … */ }
```

Or the same include into a **namespace** / free-function facade. The point is one declaration list that reflection can inventory via `members_of`.

### Why a class (or namespace) instead of a `.h` of free C functions?

`zigbee_api.h` is a **native** Zigbee surface:

- host-endian integers
- extra args (`timeout_ms`) that must **not** appear on the wire
- C linkage / IDF types (`zigbee_addr_t` vs `zb_rpc::zb_addr_t`)

You cannot point reflection at that header and magically get a correct codec.  
You *can* point it at a thin **RPC façade** whose signatures *are* the wire contract.

```
zigbee_api.h  ──(manual adapters)──►  RpcHandlers  ──(reflection)──►  catalog + dispatch
     native                           wire-shaped                     generated glue
```

That split is the load-bearing design choice of `zb_rpc_shared` today; automation should **preserve** it, not erase it.

---

## 3. What GCC 16.2 already lets us prove

Verified locally (not Godbolt):

| Facility | Works? | Role for auto-RPC |
| --- | --- | --- |
| `members_of(^^T, ctx)` | yes | inventory façade methods |
| `is_function` / `has_identifier` / skip specials | yes | filter ctors / assign |
| `identifier_of` | yes | match `api_id` enumerator by name |
| `return_type_of(fn)` | yes | `Resp` of `rpc_desc` |
| `parameters_of(fn)` | yes | arity 0 → `EmptyReq`; arity 1 → `Req` |
| `[:fn:](args…)` splice | yes | call handler without vtable |
| `enumerators_of(^^api_id)` | yes (already in tree) | stable numeric IDs by name |
| `annotations_of` | present in `<meta>` | optional `[[=rpc::id{n}]]` later |
| `define_aggregate` | yes, but **data members only** | cannot inject handler functions |

### Snippet A — name → `api_id` (no string table)

```cpp
enum class api_id : uint32_t {
    get_panid = 2,
    open_network = 4,
    find_sensor = 6,
};

consteval api_id id_for_name(std::string_view name) {
    template for (constexpr auto e : [:std::meta::reflect_constant_array(
                      std::meta::enumerators_of(^^api_id)):]) {
        if (std::meta::identifier_of(e) == name)
            return [:e:];
    }
    throw "handler name has no matching api_id enumerator";
}

static_assert(id_for_name("find_sensor") == api_id::find_sensor);
```

**Contract:** façade method name **==** enumerator name.  
Add an API → add one enum value + one method. No third string list.

### Snippet B — derive `Req` / `Resp` from the function

```cpp
struct Handlers {
    static U16Resp get_panid();
    static I32Resp open_network(OpenNetworkReq req);
};

static_assert(std::meta::parameters_of(^^Handlers::get_panid).size() == 0);
static_assert(std::meta::parameters_of(^^Handlers::open_network).size() == 1);

using Ret = [: std::meta::return_type_of(^^Handlers::get_panid) :];
using Req = [: std::meta::type_of(
                 std::meta::parameters_of(^^Handlers::open_network)[0]) :];
static_assert(std::is_same_v<Ret, U16Resp>);
static_assert(std::is_same_v<Req, OpenNetworkReq>);
```

Rules that keep the codec boring:

- **0 params** → request is `EmptyReq` (on-wire size 0)
- **exactly 1 param** → that type is the request payload
- **≥2 params** → compile error (bundle into a struct — same as today)
- `static_assert(wire_safe<Req>() && wire_safe<Resp>())`
- `static_assert(sizeof(Req) ≤ k_max_payload)` (with `EmptyReq` special-cased)

At that point **`rpc_desc` aliases become optional** — the function *is* the catalog entry.

### Snippet C — auto-dispatch (replaces the `\|\|` / `if` chain)

```cpp
template <typename HandlersT>
invoke_result dispatch(Engine &engine, const FrameHeader &header) {
    constexpr auto ctx = std::meta::access_context::current();
    template for (constexpr auto m : [:std::meta::reflect_constant_array(
                      std::meta::members_of(^^HandlersT, ctx)):]) {
        if constexpr (std::meta::is_function(m) && std::meta::has_identifier(m)
                      && !std::meta::is_special_member_function(m)) {
            if (header.id != id_for_name(std::meta::identifier_of(m)))
                continue; // or nested if — expansion-statement style

            constexpr auto n = std::meta::parameters_of(m).size();
            if constexpr (n == 0) {
                // receive EmptyReq / size 0, then:
                auto response = [:m:]();
                return engine.send(header.id, response) ? /*handled*/ : /*failed*/;
            } else if constexpr (n == 1) {
                using Req = [: std::meta::type_of(std::meta::parameters_of(m)[0]) :];
                Req request{};
                if (!engine.receive_payload(header, request)) /* … */;
                auto response = [:m:](request);
                return engine.send(header.id, response) ? /*handled*/ : /*failed*/;
            } else {
                static_assert(n <= 1, "RPC handler must take 0 or 1 argument");
            }
        }
    }
    return invoke_result::no_match;
}
```

A reduced version of this **compiled and ran** on GCC 16.2 (print-only stub, no sockets):

```
handled get_panid (0-arg)
handled open_network (1-arg sizeof Req=1)
```

That is the entire `handle_commands` / `dispatch` list in `server.cpp` / `rpc_server.cpp`, collapsed to one template.

### Snippet D — client stubs from the same façade type

Symmetric idea: generate typed callers without repeating `desc::*`.

```cpp
// Sketch — not compiled as a full Engine integration yet
template <auto Fn>
auto rpc_call(Engine &engine /*, optional Req */) {
    constexpr api_id id = id_for_name(std::meta::identifier_of(Fn));
    using Resp = [: std::meta::return_type_of(Fn) :];
    constexpr auto n = std::meta::parameters_of(Fn).size();
    Resp response{};
    if constexpr (n == 0) {
        engine.send(id);
    } else {
        using Req = [: std::meta::type_of(std::meta::parameters_of(Fn)[0]) :];
        // engine.send(id, req);
    }
    // receive_header + receive_payload into response
    return response;
}

// usage:
//   auto panid = rpc_call<^^Handlers::get_panid>(engine);
//   auto addr  = rpc_call<^^Handlers::find_sensor>(engine, FindSensorReq{…});
```

Server and client then share **one** declaration surface (`Handlers` / include file). Implementations differ (sim vs ESP adapters).

---

## 4. Design space — four layers of automation

```
┌─────────────────────────────────────────────────────────────┐
│ L0  Today                                                   │
│     hand catalog + hand dispatch + hand call sites          │
│     reflection: wire_safe / api_name / dump only            │
├─────────────────────────────────────────────────────────────┤
│ L1  Auto-dispatch                                           │
│     keep rpc_desc; generate the invoke_one chain            │
│     from a constexpr list of (Desc, ^^fn) pairs             │
├─────────────────────────────────────────────────────────────┤
│ L2  Signature-derived catalog  ← include-into-class idea    │
│     façade methods + api_id name match                      │
│     Req/Resp/arity from parameters_of / return_type_of      │
│     wire_safe asserted on both                              │
├─────────────────────────────────────────────────────────────┤
│ L3  Annotations / codegen extras (optional)                 │
│     [[=rpc::id{n}]] if names must diverge                   │
│     define_aggregate for *response DTO* toys                │
│     still not: memberwise wire codec                        │
└─────────────────────────────────────────────────────────────┘
```

**Recommended next step if we implement anything:** L1 → L2.  
L1 is a mechanical win with almost no ABI risk. L2 removes `desc::*` duplication. L3 only if naming friction appears.

---

## 5. The include-into-class pattern — strengths and traps

### Strengths

- Single declaration list; reflection is the inventory.
- Works with existing `invoke_one` / `Engine` framing (POD memcpy of wire-safe types).
- Forces the **wire façade** to be explicit C++ — good for review.
- Same header can be included by host sim and ESP server (implementations differ).

### Traps

| Trap | Why it hurts | Mitigation |
| --- | --- | --- |
| Reflecting `zigbee_api.h` directly | timeouts, host endian, wrong structs | Never; wrap in `RpcHandlers` |
| Overloads / default args | ambiguous `parameters_of` / names | Ban overloads on the façade |
| Renaming a method without enum | `id_for_name` throws at compile time | Good — fail closed |
| Reordering methods | OK if IDs come from enum **values**, not declaration index | Prefer name match, not ordinal |
| Non-static methods needing `this` | splice needs an instance | Prefer `static` members or free functions in a namespace |
| `define_aggregate` to “build the API class” | only injects **data** members | Write the façade by hand (or include); reflect it |
| Multi-arg native APIs | e.g. `read_basic(addr, ep, timeout)` | Façade takes one wire struct; adapter fills timeout |

### Include file sketch

```cpp
// zb_rpc_api.inc — the only list you edit for “new RPC”
/* return-type  name(args) */
ZB_RPC(U16Resp,    get_short_addr,  /*Empty*/)
ZB_RPC(U16Resp,    get_panid,       /*Empty*/)
ZB_RPC(U8Resp,     get_channel,     /*Empty*/)
ZB_RPC(I32Resp,    open_network,    OpenNetworkReq)
ZB_RPC(U16Resp,    wait_annce,      /*Empty*/)
ZB_RPC(zb_addr_t,  find_sensor,     FindSensorReq)
ZB_RPC(zb_basic_t, read_basic,      zb_addr_t)
// …
```

Two expansions of the same list:

```cpp
// 1) Class body (reflection surface)
#define ZB_RPC(Ret, Name, Req) \
    static Ret Name(ZB_RPC_ARGS(Req));
struct RpcHandlers {
#include "zb_rpc_api.inc"
};
#undef ZB_RPC

// 2) Optional: still emit rpc_desc for gradual migration
#define ZB_RPC(Ret, Name, Req) \
    using Name = rpc_desc<api_id::Name, ZB_RPC_REQ(Req), Ret>;
namespace desc {
#include "zb_rpc_api.inc"
}
```

Macros here are only a **declaration expander**. Reflection does the interesting work. You could also skip macros and `#include` raw member declarations (the user’s original sketch).

---

## 6. What should stay non-automatic

Keep the slide-deck boundary:

| Do automate | Do **not** automate |
| --- | --- |
| Catalog pairing Id ↔ Req ↔ Resp ↔ Fn | Memberwise / TLV serialization |
| Dispatch loop | Inventing endian wrappers from `uint16_t` |
| `api_name` / dump (already done) | Hiding IDF pointer structs on the wire |
| Compile-time `wire_safe` gates | Generating Zigbee semantics / timeouts |

Endianness remains a **type** problem (`le16`/`le32`), not a reflection codec problem. Reflection polices and names those types; it does not replace them.

The ESP `handle_*` bodies that call `zigbee_*` and convert endianness are **adapters**. Even with L2, those bodies stay handwritten — only the glue around them shrinks.

```
auto-dispatch  →  RpcHandlers::read_temp(zb_addr_t)
                       │
                       ▼
              (manual) from_le16 / timeout / zigbee_read_temp / to_le16_s…
```

---

## 7. Mapping sim → shared under L2

| Piece | Simulator today | After L2 |
| --- | --- | --- |
| `sim::find_sensor(uint16_t)` | host types, matches old catalog | either change sim to wire types, or keep a second façade |
| `catalog.hpp` | explicit aliases | derived from `RpcHandlers` / deleted |
| `server.cpp` dispatch | 12× `invoke_one` | `dispatch<RpcHandlers>(…)` |
| `client.cpp` | `rpc_call<desc::…>` | `rpc_call<^^RpcHandlers::…>` or thin wrappers |
| ESP `rpc_server.cpp` | adapters + dispatch chain | adapters remain; dispatch chain goes away |

Cleanest long-term layout:

```
zb_rpc_shared/
  protocol.hpp      # le*, frames, api_id, wire structs
  reflection.hpp    # wire_safe, api_name, dump
  engine.hpp        # I/O + call + invoke_one (kept as primitives)
  auto_rpc.hpp      # NEW: id_for_name, dispatch<Handlers>, rpc_call<^^fn>
  api.inc           # NEW: façade declaration list

zb_rpc/             # sim implements RpcHandlers with host-friendly fakes
zb_rpc_server/      # ESP implements RpcHandlers by adapting zigbee_api.h
```

---

## 8. Open questions before implementing

Resolved for the `zb_rpc` simulator implementation:

1. **Stable IDs:** name-matched enumerators (`sim::find_sensor` ↔ `api_id::find_sensor`).
2. **Façade shape:** namespace members (`namespace sim`).
3. **Empty request:** explicit `EmptyReq` parameter on every 0-payload handler.
4. **Gradual migration:** keep `desc::*` as `fn_desc<^^sim::…>` aliases alongside `dispatch_ns`.
5. **Client ergonomics:** generated `api::*` inline wrappers in `catalog.hpp`.
6. **Freestanding / ESP:** deferred — simulator-only for now.
7. **Reviewability:** `static_assert(verify_catalog<^^::sim>())` plus constexpr `k_sim_catalog` dump (logged at server start).

---

## 9. Suggested proof milestones (when we *do* implement)

1. **Spike (Godbolt / local):** `dispatch_auto` over 2–3 handlers — already done in spirit above.
2. **L1 in-tree:** replace ESP + sim dispatch chains with a fold over an explicit `std::meta::info` array of `^^handle_*` + existing `desc::*` (lowest risk).
3. **L2:** introduce `RpcHandlers` + `api.inc`; delete hand-written `desc` aliases; keep adapters.
4. **Client:** switch one call site to signature-driven `rpc_call`; expand if ergonomics feel good.
5. **Docs / slides:** update the “Dispatch stays boring” slide to “Dispatch is generated; adapters stay boring.”

---

## 10. Bottom line

Yes — the `#include` into a class/namespace + `members_of` approach is real on GCC 16.2, and it directly attacks the remaining duplication (catalog + dispatch + client pairing).

What reflection **should** own next: inventory, arity, Req/Resp typing, name↔id, and the dispatch/call loops.

What reflection **should not** own: endian layout, Zigbee timeouts, or a general-purpose serializer. Those stay as intentional wire types and thin adapters — the same boundary that made `zb_rpc_shared` an improvement over the simulator.
