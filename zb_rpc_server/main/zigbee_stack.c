/*
 * SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * Copied from thermostat/ and adapted for RPC: Zigbee stays on-chip, but
 * join/bind/temp application flow is driven by the Linux client.
 */

#include "zigbee_api.h"

#include <string.h>

#include "alarm_timer.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_zigbee.h"
#include "ezbee/zha.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "thermostat.h"

static const char *TAG = "ZB_API";

static QueueHandle_t s_annce_q;
static QueueHandle_t s_match_q;
static SemaphoreHandle_t s_basic_done;
static SemaphoreHandle_t s_temp_done;
static SemaphoreHandle_t s_bind_done;
static SemaphoreHandle_t s_report_done;

static zigbee_basic_t s_basic_cache;
static zigbee_temp_t s_temp_cache;
static int16_t s_last_temp;
static int32_t s_bind_err;
static int32_t s_report_err;

static void copy_zcl_string(char *dst, size_t dst_size, const uint8_t *zcl)
{
    if (dst_size == 0) {
        return;
    }
    memset(dst, 0, dst_size);
    if (!zcl) {
        return;
    }
    uint8_t length = zcl[0];
    if (length >= dst_size) {
        length = (uint8_t)(dst_size - 1);
    }
    memcpy(dst, zcl + 1, length);
}

static void zigbee_alarm_bdb_commissioning(alarm_timer_arg_t arg)
{
    esp_zigbee_lock_acquire(portMAX_DELAY);
    (void)ezb_bdb_start_top_level_commissioning(arg);
    esp_zigbee_lock_release();
}

static bool zigbee_app_signal_handler(const ezb_app_signal_t *app_signal)
{
    ezb_app_signal_type_t signal_type = ezb_app_signal_get_type(app_signal);

    switch (signal_type) {
    case EZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Initialize Zigbee stack");
        ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_INITIALIZATION);
        break;
    case EZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case EZB_BDB_SIGNAL_DEVICE_REBOOT: {
        ezb_bdb_comm_status_t status = *((ezb_bdb_comm_status_t *)ezb_app_signal_get_params(app_signal));
        if (status == EZB_BDB_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Device started up in%s factory-reset mode",
                     ezb_bdb_is_factory_new() ? "" : " non");
            if (ezb_bdb_is_factory_new()) {
                ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_FORMATION);
            } else {
                ESP_LOGI(TAG, "Device reboot; waiting for RPC OPEN_NETWORK");
            }
        } else {
            ESP_LOGW(TAG, "The %s failed with status(0x%02x), please retry",
                     ezb_app_signal_to_string(signal_type), status);
            alarm_timer_schedule(zigbee_alarm_bdb_commissioning, EZB_BDB_MODE_INITIALIZATION, 1000);
        }
    } break;
    case EZB_BDB_SIGNAL_FORMATION: {
        ezb_bdb_comm_status_t status = *((ezb_bdb_comm_status_t *)ezb_app_signal_get_params(app_signal));
        if (status == EZB_BDB_STATUS_SUCCESS) {
            ezb_extpanid_t extended_pan_id;
            ezb_nwk_get_extended_panid(&extended_pan_id);
            ESP_LOGI(TAG,
                     "Formed network successfully: PAN ID(0x%04hx, EXT: 0x%llx), Channel(%d), "
                     "Short Address(0x%04hx)",
                     ezb_nwk_get_panid(), extended_pan_id.u64, ezb_nwk_get_current_channel(),
                     ezb_nwk_get_short_address());
            ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
        } else {
            ESP_LOGW(TAG, "Failed to form network with status(0x%02x)", status);
            alarm_timer_schedule(zigbee_alarm_bdb_commissioning, EZB_BDB_MODE_NETWORK_FORMATION, 1000);
        }
    } break;
    case EZB_BDB_SIGNAL_STEERING: {
        ezb_bdb_comm_status_t status = *((ezb_bdb_comm_status_t *)ezb_app_signal_get_params(app_signal));
        if (status == EZB_BDB_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Network steering completed");
        } else {
            ESP_LOGW(TAG, "Failed to steering network with status(0x%02x)", status);
            alarm_timer_schedule(zigbee_alarm_bdb_commissioning, EZB_BDB_MODE_NETWORK_FORMATION, 1000);
        }
    } break;
    case EZB_ZDO_SIGNAL_DEVICE_ANNCE: {
        const ezb_zdo_signal_device_annce_params_t *dev_annce_params =
            ezb_app_signal_get_params(app_signal);
        ESP_LOGI(TAG, "New device commissioned or rejoined (short: 0x%04hx)",
                 dev_annce_params->short_addr);
        if (s_annce_q) {
            uint16_t short_addr = dev_annce_params->short_addr;
            (void)xQueueSend(s_annce_q, &short_addr, 0);
        }
    } break;
    case EZB_NWK_SIGNAL_PERMIT_JOIN_STATUS: {
        uint8_t duration = *(uint8_t *)ezb_app_signal_get_params(app_signal);
        if (duration) {
            ESP_LOGI(TAG, "Network(0x%04hx) is open for %d seconds", ezb_nwk_get_panid(), duration);
        } else {
            ESP_LOGW(TAG, "Network(0x%04hx) closed, devices joining not allowed.",
                     ezb_nwk_get_panid());
        }
    } break;
    default:
        ESP_LOGI(TAG, "Zigbee APP Signal: %s(type: 0x%02x)", ezb_app_signal_to_string(signal_type),
                 signal_type);
        break;
    }
    return true;
}

static void zdo_match_result(const ezb_zdo_match_desc_req_result_t *result, void *user_ctx)
{
    (void)user_ctx;
    zigbee_addr_t out = {.err = -1};
    if (result && result->error == EZB_ERR_NONE && result->rsp &&
        result->rsp->status == EZB_ZDP_STATUS_SUCCESS && result->rsp->match_length > 0 &&
        result->rsp->match_list) {
        out.err = 0;
        out.short_addr = result->rsp->nwk_addr_of_interest;
        out.ep = result->rsp->match_list[0];
    } else if (result) {
        out.err = (int32_t)result->error;
    }
    if (s_match_q) {
        (void)xQueueSend(s_match_q, &out, 0);
    }
}

static void zdo_bind_result(const ezb_zdp_bind_req_result_t *result, void *user_ctx)
{
    ezb_zdo_bind_req_t *bind_req = (ezb_zdo_bind_req_t *)user_ctx;
    s_bind_err = -1;
    if (result && result->error == EZB_ERR_NONE && result->rsp &&
        result->rsp->status == EZB_ZDP_STATUS_SUCCESS) {
        s_bind_err = 0;
    } else if (result) {
        s_bind_err = result->error == EZB_ERR_NONE ? (int32_t)result->rsp->status
                                                   : (int32_t)result->error;
    }
    if (s_bind_done) {
        xSemaphoreGive(s_bind_done);
    }
    free(bind_req);
}

static void zcl_read_attr_rsp(ezb_zcl_cmd_read_attr_rsp_message_t *message)
{
    if (!message) {
        return;
    }
    ESP_LOGI(TAG, "ZCL Read Attribute Response cluster(0x%04x) status(0x%02x)",
             message->info.cluster_id, message->info.status);

    if (message->info.cluster_id == EZB_ZCL_CLUSTER_ID_BASIC) {
        memset(&s_basic_cache, 0, sizeof(s_basic_cache));
        s_basic_cache.err = message->info.status == EZB_ZCL_STATUS_SUCCESS ? 0 : -1;
        for (ezb_zcl_read_attr_rsp_variable_t *var = message->in.variables; var; var = var->next) {
            if (var->status != EZB_ZCL_STATUS_SUCCESS) {
                continue;
            }
            if (var->attr_id == EZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID) {
                copy_zcl_string(s_basic_cache.manufacturer, sizeof(s_basic_cache.manufacturer),
                                var->attr_value);
            } else if (var->attr_id == EZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID) {
                copy_zcl_string(s_basic_cache.model, sizeof(s_basic_cache.model), var->attr_value);
            }
        }
        if (s_basic_done) {
            xSemaphoreGive(s_basic_done);
        }
        return;
    }

    if (message->info.cluster_id == EZB_ZCL_CLUSTER_ID_TEMPERATURE_MEASUREMENT) {
        memset(&s_temp_cache, 0, sizeof(s_temp_cache));
        s_temp_cache.err = message->info.status == EZB_ZCL_STATUS_SUCCESS ? 0 : -1;
        for (ezb_zcl_read_attr_rsp_variable_t *var = message->in.variables; var; var = var->next) {
            if (var->status != EZB_ZCL_STATUS_SUCCESS) {
                continue;
            }
            int16_t value = *(int16_t *)var->attr_value;
            switch (var->attr_id) {
            case EZB_ZCL_ATTR_TEMPERATURE_MEASUREMENT_MEASURED_VALUE_ID:
                s_temp_cache.measured = value;
                s_last_temp = value;
                break;
            case EZB_ZCL_ATTR_TEMPERATURE_MEASUREMENT_MIN_MEASURED_VALUE_ID:
                s_temp_cache.min_measured = value;
                break;
            case EZB_ZCL_ATTR_TEMPERATURE_MEASUREMENT_MAX_MEASURED_VALUE_ID:
                s_temp_cache.max_measured = value;
                break;
            case EZB_ZCL_ATTR_TEMPERATURE_MEASUREMENT_TOLERANCE_ID:
                s_temp_cache.tolerance = value;
                break;
            default:
                break;
            }
        }
        if (s_temp_done) {
            xSemaphoreGive(s_temp_done);
        }
    }
}

static void zcl_report_attr(ezb_zcl_cmd_report_attr_message_t *message)
{
    if (!message || message->info.cluster_id != EZB_ZCL_CLUSTER_ID_TEMPERATURE_MEASUREMENT) {
        return;
    }
    for (ezb_zcl_report_attr_variable_t *var = message->in.variables; var; var = var->next) {
        if (var->attr_id == EZB_ZCL_ATTR_TEMPERATURE_MEASUREMENT_MEASURED_VALUE_ID) {
            s_last_temp = *(int16_t *)var->attr_value;
            ESP_LOGI(TAG, "Temperature sensor measured value: %.2f C", s_last_temp / 100.0f);
        }
    }
}

static void zcl_config_report_rsp(ezb_zcl_cmd_config_report_rsp_message_t *message)
{
    s_report_err = -1;
    if (message && message->info.status == EZB_ZCL_STATUS_SUCCESS) {
        s_report_err = 0;
    }
    if (s_report_done) {
        xSemaphoreGive(s_report_done);
    }
}

static void zigbee_zcl_action_handler(ezb_zcl_core_action_callback_id_t callback_id, void *message)
{
    switch (callback_id) {
    case EZB_ZCL_CORE_READ_ATTR_RSP_CB_ID:
        zcl_read_attr_rsp(message);
        break;
    case EZB_ZCL_CORE_CONFIG_REPORT_RSP_CB_ID:
        zcl_config_report_rsp(message);
        break;
    case EZB_ZCL_CORE_REPORT_ATTR_CB_ID:
        zcl_report_attr(message);
        break;
    default:
        break;
    }
}

static esp_err_t zigbee_create_device(void)
{
    ezb_af_device_desc_t dev_desc = ezb_af_create_device_desc();
    ezb_zha_thermostat_config_t thermostat_cfg = EZB_ZHA_THERMOSTAT_CONFIG();
    ezb_af_ep_desc_t ep_desc =
        ezb_zha_create_thermostat(ESP_ZIGBEE_HA_THERMOSTAT_EP_ID, &thermostat_cfg);
    ezb_zcl_cluster_desc_t basic_desc = {0};

    basic_desc = ezb_af_endpoint_get_cluster_desc(ep_desc, EZB_ZCL_CLUSTER_ID_BASIC, EZB_ZCL_CLUSTER_SERVER);
    ezb_zcl_basic_cluster_desc_add_attr(basic_desc, EZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,
                                        (void *)ESP_MANUFACTURER_NAME);
    ezb_zcl_basic_cluster_desc_add_attr(basic_desc, EZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,
                                        (void *)ESP_MODEL_IDENTIFIER);
    ESP_ERROR_CHECK(ezb_af_endpoint_add_cluster_desc(
        ep_desc, ezb_zcl_basic_create_cluster_desc(NULL, EZB_ZCL_CLUSTER_CLIENT)));
    ESP_ERROR_CHECK(ezb_af_endpoint_add_cluster_desc(
        ep_desc, ezb_zcl_temperature_measurement_create_cluster_desc(NULL, EZB_ZCL_CLUSTER_CLIENT)));
    ESP_ERROR_CHECK(ezb_af_device_add_endpoint_desc(dev_desc, ep_desc));
    ESP_ERROR_CHECK(ezb_af_device_desc_register(dev_desc));
    ezb_zcl_core_action_handler_register(zigbee_zcl_action_handler);
    return ESP_OK;
}

static void zigbee_main_task(void *arg)
{
    (void)arg;
    esp_zigbee_config_t config = ESP_ZIGBEE_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(esp_zigbee_init(&config));
    ezb_aps_secur_enable_distributed_security(false);
    ESP_ERROR_CHECK(ezb_bdb_set_primary_channel_set(ESP_ZIGBEE_PRIMARY_CHANNEL_MASK));
    ESP_ERROR_CHECK(ezb_bdb_set_secondary_channel_set(ESP_ZIGBEE_SECONDARY_CHANNEL_MASK));
    ESP_ERROR_CHECK(ezb_app_signal_add_handler(zigbee_app_signal_handler));
    ESP_ERROR_CHECK(zigbee_create_device());
    ESP_ERROR_CHECK(esp_zigbee_start(false));
    esp_zigbee_launch_mainloop();
    esp_zigbee_deinit();
    vTaskDelete(NULL);
}

esp_err_t zigbee_stack_start(void)
{
    s_annce_q = xQueueCreate(4, sizeof(uint16_t));
    s_match_q = xQueueCreate(1, sizeof(zigbee_addr_t));
    s_basic_done = xSemaphoreCreateBinary();
    s_temp_done = xSemaphoreCreateBinary();
    s_bind_done = xSemaphoreCreateBinary();
    s_report_done = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_annce_q && s_match_q && s_basic_done && s_temp_done && s_bind_done &&
                            s_report_done,
                        ESP_ERR_NO_MEM, TAG, "queue/sem alloc failed");

    ESP_LOGI(TAG, "Start ESP Zigbee Stack");
    BaseType_t ok = xTaskCreate(zigbee_main_task, "Zigbee_main", 4096, NULL, 5, NULL);
    return ok == pdPASS ? ESP_OK : ESP_FAIL;
}

uint16_t zigbee_get_short_addr(void)
{
    esp_zigbee_lock_acquire(portMAX_DELAY);
    uint16_t value = ezb_nwk_get_short_address();
    esp_zigbee_lock_release();
    return value;
}

uint16_t zigbee_get_panid(void)
{
    esp_zigbee_lock_acquire(portMAX_DELAY);
    uint16_t value = ezb_nwk_get_panid();
    esp_zigbee_lock_release();
    return value;
}

uint8_t zigbee_get_channel(void)
{
    esp_zigbee_lock_acquire(portMAX_DELAY);
    uint8_t value = ezb_nwk_get_current_channel();
    esp_zigbee_lock_release();
    return value;
}

int32_t zigbee_open_network(uint8_t duration)
{
    esp_zigbee_lock_acquire(portMAX_DELAY);
    ezb_err_t err = ezb_bdb_open_network(duration);
    esp_zigbee_lock_release();
    return err == EZB_ERR_NONE ? 0 : (int32_t)err;
}

uint16_t zigbee_wait_annce(uint32_t timeout_ms)
{
    uint16_t short_addr = 0;
    if (xQueueReceive(s_annce_q, &short_addr, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return 0;
    }
    return short_addr;
}

zigbee_addr_t zigbee_find_sensor(uint16_t short_addr, uint32_t timeout_ms)
{
    zigbee_addr_t out = {.err = -1};
    uint16_t cluster_list[1] = {EZB_ZCL_CLUSTER_ID_TEMPERATURE_MEASUREMENT};
    ezb_zdo_match_desc_req_t req = {
        .dst_nwk_addr = short_addr,
        .field =
            {
                .nwk_addr_of_interest = short_addr,
                .profile_id = EZB_AF_HA_PROFILE_ID,
                .num_in_clusters = 1,
                .num_out_clusters = 0,
                .cluster_list = cluster_list,
            },
        .cb = zdo_match_result,
        .user_ctx = NULL,
    };

    xQueueReset(s_match_q);
    esp_zigbee_lock_acquire(portMAX_DELAY);
    ezb_err_t err = ezb_zdo_match_desc_req(&req);
    esp_zigbee_lock_release();
    if (err != EZB_ERR_NONE) {
        out.err = (int32_t)err;
        return out;
    }
    if (xQueueReceive(s_match_q, &out, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        out.err = -2;
    }
    return out;
}

zigbee_basic_t zigbee_read_basic(uint16_t short_addr, uint8_t ep, uint32_t timeout_ms)
{
    zigbee_basic_t out = {.err = -1};
    uint16_t attr_field[] = {EZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,
                             EZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID};
    ezb_zcl_read_attr_cmd_t read_attr_cmd = {
        .cmd_ctrl =
            {
                .dst_addr.addr_mode = EZB_ADDR_MODE_SHORT,
                .src_ep = ESP_ZIGBEE_HA_THERMOSTAT_EP_ID,
                .dst_addr.u.short_addr = short_addr,
                .dst_ep = ep,
                .cluster_id = EZB_ZCL_CLUSTER_ID_BASIC,
            },
        .payload.attr_number = sizeof(attr_field) / sizeof(attr_field[0]),
        .payload.attr_field = attr_field,
    };

    xSemaphoreTake(s_basic_done, 0);
    esp_zigbee_lock_acquire(portMAX_DELAY);
    ezb_err_t err = ezb_zcl_read_attr_cmd_req(&read_attr_cmd);
    esp_zigbee_lock_release();
    if (err != EZB_ERR_NONE) {
        out.err = (int32_t)err;
        return out;
    }
    if (xSemaphoreTake(s_basic_done, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        out.err = -2;
        return out;
    }
    return s_basic_cache;
}

static int32_t zigbee_bind_common(uint16_t remote_short, uint8_t remote_ep, bool to_local,
                                  uint32_t timeout_ms)
{
    ezb_zdo_bind_req_t *bind_req = calloc(1, sizeof(*bind_req));
    if (!bind_req) {
        return -1;
    }

    xSemaphoreTake(s_bind_done, 0);

    esp_zigbee_lock_acquire(portMAX_DELAY);
    if (to_local) {
        bind_req->dst_nwk_addr = ezb_nwk_get_short_address();
        bind_req->field.src_ep = ESP_ZIGBEE_HA_THERMOSTAT_EP_ID;
        bind_req->field.dst_ep = remote_ep;
        ezb_nwk_get_extended_address(&bind_req->field.src_addr);
        if (ezb_address_extended_by_short(remote_short, &bind_req->field.dst_addr.extended_addr) !=
            EZB_ERR_NONE) {
            esp_zigbee_lock_release();
            free(bind_req);
            return -1;
        }
    } else {
        bind_req->dst_nwk_addr = remote_short;
        bind_req->field.src_ep = remote_ep;
        bind_req->field.dst_ep = ESP_ZIGBEE_HA_THERMOSTAT_EP_ID;
        ezb_nwk_get_extended_address(&bind_req->field.dst_addr.extended_addr);
        if (ezb_address_extended_by_short(remote_short, &bind_req->field.src_addr) != EZB_ERR_NONE) {
            esp_zigbee_lock_release();
            free(bind_req);
            return -1;
        }
    }

    bind_req->field.cluster_id = EZB_ZCL_CLUSTER_ID_TEMPERATURE_MEASUREMENT;
    bind_req->field.dst_addr_mode = EZB_ADDR_MODE_EXT;
    bind_req->cb = zdo_bind_result;
    bind_req->user_ctx = bind_req;

    ezb_err_t err = ezb_zdo_bind_req(bind_req);
    esp_zigbee_lock_release();
    if (err != EZB_ERR_NONE) {
        free(bind_req);
        return (int32_t)err;
    }
    if (xSemaphoreTake(s_bind_done, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return -2;
    }
    return s_bind_err;
}

int32_t zigbee_bind_sensor(uint16_t short_addr, uint8_t ep, uint32_t timeout_ms)
{
    return zigbee_bind_common(short_addr, ep, true, timeout_ms);
}

int32_t zigbee_subscribe_sensor(uint16_t short_addr, uint8_t ep, uint32_t timeout_ms)
{
    return zigbee_bind_common(short_addr, ep, false, timeout_ms);
}

int32_t zigbee_config_report(uint16_t short_addr, uint8_t ep, uint32_t timeout_ms)
{
    (void)short_addr;
    (void)ep;
    ezb_zcl_config_report_record_t record[] = {
        {
            .direction = EZB_ZCL_REPORTING_SEND,
            .attr_id = EZB_ZCL_ATTR_TEMPERATURE_MEASUREMENT_MEASURED_VALUE_ID,
            .client =
                {
                    .attr_type = EZB_ZCL_ATTR_TYPE_INT16,
                    .min_interval = 1,
                    .max_interval = 10,
                    .reportable_change = {.s16 = 200},
                },
        },
    };
    ezb_zcl_config_report_cmd_t config_report_cmd = {
        .cmd_ctrl =
            {
                .dst_addr.addr_mode = EZB_ADDR_MODE_NONE,
                .src_ep = ESP_ZIGBEE_HA_THERMOSTAT_EP_ID,
                .cluster_id = EZB_ZCL_CLUSTER_ID_TEMPERATURE_MEASUREMENT,
            },
        .payload.record_number = sizeof(record) / sizeof(record[0]),
        .payload.record_field = record,
    };

    xSemaphoreTake(s_report_done, 0);
    esp_zigbee_lock_acquire(portMAX_DELAY);
    ezb_err_t err = ezb_zcl_config_report_cmd_req(&config_report_cmd);
    esp_zigbee_lock_release();
    if (err != EZB_ERR_NONE) {
        return (int32_t)err;
    }
    if (xSemaphoreTake(s_report_done, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return -2;
    }
    return s_report_err;
}

zigbee_temp_t zigbee_read_temp(uint16_t short_addr, uint8_t ep, uint32_t timeout_ms)
{
    zigbee_temp_t out = {.err = -1};
    uint16_t attr_field[] = {
        EZB_ZCL_ATTR_TEMPERATURE_MEASUREMENT_MEASURED_VALUE_ID,
        EZB_ZCL_ATTR_TEMPERATURE_MEASUREMENT_MIN_MEASURED_VALUE_ID,
        EZB_ZCL_ATTR_TEMPERATURE_MEASUREMENT_MAX_MEASURED_VALUE_ID,
        EZB_ZCL_ATTR_TEMPERATURE_MEASUREMENT_TOLERANCE_ID};
    ezb_zcl_read_attr_cmd_t read_attr_cmd = {
        .cmd_ctrl =
            {
                .dst_addr.addr_mode = EZB_ADDR_MODE_SHORT,
                .src_ep = ESP_ZIGBEE_HA_THERMOSTAT_EP_ID,
                .dst_addr.u.short_addr = short_addr,
                .dst_ep = ep,
                .cluster_id = EZB_ZCL_CLUSTER_ID_TEMPERATURE_MEASUREMENT,
            },
        .payload.attr_number = sizeof(attr_field) / sizeof(attr_field[0]),
        .payload.attr_field = attr_field,
    };

    xSemaphoreTake(s_temp_done, 0);
    esp_zigbee_lock_acquire(portMAX_DELAY);
    ezb_err_t err = ezb_zcl_read_attr_cmd_req(&read_attr_cmd);
    esp_zigbee_lock_release();
    if (err != EZB_ERR_NONE) {
        out.err = (int32_t)err;
        return out;
    }
    if (xSemaphoreTake(s_temp_done, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        out.err = -2;
        return out;
    }
    return s_temp_cache;
}

int16_t zigbee_get_last_temp(void)
{
    return s_last_temp;
}
