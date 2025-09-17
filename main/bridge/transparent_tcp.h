#include "esp_err.h"
#include "serial.h"
#include "setting_items.h"
#include "tcp_desc.h"
#include "bridge.h"

// Инициализация порта в режиме прозрачного моста TCP <-> RS-485
esp_err_t transparent_tcp_init_port(int index, serial_config_t *config,
                                    bridge_mode_t mode, int port, uint32_t ip,
                                    serial_desc_t **serial_desc, tcp_desc_t **tcp_desc);
