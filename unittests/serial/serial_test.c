#include "unity.h"
#include "console_log.h"

#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_bit_defs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "config.h"
#include "serial.h"
#include "malloc.h"

#include <string.h>

#define SERIAL_BUF_SIZE                 (1000)
#define SERIAL_TASK_STACK_SIZE          (1024 * 4)
#define SERIAL_TASK_PRIORITY            12
#define SERIAL_QUEUE_SIZE               20

#define EVENT_TASK_STARTED              BIT0
#define EVENT_TASK_FINISHED             BIT1
#define EVENT_TASK_EXIT_REQ             BIT8

#define SERIAL_EVENT_WAIT_TIMEOUT_MS    50

#define MOCK_RECEIVE_HANDLER_MAX_CALLS  8

typedef struct {
    size_t len;
    uint8_t data[SERIAL_BUF_SIZE];
} mock_receive_call_t;

typedef struct {
    int called;
    serial_desc_t *desc;
    uint8_t *data;      // points to mock_receive_buffer — stores last call's data
    size_t len;         // last call's length
    mock_receive_call_t calls[MOCK_RECEIVE_HANDLER_MAX_CALLS]; // per-call history
} mock_receive_handler_t;

static uint8_t mock_receive_buffer[SERIAL_BUF_SIZE];
mock_receive_handler_t mock_receive_handler_data = {0};

void setUp(void)
{
    mock_uart_reset();
    mock_freertos_event_groups_reset();
    mock_freertos_task_reset();
    mock_freertos_queue_reset();
    reset_malloc_tracking();

    memset(&mock_receive_handler_data, 0, sizeof(mock_receive_handler_data));
    memset(mock_receive_buffer, 0, sizeof(mock_receive_buffer));
    mock_receive_handler_data.data = mock_receive_buffer;
}

void tearDown(void)
{

}

static void mock_receive_handler(serial_desc_t *desc, uint8_t *data, size_t len)
{
    int idx = mock_receive_handler_data.called;
    mock_receive_handler_data.called++;
    mock_receive_handler_data.desc = desc;
    memcpy(mock_receive_handler_data.data, data, len);  // last call (backward compat)
    mock_receive_handler_data.len = len;                // last call (backward compat)
    if (idx < MOCK_RECEIVE_HANDLER_MAX_CALLS) {         // record per-call history
        mock_receive_handler_data.calls[idx].len = len;
        memcpy(mock_receive_handler_data.calls[idx].data, data, len);
    }
}

static void init_default_config(serial_config_t *config)
{
    config->port_num = UART_NUM_1;
    config->tx_pin = GPIO_NUM_10;
    config->rx_pin = GPIO_NUM_9;
    config->dir_pin = GPIO_NUM_4;
    config->baudrate = MOCK_DEFAULT_BAUDRATE;
    config->parity = UART_PARITY_DISABLE;
    config->stopbits = UART_STOP_BITS_2;
    config->databits = UART_DATA_8_BITS;
}

static void verify_uart_driver_install_args(void)
{
    TEST_ASSERT_EQUAL_MESSAGE(
        UART_NUM_1,
        mock_uart_driver_install_data.uart_num,
        "uart_driver_install should be called with correct UART port number"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        SERIAL_BUF_SIZE,
        mock_uart_driver_install_data.rx_buffer_size,
        "uart_driver_install should be called with correct RX buffer size"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        SERIAL_BUF_SIZE,
        mock_uart_driver_install_data.tx_buffer_size,
        "uart_driver_install should be called with correct TX buffer size"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        SERIAL_QUEUE_SIZE,
        mock_uart_driver_install_data.event_queue_size,
        "uart_driver_install should be called with correct event queue size"
    );

    TEST_ASSERT_NOT_NULL_MESSAGE(
        mock_uart_driver_install_data.uart_queue,
        "uart_driver_install should be called with non-NULL uart_queue pointer"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        0,
        mock_uart_driver_install_data.intr_alloc_flags,
        "uart_driver_install should be called with interrupt flags set to 0"
    );
}

static void verify_uart_param_config_args(
    uint32_t expected_baudrate,
    uart_word_length_t expected_data_bits,
    uart_parity_t expected_parity,
    uart_stop_bits_t expected_stop_bits
)
{
    TEST_ASSERT_EQUAL_MESSAGE(
        UART_NUM_1,
        mock_uart_param_config_data.uart_num,
        "uart_param_config should be called with correct UART port number"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        expected_baudrate,
        mock_uart_param_config_data.config.baud_rate,
        "uart_param_config should be called with correct baud rate"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        expected_data_bits,
        mock_uart_param_config_data.config.data_bits,
        "uart_param_config should be called with correct data bits"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        expected_parity,
        mock_uart_param_config_data.config.parity,
        "uart_param_config should be called with correct parity"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        expected_stop_bits,
        mock_uart_param_config_data.config.stop_bits,
        "uart_param_config should be called with correct stop bits"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        UART_HW_FLOWCTRL_DISABLE,
        mock_uart_param_config_data.config.flow_ctrl,
        "uart_param_config should be called with flow control disabled"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        UART_SCLK_DEFAULT,
        mock_uart_param_config_data.config.source_clk,
        "uart_param_config should be called with default source clock"
    );
}

static void verify_uart_set_pin_args(
    int expected_tx_pin,
    int expected_rx_pin,
    int expected_dir_pin
)
{
    TEST_ASSERT_EQUAL_MESSAGE(
        UART_NUM_1,
        mock_uart_set_pin_data.uart_num,
        "uart_set_pin should be called with correct UART port number"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        expected_tx_pin,
        mock_uart_set_pin_data.tx_pin,
        "uart_set_pin should be called with correct TX pin"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        expected_rx_pin,
        mock_uart_set_pin_data.rx_pin,
        "uart_set_pin should be called with correct RX pin"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        expected_dir_pin,
        mock_uart_set_pin_data.dir_pin,
        "uart_set_pin should be called with correct DIR pin"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        UART_PIN_NO_CHANGE,
        mock_uart_set_pin_data.cts_pin,
        "uart_set_pin should be called with CTS pin set to UART_PIN_NO_CHANGE"
    );
}

static void verify_uart_set_mode_args(void)
{
    TEST_ASSERT_EQUAL_MESSAGE(
        UART_NUM_1,
        mock_uart_set_mode_data.uart_num,
        "uart_set_mode should be called with correct UART port number"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        UART_MODE_RS485_HALF_DUPLEX,
        mock_uart_set_mode_data.mode,
        "uart_set_mode should be called with UART_MODE_RS485_HALF_DUPLEX"
    );
}

static void verify_uart_set_always_rx_timeout_args(void)
{
    TEST_ASSERT_EQUAL_MESSAGE(
        UART_NUM_1,
        uart_set_always_rx_timeout_data.uart_num,
        "uart_set_always_rx_timeout should be called with correct UART port number"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        true,
        uart_set_always_rx_timeout_data.always_rx_timeout_en,
        "uart_set_always_rx_timeout should be called with the always_rx_timeout_en flag set"
    );
}

static void verify_uart_set_rx_timeout_args(void)
{
    TEST_ASSERT_EQUAL_MESSAGE(
        UART_NUM_1,
        mock_uart_set_rx_timeout_data.uart_num,
        "uart_set_rx_timeout should be called with correct UART port number"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        SERIAL_RX_TOUT_SNIFFER,
        mock_uart_set_rx_timeout_data.rx_timeout,
        "uart_set_rx_timeout should be called with SERIAL_RX_TOUT_SNIFFER"
    );
}

static void verify_uart_flush_input_args(int expected_called)
{
    TEST_ASSERT_EQUAL_MESSAGE(
        expected_called,
        mock_uart_flush_input_data.called,
        "uart_flush_input call count mismatch"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        UART_NUM_1,
        mock_uart_flush_input_data.uart_num,
        "uart_flush_input should be called with correct UART port number"
    );
}

static void verify_uart_read_bytes_args(
    int expected_called,
    uint32_t expected_length,
    TickType_t expected_ticks_to_wait
)
{
    TEST_ASSERT_EQUAL_MESSAGE(
        expected_called,
        mock_uart_read_bytes_data.called,
        "uart_read_bytes call count mismatch"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        UART_NUM_1,
        mock_uart_read_bytes_data.uart_num,
        "uart_read_bytes should be called with correct UART port number"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        expected_length,
        mock_uart_read_bytes_data.length,
        "uart_read_bytes should be called with correct length"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        expected_ticks_to_wait,
        mock_uart_read_bytes_data.ticks_to_wait,
        "uart_read_bytes should be called with correct ticks to wait"
    );
}

static void verify_xEventGroupSetBits_args(
    int index,
    EventBits_t expected_uxBitsToSet
)
{
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        mock_xEventGroupCreate_data.return_value,
        mock_xEventGroupSetBits_data.xEventGroup[index],
        "xEventGroupSetBits should be called with correct event group handle"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        expected_uxBitsToSet,
        mock_xEventGroupSetBits_data.uxBitsToSet[index],
        "xEventGroupSetBits should be called with correct bits to set"
    );
}

static void verify_xEventGroupWaitBits_args(
    int index,
    EventBits_t expected_uxBitsToWaitFor,
    BaseType_t expected_xClearOnExit,
    BaseType_t expected_xWaitForAllBits,
    TickType_t expected_xTicksToWait
)
{
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        mock_xEventGroupCreate_data.return_value,
        mock_xEventGroupWaitBits_data.xEventGroup[index],
        "xEventGroupWaitBits should be called with correct event group handle"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        expected_uxBitsToWaitFor,
        mock_xEventGroupWaitBits_data.uxBitsToWaitFor[index],
        "xEventGroupWaitBits should be called with correct bits to wait for"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        expected_xClearOnExit,
        mock_xEventGroupWaitBits_data.xClearOnExit[index],
        "xEventGroupWaitBits should be called with correct xClearOnExit value"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        expected_xWaitForAllBits,
        mock_xEventGroupWaitBits_data.xWaitForAllBits[index],
        "xEventGroupWaitBits should be called with correct xWaitForAllBits value"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        expected_xTicksToWait,
        mock_xEventGroupWaitBits_data.xTicksToWait[index],
        "xEventGroupWaitBits should be called with correct ticks to wait"
    );
}

static void verify_xQueueReceive_args(
    int expected_called,
    QueueHandle_t expected_queue,
    TickType_t expected_ticks
)
{
    TEST_ASSERT_EQUAL_MESSAGE(
        expected_called,
        mock_xQueueReceive_data.called,
        "xQueueReceive should be called once"
    );

    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        expected_queue,
        mock_xQueueReceive_data.handle,
        "xQueueReceive should be called with correct queue handle"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        expected_ticks,
        mock_xQueueReceive_data.ticks,
        "xQueueReceive should be called with correct ticks to wait"
    );
}

static void verify_event_group_create_delete_handlers(void)
{
    TEST_ASSERT_EQUAL_MESSAGE(
        1,
        mock_xEventGroupCreate_data.called,
        "xEventGroupCreate should be called once"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        1,
        mock_vEventGroupDelete_data.called,
        "vEventGroupDelete should be called once"
    );

    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        mock_xEventGroupCreate_data.return_value,
        mock_vEventGroupDelete_data.xEventGroup,
        "Deleted event group should match created one"
    );
}

static void verify_task_created(void)
{
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xTaskCreate_data.called, "xTaskCreate should be called once");

    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "uart_event_task",
        mock_xTaskCreate_data.pcName,
        "Task name should be 'uart_event_task'"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        SERIAL_TASK_STACK_SIZE,
        mock_xTaskCreate_data.usStackDepth,
        "Task stack depth should be 4096"
    );

    TEST_ASSERT_EQUAL_MESSAGE(SERIAL_TASK_PRIORITY, mock_xTaskCreate_data.uxPriority, "Task priority should be 12");
}

static void verify_serial_init_calls(
    int expected_uart_driver_install,
    int expected_uart_param_config,
    int expected_uart_set_pin,
    int expected_uart_set_mode,
    int expected_uart_set_always_rx_timeout,
    int expected_uart_set_rx_timeout,
    int expected_uart_driver_delete,
    int expected_task_create,
    int expected_event_group_wait_bits
)
{
    TEST_ASSERT_EQUAL_MESSAGE(
        expected_uart_driver_install,
        mock_uart_driver_install_data.called,
        "uart_driver_install call count mismatch"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        expected_uart_param_config,
        mock_uart_param_config_data.called,
        "uart_param_config call count mismatch"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        expected_uart_set_pin,
        mock_uart_set_pin_data.called,
        "uart_set_pin call count mismatch"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        expected_uart_set_mode,
        mock_uart_set_mode_data.called,
        "uart_set_mode call count mismatch"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        expected_uart_set_always_rx_timeout,
        uart_set_always_rx_timeout_data.called,
        "uart_set_always_rx_timeout call count mismatch"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        expected_uart_set_rx_timeout,
        mock_uart_set_rx_timeout_data.called,
        "uart_set_rx_timeout call count mismatch"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        expected_uart_driver_delete,
        mock_uart_driver_delete_data.called,
        "uart_driver_delete call count mismatch"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        expected_task_create,
        mock_xTaskCreate_data.called,
        "xTaskCreate call count mismatch"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        expected_event_group_wait_bits,
        mock_xEventGroupWaitBits_data.called,
        "xEventGroupWaitBits call count mismatch"
    );
}

// Test serial_init with NULL serial_config
void test_serial_init_null_config(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init with NULL config");
    LOG_MESSAGE();

    serial_desc_t *desc = serial_init(NULL, mock_receive_handler);

    TEST_ASSERT_NULL_MESSAGE(desc, "serial_init should return NULL when config is NULL");
    verify_serial_init_calls(0, 0, 0, 0, 0, 0, 0, 0, 0);
    verify_malloc_tracking(0, 0);
}

// Test serial_init with NULL serial_receive_handler - should succeed (cache_bus mode uses serial without a receive handler)
void test_serial_init_null_handler(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init with NULL receive handler - should succeed");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED;
    serial_desc_t *desc = serial_init(&config, NULL);

    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should succeed when handler is NULL");
    verify_serial_init_calls(1, 1, 1, 1, 1, 1, 0, 1, 1);
    verify_malloc_tracking(1, 0);
    serial_deinit(desc);
}

// Test serial_init with memory allocation failure for serial_desc_t
void test_serial_init_memory_allocation_failure(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init memory allocation failure");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    malloc_should_fail = true;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);

    TEST_ASSERT_NULL_MESSAGE(desc, "serial_init should return NULL on memory allocation failure");
    verify_serial_init_calls(0, 0, 0, 0, 0, 0, 0, 0, 0);
    verify_malloc_tracking(0, 0);
}

// Test serial_init with xEventGroupCreate failure
void test_serial_init_event_group_create_failure(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init xEventGroupCreate failure");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    mock_xEventGroupCreate_data.should_fail = true;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);

    TEST_ASSERT_NULL_MESSAGE(desc, "serial_init should return NULL when xEventGroupCreate fails");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xEventGroupCreate_data.called, "xEventGroupCreate should be called once");
    verify_serial_init_calls(0, 0, 0, 0, 0, 0, 0, 0, 0);
    verify_malloc_tracking(1, 1);

    TEST_ASSERT_EQUAL_size_t_MESSAGE(
        sizeof(serial_desc_t),
        allocated_ptrs[0].size,
        "Memory size mismatch for serial_desc_t allocation"
    );
}

// Test serial_init with uart_driver_install call failure
void test_serial_init_uart_driver_install_failure(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init uart_driver_install failure");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    mock_uart_driver_install_data.result = ESP_FAIL;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);

    TEST_ASSERT_NULL_MESSAGE(desc, "serial_init should return NULL when uart_driver_install fails");
    verify_serial_init_calls(1, 0, 0, 0, 0, 0, 0, 0, 0);
    verify_event_group_create_delete_handlers();
    verify_uart_driver_install_args();
    verify_malloc_tracking(1, 1);
}

// Test serial_init with uart_param_config call failure
void test_serial_init_uart_param_config_failure(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init uart_param_config failure");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    mock_uart_param_config_data.result = ESP_FAIL;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);

    TEST_ASSERT_NULL_MESSAGE(desc, "serial_init should return NULL when uart_param_config fails");
    verify_serial_init_calls(1, 1, 0, 0, 0, 0, 1, 0, 0);
    verify_event_group_create_delete_handlers();
    verify_uart_driver_install_args();
    verify_uart_param_config_args(MOCK_DEFAULT_BAUDRATE, UART_DATA_8_BITS, UART_PARITY_DISABLE, UART_STOP_BITS_2);
    verify_malloc_tracking(1, 1);
}

// Test serial_init with uart_set_pin call failure
void test_serial_init_uart_set_pin_failure(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init uart_set_pin failure");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    mock_uart_set_pin_data.result = ESP_FAIL;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);

    TEST_ASSERT_NULL_MESSAGE(desc, "serial_init should return NULL when uart_set_pin fails");
    verify_serial_init_calls(1, 1, 1, 0, 0, 0, 1, 0, 0);
    verify_event_group_create_delete_handlers();
    verify_uart_driver_install_args();
    verify_uart_param_config_args(MOCK_DEFAULT_BAUDRATE, UART_DATA_8_BITS, UART_PARITY_DISABLE, UART_STOP_BITS_2);
    verify_uart_set_pin_args(GPIO_NUM_10, GPIO_NUM_9, GPIO_NUM_4);
    verify_malloc_tracking(1, 1);
}

// Test serial_init with uart_set_mode call failure
void test_serial_init_uart_set_mode_failure(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init uart_set_mode failure");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    mock_uart_set_mode_data.result = ESP_FAIL;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);

    TEST_ASSERT_NULL_MESSAGE(desc, "serial_init should return NULL when uart_set_mode fails");
    verify_serial_init_calls(1, 1, 1, 1, 0, 0, 1, 0, 0);
    verify_event_group_create_delete_handlers();
    verify_uart_driver_install_args();
    verify_uart_param_config_args(MOCK_DEFAULT_BAUDRATE, UART_DATA_8_BITS, UART_PARITY_DISABLE, UART_STOP_BITS_2);
    verify_uart_set_pin_args(GPIO_NUM_10, GPIO_NUM_9, GPIO_NUM_4);
    verify_uart_set_mode_args();
    verify_malloc_tracking(1, 1);
}

// Test serial_init with uart_set_rx_timeout call failure
void test_serial_init_uart_set_rx_timeout_failure(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init uart_set_rx_timeout failure");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    mock_uart_set_rx_timeout_data.result = ESP_FAIL;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);

    TEST_ASSERT_NULL_MESSAGE(desc, "serial_init should return NULL when uart_set_rx_timeout fails");
    verify_serial_init_calls(1, 1, 1, 1, 1, 1, 1, 0, 0);
    verify_event_group_create_delete_handlers();
    verify_uart_driver_install_args();
    verify_uart_param_config_args(MOCK_DEFAULT_BAUDRATE, UART_DATA_8_BITS, UART_PARITY_DISABLE, UART_STOP_BITS_2);
    verify_uart_set_pin_args(GPIO_NUM_10, GPIO_NUM_9, GPIO_NUM_4);
    verify_uart_set_mode_args();
    verify_uart_set_always_rx_timeout_args();
    verify_uart_set_rx_timeout_args();
    verify_malloc_tracking(1, 1);
}

// Test serial_init with task creation failure
void test_serial_init_task_create_failure(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init task creation failure");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    mock_xTaskCreate_data.should_fail = true;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);

    TEST_ASSERT_NULL_MESSAGE(desc, "serial_init should return NULL when task creation fails");
    verify_serial_init_calls(1, 1, 1, 1, 1, 1, 1, 1, 0);
    verify_event_group_create_delete_handlers();
    verify_uart_driver_install_args();
    verify_uart_param_config_args(MOCK_DEFAULT_BAUDRATE, UART_DATA_8_BITS, UART_PARITY_DISABLE, UART_STOP_BITS_2);
    verify_uart_set_pin_args(GPIO_NUM_10, GPIO_NUM_9, GPIO_NUM_4);
    verify_uart_set_mode_args();
    verify_uart_set_always_rx_timeout_args();
    verify_uart_set_rx_timeout_args();
    verify_task_created();
    verify_malloc_tracking(1, 1);
}

// Test serial_init initialization without actual task execution
void test_serial_init_success_no_task_execution(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init success without task execution");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);

    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should return non-NULL descriptor on success");
    verify_serial_init_calls(1, 1, 1, 1, 1, 1, 0, 1, 1);
    verify_uart_driver_install_args();
    verify_uart_param_config_args(MOCK_DEFAULT_BAUDRATE, UART_DATA_8_BITS, UART_PARITY_DISABLE, UART_STOP_BITS_2);
    verify_uart_set_pin_args(GPIO_NUM_10, GPIO_NUM_9, GPIO_NUM_4);
    verify_uart_set_mode_args();
    verify_uart_set_always_rx_timeout_args();
    verify_uart_set_rx_timeout_args();
    verify_task_created();
    verify_xEventGroupWaitBits_args(0, EVENT_TASK_STARTED, pdFALSE, pdTRUE, portMAX_DELAY);
    verify_malloc_tracking(1, 0);
}

// Test serial_init initialization with task execution and EVENT_TASK_EXIT_REQ event,
// without receiving any UART data
void test_serial_init_success_with_task_execution_no_uart_event(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init success with task execution and no UART event");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    mock_xTaskCreate_data.self_execution = true;
    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED;
    mock_xEventGroupWaitBits_data.set_event_on_call = 3;
    mock_xEventGroupWaitBits_data.events_to_set = EVENT_TASK_EXIT_REQ;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should return non-NULL descriptor on success");

    verify_serial_init_calls(1, 1, 1, 1, 1, 1, 0, 1, 5);
    verify_xEventGroupWaitBits_args(0, EVENT_TASK_EXIT_REQ, pdFALSE, pdTRUE, 0);
    verify_xEventGroupWaitBits_args(4, EVENT_TASK_STARTED, pdFALSE, pdTRUE, portMAX_DELAY);
    TEST_ASSERT_EQUAL_MESSAGE(2, mock_xEventGroupSetBits_data.called, "xEventGroupSetBits should be called twice");
    verify_xEventGroupSetBits_args(0, EVENT_TASK_STARTED);
    verify_xEventGroupSetBits_args(1, EVENT_TASK_FINISHED);
    verify_uart_flush_input_args(1);
    verify_xQueueReceive_args(4, desc->uart_queue, pdMS_TO_TICKS(SERIAL_EVENT_WAIT_TIMEOUT_MS));
    verify_malloc_tracking(2, 1);

    TEST_ASSERT_EQUAL_size_t_MESSAGE(
        sizeof(serial_desc_t),
        allocated_ptrs[0].size,
        "Memory size mismatch for serial_desc_t allocation"
    );
    TEST_ASSERT_EQUAL_size_t_MESSAGE(
        SERIAL_BUF_SIZE,
        allocated_ptrs[1].size,
        "Memory size mismatch for UART read buffer allocation"
    );
    TEST_ASSERT_TRUE_MESSAGE(
        was_ptr_freed(allocated_ptrs[1].ptr),
        "UART read buffer should be freed on task exit"
    );

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_vTaskDelete_data.called, "vTaskDelete should be called once");
    TEST_ASSERT_NULL_MESSAGE(mock_vTaskDelete_data.xTaskToDelete, "vTaskDelete should be called to delete self");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_uart_read_bytes_data.called, "uart_read_bytes should not be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_receive_handler_data.called, "Receive handler should not be called");
}

// Test receiving UART_DATA event with receive timeout flag set
void test_serial_init_success_with_uart_data_event_tout(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init with UART_DATA event - timeout_flag is set");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    size_t size_to_read = 10;

    uart_event_t event = {.type = UART_DATA, .size = size_to_read, .timeout_flag = true};
    void* events_arr[1] = {&event};
    size_t event_size_arr[1] = {sizeof(event)};
    mock_xQueueReceive_data.pvBuffer_arr = events_arr;
    mock_xQueueReceive_data.buffer_size_arr = event_size_arr;
    mock_xQueueReceive_data.array_len = 1;

    mock_xTaskCreate_data.self_execution = true;
    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED | EVENT_TASK_EXIT_REQ;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should return non-NULL descriptor on success");
    verify_uart_read_bytes_args(1, size_to_read, portMAX_DELAY);

    TEST_ASSERT_EQUAL_MESSAGE(
        1,
        mock_receive_handler_data.called,
        "Receive handler should be called once"
    );

    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        desc,
        mock_receive_handler_data.desc,
        "Receive handler should be called with correct serial descriptor"
    );

    TEST_ASSERT_EQUAL_size_t_MESSAGE(
        size_to_read,
        mock_receive_handler_data.len,
        "Receive handler should be called with correct data length"
    );

    TEST_ASSERT_EQUAL_STRING_LEN_MESSAGE(
        MOCK_DATA_FROM_UART_READ,
        mock_receive_handler_data.data,
        size_to_read,
        "Receive handler should be called with correct data"
    );
}

// Test receiving UART_DATA event with receive timeout flag cleared:
// bytes are accumulated but receive_handler must NOT be called until the idle timeout fires.
// This only applies when wait_for_idle=true (Modbus gateway mode).
// The test uses serial_test_run_uart_event_task() to allow setting wait_for_idle=true
// on the descriptor BEFORE the task processes any queued UART events.
void test_serial_init_success_with_uart_data_event_no_tout(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init with UART_DATA event - timeout_flag is not set");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    size_t size_to_read = 10;

    uart_event_t event = {.type = UART_DATA, .size = size_to_read, .timeout_flag = false};
    void* events_arr[1] = {&event};
    size_t event_size_arr[1] = {sizeof(event)};
    mock_xQueueReceive_data.pvBuffer_arr = events_arr;
    mock_xQueueReceive_data.buffer_size_arr = event_size_arr;
    mock_xQueueReceive_data.array_len = 1;

    // Do NOT run the task inline: we need to set wait_for_idle=true before the task sees events.
    mock_xTaskCreate_data.self_execution = false;
    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED;
    mock_xEventGroupWaitBits_data.set_event_on_call = 1;   // EXIT_REQ fires after 1 loop iteration
    mock_xEventGroupWaitBits_data.events_to_set = EVENT_TASK_EXIT_REQ;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should return non-NULL descriptor on success");

    // Enable Modbus gateway mode: handler only fires on idle timeout.
    desc->wait_for_idle = true;

    // Run the task now, after wait_for_idle has been configured.
    serial_test_run_uart_event_task(desc);

    verify_uart_read_bytes_args(1, size_to_read, portMAX_DELAY);

    // timeout_flag is NOT set and wait_for_idle=true: bytes are buffered, handler must NOT fire.
    TEST_ASSERT_EQUAL_MESSAGE(
        0,
        mock_receive_handler_data.called,
        "Receive handler must NOT be called when timeout_flag is false (mid-frame RXFIFO_FULL)"
    );
}

// Test receiving two UART_DATA events: first without timeout (mid-frame fragment),
// second with timeout (idle detected). Bytes accumulate across both events and
// receive_handler is called exactly once, after the second event, with all 30 bytes.
// Uses deferred task execution (wait_for_idle=true) to test Modbus RTU accumulation.
void test_serial_init_success_with_two_uart_data_events(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init with two UART_DATA events - timeout_flag is not set and then is set");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    uart_event_t event_1 = {.type = UART_DATA, .size = 10, .timeout_flag = false};
    uart_event_t event_2 = {.type = UART_DATA, .size = 20, .timeout_flag = true};
    void* events_arr[2] = {&event_1, &event_2};
    size_t event_size_arr[2] = {sizeof(event_1), sizeof(event_2)};
    mock_xQueueReceive_data.pvBuffer_arr = events_arr;
    mock_xQueueReceive_data.buffer_size_arr = event_size_arr;
    mock_xQueueReceive_data.array_len = 2;

    // Do NOT run the task inline: we need to set wait_for_idle=true before the task sees events.
    // set_event_on_call=3: serial_init uses call 0, task loop uses calls 1 and 2 (events),
    // then the empty-queue iteration uses call 3 (3>3? No wait — called becomes 4, 4>3 → exit).
    mock_xTaskCreate_data.self_execution = false;
    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED;
    mock_xEventGroupWaitBits_data.set_event_on_call = 3;
    mock_xEventGroupWaitBits_data.events_to_set = EVENT_TASK_EXIT_REQ;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should return non-NULL descriptor on success");

    // Enable Modbus gateway mode: accumulate bytes until idle timeout fires.
    desc->wait_for_idle = true;

    // Run the task now, after wait_for_idle has been configured.
    serial_test_run_uart_event_task(desc);

    verify_uart_read_bytes_args(2, 20, portMAX_DELAY);  // Last (second) read length should be 20 bytes

    // event_1 (no timeout, wait_for_idle=true): bytes buffered, handler NOT called.
    // event_2 (timeout set): handler called ONCE with accumulated 30 bytes (10 + 20).
    TEST_ASSERT_EQUAL_MESSAGE(
        1,
        mock_receive_handler_data.called,
        "Receive handler should be called once (only when timeout_flag is set)"
    );

    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        desc,
        mock_receive_handler_data.desc,
        "Receive handler should be called with correct serial descriptor"
    );

    // The single call delivers all accumulated bytes: event_1 (10) + event_2 (20) = 30.
    TEST_ASSERT_EQUAL_size_t_MESSAGE(
        30,
        mock_receive_handler_data.len,
        "Receive handler should receive all accumulated bytes from both events"
    );

    // Verify actual data content:
    //   read_1 (10 bytes at buf[0]): MOCK_DATA_FROM_UART_READ[0..9]  = "HELLO_WORL"
    //   read_2 (20 bytes at buf[10]): MOCK_DATA_FROM_UART_READ[0..19] = "HELLO_WORLD_FROM_MGE"
    //   combined (30 bytes): "HELLO_WORLHELLO_WORLD_FROM_MGE"
    static const char expected_buf2[] = "HELLO_WORLHELLO_WORLD_FROM_MGE";
    TEST_ASSERT_EQUAL_STRING_LEN_MESSAGE(
        expected_buf2,
        (const char *)mock_receive_handler_data.calls[0].data,
        30,
        "Receive handler should be called with correct accumulated data from both events"
    );
}

// Test receiving three UART_DATA events: first without timeout (mid-frame fragment),
// then two with timeout (each marks end of an RTU frame).
// Expected behaviour:
//   event_1 (no timeout): 10 bytes buffered, handler NOT called.
//   event_2 (timeout):    10 + 20 = 30 bytes delivered → handler call #1, buffer reset.
//   event_3 (timeout):    15 bytes delivered → handler call #2, buffer reset.
// Total: 2 handler calls.
// Uses deferred task execution (wait_for_idle=true) to test Modbus RTU accumulation.
void test_serial_init_success_with_three_uart_data_events(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init with three UART_DATA events - timeout_flag is not set and then is set twice");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    uart_event_t event_1 = {.type = UART_DATA, .size = 10, .timeout_flag = false};
    uart_event_t event_2 = {.type = UART_DATA, .size = 20, .timeout_flag = true};
    uart_event_t event_3 = {.type = UART_DATA, .size = 15, .timeout_flag = true};
    void* events_arr[3] = {&event_1, &event_2, &event_3};
    size_t event_size_arr[3] = {sizeof(event_1), sizeof(event_2), sizeof(event_3)};
    mock_xQueueReceive_data.pvBuffer_arr = events_arr;
    mock_xQueueReceive_data.buffer_size_arr = event_size_arr;
    mock_xQueueReceive_data.array_len = 3;

    // Do NOT run the task inline: we need to set wait_for_idle=true before the task sees events.
    // set_event_on_call=4: serial_init uses call 0, task loop uses calls 1-3 (events),
    // then the empty-queue iteration uses call 4 (called becomes 5, 5>4 → exit).
    mock_xTaskCreate_data.self_execution = false;
    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED;
    mock_xEventGroupWaitBits_data.set_event_on_call = 4;
    mock_xEventGroupWaitBits_data.events_to_set = EVENT_TASK_EXIT_REQ;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should return non-NULL descriptor on success");

    // Enable Modbus gateway mode: accumulate bytes until idle timeout fires.
    desc->wait_for_idle = true;

    // Run the task now, after wait_for_idle has been configured.
    serial_test_run_uart_event_task(desc);

    verify_uart_read_bytes_args(3, 15, portMAX_DELAY);  // Last (third) read length should be 15 bytes

    // 2 handler calls: once for event_2 (with 30 accumulated bytes), once for event_3 (15 bytes).
    TEST_ASSERT_EQUAL_MESSAGE(
        2,
        mock_receive_handler_data.called,
        "Receive handler should be called twice (once per timeout event)"
    );

    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        desc,
        mock_receive_handler_data.desc,
        "Receive handler should be called with correct serial descriptor"
    );

    // Last call (event_3): 15 bytes only (buffer was reset after event_2's delivery).
    TEST_ASSERT_EQUAL_size_t_MESSAGE(
        15,
        mock_receive_handler_data.len,
        "Last handler call should deliver event_3's 15 bytes"
    );

    // First call (event_2): accumulated 10 (event_1) + 20 (event_2) = 30 bytes.
    TEST_ASSERT_EQUAL_size_t_MESSAGE(
        30,
        mock_receive_handler_data.calls[0].len,
        "First call: receive handler should have accumulated 30 bytes (event_1 + event_2)"
    );

    // Second call (event_3): 15 bytes.
    TEST_ASSERT_EQUAL_size_t_MESSAGE(
        15,
        mock_receive_handler_data.calls[1].len,
        "Second call: receive handler should have event_3's 15 bytes"
    );

    // Verify actual data content for both calls:
    //   call #0: read_1(10 bytes at buf[0])="HELLO_WORL" + read_2(20 bytes at buf[10])="HELLO_WORLD_FROM_MGE"
    //   combined (30 bytes): "HELLO_WORLHELLO_WORLD_FROM_MGE"
    static const char expected_call0[] = "HELLO_WORLHELLO_WORLD_FROM_MGE";
    TEST_ASSERT_EQUAL_STRING_LEN_MESSAGE(
        expected_call0,
        (const char *)mock_receive_handler_data.calls[0].data,
        30,
        "First handler call should contain accumulated data from event_1 + event_2"
    );

    //   call #1: read_3(15 bytes at fresh buf[0])=MOCK_DATA_FROM_UART_READ[0..14]="HELLO_WORLD_FRO"
    static const char expected_call1[] = "HELLO_WORLD_FRO";
    TEST_ASSERT_EQUAL_STRING_LEN_MESSAGE(
        expected_call1,
        (const char *)mock_receive_handler_data.calls[1].data,
        15,
        "Second handler call should contain event_3's 15 bytes of data"
    );
}


// Test receiving UART_DATA event with size equal to the maximum buffer size
void test_serial_init_success_with_uart_data_event_buffer_max_size(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init with UART_DATA event - maximum buffer size");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    uart_event_t event = {.type = UART_DATA, .size = SERIAL_BUF_SIZE, .timeout_flag = true};
    void* events_arr[1] = {&event};
    size_t event_size_arr[1] = {sizeof(event)};
    mock_xQueueReceive_data.pvBuffer_arr = events_arr;
    mock_xQueueReceive_data.buffer_size_arr = event_size_arr;
    mock_xQueueReceive_data.array_len = 1;

    mock_xTaskCreate_data.self_execution = true;
    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED | EVENT_TASK_EXIT_REQ;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should return non-NULL descriptor on success");
    verify_uart_read_bytes_args(1, SERIAL_BUF_SIZE, portMAX_DELAY);

    TEST_ASSERT_EQUAL_MESSAGE(
        1,
        mock_receive_handler_data.called,
        "Receive handler should be called once"
    );

    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        desc,
        mock_receive_handler_data.desc,
        "Receive handler should be called with correct serial descriptor"
    );

    TEST_ASSERT_EQUAL_size_t_MESSAGE(
        SERIAL_BUF_SIZE,
        mock_receive_handler_data.len,
        "Receive handler should be called with correct data length"
    );
}

// Test receiving UART_DATA event with size larger than the buffer
void test_serial_init_success_with_uart_data_event_buffer_too_small(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init with UART_DATA event - buffer too small");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    uart_event_t event = {.type = UART_DATA, .size = SERIAL_BUF_SIZE + 1, .timeout_flag = true};
    void* events_arr[1] = {&event};
    size_t event_size_arr[1] = {sizeof(event)};
    mock_xQueueReceive_data.pvBuffer_arr = events_arr;
    mock_xQueueReceive_data.buffer_size_arr = event_size_arr;
    mock_xQueueReceive_data.array_len = 1;

    mock_xTaskCreate_data.self_execution = true;
    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED | EVENT_TASK_EXIT_REQ;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should return non-NULL descriptor on success");

    verify_uart_flush_input_args(2);
    TEST_ASSERT_EQUAL_MESSAGE(2, mock_xQueueReset_data.called, "xQueueReset should be called twice");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(desc->uart_queue, mock_xQueueReset_data.handle, "xQueueReset should be called with correct queue handle");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_uart_read_bytes_data.called, "uart_read_bytes should not be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_receive_handler_data.called, "Receive handler should not be called");
}

// Test: two UART_DATA events where the second event overflows the buffer.
// event_1 (no timeout, wait_for_idle=true) — bytes are buffered, handler NOT called.
// event_2 size (SERIAL_BUF_SIZE + 1) exceeds the full buffer size
// so the overflow path fires: flush + reset, no read, handler NOT called.
// Total handler calls: 0.
// Uses deferred task execution (wait_for_idle=true) to test Modbus RTU accumulation.
void test_serial_init_success_with_two_uart_data_events_buffer_overflow(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init with two UART_DATA events - buffer overflow on second event");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    // event_1: small, no timeout — bytes accumulated in buffer (wait_for_idle=true), handler NOT called.
    // event_2: larger than the full buffer — overflow triggers flush+reset, handler NOT called.
    uart_event_t event_1 = {.type = UART_DATA, .size = 10, .timeout_flag = false};
    uart_event_t event_2 = {.type = UART_DATA, .size = SERIAL_BUF_SIZE + 1, .timeout_flag = true};
    void* events_arr[2] = {&event_1, &event_2};
    size_t event_size_arr[2] = {sizeof(event_1), sizeof(event_2)};
    mock_xQueueReceive_data.pvBuffer_arr = events_arr;
    mock_xQueueReceive_data.buffer_size_arr = event_size_arr;
    mock_xQueueReceive_data.array_len = 2;

    // Do NOT run the task inline: we need to set wait_for_idle=true before the task sees events.
    mock_xTaskCreate_data.self_execution = false;
    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED;
    mock_xEventGroupWaitBits_data.set_event_on_call = 3;
    mock_xEventGroupWaitBits_data.events_to_set = EVENT_TASK_EXIT_REQ;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should return non-NULL descriptor on success");

    // Enable Modbus gateway mode: accumulate bytes until idle timeout fires.
    desc->wait_for_idle = true;

    // Run the task now, after wait_for_idle has been configured.
    serial_test_run_uart_event_task(desc);

    // event_1: uart_read_bytes called once (10 bytes), no handler call (no timeout, wait_for_idle=true).
    // event_2: overflow path — uart_read_bytes NOT called, flush+reset triggered.
    verify_uart_read_bytes_args(1, 10, portMAX_DELAY);

    verify_uart_flush_input_args(2);
    TEST_ASSERT_EQUAL_MESSAGE(2, mock_xQueueReset_data.called, "xQueueReset should be called twice");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(desc->uart_queue, mock_xQueueReset_data.handle, "xQueueReset should be called with correct queue handle");

    // Neither event triggered the handler: event_1 buffered (wait_for_idle=true, no timeout), event_2 hit overflow.
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_receive_handler_data.called, "Receive handler should not be called (event_1 buffered, event_2 overflowed)");
}

// Test receiving UART_FIFO_OVF event
void test_serial_init_success_with_uart_fifo_ovf_event(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init with UART_FIFO_OVF event");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    uart_event_t event = {.type = UART_FIFO_OVF, .size = 0, .timeout_flag = false};
    void* events_arr[1] = {&event};
    size_t event_size_arr[1] = {sizeof(event)};
    mock_xQueueReceive_data.pvBuffer_arr = events_arr;
    mock_xQueueReceive_data.buffer_size_arr = event_size_arr;
    mock_xQueueReceive_data.array_len = 1;

    mock_xTaskCreate_data.self_execution = true;
    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED | EVENT_TASK_EXIT_REQ;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should return non-NULL descriptor on success");

    verify_uart_flush_input_args(2);
    TEST_ASSERT_EQUAL_MESSAGE(2, mock_xQueueReset_data.called, "xQueueReset should be called twice");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(desc->uart_queue, mock_xQueueReset_data.handle, "xQueueReset should be called with correct queue handle");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_uart_read_bytes_data.called, "uart_read_bytes should not be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_receive_handler_data.called, "Receive handler should not be called");
}

// Test receiving UART_BUFFER_FULL event
void test_serial_init_success_with_uart_buffer_full_event(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init with UART_BUFFER_FULL event");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    uart_event_t event = {.type = UART_BUFFER_FULL, .size = 0, .timeout_flag = false};
    void* events_arr[1] = {&event};
    size_t event_size_arr[1] = {sizeof(event)};
    mock_xQueueReceive_data.pvBuffer_arr = events_arr;
    mock_xQueueReceive_data.buffer_size_arr = event_size_arr;
    mock_xQueueReceive_data.array_len = 1;

    mock_xTaskCreate_data.self_execution = true;
    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED | EVENT_TASK_EXIT_REQ;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should return non-NULL descriptor on success");

    verify_uart_flush_input_args(2);
    TEST_ASSERT_EQUAL_MESSAGE(2, mock_xQueueReset_data.called, "xQueueReset should be called twice");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(desc->uart_queue, mock_xQueueReset_data.handle, "xQueueReset should be called with correct queue handle");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_uart_read_bytes_data.called, "uart_read_bytes should not be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_receive_handler_data.called, "Receive handler should not be called");
}

// Test receiving UART_BREAK event
void test_serial_init_success_with_uart_break_event(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init with UART_BREAK event");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    uart_event_t event = {.type = UART_BREAK, .size = 0, .timeout_flag = false};
    void* events_arr[1] = {&event};
    size_t event_size_arr[1] = {sizeof(event)};
    mock_xQueueReceive_data.pvBuffer_arr = events_arr;
    mock_xQueueReceive_data.buffer_size_arr = event_size_arr;
    mock_xQueueReceive_data.array_len = 1;

    mock_xTaskCreate_data.self_execution = true;
    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED | EVENT_TASK_EXIT_REQ;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should return non-NULL descriptor on success");

    verify_uart_flush_input_args(1);
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xQueueReset_data.called, "xQueueReset should be called only once");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_uart_read_bytes_data.called, "uart_read_bytes should not be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_receive_handler_data.called, "Receive handler should not be called");
}

// Test receiving UART_PARITY_ERR event
void test_serial_init_success_with_uart_parity_err_event(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init with UART_PARITY_ERR event");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    uart_event_t event = {.type = UART_PARITY_ERR, .size = 0, .timeout_flag = false};
    void* events_arr[1] = {&event};
    size_t event_size_arr[1] = {sizeof(event)};
    mock_xQueueReceive_data.pvBuffer_arr = events_arr;
    mock_xQueueReceive_data.buffer_size_arr = event_size_arr;
    mock_xQueueReceive_data.array_len = 1;

    mock_xTaskCreate_data.self_execution = true;
    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED | EVENT_TASK_EXIT_REQ;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should return non-NULL descriptor on success");

    verify_uart_flush_input_args(1);
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xQueueReset_data.called, "xQueueReset should be called only once");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_uart_read_bytes_data.called, "uart_read_bytes should not be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_receive_handler_data.called, "Receive handler should not be called");
}

// Test receiving UART_FRAME_ERR event
void test_serial_init_success_with_uart_frame_err_event(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init with UART_FRAME_ERR event");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    uart_event_t event = {.type = UART_FRAME_ERR, .size = 0, .timeout_flag = false};
    void* events_arr[1] = {&event};
    size_t event_size_arr[1] = {sizeof(event)};
    mock_xQueueReceive_data.pvBuffer_arr = events_arr;
    mock_xQueueReceive_data.buffer_size_arr = event_size_arr;
    mock_xQueueReceive_data.array_len = 1;

    mock_xTaskCreate_data.self_execution = true;
    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED | EVENT_TASK_EXIT_REQ;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should return non-NULL descriptor on success");

    verify_uart_flush_input_args(1);
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xQueueReset_data.called, "xQueueReset should be called only once");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_uart_read_bytes_data.called, "uart_read_bytes should not be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_receive_handler_data.called, "Receive handler should not be called");
}

// Test receiving an unknown event
void test_serial_init_success_with_unknown_uart_event(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init with unknown UART event");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    uart_event_t event = {.type = 999, .size = 0, .timeout_flag = false};  // Unknown event type
    void* events_arr[1] = {&event};
    size_t event_size_arr[1] = {sizeof(event)};
    mock_xQueueReceive_data.pvBuffer_arr = events_arr;
    mock_xQueueReceive_data.buffer_size_arr = event_size_arr;
    mock_xQueueReceive_data.array_len = 1;

    mock_xTaskCreate_data.self_execution = true;
    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED | EVENT_TASK_EXIT_REQ;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should return non-NULL descriptor on success");

    verify_uart_flush_input_args(1);
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xQueueReset_data.called, "xQueueReset should be called only once");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_uart_read_bytes_data.called, "uart_read_bytes should not be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_receive_handler_data.called, "Receive handler should not be called");
}

// Test successful serial_send transmission
void test_serial_send_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_send success");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED;
    serial_desc_t *desc = serial_init(&config, mock_receive_handler);
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should succeed");

    uint8_t data[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
    size_t bytes_to_send = sizeof(data);
    esp_err_t err = serial_send(desc, data, bytes_to_send);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, err, "serial_send should return ESP_OK");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_uart_write_bytes_data.called, "uart_write_bytes should be called once");
    TEST_ASSERT_EQUAL_MESSAGE(UART_NUM_1, mock_uart_write_bytes_data.uart_num, "uart_write_bytes should be called with correct UART port");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(data, mock_uart_write_bytes_data.src, "uart_write_bytes should be called with correct data pointer");
    TEST_ASSERT_EQUAL_MESSAGE(bytes_to_send, mock_uart_write_bytes_data.size, "uart_write_bytes should be called with correct size");
}

// Test serial_send with a write error
void test_serial_send_partial_write(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_send with partial write");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED;
    serial_desc_t *desc = serial_init(&config, mock_receive_handler);
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should succeed");

    uint8_t data[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
    mock_uart_write_bytes_data.return_value = 3;

    esp_err_t err = serial_send(desc, data, sizeof(data));

    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, err, "serial_send should return ESP_FAIL on partial write");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_uart_write_bytes_data.called, "uart_write_bytes should be called once");
}


// Test serial_wait_tx_done
void test_serial_wait_tx_done_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_wait_tx_done");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED;
    serial_desc_t *desc = serial_init(&config, mock_receive_handler);
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should succeed");

    TickType_t timeout = pdMS_TO_TICKS(100);
    esp_err_t err = serial_wait_tx_done(desc, timeout);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, err, "serial_wait_tx_done should return ESP_OK");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_uart_wait_tx_done_data.called, "uart_wait_tx_done should be called once");
    TEST_ASSERT_EQUAL_MESSAGE(UART_NUM_1, mock_uart_wait_tx_done_data.uart_num, "uart_wait_tx_done should be called with correct UART port");
    TEST_ASSERT_EQUAL_MESSAGE(timeout, mock_uart_wait_tx_done_data.ticks_to_wait, "uart_wait_tx_done should be called with correct timeout");
}

// Test serial_wait_tx_done with errors
void test_serial_wait_tx_done_errors(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_wait_tx_done with errors");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED;
    serial_desc_t *desc = serial_init(&config, mock_receive_handler);
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should succeed");

    TickType_t timeout = pdMS_TO_TICKS(100);
    mock_uart_wait_tx_done_data.result = ESP_ERR_TIMEOUT;
    esp_err_t err = serial_wait_tx_done(desc, timeout);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_TIMEOUT, err, "serial_wait_tx_done should return ESP_ERR_TIMEOUT");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_uart_wait_tx_done_data.called, "uart_wait_tx_done should be called once");

    mock_uart_wait_tx_done_data.result = ESP_FAIL;
    err = serial_wait_tx_done(desc, timeout);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, err, "serial_wait_tx_done should return ESP_FAIL");
    TEST_ASSERT_EQUAL_MESSAGE(2, mock_uart_wait_tx_done_data.called, "uart_wait_tx_done should be called twice");
}

// Test serial_deinit with NULL descriptor
void test_serial_deinit_null_descriptor(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_deinit with NULL descriptor");
    LOG_MESSAGE();

    esp_err_t err = serial_deinit(NULL);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_INVALID_ARG, err, "serial_deinit should return ESP_ERR_INVALID_ARG for NULL descriptor");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xEventGroupSetBits_data.called, "xEventGroupSetBits should not be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xEventGroupWaitBits_data.called, "xEventGroupWaitBits should not be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_uart_driver_delete_data.called, "uart_driver_delete should not be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_vEventGroupDelete_data.called, "vEventGroupDelete should not be called");
    verify_malloc_tracking(0, 0);
}

// Test serial_deinit with an already deinitialized descriptor (task_handle == NULL)
void test_serial_deinit_already_deinitialized_task(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_deinit with already deinitialized descriptor (task_handle == NULL)");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);
    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should succeed");

    desc->task_handle = NULL;

    esp_err_t err = serial_deinit(desc);

    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_ERR_NOT_ALLOWED, err, "serial_deinit should return ESP_ERR_NOT_ALLOWED for already deinitialized descriptor"
    );
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xEventGroupSetBits_data.called, "xEventGroupSetBits should not be called");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xEventGroupWaitBits_data.called, "xEventGroupWaitBits should be called once in serial_init");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_uart_driver_delete_data.called, "uart_driver_delete should not be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_vEventGroupDelete_data.called, "vEventGroupDelete should not be called");
    verify_malloc_tracking(1, 0);
}

// Test serial_deinit with an already deinitialized descriptor (event_group == NULL)
void test_serial_deinit_already_deinitialized_event_group(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_deinit with already deinitialized descriptor (event_group == NULL)");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);
    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should succeed");

    desc->event_group = NULL;

    esp_err_t err = serial_deinit(desc);

    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_ERR_NOT_ALLOWED, err, "serial_deinit should return ESP_ERR_NOT_ALLOWED for already deinitialized descriptor"
    );
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xEventGroupSetBits_data.called, "xEventGroupSetBits should not be called");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xEventGroupWaitBits_data.called, "xEventGroupWaitBits should be called once in serial_init");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_uart_driver_delete_data.called, "uart_driver_delete should not be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_vEventGroupDelete_data.called, "vEventGroupDelete should not be called");
    verify_malloc_tracking(1, 0);
}

// Test serial_deinit successfully
void test_serial_deinit_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_deinit success");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);
    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED | EVENT_TASK_FINISHED;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should succeed");

    esp_err_t err = serial_deinit(desc);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, err, "serial_deinit should return ESP_OK");

    verify_xEventGroupSetBits_args(0, EVENT_TASK_EXIT_REQ);
    TEST_ASSERT_EQUAL_MESSAGE(2, mock_xEventGroupWaitBits_data.called, "xEventGroupWaitBits should be called twice");
    verify_xEventGroupWaitBits_args(1, EVENT_TASK_FINISHED, pdFALSE, pdTRUE, portMAX_DELAY);

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_uart_driver_delete_data.called, "uart_driver_delete should be called once");
    TEST_ASSERT_EQUAL_MESSAGE(UART_NUM_1, mock_uart_driver_delete_data.uart_num, "uart_driver_delete should be called with correct UART port");

    verify_event_group_create_delete_handlers();
    verify_malloc_tracking(1, 1);
}

// Test serial_deinit when the task does not finish in time
void test_serial_deinit_task_not_finished(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_deinit when task doesn't finish");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED;
    serial_desc_t *desc = serial_init(&config, mock_receive_handler);
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should succeed");

    mock_xEventGroupWaitBits_data.return_value = 0;

    esp_err_t err = serial_deinit(desc);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, err, "serial_deinit should return ESP_FAIL when task doesn't finish");

    verify_xEventGroupSetBits_args(0, EVENT_TASK_EXIT_REQ);
    TEST_ASSERT_EQUAL_MESSAGE(2, mock_xEventGroupWaitBits_data.called, "xEventGroupWaitBits should be called twice");
    verify_xEventGroupWaitBits_args(1, EVENT_TASK_FINISHED, pdFALSE, pdTRUE, portMAX_DELAY);

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_uart_driver_delete_data.called, "uart_driver_delete should not be called when task doesn't finish");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_vEventGroupDelete_data.called, "vEventGroupDelete should not be called when task doesn't finish");
    verify_malloc_tracking(1, 0);
}

// Test serial_set_rx_timeout — success path
void test_serial_set_rx_timeout_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_set_rx_timeout - success path");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);
    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should succeed");

    /* Reset the mock counter so only the set_rx_timeout call is counted */
    mock_uart_set_rx_timeout_data.called = 0;
    mock_uart_set_rx_timeout_data.result = ESP_OK;

    esp_err_t err = serial_set_rx_timeout(desc, 11);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, err, "serial_set_rx_timeout should return ESP_OK");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_uart_set_rx_timeout_data.called, "uart_set_rx_timeout should be called once");
    TEST_ASSERT_EQUAL_MESSAGE(UART_NUM_1, mock_uart_set_rx_timeout_data.uart_num, "uart_set_rx_timeout called with wrong uart_num");
    TEST_ASSERT_EQUAL_MESSAGE(11, mock_uart_set_rx_timeout_data.rx_timeout, "uart_set_rx_timeout called with wrong timeout value");

    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_FINISHED;
    serial_deinit(desc);
    verify_malloc_tracking(1, 1);
}

// Test serial_set_rx_timeout — failure path
void test_serial_set_rx_timeout_failure(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_set_rx_timeout - failure path");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);
    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should succeed");

    /* Reset the mock counter and configure it to return an error */
    mock_uart_set_rx_timeout_data.called = 0;
    mock_uart_set_rx_timeout_data.result = ESP_FAIL;

    esp_err_t err = serial_set_rx_timeout(desc, 11);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, err, "serial_set_rx_timeout should return ESP_FAIL on uart error");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_uart_set_rx_timeout_data.called, "uart_set_rx_timeout should be called once");

    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_FINISHED;
    serial_deinit(desc);
    verify_malloc_tracking(1, 1);
}

// Test sniffer mode (receive_handler == NULL): the sniff_handler must be delivered
// the accumulated packet ONLY when the idle timeout fires, never on a mid-frame
// (timeout_flag == false) UART_DATA fragment.
// Kills mutant M7: dropping "&& event.timeout_flag" from the sniffer branch would
// make the sniff_handler fire on every UART_DATA event (including non-timeout ones).
void test_serial_sniffer_delivers_only_on_idle_timeout(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial sniffer mode - sniff_handler fires only on idle timeout");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    // event_1: mid-frame fragment (no idle timeout) -> bytes buffered, sniff_handler NOT called.
    // event_2: idle timeout -> sniff_handler called ONCE with all accumulated bytes.
    uart_event_t event_1 = {.type = UART_DATA, .size = 10, .timeout_flag = false};
    uart_event_t event_2 = {.type = UART_DATA, .size = 20, .timeout_flag = true};
    void* events_arr[2] = {&event_1, &event_2};
    size_t event_size_arr[2] = {sizeof(event_1), sizeof(event_2)};
    mock_xQueueReceive_data.pvBuffer_arr = events_arr;
    mock_xQueueReceive_data.buffer_size_arr = event_size_arr;
    mock_xQueueReceive_data.array_len = 2;

    // Do NOT run the task inline: we need to install sniff_handler before the task sees events.
    mock_xTaskCreate_data.self_execution = false;
    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED;
    mock_xEventGroupWaitBits_data.set_event_on_call = 3;
    mock_xEventGroupWaitBits_data.events_to_set = EVENT_TASK_EXIT_REQ;

    // Sniffer mode: serial_init with NULL receive_handler.
    serial_desc_t *desc = serial_init(&config, NULL);
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should succeed in sniffer mode (NULL handler)");

    // Install the sniff_handler (reuse the receive-handler mock; same signature).
    desc->sniff_handler = mock_receive_handler;

    // Run the task now, after the sniff_handler has been configured.
    serial_test_run_uart_event_task(desc);

    verify_uart_read_bytes_args(2, 20, portMAX_DELAY);  // both events read (10 then 20)

    // event_1 (no timeout): buffered, sniff_handler NOT called.
    // event_2 (timeout): sniff_handler called exactly ONCE with all 30 accumulated bytes.
    TEST_ASSERT_EQUAL_MESSAGE(
        1,
        mock_receive_handler_data.called,
        "Sniff handler must fire ONCE, only on the idle-timeout event (not on the mid-frame fragment)"
    );

    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        desc,
        mock_receive_handler_data.desc,
        "Sniff handler should be called with correct serial descriptor"
    );

    TEST_ASSERT_EQUAL_size_t_MESSAGE(
        30,
        mock_receive_handler_data.len,
        "Sniff handler should receive all accumulated bytes (event_1 + event_2)"
    );
}

// Test serial_set_tx_disabled gates serial_send: when TX is disabled, serial_send
// returns ESP_OK immediately WITHOUT calling uart_write_bytes; re-enabling restores it.
// Kills mutant M8: inverting the "disabled == desc->tx_disabled" no-state-change guard
// would prevent the state transition, so the disable/enable would not take effect.
void test_serial_set_tx_disabled_gates_send(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_set_tx_disabled - gates serial_send transmission");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED;
    serial_desc_t *desc = serial_init(&config, mock_receive_handler);
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should succeed");

    uint8_t data[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};

    // Disable TX: this is a real state change (default tx_disabled == false).
    esp_err_t err = serial_set_tx_disabled(desc, true);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, err, "serial_set_tx_disabled(true) should return ESP_OK");

    // With TX disabled, serial_send returns ESP_OK but must NOT write to the UART.
    err = serial_send(desc, data, sizeof(data));
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, err, "serial_send should return ESP_OK when TX is disabled");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_uart_write_bytes_data.called,
        "uart_write_bytes must NOT be called while TX is disabled");

    // Re-enable TX: another real state change.
    err = serial_set_tx_disabled(desc, false);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, err, "serial_set_tx_disabled(false) should return ESP_OK");

    // Now serial_send must actually transmit.
    err = serial_send(desc, data, sizeof(data));
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, err, "serial_send should return ESP_OK after TX re-enabled");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_uart_write_bytes_data.called,
        "uart_write_bytes must be called once after TX re-enabled");
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_serial_init_null_config);
    RUN_TEST(test_serial_init_null_handler);
    RUN_TEST(test_serial_init_memory_allocation_failure);
    RUN_TEST(test_serial_init_event_group_create_failure);
    RUN_TEST(test_serial_init_uart_driver_install_failure);
    RUN_TEST(test_serial_init_uart_param_config_failure);
    RUN_TEST(test_serial_init_uart_set_pin_failure);
    RUN_TEST(test_serial_init_uart_set_mode_failure);
    RUN_TEST(test_serial_init_uart_set_rx_timeout_failure);
    RUN_TEST(test_serial_init_task_create_failure);

    RUN_TEST(test_serial_init_success_no_task_execution);
    RUN_TEST(test_serial_init_success_with_task_execution_no_uart_event);

    RUN_TEST(test_serial_init_success_with_uart_data_event_tout);
    RUN_TEST(test_serial_init_success_with_uart_data_event_no_tout);
    RUN_TEST(test_serial_init_success_with_two_uart_data_events);
    RUN_TEST(test_serial_init_success_with_three_uart_data_events);
    RUN_TEST(test_serial_init_success_with_uart_data_event_buffer_max_size);
    RUN_TEST(test_serial_init_success_with_uart_data_event_buffer_too_small);
    RUN_TEST(test_serial_init_success_with_two_uart_data_events_buffer_overflow);
    RUN_TEST(test_serial_init_success_with_uart_fifo_ovf_event);
    RUN_TEST(test_serial_init_success_with_uart_buffer_full_event);
    RUN_TEST(test_serial_init_success_with_uart_break_event);
    RUN_TEST(test_serial_init_success_with_uart_parity_err_event);
    RUN_TEST(test_serial_init_success_with_uart_frame_err_event);
    RUN_TEST(test_serial_init_success_with_unknown_uart_event);

    RUN_TEST(test_serial_sniffer_delivers_only_on_idle_timeout);

    RUN_TEST(test_serial_send_success);
    RUN_TEST(test_serial_send_partial_write);
    RUN_TEST(test_serial_set_tx_disabled_gates_send);

    RUN_TEST(test_serial_wait_tx_done_success);
    RUN_TEST(test_serial_wait_tx_done_errors);

    RUN_TEST(test_serial_deinit_null_descriptor);
    RUN_TEST(test_serial_deinit_already_deinitialized_task);
    RUN_TEST(test_serial_deinit_already_deinitialized_event_group);
    RUN_TEST(test_serial_deinit_success);
    RUN_TEST(test_serial_deinit_task_not_finished);

    RUN_TEST(test_serial_set_rx_timeout_success);
    RUN_TEST(test_serial_set_rx_timeout_failure);

    return UNITY_END();
}
