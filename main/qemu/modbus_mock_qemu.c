#include "modbus_mock_qemu.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Task configuration
// Raised from 2048 to match SERIAL_TASK_STACK_SIZE (serial.c): the mock task calls
// sniffer_process() which allocates two sniff_packet_t + stream_frame_t[16] + recursion
// (~700+ bytes). Real hardware runs sniffer_process in the UART event task (4096);
// the mock must match to avoid stack overflow in "modbus_mock" task.
#define MOCK_TASK_STACK_SIZE  4096
#define MOCK_TASK_PRIORITY    5
#define MOCK_TASK_NAME        "modbus_mock"

// Timing constants (milliseconds)
#define DELAY_CYCLE_MS        500  // Period between successive exchange cycles

// Bad-CRC injection frequency: inject a corrupt packet every N live cycles
#define BAD_CRC_PERIOD        5

// Modbus slave parameters for simulated device
#define MOCK_SLAVE_ADDR       1

// Live registers: refreshed periodically every DELAY_CYCLE_MS
#define MOCK_LIVE_START       0
#define MOCK_LIVE_COUNT       5
#define MOCK_LIVE_BASE        1000  // register[i] = MOCK_LIVE_BASE + i

// Static registers: injected once at startup, then age naturally
#define MOCK_STATIC_START     5
#define MOCK_STATIC_COUNT     5
#define MOCK_STATIC_BASE      2000  // register[i] = MOCK_STATIC_BASE + i

// FC03 packet sizes:
//   Request:  slave(1) + fc(1) + addr_hi(1) + addr_lo(1) + count_hi(1) + count_lo(1) + crc(2) = 8 bytes
//   Response: slave(1) + fc(1) + byte_count(1) + data(2*N) + crc(2)    = 5 + 2*N bytes
#define FC03_REQ_LEN          8
#define FC03_LIVE_RESP_LEN    (5 + MOCK_LIVE_COUNT * 2)    // 15 bytes for N=5
#define FC03_STATIC_RESP_LEN  (5 + MOCK_STATIC_COUNT * 2)  // 15 bytes for N=5
// Maximum FC03 response buffer size (covers both live and static register responses)
#define FC03_RESP_BUF_LEN     (FC03_LIVE_RESP_LEN > FC03_STATIC_RESP_LEN \
                                ? FC03_LIVE_RESP_LEN : FC03_STATIC_RESP_LEN)

static const char *TAG = "modbus_mock";
static TaskHandle_t s_mock_task_handle = NULL;

// ----------------------------------------------------------------------------
// CRC-16/Modbus: polynomial 0xA001, initial value 0xFFFF, LSB-first.
// Implemented locally to avoid coupling with modbus_helpers.h.
// ----------------------------------------------------------------------------
static uint16_t modbus_crc16_calc(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }

    return crc;
}

// ----------------------------------------------------------------------------
// Build a FC03 Read Holding Registers request frame into buf (must be >= 8 bytes).
// Layout: [slave][0x03][addr_hi][addr_lo][count_hi][count_lo][crc_lo][crc_hi]
// ----------------------------------------------------------------------------
static void build_fc03_request(uint8_t *buf, uint8_t slave, uint16_t start_addr, uint16_t reg_count)
{
    buf[0] = slave;
    buf[1] = 0x03;                           // FC03 – Read Holding Registers
    buf[2] = (uint8_t)(start_addr >> 8);
    buf[3] = (uint8_t)(start_addr & 0xFF);
    buf[4] = (uint8_t)(reg_count >> 8);
    buf[5] = (uint8_t)(reg_count & 0xFF);

    uint16_t crc = modbus_crc16_calc(buf, 6);
    buf[6] = (uint8_t)(crc & 0xFF);         // CRC low byte first (Modbus convention)
    buf[7] = (uint8_t)(crc >> 8);
}

// ----------------------------------------------------------------------------
// Build a FC03 response frame into buf (must be >= FC03_RESP_LEN bytes).
// Layout: [slave][0x03][byte_count][val0_hi][val0_lo]...[crc_lo][crc_hi]
// Values: register[i] = base_value + i
// ----------------------------------------------------------------------------
static void build_fc03_response(uint8_t *buf, uint8_t slave, uint16_t reg_count, uint16_t base_value)
{
    uint8_t byte_count = (uint8_t)(reg_count * 2);

    buf[0] = slave;
    buf[1] = 0x03;
    buf[2] = byte_count;

    for (uint16_t i = 0; i < reg_count; i++) {
        uint16_t value = base_value + i;
        buf[3 + (i * 2)]     = (uint8_t)(value >> 8);
        buf[3 + (i * 2) + 1] = (uint8_t)(value & 0xFF);
    }

    size_t data_len = (size_t)(3 + byte_count);
    uint16_t crc = modbus_crc16_calc(buf, data_len);
    buf[data_len]     = (uint8_t)(crc & 0xFF);
    buf[data_len + 1] = (uint8_t)(crc >> 8);
}

// ----------------------------------------------------------------------------
// Background task: periodically injects Modbus RTU byte streams into the
// sniffer via serial_desc->sniff_handler.
//
// Both the one-shot static registration and the periodic live registration use
// concatenated byte stream injection — request and response bytes are combined
// into a single buffer and delivered to sniff_handler in one call (no
// inter-frame gap between them). This exercises the stream_splitter path in
// sniffer_process(), which handles buffers containing multiple Modbus frames.
//
// Every BAD_CRC_PERIOD live cycles an additional packet with an intentionally
// corrupted CRC is injected before the valid stream. The sniffer must emit it
// with crc_valid=false and continue parsing subsequent valid data normally.
// ----------------------------------------------------------------------------
static void modbus_mock_task(void *arg)
{
    serial_desc_t *desc = (serial_desc_t *)arg;

    // Stack-local packet buffers — no heap allocation inside the loop.
    uint8_t req_buf[FC03_REQ_LEN];
    uint8_t resp_buf[FC03_RESP_BUF_LEN]; /* sized to the largest possible FC03 response */

    // Concatenated static registration buffer (req + resp as one stream).
    uint8_t static_concat_buf[FC03_REQ_LEN + FC03_STATIC_RESP_LEN];

    // Concatenated live exchange buffer (req + resp as one stream).
    uint8_t live_concat_buf[FC03_REQ_LEN + FC03_LIVE_RESP_LEN];

    // Bad-CRC packet: slave=1, FC03, addr=0x0000, count=0x0001, CRC=0xFFFF (wrong).
    // Correct CRC for this payload would be 0x840A; 0xFFFF is intentionally invalid.
    static uint8_t bad_crc_pkt[FC03_REQ_LEN] = {
        0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0xFF, 0xFF
    };

    int cycle_count = 0;

    ESP_LOGI(TAG, "Modbus mock task started, waiting for sniff_handler...");

    // Wait until sniffer_attach() installs sniff_handler.
    while (desc->sniff_handler == NULL) {
        vTaskDelay(pdMS_TO_TICKS(DELAY_CYCLE_MS));
    }

    // --- One-shot: inject static registers (5-9) as a single concatenated byte stream ---
    build_fc03_request(req_buf, MOCK_SLAVE_ADDR, MOCK_STATIC_START, MOCK_STATIC_COUNT);
    build_fc03_response(resp_buf, MOCK_SLAVE_ADDR, MOCK_STATIC_COUNT, MOCK_STATIC_BASE + MOCK_STATIC_START);
    memcpy(static_concat_buf, req_buf, FC03_REQ_LEN);
    memcpy(static_concat_buf + FC03_REQ_LEN, resp_buf, FC03_STATIC_RESP_LEN);
    desc->sniff_handler(desc, static_concat_buf, sizeof(static_concat_buf));
    ESP_LOGI(TAG, "Static registers injected as single stream (slave=%d, regs %d..%d)",
             MOCK_SLAVE_ADDR, MOCK_STATIC_START, MOCK_STATIC_START + MOCK_STATIC_COUNT - 1);

    // --- Periodic: refresh live registers (0-4) every DELAY_CYCLE_MS ---
    for (;;) {
        cycle_count++;

        // Every BAD_CRC_PERIOD cycles inject a corrupt-CRC packet first so that
        // the Python test can observe at least one packet with crc_valid=false.
        if ((cycle_count % BAD_CRC_PERIOD) == 0) {
            desc->sniff_handler(desc, bad_crc_pkt, sizeof(bad_crc_pkt));
            ESP_LOGD(TAG, "Bad-CRC packet injected (cycle %d)", cycle_count);
        }

        // Build req and resp, then concatenate into a single buffer and inject
        // as one sniff_handler call — no inter-frame gap between the two frames.
        // This forces sniffer_process() to use the stream_splitter path.
        build_fc03_request(req_buf, MOCK_SLAVE_ADDR, MOCK_LIVE_START, MOCK_LIVE_COUNT);
        build_fc03_response(resp_buf, MOCK_SLAVE_ADDR, MOCK_LIVE_COUNT, MOCK_LIVE_BASE + MOCK_LIVE_START);
        memcpy(live_concat_buf, req_buf, FC03_REQ_LEN);
        memcpy(live_concat_buf + FC03_REQ_LEN, resp_buf, FC03_LIVE_RESP_LEN);
        desc->sniff_handler(desc, live_concat_buf, sizeof(live_concat_buf));

        // Wait before the next cycle.
        vTaskDelay(pdMS_TO_TICKS(DELAY_CYCLE_MS));
    }
}

// ----------------------------------------------------------------------------
// Public API
// ----------------------------------------------------------------------------
void modbus_mock_qemu_start(serial_desc_t *serial_desc)
{
    if (s_mock_task_handle != NULL) {
        /* Task is already running — skip to avoid duplicate tasks and handle leak. */
        ESP_LOGW(TAG, "Modbus RTU mock already running, skipping start");
        return;
    }

    ESP_LOGI(TAG, "Starting Modbus RTU mock (QEMU build)");

    BaseType_t ret = xTaskCreate(modbus_mock_task,
                                 MOCK_TASK_NAME,
                                 MOCK_TASK_STACK_SIZE,
                                 serial_desc,
                                 MOCK_TASK_PRIORITY,
                                 &s_mock_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Modbus mock task");
        s_mock_task_handle = NULL;
    }
}

void modbus_mock_qemu_stop(void)
{
    if (s_mock_task_handle != NULL) {
        vTaskDelete(s_mock_task_handle);
        s_mock_task_handle = NULL;
        ESP_LOGI(TAG, "Modbus RTU mock task stopped");
    }
}
