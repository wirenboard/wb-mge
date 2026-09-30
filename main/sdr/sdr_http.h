#pragma once

#include <esp_http_server.h>

// HTTP handler for GET /sdr/info: the fixed scan range, spectrum bin width, the highest manual gain and the
// longest dwell per step
esp_err_t sdr_http_info_get_handler(httpd_req_t *req);

// HTTP handler for GET /sdr/scan?gain=&dwell_ms=: the power spectrum of the fixed scan range as chunked
// octet-stream, one int8 dBFS value per bin, each the maximum over the captures of dwell_ms at that step.
// gain=-1 lets the software AGC pick the gain; header X-Sdr-Gain carries the gain the scan used. If a step
// after the first one fails, the body ends early with the bins sent so far.
esp_err_t sdr_http_scan_get_handler(httpd_req_t *req);

// HTTP handler for GET /sdr/networks: scans for Wi-Fi networks and returns them as a JSON array of
// {ssid, bssid, channel, second, rssi}
esp_err_t sdr_http_networks_get_handler(httpd_req_t *req);
