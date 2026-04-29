#include "sniffer.h"

esp_err_t sniffer_init(void) { return ESP_OK; }
void sniffer_attach(unsigned port_index, serial_desc_t *serial_desc) { (void)port_index; (void)serial_desc; }
void sniffer_enable(unsigned port_index) { (void)port_index; }
void sniffer_disable(unsigned port_index) { (void)port_index; }
esp_err_t sniffer_register_handlers(httpd_handle_t server) { (void)server; return ESP_OK; }
