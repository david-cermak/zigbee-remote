cmake_minimum_required(VERSION 3.16)
project(zb_rpc_client C CXX)

set(CMAKE_C_STANDARD 11)
if(NOT DEFINED ENV{LWIP_DIR})
    message(FATAL_ERROR "LWIP_DIR is not set. Source export.sh before building.")
endif()

set(LWIP_DIR "$ENV{LWIP_DIR}")
set(LWIP_CONTRIB_DIR "${LWIP_DIR}/contrib")
if(NOT EXISTS "${LWIP_DIR}/src/Filelists.cmake")
    message(FATAL_ERROR "lwIP not found at LWIP_DIR=${LWIP_DIR}")
endif()
if(NOT EXISTS "${LWIP_CONTRIB_DIR}/ports/unix/port/sys_arch.c")
    message(FATAL_ERROR "lwIP Unix port not found")
endif()

include("${LWIP_DIR}/src/Filelists.cmake")
set(LWIP_PORT_SOURCES "${LWIP_CONTRIB_DIR}/ports/unix/port/sys_arch.c")
list(REMOVE_ITEM lwipnoapps_SRCS "${LWIP_DIR}/src/netif/slipif.c")
list(REMOVE_ITEM lwipnoapps_SRCS "${LWIP_DIR}/src/core/ipv4/ip4.c")
list(REMOVE_ITEM lwipnoapps_SRCS "${LWIP_DIR}/src/core/ipv6/ip6.c")

set(LWIP_IP4_SRC "${LWIP_DIR}/src/core/ipv4/ip4.c")
set_source_files_properties("${LWIP_IP4_SRC}" PROPERTIES
    COMPILE_DEFINITIONS "ip4_input=lwip_ip4_input"
)

add_executable(zb_rpc_client
    main.cpp
    ppp_link.c
    ppp_lwip.c
    serial_io.c
    netdev_fwd.c
    ip6_stub.c
    ${LWIP_IP4_SRC}
    ${lwipnoapps_SRCS}
    ${LWIP_PORT_SOURCES}
)

target_include_directories(zb_rpc_client PRIVATE
    include
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../zb_rpc_shared/include"
    "${LWIP_DIR}/src/include"
    "${LWIP_CONTRIB_DIR}/ports/unix/port/include"
)

target_compile_options(zb_rpc_client PRIVATE
    -Wall
    -Wextra
    $<$<COMPILE_LANGUAGE:CXX>:-std=c++26>
    $<$<COMPILE_LANGUAGE:CXX>:-freflection>
)
target_link_libraries(zb_rpc_client PRIVATE pthread)
