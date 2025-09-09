#include "unity.h"
#include "console_log.h"

#include "array_size.h"
#include "esp_log.h"
#include "ram_storage.h"
#include "setting_items.h"
#include "setting_validators.h"

#include <string.h>

#define SETTING_ITEMS_COUNT                         44

setting_storage_iface_t test_storage = {
    .has_key = rams_has_key,
    .write_str = rams_write_str,
    .read_str = rams_read_str,
};

static int mock_storage_read_error_code = ESP_OK;
static int mock_storage_write_error_code = ESP_OK;

static int mock_storage_read_with_error(const char* key, char* value)
{
    (void)key;
    (void)value;
    return mock_storage_read_error_code;
}

static int mock_storage_write_with_error(const char* key, const char* value)
{
    (void)key;
    (void)value;
    return mock_storage_write_error_code;
}

void esp_log_level_set(const char* tag, esp_log_level_t level)
{
    (void)tag;
    (void)level;
}

void setUp(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_BLUE, "SETUP: Initializing...");
    LOG_MESSAGE();

    mock_reset_validator_flags();

    rams_init();
    setting_items_init_with_storage(&test_storage);
}

void tearDown(void)
{

}

// Тестируем инициализацию setting_items, write_str возвращает ошибку -> setting_items_init должен вернуть ошибку
void test_setting_items_init_function(void) {
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test setting_items_init function");
    LOG_MESSAGE();

    esp_err_t result = setting_items_init();
    TEST_ASSERT_EQUAL_INT(ESP_FAIL, result);
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
        TEST_ASSERT_EQUAL_INT(ESP_OK, setting_items_save(bridge_mode_keys[i], "tcp_client"));
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
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_ERR_INVALID_ARG,
        setting_items_save(valid_key, NULL),
        "Should return ESP_ERR_INVALID_ARG for NULL value"
    );

    // Test 3: Unknown/invalid key
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_ERR_NOT_FOUND,
        setting_items_save(invalid_key, valid_value),
        "Should return ESP_ERR_NOT_FOUND for unknown key"
    );

    // Test 4: Validator rejects the value
    mock_reset_validator_flags();
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_ERR_INVALID_ARG,
        setting_items_save(valid_key, invalid_value),
        "Should return ESP_ERR_INVALID_ARG when validator rejects value"
    );
    TEST_ASSERT_TRUE_MESSAGE(mock_validate_hostname_called, "Validator should be called even when it fails");

    // Test storage write error
    setting_storage_iface_t error_storage = {
        .has_key = rams_has_key,
        .write_str = mock_storage_write_with_error,
        .read_str = rams_read_str,
    };

    setting_items_init_with_storage(&error_storage);
    mock_reset_validator_flags();
    mock_storage_write_error_code = ESP_ERR_NO_MEM;

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_ERR_NO_MEM,
        setting_items_save(valid_key, valid_value),
        "Should return ESP_ERR_NO_MEM when storage write fails"
    );
    TEST_ASSERT_TRUE_MESSAGE(mock_validate_hostname_called, "Validator should be called even when write fails");

    mock_storage_write_error_code = ESP_OK;
}

// Тестируем успешное чтение настроек
void test_setting_items_read_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test setting_items_read success");
    LOG_MESSAGE();

    const char* test_key = KEY_HOSTNAME;
    const char* test_value = "logged-hostname";
    char read_buffer[128];

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
    char read_buffer[128];

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
    setting_storage_iface_t error_storage = {
        .has_key = rams_has_key,
        .write_str = rams_write_str,
        .read_str = mock_storage_read_with_error,
    };

    setting_items_init_with_storage(&error_storage);

    mock_storage_read_error_code = ESP_ERR_NO_MEM;
    memset(read_buffer, 0, sizeof(read_buffer));

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_OK,
        setting_items_read(valid_key, read_buffer),
        "Should return ESP_OK even when storage returns ESP_ERR_NO_MEM"
    );
    TEST_ASSERT_TRUE_MESSAGE(
        strlen(read_buffer) > 0,
        "Should return default value"
    );

    mock_storage_read_error_code = ESP_ERR_NOT_FOUND;
    memset(read_buffer, 0, sizeof(read_buffer));

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_OK,
        setting_items_read(valid_key, read_buffer),
        "Should return ESP_OK when storage returns ESP_ERR_NOT_FOUND"
    );
    TEST_ASSERT_TRUE_MESSAGE(
        strlen(read_buffer) > 0,
        "Should return default value when key is not found in storage"
    );

    mock_storage_read_error_code = ESP_OK;
}

// Тестируем функцию setting_items_get_count
void test_setting_items_get_count(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test setting_items_get_count function");
    LOG_MESSAGE();

    size_t count = setting_items_get_count();
    TEST_ASSERT_EQUAL_INT_MESSAGE(SETTING_ITEMS_COUNT, count, "Should return correct number of setting items");
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

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_setting_items_init_function);
    RUN_TEST(test_authentication_validators);
    RUN_TEST(test_port_validators);
    RUN_TEST(test_ip_validators);
    RUN_TEST(test_wifi_validators);
    RUN_TEST(test_serial_validators);
    RUN_TEST(test_bridge_and_bool_validators);
    RUN_TEST(test_setting_items_save_error_conditions);
    RUN_TEST(test_setting_items_read_success);
    RUN_TEST(test_setting_items_read_error_conditions);
    RUN_TEST(test_setting_items_get_count);
    RUN_TEST(test_setting_items_get_key_at);

    return UNITY_END();
}
