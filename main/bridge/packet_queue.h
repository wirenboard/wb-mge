#pragma once

#include <esp_err.h>
#include <stdint.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef QueueHandle_t packet_queue_handle;


// Инициализация очереди с максимальной длиной max_len пакетов
// Возвращает handle созданной очереди
// В случае ошибки возвращает NULL
packet_queue_handle packet_queue_create(const size_t max_len);

// Удаление очереди пакетов
void packet_queue_delete(const packet_queue_handle handle);

// Получение количества пакетов, находящихся в очереди
size_t packet_queue_count(const packet_queue_handle handle);

// Удаление из очереди всех пакетов
void packet_queue_clear(const packet_queue_handle handle);

// Добавление пакета в очередь, данные пакета копируются
// data - данные пакета, len - длина пакета
// Возвращает ESP_OK в случае успеха
esp_err_t packet_queue_push(const packet_queue_handle handle, const uint8_t* data, const size_t len);

// Извлечение пакета из очереди с максимальным ожиданием timeout_ticks тиков RTOS
// В случае успеха возвращает размер пакета и устанавливает указатель buf_ptr на данные пакета
// Возвращает 0, если пакетов в очереди нет или произошел тайм-аут
// Буфер после использования необходимо освободить через вызов free(buf_ptr)
size_t packet_queue_pop(const packet_queue_handle handle, uint8_t** buf_ptr, const TickType_t timeout_ticks);
