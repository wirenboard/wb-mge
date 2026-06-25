#include "unity.h"
#include "console_log.h"

#include "setting_items.h"
#include "cJSON.h"

#include <stdbool.h>

/* ---- Test shim exposing the static AP-clients JSON builder ----------------
 * info_build_ap_clients_json() is static in info_handlers.c. The production file
 * exposes it under __unittest_env__ via info_handlers_test_build_ap_clients_json()
 * (mirrors the cache_modbus_server.c test-shim pattern). */
esp_err_t info_handlers_test_build_ap_clients_json(cJSON **clients_json);

/* ---- Mock state exposed by mocks/setting_items.c ------------------------- */
void mock_setting_items_set_wifi_perm_disable(bool value);
void mock_setting_items_reset(void);

/* ---- Mock state exposed by mocks/esp_wifi.c ------------------------------ */
extern int mock_esp_wifi_get_sta_list_called;
void       mock_esp_wifi_reset(void);

/* ---- setUp / tearDown ---------------------------------------------------- */

void setUp(void)
{
    mock_setting_items_reset();
    mock_esp_wifi_reset();
}

void tearDown(void)
{
}

/* ===================================================================
 * Tests for info_build_ap_clients_json wifi_perm_disable early return
 * info_handlers.c:193-197
 * =================================================================== */

/* When Wi-Fi is permanently disabled, the AP-clients builder must short-circuit:
 * it returns ESP_OK with an empty JSON array and must NOT consult the Wi-Fi
 * driver (which was never started — calling it on hardware would crash). */
void test_ap_clients_perm_disable_true_returns_empty_array(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "ap_clients: wifi_perm_disable=true -> empty array, ESP_OK, no wifi driver call");
    LOG_MESSAGE();

    mock_setting_items_set_wifi_perm_disable(true);
    TEST_ASSERT_TRUE_MESSAGE(setting_items_read_bool(KEY_WIFI_PERM_DISABLE),
        "Precondition: wifi_perm_disable must read true");

    cJSON *clients = NULL;
    esp_err_t ret = info_handlers_test_build_ap_clients_json(&clients);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret,
        "perm-disabled early return must succeed (ESP_OK)");
    TEST_ASSERT_NOT_NULL_MESSAGE(clients, "clients JSON must be allocated");
    TEST_ASSERT_TRUE_MESSAGE(cJSON_IsArray(clients),
        "clients JSON must be an array");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, cJSON_GetArraySize(clients),
        "clients array must be empty when Wi-Fi is permanently disabled");

    /* The early return must happen BEFORE the Wi-Fi station list is queried. */
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_esp_wifi_get_sta_list_called,
        "perm-disabled path must NOT query the Wi-Fi driver");

    cJSON_Delete(clients);
}

/* When Wi-Fi is NOT permanently disabled, the builder takes the normal path:
 * it queries the Wi-Fi station list (mocked to report zero stations) and returns
 * an empty array with ESP_OK. This locks the branch boundary: the early return
 * is taken only on perm-disable, otherwise the driver is consulted. */
void test_ap_clients_perm_disable_false_uses_normal_path(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "ap_clients: wifi_perm_disable=false -> normal path queries wifi driver");
    LOG_MESSAGE();

    mock_setting_items_set_wifi_perm_disable(false);
    TEST_ASSERT_FALSE_MESSAGE(setting_items_read_bool(KEY_WIFI_PERM_DISABLE),
        "Precondition: wifi_perm_disable must read false");

    cJSON *clients = NULL;
    esp_err_t ret = info_handlers_test_build_ap_clients_json(&clients);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "normal path must succeed (ESP_OK)");
    TEST_ASSERT_NOT_NULL_MESSAGE(clients, "clients JSON must be allocated");
    TEST_ASSERT_TRUE_MESSAGE(cJSON_IsArray(clients), "clients JSON must be an array");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, cJSON_GetArraySize(clients),
        "no associated stations -> empty array on the normal path");

    /* The normal path must consult the Wi-Fi driver (proves the early return
     * was NOT taken when Wi-Fi is enabled). */
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_esp_wifi_get_sta_list_called,
        "normal path must query the Wi-Fi station list exactly once");

    cJSON_Delete(clients);
}

/* ---- Test runner --------------------------------------------------------- */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_ap_clients_perm_disable_true_returns_empty_array);
    RUN_TEST(test_ap_clients_perm_disable_false_uses_normal_path);

    return UNITY_END();
}
