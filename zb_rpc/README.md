# zigbee-remote (host simulation)

Working name for the new GitHub repo: **`zigbee-remote`**. This directory is the host-only RPC demo; the intended product is a Zigbee RPC controller (laptop ↔ coordinator), not a one-off example.

Full design: [`../PLAN.md`](../PLAN.md). Copy that file into the new repo.

## Build

GCC 16.2 with `-freflection` (not the system compiler):

```bash
source /home/david/local/export.sh   # gcc -> /home/david/local/bin/gcc
make
make demo                            # server + client on 127.0.0.1:3333
# ./zb_rpc_server [host] [port]
# ./zb_rpc_client [host] [port]
```

## Layout

- `include/zb_rpc/` — protocol, sockets, reflection dump/safety, catalog, invoke. **Reuse on ESP-IDF** (same POSIX socket API). Swap `log.hpp` to `ESP_LOG` later.
- `src/sim_zigbee.*` — fake stack. **Do not ship to the chip**; replace with `ezb_*` + `esp_zigbee_lock_acquire/release`.
- `src/client.cpp` — thermostat application sequence (stays on Linux).
- `src/server.cpp` — listen/accept + catalog dispatch (becomes the H2 RPC task).

## Notes for the next session

- Wire format is wifi-remote: `{RpcHeader {api_id, size} | memcpy POD}`. Reflection is dump + `rpc_memcpy_safe` + catalog glue, **not** a new codec.
- Always loop `read`/`write` until the header and blob are complete. Do not copy wifi-remote’s single-`read` shortcut.
- Native ZCL cmds (`ezb_zcl_read_attr_cmd_t`, bind `cb`/`user_ctx`) have pointers. Keep packed `zb_addr_t` / `zb_basic_t` / `zb_temp_t` on the wire.
- Roles: client = thermostat app; server = stack. Real hardware: chip 1 = stock `temperature_sensor`; chip 2 = cloned thermostat + this server + PPP UART; laptop = this client with `--host` = PPP IP.
- USB-JTAG = console. PPP = **second UART** (IDF `ppp_connect.c` UART path, not USB CDC). Do not PPP on the monitor port.
- Next work after this sim: PPP echo on H2, then link `zb_rpc` into a thermostat clone. Catalog and client stay.
