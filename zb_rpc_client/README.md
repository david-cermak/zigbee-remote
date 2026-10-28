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

## Firmware-style API

`include/zigbee_api.h` implements the same small API as
`../zb_rpc_server/main/zigbee_api.h`, but over RPC.  Application code can now
call plain functions instead of building wire structs and converting
endianness:

```cpp
zigbee_rpc::attach(socket_fd);

uint16_t panid = zigbee_get_panid();
int32_t err = zigbee_open_network(180);
zigbee_addr_t sensor = zigbee_find_sensor(peer_short, 15000);
zigbee_temp_t temp = zigbee_read_temp(sensor.short_addr, sensor.ep, 15000);

zigbee_rpc::detach();
```

The timeout arguments are kept so the signatures match the firmware; the RPC
server currently applies its own fixed request/announcement windows.

`include/thermostat_app.hpp` is the complete thermostat sequence written only
against that API.  It has no socket, PPP or RPC dependencies, so it can be
copied to the coordinator firmware (for example a future `zb-coordinator`
project) and compiled against the native `zigbee_api.h`.  On firmware it uses
`ESP_LOGI` and `vTaskDelay` automatically; override `ZB_APP_LOG` /
`ZB_APP_SLEEP_MS` if needed.

## Single-file build (Compiler Explorer)

`generate_single_file.py` embeds the RPC headers, the serial/PPP sources and
the required lwIP sources into `header_zb_rpc_client.hpp`.  It also embeds
`include/zigbee_api.h` and `include/thermostat_app.hpp`, so code in `main.cpp`
can call the firmware-style API directly.  `main.cpp` includes that header when
`SINGLE_FILE_COMPILATION` is defined, so the client can be built from a single
translation unit:

```bash
./generate_single_file.py                 # uses $LWIP_DIR or ../lwip
g++ -std=c++26 -freflection -fpermissive -w -pthread \
    -DSINGLE_FILE_COMPILATION main.cpp -o zb_rpc_client
```

With CMake:

```bash
cmake -S . -B build -DZB_RPC_CLIENT_SINGLE_FILE=ON \
      -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build build --target zb_rpc_client_single -j
```

The generated header is self-contained: `main.cpp` and
`header_zb_rpc_client.hpp` are the only files needed to build it (for example
as two files in the Compiler Explorer editor).  `-fpermissive` is required
because the C sources are compiled as C++ in the single translation unit;
`-freflection` is required by the RPC engine.
