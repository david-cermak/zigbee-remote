---
marp: true
title: C++26 Reflection for a Tiny RPC
description: ~20 min demo of zb_rpc_shared — endian-safe framing, compile-time safety, and catalog dispatch
theme: default
paginate: true
size: 16:9
style: |
  section { font-size: 28px; }
  h1 { font-size: 44px; }
  h2 { font-size: 36px; }
  code { font-size: 20px; }
  pre { font-size: 18px; }
  footer { font-size: 14px; color: #666; }
  table { font-size: 22px; }
  .cols { display: grid; grid-template-columns: 1fr 1fr; gap: 1.5rem; }
  small, .note { font-size: 20px; color: #444; }
---

<!--
Timing guide (~20 min):
  0–2   Motivation + use case
  2–5   Architecture + wire types
  5–12  Reflection deep dive
 12–17  Engine / invoke / call
 17–20  Demo notes + takeaways
-->

# C++26 Reflection for a Tiny RPC

**Header-only wire protocol · compile-time safety · catalog dispatch**

`zb_rpc_shared/include/zb_rpc/`

<small>Audience: C++ engineers · ~20 minutes · Zigbee is the demo payload, not the point</small>

---

# Agenda

1. Why remoting a few APIs is still hard
2. Use case in one slide (Zigbee over PPP)
3. **Wire types** — explicit little-endian, no host `memcpy` of `int`
4. **Reflection** — `wire_safe`, dump, enum names
5. **Engine** — framing, `call<>`, `invoke_one`
6. What we deliberately *don’t* generate
7. Live-demo checklist & takeaways

---

# The problem

You want:

- the **same** headers on Linux and on a bare-metal MCU
- a **plain TCP** socket (`read` / `write` loops)
- typed request/response pairs
- logs that print **field names**, not hex dumps
- a compile error if someone puts a **pointer** on the wire

You do *not* want:

- protobuf / flatbuffers / IDL codegen for a dozen APIs
- `#ifdef __BYTE_ORDER__` sprinkled through handlers
- reflection as a *serializer* (memberwise encode)

---

# Use case (brief)

```
[temp sensor] --802.15.4-- [ESP32-H2 Zigbee stack]
                                    |
                               UART PPP
                                    |
                         [Linux zb_rpc_client]
```

- ESP exposes a **tiny Zigbee API** over TCP (`192.168.11.2:3333`)
- Laptop runs the **thermostat application** sequence via RPC
- Zigbee async quirks live on the chip; the **RPC layer is domain-agnostic**

<small>Today we look at the four shared headers — the Zigbee handlers are just callables.</small>

---

# Shared surface (four files)

| File | Job |
| --- | --- |
| `protocol.hpp` | `le16`/`le32`, frames, catalog `rpc_desc` |
| `reflection.hpp` | `wire_safe`, `api_name`, recursive `dump` |
| `engine.hpp` | POSIX I/O, `call<>`, `invoke_one` |
| `log.hpp` | one macro: host `fprintf` / ESP `ESP_LOGI` |

```cpp
#ifndef ZB_RPC_LOG
#define ZB_RPC_LOG(tag, fmt, ...) \
    std::fprintf(stderr, "%s: " fmt "\n", tag, ##__VA_ARGS__)
#endif
```

<small>On firmware: `#define ZB_RPC_LOG ... ESP_LOGI` before including `engine.hpp`.</small>

---

# Explicit endianness

Host (x86) and ESP32-H2 (RISC-V) are both little-endian *today*.
We still refuse native multi-byte integers on the wire.

```cpp
struct le16 { uint8_t bytes[2]; };
struct le32 { uint8_t bytes[4]; };

constexpr le32 to_le32(uint32_t value) {
    return {{ static_cast<uint8_t>(value),
              static_cast<uint8_t>(value >> 8),
              static_cast<uint8_t>(value >> 16),
              static_cast<uint8_t>(value >> 24) }};
}
```

**Allowed wire leaves:** `uint8_t`, `char`, `le16`, `le32`  
**Rejected:** `int`, `uint16_t`, pointers, unions

---

# Frame layout

```
┌──────────── WireHeader (12 B) ────────────┐┌── payload ──┐
│ magic "RPC2" │ api_id │ size (LE)         ││ 0…64 bytes  │
└───────────────────────────────────────────┘└─────────────┘
```

```cpp
struct WireHeader {
    le32 magic;   // 0x32435052
    le32 id;
    le32 size;
};

struct zb_addr_t {
    le32 err;
    le16 short_addr;
    uint8_t ep;
} __attribute__((packed));   // sizeof == 7
```

`static_assert` on every public wire size — layout is part of the ABI.

---

# Catalog without codegen

```cpp
template <api_id Id, typename Req, typename Resp>
struct rpc_desc {
    static constexpr api_id id = Id;
    using req_type = Req;
    using resp_type = Resp;
};

namespace desc {
using get_panid =
    rpc_desc<api_id::get_panid, EmptyReq, U16Resp>;
using find_sensor =
    rpc_desc<api_id::find_sensor, FindSensorReq, zb_addr_t>;
using read_temp =
    rpc_desc<api_id::read_temp, zb_addr_t, zb_temp_t>;
}
```

Adding an API = wire structs + one alias + one handler line.  
No `.proto`, no stub generator.

---

# Reflection #1 — `wire_safe<T>`

```cpp
template <typename T>
consteval bool wire_safe() {
    constexpr auto type = ^^T;
    if constexpr (/* uint8_t | char | le16 | le32 */) {
        return true;
    } else if constexpr (std::meta::is_array_type(type)) {
        using Element = [: std::meta::remove_all_extents(type) :];
        return wire_safe<Element>();
    } else if constexpr (std::meta::is_class_type(type)) {
        template for (constexpr auto member :
            [: std::meta::reflect_constant_array(
                   std::meta::nonstatic_data_members_of(
                       type, std::meta::access_context::current())) :]) {
            using Member = [: std::meta::type_of(member) :];
            if constexpr (!wire_safe<Member>()) return false;
        }
        return true;
    }
    return false;   // pointers, ints, unions, …
}
```

---

# Reflection #1 — what that buys you

```cpp
template <typename T>
bool Engine::send(api_id id, const T &payload) {
    static_assert(wire_safe<T>());
    static_assert(sizeof(T) <= k_max_payload);
    // …
}
```

| Attempt | Compile? |
| --- | --- |
| `struct { le32 x; }` | ✅ |
| `struct { uint16_t x; }` | ❌ `wire_safe` |
| `struct { char *p; }` | ❌ |
| payload > 64 B | ❌ `sizeof` assert |

**Safety is a type property**, checked at every send/recv — not a runtime convention.

---

# Reflection #2 — enum → string

```cpp
inline const char *api_name(api_id id) {
    template for (constexpr auto enumerator :
        [: std::meta::reflect_constant_array(
               std::meta::enumerators_of(^^api_id)) :]) {
        if (id == [:enumerator:])
            return std::meta::identifier_of(enumerator).data();
    }
    return "unknown";
}
```

Logs read:

```
rpc: recv find_sensor size=2
rpc: send zb_addr_t { err=0x00000000, short_addr=0x1956, ep=10 }
```

No hand-maintained `switch` / string table that drifts from `api_id`.

---

# Reflection #3 — recursive dump

```cpp
} else if constexpr (std::meta::is_class_type(type)) {
    buffer.append("%s { ", std::meta::identifier_of(type).data());
    template for (constexpr auto member : /* members_of */) {
        buffer.append("%s=", std::meta::identifier_of(member).data());
        dump_value(buffer, value.[:member:]);  // splice + recurse
    }
    buffer.append(" }");
}
```

```cpp
template <typename T>
void dump(const char *tag, const T &value) {
    static_assert(wire_safe<T>());
    DumpBuffer buffer;
    dump_value(buffer, value);
    ZB_RPC_LOG(tag, "%s", buffer.data);
}
```

Same reflection walk as `wire_safe` — dump only for types already proven wire-safe.

---

# Reflection vocabulary used

| Construct | Role here |
| --- | --- |
| `^^T` | reflect a type / function |
| `[: info :]` | splice reflection → C++ entity |
| `template for` | compile-time loop over members / enumerators |
| `nonstatic_data_members_of` | struct walk |
| `enumerators_of` | enum walk |
| `identifier_of` | printable names |
| `type_of` / `is_*_type` | classify |
| `reflect_constant_array` | materialize a reflection range |

<small>Requires GCC 16 + `-std=c++26 -freflection` (host and IDF RISC-V GCC). esp-clang does not support `-freflection` yet.</small>

---

# Engine — fragmentation-safe I/O

```cpp
inline bool read_all(int fd, void *data, std::size_t size) {
    auto *bytes = static_cast<uint8_t *>(data);
    while (size > 0) {
        ssize_t n = ::read(fd, bytes, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        bytes += n;
        size  -= static_cast<std::size_t>(n);
    }
    return true;
}
```

Same on Linux and ESP-IDF lwIP POSIX sockets.  
Header and payload are always read to completion — TCP is a stream.

---

# Engine — receive path

```cpp
bool receive_header(FrameHeader &header) {
    WireHeader wire{};
    if (!read_all(fd_, &wire, sizeof(wire))) return false;
    if (from_le32(wire.magic) != k_magic) {
        last_error_ = error_code::bad_magic;
        return false;
    }
    header.id   = static_cast<api_id>(from_le32(wire.id));
    header.size = from_le32(wire.size);
    // … reject size > k_max_payload
}

template <typename T>
bool receive_payload(const FrameHeader &h, T &payload) {
    static_assert(wire_safe<T>());
    if (h.size != sizeof(T)) { /* payload_mismatch */ }
    read_all(fd_, &payload, sizeof(payload));
    dump("rpc", payload);
}
```

`EmptyReq` is a special case: on-wire size `0` (not `sizeof(EmptyReq)==1`).

---

# Client — `call<Desc>`

```cpp
template <typename Desc>
bool call(Engine &engine,
          const typename Desc::req_type &request,
          typename Desc::resp_type &response)
    requires (!std::is_same_v<typename Desc::req_type, EmptyReq>)
{
    if (!engine.send(Desc::id, request)) return false;
    FrameHeader header{};
    if (!engine.receive_header(header)) return false;
    if (header.id != Desc::id) {
        engine.discard_payload(header.size);
        return false;
    }
    return engine.receive_payload(header, response);
}
```

Usage:

```cpp
zb_temp_t attrs{};
call<desc::read_temp>(engine, sensor, attrs);
```

Type of request/response comes from the catalog — no manual `api_id` / `sizeof` pairing.

---

# Server — `invoke_one` + splice

```cpp
template <typename Desc, auto Function>
invoke_result invoke_one(Engine &engine, const FrameHeader &header) {
    if (header.id != Desc::id) return invoke_result::no_match;

    using Request  = typename Desc::req_type;
    using Response = typename Desc::resp_type;
    Request request{};
    if (!engine.receive_payload(header, request)) { /* send_error */ }

    Response response{};
    if constexpr (std::is_same_v<Request, EmptyReq>)
        response = [:Function:]();
    else
        response = [:Function:](request);

    return engine.send(Desc::id, response)
               ? invoke_result::handled : invoke_result::failed;
}
```

`Function` is a **reflection** (`^^handle_read_temp`).  
Splicing calls the real function — no `std::function`, no vtable.

---

# Dispatch stays boring (on purpose)

```cpp
result = invoke_one<desc::get_panid,     ^^handle_get_panid>(e, h);
result = invoke_one<desc::find_sensor,   ^^handle_find_sensor>(e, h);
result = invoke_one<desc::read_temp,     ^^handle_read_temp>(e, h);
// …
```

- Each line is one catalog entry ↔ one native handler
- Unknown `api_id` → discard payload → `error` frame
- Reflection does **not** auto-generate this loop (yet) — explicit is reviewable

<small>Future: `define_aggregate` / parameter packs could shrink this further; we stopped where the demo stays readable.</small>

---

# Design boundaries

| Reflection **does** | Reflection **does not** |
| --- | --- |
| Reject unsafe wire types | Memberwise serialize |
| Print field / enum names | Invent a TLV codec |
| Bind `^^fn` into dispatch | Wrap raw IDF pointer structs |
| Keep host ↔ MCU headers identical | Hide endianness behind `ntohl` |

Wire format = **packed POD of endian wrappers**.  
Reflection polices and observes that POD — it is not the codec.

---

# Toolchain reality check

| Side | Compiler | Flags |
| --- | --- | --- |
| Host | GCC **16.2** | `-std=c++26 -freflection` |
| ESP32-H2 | IDF GCC **16.1** | `-std=gnu++26` + `-freflection` |
| esp-clang 20 | — | rejects `-freflection` |

`<meta>` alone is not enough — you need the flag.  
Same sources compile on both ends of the PPP link.

---

# Demo sketch (live)

1. Flash `zb_rpc_server` → PPP up → `RPC listening :3333`
2. Run `zb_rpc_client` → open network → wait for sensor join
3. Watch reflected logs: `find_sensor`, `zb_basic_t { manufacturer=… }`
4. Optional: leave a 2s gap between calls — idle socket must survive

```bash
# ESP
idf.py -p /dev/ttyUSB0 flash monitor

# Host
./build/zb_rpc_client --device /dev/ttyUSB1
```

---

# Takeaways

1. **C++26 reflection** is already useful for *policy* and *glue*, not only pretty-printing.
2. **`consteval wire_safe`** turns “don’t put pointers on the wire” into a type system rule.
3. **`^^fn` + splice** gives typed server dispatch without macros or codegen.
4. **Endian wrappers** keep the protocol honest even when both CPUs are LE.
5. Keep the catalog tiny — reflection shines when the ABI is intentional and small.

---

# References in-tree

```
zb_rpc_shared/include/zb_rpc/
  protocol.hpp      # le*, frames, rpc_desc catalog
  reflection.hpp    # wire_safe, api_name, dump
  engine.hpp        # Engine, call, invoke_one
  log.hpp           # ZB_RPC_LOG hook

zb_rpc_server/      # ESP: Zigbee handlers + PPP TCP
zb_rpc_client/      # Linux: thermostat sequence + smolppp
```

**Questions?**

<small>Inspired by the reflection-RPC sketch in esp-wifi-remote; this demo hardens endianness and ships on GCC 16 host + IDF.</small>
