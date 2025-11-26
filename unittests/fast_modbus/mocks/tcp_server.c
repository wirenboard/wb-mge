#include "unity.h"

#include "bridge/tcp_desc.h"
#include "tcp_server.h"

esp_err_t mock_tcp_send_result = ESP_OK;

esp_err_t tcp_server_send(tcp_desc_t *desc, uint8_t *data, size_t len)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "tcp_server_send: desc should not be NULL");
    TEST_ASSERT_NOT_NULL_MESSAGE(data, "tcp_server_send: data should not be NULL");
    TEST_ASSERT_EQUAL_MESSAGE(25, len, "tcp_server_send: len should be 25 for test cases");

    return mock_tcp_send_result;
}
