#include "mqtt_manager.h"
#include "setting_items.h"

#include "esp_log.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "mqtt_manager";

static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static bool s_connected = false;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected");
        s_connected = true;
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected");
        s_connected = false;
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        s_connected = false;
        break;
    default:
        break;
    }
}

esp_err_t mqtt_manager_init(void)
{
    if (!setting_items_read_bool(KEY_MQTT_ENABLED)) {
        ESP_LOGI(TAG, "MQTT disabled, skipping init");
        return ESP_OK;
    }

    char host[SETTING_ITEM_MAX_STR_LEN] = {0};
    char user[SETTING_ITEM_MAX_STR_LEN] = {0};
    char pass[SETTING_ITEM_MAX_STR_LEN] = {0};

    setting_items_read(KEY_MQTT_HOST, host);
    setting_items_read(KEY_MQTT_USER, user);
    setting_items_read(KEY_MQTT_PASS, pass);
    int port = setting_items_read_int(KEY_MQTT_PORT);

    if (host[0] == '\0') {
        ESP_LOGW(TAG, "MQTT host not configured");
        return ESP_OK;
    }

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.hostname = host,
        .broker.address.port = (uint32_t)port,
        .broker.address.transport = MQTT_TRANSPORT_OVER_TCP,
        .credentials.username = user[0] ? user : NULL,
        .credentials.authentication.password = pass[0] ? pass : NULL,
    };

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_mqtt_client) {
        ESP_LOGE(TAG, "Failed to init MQTT client");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_err_t ret = esp_mqtt_client_start(s_mqtt_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "MQTT client started, connecting to %s:%d", host, port);
    return ESP_OK;
}

void mqtt_manager_restart(void)
{
    if (s_mqtt_client) {
        esp_mqtt_client_stop(s_mqtt_client);
        esp_mqtt_client_destroy(s_mqtt_client);
        s_mqtt_client = NULL;
        s_connected = false;
    }
    mqtt_manager_init();
}

bool mqtt_manager_is_connected(void)
{
    return s_connected;
}
