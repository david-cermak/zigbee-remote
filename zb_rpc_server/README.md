# zb_rpc_server

ESP32-H2 Zigbee coordinator + PPP TCP RPC server. Zigbee stays on-chip; the
Linux client in `zb_rpc_client/` drives the thermostat application sequence
through `zb_rpc_shared`.

Copied/adapted from `thermostat/` (Zigbee) and `ppp_tcp_server/` (PPP UART).
Does not modify those projects.

## Wiring

PPP UART and the console UART must be different; this is checked at build
time. The UART number and pins are configurable via menuconfig (`PPP UART
Zigbee RPC server`).

Default, single USB-UART cable (PPP on UART0, console on UART1):

- PPP: `UART0` ↔ GPIO24 TX / GPIO23 RX @ 115200 (the ESP32-H2 USB bridge)
- Console: `UART1` ↔ GPIO10 TX / GPIO11 RX (only needed for `idf.py monitor`)
- Host `192.168.11.1`, ESP `192.168.11.2:3333`

Two cables, monitor on the default UART0 (`sdkconfig.defaults.monitor-uart0`):

- Console flash/monitor: `/dev/ttyUSB0` (`UART0`, GPIO24/23)
- PPP UART: `/dev/ttyUSB1` ↔ GPIO10 TX / GPIO11 RX @ 115200 (`UART1`)

## Build / flash

```bash
source /home/david/dev/idf/export.sh
idf.py set-target esp32h2
idf.py -p /dev/ttyUSB0 flash monitor
```

For the two-cable wiring, generate the config with the alternate defaults
(remove the existing `sdkconfig` first when switching between modes):

```bash
idf.py -DSDKCONFIG_DEFAULTS=sdkconfig.defaults.monitor-uart0 build
```

Pair with `../zb_rpc_client` after `tun0` exists. The temperature sensor board
must join after the client opens the network.
