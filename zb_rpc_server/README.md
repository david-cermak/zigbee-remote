# zb_rpc_server

ESP32-H2 Zigbee coordinator + PPP TCP RPC server. Zigbee stays on-chip; the
Linux client in `zb_rpc_client/` drives the thermostat application sequence
through `zb_rpc_shared`.

Copied/adapted from `thermostat/` (Zigbee) and `ppp_tcp_server/` (PPP UART).
Does not modify those projects.

## Wiring

- Console flash/monitor: `/dev/ttyUSB0`
- PPP UART: `/dev/ttyUSB1` ↔ GPIO10 TX / GPIO11 RX @ 115200
- Host `192.168.11.1`, ESP `192.168.11.2:3333`

## Build / flash

```bash
source /home/david/dev/idf/export.sh
idf.py set-target esp32h2
idf.py -p /dev/ttyUSB0 flash monitor
```

Pair with `../zb_rpc_client` after `tun0` exists. The temperature sensor board
must join after the client opens the network.
