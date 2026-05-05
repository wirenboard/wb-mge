#pragma once

#include "esp_err.h"

// Default TCP port for the cache Modbus server.
#define CACHE_MODBUS_SERVER_PORT 504

/**
 * @brief Initialize and start the cache Modbus TCP server.
 *
 * Starts a TCP server on the given port that answers FC01/FC02/FC03/FC04
 * read requests directly from the in-memory register cache without touching
 * the RS-485 bus.  Multiple simultaneous clients are supported via the
 * tcp_server acceptor/receiver task model.
 *
 * @param port TCP port to listen on (typically CACHE_MODBUS_SERVER_PORT).
 * @return ESP_OK on success.
 */
esp_err_t cache_modbus_server_init(int port);

/**
 * @brief Deinitialize the cache Modbus TCP server.
 *
 * Stops the TCP listener and frees all associated resources.
 *
 * @return ESP_OK on success.
 */
esp_err_t cache_modbus_server_deinit(void);
