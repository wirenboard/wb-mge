#pragma once

/*
 * esp_crt_bundle.h: the attach function tg_bot hands to esp_http_client. The fake client never
 * calls it; a test checks only that it was handed over.
 */

#include "esp_err.h"

esp_err_t esp_crt_bundle_attach(void *conf);
