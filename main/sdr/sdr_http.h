#pragma once

#include <esp_http_server.h>

// HTTP handler for GET /sdr/info: the fixed scan range, spectrum bin width and the highest manual gain
esp_err_t sdr_http_info_get_handler(httpd_req_t *req);

// HTTP handler for GET /sdr/scan?gain=: the power spectrum of the fixed scan range as chunked octet-stream,
// one int8 dBFS value per bin. If a capture after the first one fails, the body ends early with the bins
// sent so far.
esp_err_t sdr_http_scan_get_handler(httpd_req_t *req);
