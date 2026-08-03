#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// Port the web server falls back to when NVS holds no usable web_port. It is also the last-resort
// port the settings update task tries when the web UI comes up on neither the newly configured nor
// the previously served port.
#define HTTP_SERVER_DEFAULT_PORT 80

// How long a single blocking read on a client socket waits before httpd_req_recv() reports a
// timeout. Same value the ESP-IDF default carried implicitly, but stated explicitly here because
// the OTA handler derives its stall limit from it (OTA_RECV_STALL_TIMEOUT_MS in main/ota_handler.c):
// a receive window that only exists as an IDF default would silently move that limit when IDF
// changes it.
#define HTTP_RECV_WAIT_TIMEOUT_S 5

// Start the web server on the port configured in NVS (KEY_WEB_PORT).
esp_err_t http_server_init(void);

// Start the web server on an explicit port, bypassing NVS. Used by the settings update task to put
// the web UI back on the port it was serving when starting it on the newly configured port failed:
// a web UI that is down everywhere leaves the user with no way to undo the setting that broke it —
// the API that would fix it IS the web server. Everything else must go through http_server_init().
esp_err_t http_server_init_port(uint16_t port);

esp_err_t http_server_deinit(void);

// TCP port the web server is currently listening on; 0 when it is not running.
uint16_t http_server_get_port(void);

bool http_server_check_settings_changed(void);
