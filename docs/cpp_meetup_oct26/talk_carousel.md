---
marp: true
theme: default
paginate: true
header: 'C++26 Reflection, practically'
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

# C++ Reflection, practically

**C++26 static reflection**: inspect types, generate code, zero runtime tax for the inspection itself.

A real RPC: host reflects on a slave’s API.
No codegen. Same headers on both sides.

### &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;David Cermak, **Embedded developer**
---

# Agenda

1. What is C++26 Reflection (reads code + writes code)
2. Project: Wi-Fi RPC, Zigbee RPC
3. Three patterns
   - **wire-safe**: refuse bad payloads at compile time
   - **dispatch by name**: enum ↔ handler can’t drift
   - **auto catalog**: bake an API string at compile time
4. Cost: flash, CMake, static analysis

---

# What is C++26 reflection?

Compile-time feature that lets a program:

1. **inspect** its own structure
2. **generate** code from those observations

No runtime RTTI walk. You pay only for what you specialize: like a template library.

```cpp
constexpr std::meta::info type_meta = ^^float;
typename [:type_meta:] my_variable = 3.14f;
```

`^^` = look at · `[: :]` = splice back as code

---

# Principle: two verbs + a loop

| Construct | Role |
| --- | --- |
| `^^T` | reflect a type / function |
| `[: info :]` | splice → C++ entity |
| `template for` | compile-time loop |
| `nonstatic_data_members_of` | struct walk |
| `enumerators_of` | enum walk |
| `identifier_of` | printable names |
| `type_of` / `is_*_type` | classify |
| `reflect_constant_array` | materialize a reflection range |

Almost everything below is those verbs plus `template for`.

---

# 1) Read code: pretty-print without annotations

Walk members. Print names from the program, values from the object.
No X-macros. No per-type printer. No extra attributes on the struct.

https://godbolt.org/z/ToWhjYh8j

---

# Read: the loop that matters

```cpp
template <typename T>
std::string to_string(const T& obj) {
  constexpr auto ctx = std::meta::access_context::current();
  std::ostringstream os;
  os << std::meta::identifier_of(^^T) << " { ";
  bool first = true;
  template for (constexpr auto m :
      [: std::meta::reflect_constant_array(
          std::meta::nonstatic_data_members_of(^^T, ctx)) :])
  {
    if (!first) os << ", ";
    os << std::meta::identifier_of(m) << " = " << obj.[:m:];
    first = false;
  }
  os << " }";
  return os.str();
}
```

---

# Read: what you get

```cpp
struct Point  { int x; int y; };
struct Person { std::string name; int age; };

to_string(Point{3, 7});
// → Point { x = 3, y = 7 }

to_string(Person{"Alice", 30});
// → Person { name = Alice, age = 30 }
```

**Takeaway:** layout and names come from the type: not a comment you forgot to update.

---

# 2) Write code: generate machinery

Reflection can build a vtable / duck wrapper from method shapes.
Credit **Ryan Keane**: https://github.com/RyanJK5/rjk-duck

https://godbolt.org/z/3PWPT1dEW

Our RPC uses a narrower scope: catalog lines + splice handlers into dispatch.
We do **not** implement a serializer.

---

# Write: duck typing from reflection

```cpp
struct Device { void boot(); };

struct STM32 {
  auto boot() { std::println("bonjour"); }
};
struct ESP32 {
  auto boot() { std::println("nihao"); }
};

int main() {
  duck<Device> board{ESP32{}};
  board.boot();   // nihao
  board = STM32{};
  board.boot();   // bonjour
}
```

If it has `boot`, it fits. The dispatch table is reflected, not handwritten.

---

# What do you think?

🟢 **About time**: C++ needed this for decades  
🔴 **Bloated enough**: please stop adding features  
👀 **Convince me**: show a real use case  
⚪ **Don’t care**: I’ll never use it  

This deck is for 👀: and for 🔴 who only move if the cost is small.

---

# Project: Host reflects on Slave’s API

Wi-Fi RPC between two nodes.

- **Slave (server):** ESP32-H2: Zigbee stays on the chip
- **Host (client):** PC / any node without 802.15.4
- Pipe: TCP (lab: PPP-over-UART that uses socket API)

![width:720px](./eppp.webp)

---

# Same headers. No codegen.

https://github.com/david-cermak/zigbee-remote

Bring a Zigbee coordinator API to your laptop over plain TCP.
C++26 reflection on the **same** wire types and catalog on both sides.

- Not a full Zigbee-stack remoting tool
- Not IDL codegen
- Payload is little-endian **by construction** (`le16` / `le32` byte wrappers)

Misspell a handler → build fails. Not a silent unknown opcode at runtime.

---

# Pattern A: Wire-safe

**Principle:** reflection polices layout; it does not invent encoding.

A `consteval` predicate walks the type and answers yes/no.
`send()` is gated with `static_assert`: before anything hits the socket.

https://godbolt.org/z/qa661n4a6

---

# Wire-safe: what is allowed

```cpp
template <typename T>
consteval bool wire_safe()
{
  constexpr auto type = ^^T;
  if constexpr (std::is_void_v<T> || std::is_same_v<T, EmptyReq>)
    return true;
  else if constexpr (std::is_same_v<T, uint8_t> ||
                     std::is_same_v<T, char> ||
                     std::is_same_v<T, le16> ||
                     std::is_same_v<T, le32>)
    return true;
  // …
}
```

Leaves: `uint8_t`, `char`, `le16`, `le32`, empty request.
Host `uint16_t` / pointers / unions → **no**.

---

# Wire-safe: recurse into structs & arrays

```cpp
  else if constexpr (std::meta::is_array_type(type)) {
    using Element = [:std::meta::remove_all_extents(type):];
    return wire_safe<Element>();
  } else if constexpr (std::meta::is_class_type(type)) {
    constexpr auto ctx = std::meta::access_context::current();
    template for (constexpr auto member :
        [:std::meta::reflect_constant_array(
            std::meta::nonstatic_data_members_of(type, ctx)):]) {
      using Member = [:std::meta::type_of(member):];
      if constexpr (!wire_safe<Member>())
        return false;
    }
    return true;
  }
  return false; // pointers, host ints, unions, …
```

---

# Wire-safe: gate at the boundary

```cpp
template <typename T>
void send(const T & /*payload*/)
{
  static_assert(wire_safe<T>(), "payload is not wire-safe");
  static_assert(sizeof(T) <= k_max_payload, "payload too big");
}

struct GoodPayload { le32 err; le16 short_addr; uint8_t ep; };
struct BadHostInt  { uint16_t x; };  // host endian: rejected
struct BadPointer  { char *p; };     // not on the wire: rejected

send(GoodPayload{});   // OK
// send(BadHostInt{}); // compile error
```

**Why not a serializer?** A memberwise packer would happily ship a pointer’s bits.

---

# Pattern B: Dispatch by id

**Principle:** handler name **must** equal an `api_id` enumerator.
Drift is a compile-time failure, not a runtime mystery.

https://godbolt.org/z/qdjE4az3o

---

# Dispatch: the contract is the name

```cpp
enum class api_id : uint32_t {
  error = 0,
  get_short_addr = 1,
  get_panid = 2,
  open_network = 4,
};

namespace sim {
  U16Resp get_short_addr(EmptyReq);
  U16Resp get_panid(EmptyReq);
  I32Resp open_network(OpenNetworkReq);
  // U16Resp typo_handler(EmptyReq);  // ❌ no matching enumerator
}
```

Add a call: add the enumerator **and** the function. Names lock them together.

---

# Dispatch: name → id at compile time

```cpp
consteval api_id id_for_name(std::string_view name)
{
  template for (constexpr auto enumerator :
      [:std::meta::reflect_constant_array(
          std::meta::enumerators_of(^^api_id)):]) {
    if (std::meta::identifier_of(enumerator) == name)
      return [:enumerator:];
  }
  throw "handler name has no matching api_id enumerator";
}
```

`throw` in `consteval` → hard compile error. Typos don’t ship.

---

# Dispatch: filter + splice + invoke

```cpp
consteval bool is_rpc_handler(std::meta::info member)
{
  return std::meta::is_function(member)
      && std::meta::has_identifier(member)
      && !std::meta::is_special_member_function(member)
      && std::meta::parameters_of(member).size() == 1;
}

// Walk namespace members; on id match:
using Request = [:std::meta::type_of(
    std::meta::parameters_of(member)[0]):];
Request request{};
auto response = [:member:](request);  // splice the function
```

Runtime id vs compile-time id-from-name. Match → call. No hand-rolled switch.

---

# Pattern C: Auto catalog

**Principle:** same namespace, same enum → **string baked at compile time**.
Dump walks handlers, resolves id, appends `name / id / req / resp`.

https://godbolt.org/z/8cono9jPx

---

# Catalog: describe one function

```cpp
template <std::meta::info Function>
struct fn_desc {
  static constexpr api_id id =
      id_for_name(std::meta::identifier_of(Function));
  using req_type =
      [:std::meta::type_of(std::meta::parameters_of(Function)[0]):];
  using resp_type =
      [:std::meta::return_type_of(Function):];
};

using GetPanid = fn_desc<^^sim::get_panid>;
static_assert(GetPanid::id == api_id::get_panid);
static_assert(std::is_same_v<GetPanid::req_type, EmptyReq>);
```

One reflection of a function → id + request + response types.

---

# Catalog: bake the whole API string

```cpp
template <std::meta::info Namespace>
consteval std::string catalog_dump()
{
  std::string out = "rpc catalog:\n";
  constexpr auto ctx = std::meta::access_context::current();
  template for (constexpr auto member :
      [:std::meta::reflect_constant_array(
          std::meta::members_of(Namespace, ctx)):]) {
    if constexpr (is_rpc_handler(member)) {
      (void)id_for_name(std::meta::identifier_of(member));
      append_catalog_line(out, member);
    }
  }
  return out;
}

inline constexpr auto k_catalog =
    std::define_static_string(catalog_dump<^^::sim>());
```

---

# Catalog: what prints

```text
rpc catalog:
  get_short_addr id=1 req=EmptyReq resp=U16Resp
  get_panid      id=2 req=EmptyReq resp=U16Resp
  open_network   id=4 req=OpenNetworkReq resp=I32Resp
```

Uncomment a new handler + enumerator → catalog grows.
No other edits. No IDL regen step.

---

# Practical: footprint (ESP32)

You pay for the specializations you **use**.

| Step | Flash code Δ | Flash data Δ | RAM |
| --- | ---: | ---: | ---: |
| `-freflection` / `<meta>` unused | **0** | **0** | **0** |
| `print_object` (flat structs) | **+492** | **+144** | **0** |
| + nested `SubPerson` | **+176** | **+64** | **0** |
| + runtime dispatch (4 cmds) | **+544** | **+240** | **0** |

Enable the flag alone → no image growth. Cost is compile-time (+ debug info).

---

# Practical: CMake detection

`-freflection` is only accepted with `-std=c++26` / `gnu++26`.

```cmake
include(CheckCXXCompilerFlag)
set(_saved "${CMAKE_REQUIRED_FLAGS}")
set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -std=gnu++26")
check_cxx_compiler_flag(-freflection HAS_FREFLECTION)
set(CMAKE_REQUIRED_FLAGS "${_saved}")

if(HAS_FREFLECTION)
  target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++26>
    $<$<COMPILE_LANGUAGE:CXX>:-freflection>)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE HAS_FREFLECTION=1)
endif()
```

---

# Practical: static analysis today

Prod toolchain not on C++26 yet?

- Build a **slice** of your classes with GCC 16
- Encode project constraints as `consteval` predicates
- Same idea as semgrep rules: but with native C++ types

Wire-safe is a policy engine.

---

# Practical: GCC 16 gotchas

* `members_of` / `parameters_of` → vectors in a consteval context  
  → expand via `reflect_constant_array`
* `identifier_of` fails on unnamed entities  
  → filter constructors / special members
* Empty C++ classes: `sizeof == 1`  
  → wire “empty” needs an `EmptyReq{}` specialization (size 0 on the wire)

---

# Three principles to remember

1. **Police, don’t invent**: reflection validates wire types; encoding stays explicit
2. **Generate what you can**: catalogs and ids at compile time; runtime only carries bytes
3. **Lint your codebase**: build your own project specific C++ strict typing rules; don't need C++26-ready production toolchain

---

# Links

| | |
| --- | --- |
| Repo | https://github.com/david-cermak/zigbee-remote |
| Pretty-print | https://godbolt.org/z/ToWhjYh8j |
| Duck / write | https://godbolt.org/z/3PWPT1dEW |
| Wire-safe | https://godbolt.org/z/qa661n4a6 |
| Dispatch | https://godbolt.org/z/qdjE4az3o |
| Catalog | https://godbolt.org/z/8cono9jPx |
| rjk-duck | https://github.com/RyanJK5/rjk-duck |

---

# C++ Reflection: WYT now?

🟢 **Great feature**  
🔴 **Doesn’t belong in C++**  
⚪ **Don’t care**

If you’re still 👀: clone the repo, open a Godbolt, break a `wire_safe` assert on purpose.
