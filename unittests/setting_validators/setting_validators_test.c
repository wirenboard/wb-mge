#include "unity.h"
#include "console_log.h"

#include "setting_validators.h"
#include "setting_items.h"

#include "esp_efuse.h"
#include "esp_mac.h"

#include <stdio.h>
#include <string.h>

#define TEST_BUFFER_SIZE                    300

// Storage backend for the settings table: every read misses, which makes
// setting_items_read() fall back to the EFFECTIVE default — the dynamically generated
// one for hostname/ap_ssid/ap_pass. Writes are accepted so init still succeeds.
static bool stub_has_key(const char *key)
{
    (void)key;
    return false;
}

static esp_err_t stub_write_str(const char *key, const char *value)
{
    (void)key;
    (void)value;
    return ESP_OK;
}

static esp_err_t stub_read_str(const char *key, char *value)
{
    (void)key;
    (void)value;
    return ESP_ERR_NOT_FOUND;
}

static const setting_storage_iface_t stub_storage = {
    .has_key = stub_has_key,
    .write_str = stub_write_str,
    .read_str = stub_read_str,
    .erase_key = NULL,
};

void setUp(void)
{

}

void tearDown(void)
{

}

// Test validate_hostname function
void test_validate_hostname(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test validate_hostname function");
    LOG_MESSAGE();

    // Valid hostnames
    TEST_ASSERT_TRUE_MESSAGE(validate_hostname("WB-MGE"), "Simple hostname should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_hostname("device123"), "Alphanumeric hostname should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_hostname("test-device-01"), "Hostname with hyphens should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_hostname("A"), "Single character hostname should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_hostname("1234567890123456789012345678901"), "31-character hostname should be valid");

    // Invalid hostnames
    TEST_ASSERT_FALSE_MESSAGE(validate_hostname(NULL), "NULL hostname should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_hostname(""), "Empty hostname should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_hostname("12345678901234567890123456789012"), "32-character hostname should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_hostname("host.name"), "Hostname with dots should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_hostname("host_name"), "Hostname with underscores should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_hostname("host name"), "Hostname with spaces should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_hostname("host@name"), "Hostname with special characters should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_hostname("host{name"), "Hostname with brace character should be invalid");
}

// Test validate_ssid function
void test_validate_ssid(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test validate_ssid function");
    LOG_MESSAGE();

    // Valid SSIDs
    TEST_ASSERT_TRUE_MESSAGE(validate_ssid("WB-MGE"), "Simple SSID should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_ssid("wifi123"), "Alphanumeric SSID should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_ssid("test-wifi-01"), "SSID with hyphens should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_ssid("1234567890123456789012345678901"), "31-character SSID should be valid");

    // Invalid SSIDs
    TEST_ASSERT_FALSE_MESSAGE(validate_ssid(NULL), "NULL SSID should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_ssid(""), "Empty SSID should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_ssid("12345678901234567890123456789012"), "32-character SSID should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_ssid("wifi\tssid"), "SSID with tab should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_ssid("wifi\x7Fssid"), "SSID with extended ASCII should be invalid");
}

// Test validate_ssid printable-range boundary characters (0x20 space, 0x7E '~')
void test_validate_ssid_printable_range_boundaries(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test validate_ssid - inclusive printable-range boundaries 0x20 and 0x7E");
    LOG_MESSAGE();

    // Inclusive lower bound: space (0x20) is the lowest allowed printable char
    TEST_ASSERT_TRUE_MESSAGE(validate_ssid("my ssid"), "SSID with space (0x20) must be valid (inclusive lower bound)");
    TEST_ASSERT_TRUE_MESSAGE(validate_ssid(" "), "Single space (0x20) must be valid (inclusive lower bound)");

    // Inclusive upper bound: tilde (0x7E) is the highest allowed printable char
    TEST_ASSERT_TRUE_MESSAGE(validate_ssid("wifi~ssid"), "SSID with tilde (0x7E) must be valid (inclusive upper bound)");
    TEST_ASSERT_TRUE_MESSAGE(validate_ssid("~"), "Single tilde (0x7E) must be valid (inclusive upper bound)");

    // Just outside the bounds must be rejected
    TEST_ASSERT_FALSE_MESSAGE(validate_ssid("wifi\x1Fssid"), "SSID with 0x1F (just below space) must be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_ssid("wifi\x7Fssid"), "SSID with 0x7F (just above tilde) must be invalid");
}

// Test validate_sta_ssid function
void test_validate_sta_ssid(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test validate_sta_ssid function");
    LOG_MESSAGE();

    // The empty string is the factory default for sta_ssid ("no station network
    // configured"), so it must be accepted or the device rejects its own defaults
    TEST_ASSERT_TRUE_MESSAGE(validate_sta_ssid(""), "Empty station SSID should be valid (no station network configured)");

    // Non-empty values follow exactly the same rules as the AP SSID
    TEST_ASSERT_TRUE_MESSAGE(validate_sta_ssid("Station-SSID"), "Simple station SSID should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_sta_ssid("1234567890123456789012345678901"), "31-character station SSID should be valid");

    TEST_ASSERT_FALSE_MESSAGE(validate_sta_ssid(NULL), "NULL station SSID should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_sta_ssid("12345678901234567890123456789012"), "32-character station SSID should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_sta_ssid("wifi\x19ssid"), "Station SSID with control character (0x19) should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_sta_ssid("wifi\x7Fssid"), "Station SSID with character above 0x7E should be invalid");
}

// Test validate_port function
void test_validate_port(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test validate_port function");
    LOG_MESSAGE();

    // Valid ports
    TEST_ASSERT_TRUE_MESSAGE(validate_port("1"), "Port 1 should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_port("80"), "Port 80 should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_port("443"), "Port 443 should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_port("8080"), "Port 8080 should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_port("65535"), "Port 65535 should be valid");

    // Invalid ports
    TEST_ASSERT_FALSE_MESSAGE(validate_port(NULL), "NULL port should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_port(""), "Empty port should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_port("0"), "Port 0 should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_port("65536"), "Port 65536 should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_port("-1"), "Negative port should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_port("abc"), "Non-numeric port should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_port("80.5"), "Decimal port should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_port("80a"), "Port with letters should be invalid");
}

// Test validate_slave_id function
// A Modbus slave may only ever answer as 1..247: 0 is the broadcast address and 248-255
// are reserved by the specification, so both ends of that window are boundary cases.
void test_validate_slave_id(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test validate_slave_id function");
    LOG_MESSAGE();

    // Valid unit ids — the two boundaries of the allowed range
    TEST_ASSERT_TRUE_MESSAGE(validate_slave_id("1"), "Slave id 1 should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_slave_id("247"), "Slave id 247 should be valid");

    // Invalid unit ids
    TEST_ASSERT_FALSE_MESSAGE(validate_slave_id("0"), "Slave id 0 is the broadcast address, not a slave");
    TEST_ASSERT_FALSE_MESSAGE(validate_slave_id("248"), "Slave id 248 is reserved and should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_slave_id(""), "Empty slave id should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_slave_id("abc"), "Non-numeric slave id should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_slave_id(NULL), "NULL slave id should be invalid");
}

// Test validate_baudrate function
void test_validate_baudrate(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test validate_baudrate function");
    LOG_MESSAGE();

    // Valid baudrates
    TEST_ASSERT_TRUE_MESSAGE(validate_baudrate("1200"), "Baudrate 1200 should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_baudrate("9600"), "Baudrate 9600 should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_baudrate("115200"), "Baudrate 115200 should be valid");

    // Invalid baudrates
    TEST_ASSERT_FALSE_MESSAGE(validate_baudrate(NULL), "NULL baudrate should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_baudrate(""), "Empty baudrate should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_baudrate("1199"), "Baudrate 1199 should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_baudrate("115201"), "Baudrate 115201 should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_baudrate("0"), "Baudrate 0 should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_baudrate("abc"), "Non-numeric baudrate should be invalid");
}

// Test validate_stopbits function
void test_validate_stopbits(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test validate_stopbits function");
    LOG_MESSAGE();

    // Valid stopbits
    TEST_ASSERT_TRUE_MESSAGE(validate_stopbits("1"), "Stopbits '1' should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_stopbits("1.5"), "Stopbits '1.5' should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_stopbits("2"), "Stopbits '2' should be valid");

    // Invalid stopbits
    TEST_ASSERT_FALSE_MESSAGE(validate_stopbits(NULL), "NULL stopbits should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_stopbits(""), "Empty stopbits should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_stopbits("0"), "Stopbits '0' should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_stopbits("3"), "Stopbits '3' should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_stopbits("1.0"), "Stopbits '1.0' should be invalid");
}

// Test validate_parity function
void test_validate_parity(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test validate_parity function");
    LOG_MESSAGE();

    // Valid parity
    TEST_ASSERT_TRUE_MESSAGE(validate_parity("none"), "Parity 'none' should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_parity("even"), "Parity 'even' should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_parity("odd"), "Parity 'odd' should be valid");

    // Invalid parity
    TEST_ASSERT_FALSE_MESSAGE(validate_parity(NULL), "NULL parity should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_parity(""), "Empty parity should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_parity("mark"), "Parity 'mark' should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_parity("space"), "Parity 'space' should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_parity("NONE"), "Uppercase parity should be invalid");
}

// Test validate_databits function
void test_validate_databits(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test validate_databits function");
    LOG_MESSAGE();

    // Valid databits
    TEST_ASSERT_TRUE_MESSAGE(validate_databits("5"), "Databits '5' should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_databits("6"), "Databits '6' should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_databits("7"), "Databits '7' should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_databits("8"), "Databits '8' should be valid");

    // Invalid databits
    TEST_ASSERT_FALSE_MESSAGE(validate_databits(NULL), "NULL databits should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_databits(""), "Empty databits should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_databits("4"), "Databits '4' should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_databits("9"), "Databits '9' should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_databits("0"), "Databits '0' should be invalid");
}

// Test validate_ip function
void test_validate_ip(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test validate_ip function");
    LOG_MESSAGE();

    // Valid IP addresses
    TEST_ASSERT_TRUE_MESSAGE(validate_ip("192.168.1.1"), "Standard IP should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_ip("0.0.0.0"), "Zero IP should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_ip("255.255.255.255"), "Max IP should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_ip("10.0.0.1"), "Private IP should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_ip("172.16.0.1"), "Private IP range should be valid");

    // Invalid IP addresses
    TEST_ASSERT_FALSE_MESSAGE(validate_ip(NULL), "NULL IP should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_ip(""), "Empty IP should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_ip("-1.1.1.1"), "IP with negative first octet should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_ip("256.1.1.1"), "IP with octet > 255 should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_ip("1.-1.1.1"), "IP with negative second octet should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_ip("1.256.1.1"), "IP with second octet > 255 should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_ip("1.1.-1.1"), "IP with negative third octet should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_ip("1.1.256.1"), "IP with third octet > 255 should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_ip("1.1.1.-1"), "IP with negative fourth octet should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_ip("1.1.1.256"), "IP with fourth octet > 255 should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_ip("192.168.abc.1"), "IP with non-numeric octet should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_ip("192.168.1.1 "), "IP with trailing space should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_ip("192.168.1"), "Incomplete IP should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_ip("192.168.1.1.1"), "IP with extra octet should be invalid");
}

// Test validate_wifi_mode function
void test_validate_wifi_mode(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test validate_wifi_mode function");
    LOG_MESSAGE();

    // Valid WiFi modes
    TEST_ASSERT_TRUE_MESSAGE(validate_wifi_mode("ap"), "WiFi mode 'ap' should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_wifi_mode("sta"), "WiFi mode 'sta' should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_wifi_mode("apsta"), "WiFi mode 'apsta' should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_wifi_mode("none"), "WiFi mode 'none' should be valid");

    // Invalid WiFi modes
    TEST_ASSERT_FALSE_MESSAGE(validate_wifi_mode(NULL), "NULL WiFi mode should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_wifi_mode(""), "Empty WiFi mode should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_wifi_mode("client"), "WiFi mode 'client' should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_wifi_mode("AP"), "Uppercase WiFi mode should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_wifi_mode("station"), "WiFi mode 'station' should be invalid");
}

// Test validate_wifi_auth function
void test_validate_wifi_auth(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test validate_wifi_auth function");
    LOG_MESSAGE();

    // Valid WiFi auth modes
    TEST_ASSERT_TRUE_MESSAGE(validate_wifi_auth("open"), "WiFi auth 'open' should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_wifi_auth("wpa2_psk"), "WiFi auth 'wpa2_psk' should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_wifi_auth("wpa3_psk"), "WiFi auth 'wpa3_psk' should be valid");

    // Invalid WiFi auth modes
    TEST_ASSERT_FALSE_MESSAGE(validate_wifi_auth(NULL), "NULL WiFi auth should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_wifi_auth(""), "Empty WiFi auth should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_wifi_auth("wep"), "WiFi auth 'wep' should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_wifi_auth("wpa"), "WiFi auth 'wpa' should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_wifi_auth("OPEN"), "Uppercase WiFi auth should be invalid");
}

// Test validate_update_channel function
void test_validate_update_channel(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test validate_update_channel function");
    LOG_MESSAGE();

    // Valid update channels
    TEST_ASSERT_TRUE_MESSAGE(validate_update_channel("stable"), "Update channel 'stable' should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_update_channel("testing"), "Update channel 'testing' should be valid");

    // Invalid update channels
    TEST_ASSERT_FALSE_MESSAGE(validate_update_channel(NULL), "NULL update channel should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_update_channel(""), "Empty update channel should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_update_channel("unstable"),
                              "Update channel 'unstable' should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_update_channel("Stable"),
                              "Uppercase update channel should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_update_channel("stable "),
                              "Update channel with a trailing space should be invalid");

    // A value longer than the setting string buffer must not be accepted
    char too_long[SETTING_ITEM_MAX_STR_LEN + 10];
    memset(too_long, 'a', sizeof(too_long) - 1);
    too_long[sizeof(too_long) - 1] = '\0';
    TEST_ASSERT_FALSE_MESSAGE(validate_update_channel(too_long),
                              "Update channel longer than SETTING_ITEM_MAX_STR_LEN should be invalid");
}

// Test validate_bool function
void test_validate_bool(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test validate_bool function");
    LOG_MESSAGE();

    // Valid boolean values
    TEST_ASSERT_TRUE_MESSAGE(validate_bool("true"), "Boolean 'true' should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_bool("false"), "Boolean 'false' should be valid");

    // Invalid boolean values
    TEST_ASSERT_FALSE_MESSAGE(validate_bool(NULL), "NULL boolean should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_bool(""), "Empty boolean should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_bool("TRUE"), "Uppercase boolean should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_bool("FALSE"), "Uppercase boolean should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_bool("1"), "Numeric boolean should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_bool("0"), "Numeric boolean should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_bool("yes"), "Alternative boolean should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_bool("no"), "Alternative boolean should be invalid");
}

// Test validate_login function
void test_validate_login(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test validate_login function");
    LOG_MESSAGE();

    // Valid logins
    TEST_ASSERT_TRUE_MESSAGE(validate_login("admin"), "Login 'admin' should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_login("user123"), "Alphanumeric login should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_login("test_user"), "Login with underscore should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_login("test-user"), "Login with hyphen should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_login("A"), "Single character login should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_login("1234567890123456789012345678901"), "31-character login should be valid");

    // Invalid logins
    TEST_ASSERT_FALSE_MESSAGE(validate_login(NULL), "NULL login should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_login(""), "Empty login should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_login("12345678901234567890123456789012"), "32-character login should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_login("user name"), "Login with space should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_login("user@domain"), "Login with special characters should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_login("user.name"), "Login with dot should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_login("user|name"), "Login with pipe should be invalid");
}

// Test validate_password function
void test_validate_password(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test validate_password function");
    LOG_MESSAGE();

    // Valid passwords
    TEST_ASSERT_TRUE_MESSAGE(validate_password("admin"), "Simple password should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_password("Password123!"), "Complex password should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_password(" "), "Single space password should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_password("~"), "Tilde password should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_password("1234567890123456789012345678901"), "31-character password should be valid");

    // Invalid passwords
    TEST_ASSERT_FALSE_MESSAGE(validate_password(NULL), "NULL password should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_password(""), "Empty password should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_password("12345678901234567890123456789012"), "32-character password should be invalid");

    // Test non-printable ASCII characters (coverage for ASCII range check)
    char invalid_password_with_tab[] = "pass\tword";  // Tab character (ASCII 9)
    TEST_ASSERT_FALSE_MESSAGE(validate_password(invalid_password_with_tab), "Password with tab character should be invalid");

    char invalid_password_with_newline[] = "pass\nword";  // Newline character (ASCII 10)
    TEST_ASSERT_FALSE_MESSAGE(validate_password(invalid_password_with_newline), "Password with newline character should be invalid");

    char invalid_password_with_del[] = "password\x7F";  // DEL character (ASCII 127)
    TEST_ASSERT_FALSE_MESSAGE(validate_password(invalid_password_with_del), "Password with DEL character should be invalid");

    char invalid_password_with_null[] = "pass\x1Fword";  // Unit Separator character (ASCII 31)
    TEST_ASSERT_FALSE_MESSAGE(validate_password(invalid_password_with_null), "Password with control character should be invalid");
}

// Test validate_wifi_password function (sta_pass / ap_pass)
void test_validate_wifi_password(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test validate_wifi_password function");
    LOG_MESSAGE();

    // Valid Wi-Fi passphrases
    TEST_ASSERT_TRUE_MESSAGE(validate_wifi_password(""), "Empty Wi-Fi password (open network) should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_wifi_password("12345678"), "8-character Wi-Fi password should be valid (inclusive lower length bound)");
    TEST_ASSERT_TRUE_MESSAGE(validate_wifi_password("pass with space"), "Wi-Fi password with space should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_wifi_password("P@ssw0rd!~"), "Wi-Fi password with special chars and tilde (0x7E upper bound) should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_wifi_password("        "), "8 spaces (0x20 lower printable bound) should be valid");

    // Exactly 63 printable chars is the maximum WPA2 passphrase length (valid)
    char wifi_pass_63[64];
    memset(wifi_pass_63, 'A', 63);
    wifi_pass_63[63] = '\0';
    TEST_ASSERT_TRUE_MESSAGE(validate_wifi_password(wifi_pass_63), "63-character Wi-Fi password should be valid (inclusive upper length bound)");

    // Invalid Wi-Fi passphrases
    TEST_ASSERT_FALSE_MESSAGE(validate_wifi_password(NULL), "NULL Wi-Fi password should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_wifi_password("1234567"), "7-character (non-empty, too short) Wi-Fi password should be invalid");

    // 64 printable chars exceeds the maximum length (invalid)
    char wifi_pass_64[65];
    memset(wifi_pass_64, 'A', 64);
    wifi_pass_64[64] = '\0';
    TEST_ASSERT_FALSE_MESSAGE(validate_wifi_password(wifi_pass_64), "64-character Wi-Fi password should be invalid");

    // Non-printable characters within an otherwise valid-length password
    char wifi_pass_with_tab[] = "pass\twords";  // Tab character (ASCII 9)
    TEST_ASSERT_FALSE_MESSAGE(validate_wifi_password(wifi_pass_with_tab), "Wi-Fi password with tab character should be invalid");

    char wifi_pass_with_us[] = "pass\x1Fwords";  // Unit Separator character (ASCII 31)
    TEST_ASSERT_FALSE_MESSAGE(validate_wifi_password(wifi_pass_with_us), "Wi-Fi password with control character (0x1F) should be invalid");

    char wifi_pass_with_del[] = "password\x7F";  // DEL character (ASCII 127)
    TEST_ASSERT_FALSE_MESSAGE(validate_wifi_password(wifi_pass_with_del), "Wi-Fi password with DEL character (0x7F) should be invalid");
}

/* The mixer program, as the settings API sees it: one base64 string holding four-byte steps.
 * The bytes behind each constant, so a failure says what was being stored:
 *   B64_PROG_ONE_STEP    01 14 00 78              forward, 200 rpm, 120 s
 *   B64_PROG_ZERO_DUR    01 14 00 00              forward, 200 rpm, no time at all
 *   B64_PROG_ZERO_SPEED  01 00 00 0A              forward at no speed, 10 s
 *   B64_PROG_FIVE_BYTES  01 14 00 78 02           a step and a quarter
 *   B64_PROG_TWELVE      twelve steps, one more than a setting value can hold */
#define B64_PROG_ONE_STEP       "ARQAeA=="
#define B64_PROG_ZERO_DUR       "ARQAAA=="
#define B64_PROG_ZERO_SPEED     "AQAACg=="
#define B64_PROG_FIVE_BYTES     "ARQAeAI="
#define B64_PROG_ELEVEN         "AQEAAQECAAIBAwADAQQABAEFAAUBBgAGAQcABwEIAAgBCQAJAQoACgELAAs="
#define B64_PROG_TWELVE         "AQEAAQECAAIBAwADAQQABAEFAAUBBgAGAQcABwEIAAgBCQAJAQoACgELAAsBDAAM"

// Test validate_mqtt_id function
// This one is the strict one of the three MQTT strings: what it accepts is pasted straight into
// /devices/<id>/..., so a value carrying a character MQTT reads as structure would either split
// the id across topic levels or turn this device's own topics into a wildcard.
void test_validate_mqtt_id(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test validate_mqtt_id function");
    LOG_MESSAGE();

    // The empty string is the factory default and means "build an id from the model and the MAC"
    TEST_ASSERT_TRUE_MESSAGE(validate_mqtt_id(""), "An unnamed device should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_mqtt_id("WB-MGE-030405"), "The default-shaped id should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_mqtt_id("mixer_1"), "Underscores and digits should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_mqtt_id("~"), "Tilde (0x7E) is the top of the printable range");

    // The three characters that mean something to a broker
    TEST_ASSERT_FALSE_MESSAGE(validate_mqtt_id("wb/mge"), "'/' would split the id across topic levels");
    TEST_ASSERT_FALSE_MESSAGE(validate_mqtt_id("/"), "An id that is only a separator should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mqtt_id("wb+mge"), "'+' is the single-level subscription wildcard");
    TEST_ASSERT_FALSE_MESSAGE(validate_mqtt_id("+"), "A bare '+' should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mqtt_id("wb#mge"), "'#' is the multi-level subscription wildcard");
    TEST_ASSERT_FALSE_MESSAGE(validate_mqtt_id("#"), "A bare '#' would subscribe this device to everything");

    // A space is printable, so it has to be refused by name rather than by the range
    TEST_ASSERT_FALSE_MESSAGE(validate_mqtt_id("wb mge"), "A space in a topic should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mqtt_id(" "), "A single space should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mqtt_id("wb-mge "), "A trailing space should be invalid");

    TEST_ASSERT_FALSE_MESSAGE(validate_mqtt_id(NULL), "NULL id should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mqtt_id("wb\tmge"), "An id with a tab should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mqtt_id("wb\nmge"), "An id with a newline should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mqtt_id("wb\x1Fmge"), "An id with a control character should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mqtt_id("wb\x7Fmge"), "An id with DEL should be invalid");
    // UTF-8 Cyrillic: every byte of it is above 0x7E, and a topic is ASCII
    TEST_ASSERT_FALSE_MESSAGE(validate_mqtt_id("\xD0\xBC\xD0\xB5\xD1\x88\xD0\xB0\xD0\xBB\xD0\xBA\xD0\xB0"),
                              "A non-ASCII id should be invalid");

    // 63 bytes is the whole room a setting value has
    char id_63[64];
    memset(id_63, 'a', 63);
    id_63[63] = '\0';
    TEST_ASSERT_TRUE_MESSAGE(validate_mqtt_id(id_63), "A 63-character id should be valid");

    char id_64[65];
    memset(id_64, 'a', 64);
    id_64[64] = '\0';
    TEST_ASSERT_FALSE_MESSAGE(validate_mqtt_id(id_64), "A 64-character id should be invalid");
}

// Test validate_mqtt_host function
void test_validate_mqtt_host(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test validate_mqtt_host function");
    LOG_MESSAGE();

    // The empty string is the factory default: no broker configured, and how a user removes one
    TEST_ASSERT_TRUE_MESSAGE(validate_mqtt_host(""), "No broker configured should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_mqtt_host("192.168.1.10"), "An IP literal should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_mqtt_host("wirenboard-A1B2.local"), "A host name should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_mqtt_host("broker"), "A bare host name should be valid");

    TEST_ASSERT_FALSE_MESSAGE(validate_mqtt_host(NULL), "NULL host should be invalid");
    // A host name cannot contain a space, so one is a typo rather than an address
    TEST_ASSERT_FALSE_MESSAGE(validate_mqtt_host("192.168.1.10 "), "A trailing space should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mqtt_host(" broker"), "A leading space should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mqtt_host("my broker"), "A space inside the host should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mqtt_host(" "), "A single space should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mqtt_host("broker\tlocal"), "A host with a tab should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mqtt_host("broker\nlocal"), "A host with a newline should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mqtt_host("broker\x7F"), "A host with DEL should be invalid");

    char host_63[64];
    memset(host_63, 'b', 63);
    host_63[63] = '\0';
    TEST_ASSERT_TRUE_MESSAGE(validate_mqtt_host(host_63), "A 63-character host should be valid");

    char host_64[65];
    memset(host_64, 'b', 64);
    host_64[64] = '\0';
    TEST_ASSERT_FALSE_MESSAGE(validate_mqtt_host(host_64), "A 64-character host should be invalid");
}

// Test validate_mqtt_credential function (mqtt_user / mqtt_pass)
void test_validate_mqtt_credential(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test validate_mqtt_credential function");
    LOG_MESSAGE();

    // Both may be empty: an anonymous connection is the common case on a controller's own bus
    TEST_ASSERT_TRUE_MESSAGE(validate_mqtt_credential(""), "An anonymous connection should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_mqtt_credential("wirenboard"), "A plain user name should be valid");
    // Unlike the id, a credential never reaches a topic, so a space in it is just a character
    TEST_ASSERT_TRUE_MESSAGE(validate_mqtt_credential("pass word"), "A space should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_mqtt_credential("p@ss:w0rd/+#"), "Punctuation should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_mqtt_credential("~"), "Tilde (0x7E) should be valid");

    TEST_ASSERT_FALSE_MESSAGE(validate_mqtt_credential(NULL), "NULL credential should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mqtt_credential("pass\tword"), "A tab should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mqtt_credential("pass\nword"), "A newline should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mqtt_credential("pass\x1Fword"), "A control character should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mqtt_credential("password\x7F"), "DEL should be invalid");

    char credential_63[64];
    memset(credential_63, 'c', 63);
    credential_63[63] = '\0';
    TEST_ASSERT_TRUE_MESSAGE(validate_mqtt_credential(credential_63), "63 characters should be valid");

    char credential_64[65];
    memset(credential_64, 'c', 64);
    credential_64[64] = '\0';
    TEST_ASSERT_FALSE_MESSAGE(validate_mqtt_credential(credential_64), "64 characters should be invalid");
}

// Test validate_mixer_prog_index function
// Four programs are stored (mix_p0..mix_p3), so the active one is 0..3 and nothing else.
void test_validate_mixer_prog_index(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test validate_mixer_prog_index function");
    LOG_MESSAGE();

    TEST_ASSERT_TRUE_MESSAGE(validate_mixer_prog_index("0"), "Program 0 should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_mixer_prog_index("3"), "Program 3 should be valid");

    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_prog_index("4"), "There is no fifth program");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_prog_index("-1"), "A negative index should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_prog_index(""), "An empty index should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_prog_index(NULL), "NULL should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_prog_index("abc"), "A non-numeric index should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_prog_index("1.5"), "A fractional index should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_prog_index("2x"), "Trailing rubbish should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_prog_index("99999999999999999999"),
                              "An index that overflows a long should be invalid");
}

// Test validate_mixer_prog_name function
// A program name is a label a human reads and types, so it carries whatever alphabet that human
// writes in - the length bound is in BYTES and a UTF-8 name simply spends more of them.
void test_validate_mixer_prog_name(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test validate_mixer_prog_name function");
    LOG_MESSAGE();

    TEST_ASSERT_TRUE_MESSAGE(validate_mixer_prog_name("Program 1"), "The factory default should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_mixer_prog_name("A"), "A one-character name should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_mixer_prog_name("Slip 50/50 + water"), "Punctuation should be valid");
    // UTF-8 Cyrillic for a slip mixing program: two bytes per character, and all of them above
    // the ASCII range that the id and the host are held to
    TEST_ASSERT_TRUE_MESSAGE(validate_mixer_prog_name("\xD0\xA8\xD0\xBB\xD0\xB8\xD0\xBA\xD0\xB5\xD1\x80"),
                             "A Cyrillic name should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_mixer_prog_name("\xE5\x9C\x9F"), "A three-byte UTF-8 name should be valid");

    // A nameless program cannot be told from another one in the web UI or in the MQTT selector
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_prog_name(""), "An empty name should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_prog_name(NULL), "NULL should be invalid");

    // Control codes would break the JSON of GET /settings and mean nothing on a button
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_prog_name("Slip\tmix"), "A tab should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_prog_name("Slip\nmix"), "A newline should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_prog_name("Slip\rmix"), "A carriage return should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_prog_name("Slip\x1Fmix"), "A control character should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_prog_name("Slip\x7F"), "DEL should be invalid");

    // 63 bytes is the whole room a setting value has, in either alphabet
    char name_63[64];
    memset(name_63, 'n', 63);
    name_63[63] = '\0';
    TEST_ASSERT_TRUE_MESSAGE(validate_mixer_prog_name(name_63), "A 63-byte name should be valid");

    char name_64[65];
    memset(name_64, 'n', 64);
    name_64[64] = '\0';
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_prog_name(name_64), "A 64-byte name should be invalid");

    // The same bound as the one above, spent on 31 two-byte characters plus one ASCII one
    char cyrillic_63[64];
    for (int i = 0; i < 62; i += 2) {
        cyrillic_63[i] = (char)0xD0;
        cyrillic_63[i + 1] = (char)0xA8;
    }
    cyrillic_63[62] = 'x';
    cyrillic_63[63] = '\0';
    TEST_ASSERT_TRUE_MESSAGE(validate_mixer_prog_name(cyrillic_63),
                             "31 Cyrillic characters and one more byte should still fit");
}

// Test validate_mixer_rpm_50hz function
// The gearbox ratio: how many rpm the shaft does at 50 Hz. It is the divisor that turns a step's
// speed into a frequency, so zero is not a ratio.
void test_validate_mixer_rpm_50hz(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test validate_mixer_rpm_50hz function");
    LOG_MESSAGE();

    TEST_ASSERT_TRUE_MESSAGE(validate_mixer_rpm_50hz("1"), "One rpm at 50 Hz is the lower bound");
    TEST_ASSERT_TRUE_MESSAGE(validate_mixer_rpm_50hz("1500"), "The factory default should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_mixer_rpm_50hz("20000"), "20000 rpm is the upper bound");

    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_rpm_50hz("0"), "Zero would be a division by zero");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_rpm_50hz("-1500"), "A shaft does not turn backwards here");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_rpm_50hz("20001"), "Above the upper bound should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_rpm_50hz(""), "An empty value should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_rpm_50hz(NULL), "NULL should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_rpm_50hz("fast"), "A non-numeric value should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_rpm_50hz("1500rpm"), "Trailing rubbish should be invalid");
}

// Test validate_mixer_vfd_port function
// The setting numbers the RS-485 ports 1..2 the way the case does; the engine subtracts one.
void test_validate_mixer_vfd_port(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test validate_mixer_vfd_port function");
    LOG_MESSAGE();

    TEST_ASSERT_TRUE_MESSAGE(validate_mixer_vfd_port("1"), "Port 1 should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_mixer_vfd_port("2"), "Port 2 should be valid");

    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_vfd_port("0"),
                              "The ports are numbered from one, so 0 is not a port");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_vfd_port("3"), "The device has two RS-485 ports");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_vfd_port("-1"), "A negative port should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_vfd_port(""), "An empty port should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_vfd_port(NULL), "NULL should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_vfd_port("1a"), "Trailing rubbish should be invalid");
}

// Test validate_mixer_rev_code function
// Reverse is command 5 on new drive revisions and 3 on old ones. Nothing else is a reverse, so
// the range is those two values and NOT 3..5 - 4 is not a command the drive knows.
void test_validate_mixer_rev_code(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test validate_mixer_rev_code function");
    LOG_MESSAGE();

    TEST_ASSERT_TRUE_MESSAGE(validate_mixer_rev_code("3"), "Old revisions want 3");
    TEST_ASSERT_TRUE_MESSAGE(validate_mixer_rev_code("5"), "New ones want 5");

    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_rev_code("4"), "4 is between the two and is neither");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_rev_code("1"), "1 is the forward command, not reverse");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_rev_code("2"), "2 is the stop command, not reverse");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_rev_code("0"), "Zero is not a command");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_rev_code(""), "An empty code should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_rev_code(NULL), "NULL should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_rev_code("rev"), "A non-numeric code should be invalid");
}

// Test validate_mixer_program function
// The whole mixing program travels as one base64 string, which is the largest piece of
// untrusted input the settings API takes: everything this accepts is decoded by the engine
// straight into steps the drive is then run through.
void test_validate_mixer_program(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test validate_mixer_program function");
    LOG_MESSAGE();

    // The empty string is the factory default and how a user clears a program
    TEST_ASSERT_TRUE_MESSAGE(validate_mixer_program(""), "An empty program should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_mixer_program(B64_PROG_ONE_STEP), "One step should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_mixer_program(B64_PROG_ELEVEN),
                             "Eleven steps are what a setting value holds");

    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_program(NULL), "NULL should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_program("!!!!"),
                              "Characters outside the base64 alphabet should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_program("ARQAeA="),
                              "Truncated base64 padding should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_program("ARQAeA"),
                              "A length that is not a whole base64 quantum should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_program(B64_PROG_FIVE_BYTES),
                              "Five bytes is not a whole number of four-byte steps");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_program(B64_PROG_TWELVE),
                              "A twelfth step does not fit a setting value");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_program(B64_PROG_ZERO_DUR),
                              "A step of zero seconds should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_program(B64_PROG_ZERO_SPEED),
                              "A motion step at zero rpm should be invalid");
    TEST_ASSERT_FALSE_MESSAGE(validate_mixer_program("AxQACg=="),
                              "Step type 3 does not exist and should be invalid");
}

// Table-wide invariant: every entry of the settings table must be accepted by the real
// validator bound to it. A key whose own default its own validator rejects is unusable
// over the settings API — the device writes the value on first boot, exports it via
// GET /settings and then refuses the very same document on POST /settings, and because
// validation is all-or-nothing that kills the whole import. sta_ssid was exactly that
// case: an empty DEFAULT_STA_SSID bound to the AP-strict validate_ssid.
//
// This lives in the setting_validators suite rather than next to the table because the
// setting_items suite links mock validators that accept almost anything, so the same
// loop there would prove nothing.
void test_setting_defaults_pass_their_validators(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test that every settings default is accepted by its own validator");
    LOG_MESSAGE();

    // Pin the inputs of the generated defaults instead of inheriting whatever the shared
    // mocks happen to initialise themselves to. Both generators derive from the MAC (the
    // eFuse is reset to "no factory Wi-Fi password", which is the documented MAC-fallback
    // path), and both cache their result on first use — so this must run before the first
    // read below. Should esp_read_mac() ever start failing by default, the generators
    // would quietly return the literal table defaults and the second half of this test
    // would stop covering anything.
    mock_esp_efuse_reset();
    mock_esp_mac_reset();

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, setting_items_init_with_storage(&stub_storage),
                                  "Initialising the settings table should succeed");

    size_t count = setting_items_get_count();
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, count, "Settings table should not be empty");

    for (size_t i = 0; i < count; i++) {
        const char *key = setting_items_get_key_at(i);
        TEST_ASSERT_NOT_NULL_MESSAGE(key, "Key should not be NULL");

        char log_message[TEST_BUFFER_SIZE];

        // The literal default stored in the table
        const char *table_default = setting_items_get_default_value(key);
        TEST_ASSERT_NOT_NULL_MESSAGE(table_default, "Default value should not be NULL");
        snprintf(log_message, sizeof(log_message),
                 "Table default '%s' of setting '%s' is rejected by its own validator", table_default, key);
        TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, setting_items_validate(key, table_default), log_message);

        // The value the device actually writes on first boot and then exports — for
        // hostname, ap_ssid and ap_pass that is generated at runtime, not the literal above
        char effective_default[SETTING_ITEM_MAX_STR_LEN] = {0};
        TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, setting_items_read(key, effective_default),
                                      "Reading the effective default should succeed");
        snprintf(log_message, sizeof(log_message),
                 "Effective default '%s' of setting '%s' is rejected by its own validator", effective_default, key);
        TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, setting_items_validate(key, effective_default), log_message);

        // Guard the check above against degrading into a copy of the one before it: a
        // generator that falls back (esp_read_mac() failing is how that happens today)
        // returns exactly the table default, and the generated path would then go
        // untested without a single test failing. This catches hostname and ap_ssid today;
        // for ap_pass the fallback is the literal "wirenboard" rather than the empty table
        // default, so that branch is a forward guard for the day either side changes.
        if ((strncmp(key, KEY_HOSTNAME, SETTING_ITEM_MAX_STR_LEN) == 0) ||
            (strncmp(key, KEY_AP_SSID, SETTING_ITEM_MAX_STR_LEN) == 0) ||
            (strncmp(key, KEY_AP_PASS, SETTING_ITEM_MAX_STR_LEN) == 0)) {
            snprintf(log_message, sizeof(log_message),
                     "Setting '%s' must get a generated default, but it fell back to the table default '%s'",
                     key, table_default);
            TEST_ASSERT_TRUE_MESSAGE(strncmp(effective_default, table_default, SETTING_ITEM_MAX_STR_LEN) != 0,
                                     log_message);
        }
    }
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_validate_hostname);
    RUN_TEST(test_validate_ssid);
    RUN_TEST(test_validate_ssid_printable_range_boundaries);
    RUN_TEST(test_validate_sta_ssid);
    RUN_TEST(test_validate_port);
    RUN_TEST(test_validate_slave_id);
    RUN_TEST(test_validate_baudrate);
    RUN_TEST(test_validate_stopbits);
    RUN_TEST(test_validate_parity);
    RUN_TEST(test_validate_databits);
    RUN_TEST(test_validate_ip);
    RUN_TEST(test_validate_wifi_mode);
    RUN_TEST(test_validate_wifi_auth);
    RUN_TEST(test_validate_update_channel);
    RUN_TEST(test_validate_bool);
    RUN_TEST(test_validate_login);
    RUN_TEST(test_validate_password);
    RUN_TEST(test_validate_wifi_password);

    RUN_TEST(test_validate_mqtt_id);
    RUN_TEST(test_validate_mqtt_host);
    RUN_TEST(test_validate_mqtt_credential);
    RUN_TEST(test_validate_mixer_prog_index);
    RUN_TEST(test_validate_mixer_prog_name);
    RUN_TEST(test_validate_mixer_rpm_50hz);
    RUN_TEST(test_validate_mixer_vfd_port);
    RUN_TEST(test_validate_mixer_rev_code);
    RUN_TEST(test_validate_mixer_program);

    RUN_TEST(test_setting_defaults_pass_their_validators);

    return UNITY_END();
}
