# zb_rpc_client

Linux host: embedded smolppp PPP server + Zigbee thermostat application over
RPC. Shares only `../zb_rpc_shared` with `zb_rpc_server`.

Copied from `ppp_tcp_client/` (PPP/TUN) and adapted from `zb_rpc/src/client.cpp`
(thermostat call sequence).

## Build

```bash
source /home/david/dev/idf/export.sh
source /home/david/local/export.sh
source ./export.sh
cmake -S . -B build -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build build -j
```

## Run

```bash
./make_tun_netif tun0 192.168.11.1 192.168.11.2   # once
./build/zb_rpc_client --device /dev/ttyUSB1
```
