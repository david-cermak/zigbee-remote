# zigbee-remote (host simulation)

Host-only Zigbee RPC demo: laptop client ↔ simulated coordinator server.

## Build

GCC 16.2 with `-freflection` (not the system compiler):

```bash
source ~/local/export.sh
make
make demo                            # server + client on 127.0.0.1:3333
# ./zb_rpc_server [host] [port]
# ./zb_rpc_client [host] [port]
```

## Layout

- `include/zb_rpc/protocol.hpp` — `le16`/`le32`, frames, `api_id`
- `include/zb_rpc/reflection.hpp` — `wire_safe`, `api_name`, recursive dump
- `include/zb_rpc/engine.hpp` — fragmentation-safe I/O, `call<>`, `invoke_one`
- `include/zb_rpc/auto_rpc.hpp` — name↔id, `fn_desc`, `dispatch_ns`, catalog verify
- `include/zb_rpc/catalog.hpp` — `desc::*` aliases + `api::*` client wrappers (from `sim::`)
- `include/zb_rpc/dispatch.hpp` — L1 fold over explicit `handler<Desc, ^^fn>` packs
- `src/sim_zigbee.*` — fake stack; **namespace `sim` is the RPC façade** (names match `api_id`)
- `src/client.cpp` / `src/server.cpp` — thermostat sequence / `dispatch_ns<^^::sim>`

## Wire format

Frames are `{WireHeader {magic, id, size} | packed POD}`. Multi-byte fields use explicit little-endian wrappers — never host `uint16_t`/`uint32_t` on the wire. Reflection polices types (`wire_safe`), dumps field names, and (L2) inventories `sim::` for dispatch; it is not a serializer.
