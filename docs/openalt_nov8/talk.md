---
marp: true
theme: default
paginate: true
header: 'Zigbee from your laptop'
footer: 'github.com/david-cermak/zigbee-remote'
style: |
  section {
    font-size: 28px;
  }
  h1 {
    font-size: 1.55em;
  }
  h2 {
    font-size: 1.15em;
  }
  pre {
    font-size: 0.72em;
  }
  table {
    font-size: 0.75em;
  }
  a {
    color: #0a66c2;
  }
---

# Zigbee from your laptop

**Run the application on your PC, keep the radio on the chip.**

* Coordinator's API reflected over TCP (same headers/types).
* Using C++26 reflection.

&nbsp;

### &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;David Cermak, **Embedded developer**

---

# Agenda

* Zigbee + SDK (*esp-zigbee-sdk*)
* Abstraction vs. Reflection
* Use cases: Remote the coordinator from the **Host**
  - `wifi-remote` pattern: *Host* is a general purpose chip
  - prototype in **Compiler Explorer**: *Host* is your PC
* Demo

* Where the C++ comes in

---


![width:1024px](./host-slave-2.png)

---


![width:1024px](./host-slave-1.png)

---


![width:1024px](./host-slave.png)

---

# Wifi-remote pattern

Wi-Fi RPC between two nodes: **Host** and **Slave**.
The host **reflects** the slave's API.

![width:1024px](./eppp.webp)

---

# Wifi-remote pattern

* Specific radio chip
* General purpose mico

![bg left width:760px](./ESP32-P4-Pi.jpg)


---
# Maya and Zig-bee

 ![bg left width:420px](maya.jpg)

In the 2012 animated series, there is a mischievous minor character named **Zig**, who is a runaway baby bee.

Maya meets Zig when he flees his hive after getting into trouble.

<small>source: Wikipedia </small>

---
# Zigbee: name

![bg left width:420px](bee-dance.svg)

**Waggle dance**: honey bees perform to communicate with each other.

Bees **zig-zagging** back and forth to share information about where food is

Zigbee smart devices pass data mesh-style from one node to another to establish a connection.

<small>source: Wikipedia </small>

---

# Zigbee: basics

Built on **IEEE 802.15.4**: PHY and MAC for low-rate PAN (personal area networks).

| | Wi-Fi | BLE | **Zigbee** |
| --- | --- | --- | --- |
| Band | 2.4 / 5 GHz | 2.4 GHz | 2.4 GHz |
| Rate | Mbps–Gbps | ~1 Mbps | **250 kbps** |
| Power | wall | coin cell, days | **coin cell, years** |
| Topology | star | point-to-point | **mesh** |
| Payload | streams | small | **tens of bytes** |

16 channels (11–26), CSMA/CA. Designed for sensors and switches, not video.

---

# Network, roles

```
              +------------------+
              | Coordinator (ZC) |   1 per network
              +--------+---------+
                       |
          +------------+------------+
          |                         |
   +------+------+           +------+------+
   | Router (ZR) |           | Router (ZR) |   0..n, mains-powered
   +------+------+           +------+------+
          |                         |
   +------+------+           +------+------+
   | End Device  |           | End Device  |   0..n, battery, sleeps
   +-------------+           +-------------+
```

- ZC forms the network, picks channel + PAN ID, hands out **16-bit short addresses**
- ZR forwards traffic, buffers for sleeping children
- ZED talks only to its parent, sleeps ~99% of the time

---

# The data model: node → endpoint → cluster

```
[ Node ]  one IEEE (EUI-64) address, one 16-bit short address
   └── [ Endpoint 1 ]  logical app container, 1..240
   │      ├── Cluster 0x0006  On/Off      (server: state + commands)
   │      └── Cluster 0x0402  Temperature (server: measured value)
   └── [ Endpoint 2 ]
          └── Cluster 0x0006  On/Off      (second gang)
```

| Cluster | ID | What lives there |
| --- | --- | --- |
| Basic | `0x0000` | manufacturer, model, build |
| On/Off | `0x0006` | `OnOff` attribute + on/off/toggle |
| Temperature Measurement | `0x0402` | `MeasuredValue`, min/max, tolerance |

**Client** sends commands / reads; **server** owns attributes and executes.
Binding = direct device→device link. Reporting = sensor pushes, nobody polls.

---

# The chip: ESP32-H2 + esp-zigbee-sdk

Official Espressif Zigbee framework, built on ESP-IDF.

- Stack ships as a **prebuilt library** (`esp-zigbee-lib`), pulled from the component registry
- `v2.x`: Espressif's own Zigbee stack (recommended for new designs)
- Targets with 802.15.4 radio: ESP32-H2, ESP32-C6, ESP32-C5
- Examples: lights, switches, thermostats, sleepy devices, gateway, console

---

# The API

Plain C. `ezb_*` for the stack, `esp_zb_*` for ZCL/ZDO helpers.

```c
/* network */
ezb_shortaddr_t ezb_nwk_get_short_address(void);
uint16_t        ezb_nwk_get_panid(void);
uint8_t         ezb_nwk_get_current_channel(void);

/* commissioning */
ezb_err_t ezb_bdb_start_top_level_commissioning(ezb_bdb_comm_mode_mask_t mode);
ezb_err_t ezb_bdb_open_network(uint8_t permit_duration);

/* ZCL (Cluster Library management) / ZDO (Device Object management) */
uint8_t esp_zb_zcl_read_attr_cmd_req(esp_zb_zcl_read_attr_cmd_t *cmd);
uint8_t esp_zb_zcl_config_report_cmd_req(esp_zb_zcl_config_report_cmd_t *cmd);
void    esp_zb_zdo_device_bind_req(esp_zb_zdo_bind_req_param_t *req, cb_t cb, void *ctx);
```

Asynchronous: requests go out, callbacks/signals come back.

---

# "Reflection" here means two things

1. **API reflection**: Host exposes the same functions, names, structs
   as the firmware. A transparent copy, not a wrapper.
2. **C++26 reflection**: Compiler generates the glue from those same
   declarations.

Same header on both sides.

```cpp
struct zb_temp_t {          // firmware and host: identical layout
    int32_t err;
    le16    measured;       // explicit little-endian, not host int16_t
    le16    min_measured;
    le16    max_measured;
    le16    tolerance;
};
```

---

# Abstraction vs. Reflection


### AUTOSAR/MCAL (Hardware Abstraction)

MCAL (Microcontroller Abstraction Layer) provides standard API to the upper  layers.
* Hide the chip-specific register complexities. Because of this abstraction, 
* Swap a Renesas for an NXP chip; leave the app code untouched.

### zigbee-remote (Opposite approach)

* Reflects the actual SDK API onto the host
* Host uses this API, it doesn't need a simplified API

Abstraction hides the device. Reflection **exposes** it, one hop away.

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;<small>transparent proxy vs. gateway</small>


---

# Demo (1/3)

```
  [ temperature sensor ]
          │  802.15.4, ZCL reports
          ▼
  ┌──────────────────────────────┐
  │  ESP32-H2  (Slave / server)  │
  │  esp-zigbee-sdk + ezb_* API  │
  │  RPC server (reflection)     │
  └──────────────┬───────────────┘
                 │  PPP over UART, 115200
                 │  then TCP 192.168.11.2:3333
  ┌──────────────▼───────────────┐
  │  Laptop  (Host / client)     │
  │  thermostat_app.hpp          │
  │  zigbee_api.h  → RPC         │
  └──────────────────────────────┘
```

Host = client. Slave = server. The Zigbee stack never leaves the chip.

---

# Demo (2/3)


1. ESP boots, forms the network, prints PAN ID / channel / short address
2. Laptop connects over PPP, opens the network for 180 s
3. Sensor joins → `DEVICE_ANNCE` → find → Basic read → bind → report config
4. Temperature reports arrive; the app prints degrees Celsius
5. Same binary logic, same source, running on two very different machines

---

# Demoe (3/3)

```cpp
zigbee_rpc::attach(fd);                          // laptop only

uint16_t panid = zigbee_get_panid();             // 0x4c19
zigbee_open_network(180);                        // permit join
uint16_t peer  = zigbee_wait_annce(180000);      // 0x1956 joined
auto sensor    = zigbee_find_sensor(peer, 15000); // match descriptor
auto basic     = zigbee_read_basic(sensor.short_addr, sensor.ep, 15000);
zigbee_bind_sensor(sensor.short_addr, sensor.ep, 15000);
zigbee_config_report(sensor.short_addr, sensor.ep, 15000);
auto temp      = zigbee_read_temp(sensor.short_addr, sensor.ep, 15000);
```

---

# Thank you

## Takeaway

* Reflection and C++26 reflection
* Dual chip solution
* Prototype on your PC
  - Compiler explorer used as jupyter notebook
  - Prototype as you type
  - Copy the code from CE -> Paste to FW

---

# Links

| | |
| --- | --- |
| Repo | https://github.com/david-cermak/zigbee-remote |
| ESP Zigbee SDK | https://github.com/espressif/esp-zigbee-sdk |
| SDK docs | https://docs.espressif.com/projects/esp-zigbee-sdk |
| IEEE 802.15.4 | https://en.wikipedia.org/wiki/IEEE_802.15.4 |

---

# Zigbee remote

Same shape, different pipe.

- **Slave:** ESP32-H2 — Zigbee coordinator, 802.15.4, PPP UART
- **Host:** laptop — application, no radio at all
- Transport: PPP over UART → `tun0` → plain TCP `:3333`

What the host gets:

- `ezb_nwk_get_short_address()` → `zigbee_get_short_addr()`
- `ezb_bdb_open_network(duration)` → `zigbee_open_network(duration)`
- ZCL read of `0x0402` → `zigbee_read_temp(addr, ep, timeout)`

Thin adapters keep the firmware's argument types, timeouts included.

---

# What actually goes on the wire

Frame = 12-byte header + a packed POD. No text, no schema, no codegen.

```cpp
struct WireHeader {
    le32 magic;   // "RPC2"
    le32 id;      // api_id::read_temp
    le32 size;    // payload bytes
};
```

```text
[ magic | id | size ][ err | measured | min | max | tolerance ]
```

- Little-endian **by construction** — `le16` / `le32` wrappers
- Pointers, host ints, unions → **compile error**
- Small fixed payload; engine fragments. Hex-dump friendly.

---

# Same headers, both sides

`zb_rpc_server/main/zigbee_api.h` — real stack on the ESP:

```c
uint16_t  zigbee_get_panid(void);
int32_t   zigbee_open_network(uint8_t duration);
zigbee_temp_t zigbee_read_temp(uint16_t short_addr, uint8_t ep, uint32_t timeout_ms);
```

`zb_rpc_client/include/zigbee_api.h` — same file surface, implemented over RPC:

```cpp
inline uint16_t zigbee_get_panid(void) {
    zb_rpc::U16Resp response{};
    if (!zb_rpc::call<zb_rpc::desc::get_panid>(zigbee_rpc::engine(), response))
        return 0;
    return zb_rpc::from_le16(response.value);
}
```

The host version is boring on purpose. Every function is one RPC call.

---

# The C++ part, in one slide

Reflection is used for the glue, not for the application.

```cpp
constexpr auto type = ^^T;                        // look at the type
typename [:type_meta:] value;                     // splice it back as code
template for (constexpr auto m : members_of(^^T)) // compile-time loop
```

`wire_safe<T>()` walks the struct at compile time and answers one question:
*can this go on the wire?* `send<T>()` gates on it:

```cpp
template <typename T>
void send(const T &payload)
{
    static_assert(wire_safe<T>(), "payload is not wire-safe");
    static_assert(sizeof(T) <= k_max_payload, "payload too big");
}
```

A pointer or a host `uint16_t` in a wire struct → **build fails**, not a bad packet.

---

# The contract is the name

One namespace names the RPC. The enum gives it an id.

```cpp
enum class api_id : uint32_t {
    get_panid = 2, open_network = 4, read_temp = 11, /* ... */
};
namespace sim {
    U16Resp get_panid(EmptyReq);
    I32Resp open_network(OpenNetworkReq);
    zb_temp_t read_temp(zb_addr_t);
}
```

Baked at compile time — catalog, dispatch table, client wrappers:

```text
  get_panid     id=2  req=EmptyReq         resp=U16Resp
  open_network  id=4  req=OpenNetworkReq   resp=I32Resp
  read_temp     id=11 req=zb_addr_t        resp=zb_temp_t
```

Rename a handler without the enum (or vice versa) → build error. No drift.

---

# Adding one call: three edits, zero codegen

1. Wire structs (request / response), `le*` fields only
2. One handler in the namespace + one `api_id` enumerator, same name
3. One thin `zigbee_*` adapter on the host

Then it exists everywhere: server dispatch, client call, catalog line, log names.

No IDL file. No regeneration step. No second schema to keep in sync.
`static_assert(verify_catalog<^^::sim>())` checks the whole namespace on every build.

---

# Example: the whole flow

```cpp
zigbee_rpc::attach(fd);                          // laptop only

uint16_t panid = zigbee_get_panid();             // 0x4c19
zigbee_open_network(180);                        // permit join
uint16_t peer  = zigbee_wait_annce(180000);      // 0x1956 joined
auto sensor    = zigbee_find_sensor(peer, 15000); // match descriptor
auto basic     = zigbee_read_basic(sensor.short_addr, sensor.ep, 15000);
zigbee_bind_sensor(sensor.short_addr, sensor.ep, 15000);
zigbee_config_report(sensor.short_addr, sensor.ep, 15000);
auto temp      = zigbee_read_temp(sensor.short_addr, sensor.ep, 15000);
```

On the H2 each line becomes `ezb_*` / `esp_zb_*`. On the laptop, an RPC frame.



<!--
Have the host-only demo ready. Hardware demos fail in the most educational ways.
-->

---

# Takeaway

* C++26 reflection
* Dual chip solution
* Prototype on your PC

---

# Links

| | |
| --- | --- |
| Repo | https://github.com/david-cermak/zigbee-remote |
| ESP Zigbee SDK | https://github.com/espressif/esp-zigbee-sdk |
| SDK docs | https://docs.espressif.com/projects/esp-zigbee-sdk |
| IEEE 802.15.4 | https://en.wikipedia.org/wiki/IEEE_802.15.4 |
| C++26 reflection talk | `docs/cpp_meetup_oct26/talk_carousel.md` |


