#ifdef __unittest_env__
    #define malloc test_malloc
    #define free test_free
#endif

#include "serial.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_bit_defs.h"


// Buffer must be larger than the maximum Modbus packet size + fast Modbus arbitration bytes
// However, since the device can work in "transparent" gateway mode, the buffer size should be chosen with a margin
// When the buffer overflows, UART_BUFFER_FULL event will occur
#define SERIAL_BUF_SIZE                 (1000)
#define SERIAL_TASK_STACK_SIZE          (1024 * 4)
#define SERIAL_TASK_PRIORITY            12
#define SERIAL_QUEUE_SIZE               20          // UART event queue size

#define EVENT_TASK_STARTED              BIT0
#define EVENT_TASK_FINISHED             BIT1
#define EVENT_TASK_EXIT_REQ             BIT8

#define SERIAL_EVENT_WAIT_TIMEOUT_MS    50


static const char *TAG = "serial";


typedef struct {
    uint8_t *data;        // bridge path buffer (unchanged)
    size_t data_len;      // bridge path length
    uint8_t *sniff_data;  // independent sniffer accumulator (additive overlay)
    size_t sniff_len;     // sniffer accumulator length
} buffer_ctx_t;


// Report an RX-stage loss (driver-ring / FIFO overflow) then flush the driver ring and
// reset the SW buffers. ring_len is read BEFORE the flush; the uart_get_buffered_data_len
// return value is intentionally ignored (best-effort: ring_len stays 0 if it fails, so the
// count just omits the ring contents). Only the count differs from the UART_DATA overflow
// path, which reports data_len + event.size instead and stays inline.
static void serial_drop_and_flush(serial_desc_t *desc, buffer_ctx_t *buffer_ctx)
{
    if (desc->drop_handler) {
        size_t ring_len = 0;
        uart_get_buffered_data_len(desc->port_num, &ring_len);
        desc->drop_handler(desc, buffer_ctx->data_len + ring_len);
    }
    uart_flush_input(desc->port_num);
    xQueueReset(desc->uart_queue);
    buffer_ctx->data_len = 0;
    buffer_ctx->sniff_len = 0;
}

static void handle_uart_event(serial_desc_t *desc, uart_event_t event, buffer_ctx_t *buffer_ctx)
{
    switch (event.type) {
        case UART_DATA: {
            int free_space = (int)SERIAL_BUF_SIZE - (int)buffer_ctx->data_len;
            if (free_space < (int)event.size) {
                ESP_LOGE(TAG, "UART[%d] receive buffer overflow, free: %d, expected: >= %zu", desc->port_num, free_space, event.size);
                // RX-stage loss: the buffered partial frame plus the incoming chunk that did not fit
                // are both discarded by uart_flush_input below. Report them so they are counted as dropped.
                if (desc->drop_handler) {
                    desc->drop_handler(desc, buffer_ctx->data_len + event.size);
                }
                uart_flush_input(desc->port_num);
                xQueueReset(desc->uart_queue);
                buffer_ctx->data_len = 0;
                buffer_ctx->sniff_len = 0;
                break;
            }
            ESP_LOGD(TAG, "UART[%d] DATA: %zu, TIMEOUT: %u", desc->port_num, event.size, (unsigned)event.timeout_flag);
            size_t old_len = buffer_ctx->data_len;
            uart_read_bytes(desc->port_num, &buffer_ctx->data[old_len], event.size, portMAX_DELAY);
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, &buffer_ctx->data[old_len], event.size, ESP_LOG_DEBUG);
            buffer_ctx->data_len += event.size;

            // (A) Sniffer feed — additive overlay, independent of the bridge path.
            // Accumulate every received byte into a dedicated buffer and deliver an
            // idle-delimited frame on timeout_flag. This works identically in passive
            // and tcp_bridge modes (transparent or Modbus gateway): the sniffer/cache
            // observe RX traffic without ever altering the bytes/timing the bridge
            // forwards. The sniffer_process()/cache work it triggers is gated
            // by the reasons bitmask, so an idle bridge with no overlay does no real work.
            //
            // sniff_handler is published by sniffer_attach() with a release store; load it
            // ONCE with a matching acquire load and use only the local copy for both the NULL
            // check and the call. Reading desc->sniff_handler twice would let the compiler
            // reload it after the memcpy (which may alias the descriptor).
            // The teardown ordering makes this fully safe: port_manager calls sniffer_detach()
            // only AFTER serial_deinit() has joined this task, so the sniffer state is never
            // torn down while this task is still running. The acquire-load here still matters
            // for correctness against sniffer_attach()'s release store while the port is live.
            serial_receive_handler_t sniff_handler = __atomic_load_n(&desc->sniff_handler, __ATOMIC_ACQUIRE);
            if (sniff_handler && buffer_ctx->sniff_data) {
                if (buffer_ctx->sniff_len + event.size > SERIAL_BUF_SIZE) {
                    buffer_ctx->sniff_len = 0;  // overflow guard: drop the partial frame
                }
                memcpy(&buffer_ctx->sniff_data[buffer_ctx->sniff_len], &buffer_ctx->data[old_len], event.size);
                buffer_ctx->sniff_len += event.size;
                if (event.timeout_flag) {
                    sniff_handler(desc, buffer_ctx->sniff_data, buffer_ctx->sniff_len);
                    buffer_ctx->sniff_len = 0;
                }
            }

            // Bridge data path — behavior unchanged from before the additive sniffer feed.
            if (desc->receive_handler && ((!desc->wait_for_idle) || event.timeout_flag)) {
                // For transparent bridge (wait_for_idle=false): forward immediately on every UART_DATA.
                // For Modbus gateway (wait_for_idle=true): forward only when idle timeout fires
                // (complete RTU frame boundary detected — bus silent for >= RX_TOUT symbol periods).
                desc->receive_handler(desc, buffer_ctx->data, buffer_ctx->data_len);
                buffer_ctx->data_len = 0;
            } else if (!desc->receive_handler) {
                // No bridge consumer (passive): the sniffer already consumed via (A);
                // reset the main buffer every event so it cannot grow unbounded.
                buffer_ctx->data_len = 0;
            }
            // If receive_handler is set with wait_for_idle=true and no timeout yet:
            // keep accumulating bytes in buffer_ctx->data until the idle timeout fires.
            break;
        }
        case UART_FIFO_OVF:
            ESP_LOGW(TAG, "UART[%d] HW fifo overflow", desc->port_num);
            serial_drop_and_flush(desc, buffer_ctx);
            break;
        case UART_BUFFER_FULL:
            ESP_LOGW(TAG, "UART[%d] ring buffer full", desc->port_num);
            serial_drop_and_flush(desc, buffer_ctx);
            break;
        case UART_BREAK:
            ESP_LOGD(TAG, "UART[%d] rx break", desc->port_num);
            break;
        case UART_PARITY_ERR:
            ESP_LOGW(TAG, "UART[%d] parity error", desc->port_num);
            break;
        case UART_FRAME_ERR:
            ESP_LOGW(TAG, "UART[%d] frame error", desc->port_num);
            break;
        default:
            ESP_LOGW(TAG, "UART[%d] not handled event type: %d", desc->port_num, event.type);
            break;
    }
}

// This task must not be blocked for long in the callback
// Otherwise the UART event queue may overflow,
// causing packets to merge and partially drop
static void uart_event_task(void *pvParameters)
{
    serial_desc_t *desc = (serial_desc_t *)pvParameters;
    xEventGroupSetBits(desc->event_group, EVENT_TASK_STARTED);
    ESP_LOGD(TAG, "UART[%d] event task started", desc->port_num);

    uint8_t *dtmp = (uint8_t *)malloc(SERIAL_BUF_SIZE);
    uint8_t *sniff_tmp = (uint8_t *)malloc(SERIAL_BUF_SIZE);
    // Do NOT return early when an allocation fails: the task must keep servicing
    // EVENT_TASK_EXIT_REQ below, otherwise serial_deinit() would block on
    // EVENT_TASK_FINISHED forever. The NULL guards (buffer_ctx.data in the loop below,
    // sniff_data in handle_uart_event) keep it from dereferencing the failed buffer.
    // The two buffers have very different consequences, so report them separately
    // instead of blaming the sniffer buffer for killing RX.
    if (dtmp == NULL) {
        // The event loop is gated on buffer_ctx.data, so without it every received byte
        // is silently discarded: RX really is dead on this port. Say so loudly.
        ESP_LOGE(TAG, "UART[%d] failed to allocate the %d-byte RX buffer: RX inoperative on this port",
                 desc->port_num, SERIAL_BUF_SIZE);
    }
    if (sniff_tmp == NULL) {
        // RX is unaffected — received frames still reach receive_handler. Only the
        // sniffer/cache overlay gets nothing from this port (handle_uart_event skips the
        // sniff path when sniff_data is NULL).
        ESP_LOGE(TAG, "UART[%d] failed to allocate the %d-byte sniffer buffer: RX still works, "
                      "but the sniffer/cache overlay is inoperative on this port",
                 desc->port_num, SERIAL_BUF_SIZE);
    }
    buffer_ctx_t buffer_ctx = {
        .data = dtmp,
        .data_len = 0,
        .sniff_data = sniff_tmp,
        .sniff_len = 0
    };

    uart_flush_input(desc->port_num);
    xQueueReset(desc->uart_queue);

    while(1) {
        uart_event_t event;
        BaseType_t result = xQueueReceive(desc->uart_queue, (void *)&event, pdMS_TO_TICKS(SERIAL_EVENT_WAIT_TIMEOUT_MS));
        // Skip event handling if the RX buffer failed to allocate; the loop still
        // honours EVENT_TASK_EXIT_REQ below so serial_deinit() does not block forever.
        if (result == pdPASS && buffer_ctx.data != NULL) {
            handle_uart_event(desc, event, &buffer_ctx);
        }

        EventBits_t bits = xEventGroupWaitBits(desc->event_group, EVENT_TASK_EXIT_REQ, pdFALSE, pdTRUE, 0);
        if (bits & EVENT_TASK_EXIT_REQ) {
            break;
        }
    }

    free(dtmp);
    free(sniff_tmp);
    ESP_LOGI(TAG, "UART[%d] event task finished", desc->port_num);
    /* IMPORTANT: clear task_handle BEFORE signalling EVENT_TASK_FINISHED.
     * serial_deinit() blocks waiting on that bit and, on wake, may free(desc)
     * before this task is rescheduled. If the heap reuses the same address for
     * the next serial_init(), writing task_handle = NULL afterwards would
     * clobber the freshly initialised handle and the *next* serial_deinit()
     * would see "UART[N] not initialized" and skip uart_driver_delete() —
     * leaving the driver installed and breaking subsequent uart_driver_install().
     */
    desc->task_handle = NULL;
    xEventGroupSetBits(desc->event_group, EVENT_TASK_FINISHED);
    vTaskDelete(NULL);
}

static esp_err_t configure_uart_parameters(serial_config_t *serial_config)
{
    uart_config_t uart_config = {
        .baud_rate = serial_config->baudrate,
        .data_bits = serial_config->databits,
        .parity = serial_config->parity,
        .stop_bits = serial_config->stopbits,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_param_config(serial_config->port_num, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error during UART parameters configuring");
        return err;
    }

    err = uart_set_pin(serial_config->port_num, serial_config->tx_pin, serial_config->rx_pin, serial_config->dir_pin, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error during UART pin set");
        return err;
    }

#if QEMU_BUILD
    /* QEMU does not implement RS485 half-duplex mode: uart_set_mode() with
     * UART_MODE_RS485_HALF_DUPLEX sets the RS485_EN bit, causing
     * uart_wait_tx_done() to assert on the TX_DONE interrupt state (uart.c:1348).
     * Use plain UART mode in QEMU — the chardev TCP socket is a simple byte stream
     * without RTS/CTS or RS485 direction control. */
    err = uart_set_mode(serial_config->port_num, UART_MODE_UART);
#else
    err = uart_set_mode(serial_config->port_num, UART_MODE_RS485_HALF_DUPLEX);
#endif
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error during UART mode set");
        return err;
    }

    // Force enable Rx timeout events generation to be able to detect
    // the end of a packet if its length is equal to the UART receive buffer size
    uart_set_always_rx_timeout(serial_config->port_num, true);

    err = uart_set_rx_timeout(serial_config->port_num, SERIAL_RX_TOUT_SNIFFER);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error during UART receive timeout set");
        return err;
    }

    return ESP_OK;
}

serial_desc_t* serial_init(serial_config_t *serial_config, serial_receive_handler_t serial_receive_handler)
{
    if (serial_config == NULL) {
        ESP_LOGE(TAG, "Serial config pointer is NULL");
        return NULL;
    }
    ESP_LOGD(TAG, "UART[%d] initializing...", serial_config->port_num);

    serial_desc_t *desc = malloc(sizeof(serial_desc_t));
    if (!desc) {
        ESP_LOGE(TAG, "Unable to allocate memory for serial_desc_t");
        return NULL;
    }

    EventGroupHandle_t event_group = xEventGroupCreate();
    if (event_group == NULL) {
        ESP_LOGE(TAG, "Unable to create event group");
        free(desc);
        return NULL;
    }

    desc->port_num = serial_config->port_num;
    desc->tx_pin = serial_config->tx_pin;
    desc->rx_pin = serial_config->rx_pin;
    desc->dir_pin = serial_config->dir_pin;
    // The one plain store: nothing else can see the descriptor yet — xTaskCreate() below is what
    // first hands it out — so there is no reader to pair with (see the note on tx_disabled in serial.h).
    desc->tx_disabled = false;
    desc->wait_for_idle = false;   // default: forward immediately (transparent bridge behavior)
    desc->receive_handler = serial_receive_handler;
    desc->sniff_handler = NULL;
    desc->drop_handler = NULL;
    desc->uart_queue = NULL;
    desc->task_handle = NULL;
    desc->event_group = event_group;

    esp_err_t err = uart_driver_install(serial_config->port_num, SERIAL_BUF_SIZE, SERIAL_BUF_SIZE, SERIAL_QUEUE_SIZE, &desc->uart_queue, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error during UART driver installation");
        free(desc);
        vEventGroupDelete(event_group);
        return NULL;
    }

    err = configure_uart_parameters(serial_config);
    if (err != ESP_OK) {
        uart_driver_delete(serial_config->port_num);
        vEventGroupDelete(event_group);
        free(desc);
        return NULL;
    }

    TaskHandle_t task_handle = NULL;
    BaseType_t ret = xTaskCreate(uart_event_task, "uart_event_task", SERIAL_TASK_STACK_SIZE, desc, SERIAL_TASK_PRIORITY, &task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Unable to create UART event task");
        uart_driver_delete(serial_config->port_num);
        vEventGroupDelete(event_group);
        free(desc);
        return NULL;
    }

    desc->task_handle = task_handle;
    xEventGroupWaitBits(desc->event_group, EVENT_TASK_STARTED, pdFALSE, pdTRUE, portMAX_DELAY);

    ESP_LOGD(TAG, "UART[%d] initialized", serial_config->port_num);
    return desc;
}

esp_err_t serial_send(serial_desc_t *desc, uint8_t *data, size_t len)
{
    if (serial_tx_disabled(desc)) {
        ESP_LOGD(TAG, "UART[%d] TX skipped: transmission is disabled", desc->port_num);
        return ESP_OK;
    }
    int written = uart_write_bytes(desc->port_num, (const char *)data, len);

    if (written != len) {
        ESP_LOGE(TAG, "Error sending data to serial port %d", desc->port_num);
        return ESP_FAIL;
    }

    // TX visibility for the sniffer/cache: the bytes we just put on the wire (e.g. the
    // bridge forwarding a TCP client's request to the bus — the "master request" side).
    // sniff_handler is the same per-port callback used for RX; sniffer_process() is
    // internally sniff_mux-guarded, so calling it here (TCP server / HTTP task context)
    // is safe and adds no lock-ordering hazard. Only reached when the data was actually
    // transmitted (tx_disabled returns early above; partial writes return ESP_FAIL above).
    // Same rule as the RX path: acquire-load the handler ONCE into a local, then check and
    // call only that copy. sniffer_detach() runs only after the transport tasks (this one
    // included) have been joined and the descriptor freed, so no detach can land here
    // concurrently — see the RX note.
    serial_receive_handler_t sniff_handler = __atomic_load_n(&desc->sniff_handler, __ATOMIC_ACQUIRE);
    if (sniff_handler) {
        sniff_handler(desc, data, len);
    }
    return ESP_OK;
}

esp_err_t serial_wait_tx_done(serial_desc_t *desc, TickType_t timeout_ticks)
{
#if QEMU_BUILD
    /* uart_wait_tx_done() asserts when called in RS485 mode in QEMU (uart.c:1348).
     * In QEMU the UART chardev flushes bytes synchronously — TX is always done. */
    (void)desc;
    (void)timeout_ticks;
    return ESP_OK;
#else
    return uart_wait_tx_done(desc->port_num, timeout_ticks);
#endif
}

esp_err_t serial_set_rx_timeout(serial_desc_t *desc, uint8_t tout_symbols)
{
    esp_err_t err = uart_set_rx_timeout(desc->port_num, tout_symbols);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART[%d] failed to set RX timeout to %u symbols", desc->port_num, tout_symbols);
        return err;
    }
    ESP_LOGD(TAG, "UART[%d] RX timeout set to %u symbols", desc->port_num, tout_symbols);
    return ESP_OK;
}

esp_err_t serial_set_tx_disabled(serial_desc_t *desc, bool disabled)
{
    if (desc == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    // The writer's own read goes through the accessor too: port_init_mode() reaches this unlocked on
    // port_manager_init()'s boot-loop pass, so the comparison is exposed exactly like the reads in
    // serial_send() and repeater_rx_handler() — neither of those is under pm_lock either.
    if (disabled == serial_tx_disabled(desc)) {
        return ESP_OK; // no state change needed
    }
    // Both stores below are __atomic_store_n(RELEASE) and both land AFTER the pin work they describe,
    // which is what the acquire load in serial_tx_disabled() picks up — per writer; the rationale is
    // at the tx_disabled field in serial.h.
    if (disabled) {
        // Detach dir_pin from UART control and force LOW (RS-485 TX disabled).
        // In QEMU the wrap shim mirrors these IDF calls onto the virtual native
        // GPIO so the host can observe the software-driven tx_disabled state.
        gpio_reset_pin(desc->dir_pin);
        // Set the safe level first, then switch to output, so the pin never
        // drives an undefined level in the window between direction and level.
        gpio_set_level(desc->dir_pin, 0);
        gpio_set_direction(desc->dir_pin, GPIO_MODE_OUTPUT);
        __atomic_store_n(&desc->tx_disabled, true, __ATOMIC_RELEASE);
        ESP_LOGI(TAG, "UART[%d] TX physically disabled (dir_pin=%d forced LOW)", desc->port_num, desc->dir_pin);
    } else {
        // Re-attach dir_pin to UART for automatic half-duplex direction control.
        // The wrap shim mirrors this back to OUTPUT on the virtual native GPIO.
        // TX and RX are passed explicitly instead of UART_PIN_NO_CHANGE, because
        // uart_set_pin() starts by releasing ALL previously configured pins — it disables the
        // TX/RTS pad outputs and re-routes the RX/CTS matrix inputs to constants — and only
        // then reconfigures the signals whose argument is a real pin number. With
        // UART_PIN_NO_CHANGE the bus would stay silent: TX left with its output disabled and
        // RX detached, unrecoverably, since the driver has already forgotten the pin numbers.
        //
        // That unconditional release is an upstream regression, present in esp-idf v5.4.2,
        // v5.4.3, v5.5 and v5.5.1: added by commit 007a497483 ("feat(uart): add pin release
        // process to uart driver") and fixed by 85f0da63fc ("fix(uart): fix release pin logic
        // if switching only one pin"), so it is gone again in v5.4.4+ and v5.5.2+; v5.4.1 and
        // older have no release step at all. Passing the real pin numbers is correct on every
        // one of those versions, so this call does not depend on which IDF the build picks
        // up. To tell whether a given build sits inside the broken window, check the toolchain
        // that is actually installed rather than a version number written down here: both
        // EIM_IDF_VERSION in the Makefile and the Dockerfile's base image now pin an exact
        // tag, but the pins are bumped independently and neither one is what a local machine
        // or a CI agent necessarily has in IDF_PATH. Any build that goes through make does
        // check exactly that: scripts/idf_env.sh reads the version out of the IDF actually in
        // IDF_PATH and fails the build when it differs from EIM_IDF_VERSION — unless
        // IDF_VERSION_CHECK=0 is set, which skips that comparison.
        esp_err_t err = uart_set_pin(desc->port_num, desc->tx_pin, desc->rx_pin, desc->dir_pin, UART_PIN_NO_CHANGE);
        if (err != ESP_OK) {
            // Keep tx_disabled = true and return early: the routing was not restored, so
            // TX really is still off, and the flag has to keep describing the hardware —
            // serial_send() stays gated instead of pushing bytes onto a bus the pins no
            // longer reach. Argument validation runs before the pin release, so a rejected
            // call leaves the hardware as it was and the disabled state still fits it.
            // The failure itself surfaces only through the ESP_LOGE below.
            // Known limitation: the error does not reach the API. The only production path
            // that can get here is update_serial_tx_disabled() -> port_manager_set_tx_disabled()
            // (the other production caller, port_init_mode() in port_manager.c, only ever
            // passes disabled=true and so never enters this branch). port_manager_set_tx_disabled()
            // does propagate the esp_err_t, but update_serial_tx_disabled() discards it, and
            // REST reports tx_disabled from NVS rather than from this descriptor.
            ESP_LOGE(TAG, "UART[%d] failed to restore pin routing: %s", desc->port_num, esp_err_to_name(err));
            return err;
        }
        __atomic_store_n(&desc->tx_disabled, false, __ATOMIC_RELEASE);
        ESP_LOGI(TAG, "UART[%d] TX re-enabled (dir_pin=%d restored to UART)", desc->port_num, desc->dir_pin);
    }
    return ESP_OK;
}

esp_err_t serial_deinit(serial_desc_t *desc)
{
    if (desc == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (desc->task_handle == NULL || desc->event_group == NULL) {
        ESP_LOGE(TAG, "UART[%d] not initialized", desc->port_num);
        return ESP_ERR_NOT_ALLOWED;
    }

    ESP_LOGD(TAG, "UART[%d] deinitializing...", desc->port_num);

    xEventGroupSetBits(desc->event_group, EVENT_TASK_EXIT_REQ);
    EventBits_t bits = xEventGroupWaitBits(desc->event_group, EVENT_TASK_FINISHED, pdFALSE, pdTRUE, portMAX_DELAY);
    if (!(bits & EVENT_TASK_FINISHED)) {
        return ESP_FAIL;
    }

    // Disable UART peripheral interrupts BEFORE uart_driver_delete().
    // uart_driver_delete() calls esp_intr_free() before uart_disable_*_intr(),
    // leaving a window where the ISR is detached but the UART interrupt is still
    // enabled. If an interrupt fires in that window it lands in the default
    // xt_unhandled_interrupt handler, which never clears the source, so the CPU
    // spins forever ("Unhandled interrupt 9 on cpu 0!"). Masking here closes it.
    uart_disable_rx_intr(desc->port_num);
    uart_disable_tx_intr(desc->port_num);

    uart_driver_delete(desc->port_num);
    vEventGroupDelete(desc->event_group);

    ESP_LOGD(TAG, "UART[%d] deinitialized", desc->port_num);
    free(desc);

    return ESP_OK;
}

#ifdef __unittest_env__
/* Test shim: run the uart_event_task inline so unit tests can verify event-handling
 * logic after modifying desc fields (e.g. wait_for_idle) that affect task behaviour. */
void serial_test_run_uart_event_task(serial_desc_t *desc)
{
    uart_event_task(desc);
}
#endif /* __unittest_env__ */
