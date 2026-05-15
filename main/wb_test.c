#include "esp_err.h"
#include <esp_http_server.h>
#include "driver/ledc.h"
#include "auth.h"
#include "json_utils.h"
#include "bridge/port_manager.h"
#include "esp_log.h"


#define BRIDGE_PORT_INDEX       0

#define CLK_OUT_PIN             GPIO_NUM_10
#define CLK_OUT_FREQ_HZ         100000
#define CLK_OUT_PWM_CHANNEL     LEDC_CHANNEL_0
#define CLK_OUT_PWM_TIMER       LEDC_TIMER_0

#define CLK_OUT_EN_PIN          GPIO_NUM_4

#define CLK_OUT_JSON_FIELD      "clock_out"


static bool clock_out_en = false;
static pm_mode_t s_saved_port_mode = PM_MODE_TCP_BRIDGE;

static ledc_timer_config_t timer_config = {
    .speed_mode = LEDC_HIGH_SPEED_MODE,
    .duty_resolution = 1,
    .timer_num = CLK_OUT_PWM_TIMER,
    .freq_hz = CLK_OUT_FREQ_HZ,
    .clk_cfg = LEDC_USE_APB_CLK,
    .deconfigure = false
};

static ledc_channel_config_t channel_config = {
    .gpio_num = CLK_OUT_PIN,
    .speed_mode = LEDC_HIGH_SPEED_MODE,
    .channel = CLK_OUT_PWM_CHANNEL,
    .intr_type = LEDC_INTR_DISABLE,
    .timer_sel = CLK_OUT_PWM_TIMER,
    .duty = 1, // 50% output duty for 1-bit resolution
    .hpoint = 0,
    .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
    .flags.output_invert = 0
};

gpio_config_t gpio_clk_en_config = {
    .pin_bit_mask = (1ULL << CLK_OUT_EN_PIN),
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE
};

static const char* TAG = "wb_test";


static void start_clock_out(void)
{
    gpio_config(&gpio_clk_en_config);
    gpio_set_level(CLK_OUT_EN_PIN, 0);

    ledc_timer_config_t tim_conf = timer_config;
    tim_conf.deconfigure = false;
    ledc_timer_config(&tim_conf);

    ledc_channel_config_t ch_conf = channel_config;
    ledc_channel_config(&ch_conf);

    gpio_set_level(CLK_OUT_EN_PIN, 1);

    ESP_LOGW(TAG, "100 kHz clock output on RS485-1 port enabled");
}


static void stop_clock_out(void)
{
    gpio_set_level(CLK_OUT_EN_PIN, 0);

    ledc_stop(channel_config.speed_mode, channel_config.channel, 0);
    ledc_timer_pause(timer_config.speed_mode, timer_config.timer_num);

    ledc_timer_config_t tim_conf = timer_config;
    tim_conf.deconfigure = true;
    ledc_timer_config(&tim_conf);

    gpio_reset_pin(CLK_OUT_PIN);
    gpio_reset_pin(CLK_OUT_EN_PIN);

    ESP_LOGW(TAG, "100 kHz clock output on RS485-1 port disabled");
}


static esp_err_t process_request_json(cJSON *request_json)
{
    if (request_json == NULL) {
        return ESP_FAIL;
    }

    // Check if command field exists
    if (!cJSON_HasObjectItem(request_json, CLK_OUT_JSON_FIELD)) {
        ESP_LOGW(TAG, "Field '%s' not found in request", CLK_OUT_JSON_FIELD);
        return ESP_ERR_NOT_FOUND;
    }

    cJSON *cmd_item = cJSON_GetObjectItem(request_json, CLK_OUT_JSON_FIELD);
    if (!cJSON_IsBool(cmd_item)) {
        ESP_LOGW(TAG, "Field '%s' value is not boolean", CLK_OUT_JSON_FIELD);
        return ESP_ERR_INVALID_ARG;
    }

    if (cmd_item->valueint) {
        if (!clock_out_en) {
            clock_out_en = true;
            s_saved_port_mode = port_manager_get_mode(BRIDGE_PORT_INDEX);
            esp_err_t err = port_manager_set_mode(BRIDGE_PORT_INDEX, PM_MODE_DISABLED);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to disable port for clock_out: %s", esp_err_to_name(err));
            }
            start_clock_out();
        }
    } else {
        if (clock_out_en) {
            clock_out_en = false;
            stop_clock_out();
            esp_err_t err = port_manager_set_mode(BRIDGE_PORT_INDEX, s_saved_port_mode);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to restore port mode after clock_out: %s", esp_err_to_name(err));
            }
        }
    }

    return ESP_OK;
}


static void fill_response_json(cJSON *response_json)
{
    cJSON_AddBoolToObject(response_json, CLK_OUT_JSON_FIELD, clock_out_en);
}


esp_err_t wb_test_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "WB Test GET request received");

    if (!auth_middleware_check(req)) {
        // Func will send 401 Unauthorized if auth fails
        return ESP_OK;
    }

    // Create success response
    cJSON *response_json = cJSON_CreateObject();
    if (response_json == NULL) {
        ESP_LOGE(TAG, "Failed to create response JSON");
        return json_utils_send_error(req, "Failed to create response");
    }

    fill_response_json(response_json);
    json_utils_send_response(req, NULL, response_json);

    return ESP_OK;
}


esp_err_t wb_test_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "WB Test POST request received");

    if (!auth_middleware_check(req)) {
        // Func will send 401 Unauthorized if auth fails
        return ESP_OK;
    }

    cJSON *request_json = json_utils_receive_json(req);
    if (request_json == NULL) {
        return ESP_FAIL;
    }

    esp_err_t res = process_request_json(request_json);
    if (res != ESP_OK) {
        json_utils_cleanup(request_json, NULL);
        if (res == ESP_ERR_NOT_FOUND) {
            return json_utils_send_error(req, "Field 'clock_out' not found in request");
        } else if (res == ESP_ERR_INVALID_ARG) {
            return json_utils_send_error(req, "Incorrect command field value");
        } else {
            return json_utils_send_error(req, "Failed to process request");
        }
    }

    // Create success response
    cJSON *response_json = cJSON_CreateObject();
    if (response_json == NULL) {
        ESP_LOGE(TAG, "Failed to create response JSON");
        json_utils_cleanup(request_json, NULL);
        return json_utils_send_error(req, "Failed to create response");
    }

    cJSON_AddBoolToObject(response_json, "success", true);
    fill_response_json(response_json);

    json_utils_send_response(req, request_json, response_json);

    return ESP_OK;
}
