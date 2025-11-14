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
#define SERIAL_READ_TOUT                10
#define SERIAL_TASK_STACK_SIZE          (1024 * 4)
#define SERIAL_TASK_PRIORITY            12
#define SERIAL_QUEUE_SIZE               20

#define EVENT_TASK_STARTED              BIT0
#define EVENT_TASK_FINISHED             BIT1
#define EVENT_TASK_EXIT_REQ             BIT8

#define SERIAL_EVENT_WAIT_TIMEOUT_MS    50

typedef struct {
    int called;
    serial_desc_t *desc;
    uint8_t *data;
    size_t len;
} mock_receive_handler_t;

mock_receive_handler_t mock_receive_handler_data = {0};

void setUp(void)
{
    mock_uart_reset();
    mock_freertos_event_groups_reset();
    mock_freertos_task_reset();
    mock_freertos_queue_reset();
    reset_malloc_tracking();
}

void tearDown(void)
{

}

static void mock_receive_handler(serial_desc_t *desc, uint8_t *data, size_t len)
{
    mock_receive_handler_data.called++;
    mock_receive_handler_data.desc = desc;
    mock_receive_handler_data.data = data;
    mock_receive_handler_data.len = len;
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

static void verify_uart_set_rx_timeout_args(void)
{
    TEST_ASSERT_EQUAL_MESSAGE(
        UART_NUM_1,
        mock_uart_set_rx_timeout_data.uart_num,
        "uart_set_rx_timeout should be called with correct UART port number"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        SERIAL_READ_TOUT,
        mock_uart_set_rx_timeout_data.rx_timeout,
        "uart_set_rx_timeout should be called with SERIAL_READ_TOUT"
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
    QueueHandle_t expected_queue,
    TickType_t expected_ticks
)
{
    TEST_ASSERT_EQUAL_MESSAGE(
        1,
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

// Тестируем serial_init с NULL serial_config
void test_serial_init_null_config(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init with NULL config");
    LOG_MESSAGE();

    serial_desc_t *desc = serial_init(NULL, mock_receive_handler);

    TEST_ASSERT_NULL_MESSAGE(desc, "serial_init should return NULL when config is NULL");
    verify_serial_init_calls(0, 0, 0, 0, 0, 0, 0, 0);
    verify_malloc_tracking(0, 0);
}

// Тестируем serial_init с NULL serial_receive_handler
void test_serial_init_null_handler(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init with NULL receive handler");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    serial_desc_t *desc = serial_init(&config, NULL);

    TEST_ASSERT_NULL_MESSAGE(desc, "serial_init should return NULL when handler is NULL");
    verify_serial_init_calls(0, 0, 0, 0, 0, 0, 0, 0);
    verify_malloc_tracking(0, 0);
}

// Тестируем serial_init с ошибкой при выделении памяти для serial_desc_t
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
    verify_serial_init_calls(0, 0, 0, 0, 0, 0, 0, 0);
    verify_malloc_tracking(0, 0);
}

// Тестируем serial_init с ошибкой создания xEventGroupCreate
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
    verify_serial_init_calls(0, 0, 0, 0, 0, 0, 0, 0);
    verify_malloc_tracking(1, 1);

    TEST_ASSERT_EQUAL_size_t_MESSAGE(
        sizeof(serial_desc_t),
        allocated_ptrs[0].size,
        "Memory size mismatch for serial_desc_t allocation"
    );
}

// Тестируем serial_init с ошибкой вызова uart_driver_install
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
    verify_serial_init_calls(1, 0, 0, 0, 0, 0, 0, 0);
    verify_event_group_create_delete_handlers();
    verify_uart_driver_install_args();
    verify_malloc_tracking(1, 1);
}

// Тестируем serial_init с ошибкой вызова uart_param_config
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
    verify_serial_init_calls(1, 1, 0, 0, 0, 1, 0, 0);
    verify_event_group_create_delete_handlers();
    verify_uart_driver_install_args();
    verify_uart_param_config_args(MOCK_DEFAULT_BAUDRATE, UART_DATA_8_BITS, UART_PARITY_DISABLE, UART_STOP_BITS_2);
    verify_malloc_tracking(1, 1);
}

// Тестируем serial_init с ошибкой вызова uart_set_pin
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
    verify_serial_init_calls(1, 1, 1, 0, 0, 1, 0, 0);
    verify_event_group_create_delete_handlers();
    verify_uart_driver_install_args();
    verify_uart_param_config_args(MOCK_DEFAULT_BAUDRATE, UART_DATA_8_BITS, UART_PARITY_DISABLE, UART_STOP_BITS_2);
    verify_uart_set_pin_args(GPIO_NUM_10, GPIO_NUM_9, GPIO_NUM_4);
    verify_malloc_tracking(1, 1);
}

// Тестируем serial_init с ошибкой вызова uart_set_mode
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
    verify_serial_init_calls(1, 1, 1, 1, 0, 1, 0, 0);
    verify_event_group_create_delete_handlers();
    verify_uart_driver_install_args();
    verify_uart_param_config_args(MOCK_DEFAULT_BAUDRATE, UART_DATA_8_BITS, UART_PARITY_DISABLE, UART_STOP_BITS_2);
    verify_uart_set_pin_args(GPIO_NUM_10, GPIO_NUM_9, GPIO_NUM_4);
    verify_uart_set_mode_args();
    verify_malloc_tracking(1, 1);
}

// Тестируем serial_init с ошибкой вызова uart_set_rx_timeout
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
    verify_serial_init_calls(1, 1, 1, 1, 1, 1, 0, 0);
    verify_event_group_create_delete_handlers();
    verify_uart_driver_install_args();
    verify_uart_param_config_args(MOCK_DEFAULT_BAUDRATE, UART_DATA_8_BITS, UART_PARITY_DISABLE, UART_STOP_BITS_2);
    verify_uart_set_pin_args(GPIO_NUM_10, GPIO_NUM_9, GPIO_NUM_4);
    verify_uart_set_mode_args();
    verify_uart_set_rx_timeout_args();
    verify_malloc_tracking(1, 1);
}

// Тестируем serial_init с ошибкой создания задачи
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
    verify_serial_init_calls(1, 1, 1, 1, 1, 1, 1, 0);
    verify_event_group_create_delete_handlers();
    verify_uart_driver_install_args();
    verify_uart_param_config_args(MOCK_DEFAULT_BAUDRATE, UART_DATA_8_BITS, UART_PARITY_DISABLE, UART_STOP_BITS_2);
    verify_uart_set_pin_args(GPIO_NUM_10, GPIO_NUM_9, GPIO_NUM_4);
    verify_uart_set_mode_args();
    verify_uart_set_rx_timeout_args();
    verify_task_created();
    verify_malloc_tracking(1, 1);
}

// Тестируем инициализацию serial_init без фактического запуска задачи
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
    verify_serial_init_calls(1, 1, 1, 1, 1, 0, 1, 1);
    verify_uart_driver_install_args();
    verify_uart_param_config_args(MOCK_DEFAULT_BAUDRATE, UART_DATA_8_BITS, UART_PARITY_DISABLE, UART_STOP_BITS_2);
    verify_uart_set_pin_args(GPIO_NUM_10, GPIO_NUM_9, GPIO_NUM_4);
    verify_uart_set_mode_args();
    verify_uart_set_rx_timeout_args();
    verify_task_created();
    verify_xEventGroupWaitBits_args(0, EVENT_TASK_STARTED, pdFALSE, pdTRUE, portMAX_DELAY);
    verify_malloc_tracking(1, 0);
}

// Тестируем инициализацию serial_init c запуском задачи и возникновением события EVENT_TASK_EXIT_REQ,
// без получения данных по UART
void test_serial_init_success_with_task_execution_no_uart_event(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init success with task execution and no UART event");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    mock_xTaskCreate_data.self_execution = true;
    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED | EVENT_TASK_EXIT_REQ;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should return non-NULL descriptor on success");

    verify_serial_init_calls(1, 1, 1, 1, 1, 0, 1, 2);
    verify_xEventGroupWaitBits_args(0, EVENT_TASK_EXIT_REQ, pdFALSE, pdTRUE, 0);
    verify_xEventGroupWaitBits_args(1, EVENT_TASK_STARTED, pdFALSE, pdTRUE, portMAX_DELAY);
    TEST_ASSERT_EQUAL_MESSAGE(2, mock_xEventGroupSetBits_data.called, "xEventGroupSetBits should be called twice");
    verify_xEventGroupSetBits_args(0, EVENT_TASK_STARTED);
    verify_xEventGroupSetBits_args(1, EVENT_TASK_FINISHED);
    verify_uart_flush_input_args(1);
    verify_xQueueReceive_args(desc->uart_queue, pdMS_TO_TICKS(SERIAL_EVENT_WAIT_TIMEOUT_MS));
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
}

// Тестируем получение события UART_DATA
void test_serial_init_success_with_uart_data_event(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init with UART_DATA event");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    size_t size_to_read = 10;

    uart_event_t event = {.type = UART_DATA, .size = size_to_read, .timeout_flag = false};
    mock_xQueueReceive_data.pvBuffer = &event;
    mock_xQueueReceive_data.buffer_size = sizeof(event);

    mock_xTaskCreate_data.self_execution = true;
    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED | EVENT_TASK_EXIT_REQ;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should return non-NULL descriptor on success");

    verify_uart_read_bytes_args(1, size_to_read, portMAX_DELAY);
}

// Тестируем получение события UART_DATA с включенной защитой копирования
void test_serial_init_success_with_uart_data_event_copy_protection(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init with UART_DATA event and copy protection");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    size_t size_to_read = 10;

    uart_event_t event = {.type = UART_DATA, .size = size_to_read, .timeout_flag = false};
    mock_xQueueReceive_data.pvBuffer = &event;
    mock_xQueueReceive_data.buffer_size = sizeof(event);

    mock_xTaskCreate_data.self_execution = true;
    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED | EVENT_TASK_EXIT_REQ;

    serial_activate_copy_protection();

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should return non-NULL descriptor on success");

    verify_uart_read_bytes_args(1, size_to_read, portMAX_DELAY);
}

// Тестируем получение события UART_DATA с размером больше буфера
void test_serial_init_success_with_uart_data_event_buffer_too_small(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init with UART_DATA event - buffer too small");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    uart_event_t event = {.type = UART_DATA, .size = SERIAL_BUF_SIZE + 100, .timeout_flag = false};
    mock_xQueueReceive_data.pvBuffer = &event;
    mock_xQueueReceive_data.buffer_size = sizeof(event);

    mock_xTaskCreate_data.self_execution = true;
    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED | EVENT_TASK_EXIT_REQ;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should return non-NULL descriptor on success");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_uart_read_bytes_data.called, "uart_read_bytes should not be called");
}

// Тестируем получение события UART_FIFO_OVF
void test_serial_init_success_with_uart_fifo_ovf_event(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init with UART_FIFO_OVF event");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    uart_event_t event = {.type = UART_FIFO_OVF, .size = 0, .timeout_flag = false};
    mock_xQueueReceive_data.pvBuffer = &event;
    mock_xQueueReceive_data.buffer_size = sizeof(event);

    mock_xTaskCreate_data.self_execution = true;
    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED | EVENT_TASK_EXIT_REQ;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should return non-NULL descriptor on success");

    verify_uart_flush_input_args(2);
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xQueueReset_data.called, "xQueueReset should be called once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(desc->uart_queue, mock_xQueueReset_data.handle, "xQueueReset should be called with correct queue handle");
}

// Тестируем получение события UART_BUFFER_FULL
void test_serial_init_success_with_uart_buffer_full_event(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init with UART_BUFFER_FULL event");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    uart_event_t event = {.type = UART_BUFFER_FULL, .size = 0, .timeout_flag = false};
    mock_xQueueReceive_data.pvBuffer = &event;
    mock_xQueueReceive_data.buffer_size = sizeof(event);

    mock_xTaskCreate_data.self_execution = true;
    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED | EVENT_TASK_EXIT_REQ;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should return non-NULL descriptor on success");

    verify_uart_flush_input_args(2);
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xQueueReset_data.called, "xQueueReset should be called once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(desc->uart_queue, mock_xQueueReset_data.handle, "xQueueReset should be called with correct queue handle");
}

// Тестируем получение события UART_BREAK
void test_serial_init_success_with_uart_break_event(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init with UART_BREAK event");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    uart_event_t event = {.type = UART_BREAK, .size = 0, .timeout_flag = false};
    mock_xQueueReceive_data.pvBuffer = &event;
    mock_xQueueReceive_data.buffer_size = sizeof(event);

    mock_xTaskCreate_data.self_execution = true;
    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED | EVENT_TASK_EXIT_REQ;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should return non-NULL descriptor on success");
    verify_uart_flush_input_args(1);
}

// Тестируем получение события UART_PARITY_ERR
void test_serial_init_success_with_uart_parity_err_event(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init with UART_PARITY_ERR event");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    uart_event_t event = {.type = UART_PARITY_ERR, .size = 0, .timeout_flag = false};
    mock_xQueueReceive_data.pvBuffer = &event;
    mock_xQueueReceive_data.buffer_size = sizeof(event);

    mock_xTaskCreate_data.self_execution = true;
    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED | EVENT_TASK_EXIT_REQ;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should return non-NULL descriptor on success");
    verify_uart_flush_input_args(1);
}

// Тестируем получение события UART_FRAME_ERR
void test_serial_init_success_with_uart_frame_err_event(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init with UART_FRAME_ERR event");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    uart_event_t event = {.type = UART_FRAME_ERR, .size = 0, .timeout_flag = false};
    mock_xQueueReceive_data.pvBuffer = &event;
    mock_xQueueReceive_data.buffer_size = sizeof(event);

    mock_xTaskCreate_data.self_execution = true;
    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED | EVENT_TASK_EXIT_REQ;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should return non-NULL descriptor on success");
    verify_uart_flush_input_args(1);
}

// Тестируем получение неизвестного события
void test_serial_init_success_with_unknown_uart_event(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial_init with unknown UART event");
    LOG_MESSAGE();

    serial_config_t config;
    init_default_config(&config);

    uart_event_t event = {.type = 999, .size = 0, .timeout_flag = false};  // Unknown event type
    mock_xQueueReceive_data.pvBuffer = &event;
    mock_xQueueReceive_data.buffer_size = sizeof(event);

    mock_xTaskCreate_data.self_execution = true;
    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_STARTED | EVENT_TASK_EXIT_REQ;

    serial_desc_t *desc = serial_init(&config, mock_receive_handler);
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_init should return non-NULL descriptor on success");
    verify_uart_flush_input_args(1);
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

    RUN_TEST(test_serial_init_success_with_uart_data_event);
    RUN_TEST(test_serial_init_success_with_uart_data_event_copy_protection);
    RUN_TEST(test_serial_init_success_with_uart_data_event_buffer_too_small);
    RUN_TEST(test_serial_init_success_with_uart_fifo_ovf_event);
    RUN_TEST(test_serial_init_success_with_uart_buffer_full_event);
    RUN_TEST(test_serial_init_success_with_uart_break_event);
    RUN_TEST(test_serial_init_success_with_uart_parity_err_event);
    RUN_TEST(test_serial_init_success_with_uart_frame_err_event);
    RUN_TEST(test_serial_init_success_with_unknown_uart_event);

    return UNITY_END();
}
