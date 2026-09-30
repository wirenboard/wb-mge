#pragma once

#include <esp_http_server.h>

// HTTP handler for GET /sdr/info: capture limits and the highest manual gain
esp_err_t sdr_http_info_get_handler(httpd_req_t *req);

// HTTP handler for GET /sdr/capture?freq=&rate=&bw=&gain=: one raw IQ capture as octet-stream,
// int8 I then int8 Q per sample
esp_err_t sdr_http_capture_get_handler(httpd_req_t *req);
