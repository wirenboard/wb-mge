#include "unity.h"
#include "console_log.h"

#include "bridge/fast_modbus.h"
#include "bridge/modbus_helpers.h"
#include "tcp_server.h"

#include "malloc.h"

#include <string.h>

#define MODBUS_TCP_TRANSACTION_ID               0x1234
#define MODBUS_TCP_PROTOCOL_ID                  0x0000
#define MODBUS_MGE_DETECT_LENGTH                17
#define MODBUS_MGE_DETECT_UID                   0x00
#define MODBUS_MGE_DETECT_FCODE                 0x47

#define MODBUS_TCP_RESP_LENGTH                  25 // sizeof(mb_tcp_header_t) (8) + strlen(FAST_MODBUS_RESPONSE_STR) (17)

typedef struct {
    int index;
    tcp_desc_t* tcp_desc;
} mb_tcp_task_ctx_t;

static tcp_desc_t mock_tcp_desc = {0};

static const mb_tcp_task_ctx_t test_ctx = {
    .index = 1,
    .tcp_desc = &mock_tcp_desc
};

static const char *FAST_MODBUS_REQUEST_STR  = "WB-FAST-MODBUS?";
static const char *FAST_MODBUS_RESPONSE_STR = "WB-FAST-MODBUS-OK";
static const size_t tcp_req_data_len = 15;
static const size_t tcp_req_len = sizeof(mb_tcp_header_t) + tcp_req_data_len;

static uint8_t *test_request = NULL;
static mb_tcp_header_t *test_request_header = NULL;

void setUp(void)
{
    mock_tcp_server_reset();

    test_request = malloc(tcp_req_len);
    TEST_ASSERT_NOT_NULL(test_request);

    test_request_header = (mb_tcp_header_t *)test_request;
    TEST_ASSERT_NOT_NULL(test_request_header);
}

void tearDown(void)
{
    reset_malloc_tracking();
    free(test_request);
}

// Тестируем успешную отправку ответа на запрос Быстрого Modbus
void test_fast_modbus_send_probe_response_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test fast_modbus_send_probe_response - success case");
    LOG_MESSAGE();

    test_request_header->transaction_id = MODBUS_TCP_TRANSACTION_ID;
    test_request_header->protocol_id = MODBUS_TCP_PROTOCOL_ID;
    test_request_header->length = modbus_swap16(MODBUS_MGE_DETECT_LENGTH);
    test_request_header->unit_id = MODBUS_MGE_DETECT_UID;
    test_request_header->function = MODBUS_MGE_DETECT_FCODE;

    memcpy(&test_request[sizeof(mb_tcp_header_t)], FAST_MODBUS_REQUEST_STR, tcp_req_data_len);

    enum fast_modbus_probe_result result = fast_modbus_send_probe_response(
        test_ctx.index, test_ctx.tcp_desc, -1, test_request
    );

    TEST_ASSERT_EQUAL(FAST_MODBUS_PROBE_SUCCESS, result);

    TEST_ASSERT_EQUAL_MESSAGE(1, tcp_server_send_mock.called, "tcp_server_send should be called once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(tcp_server_send_mock.desc, test_ctx.tcp_desc, "tcp_server_send called with incorrect tcp_desc");
    TEST_ASSERT_EQUAL_MESSAGE(MODBUS_TCP_RESP_LENGTH, tcp_server_send_mock.len, "tcp_server_send called with incorrect length");
    verify_malloc_tracking(1, 1);

    /* Verify response content: transaction_id must echo the request value */
    mb_tcp_header_t *resp_header = (mb_tcp_header_t *)tcp_server_send_mock.last_data;
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(
        MODBUS_TCP_TRANSACTION_ID,
        resp_header->transaction_id,
        "Response transaction_id must echo the request transaction_id"
    );

    /* Verify payload (after MBAP header) contains the Fast Modbus response string */
    TEST_ASSERT_EQUAL_STRING_LEN_MESSAGE(
        FAST_MODBUS_RESPONSE_STR,
        (const char *)&tcp_server_send_mock.last_data[sizeof(mb_tcp_header_t)],
        strlen(FAST_MODBUS_RESPONSE_STR),
        "Response payload must contain WB-FAST-MODBUS-OK"
    );
}

// Тестируем случай, когда запрос не является запросом Быстрого Modbus (другой код функции)
void test_fast_modbus_send_probe_response_not_probe_function(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test fast_modbus_send_probe_response - not probe function");
    LOG_MESSAGE();

    test_request_header->transaction_id = MODBUS_TCP_TRANSACTION_ID;
    test_request_header->protocol_id = MODBUS_TCP_PROTOCOL_ID;
    test_request_header->length = modbus_swap16(MODBUS_MGE_DETECT_LENGTH);
    test_request_header->unit_id = MODBUS_MGE_DETECT_UID;
    test_request_header->function = 0x03;

    memcpy(&test_request[sizeof(mb_tcp_header_t)], FAST_MODBUS_REQUEST_STR, tcp_req_data_len);

    enum fast_modbus_probe_result result = fast_modbus_send_probe_response(
        test_ctx.index, test_ctx.tcp_desc, -1, test_request
    );

    TEST_ASSERT_EQUAL(FAST_MODBUS_NOT_PROBE, result);
    TEST_ASSERT_EQUAL_MESSAGE(0, tcp_server_send_mock.called, "tcp_server_send should not be called");
    verify_malloc_tracking(0, 0);
}

// Тестируем случай, когда запрос не является запросом Быстрого Modbus (другой Unit ID)
void test_fast_modbus_send_probe_response_not_probe_unit_id(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test fast_modbus_send_probe_response - not probe unit ID");
    LOG_MESSAGE();

    test_request_header->transaction_id = MODBUS_TCP_TRANSACTION_ID;
    test_request_header->protocol_id = MODBUS_TCP_PROTOCOL_ID;
    test_request_header->length = modbus_swap16(MODBUS_MGE_DETECT_LENGTH);
    test_request_header->unit_id = 0x01;
    test_request_header->function = MODBUS_MGE_DETECT_FCODE;

    memcpy(&test_request[sizeof(mb_tcp_header_t)], FAST_MODBUS_REQUEST_STR, tcp_req_data_len);

    enum fast_modbus_probe_result result = fast_modbus_send_probe_response(
        test_ctx.index, test_ctx.tcp_desc, -1, test_request
    );

    TEST_ASSERT_EQUAL(FAST_MODBUS_NOT_PROBE, result);
    TEST_ASSERT_EQUAL_MESSAGE(0, tcp_server_send_mock.called, "tcp_server_send should not be called");
    verify_malloc_tracking(0, 0);
}

// Тестируем случай, когда запрос не является запросом Быстрого Modbus (другая длина)
void test_fast_modbus_send_probe_response_not_probe_length(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test fast_modbus_send_probe_response - not probe length");
    LOG_MESSAGE();

    test_request_header->transaction_id = MODBUS_TCP_TRANSACTION_ID;
    test_request_header->protocol_id = MODBUS_TCP_PROTOCOL_ID;
    test_request_header->length = MODBUS_MGE_DETECT_LENGTH;
    test_request_header->unit_id = MODBUS_MGE_DETECT_UID;
    test_request_header->function = MODBUS_MGE_DETECT_FCODE;

    memcpy(&test_request[sizeof(mb_tcp_header_t)], FAST_MODBUS_REQUEST_STR, tcp_req_data_len);

    enum fast_modbus_probe_result result = fast_modbus_send_probe_response(
        test_ctx.index, test_ctx.tcp_desc, -1, test_request
    );

    TEST_ASSERT_EQUAL(FAST_MODBUS_NOT_PROBE, result);
    TEST_ASSERT_EQUAL_MESSAGE(0, tcp_server_send_mock.called, "tcp_server_send should not be called");
    verify_malloc_tracking(0, 0);
}

// Тестируем случай, когда запрос не является запросом Быстрого Modbus (другая строка запроса)
void test_fast_modbus_send_probe_response_not_probe_string(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test fast_modbus_send_probe_response - not probe string");
    LOG_MESSAGE();

    test_request_header->transaction_id = MODBUS_TCP_TRANSACTION_ID;
    test_request_header->protocol_id = MODBUS_TCP_PROTOCOL_ID;
    test_request_header->length = modbus_swap16(MODBUS_MGE_DETECT_LENGTH);
    test_request_header->unit_id = MODBUS_MGE_DETECT_UID;
    test_request_header->function = MODBUS_MGE_DETECT_FCODE;

    memcpy(&test_request[sizeof(mb_tcp_header_t)], "INVALID_STRING", tcp_req_data_len);

    enum fast_modbus_probe_result result = fast_modbus_send_probe_response(
        test_ctx.index, test_ctx.tcp_desc, -1, test_request
    );

    TEST_ASSERT_EQUAL(FAST_MODBUS_NOT_PROBE, result);
    TEST_ASSERT_EQUAL_MESSAGE(0, tcp_server_send_mock.called, "tcp_server_send should not be called");
    verify_malloc_tracking(0, 0);
}

// Тестируем случай, когда tcp_server_send возвращает ошибку
void test_fast_modbus_send_probe_response_send_fail(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test fast_modbus_send_probe_response - send fail");
    LOG_MESSAGE();

    test_request_header->transaction_id = MODBUS_TCP_TRANSACTION_ID;
    test_request_header->protocol_id = MODBUS_TCP_PROTOCOL_ID;
    test_request_header->length = modbus_swap16(MODBUS_MGE_DETECT_LENGTH);
    test_request_header->unit_id = MODBUS_MGE_DETECT_UID;
    test_request_header->function = MODBUS_MGE_DETECT_FCODE;

    memcpy(&test_request[sizeof(mb_tcp_header_t)], FAST_MODBUS_REQUEST_STR, tcp_req_data_len);

    tcp_server_send_mock.result = ESP_FAIL;

    enum fast_modbus_probe_result result = fast_modbus_send_probe_response(
        test_ctx.index, test_ctx.tcp_desc, -1, test_request
    );

    TEST_ASSERT_EQUAL(FAST_MODBUS_PROBE_SEND_FAIL, result);
    TEST_ASSERT_EQUAL_MESSAGE(1, tcp_server_send_mock.called, "tcp_server_send should be called once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(tcp_server_send_mock.desc, test_ctx.tcp_desc, "tcp_server_send called with incorrect tcp_desc");
    TEST_ASSERT_EQUAL_MESSAGE(MODBUS_TCP_RESP_LENGTH, tcp_server_send_mock.len, "tcp_server_send called with incorrect length");
    verify_malloc_tracking(1, 1);
}

// Тестируем случай, когда malloc возвращает NULL
void test_fast_modbus_send_probe_response_malloc_fail(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test fast_modbus_send_probe_response - malloc fail");
    LOG_MESSAGE();

    test_request_header->transaction_id = MODBUS_TCP_TRANSACTION_ID;
    test_request_header->protocol_id = MODBUS_TCP_PROTOCOL_ID;
    test_request_header->length = modbus_swap16(MODBUS_MGE_DETECT_LENGTH);
    test_request_header->unit_id = MODBUS_MGE_DETECT_UID;
    test_request_header->function = MODBUS_MGE_DETECT_FCODE;

    memcpy(&test_request[sizeof(mb_tcp_header_t)], FAST_MODBUS_REQUEST_STR, tcp_req_data_len);

    malloc_should_fail = true;

    enum fast_modbus_probe_result result = fast_modbus_send_probe_response(
        test_ctx.index, test_ctx.tcp_desc, -1, test_request
    );

    TEST_ASSERT_EQUAL(FAST_MODBUS_PROBE_MALLOC_FAIL, result);
    TEST_ASSERT_EQUAL_MESSAGE(0, tcp_server_send_mock.called, "tcp_server_send should not be called");
    verify_malloc_tracking(0, 0);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_fast_modbus_send_probe_response_success);
    RUN_TEST(test_fast_modbus_send_probe_response_not_probe_function);
    RUN_TEST(test_fast_modbus_send_probe_response_not_probe_unit_id);
    RUN_TEST(test_fast_modbus_send_probe_response_not_probe_length);
    RUN_TEST(test_fast_modbus_send_probe_response_not_probe_string);
    RUN_TEST(test_fast_modbus_send_probe_response_send_fail);
    RUN_TEST(test_fast_modbus_send_probe_response_malloc_fail);

    return UNITY_END();
}
