#include "json_utils.h"

#include <esp_log.h>
#include <esp_system.h>
#include <esp_http_server.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "json_utils";

cJSON *json_utils_receive_json(httpd_req_t *req)
{
    if (req->content_len > JSON_UTILS_REQ_RECV_BUF_SIZE) {
        ESP_LOGE(TAG, "Request too large: %d bytes", req->content_len);
        return NULL;
    }

    char *buf = (char *)malloc(JSON_UTILS_REQ_RECV_BUF_SIZE);
    if (buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for request buffer");
        return NULL;
    }

    int received = httpd_req_recv(req, buf, JSON_UTILS_REQ_RECV_BUF_SIZE);
    if (received <= 0) {
        ESP_LOGE(TAG, "Failed to receive data: %d", received);
        free(buf);
        return NULL;
    }

    // Null-terminate the buffer
    if (received < JSON_UTILS_REQ_RECV_BUF_SIZE) {
        buf[received] = '\0';
    } else {
        buf[JSON_UTILS_REQ_RECV_BUF_SIZE - 1] = '\0';
    }

    cJSON *req_json = cJSON_Parse(buf);
    if (req_json == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        free(buf);
        return NULL;
    }

    free(buf);
    return req_json;
}

void json_utils_send_response(httpd_req_t *req, cJSON *req_json, cJSON *resp_json)
{
    if (resp_json == NULL) {
        ESP_LOGE(TAG, "Response JSON is NULL");
        json_utils_cleanup(req_json, NULL);
        return;
    }

    char *json_str = cJSON_Print(resp_json);
    if (json_str == NULL) {
        ESP_LOGE(TAG, "Failed to print JSON");
        json_utils_cleanup(req_json, resp_json);
        return;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);

    free(json_str);
    json_utils_cleanup(req_json, resp_json);
}

esp_err_t json_utils_send_error(httpd_req_t *req, const char *error_message)
{
    cJSON *resp_json = cJSON_CreateObject();
    if (resp_json == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return ESP_FAIL;
    }

    cJSON_AddBoolToObject(resp_json, "success", false);
    if (error_message != NULL) {
        cJSON_AddStringToObject(resp_json, "error", error_message);
    }

    json_utils_send_response(req, NULL, resp_json);
    return ESP_OK;
}

void json_utils_cleanup(cJSON *req_json, cJSON *resp_json)
{
    if (req_json != NULL) {
        cJSON_Delete(req_json);
    }
    if (resp_json != NULL) {
        cJSON_Delete(resp_json);
    }
}
