/*
 * wb_modbus_bridge  -  Proof-of-Concept
 *
 * Reads a wb-mqtt-serial JSON device template, polls a single Modbus RTU
 * device (WB-style, standard Modbus only) and publishes register values
 * to MQTT.  Also subscribes to writable channel topics and writes back.
 *
 * Topic scheme (same as wb-mqtt-serial on Wirenboard):
 *   /devices/<device_id>/controls/<channel_name>          - published value
 *   /devices/<device_id>/controls/<channel_name>/on       - write target
 *   /devices/<device_id>/meta/name                        - device name
 *
 * Usage:
 *   wb_bridge <serial_port> <baud> <slave_id> <template.json> \
 *             <mqtt_host> [mqtt_port]
 *
 * Example:
 *   wb_bridge /dev/ttyRS485-1 9600 1 templates/config-wb-mrps6.json \
 *             localhost 1883
 */

/* Required for POSIX extensions: strdup, nanosleep, struct timespec */
#define _POSIX_C_SOURCE 200809L

#include "template.h"
#include "modbus_rtu.h"
#include "mqtt_client.h"
#include "value_conv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>
#include <time.h>

/* ------------------------------------------------------------------
 * MQTT topic helpers
 * ------------------------------------------------------------------ */

#define TOPIC_MAX 256

static void make_value_topic(const char *device_id, const char *channel,
                              char *buf, int buf_size)
{
    snprintf(buf, (size_t)buf_size, "/devices/%s/controls/%s",
             device_id, channel);
}

static void make_set_topic(const char *device_id, const char *channel,
                            char *buf, int buf_size)
{
    snprintf(buf, (size_t)buf_size, "/devices/%s/controls/%s/on",
             device_id, channel);
}

/* ------------------------------------------------------------------
 * Bridge state
 * ------------------------------------------------------------------ */

typedef struct {
    wb_template_t   tmpl;
    mb_rtu_port_t  *mb;
    mqtt_client_t  *mqtt;
    uint8_t         slave_id;

    /* Last published values (to detect changes and avoid re-publishing) */
    char          **last_values;  /* one string per channel */
} bridge_t;

static bridge_t g_bridge;   /* single global instance (fine for PoC/MCU) */

/* ------------------------------------------------------------------
 * Poll one channel: read Modbus, publish if changed
 * ------------------------------------------------------------------ */

/* Returns 0 on success (or channel disabled/skipped), -1 on Modbus error. */
static int poll_channel(bridge_t *b, int idx)
{
    wb_channel_t *ch = &b->tmpl.channels[idx];
    if (!ch->enabled) return 0;

    uint16_t regs[64] = {0};  /* 64 regs = 128 bytes, enough for any WB string */
    uint8_t  bit      = 0;
    int      rc       = -1;

    switch (ch->reg_type) {
        case REG_HOLDING:
            rc = mb_read_holding(b->mb, b->slave_id,
                                 (uint16_t)ch->address,
                                 (uint16_t)ch->num_regs, regs);
            break;
        case REG_INPUT:
            rc = mb_read_input(b->mb, b->slave_id,
                               (uint16_t)ch->address,
                               (uint16_t)ch->num_regs, regs);
            break;
        case REG_COIL:
            rc = mb_read_coils(b->mb, b->slave_id,
                               (uint16_t)ch->address, 1, &bit);
            if (rc == 0) regs[0] = bit;
            break;
        case REG_DISCRETE:
            rc = mb_read_discrete(b->mb, b->slave_id,
                                  (uint16_t)ch->address, 1, &bit);
            if (rc == 0) regs[0] = bit;
            break;
    }

    if (rc != 0) {
        fprintf(stderr, "bridge: poll failed for '%s' (addr=%u)\n",
                ch->name, ch->address);
        return -1;
    }

    char val_str[256];  /* 256 chars covers 128-byte WB strings + numerics */
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

    /* Publish only if value changed */
    if (b->last_values[idx] && strcmp(b->last_values[idx], val_str) == 0)
        return;

    free(b->last_values[idx]);
    b->last_values[idx] = strdup(val_str);

    char topic[TOPIC_MAX];
    make_value_topic(b->tmpl.device_id, ch->name, topic, sizeof(topic));
    if (mqtt_publish(b->mqtt, topic, val_str) == 0) {
        printf("[PUB] %s = %s\n", topic, val_str);
        fflush(stdout);
    } else {
        fprintf(stderr, "[ERR] mqtt publish failed for %s\n", topic);
    }
    return 0;
}

/* ------------------------------------------------------------------
 * MQTT message callback: handle writes to /on topics
 * ------------------------------------------------------------------ */

static void on_mqtt_message(const char *topic, const char *payload, void *userdata)
{
    bridge_t *b = (bridge_t *)userdata;

    /* Topic format: /devices/<id>/controls/<name>/on */
    char prefix[TOPIC_MAX];
    snprintf(prefix, sizeof(prefix), "/devices/%s/controls/", b->tmpl.device_id);
    if (strncmp(topic, prefix, strlen(prefix)) != 0) return;

    const char *rest = topic + strlen(prefix);
    /* Find trailing /on */
    const char *on_suffix = "/on";
    size_t rest_len = strlen(rest);
    size_t on_len   = strlen(on_suffix);
    if (rest_len <= on_len) return;
    if (strcmp(rest + rest_len - on_len, on_suffix) != 0) return;

    /* Extract channel name */
    char ch_name[256];
    size_t name_len = rest_len - on_len;
    if (name_len >= sizeof(ch_name)) return;
    memcpy(ch_name, rest, name_len);
    ch_name[name_len] = '\0';

    /* Find channel */
    int idx = -1;
    for (int i = 0; i < b->tmpl.num_channels; i++) {
        if (strcmp(b->tmpl.channels[i].name, ch_name) == 0) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        fprintf(stderr, "bridge: unknown channel '%s'\n", ch_name);
        return;
    }

    wb_channel_t *ch = &b->tmpl.channels[idx];
    if (ch->readonly) {
        fprintf(stderr, "bridge: channel '%s' is read-only\n", ch->name);
        return;
    }

    printf("set  %s <- %s\n", ch->name, payload);

    int rc = -1;
    switch (ch->reg_type) {
        case REG_COIL: {
            int bit = (strcmp(payload, "0") != 0 &&
                       strcmp(payload, "false") != 0 &&
                       strlen(payload) > 0);
            rc = mb_write_coil(b->mb, b->slave_id, (uint16_t)ch->address, bit);
            break;
        }
        case REG_HOLDING: {
            /* Reject unsupported write formats before touching the bus */
            if (ch->format == FMT_U64 || ch->format == FMT_S64 ||
                ch->format == FMT_STRING ||
                ch->format == FMT_BCD8  || ch->format == FMT_BCD16 ||
                ch->format == FMT_BCD24 || ch->format == FMT_BCD32) {
                fprintf(stderr, "bridge: write to format %d not supported for '%s'\n",
                        ch->format, ch->name);
                return;
            }
            double fval = string_to_value(payload, ch->scale, ch->offset);
            uint16_t regs[4] = {0};
            word_order_t wo = (ch->word_order == CH_WORD_ORDER_LITTLE)
                              ? WORD_ORDER_LITTLE_ENDIAN : WORD_ORDER_BIG_ENDIAN;
            byte_order_t bo = (ch->byte_order == CH_BYTE_ORDER_LITTLE)
                              ? BYTE_ORDER_LITTLE_ENDIAN : BYTE_ORDER_BIG_ENDIAN;
            double_to_regs(fval, ch->format, wo, bo, regs);
            if (ch->num_regs == 1) {
                rc = mb_write_holding(b->mb, b->slave_id,
                                      (uint16_t)ch->address, regs[0]);
            } else {
                rc = mb_write_holding_multi(b->mb, b->slave_id,
                                            (uint16_t)ch->address,
                                            (uint16_t)ch->num_regs, regs);
            }
            break;
        }
        default:
            fprintf(stderr, "bridge: cannot write to reg_type for '%s'\n",
                    ch->name);
            return;
    }

    if (rc != 0) {
        fprintf(stderr, "bridge: write failed for '%s'\n", ch->name);
    }
}

/* ------------------------------------------------------------------
 * Publish device meta (name)
 * ------------------------------------------------------------------ */

static void publish_meta(bridge_t *b)
{
    char topic[TOPIC_MAX];
    snprintf(topic, sizeof(topic), "/devices/%s/meta/name", b->tmpl.device_id);
    mqtt_publish(b->mqtt, topic, b->tmpl.device_name);

    /* Subscribe to all writable channel /on topics */
    for (int i = 0; i < b->tmpl.num_channels; i++) {
        wb_channel_t *ch = &b->tmpl.channels[i];
        if (ch->readonly) continue;
        char set_topic[TOPIC_MAX];
        make_set_topic(b->tmpl.device_id, ch->name, set_topic, sizeof(set_topic));
        mqtt_subscribe(b->mqtt, set_topic);
    }
}

/* ------------------------------------------------------------------
 * Main poll loop
 * ------------------------------------------------------------------ */

static void run_bridge(bridge_t *b)
{
    publish_meta(b);
    printf("[INFO] bridge: starting poll loop for '%s' (id: %s, slave %d)\n",
           b->tmpl.device_name, b->tmpl.device_id, b->slave_id);
    printf("[INFO] polling %d enabled channels\n",
           b->tmpl.num_channels);
    fflush(stdout);

    /*
     * Simple sequential polling.
     * Backoff: after 5 consecutive failed cycles (all enabled channels fail)
     * we insert a 1 s delay to avoid flooding stderr when device is offline.
     */
    int consecutive_fail_cycles = 0;
    while (1) {
        int cycle_ok = 0;
        for (int i = 0; i < b->tmpl.num_channels; i++) {
            if (poll_channel(b, i) == 0) cycle_ok = 1;

            /* Process incoming MQTT messages between reads */
            mqtt_loop(b->mqtt);

#if defined(__linux__) || defined(__APPLE__) || defined(__unix__)
            {
                struct timespec ts = {0, 20 * 1000 * 1000L}; /* 20 ms */
                nanosleep(&ts, NULL);
            }
#endif
        }

        if (cycle_ok) {
            consecutive_fail_cycles = 0;
        } else {
            consecutive_fail_cycles++;
            if (consecutive_fail_cycles >= 5) {
                fprintf(stderr, "[WARN] device not responding, backing off 1 s\n");
#if defined(__linux__) || defined(__APPLE__) || defined(__unix__)
                struct timespec ts = {1, 0};
                nanosleep(&ts, NULL);
#endif
                consecutive_fail_cycles = 0; /* reset to log at next failure */
            }
        }
    }
}

/* ------------------------------------------------------------------
 * Entry point
 * ------------------------------------------------------------------ */

static void print_template_info(const wb_template_t *t)
{
    printf("Device: '%s'  id: '%s'\n", t->device_name, t->device_id);
    printf("Channels (%d):\n", t->num_channels);
    for (int i = 0; i < t->num_channels; i++) {
        const wb_channel_t *ch = &t->channels[i];
        static const char *rt_names[] = {"holding", "input", "coil", "discrete"};
        /* Must match reg_format_t enum order exactly */
        static const char *fmt_names[] = {
            "u8","s8","u16","s16","u32","s32","u64","s64",
            "float","string","bcd8","bcd16","bcd24","bcd32"
        };
        const char *fmt_str = (ch->format < (int)(sizeof(fmt_names)/sizeof(*fmt_names)))
                              ? fmt_names[ch->format] : "?";
        printf("  [%2d] %-40s  %s addr=%-4u regs=%-2u fmt=%-6s scale=%-8g off=%g%s%s\n",
               i, ch->name,
               rt_names[ch->reg_type], ch->address, ch->num_regs,
               fmt_str,
               ch->scale, ch->offset,
               ch->readonly  ? " RO"  : "",
               !ch->enabled  ? " DISABLED" : "");
    }
}

int main(int argc, char *argv[])
{
    /* Quick template test mode: wb_bridge --test-template <template.json> */
    if (argc == 3 && strcmp(argv[1], "--test-template") == 0) {
        wb_template_t t;
        if (wb_template_parse(argv[2], &t) != 0) return 1;
        print_template_info(&t);
        wb_template_free(&t);
        return 0;
    }

    if (argc < 7) {
        fprintf(stderr,
            "Usage: %s <serial_port> <baud> <stop_bits> <slave_id> <template.json>"
            " <mqtt_host> <mqtt_port>\n"
            "       %s --test-template <template.json>\n"
            "Example:\n"
            "  %s /dev/ttyRS485-1 9600 2 131 "
            "templates/config-wb-msw_v4.json localhost 1883\n",
            argv[0], argv[0], argv[0]);
        return 1;
    }

    const char *serial_port = argv[1];
    int         baud        = (int)strtol(argv[2], NULL, 10);
    int         stop_bits   = (int)strtol(argv[3], NULL, 10);
    int         slave_id    = (int)strtol(argv[4], NULL, 10);
    const char *tmpl_path   = argv[5];
    const char *mqtt_host   = argv[6];
    int         mqtt_port   = argc > 7 ? (int)strtol(argv[7], NULL, 10) : 1883;

    if (baud <= 0 || stop_bits < 1 || stop_bits > 2 ||
        slave_id < 1 || slave_id > 247) {
        fprintf(stderr, "Invalid arguments: baud=%d stop_bits=%d slave_id=%d\n",
                baud, stop_bits, slave_id);
        return 1;
    }

    /* Parse template */
    if (wb_template_parse(tmpl_path, &g_bridge.tmpl) != 0) {
        fprintf(stderr, "Failed to parse template '%s'\n", tmpl_path);
        return 1;
    }
    printf("Template loaded: %d channels for device '%s' (id: %s)\n",
           g_bridge.tmpl.num_channels,
           g_bridge.tmpl.device_name,
           g_bridge.tmpl.device_id);

    /* Allocate last_values array */
    g_bridge.last_values = calloc((size_t)g_bridge.tmpl.num_channels,
                                  sizeof(char *));
    if (!g_bridge.last_values) {
        fprintf(stderr, "OOM\n"); return 1;
    }

    /* Open serial port. Response timeout 300 ms is safe for 9600 baud.
     * Parity is hardcoded to 'N' (most WB devices use 8N1 or 8N2). */
    g_bridge.mb = mb_rtu_open(serial_port, baud, 'N', stop_bits, 300);
    if (!g_bridge.mb) {
        fprintf(stderr, "Failed to open serial port '%s'\n", serial_port);
        return 1;
    }
    g_bridge.slave_id = (uint8_t)slave_id;

    /* Connect to MQTT.
     * Use device_id as client_id to make it unique per device.
     * Two instances for the same device would conflict on the broker. */
    g_bridge.mqtt = mqtt_connect(mqtt_host, mqtt_port,
                                  g_bridge.tmpl.device_id,
                                  on_mqtt_message, &g_bridge);
    if (!g_bridge.mqtt) {
        fprintf(stderr, "Failed to connect to MQTT broker %s:%d\n",
                mqtt_host, mqtt_port);
        mb_rtu_close(g_bridge.mb);
        return 1;
    }

    /* Run forever */
    run_bridge(&g_bridge);

    /* Unreachable in this PoC, but good practice: */
    mqtt_disconnect(g_bridge.mqtt);
    mb_rtu_close(g_bridge.mb);
    wb_template_free(&g_bridge.tmpl);
    return 0;
}
