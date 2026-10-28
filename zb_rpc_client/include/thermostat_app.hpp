#pragma once

/*
 * Portable thermostat application sequence.
 *
 * This file only uses the small Zigbee API declared in zigbee_api.h.  On the
 * Linux client that API is implemented over RPC by
 * zb_rpc_client/include/zigbee_api.h; on the coordinator firmware the very
 * same calls are backed by the ESP Zigbee stack.  Keep this file free of
 * socket, PPP and RPC details so it can be copied to the firmware unchanged.
 */

#include "zigbee_api.h"

#include <cstdint>

#ifndef ZB_APP_LOG
#  ifdef ESP_PLATFORM
#    include "esp_log.h"
#    define ZB_APP_LOG(tag, fmt, ...) ESP_LOGI(tag, fmt, ##__VA_ARGS__)
#  else
#    include <cstdio>
#    define ZB_APP_LOG(tag, fmt, ...) std::fprintf(stderr, "%s: " fmt "\n", tag, ##__VA_ARGS__)
#  endif
#endif

#ifndef ZB_APP_SLEEP_MS
#  ifdef ESP_PLATFORM
#    include "freertos/FreeRTOS.h"
#    include "freertos/task.h"
#    define ZB_APP_SLEEP_MS(ms) vTaskDelay(pdMS_TO_TICKS(ms))
#  else
#    include <unistd.h>
#    define ZB_APP_SLEEP_MS(ms) ::usleep(static_cast<useconds_t>((ms) * 1000))
#  endif
#endif

namespace thermostat_app {

inline constexpr const char *k_tag = "THERMOSTAT";

/* These match the current server-side defaults.  The client RPC layer does
 * not forward its timeout arguments yet, so keep them in sync if you change
 * the server constants. */
inline constexpr uint32_t k_request_timeout_ms = 15000;
inline constexpr uint32_t k_annce_timeout_ms = 180000;
inline constexpr uint8_t k_open_network_seconds = 180;

/* zigbee_get_sensor_addr() returns this when the RPC failed (for example a
 * server that predates the cached-sensor API).  A real sensor never uses it. */
inline constexpr uint16_t k_invalid_short_addr = 0xFFFF;

enum class Result {
    success,
    failure,
    /* The server closed the connection while probing for the cached sensor
     * API.  Reconnect and run once more without that probe. */
    retry_without_cache,
};

inline float to_celsius(int16_t hundredths)
{
    return hundredths / 100.0f;
}

inline void print_temp_attrs(const zigbee_temp_t &attrs)
{
    ZB_APP_LOG(k_tag, "Read Attributes of Temperature Measurement");
    ZB_APP_LOG(k_tag, "Measured value: %.2f degrees Celsius", to_celsius(attrs.measured));
    ZB_APP_LOG(k_tag, "Min measured value: %.2f degrees Celsius", to_celsius(attrs.min_measured));
    ZB_APP_LOG(k_tag, "Max measured value: %.2f degrees Celsius", to_celsius(attrs.max_measured));
}

inline Result run(bool probe_cached_sensor)
{
    const uint16_t panid = zigbee_get_panid();
    const uint8_t channel = zigbee_get_channel();

    zigbee_addr_t sensor{};
    bool reused_sensor = false;

    /* If this server already commissioned a sensor, skip OPEN_NETWORK and the
     * DEVICE_ANNCE wait: the network is up and the device is known. */
    if (probe_cached_sensor && panid != 0) {
        const uint16_t known = zigbee_get_sensor_addr();
        if (known == k_invalid_short_addr) {
            ZB_APP_LOG(k_tag, "Server has no cached sensor API; reconnecting");
            return Result::retry_without_cache;
        }

        if (known != 0) {
            ZB_APP_LOG(k_tag,
                       "Network(0x%04x) on channel %u is already running; trying known sensor "
                       "0x%04x",
                       panid, channel, known);
            sensor = zigbee_find_sensor(known, k_request_timeout_ms);
            if (sensor.err == 0) {
                reused_sensor = true;
                ZB_APP_LOG(k_tag, "Reusing HA temperature sensor at (0x%04x, 0x%02x)",
                           sensor.short_addr, sensor.ep);
            } else {
                ZB_APP_LOG(k_tag, "Known sensor did not respond; falling back to commissioning");
            }
        }
    }

    if (!reused_sensor) {
        const int32_t open_status = zigbee_open_network(k_open_network_seconds);
        if (open_status != 0) {
            ZB_APP_LOG(k_tag, "Failed to open network (%ld)", static_cast<long>(open_status));
            return Result::failure;
        }
        ZB_APP_LOG(k_tag, "Network(0x%04x) is open for %u seconds", panid,
                   k_open_network_seconds);

        const uint16_t peer = zigbee_wait_annce(k_annce_timeout_ms);
        if (peer == 0) {
            ZB_APP_LOG(k_tag, "Timed out waiting for DEVICE_ANNCE");
            return Result::failure;
        }
        ZB_APP_LOG(k_tag, "New device commissioned or rejoined (short: 0x%04x)", peer);

        ZB_APP_LOG(k_tag, "Attempt to find HA temperature sensor device on address(0x%04x)", peer);
        sensor = zigbee_find_sensor(peer, k_request_timeout_ms);
        if (sensor.err != 0) {
            ZB_APP_LOG(k_tag, "Failed to find HA temperature sensor (%ld)",
                       static_cast<long>(sensor.err));
            return Result::failure;
        }

        ZB_APP_LOG(k_tag, "Attempt to read manuf_code and model_id from device (0x%04x, 0x%02x)",
                   sensor.short_addr, sensor.ep);
        const zigbee_basic_t basic =
            zigbee_read_basic(sensor.short_addr, sensor.ep, k_request_timeout_ms);
        if (basic.err != 0) {
            ZB_APP_LOG(k_tag, "Failed to read Basic attributes (%ld)",
                       static_cast<long>(basic.err));
            return Result::failure;
        }
        ZB_APP_LOG(k_tag, "Model identifier: %s", basic.model);
        ZB_APP_LOG(k_tag, "Manufacturer name: %s", basic.manufacturer);

        ZB_APP_LOG(k_tag, "Attempt to bind temperature sensor device (0x%04x, 0x%02x) to local",
                   sensor.short_addr, sensor.ep);
        const int32_t bind_status =
            zigbee_bind_sensor(sensor.short_addr, sensor.ep, k_request_timeout_ms);
        if (bind_status != 0) {
            ZB_APP_LOG(k_tag, "Failed to bind temperature sensor (%ld)",
                       static_cast<long>(bind_status));
            return Result::failure;
        }
        ZB_APP_LOG(k_tag, "Bound HA temperature sensor device (0x%04x, 0x%02x) to local successfully",
                   sensor.short_addr, sensor.ep);

        ZB_APP_LOG(k_tag,
                   "Attempt to subscribe temperature sensor device (0x%04x, 0x%02x) from local",
                   sensor.short_addr, sensor.ep);
        const int32_t subscribe_status =
            zigbee_subscribe_sensor(sensor.short_addr, sensor.ep, k_request_timeout_ms);
        if (subscribe_status != 0) {
            ZB_APP_LOG(k_tag, "Failed to subscribe temperature sensor (%ld)",
                       static_cast<long>(subscribe_status));
            return Result::failure;
        }
        ZB_APP_LOG(k_tag,
                   "Subscribed HA temperature sensor device (0x%04x, 0x%02x) from local successfully",
                   sensor.short_addr, sensor.ep);

        ZB_APP_LOG(k_tag, "Attempt to configure reporting for HA temperature sensor");
        const int32_t report_status =
            zigbee_config_report(sensor.short_addr, sensor.ep, k_request_timeout_ms);
        if (report_status != 0) {
            ZB_APP_LOG(k_tag, "Failed to configure reporting (%ld)",
                       static_cast<long>(report_status));
            return Result::failure;
        }

        for (int i = 0; i < 5; ++i) {
            const int16_t last = zigbee_get_last_temp();
            ZB_APP_LOG(k_tag, "Temperature sensor measured value: %.2f degrees Celsius",
                       to_celsius(last));
            ZB_APP_SLEEP_MS(2000);
        }
    }

    const zigbee_temp_t attrs =
        zigbee_read_temp(sensor.short_addr, sensor.ep, k_request_timeout_ms);
    if (attrs.err != 0) {
        ZB_APP_LOG(k_tag, "Failed to read temperature attributes (%ld)",
                   static_cast<long>(attrs.err));
        return Result::failure;
    }
    print_temp_attrs(attrs);
    return Result::success;
}

} // namespace thermostat_app
