#pragma once

#include <esp_http_server.h>

// HTTP handler for command POST endpoint
esp_err_t cmd_post_handler(httpd_req_t *req);

// async func to reboot device
void cmd_reboot_device(void);
