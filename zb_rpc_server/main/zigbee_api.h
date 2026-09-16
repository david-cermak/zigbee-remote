#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t err;
    uint16_t short_addr;
    uint8_t ep;
} zigbee_addr_t;

typedef struct {
    int32_t err;
    char manufacturer[16];
    char model[16];
} zigbee_basic_t;

typedef struct {
    int32_t err;
    int16_t measured;
    int16_t min_measured;
    int16_t max_measured;
    int16_t tolerance;
} zigbee_temp_t;

esp_err_t zigbee_stack_start(void);

uint16_t zigbee_get_short_addr(void);
uint16_t zigbee_get_panid(void);
uint8_t zigbee_get_channel(void);
int32_t zigbee_open_network(uint8_t duration);
uint16_t zigbee_wait_annce(uint32_t timeout_ms);
zigbee_addr_t zigbee_find_sensor(uint16_t short_addr, uint32_t timeout_ms);
zigbee_basic_t zigbee_read_basic(uint16_t short_addr, uint8_t ep, uint32_t timeout_ms);
int32_t zigbee_bind_sensor(uint16_t short_addr, uint8_t ep, uint32_t timeout_ms);
int32_t zigbee_subscribe_sensor(uint16_t short_addr, uint8_t ep, uint32_t timeout_ms);
int32_t zigbee_config_report(uint16_t short_addr, uint8_t ep, uint32_t timeout_ms);
zigbee_temp_t zigbee_read_temp(uint16_t short_addr, uint8_t ep, uint32_t timeout_ms);
int16_t zigbee_get_last_temp(void);

#ifdef __cplusplus
}
#endif
