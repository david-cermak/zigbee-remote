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

## Single-file build (Compiler Explorer)

`generate_single_file.py` embeds the RPC headers, the serial/PPP sources and
the required lwIP sources into `header_zb_rpc_client.hpp`.  `main.cpp` includes
that header when `SINGLE_FILE_COMPILATION` is defined, so the client can be
built from a single translation unit:

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
