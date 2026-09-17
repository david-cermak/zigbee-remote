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

- `include/zb_rpc/` — wire protocol (`le16`/`le32`), `wire_safe` reflection, `Engine`, sockets, logging
- `src/sim_zigbee.*` — fake stack with wire-typed handlers (do not ship to the chip)
- `src/client.cpp` — thermostat application sequence
- `src/server.cpp` — listen/accept + catalog dispatch

## Wire format

Frames are `{WireHeader {magic, id, size} | packed POD}`. Multi-byte fields use explicit little-endian wrappers — never host `uint16_t`/`uint32_t` on the wire. Reflection polices types (`wire_safe`) and dumps field names; it is not a serializer.
