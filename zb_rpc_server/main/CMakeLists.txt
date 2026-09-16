idf_component_register(
    SRCS
        "app_main.c"
        "ppp_uart.c"
        "rpc_server.cpp"
        "zigbee_stack.c"
    INCLUDE_DIRS
        "."
        "../../zb_rpc_shared/include"
    PRIV_REQUIRES
        esp_driver_uart
        esp_event
        esp_netif
        esp_timer
        lwip
        nvs_flash
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-freflection>
)
