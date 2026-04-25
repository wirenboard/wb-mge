#include "mqtt_serial_bridge.h"

#include "template.h"
#include "modbus_rtu.h"
#include "value_conv.h"

#include "setting_items.h"
#include "port_manager.h"   /* port_manager_set_mode / port_manager_get_mode */
#include "mqtt_client.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>
#include <math.h>

static const char *TAG = "mqtt_serial_bridge";

/* ------------------------------------------------------------------
 * Hardware pins — same as bridge/bridge.c, defined here separately
 * to avoid a hard coupling to that module's internals.
 * ------------------------------------------------------------------
 * TODO: move to a shared board header if hardware is ever revised.
 * ------------------------------------------------------------------ */
#define MB_SERIAL_PORT_NUM_1    1
#define MB_SERIAL_INPUT_PIN_1   GPIO_NUM_9
#define MB_SERIAL_OUTPUT_PIN_1  GPIO_NUM_10
#define MB_SERIAL_IO_PIN_1      GPIO_NUM_4

#define MB_SERIAL_PORT_NUM_2    2
#define MB_SERIAL_INPUT_PIN_2   GPIO_NUM_12
#define MB_SERIAL_OUTPUT_PIN_2  GPIO_NUM_14
#define MB_SERIAL_IO_PIN_2      GPIO_NUM_15

#define TOPIC_MAX   256
#define WRITE_QUEUE_DEPTH  8

/* ------------------------------------------------------------------
 * Embedded device template (compiled in via EMBED_TXTFILES)
 * ------------------------------------------------------------------ */
extern const uint8_t device_template_start[] asm("_binary_device_template_json_start");
extern const uint8_t device_template_end[]   asm("_binary_device_template_json_end");

/* ------------------------------------------------------------------
 * Write command dispatched from MQTT event to bridge task
 * ------------------------------------------------------------------ */
typedef struct {
    int      channel_idx;
    char     payload[256];
} write_cmd_t;

/* ------------------------------------------------------------------
 * Bridge state
 * ------------------------------------------------------------------ */
typedef struct {
    wb_template_t            tmpl;
    mb_rtu_port_t           *mb;
    esp_mqtt_client_handle_t mqtt;
    uint8_t                  slave_id;
    char                   **last_values;
    QueueHandle_t            write_queue;   /* MQTT -> bridge task */
    int                      bridge_port_index; /* 0-based index for the port_manager port */
    pm_mode_t                saved_port_mode;   /* port mode to restore when the bridge stops */
} bridge_ctx_t;

static bridge_ctx_t    g_ctx;
static TaskHandle_t    g_task_handle  = NULL;
static EventGroupHandle_t g_stop_eg   = NULL;

#define EV_STOP_REQ  BIT0
#define EV_STOPPED   BIT1

/* ------------------------------------------------------------------
 * Topic helpers
 * ------------------------------------------------------------------ */
static void make_value_topic(const char *dev_id, const char *ch_name, char *buf, int sz)
{
    snprintf(buf, (size_t)sz, "/devices/%s/controls/%s", dev_id, ch_name);
}

static void make_set_topic(const char *dev_id, const char *ch_name, char *buf, int sz)
{
    snprintf(buf, (size_t)sz, "/devices/%s/controls/%s/on", dev_id, ch_name);
}

/* ------------------------------------------------------------------
 * Execute one write command in the bridge task context
 * (safe: runs in the same task as Modbus I/O)
 * ------------------------------------------------------------------ */
static void execute_write(bridge_ctx_t *b, const write_cmd_t *cmd)
{
    int idx = cmd->channel_idx;
    if (idx < 0 || idx >= b->tmpl.num_channels) return;

    wb_channel_t *ch = &b->tmpl.channels[idx];
    if (ch->readonly) return;

    const char *payload = cmd->payload;
    ESP_LOGI(TAG, "set  %s <- %s", ch->name, payload);

    switch (ch->reg_type) {
    case REG_COIL: {
        int bit = (strcmp(payload, "0") != 0 &&
                   strcmp(payload, "false") != 0 &&
                   strlen(payload) > 0);
        mb_write_coil(b->mb, b->slave_id, (uint16_t)ch->address, bit);
        break;
    }
    case REG_HOLDING: {
        if (ch->format == FMT_U64 || ch->format == FMT_S64 ||
            ch->format == FMT_STRING ||
            ch->format == FMT_BCD8  || ch->format == FMT_BCD16 ||
            ch->format == FMT_BCD24 || ch->format == FMT_BCD32) {
            ESP_LOGW(TAG, "write to format %d not supported for '%s'", ch->format, ch->name);
            break;
        }
        word_order_t wo = (ch->word_order == CH_WORD_ORDER_LITTLE)
                          ? WORD_ORDER_LITTLE_ENDIAN : WORD_ORDER_BIG_ENDIAN;
        byte_order_t bo = (ch->byte_order == CH_BYTE_ORDER_LITTLE)
                          ? BYTE_ORDER_LITTLE_ENDIAN : BYTE_ORDER_BIG_ENDIAN;
        double fval = string_to_value(payload, ch->scale, ch->offset);
        uint16_t regs[4] = {0};
        double_to_regs(fval, ch->format, wo, bo, regs);
        if (ch->num_regs == 1)
            mb_write_holding(b->mb, b->slave_id, (uint16_t)ch->address, regs[0]);
        else
            mb_write_holding_multi(b->mb, b->slave_id, (uint16_t)ch->address,
                                   (uint16_t)ch->num_regs, regs);
        break;
    }
    default:
        ESP_LOGW(TAG, "cannot write reg_type for '%s'", ch->name);
        break;
    }
}

/* ------------------------------------------------------------------
 * MQTT event handler — runs in MQTT client task.
 * MUST NOT call blocking Modbus I/O here.
 * Writes are dispatched via write_queue to the bridge task.
 * ------------------------------------------------------------------ */
static void on_mqtt_event(void *handler_args, esp_event_base_t base,
                          int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;

    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    bridge_ctx_t *b = &g_ctx;

    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected");
        {
            char topic[TOPIC_MAX];
            snprintf(topic, sizeof(topic), "/devices/%s/meta/name", b->tmpl.device_id);
            esp_mqtt_client_publish(b->mqtt, topic, b->tmpl.device_name, 0, 0, 1);
        }
        for (int i = 0; i < b->tmpl.num_channels; i++) {
            wb_channel_t *ch = &b->tmpl.channels[i];
            if (ch->readonly) continue;
            char set_topic[TOPIC_MAX];
            make_set_topic(b->tmpl.device_id, ch->name, set_topic, sizeof(set_topic));
            esp_mqtt_client_subscribe(b->mqtt, set_topic, 0);
        }
        break;

    case MQTT_EVENT_DATA: {
        if (!b->write_queue) break;

        char topic[TOPIC_MAX];
        int tlen = event->topic_len < (int)sizeof(topic) - 1 ? event->topic_len : (int)sizeof(topic) - 1;
        memcpy(topic, event->topic, (size_t)tlen);
        topic[tlen] = '\0';

        /* Check prefix */
        char prefix[TOPIC_MAX];
        snprintf(prefix, sizeof(prefix), "/devices/%s/controls/", b->tmpl.device_id);
        if (strncmp(topic, prefix, strlen(prefix)) != 0) break;

        const char *rest   = topic + strlen(prefix);
        const char *on_suf = "/on";
        size_t rest_len = strlen(rest);
        size_t on_len   = strlen(on_suf);
        if (rest_len <= on_len) break;
        if (strcmp(rest + rest_len - on_len, on_suf) != 0) break;

        char ch_name[256];
        size_t name_len = rest_len - on_len;
        if (name_len >= sizeof(ch_name)) break;
        memcpy(ch_name, rest, name_len);
        ch_name[name_len] = '\0';

        int idx = -1;
        for (int i = 0; i < b->tmpl.num_channels; i++) {
            if (strcmp(b->tmpl.channels[i].name, ch_name) == 0) { idx = i; break; }
        }
        if (idx < 0) { ESP_LOGW(TAG, "unknown channel '%s'", ch_name); break; }
        if (b->tmpl.channels[idx].readonly) { ESP_LOGW(TAG, "channel '%s' is read-only", ch_name); break; }

        write_cmd_t cmd = { .channel_idx = idx };
        int plen = event->data_len < (int)sizeof(cmd.payload) - 1 ? event->data_len : (int)sizeof(cmd.payload) - 1;
        memcpy(cmd.payload, event->data, (size_t)plen);
        cmd.payload[plen] = '\0';

        /* Non-blocking send: drop if queue full (don't block MQTT task) */
        if (xQueueSend(b->write_queue, &cmd, 0) != pdTRUE) {
            ESP_LOGW(TAG, "write queue full, dropping write to '%s'", ch_name);
        }
        break;
    }

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected, will reconnect automatically");
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error event");
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------
 * Poll one channel: read Modbus, publish to MQTT if changed
 * ------------------------------------------------------------------ */
static int poll_channel(bridge_ctx_t *b, int idx)
{
    wb_channel_t *ch = &b->tmpl.channels[idx];
    if (!ch->enabled) return 0;

    uint16_t regs[64] = {0};
    uint8_t  bit      = 0;
    int      rc       = -1;

    switch (ch->reg_type) {
    case REG_HOLDING:
        rc = mb_read_holding(b->mb, b->slave_id, (uint16_t)ch->address, (uint16_t)ch->num_regs, regs);
        break;
    case REG_INPUT:
        rc = mb_read_input(b->mb, b->slave_id, (uint16_t)ch->address, (uint16_t)ch->num_regs, regs);
        break;
    case REG_COIL:
        rc = mb_read_coils(b->mb, b->slave_id, (uint16_t)ch->address, 1, &bit);
        if (rc == 0) regs[0] = bit;
        break;
    case REG_DISCRETE:
        rc = mb_read_discrete(b->mb, b->slave_id, (uint16_t)ch->address, 1, &bit);
        if (rc == 0) regs[0] = bit;
        break;
    }

    if (rc != 0) {
        ESP_LOGW(TAG, "poll failed for '%s' (addr=%" PRIu32 ")", ch->name, ch->address);
        return -1;
    }

    char val_str[256];
    if (ch->reg_type == REG_COIL || ch->reg_type == REG_DISCRETE) {
        snprintf(val_str, sizeof(val_str), "%d", (int)regs[0]);
    } else if (ch->format == FMT_STRING) {
        decode_string_regs(regs, ch->num_regs, val_str, sizeof(val_str));
    } else {
        word_order_t wo = (ch->word_order == CH_WORD_ORDER_LITTLE)
                          ? WORD_ORDER_LITTLE_ENDIAN : WORD_ORDER_BIG_ENDIAN;
        byte_order_t bo = (ch->byte_order == CH_BYTE_ORDER_LITTLE)
                          ? BYTE_ORDER_LITTLE_ENDIAN : BYTE_ORDER_BIG_ENDIAN;
        double raw = raw_to_double(regs, ch->format, wo, bo);
        if (is_error_value(raw, ch->error_value)) {
            snprintf(val_str, sizeof(val_str), "Error");
        } else {
            value_to_string(raw, ch->scale, ch->offset, val_str, sizeof(val_str));
        }
    }

    if (b->last_values[idx] && strcmp(b->last_values[idx], val_str) == 0)
        return 0;

    free(b->last_values[idx]);
    b->last_values[idx] = strdup(val_str);

    char topic[TOPIC_MAX];
    make_value_topic(b->tmpl.device_id, ch->name, topic, sizeof(topic));
    int r = esp_mqtt_client_publish(b->mqtt, topic, val_str, 0, 0, 1);
    if (r < 0)
        ESP_LOGE(TAG, "publish failed for %s", topic);
    else
        ESP_LOGD(TAG, "[PUB] %s = %s", topic, val_str);
    return 0;
}

/* ------------------------------------------------------------------
 * Bridge FreeRTOS task
 * ------------------------------------------------------------------ */
static void bridge_task(void *pvParameters)
{
    bridge_ctx_t *b = (bridge_ctx_t *)pvParameters;
    int consecutive_fail_cycles = 0;

    while (1) {
        /* Check stop request */
        EventBits_t bits = xEventGroupGetBits(g_stop_eg);
        if (bits & EV_STOP_REQ) break;

        /* Drain pending write commands first */
        write_cmd_t cmd;
        while (xQueueReceive(b->write_queue, &cmd, 0) == pdTRUE) {
            execute_write(b, &cmd);
        }

        /* Poll all channels */
        int cycle_ok = 0;
        for (int i = 0; i < b->tmpl.num_channels; i++) {
            /* Check stop between channels */
            if (xEventGroupGetBits(g_stop_eg) & EV_STOP_REQ) goto done;

            if (poll_channel(b, i) == 0) cycle_ok = 1;
            vTaskDelay(pdMS_TO_TICKS(20));
        }

        if (cycle_ok) {
            consecutive_fail_cycles = 0;
        } else if (b->tmpl.num_channels > 0) {
            consecutive_fail_cycles++;
            if (consecutive_fail_cycles >= 5) {
                ESP_LOGW(TAG, "device not responding, backing off 1s");
                vTaskDelay(pdMS_TO_TICKS(1000));
                consecutive_fail_cycles = 0;
            }
        } else {
            /* No channels — idle */
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }

done:
    ESP_LOGI(TAG, "bridge task stopping");

    esp_mqtt_client_stop(b->mqtt);
    esp_mqtt_client_destroy(b->mqtt);
    b->mqtt = NULL;

    mb_rtu_close(b->mb);
    b->mb = NULL;

    /* Restore the port to the mode it had before the bridge took it over */
    port_manager_set_mode((unsigned)b->bridge_port_index, b->saved_port_mode);

    for (int i = 0; i < b->tmpl.num_channels; i++) free(b->last_values[i]);
    free(b->last_values);
    b->last_values = NULL;

    vQueueDelete(b->write_queue);
    b->write_queue = NULL;

    wb_template_free(&b->tmpl);

    g_task_handle = NULL;
    xEventGroupSetBits(g_stop_eg, EV_STOPPED);
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------ */

esp_err_t mqtt_serial_bridge_start(void)
{
    mqtt_serial_bridge_stop();

    if (!setting_items_read_bool(KEY_MQTT_ENABLED)) {
        ESP_LOGI(TAG, "MQTT disabled — bridge not started");
        return ESP_OK;
    }
    if (!setting_items_read_bool(KEY_MQTS_ENABLED)) {
        ESP_LOGI(TAG, "MQTT serial bridge disabled");
        return ESP_OK;
    }

    /* ---- Parse embedded template ---- */
    size_t tmpl_len = (size_t)(device_template_end - device_template_start);
    char *tmpl_buf = malloc(tmpl_len + 1);
    if (!tmpl_buf) return ESP_ERR_NO_MEM;
    memcpy(tmpl_buf, device_template_start, tmpl_len);
    tmpl_buf[tmpl_len] = '\0';

    if (wb_template_parse_str(tmpl_buf, &g_ctx.tmpl) != 0) {
        ESP_LOGE(TAG, "Failed to parse embedded device template");
        free(tmpl_buf);
        return ESP_FAIL;
    }
    free(tmpl_buf);

    ESP_LOGI(TAG, "Template: '%s' id='%s' (%d channels)",
             g_ctx.tmpl.device_name, g_ctx.tmpl.device_id, g_ctx.tmpl.num_channels);

    g_ctx.last_values = calloc((size_t)g_ctx.tmpl.num_channels, sizeof(char *));
    if (!g_ctx.last_values) { wb_template_free(&g_ctx.tmpl); return ESP_ERR_NO_MEM; }

    g_ctx.write_queue = xQueueCreate(WRITE_QUEUE_DEPTH, sizeof(write_cmd_t));
    if (!g_ctx.write_queue) {
        free(g_ctx.last_values);
        wb_template_free(&g_ctx.tmpl);
        return ESP_ERR_NO_MEM;
    }

    /* ---- Determine RS485 port ---- */
    int port_num_1based = setting_items_read_int(KEY_MQTS_PORT);
    if (port_num_1based != 2) port_num_1based = 1;
    int bridge_port_index = port_num_1based - 1; /* 0-based for the port_manager API */

    /* Free the UART so the mqtt-serial bridge can take exclusive ownership:
     * remember the current port mode, then disable the port. Disabling is
     * persisted by port_manager so the settings task does not re-open the
     * port and steal the UART back; the saved mode is restored on stop. */
    g_ctx.saved_port_mode = port_manager_get_mode((unsigned)bridge_port_index);
    port_manager_set_mode((unsigned)bridge_port_index, PM_MODE_DISABLED);
    g_ctx.bridge_port_index = bridge_port_index;

    int uart_num;
    gpio_num_t tx_pin, rx_pin, dir_pin;
    if (port_num_1based == 1) {
        uart_num = MB_SERIAL_PORT_NUM_1;
        tx_pin   = MB_SERIAL_OUTPUT_PIN_1;
        rx_pin   = MB_SERIAL_INPUT_PIN_1;
        dir_pin  = MB_SERIAL_IO_PIN_1;
    } else {
        uart_num = MB_SERIAL_PORT_NUM_2;
        tx_pin   = MB_SERIAL_OUTPUT_PIN_2;
        rx_pin   = MB_SERIAL_INPUT_PIN_2;
        dir_pin  = MB_SERIAL_IO_PIN_2;
    }

    /* Configure UART pins — must be done before mb_rtu_open installs driver */
    esp_err_t pin_err = uart_set_pin((uart_port_t)uart_num, tx_pin, rx_pin, dir_pin, UART_PIN_NO_CHANGE);
    if (pin_err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(pin_err));
        port_manager_set_mode((unsigned)bridge_port_index, g_ctx.saved_port_mode);
        vQueueDelete(g_ctx.write_queue);
        free(g_ctx.last_values);
        wb_template_free(&g_ctx.tmpl);
        return pin_err;
    }

    /* Read serial settings */
    int baud = (port_num_1based == 1) ? setting_items_read_int(KEY_BAUDRATE1)
                                      : setting_items_read_int(KEY_BAUDRATE2);
    if (baud <= 0) baud = 9600;

    char stop_str[8] = {0};
    char parity_str[8] = {0};
    if (port_num_1based == 1) {
        setting_items_read(KEY_STOPBITS1, stop_str);
        setting_items_read(KEY_PARITY1, parity_str);
    } else {
        setting_items_read(KEY_STOPBITS2, stop_str);
        setting_items_read(KEY_PARITY2, parity_str);
    }
    int  stop_bits = (stop_str[0] == '2') ? 2 : 1;
    char parity    = 'N';
    if (strncmp(parity_str, "even", 4) == 0) parity = 'E';
    else if (strncmp(parity_str, "odd", 3) == 0) parity = 'O';

    /* Open Modbus RTU (passes uart_num as string to mb_rtu_open) */
    char uart_dev[4];
    snprintf(uart_dev, sizeof(uart_dev), "%d", uart_num);
    g_ctx.mb = mb_rtu_open(uart_dev, baud, parity, stop_bits, 300);
    if (!g_ctx.mb) {
        ESP_LOGE(TAG, "Failed to open UART%d for Modbus RTU", uart_num);
        port_manager_set_mode((unsigned)bridge_port_index, g_ctx.saved_port_mode);
        vQueueDelete(g_ctx.write_queue);
        free(g_ctx.last_values);
        wb_template_free(&g_ctx.tmpl);
        return ESP_FAIL;
    }

    /* Validate and store slave ID */
    int slave_id = setting_items_read_int(KEY_MQTS_SLAVE_ID);
    if (slave_id < 1 || slave_id > 247) {
        ESP_LOGE(TAG, "Invalid slave ID %d (must be 1-247)", slave_id);
        mb_rtu_close(g_ctx.mb);
        port_manager_set_mode((unsigned)bridge_port_index, g_ctx.saved_port_mode);
        vQueueDelete(g_ctx.write_queue);
        free(g_ctx.last_values);
        wb_template_free(&g_ctx.tmpl);
        return ESP_ERR_INVALID_ARG;
    }
    g_ctx.slave_id = (uint8_t)slave_id;

    /* ---- Connect to MQTT ---- */
    char mqtt_host[SETTING_ITEM_MAX_STR_LEN] = {0};
    char mqtt_user[SETTING_ITEM_MAX_STR_LEN] = {0};
    char mqtt_pass[SETTING_ITEM_MAX_STR_LEN] = {0};
    setting_items_read(KEY_MQTT_HOST, mqtt_host);
    setting_items_read(KEY_MQTT_USER, mqtt_user);
    setting_items_read(KEY_MQTT_PASS, mqtt_pass);
    int mqtt_port = setting_items_read_int(KEY_MQTT_PORT);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.hostname  = mqtt_host,
        .broker.address.port      = (uint32_t)mqtt_port,
        .broker.address.transport = MQTT_TRANSPORT_OVER_TCP,
        .credentials.client_id    = g_ctx.tmpl.device_id,
        .credentials.username     = mqtt_user[0] ? mqtt_user : NULL,
        .credentials.authentication.password = mqtt_pass[0] ? mqtt_pass : NULL,
    };

    g_ctx.mqtt = esp_mqtt_client_init(&mqtt_cfg);
    if (!g_ctx.mqtt) {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        mb_rtu_close(g_ctx.mb);
        port_manager_set_mode((unsigned)bridge_port_index, g_ctx.saved_port_mode);
        vQueueDelete(g_ctx.write_queue);
        free(g_ctx.last_values);
        wb_template_free(&g_ctx.tmpl);
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(g_ctx.mqtt, ESP_EVENT_ANY_ID, on_mqtt_event, NULL);
    esp_err_t ret = esp_mqtt_client_start(g_ctx.mqtt);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_mqtt_client_start: %s", esp_err_to_name(ret));
        esp_mqtt_client_destroy(g_ctx.mqtt);
        mb_rtu_close(g_ctx.mb);
        port_manager_set_mode((unsigned)bridge_port_index, g_ctx.saved_port_mode);
        vQueueDelete(g_ctx.write_queue);
        free(g_ctx.last_values);
        wb_template_free(&g_ctx.tmpl);
        return ret;
    }

    /* ---- Create event group for stop signalling ---- */
    if (!g_stop_eg) g_stop_eg = xEventGroupCreate();
    xEventGroupClearBits(g_stop_eg, EV_STOP_REQ | EV_STOPPED);

    BaseType_t r = xTaskCreate(bridge_task, "mqtt_serial_bridge",
                               6144, &g_ctx, 5, &g_task_handle);
    if (r != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate failed");
        esp_mqtt_client_stop(g_ctx.mqtt);
        esp_mqtt_client_destroy(g_ctx.mqtt);
        mb_rtu_close(g_ctx.mb);
        port_manager_set_mode((unsigned)bridge_port_index, g_ctx.saved_port_mode);
        vQueueDelete(g_ctx.write_queue);
        free(g_ctx.last_values);
        wb_template_free(&g_ctx.tmpl);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Bridge started: UART%d baud=%d slave=%d -> MQTT %s:%d",
             uart_num, baud, g_ctx.slave_id, mqtt_host, mqtt_port);
    return ESP_OK;
}

void mqtt_serial_bridge_stop(void)
{
    if (!g_task_handle || !g_stop_eg) return;

    ESP_LOGI(TAG, "Stopping bridge task...");
    xEventGroupSetBits(g_stop_eg, EV_STOP_REQ);
    xEventGroupWaitBits(g_stop_eg, EV_STOPPED, pdFALSE, pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "Bridge task stopped");
}
