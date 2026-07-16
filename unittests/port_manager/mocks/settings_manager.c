#include "settings_manager.h"

/* Minimal settings_manager stub for port_manager tests.
 *
 * port_manager.c calls settings_manager_check_port_mode_collision() from
 * port_set_mode_handler() before applying a new mode. The real collision logic is covered by the
 * settings_manager suite against the actual implementation; here the return value is injectable so
 * a test can drive the handler's 409-collision branch. It defaults to ESP_OK ("no collision"), so
 * every other test proceeds to port_manager_set_mode() exactly as before this check existed.
 *
 * Reset g_mock_port_mode_collision_ret to ESP_OK in setUp()/tearDown() to keep tests isolated. */

esp_err_t g_mock_port_mode_collision_ret = ESP_OK;

esp_err_t settings_manager_check_port_mode_collision(unsigned port_index, const char *new_port_mode)
{
    (void)port_index;
    (void)new_port_mode;
    return g_mock_port_mode_collision_ret;
}
