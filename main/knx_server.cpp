/* Suppress knxd header warnings when compiled with main's -Werror */
#pragma GCC diagnostic ignored "-Wreorder"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"

/*
 * KNX IP Secure server for ESP32 — thin wrapper around knxd.
 *
 * Builds an IniData config, registers the needed knxd factories,
 * instantiates the Router, and runs ev_run() in a FreeRTOS task.
 */
#include "knx_server.h"

#include "esp_log.h"
#include "esp_check.h"
#include "esp_task_wdt.h"
#include "esp_vfs_dev.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "mbedtls/pkcs5.h"
#include "mbedtls/md.h"

#include "inifile.h"
#include "router.h"
#include "link.h"
#include "tcptunserver.h"
#include "ncn5120.h"
#include "retry.h"
#include "dummy.h"
#include "nat.h"
#include <ev++.h>

#include <cstring>
#include <cstdio>

static const char *TAG = "knx_server";

/* Exposed for llserial.cpp to switch parity after NCN5120 init */
uart_port_t g_knx_uart_num = UART_NUM_1;

static TaskHandle_t s_task = nullptr;
static volatile bool s_running = false;
static Router *s_router = nullptr;

/* ============================================================
 * Factory registration — knxd's AutoRegister statics get
 * stripped from static archives, so we register manually.
 * ============================================================ */
static void register_knxd_factories()
{
    static Maker<NCN5120, Driver> ncn5120_maker;
    static Maker<TcpTunServer, Server> tcptunsrv_maker;
    static Maker<RetryFilter, Filter> retry_maker;
    static Maker<DummyL2Driver, Driver> dummy_maker;
    static Maker<NatL2Filter, Filter> single_maker;

    Factory<Driver>::Instance().reg(ncn5120_maker, "ncn5120");
    Factory<Server>::Instance().reg(tcptunsrv_maker, "tcptunsrv");
    Factory<Filter>::Instance().reg(retry_maker, "retry");
    Factory<Driver>::Instance().reg(dummy_maker, "dummy");
    Factory<Filter>::Instance().reg(single_maker, "single");

    printf("[KNX] Filters registered: ");
    for (auto& kv : Factory<Filter>::Instance().map())
        printf("%s ", kv.first.c_str());
    printf("\n");
}

/* ============================================================
 * PBKDF2 key derivation with NVS caching.
 * Returns the 32-hex-char derived key in `hex_out`.
 * On cache hit, returns instantly. On miss, takes ~10s on ESP32.
 * ============================================================ */
static void derive_cached_key(const char *password, const char *nvs_prefix,
                              const char *salt, size_t salt_len,
                              char hex_out[33])
{
    /* NVS key: prefix + truncated hash (max 15 chars for ESP-IDF NVS) */
    char nvs_key[16];
    uint32_t h = 0;
    for (const char *p = password; *p; p++)
        h = h * 31 + (uint8_t)*p;
    snprintf(nvs_key, sizeof(nvs_key), "%.6s%08lx", nvs_prefix, (unsigned long)h);

    /* Try NVS cache */
    nvs_handle_t nvs;
    if (nvs_open("knx_keys", NVS_READWRITE, &nvs) == ESP_OK) {
        size_t len = 33;
        if (nvs_get_str(nvs, nvs_key, hex_out, &len) == ESP_OK && len == 33) {
            nvs_close(nvs);
            ESP_LOGI(TAG, "%s key from NVS cache", nvs_prefix);
            return;
        }

        /* Cache miss — derive */
        ESP_LOGI(TAG, "Deriving %s key (PBKDF2, ~10s)...", nvs_prefix);
        uint8_t key[16];
        mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256,
            (const uint8_t *)password, strlen(password),
            (const uint8_t *)salt, salt_len, 65536, 16, key);

        for (int i = 0; i < 16; i++)
            sprintf(hex_out + i * 2, "%02x", key[i]);
        hex_out[32] = 0;

        nvs_set_str(nvs, nvs_key, hex_out);
        nvs_commit(nvs);
        nvs_close(nvs);
        ESP_LOGI(TAG, "%s key derived and cached", nvs_prefix);
    } else {
        /* NVS unavailable — derive without caching */
        ESP_LOGW(TAG, "NVS unavailable, deriving %s key without cache", nvs_prefix);
        uint8_t key[16];
        mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256,
            (const uint8_t *)password, strlen(password),
            (const uint8_t *)salt, salt_len, 65536, 16, key);
        for (int i = 0; i < 16; i++)
            sprintf(hex_out + i * 2, "%02x", key[i]);
        hex_out[32] = 0;
    }
}

/* ============================================================
 * UART + VFS init for NCN5120
 * ============================================================ */
static esp_err_t uart_vfs_init(const knx_server_config_t *cfg)
{
    uart_config_t uart_cfg = {};
    uart_cfg.baud_rate = cfg->uart_baud;
    uart_cfg.data_bits = UART_DATA_8_BITS;
    uart_cfg.parity = UART_PARITY_DISABLE; /* NCN5120 starts in 8N1, switches to 8E1 after RESET */
    g_knx_uart_num = cfg->uart_num;
    uart_cfg.stop_bits = UART_STOP_BITS_1;
    uart_cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_cfg.source_clk = UART_SCLK_DEFAULT;

    if (!uart_is_driver_installed(cfg->uart_num)) {
        ESP_RETURN_ON_ERROR(
            uart_driver_install(cfg->uart_num, 4096, 512, 0, NULL, 0),
            TAG, "uart_driver_install");
    }
    ESP_RETURN_ON_ERROR(uart_param_config(cfg->uart_num, &uart_cfg), TAG, "uart_param_config");
    ESP_RETURN_ON_ERROR(
        uart_set_pin(cfg->uart_num, cfg->uart_tx_pin, cfg->uart_rx_pin,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE),
        TAG, "uart_set_pin");

    uart_vfs_dev_register();
    uart_vfs_dev_use_driver(cfg->uart_num);
    /* Disable CR→LF translation — NCN5120 sends raw binary data where
     * 0x0D is a valid byte (e.g. PID=13). Default VFS converts CR→LF,
     * corrupting KNX frames containing 0x0D. */
    uart_vfs_dev_port_set_rx_line_endings(cfg->uart_num, ESP_LINE_ENDINGS_LF);
    uart_vfs_dev_port_set_tx_line_endings(cfg->uart_num, ESP_LINE_ENDINGS_LF);

    ESP_LOGI(TAG, "UART%d: TX=%d, RX=%d, %d baud",
             cfg->uart_num, cfg->uart_tx_pin, cfg->uart_rx_pin, cfg->uart_baud);
    return ESP_OK;
}

/* ============================================================
 * knxd Router task
 * ============================================================ */
static void knx_router_task(void *arg)
{
    knx_server_config_t *cfg = (knx_server_config_t *)arg;
    esp_task_wdt_delete(xTaskGetCurrentTaskHandle());

    register_knxd_factories();

    /* Build INI config */
    IniData ini;

    IniSectionPtr main_sec = ini["main"];
    (*main_sec)["addr"] = "1.1.1";
    (*main_sec)["client-addrs"] = "1.1.200:4";
    (*main_sec)["connections"] = "A.ncn5120,B.tcptun";
    (*main_sec)["name"] = "ESP32-KNX";
    (*main_sec)["unknown-ok"] = "true";
    (*main_sec)["debug"] = "debug";
    IniSectionPtr dbg = ini["debug"];
    (*dbg)["trace-mask"] = "0xff"; /* all layers for debugging */

    IniSectionPtr ncn = ini["A.ncn5120"];
    char devpath[32];
    snprintf(devpath, sizeof(devpath), "/dev/uart/%d", cfg->uart_num);
    (*ncn)["device"] = devpath;
    (*ncn)["driver"] = "ncn5120";
    (*ncn)["filter"] = "single";
    (*ncn)["addr"] = "1.1.1"; /* for NCN5120 hardware ACK */
    char baud_str[16];
    snprintf(baud_str, sizeof(baud_str), "%d", cfg->uart_baud);
    (*ncn)["baudrate"] = baud_str;

    IniSectionPtr tcptun = ini["B.tcptun"];
    (*tcptun)["server"] = "tcptunsrv";
    (*tcptun)["tunnel"] = "B.tunnel";
    IniSectionPtr tunnel_sec = ini["B.tunnel"]; /* empty section for tunnel client config */
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", cfg->tcp_port ? cfg->tcp_port : 3671);
    (*tcptun)["port"] = port_str;

    /* Pre-derive PBKDF2 keys (cached in NVS after first boot) */
    if (cfg->device_auth[0]) {
        char hex[33];
        derive_cached_key(cfg->device_auth, "kda_",
                          "device-authentication-code.1.secure.ip.knx.org", 46, hex);
        (*tcptun)["device-fdsk"] = hex;
    }
    if (cfg->user_password[0]) {
        char hex[33];
        derive_cached_key(cfg->user_password, "kup_",
                          "user-password.1.secure.ip.knx.org", 33, hex);
        (*tcptun)["user-password-key"] = hex;
    }

    char sno_str[16];
    snprintf(sno_str, sizeof(sno_str), "%02x%02x%02x%02x%02x%02x",
             cfg->serial_number[0], cfg->serial_number[1], cfg->serial_number[2],
             cfg->serial_number[3], cfg->serial_number[4], cfg->serial_number[5]);
    (*tcptun)["serial-number"] = sno_str;

    ESP_LOGI(TAG, "Starting knxd: ncn5120@%s, tcptunsrv port %s", devpath, port_str);

    s_router = new Router(ini, "main");
    if (!s_router->setup()) {
        ESP_LOGE(TAG, "knxd Router setup failed");
        delete s_router;
        s_router = nullptr;
        goto done;
    }

    s_router->start();
    s_running = true;
    ESP_LOGI(TAG, "knxd running");

    ev_run(EV_A_ 0);

    s_router->stop(false);
    delete s_router;
    s_router = nullptr;

done:
    s_running = false;
    s_task = nullptr;
    delete cfg;
    vTaskDelete(nullptr);
}

/* ============================================================
 * Public C API
 * ============================================================ */
extern "C" esp_err_t knx_server_start(const knx_server_config_t *config)
{
    if (s_running)
        return ESP_ERR_INVALID_STATE;

    if (config->uart_baud > 0) {
        esp_err_t err = uart_vfs_init(config);
        if (err != ESP_OK)
            return err;
    }

    knx_server_config_t *cfg = new knx_server_config_t(*config);
    BaseType_t ret = xTaskCreate(knx_router_task, "knxd", 16384, cfg, 5, &s_task);
    if (ret != pdPASS) {
        delete cfg;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

extern "C" esp_err_t knx_server_stop(void)
{
    if (!s_running)
        return ESP_OK;
    ev_break(EV_A_ EVBREAK_ALL);
    for (int i = 0; i < 50 && s_task; i++)
        vTaskDelay(pdMS_TO_TICKS(100));
    return ESP_OK;
}

extern "C" bool knx_server_is_running(void)
{
    return s_running;
}
