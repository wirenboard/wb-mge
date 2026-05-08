#include "port_manager.h"
#include "bridge.h"
#include "sniffer.h"
#include "cache_multimaster.h"
#include "cache_modbus_server.h"
#include "serial.h"
#include "setting_items.h"
#include "rs485_stats.h"
#include "auth.h"
#include "json_utils.h"

#include "esp_check.h"
#include "esp_log.h"
#include "cJSON.h"

#include <string.h>

static const char *TAG = "port_manager";

// Per-port runtime context.
typedef struct {
    pm_mode_t       mode;               // Currently active mode.
    serial_desc_t  *serial_desc;        // Non-NULL only for SNIFFER and CACHE_BUS modes.
                                        // For TCP_BRIDGE the serial_desc lives inside bridge_ctx.
    serial_config_t serial_cfg_at_init; // Serial config snapshot taken at port init time,
                                        // used to detect serial parameter changes for
                                        // SNIFFER and CACHE_BUS modes.
} pm_ctx_t;

static pm_ctx_t pm_ctx[BRIDGES_COUNT] = {0};

// ────────────────────────────────────────────────────────────────
// Helpers
// ────────────────────────────────────────────────────────────────

const char *port_manager_mode_to_str(pm_mode_t mode)
{
    switch (mode) {
    case PM_MODE_DISABLED:   return PORT_MODE_DISABLED_STR;
    case PM_MODE_TCP_BRIDGE: return PORT_MODE_TCP_BRIDGE_STR;
    case PM_MODE_SNIFFER:    return PORT_MODE_SNIFFER_STR;
    case PM_MODE_CACHE_BUS:  return PORT_MODE_CACHE_BUS_STR;
    default:                 return "unknown";
    }
}

static pm_mode_t str_to_pm_mode(const char *str)
{
    if (!str) {
        return PM_MODE_DISABLED;
    }
    if (strncmp(str, PORT_MODE_TCP_BRIDGE_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return PM_MODE_TCP_BRIDGE;
    }
    if (strncmp(str, PORT_MODE_SNIFFER_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return PM_MODE_SNIFFER;
    }
    if (strncmp(str, PORT_MODE_CACHE_BUS_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return PM_MODE_CACHE_BUS;
    }
    return PM_MODE_DISABLED;
}

// Return the NVS key for the port mode setting (port_mode_1 / port_mode_2).
static const char *port_mode_nvs_key(unsigned index)
{
    static const char *keys[BRIDGES_COUNT] = {KEY_PORT_MODE1, KEY_PORT_MODE2};
    if (index >= BRIDGES_COUNT) {
        return KEY_PORT_MODE1;
    }
    return keys[index];
}

// Read the port mode from NVS for the given port index.
static pm_mode_t read_port_mode_from_nvs(unsigned index)
{
    char value[SETTING_ITEM_MAX_STR_LEN] = {0};
    esp_err_t ret = setting_items_read(port_mode_nvs_key(index), value);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Port[%u]: Failed to read port mode from NVS, defaulting to disabled", index + 1);
        return PM_MODE_DISABLED;
    }
    return str_to_pm_mode(value);
}

// ────────────────────────────────────────────────────────────────
// Per-port init / deinit
// ────────────────────────────────────────────────────────────────

static esp_err_t port_init_mode(unsigned index, pm_mode_t mode)
{
    ESP_LOGI(TAG, "Port[%u]: Initializing mode '%s'", index + 1, port_manager_mode_to_str(mode));

    switch (mode) {
    case PM_MODE_DISABLED:
        // Nothing to do; serial port stays closed.
        break;

    case PM_MODE_TCP_BRIDGE:
        // bridge_port_init() opens serial + starts TCP subsystem.
        ESP_RETURN_ON_ERROR(bridge_port_init(index),
                            TAG, "Port[%u]: bridge_port_init failed", index + 1);
        // Attach sniffer so that WebSocket clients can passively observe traffic
        // when they connect.  sniffer_enable() is NOT called here — the WS
        // connection handler does that on demand.
        {
            serial_desc_t *sd = bridge_get_serial_desc(index);
            if (sd) {
                sniffer_attach(index, sd);
            } else {
                ESP_LOGW(TAG, "Port[%u]: TCP bridge has no serial_desc (inner bridge_mode may be disabled), sniffer not attached", index + 1);
            }
        }
        break;

    case PM_MODE_SNIFFER:
        // Open serial-only (no TCP layer).
        ESP_RETURN_ON_ERROR(bridge_port_init_serial_only(index, &pm_ctx[index].serial_desc),
                            TAG, "Port[%u]: bridge_port_init_serial_only failed", index + 1);
        // Tighter inter-character timeout for Modbus packet boundary detection.
        serial_set_rx_timeout(pm_ctx[index].serial_desc, SERIAL_RX_TOUT_SNIFFER);
        sniffer_attach(index, pm_ctx[index].serial_desc);
        sniffer_enable(index);
        // Save the serial config used at init so we can detect changes later.
        bridge_read_serial_config(index, &pm_ctx[index].serial_cfg_at_init);
        break;

    case PM_MODE_CACHE_BUS:
        // Open serial-only (no TCP layer).
        ESP_RETURN_ON_ERROR(bridge_port_init_serial_only(index, &pm_ctx[index].serial_desc),
                            TAG, "Port[%u]: bridge_port_init_serial_only failed", index + 1);
        serial_set_rx_timeout(pm_ctx[index].serial_desc, SERIAL_RX_TOUT_SNIFFER);
        sniffer_attach(index, pm_ctx[index].serial_desc);
        sniffer_enable(index);
        // NOTE: cache_multimaster is a global resource.  If both ports are in
        // CACHE_BUS mode, enable() is called twice — this is intentional and
        // idempotent.  However, disable() on deinit of one port will also
        // disable the cache for the other port (current architectural limitation).
        cache_multimaster_enable();
        sniffer_set_cache_active(true);
        // Save the serial config used at init so we can detect changes later.
        bridge_read_serial_config(index, &pm_ctx[index].serial_cfg_at_init);
        break;

    default:
        ESP_LOGE(TAG, "Port[%u]: Unknown mode %d", index + 1, (int)mode);
        return ESP_ERR_INVALID_ARG;
    }

    pm_ctx[index].mode = mode;
    return ESP_OK;
}

static void port_deinit_mode(unsigned index)
{
    pm_mode_t mode = pm_ctx[index].mode;
    ESP_LOGI(TAG, "Port[%u]: Deinitializing mode '%s'", index + 1, port_manager_mode_to_str(mode));

    switch (mode) {
    case PM_MODE_DISABLED:
        // Nothing to deinit.
        break;

    case PM_MODE_TCP_BRIDGE:
        // Detach sniffer before the serial port is destroyed to prevent use-after-free.
        sniffer_detach(index);
        bridge_port_deinit(index);
        // bridge_port_deinit() clears bridge_ctx[index].serial_desc internally.
        rs485_busy_monitor_reset(index);
        rs485_stats_reset(index);
        break;

    case PM_MODE_SNIFFER:
        // sniffer_detach() disables and clears the sniff_handler pointer.
        sniffer_detach(index);
        serial_deinit(pm_ctx[index].serial_desc);
        pm_ctx[index].serial_desc = NULL;
        memset(&pm_ctx[index].serial_cfg_at_init, 0, sizeof(pm_ctx[index].serial_cfg_at_init));
        rs485_busy_monitor_reset(index);
        rs485_stats_reset(index);
        break;

    case PM_MODE_CACHE_BUS:
        sniffer_detach(index);
        serial_deinit(pm_ctx[index].serial_desc);
        pm_ctx[index].serial_desc = NULL;
        memset(&pm_ctx[index].serial_cfg_at_init, 0, sizeof(pm_ctx[index].serial_cfg_at_init));
        // Temporarily mark this port as disabled before counting active CACHE_BUS ports,
        // so the count reflects the state after this deinit completes.
        pm_ctx[index].mode = PM_MODE_DISABLED;
        {
            unsigned cache_bus_count = 0;
            for (unsigned i = 0; i < BRIDGES_COUNT; i++) {
                if (pm_ctx[i].mode == PM_MODE_CACHE_BUS) cache_bus_count++;
            }
            if (cache_bus_count == 0) {
                // Last CACHE_BUS port: safe to disable the global cache.
                cache_multimaster_disable();
                sniffer_set_cache_active(false);
            }
        }
        rs485_busy_monitor_reset(index);
        rs485_stats_reset(index);
        break;

    default:
        ESP_LOGW(TAG, "Port[%u]: Unknown mode %d during deinit — skipping", index + 1, (int)mode);
        break;
    }

    pm_ctx[index].mode = PM_MODE_DISABLED;
}

// ────────────────────────────────────────────────────────────────
// Public API
// ────────────────────────────────────────────────────────────────

esp_err_t port_manager_init(void)
{
    // Initialise shared RS-485 infrastructure (previously done inside bridge_init()).
    rs485_busy_monitor_init();
    rs485_stats_init();

    // Initialise global subsystems once.
    ESP_RETURN_ON_ERROR(sniffer_init(), TAG, "sniffer_init failed");
    ESP_RETURN_ON_ERROR(cache_multimaster_init(), TAG, "cache_multimaster_init failed");
    // cache_modbus_server is harmless without an active cache — start it unconditionally.
    // Read TCP port from NVS; fall back to compile-time default if unset.
    int cache_port = setting_items_read_int(KEY_CACHE_MODBUS_PORT);
    if (cache_port <= 0) cache_port = CACHE_MODBUS_SERVER_PORT;
    ESP_RETURN_ON_ERROR(cache_modbus_server_init(cache_port),
                        TAG, "cache_modbus_server_init failed");

    // Bring up each port in the mode stored in NVS.
    for (unsigned i = 0; i < BRIDGES_COUNT; i++) {
        pm_mode_t mode = read_port_mode_from_nvs(i);
        esp_err_t ret = port_init_mode(i, mode);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Port[%u]: Initialization failed (mode '%s'): %s",
                     i + 1, port_manager_mode_to_str(mode), esp_err_to_name(ret));
            // Continue with remaining ports rather than aborting.
        }
    }

    ESP_LOGI(TAG, "Port manager initialized");
    return ESP_OK;
}

pm_mode_t port_manager_get_mode(unsigned port_index)
{
    if (port_index >= BRIDGES_COUNT) {
        return PM_MODE_DISABLED;
    }
    return pm_ctx[port_index].mode;
}

esp_err_t port_manager_set_mode(unsigned port_index, pm_mode_t mode)
{
    if (port_index >= BRIDGES_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    port_deinit_mode(port_index);

    // Persist the new mode.
    esp_err_t ret = setting_items_save(port_mode_nvs_key(port_index),
                                       port_manager_mode_to_str(mode));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Port[%u]: Failed to save port mode to NVS: %s",
                 port_index + 1, esp_err_to_name(ret));
        // Proceed with init anyway; mode is lost on reboot but at least works now.
    }

    return port_init_mode(port_index, mode);
}

esp_err_t port_manager_apply_settings(unsigned port_index)
{
    if (port_index >= BRIDGES_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    port_deinit_mode(port_index);

    // Re-read the mode from NVS (it may have been changed externally).
    pm_mode_t mode = read_port_mode_from_nvs(port_index);
    return port_init_mode(port_index, mode);
}

bool port_manager_check_settings_changed(unsigned port_index)
{
    if (port_index >= BRIDGES_COUNT) {
        return false;
    }

    pm_mode_t current_mode = pm_ctx[port_index].mode;

    // Always check whether the port mode itself has changed.
    pm_mode_t nvs_mode = read_port_mode_from_nvs(port_index);
    if (nvs_mode != current_mode) {
        ESP_LOGD(TAG, "Port[%u]: Mode changed from '%s' to '%s'",
                 port_index + 1,
                 port_manager_mode_to_str(current_mode),
                 port_manager_mode_to_str(nvs_mode));
        return true;
    }

    // For TCP_BRIDGE delegate to the bridge module which compares all TCP/serial params.
    if (current_mode == PM_MODE_TCP_BRIDGE) {
        return bridge_port_check_settings_changed(port_index);
    }

    // For SNIFFER and CACHE_BUS compare only the serial parameters.
    // bridge_port_check_settings_changed() must NOT be used here because
    // bridge_ctx[index].initialized is always false for these modes, which
    // causes that function to return incorrect results.
    if (current_mode == PM_MODE_SNIFFER || current_mode == PM_MODE_CACHE_BUS) {
        serial_config_t nvs_cfg = {0};
        // If reading fails, assume changed to trigger re-init.
        if (bridge_read_serial_config(port_index, &nvs_cfg) != ESP_OK) {
            return true;
        }
        return memcmp(&pm_ctx[port_index].serial_cfg_at_init, &nvs_cfg, sizeof(serial_config_t)) != 0;
    }

    // PM_MODE_DISABLED — nothing to check.
    return false;
}

// ────────────────────────────────────────────────────────────────
// HTTP handlers
// ────────────────────────────────────────────────────────────────

static esp_err_t port_set_mode_handler(httpd_req_t *req, unsigned port_index)
{
    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }

    cJSON *req_json = json_utils_receive_json(req);
    if (!req_json) {
        return json_utils_send_error(req, "Invalid JSON");
    }

    cJSON *mode_item = cJSON_GetObjectItem(req_json, "mode");
    if (!mode_item || !cJSON_IsString(mode_item)) {
        cJSON_Delete(req_json);
        return json_utils_send_error(req, "Missing or invalid 'mode' field");
    }

    pm_mode_t new_mode = str_to_pm_mode(mode_item->valuestring);

    // Reject unknown mode strings (str_to_pm_mode maps unknown → disabled,
    // but the caller might have sent a genuinely invalid string).
    if (new_mode == PM_MODE_DISABLED &&
        strncmp(mode_item->valuestring, PORT_MODE_DISABLED_STR, SETTING_ITEM_MAX_STR_LEN) != 0) {
        cJSON_Delete(req_json);
        return json_utils_send_error(req, "Unknown mode value");
    }

    esp_err_t ret = port_manager_set_mode(port_index, new_mode);
    if (ret != ESP_OK) {
        cJSON_Delete(req_json);
        return json_utils_send_error(req, esp_err_to_name(ret));
    }

    cJSON *resp = cJSON_CreateObject();
    if (resp) {
        cJSON_AddStringToObject(resp, "mode", port_manager_mode_to_str(new_mode));
    }
    json_utils_send_response(req, req_json, resp);
    return ESP_OK;
}

// User-facing port numbers are 1-based (Port 1, Port 2).
// Handlers convert to 0-based index before calling port_manager_set_mode().
static esp_err_t port1_set_mode_handler(httpd_req_t *req)
{
    return port_set_mode_handler(req, 0);
}

static esp_err_t port2_set_mode_handler(httpd_req_t *req)
{
    return port_set_mode_handler(req, 1);
}

static const httpd_uri_t uri_port1_mode = {
    .uri     = "/ports/1/mode",
    .method  = HTTP_POST,
    .handler = port1_set_mode_handler,
};

static const httpd_uri_t uri_port2_mode = {
    .uri     = "/ports/2/mode",
    .method  = HTTP_POST,
    .handler = port2_set_mode_handler,
};

esp_err_t port_manager_register_handlers(httpd_handle_t server)
{
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &uri_port1_mode),
                        TAG, "Failed to register POST /ports/1/mode");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &uri_port2_mode),
                        TAG, "Failed to register POST /ports/2/mode");

    ESP_LOGI(TAG, "HTTP handlers registered");
    return ESP_OK;
}
