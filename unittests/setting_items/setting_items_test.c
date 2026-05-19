#include "unity.h"
#include "console_log.h"

#include "array_size.h"
#include "config.h"
#include "esp_log.h"
#include "esp_efuse.h"
#include "esp_mac.h"
#include "ram_storage.h"
#include "setting_items.h"
#include "setting_validators.h"

#include <string.h>

#define TEST_BUFFER_SIZE                            300

setting_storage_iface_t test_storage = {
    .has_key = rams_has_key,
    .write_str = rams_write_str,
    .read_str = rams_read_str,
};

typedef struct {
    const char *key;
    const char *default_value;
    setting_item_type_t type;
} mock_setting_item_t;

const mock_setting_item_t expected_items[] = {
    {"hostname", "WB-MGE", SETTING_ITEM_TYPE_STRING},
    {"login", "admin", SETTING_ITEM_TYPE_STRING},
    {"pass", "admin", SETTING_ITEM_TYPE_STRING},
    {"web_port", "80", SETTING_ITEM_TYPE_INT},
    {"io_bus", "true", SETTING_ITEM_TYPE_BOOL},
    {"vout", "true", SETTING_ITEM_TYPE_BOOL},

    {"wifi_mode", "ap", SETTING_ITEM_TYPE_STRING},
    {"ap_auth", "wpa2_psk", SETTING_ITEM_TYPE_STRING},
    {"sta_auth", "wpa2_psk", SETTING_ITEM_TYPE_STRING},
    {"ap_ssid", "WB-MGE", SETTING_ITEM_TYPE_STRING},
    {"ap_pass", "", SETTING_ITEM_TYPE_STRING},
    {"ap_ip_static", "192.168.5.1", SETTING_ITEM_TYPE_STRING},
    {"ap_mask_static", "255.255.255.0", SETTING_ITEM_TYPE_STRING},
    {"ap_gw_static", "192.168.5.1", SETTING_ITEM_TYPE_STRING},
    {"sta_ssid", "", SETTING_ITEM_TYPE_STRING},
    {"sta_pass", "", SETTING_ITEM_TYPE_STRING},
    {"sta_dhcpc", "true", SETTING_ITEM_TYPE_BOOL},
    {"sta_ip_static", "192.168.1.7", SETTING_ITEM_TYPE_STRING},
    {"sta_mask_static", "255.255.255.0", SETTING_ITEM_TYPE_STRING},
    {"sta_gw_static", "192.168.1.1", SETTING_ITEM_TYPE_STRING},

    {"eth_ip_static", "192.168.0.7", SETTING_ITEM_TYPE_STRING},
    {"eth_mask_static", "255.255.255.0", SETTING_ITEM_TYPE_STRING},
    {"eth_gw_static", "192.168.0.1", SETTING_ITEM_TYPE_STRING},
    {"eth_dhcpc", "true", SETTING_ITEM_TYPE_BOOL},

    {"baudrate_1", "9600", SETTING_ITEM_TYPE_INT},
    {"stopbits_1", "2", SETTING_ITEM_TYPE_STRING},
    {"parity_1", "none", SETTING_ITEM_TYPE_STRING},
    {"databits_1", "8", SETTING_ITEM_TYPE_STRING},
    {"485_term_1", "true", SETTING_ITEM_TYPE_BOOL},
    {"485_fail_safe_1", "true", SETTING_ITEM_TYPE_BOOL},
    {"bridge_mode_1", "server", SETTING_ITEM_TYPE_STRING},
    {"bridge_port_1", "502", SETTING_ITEM_TYPE_INT},
    {"bridge_ip_1", "192.168.5.2", SETTING_ITEM_TYPE_STRING},
    {"bridge_modbus_1", "false", SETTING_ITEM_TYPE_BOOL},

    {"baudrate_2", "9600", SETTING_ITEM_TYPE_INT},
    {"stopbits_2", "2", SETTING_ITEM_TYPE_STRING},
    {"parity_2", "none", SETTING_ITEM_TYPE_STRING},
    {"databits_2", "8", SETTING_ITEM_TYPE_STRING},
    {"485_term_2", "true", SETTING_ITEM_TYPE_BOOL},
    {"485_fail_safe_2", "true", SETTING_ITEM_TYPE_BOOL},
    {"bridge_mode_2", "server", SETTING_ITEM_TYPE_STRING},
    {"bridge_port_2", "503", SETTING_ITEM_TYPE_INT},
    {"bridge_ip_2", "192.168.5.2", SETTING_ITEM_TYPE_STRING},
    {"bridge_modbus_2", "false", SETTING_ITEM_TYPE_BOOL},

    {"port_mode_1", "tcp_bridge", SETTING_ITEM_TYPE_STRING},
    {"port_mode_2", "tcp_bridge", SETTING_ITEM_TYPE_STRING},
    {"cache_mb_port", "504", SETTING_ITEM_TYPE_INT},
    {"cache_mb_srv_en", "true", SETTING_ITEM_TYPE_BOOL},
    {"cache_val_tout", "60", SETTING_ITEM_TYPE_INT},
};

#define SETTING_ITEMS_COUNT         (ARRAY_SIZE(expected_items))

const char* ap_pass_default = "0033752069";
const char* hostname_default = "WB-MGE-030405";

void setting_items_reset(void);

void setUp(void)
{
    mock_reset_validator_flags();
    mock_esp_mac_reset();
    setting_items_reset();

    mock_storage_read_error_code = ESP_OK;
    mock_storage_write_error_code = ESP_OK;

    rams_init();
}

void tearDown(void)
{

}

// Тестируем инициализацию setting_items, write_str возвращает ошибку -> setting_items_init должен вернуть ошибку
void test_setting_items_init_function(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test setting_items_init function");
    LOG_MESSAGE();

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, setting_items_init(), "Initialization should fail when write_str returns error");
}

// Тестируем, что при инициализации и отсутствии ключей в хранилище записываются значения по умолчанию
void test_setting_items_init_with_storage(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test setting_items_init_with_storage function");
    LOG_MESSAGE();

    esp_err_t result = setting_items_init_with_storage(&test_storage);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "Initialization should succeed");

    for (size_t i = 0; i < SETTING_ITEMS_COUNT; i++) {
        const char *key = setting_items_get_key_at(i);
        TEST_ASSERT_NOT_NULL_MESSAGE(key, "Key should not be NULL");

        char value[SETTING_ITEM_MAX_STR_LEN] = {0};
        result = setting_items_read(key, value);
        TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "Reading of setting should succeed");

        if (strncmp(key, KEY_AP_PASS, SETTING_ITEM_MAX_STR_LEN) == 0) {
            TEST_ASSERT_EQUAL_STRING_MESSAGE(
                ap_pass_default,
                value,
                "The value read for ap_pass should match the expected default value"
            );
            continue;
        } else if (strncmp(key, KEY_HOSTNAME, SETTING_ITEM_MAX_STR_LEN) == 0) {
            TEST_ASSERT_EQUAL_STRING_MESSAGE(
                hostname_default,
                value,
                "The value read for hostname should match the expected default value"
            );
            continue;
        } else if (strncmp(key, KEY_AP_SSID, SETTING_ITEM_MAX_STR_LEN) == 0) {
            TEST_ASSERT_EQUAL_STRING_MESSAGE(
                hostname_default,
                value,
                "The value read for ap_ssid should match the expected default value"
            );
            continue;
        }

        for (size_t j = 0; j < SETTING_ITEMS_COUNT; j++) {
            if (strcmp(expected_items[j].key, key) == 0) {
                if (strcmp(expected_items[j].default_value, value) == 0) {
                    break;
                } else {
                    char log_message[TEST_BUFFER_SIZE];
                    snprintf(log_message, sizeof(log_message), "The value read for %s should match the expected default value", key);
                    TEST_FAIL_MESSAGE(log_message);
                }
                break;
            } else {
                if (j == SETTING_ITEMS_COUNT - 1) {
                    char log_message[TEST_BUFFER_SIZE];
                    snprintf(log_message, sizeof(log_message), "Key %s not found in expected_items array", key);
                    TEST_FAIL_MESSAGE(log_message);
                }
            }
        }
    }
}

// Тестируем содержимое массива setting_items
void test_setting_items_array_contents(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test setting_items array contents");
    LOG_MESSAGE();

    size_t actual_items_count = setting_items_get_count();
    TEST_ASSERT_EQUAL_INT_MESSAGE(SETTING_ITEMS_COUNT, actual_items_count, "Items count should match expected");

    for (size_t i = 0; i < SETTING_ITEMS_COUNT; i++) {
        bool found = false;
        for (size_t j = 0; j < actual_items_count; j++) {
            const char *key = setting_items_get_key_at(j);
            if (strcmp(expected_items[i].key, key) == 0) {
                if (strcmp(expected_items[i].default_value, setting_items_get_default_value(key)) == 0) {
                    if (expected_items[i].type == setting_items_get_type(key)) {
                        found = true;
                        break;
                    }
                }
            }
        }
        if (!found) {
            char search_message[TEST_BUFFER_SIZE];
            snprintf(search_message, sizeof(search_message),
                    "Setting item %s should have default value of %s type %s",
                    expected_items[i].key, expected_items[i].default_value,
                    setting_items_type_to_string(expected_items[i].type)
            );
            TEST_FAIL_MESSAGE(search_message);
        }
    }
}

// Тестируем, что нужные валидаторы вызываются для соответствующих настроек
void test_authentication_validators(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test authentication validators");
    LOG_MESSAGE();

    // Test hostname validator
    mock_reset_validator_flags();
    TEST_ASSERT_EQUAL_INT(ESP_OK, setting_items_save(KEY_HOSTNAME, "test-host"));
    TEST_ASSERT_TRUE_MESSAGE(mock_validate_hostname_called, "validate_hostname should be called for hostname");

    // Test login validator
    mock_reset_validator_flags();
    TEST_ASSERT_EQUAL_INT(ESP_OK, setting_items_save(KEY_LOGIN, "admin"));
    TEST_ASSERT_TRUE_MESSAGE(mock_validate_login_called, "validate_login should be called for login");

    // Test password validator
    mock_reset_validator_flags();
    TEST_ASSERT_EQUAL_INT(ESP_OK, setting_items_save(KEY_PASS, "password123"));
    TEST_ASSERT_TRUE_MESSAGE(mock_validate_password_called, "validate_password should be called for password");
}

void test_port_validators(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test port validators");
    LOG_MESSAGE();

    // Test web port validator
    mock_reset_validator_flags();
    TEST_ASSERT_EQUAL_INT(ESP_OK, setting_items_save(KEY_WEB_PORT, "80"));
    TEST_ASSERT_TRUE_MESSAGE(mock_validate_port_called, "validate_port should be called for web port");

    // Test bridge port 1 validator
    mock_reset_validator_flags();
    TEST_ASSERT_EQUAL_INT(ESP_OK, setting_items_save(KEY_BRIDGE_PORT1, "80"));
    TEST_ASSERT_TRUE_MESSAGE(mock_validate_port_called, "validate_port should be called for bridge port 1");

    // Test bridge port 2 validator
    mock_reset_validator_flags();
    TEST_ASSERT_EQUAL_INT(ESP_OK, setting_items_save(KEY_BRIDGE_PORT2, "80"));
    TEST_ASSERT_TRUE_MESSAGE(mock_validate_port_called, "validate_port should be called for bridge port 2");

    // Test cache modbus port validator
    mock_reset_validator_flags();
    TEST_ASSERT_EQUAL_INT(ESP_OK, setting_items_save(KEY_CACHE_MODBUS_PORT, "502"));
    TEST_ASSERT_TRUE_MESSAGE(mock_validate_port_called, "validate_port should be called for cache modbus port");

    // Test cache value timeout validator
    mock_reset_validator_flags();
    TEST_ASSERT_EQUAL_INT(ESP_OK, setting_items_save(KEY_CACHE_VALUE_TIMEOUT_S, "120"));
    TEST_ASSERT_TRUE_MESSAGE(mock_validate_timeout_called, "validate_timeout should be called for cache_val_tout");
}

void test_ip_validators(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test IP validators");
    LOG_MESSAGE();

    const char* ip_keys[] = {
        KEY_ETH_IP_STATIC, KEY_ETH_MASK_STATIC, KEY_ETH_GW_STATIC,
        KEY_AP_IP_STATIC, KEY_AP_MASK_STATIC, KEY_AP_GW_STATIC,
        KEY_STA_IP_STATIC, KEY_STA_MASK_STATIC, KEY_STA_GW_STATIC,
        KEY_BRIDGE_IP1, KEY_BRIDGE_IP2
    };

    for (size_t i = 0; i < ARRAY_SIZE(ip_keys); i++) {
        mock_reset_validator_flags();
        TEST_ASSERT_EQUAL_INT(ESP_OK, setting_items_save(ip_keys[i], "192.168.1.1"));
        TEST_ASSERT_TRUE_MESSAGE(mock_validate_ip_called, "validate_ip should be called for IP addresses");
    }
}

void test_wifi_validators(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test WiFi validators");
    LOG_MESSAGE();

    // Test WiFi mode validator
    mock_reset_validator_flags();
    TEST_ASSERT_EQUAL_INT(ESP_OK, setting_items_save(KEY_WIFI_MODE, "ap"));
    TEST_ASSERT_TRUE_MESSAGE(mock_validate_wifi_mode_called, "validate_wifi_mode should be called for WiFi mode");

    // Test WiFi auth validators
    mock_reset_validator_flags();
    TEST_ASSERT_EQUAL_INT(ESP_OK, setting_items_save(KEY_WIFI_AUTH_AP, "wpa2_psk"));
    TEST_ASSERT_TRUE_MESSAGE(mock_validate_wifi_auth_called, "validate_wifi_auth should be called for AP auth");

    mock_reset_validator_flags();
    TEST_ASSERT_EQUAL_INT(ESP_OK, setting_items_save(KEY_WIFI_AUTH_STA, "wpa2_psk"));
    TEST_ASSERT_TRUE_MESSAGE(mock_validate_wifi_auth_called, "validate_wifi_auth should be called for STA auth");

    // Test SSID validators
    mock_reset_validator_flags();
    TEST_ASSERT_EQUAL_INT(ESP_OK, setting_items_save(KEY_AP_SSID, "test-ap"));
    TEST_ASSERT_TRUE_MESSAGE(mock_validate_ssid_called, "validate_ssid should be called for AP SSID");

    mock_reset_validator_flags();
    TEST_ASSERT_EQUAL_INT(ESP_OK, setting_items_save(KEY_STA_SSID, "test-sta"));
    TEST_ASSERT_TRUE_MESSAGE(mock_validate_ssid_called, "validate_ssid should be called for STA SSID");

    // Test WiFi password validators
    mock_reset_validator_flags();
    TEST_ASSERT_EQUAL_INT(ESP_OK, setting_items_save(KEY_AP_PASS, "wifipass123"));
    TEST_ASSERT_TRUE_MESSAGE(mock_validate_password_called, "validate_password should be called for AP password");

    mock_reset_validator_flags();
    TEST_ASSERT_EQUAL_INT(ESP_OK, setting_items_save(KEY_STA_PASS, "wifipass456"));
    TEST_ASSERT_TRUE_MESSAGE(mock_validate_password_called, "validate_password should be called for STA password");
}

void test_serial_validators(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test serial validators");
    LOG_MESSAGE();

    // Test baudrate validators
    const char* baudrate_keys[] = {KEY_BAUDRATE1, KEY_BAUDRATE2};
    for (size_t i = 0; i < ARRAY_SIZE(baudrate_keys); i++) {
        mock_reset_validator_flags();
        TEST_ASSERT_EQUAL_INT(ESP_OK, setting_items_save(baudrate_keys[i], "115200"));
        TEST_ASSERT_TRUE_MESSAGE(mock_validate_baudrate_called, "validate_baudrate should be called for baudrate");
    }

    // Test stopbits validators
    const char* stopbits_keys[] = {KEY_STOPBITS1, KEY_STOPBITS2};
    for (size_t i = 0; i < ARRAY_SIZE(stopbits_keys); i++) {
        mock_reset_validator_flags();
        TEST_ASSERT_EQUAL_INT(ESP_OK, setting_items_save(stopbits_keys[i], "1"));
        TEST_ASSERT_TRUE_MESSAGE(mock_validate_stopbits_called, "validate_stopbits should be called for stopbits");
    }

    // Test parity validators
    const char* parity_keys[] = {KEY_PARITY1, KEY_PARITY2};
    for (size_t i = 0; i < ARRAY_SIZE(parity_keys); i++) {
        mock_reset_validator_flags();
        TEST_ASSERT_EQUAL_INT(ESP_OK, setting_items_save(parity_keys[i], "none"));
        TEST_ASSERT_TRUE_MESSAGE(mock_validate_parity_called, "validate_parity should be called for parity");
    }

    // Test databits validators
    const char* databits_keys[] = {KEY_DATABITS1, KEY_DATABITS2};
    for (size_t i = 0; i < ARRAY_SIZE(databits_keys); i++) {
        mock_reset_validator_flags();
        TEST_ASSERT_EQUAL_INT(ESP_OK, setting_items_save(databits_keys[i], "8"));
        TEST_ASSERT_TRUE_MESSAGE(mock_validate_databits_called, "validate_databits should be called for databits");
    }
}

void test_bridge_and_bool_validators(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge and boolean validators");
    LOG_MESSAGE();

    // Test bridge mode validators
    const char* bridge_mode_keys[] = {KEY_BRIDGE_MODE1, KEY_BRIDGE_MODE2};
    for (size_t i = 0; i < ARRAY_SIZE(bridge_mode_keys); i++) {
        mock_reset_validator_flags();
        TEST_ASSERT_EQUAL_INT(ESP_OK, setting_items_save(bridge_mode_keys[i], "client"));
        TEST_ASSERT_TRUE_MESSAGE(mock_validate_bridge_mode_called, "validate_bridge_mode should be called for bridge mode");
    }

    // Test boolean validators
    const char* bool_keys[] = {
        KEY_ETH_DHCPC, KEY_STA_DHCPC, KEY_IO_BUS_ENABLED, KEY_485_VOUT,
        KEY_485_TERM_1, KEY_485_FAIL_SAFE_1, KEY_BRIDGE_MB1,
        KEY_485_TERM_2, KEY_485_FAIL_SAFE_2, KEY_BRIDGE_MB2
    };

    for (size_t i = 0; i < ARRAY_SIZE(bool_keys); i++) {
        mock_reset_validator_flags();
        TEST_ASSERT_EQUAL_INT(ESP_OK, setting_items_save(bool_keys[i], "true"));
        TEST_ASSERT_TRUE_MESSAGE(mock_validate_bool_called, "validate_bool should be called for boolean values");
    }
}

// Тестируем все ошибочные условия в функции setting_items_save
void test_setting_items_save_error_conditions(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test setting_items_save error conditions");
    LOG_MESSAGE();

    const char* valid_key = KEY_HOSTNAME;
    const char* valid_value = "test-host";
    const char* invalid_key = "nonexistent_key";
    const char* invalid_value = "";

    // Test 1: NULL key parameter
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_ERR_INVALID_ARG,
        setting_items_save(NULL, valid_value),
        "Should return ESP_ERR_INVALID_ARG for NULL key"
    );

    // Test 2: NULL value parameter
    char read_buffer_before[TEST_BUFFER_SIZE];
    char read_buffer_after[TEST_BUFFER_SIZE];
    memset(read_buffer_before, 0, sizeof(read_buffer_before));
    memset(read_buffer_after, 0, sizeof(read_buffer_after));

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_OK,
        setting_items_read(valid_key, read_buffer_before),
        "Should return ESP_OK for successful read"
    );

    mock_rams_write_str_called = false;

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_ERR_INVALID_ARG,
        setting_items_save(valid_key, NULL),
        "Should return ESP_ERR_INVALID_ARG for NULL value"
    );

    TEST_ASSERT_FALSE_MESSAGE(mock_rams_write_str_called, "RAM storage write should not be called");

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_OK,
        setting_items_read(valid_key, read_buffer_after),
        "Should return ESP_OK for successful read"
    );

    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        read_buffer_before,
        read_buffer_after,
        "Value should remain unchanged after failed save with NULL value"
    );

    // Test 3: Unknown/invalid key
    mock_rams_write_str_called = false;

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_ERR_NOT_FOUND,
        setting_items_save(invalid_key, valid_value),
        "Should return ESP_ERR_NOT_FOUND for unknown key"
    );

    TEST_ASSERT_FALSE_MESSAGE(mock_rams_write_str_called, "RAM storage write should not be called");

    // Test 4: Validator rejects the value
    mock_reset_validator_flags();
    mock_rams_write_str_called = false;
    memset(read_buffer_before, 0, sizeof(read_buffer_before));
    memset(read_buffer_after, 0, sizeof(read_buffer_after));

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_OK,
        setting_items_read(valid_key, read_buffer_before),
        "Should return ESP_OK for successful read"
    );

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_ERR_INVALID_ARG,
        setting_items_save(valid_key, invalid_value),
        "Should return ESP_ERR_INVALID_ARG when validator rejects value"
    );

    TEST_ASSERT_FALSE_MESSAGE(mock_rams_write_str_called, "RAM storage write should not be called");
    TEST_ASSERT_TRUE_MESSAGE(mock_validate_hostname_called, "Validator should be called even when it fails");

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_OK,
        setting_items_read(valid_key, read_buffer_after),
        "Should return ESP_OK for successful read"
    );

    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        read_buffer_before,
        read_buffer_after,
        "Value should remain unchanged after failed save with invalid value"
    );

    // Test 5: Storage write error
    mock_reset_validator_flags();
    mock_storage_write_error_code = ESP_ERR_NO_MEM;

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_ERR_NO_MEM,
        setting_items_save(valid_key, valid_value),
        "Should return ESP_ERR_NO_MEM when storage write fails"
    );
    TEST_ASSERT_TRUE_MESSAGE(mock_validate_hostname_called, "Validator should be called even when write fails");
}

// Тестируем успешное чтение настроек
void test_setting_items_read_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test setting_items_read success");
    LOG_MESSAGE();

    const char* test_key = KEY_HOSTNAME;
    const char* test_value = "logged-hostname";
    char read_buffer[TEST_BUFFER_SIZE];

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_OK,
        setting_items_save(test_key, test_value),
        "Should successfully save hostname for read test"
    );

    memset(read_buffer, 0, sizeof(read_buffer));

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_OK,
        setting_items_read(test_key, read_buffer),
        "Should return ESP_OK for successful read"
    );
    TEST_ASSERT_EQUAL_STRING_MESSAGE(test_value, read_buffer, "Should read back the exact saved value");
}

// Тестируем все ошибочные условия в функции setting_items_read
void test_setting_items_read_error_conditions(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test setting_items_read error conditions");
    LOG_MESSAGE();

    // Valid parameters for comparison
    const char* valid_key = KEY_HOSTNAME;
    const char* invalid_key = "nonexistent_key";
    char read_buffer[TEST_BUFFER_SIZE];

    // Test 1: NULL key parameter
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_ERR_INVALID_ARG,
        setting_items_read(NULL, read_buffer),
        "Should return ESP_ERR_INVALID_ARG for NULL key"
    );

    // Test 2: NULL value parameter
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_ERR_INVALID_ARG,
        setting_items_read(valid_key, NULL),
        "Should return ESP_ERR_INVALID_ARG for NULL value buffer"
    );

    // Test 3: Unknown/invalid key
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_ERR_NOT_FOUND,
        setting_items_read(invalid_key, read_buffer),
        "Should return ESP_ERR_NOT_FOUND for unknown key"
    );

    // Test 4: Storage read error
    mock_storage_read_error_code = ESP_ERR_NO_MEM;
    memset(read_buffer, 0, sizeof(read_buffer));

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_OK,
        setting_items_read(valid_key, read_buffer),
        "Should return ESP_OK even when storage returns error"
    );

    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        hostname_default,
        read_buffer,
        "Should return default value when storage read fails"
    );
}

// Тестируем функцию setting_items_get_key_at
void test_setting_items_get_key_at(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test setting_items_get_key_at function");
    LOG_MESSAGE();

    size_t total_count = setting_items_get_count();

    // Test valid indices - check first few keys
    const char* first_key = setting_items_get_key_at(0);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(KEY_HOSTNAME, first_key, "First key should be hostname");

    const char* second_key = setting_items_get_key_at(1);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(KEY_LOGIN, second_key, "Second key should be login");

    const char* last_key = setting_items_get_key_at(total_count - 1);
    TEST_ASSERT_NOT_NULL_MESSAGE(last_key, "Last key should not be NULL");

    // Test invalid indices (out of bounds)
    const char* invalid_key1 = setting_items_get_key_at(total_count);
    TEST_ASSERT_NULL_MESSAGE(invalid_key1, "Should return NULL for index equal to count");

    const char* invalid_key2 = setting_items_get_key_at(total_count + 1);
    TEST_ASSERT_NULL_MESSAGE(invalid_key2, "Should return NULL for index greater than count");

    const char* invalid_key3 = setting_items_get_key_at(SIZE_MAX);
    TEST_ASSERT_NULL_MESSAGE(invalid_key3, "Should return NULL for very large index");

    // Test that all keys are valid and unique
    for (size_t i = 0; i < total_count; i++) {
        const char* key = setting_items_get_key_at(i);
        TEST_ASSERT_NOT_NULL_MESSAGE(key, "All valid indices should return non-NULL keys");
        TEST_ASSERT_TRUE_MESSAGE(strlen(key) > 0, "All keys should have non-zero length");

        // Ensure no duplicate keys
        for (size_t j = i + 1; j < total_count; j++) {
            const char* other_key = setting_items_get_key_at(j);
            TEST_ASSERT_TRUE_MESSAGE(strcmp(key, other_key) != 0, "Keys should be unique");
        }
    }
}

// Тестируем функцию setting_items_read_int
void test_setting_items_read_int(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test setting_items_read_int function");
    LOG_MESSAGE();

    int read_value = 0;

    // Test 1: NULL key parameter
    read_value = setting_items_read_int(NULL);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, read_value, "Should return 0 for NULL key");

    // Test 2: Unknown/invalid key - should return 0
    read_value = setting_items_read_int("nonexistent_key");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, read_value, "Should return 0 for unknown key");

    // Test 3: Read integer setting that was saved
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_OK,
        setting_items_save(KEY_WEB_PORT, "8080"),
        "Should successfully save web port"
    );

    read_value = setting_items_read_int(KEY_WEB_PORT);
    TEST_ASSERT_EQUAL_INT_MESSAGE(8080, read_value, "Should read the correct integer value");

    // Test 4: Test non-numeric key (should return 0)
    read_value = setting_items_read_int(KEY_HOSTNAME);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, read_value, "Should return 0 for non-numeric setting");
}

// Тестируем функцию setting_items_read_bool
void test_setting_items_read_bool(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test setting_items_read_bool function");
    LOG_MESSAGE();

    bool read_value = false;

    // Test 1: NULL key parameter
    read_value = setting_items_read_bool(NULL);
    TEST_ASSERT_FALSE_MESSAGE(read_value, "Should return false for NULL key");

    // Test 2: Unknown/invalid key - should return false
    read_value = setting_items_read_bool("nonexistent_key");
    TEST_ASSERT_FALSE_MESSAGE(read_value, "Should return false for unknown key");

    // Test 3: Non-boolean key (should return false due to type mismatch)
    read_value = setting_items_read_bool(KEY_HOSTNAME);
    TEST_ASSERT_FALSE_MESSAGE(read_value, "Should return false for non-boolean setting");

    // Test 4: Test boolean key that defaults to false
    read_value = setting_items_read_bool(KEY_BRIDGE_MB1);
    TEST_ASSERT_FALSE_MESSAGE(read_value, "Should return default bridge_modbus_1 value (false) from default_value");

    // Test 5: Test boolean key that defaults to true
    read_value = setting_items_read_bool(KEY_485_TERM_1);
    TEST_ASSERT_TRUE_MESSAGE(read_value, "Should return default 485_term_1 value (true) from default_value");
}

// Тестируем функцию setting_items_save_int
void test_setting_items_save_int(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test setting_items_save_int function");
    LOG_MESSAGE();

    esp_err_t result = ESP_OK;
    int read_value = 0;

    // Test 1: NULL key parameter
    result = setting_items_save_int(NULL, 123);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_ERR_INVALID_ARG, result, "Should return ESP_ERR_INVALID_ARG for NULL key");

    // Test 2: Unknown/invalid key
    result = setting_items_save_int("nonexistent_key", 456);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_ERR_NOT_FOUND, result, "Should return ESP_ERR_NOT_FOUND for unknown key");

    // Test 3: Save positive integer value
    result = setting_items_save_int(KEY_WEB_PORT, 8080);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "Should successfully save positive integer");

    read_value = setting_items_read_int(KEY_WEB_PORT);
    TEST_ASSERT_EQUAL_INT_MESSAGE(8080, read_value, "Should read back the same positive integer value");
}

// Тестируем функцию setting_items_save_bool
void test_setting_items_save_bool(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test setting_items_save_bool function");
    LOG_MESSAGE();

    esp_err_t result = ESP_OK;
    bool read_value = false;

    // Test 1: NULL key parameter
    result = setting_items_save_bool(NULL, true);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_ERR_INVALID_ARG, result, "Should return ESP_ERR_INVALID_ARG for NULL key");

    // Test 2: Unknown/invalid key
    result = setting_items_save_bool("nonexistent_key", false);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_ERR_NOT_FOUND, result, "Should return ESP_ERR_NOT_FOUND for unknown key");

    // Test 3: Save true value to boolean setting
    result = setting_items_save_bool(KEY_BRIDGE_MB1, false);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "Should successfully save false value");

    read_value = setting_items_read_bool(KEY_BRIDGE_MB1);
    TEST_ASSERT_FALSE_MESSAGE(read_value, "Should read back false value");

    // Test 4: Save false value to boolean setting
    result = setting_items_save_bool(KEY_485_VOUT, false);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "Should successfully save false value");

    read_value = setting_items_read_bool(KEY_485_VOUT);
    TEST_ASSERT_FALSE_MESSAGE(read_value, "Should read back false value");
}

// Тестируем функцию setting_items_get_default_value
void test_setting_items_get_default_value(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test setting_items_get_default_value function");
    LOG_MESSAGE();

    // Test 1: NULL key parameter
    const char* default_value = setting_items_get_default_value(NULL);
    TEST_ASSERT_NULL_MESSAGE(default_value, "Should return NULL for NULL key");

    // Test 2: Unknown/invalid key
    default_value = setting_items_get_default_value("nonexistent_key");
    TEST_ASSERT_NULL_MESSAGE(default_value, "Should return NULL for unknown key");

    // Test 3: Valid key - hostname
    default_value = setting_items_get_default_value(KEY_HOSTNAME);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("WB-MGE", default_value, "Default value for hostname should match expected");
}

// Тестируем функцию setting_items_get_type
void test_setting_items_get_type(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test setting_items_get_type function");
    LOG_MESSAGE();

    setting_item_type_t type = SETTING_ITEM_TYPE_STRING;

    // Test 1: NULL key parameter
    type = setting_items_get_type(NULL);
    TEST_ASSERT_EQUAL_INT_MESSAGE(SETTING_ITEM_TYPE_INVALID, type, "Should return SETTING_ITEM_TYPE_INVALID for NULL key");

    // Test 2: Unknown/invalid key
    type = setting_items_get_type("nonexistent_key");
    TEST_ASSERT_EQUAL_INT_MESSAGE(SETTING_ITEM_TYPE_INVALID, type, "Should return SETTING_ITEM_TYPE_INVALID for unknown key");

    // Test 3: String type settings
    type = setting_items_get_type(KEY_HOSTNAME);
    TEST_ASSERT_EQUAL_INT_MESSAGE(SETTING_ITEM_TYPE_STRING, type, "KEY_HOSTNAME should be STRING type");

    // Test 4: Boolean type settings
    type = setting_items_get_type(KEY_IO_BUS_ENABLED);
    TEST_ASSERT_EQUAL_INT_MESSAGE(SETTING_ITEM_TYPE_BOOL, type, "KEY_IO_BUS_ENABLED should be BOOL type");

    // Test 5: Integer type settings
    type = setting_items_get_type(KEY_WEB_PORT);
    TEST_ASSERT_EQUAL_INT_MESSAGE(SETTING_ITEM_TYPE_INT, type, "KEY_WEB_PORT should be INT type");
}

// Тестируем функцию setting_items_type_to_string
void test_setting_items_type_to_string(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test setting_items_type_to_string function");
    LOG_MESSAGE();

    const char* type_string;

    // Test 1: STRING type
    type_string = setting_items_type_to_string(SETTING_ITEM_TYPE_STRING);
    TEST_ASSERT_NOT_NULL_MESSAGE(type_string, "Should return non-NULL string for STRING type");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("STRING", type_string, "Should return 'STRING' for SETTING_ITEM_TYPE_STRING");

    // Test 2: BOOL type
    type_string = setting_items_type_to_string(SETTING_ITEM_TYPE_BOOL);
    TEST_ASSERT_NOT_NULL_MESSAGE(type_string, "Should return non-NULL string for BOOL type");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("BOOL", type_string, "Should return 'BOOL' for SETTING_ITEM_TYPE_BOOL");

    // Test 3: INT type
    type_string = setting_items_type_to_string(SETTING_ITEM_TYPE_INT);
    TEST_ASSERT_NOT_NULL_MESSAGE(type_string, "Should return non-NULL string for INT type");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("INT", type_string, "Should return 'INT' for SETTING_ITEM_TYPE_INT");

    // Test 4: INVALID type
    type_string = setting_items_type_to_string(SETTING_ITEM_TYPE_INVALID);
    TEST_ASSERT_NOT_NULL_MESSAGE(type_string, "Should return non-NULL string for INVALID type");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("INVALID", type_string, "Should return 'INVALID' for SETTING_ITEM_TYPE_INVALID");

    // Test 5: Unknown/out-of-range type (should return "UNKNOWN")
    type_string = setting_items_type_to_string((setting_item_type_t)999);
    TEST_ASSERT_NOT_NULL_MESSAGE(type_string, "Should return non-NULL string for unknown type");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("UNKNOWN", type_string, "Should return 'UNKNOWN' for unknown type value");

    // Test 6: Another unknown type (negative value)
    type_string = setting_items_type_to_string((setting_item_type_t)-1);
    TEST_ASSERT_NOT_NULL_MESSAGE(type_string, "Should return non-NULL string for negative type");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("UNKNOWN", type_string, "Should return 'UNKNOWN' for negative type value");
}

// Тестируем get_dynamic_ap_pass_default и get_dynamic_hostname_default в случае, когда esp_read_mac возвращает ошибку,
// а затем повторный вызов setting_items_init_with_storage
void test_dynamic_defaults_generation_mac_error(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test dynamic defaults generation - MAC error");
    LOG_MESSAGE();

    mock_esp_read_mac_return = ESP_FAIL;

    esp_err_t result = setting_items_init_with_storage(&test_storage);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "Initialization should succeed");

    char buffer[SETTING_ITEM_MAX_STR_LEN];
    memset(buffer, 0, sizeof(buffer));

    result = setting_items_read(KEY_AP_PASS, buffer);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "Should return ESP_OK for successful read");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("wirenboard", buffer, "Should return fallback password when MAC read fails");

    memset(buffer, 0, sizeof(buffer));
    result = setting_items_read(KEY_HOSTNAME, buffer);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "Should return ESP_OK for successful read");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("WB-MGE", buffer, "Should return fallback hostname when MAC read fails");

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, mock_esp_read_mac_called, "esp_read_mac should be called twice");

    // Повторный вызов get_dynamic_ap_pass_default и get_dynamic_hostname_default не должен вызывать esp_read_mac снова
    rams_init();
    result = setting_items_init_with_storage(&test_storage);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "Initialization should succeed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, mock_esp_read_mac_called, "esp_read_mac should not be called again");
}

// Тестируем генерацию пароля на основе MAC-адреса, когда MAC-адрес короткий
void test_generate_mac_based_password_short_mac(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test generate_mac_based_password - short MAC address");
    LOG_MESSAGE();

    memset(mock_mac_address, 0, MAC_ADDRESS_SIZE);
    mock_mac_address[MAC_ADDRESS_SIZE - 1] = 1;

    setting_items_init_with_storage(&test_storage);

    char buffer[SETTING_ITEM_MAX_STR_LEN];
    memset(buffer, 0, sizeof(buffer));

    esp_err_t ret = setting_items_read(KEY_AP_PASS, buffer);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "Reading generated AP password should succeed");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("0010000001", buffer, "Generated password should match expected value");
    TEST_ASSERT_EQUAL_MESSAGE(10, strlen(buffer), "Generated password length should be 10");
}

// Тестируем функцию read_wifi_pass_from_efuse с разной длиной пароля в efuse
void test_read_wifi_pass_from_efuse_7_symbols(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test read_wifi_pass_from_efuse - various lengths");
    LOG_MESSAGE();

    mock_esp_efuse_set_wifi_password("testpas");

    esp_err_t result = setting_items_init_with_storage(&test_storage);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "Initialization should succeed");

    char buffer[SETTING_ITEM_MAX_STR_LEN];
    memset(buffer, 0, sizeof(buffer));

    result = setting_items_read(KEY_AP_PASS, buffer);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "Should return ESP_OK for successful read");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(ap_pass_default, buffer, "Should return MAC-based password when eFuse password is too short");
}

void test_read_wifi_pass_from_efuse_8_symbols(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test read_wifi_pass_from_efuse - various lengths");
    LOG_MESSAGE();

    mock_esp_efuse_set_wifi_password("testpass");

    esp_err_t result = setting_items_init_with_storage(&test_storage);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "Initialization should succeed");

    char buffer[SETTING_ITEM_MAX_STR_LEN];
    memset(buffer, 0, sizeof(buffer));

    result = setting_items_read(KEY_AP_PASS, buffer);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "Should return ESP_OK for successful read");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("testpass", buffer, "Should return eFuse password when length is valid");
}

void test_read_wifi_pass_from_efuse_12_symbols(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test read_wifi_pass_from_efuse - various lengths");
    LOG_MESSAGE();

    mock_esp_efuse_set_wifi_password("testpass1234");

    esp_err_t result = setting_items_init_with_storage(&test_storage);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "Initialization should succeed");

    char buffer[SETTING_ITEM_MAX_STR_LEN];
    memset(buffer, 0, sizeof(buffer));

    result = setting_items_read(KEY_AP_PASS, buffer);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "Should return ESP_OK for successful read");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("testpass1234", buffer, "Should return eFuse password when length is valid");
}

void test_read_wifi_pass_from_efuse_13_symbols(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test read_wifi_pass_from_efuse - various lengths");
    LOG_MESSAGE();

    mock_esp_efuse_set_wifi_password("testpass12345");

    esp_err_t result = setting_items_init_with_storage(&test_storage);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "Initialization should succeed");

    char buffer[SETTING_ITEM_MAX_STR_LEN];
    memset(buffer, 0, sizeof(buffer));

    result = setting_items_read(KEY_AP_PASS, buffer);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "Should return ESP_OK for successful read");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("testpass1234", buffer, "Should return truncated eFuse password when length exceeds max");
}

void test_read_wifi_pass_from_efuse_read_error(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test read_wifi_pass_from_efuse - read error");
    LOG_MESSAGE();

    mock_esp_efuse_set_wifi_password("testpass1234");
    mock_esp_efuse_read_block_return = ESP_ERR_INVALID_STATE;

    esp_err_t result = setting_items_init_with_storage(&test_storage);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "Initialization should succeed");

    char buffer[SETTING_ITEM_MAX_STR_LEN];
    memset(buffer, 0, sizeof(buffer));

    result = setting_items_read(KEY_AP_PASS, buffer);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "Should return ESP_OK for successful read");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(ap_pass_default, buffer, "Should return MAC-based password when eFuse read fails");
}

// Test setting_items_validate function
void test_setting_items_validate(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test setting_items_validate function");
    LOG_MESSAGE();

    // NULL key must return ESP_ERR_INVALID_ARG
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_ERR_INVALID_ARG,
        setting_items_validate(NULL, "100"),
        "NULL key should return ESP_ERR_INVALID_ARG"
    );

    // Unknown key must return ESP_ERR_NOT_FOUND
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_ERR_NOT_FOUND,
        setting_items_validate("nonexistent_key_xyz", "100"),
        "Unknown key should return ESP_ERR_NOT_FOUND"
    );

    // Known key with valid value must return ESP_OK
    // validate_timeout mock always returns true, so "100" is valid
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_OK,
        setting_items_validate(KEY_CACHE_VALUE_TIMEOUT_S, "100"),
        "Known key with valid value should return ESP_OK"
    );

    // Known key with invalid value: validate_hostname mock rejects empty strings
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_ERR_INVALID_ARG,
        setting_items_validate(KEY_HOSTNAME, ""),
        "Known key with invalid value should return ESP_ERR_INVALID_ARG"
    );
}

// Test that setting_items_set_defaults(false) force-overwrites existing values with defaults
void test_setting_items_set_defaults_force(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test setting_items_set_defaults - force reset overwrites existing values");
    LOG_MESSAGE();

    esp_err_t result = setting_items_init_with_storage(&test_storage);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "Initialization should succeed");

    // Read the value that was written by init (this is the effective default for this key)
    char default_buf[SETTING_ITEM_MAX_STR_LEN];
    memset(default_buf, 0, sizeof(default_buf));
    result = setting_items_read(KEY_HOSTNAME, default_buf);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "Reading initial hostname should succeed");

    // Write a custom value that differs from the default
    result = setting_items_save(KEY_HOSTNAME, "custom-hostname");
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "Saving custom hostname should succeed");

    // Confirm the custom value is stored
    char buf[SETTING_ITEM_MAX_STR_LEN];
    memset(buf, 0, sizeof(buf));
    result = setting_items_read(KEY_HOSTNAME, buf);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "Reading hostname should succeed");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("custom-hostname", buf, "Custom hostname should be stored before force reset");

    // Force-reset all settings to defaults
    result = setting_items_set_defaults(false);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "setting_items_set_defaults(false) should return ESP_OK");

    // Verify hostname was reset to the default value (not the custom value)
    memset(buf, 0, sizeof(buf));
    result = setting_items_read(KEY_HOSTNAME, buf);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "Reading hostname after force reset should succeed");

    TEST_ASSERT_NOT_EQUAL_MESSAGE(
        0, strcmp(buf, "custom-hostname"),
        "Force reset must overwrite the custom hostname — 'custom-hostname' must no longer be stored"
    );
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        default_buf, buf,
        "Hostname after force reset must equal the value written during initialization"
    );
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_setting_items_init_function);
    RUN_TEST(test_setting_items_init_with_storage);
    RUN_TEST(test_setting_items_array_contents);

    RUN_TEST(test_authentication_validators);
    RUN_TEST(test_port_validators);
    RUN_TEST(test_ip_validators);
    RUN_TEST(test_wifi_validators);
    RUN_TEST(test_serial_validators);
    RUN_TEST(test_bridge_and_bool_validators);

    RUN_TEST(test_setting_items_save_error_conditions);
    RUN_TEST(test_setting_items_read_success);
    RUN_TEST(test_setting_items_read_error_conditions);
    RUN_TEST(test_setting_items_get_key_at);
    RUN_TEST(test_setting_items_read_int);
    RUN_TEST(test_setting_items_read_bool);
    RUN_TEST(test_setting_items_save_int);
    RUN_TEST(test_setting_items_save_bool);
    RUN_TEST(test_setting_items_get_default_value);
    RUN_TEST(test_setting_items_get_type);
    RUN_TEST(test_setting_items_type_to_string);

    RUN_TEST(test_dynamic_defaults_generation_mac_error);
    RUN_TEST(test_generate_mac_based_password_short_mac);

    RUN_TEST(test_read_wifi_pass_from_efuse_7_symbols);
    RUN_TEST(test_read_wifi_pass_from_efuse_8_symbols);
    RUN_TEST(test_read_wifi_pass_from_efuse_12_symbols);
    RUN_TEST(test_read_wifi_pass_from_efuse_13_symbols);
    RUN_TEST(test_read_wifi_pass_from_efuse_read_error);

    RUN_TEST(test_setting_items_validate);
    RUN_TEST(test_setting_items_set_defaults_force);

    return UNITY_END();
}
