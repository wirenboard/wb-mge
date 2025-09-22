#pragma once

#include "esp_err.h"
#include "serial.h"
#include "setting_items.h"
#include "tcp_desc.h"
#include "bridge.h"
#include "packet_queue.h"

#define MODBUS_TCP_SEND_BUFFER_SIZE         1024            // Размер буфера передачи для TCP и RTU пакетов

typedef struct {
    bool initialized;
    int index;
    serial_desc_t* serial_desc;
    tcp_desc_t* tcp_desc;
    bridge_mode_t mode;
    packet_queue_handle tcp_queue;
    EventGroupHandle_t event_group;
    uint16_t pending_tid;
    uint8_t pending_slave_id;
    unsigned resp_timeout_ticks;
} mb_tcp_task_ctx_t;

// Инициализация порта в режиме Modbus TCP
esp_err_t modbus_tcp_init_port(int index, serial_config_t *config,
                                bridge_mode_t mode, int port, uint32_t ip,
                                serial_desc_t **serial_desc, tcp_desc_t **tcp_desc);
