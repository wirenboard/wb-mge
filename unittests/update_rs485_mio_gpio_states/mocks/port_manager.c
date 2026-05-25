#include "bridge/port_manager.h"

// Stub: update_rs485_control now calls port_manager_set_tx_disabled().
// This mock simply accepts the call without any side effects.
esp_err_t port_manager_set_tx_disabled(unsigned port_index, bool disabled)
{
    (void)port_index;
    (void)disabled;
    return ESP_OK;
}
