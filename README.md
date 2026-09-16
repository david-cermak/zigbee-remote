# zigbee-remote

Bring a Zigbee coordinator API to your laptop (or any node without 802.15.4) over plain TCP, using **C++26 static reflection** on the same wire types and catalog on both sides—not a full Zigbee stack remoting tool, and not IDL codegen.

Zigbee stays on an ESP32-H2. The host runs the application (thermostat sequence today) and talks RPC. Reflection checks wire-safe PODs, dumps field names, and splices handlers into dispatch. The payload is little-endian by construction.

## Layout

| Path | Role |
| --- | --- |
| [zb_rpc_shared/](zb_rpc_shared/) | Shared headers: endian framing, catalog, `wire_safe` / dump, `call` / `invoke_one` |
| [zb_rpc_server/](zb_rpc_server/) | ESP32-H2: Zigbee stack + PPP UART + RPC server |
| [zb_rpc_client/](zb_rpc_client/) | Linux: smolppp PPP/TUN + thermostat client over RPC |
| [zb_rpc/](zb_rpc/) | Earlier host-only simulation (localhost TCP + fake Zigbee) |
| [docs/](docs/) | Marp talk + audio-book narration on the reflection RPC |

## Status

- **Shared RPC + reflection** (`zb_rpc_shared`) and **ESP/Linux step-3 projects** build.
- Lab path: ESP PPP client on UART (GPIO10/11) ↔ host PPP server/`tun0` ↔ TCP `:3333`.
- Live sensor join/temperature demo depends on a paired temperature sensor board.

See [zb_rpc_server/README.md](zb_rpc_server/README.md) and [zb_rpc_client/README.md](zb_rpc_client/README.md) for build/run.
