#include "unity.h"
#include "console_log.h"

#include "setting_validators.h"

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
    TEST_ASSERT_TRUE_MESSAGE(validate_password(""), "Empty password should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_password("Password123!"), "Complex password should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_password(" "), "Single space password should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_password("~"), "Tilde password should be valid");
    TEST_ASSERT_TRUE_MESSAGE(validate_password("1234567890123456789012345678901"), "31-character password should be valid");

    // Invalid passwords
    TEST_ASSERT_FALSE_MESSAGE(validate_password(NULL), "NULL password should be invalid");
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

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_validate_hostname);
    RUN_TEST(test_validate_ssid);
    RUN_TEST(test_validate_port);
    RUN_TEST(test_validate_baudrate);
    RUN_TEST(test_validate_stopbits);
    RUN_TEST(test_validate_parity);
    RUN_TEST(test_validate_databits);
    RUN_TEST(test_validate_ip);
    RUN_TEST(test_validate_wifi_mode);
    RUN_TEST(test_validate_wifi_auth);
    RUN_TEST(test_validate_bool);
    RUN_TEST(test_validate_login);
    RUN_TEST(test_validate_password);

    return UNITY_END();
}
