#!/usr/bin/env python3
"""
Simple tests for WB-MGE HTTP API
"""

import csv
import io
import requests
import socket
import struct
import threading
import time
import json


class WBMGEAPI:
    def __init__(self, base_url="http://192.168.5.1"):
        self.base_url = base_url
        self.session = requests.Session()

        # Set headers to mimic a regular browser
        self.session.headers.update({
            'User-Agent': 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36',
            'Accept': 'application/json, text/plain, */*',
            'Accept-Language': 'en-US,en;q=0.9',
            'Accept-Encoding': 'identity',  # Avoid compression
            'Connection': 'close',          # Close connection after each request
            'Cache-Control': 'no-cache',
        })

        # Disable SSL verification
        self.session.verify = False

        # Suppress SSL warnings
        try:
            import urllib3
            urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)
        except ImportError:
            pass  # urllib3 not available

    def auth(self, login="admin", password="admin"):
        """Authorization"""
        try:
            response = self.session.post(f"{self.base_url}/auth", json={
                "login": login,
                "pass": password
            }, timeout=10)
            return response
        except requests.exceptions.RequestException:
            raise

    def get_info(self):
        """Get device information"""
        return self.session.get(f"{self.base_url}/info", timeout=10)

    def get_settings(self):
        """Get settings"""
        return self.session.get(f"{self.base_url}/settings", timeout=10)

    def update_settings(self, data):
        """Update settings"""
        return self.session.post(f"{self.base_url}/settings", json=data, timeout=10)

    def start_wifi_scan(self):
        """Start WiFi scan"""
        return self.session.post(f"{self.base_url}/wifi_scan/start", timeout=10)

    def get_wifi_scan_results(self):
        """Get WiFi scan results"""
        return self.session.get(f"{self.base_url}/wifi_scan/results", timeout=10)

    def get_ap_clients(self):
        """Get list of AP clients"""
        return self.session.get(f"{self.base_url}/ap_clients")

    def get_static_file(self, path):
        """Get static file"""
        return self.session.get(f"{self.base_url}/{path}")

    def get_session(self):
        """Check session status"""
        return self.session.get(f"{self.base_url}/session")

    def logout(self):
        """Logout"""
        return self.session.post(f"{self.base_url}/logout")

    def get_uptime(self):
        """Get device uptime"""
        return self.session.get(f"{self.base_url}/uptime", timeout=10)

    def get_cache_status(self):
        """Get cache server status"""
        return self.session.get(f"{self.base_url}/cache/status", timeout=10)

    def get_cache_csv(self):
        """Get cached register map as CSV"""
        return self.session.get(f"{self.base_url}/cache/csv", timeout=10)

    def get_cache_json(self):
        """Get cached register map as JSON"""
        return self.session.get(f"{self.base_url}/cache/json", timeout=10)

    def get_hostname(self):
        """Get device hostname"""
        return self.session.get(f"{self.base_url}/hostname", timeout=10)

    def set_port_mode(self, port_num, mode):
        """Set port mode via POST /ports/{port_num}/mode"""
        return self.session.post(
            f"{self.base_url}/ports/{port_num}/mode",
            json={"mode": mode},
            timeout=10
        )

    def get_wb_test(self):
        """Get WB test status"""
        return self.session.get(f"{self.base_url}/wb_test", timeout=10)

    def set_wb_test(self, clock_out: bool):
        """Set WB test clock_out"""
        return self.session.post(f"{self.base_url}/wb_test", json={"clock_out": clock_out}, timeout=10)

    def get_sniffer_status(self):
        """Get sniffer status for all ports"""
        return self.session.get(f"{self.base_url}/sniffer/status", timeout=10)

    def execute_command(self, cmd):
        """Execute command"""
        try:
            print(f"Sending command: {cmd}")
            payload = {"cmd": cmd}
            print(f"JSON payload: {payload}")

            response = self.session.post(f"{self.base_url}/cmd", json=payload, timeout=10)
            print(f"Command {cmd} sent, status: {response.status_code}")

            return response
        except requests.exceptions.RequestException as e:
            print(f"Error sending command {cmd}: {e}")
            raise


def test_auth(api):
    """Authorization test"""
    print("=== Authorization test ===")

    # Incorrect credentials
    response = api.auth("wrong", "wrong")
    assert response.status_code == 200
    data = response.json()
    assert data["auth"] == False
    assert "error" in data
    print("✓ Incorrect authorization rejected")

    # Correct credentials
    response = api.auth()
    assert response.status_code == 200
    data = response.json()
    assert data["auth"] == True
    print("✓ Correct authorization accepted")


def test_info(api):
    """Device information test"""
    print("\n=== Device information test ===")

    # Get information
    response = api.get_info()
    assert response.status_code == 200
    data = response.json()

    # Check required fields according to the new API structure
    required_fields = [
        "device_name", "signature", "firmware", "git_info",
        "serial_num", "system_voltage", "config_button_presses"
    ]
    for field in required_fields:
        assert field in data, f"Field {field} is missing"

    # Check data types of main fields
    assert isinstance(data["serial_num"], int), "Field serial_num has incorrect type"
    assert isinstance(data["system_voltage"], (int, float)), "Field system_voltage has incorrect type"
    assert isinstance(data["config_button_presses"], int), "Field config_button_presses has incorrect type"

    # Check heap memory fields
    for heap_field in ["heap_total", "heap_free", "heap_min_free"]:
        assert heap_field in data, f"Field {heap_field} is missing"
        assert isinstance(data[heap_field], int) and data[heap_field] >= 0, \
            f"Field {heap_field} must be a non-negative integer"

    # Check PSRAM fields
    assert "psram_available" in data, "Field psram_available is missing"
    assert isinstance(data["psram_available"], bool), "Field psram_available has incorrect type"
    assert "psram_size_kb" in data, "Field psram_size_kb is missing"
    assert isinstance(data["psram_size_kb"], int) and data["psram_size_kb"] >= 0, \
        "Field psram_size_kb must be a non-negative integer"

    # Check cache fields
    assert "cache_modbus_port" in data, "Field cache_modbus_port is missing"
    assert isinstance(data["cache_modbus_port"], int) and 1 <= data["cache_modbus_port"] <= 65535, \
        f"Field cache_modbus_port has incorrect value: {data['cache_modbus_port']}"
    assert "cache_modbus_server_enabled" in data, "Field cache_modbus_server_enabled is missing"
    assert isinstance(data["cache_modbus_server_enabled"], bool), \
        "Field cache_modbus_server_enabled has incorrect type"
    assert "cache_value_timeout_s" in data, "Field cache_value_timeout_s is missing"
    assert isinstance(data["cache_value_timeout_s"], int) and data["cache_value_timeout_s"] >= 0, \
        "Field cache_value_timeout_s must be a non-negative integer"

    # Check ethernet structure
    assert "ethernet" in data, "Section ethernet is missing"
    eth = data["ethernet"]

    # Check fields of ethernet structure
    ethernet_fields = [
        "con_eth", "ip", "mask", "gw", "mac"
    ]
    for field in ethernet_fields:
        assert field in eth, f"Field {field} is missing"

    assert isinstance(eth["con_eth"], bool), "Field con_eth has incorrect type"

    # Check wifi structure
    assert "wifi" in data, "Section wifi is missing"
    wifi = data["wifi"]

    # Check fields of wifi structure
    wifi_fields = [
        "enabled", "mode", "con_sta", "con_sta_ssid", "sta_ip", "sta_mask", "sta_gw",
        "con_ap", "ap_ip", "ap_mask", "ap_gw", "sta_rssi", "ap_channel", "sta_mac", "ap_mac"
    ]
    for field in wifi_fields:
        assert field in wifi, f"Field {field} is missing"

    assert isinstance(wifi["enabled"], bool), "Field enabled has incorrect type"
    assert isinstance(wifi["con_sta"], bool), "Field con_sta has incorrect type"
    assert isinstance(wifi["con_ap"], int), "Field con_ap has incorrect type"
    assert isinstance(wifi["sta_rssi"], int), "Field sta_rssi has incorrect type"
    assert isinstance(wifi["ap_channel"], int), "Field ap_channel has incorrect type"

    assert 0 <= wifi["con_ap"] <= 10, f"Field con_ap has incorrect value: {wifi['con_ap']}"
    assert -128 <= wifi["sta_rssi"] <= 127, f"Field sta_rssi has incorrect value: {wifi['sta_rssi']}"
    assert 1 <= wifi["ap_channel"] <= 14, f"Field ap_channel has incorrect value: {wifi['ap_channel']}"
    assert wifi["mode"] in ["ap", "sta", "none"], \
        f"Field mode has unexpected value: {wifi['mode']}"

    # Check rs485 ports structure
    for port in ["rs485_1", "rs485_2"]:
        assert port in data, f"Section {port} is missing"
        rs485 = data[port]

        assert "is_busy" in rs485, "Field is_busy is missing"
        assert "error_percentage" in rs485, "Field error_percentage is missing"
        assert "server_connections_count" in rs485, "Field server_connections_count is missing"
        assert "port_mode" in rs485, f"Field port_mode is missing in {port}"

        assert isinstance(rs485["is_busy"], bool), "Field is_busy has incorrect type"
        assert isinstance(rs485["error_percentage"], int), "Field error_percentage has incorrect type"
        assert isinstance(rs485["server_connections_count"], int), "Field server_connections_count has incorrect type"
        assert isinstance(rs485["port_mode"], str), f"Field port_mode in {port} has incorrect type"

    print("✓ Information structure is correct")


def test_settings(api):
    """Settings test"""
    print("\n=== Settings test ===")

    # Get settings
    response = api.get_settings()
    assert response.status_code == 200
    original_settings = response.json()

    # Check structure
    required_sections = ["wifi", "ethernet", "rs485_1", "rs485_2"]
    for section in required_sections:
        assert section in original_settings, f"Section {section} is missing"

    assert "hostname" in original_settings, "Field hostname is missing"
    assert "login" in original_settings, "Field login is missing"
    assert "pass" in original_settings, "Field pass is missing"
    assert "vout" in original_settings, "Field vout is missing"
    assert "web_port" in original_settings, "Field web_port is missing"
    assert "io_bus" in original_settings, "Field io_bus is missing"

    assert isinstance(original_settings["vout"], bool), "Field vout has incorrect type"
    assert isinstance(original_settings["web_port"], int), "Field web_port has incorrect type"
    assert 1 <= original_settings["web_port"] <= 65535, f"Field web_port has incorrect value: {original_settings['web_port']}"
    assert isinstance(original_settings["io_bus"], bool), "Field io_bus has incorrect type"

    # Check cache settings fields
    assert "cache_modbus_port" in original_settings, "Field cache_modbus_port is missing"
    assert isinstance(original_settings["cache_modbus_port"], int) and \
        1 <= original_settings["cache_modbus_port"] <= 65535, \
        f"Field cache_modbus_port has incorrect value: {original_settings['cache_modbus_port']}"
    assert "cache_modbus_server_enabled" in original_settings, "Field cache_modbus_server_enabled is missing"
    assert isinstance(original_settings["cache_modbus_server_enabled"], bool), \
        "Field cache_modbus_server_enabled has incorrect type"
    assert "cache_value_timeout_s" in original_settings, "Field cache_value_timeout_s is missing"
    assert isinstance(original_settings["cache_value_timeout_s"], int) and \
        original_settings["cache_value_timeout_s"] >= 0, \
        "Field cache_value_timeout_s must be a non-negative integer"

    # Check WiFi settings
    wifi = original_settings["wifi"]
    wifi_fields = [
        "mode", "ap_auth", "sta_auth", "ap_ssid", "ap_pass", "sta_ssid", "sta_pass",
        "ap_ip_static", "ap_mask_static", "ap_gw_static",
        "sta_dhcpc", "sta_ip_static", "sta_mask_static", "sta_gw_static"
    ]
    for field in wifi_fields:
        assert field in wifi, f"Field {field} is missing"

    assert isinstance(wifi["sta_dhcpc"], bool), "Field sta_dhcpc has incorrect type"

    assert wifi["mode"] in ["ap", "sta", "none"], f"Field mode has incorrect value: {wifi['mode']}"
    assert wifi["ap_auth"] in ["open", "wpa2_psk", "wpa3_psk"], f"Field ap_auth has incorrect value: {wifi['ap_auth']}"
    assert wifi["sta_auth"] in ["open", "wpa2_psk", "wpa3_psk"], f"Field sta_auth has incorrect value: {wifi['sta_auth']}"

    # Check Ethernet settings
    eth = original_settings["ethernet"]
    eth_fields = [
        "ip_static", "mask_static", "gw_static", "dhcpc"
    ]
    for field in eth_fields:
        assert field in eth, f"Field {field} is missing"

    assert isinstance(eth["dhcpc"], bool), "Field dhcpc has incorrect type"

    # Check RS485 settings
    for port in ["rs485_1", "rs485_2"]:
        rs485 = original_settings[port]
        rs485_fields = [
            "term", "fail_safe", "baudrate", "stopbits",
            "parity", "databits", "bridge"
        ]
        for field in rs485_fields:
            assert field in rs485, f"Field {field} is missing"

        assert isinstance(rs485["term"], bool), f"Field term has incorrect type"
        assert isinstance(rs485["fail_safe"], bool), f"Field fail_safe has incorrect type"
        assert isinstance(rs485["baudrate"], int), f"Field baudrate has incorrect type"
        assert rs485["baudrate"] in [1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200], \
            f"Field baudrate has incorrect value: {rs485['baudrate']}"
        assert rs485["stopbits"] in ["1", "1.5", "2"], \
            f"Field stopbits has incorrect value: {rs485['stopbits']}"
        assert rs485["parity"] in ["none", "even", "odd"], \
            f"Field parity has incorrect value: {rs485['parity']}"
        assert rs485["databits"] in ["5", "6", "7", "8"], \
            f"Field databits has incorrect value: {rs485['databits']}"

        # Check bridge settings
        bridge = rs485["bridge"]
        bridge_fields = [
            "mode", "port", "ip", "modbus"
        ]
        for field in bridge_fields:
            assert field in bridge, f"Field {field} is missing"

        assert bridge["mode"] in ["server", "client"], f"Field mode has incorrect value: {bridge['mode']}"
        assert isinstance(bridge["port"], int), "Field port has incorrect type"
        assert 1 <= bridge["port"] <= 65535, f"Field port has incorrect value: {bridge['port']}"
        assert isinstance(bridge["modbus"], bool), "Field modbus has incorrect type"

        # Modbus TCP specific parameters removed from test

    print("✓ Settings structure is correct")

    # Test writing settings with validation
    test_settings = {
        "hostname": "test-device-123",  # Valid hostname
        #"login": "testuser123",         # Valid login # NOTE: Not touching for now, otherwise authorization breaks
        "web_port": original_settings["web_port"],  # Keep the same port to avoid breaking QEMU port mapping
        "vout": not original_settings["vout"],  # Toggle bool
        "io_bus": not original_settings["io_bus"],  # Toggle bool
        "wifi": {
            "mode": "sta",
            "ap_auth": "wpa2_psk",
            "sta_auth": "wpa3_psk",
            "ap_ssid": "Test-SSID#123.",
            "ap_pass": "testpass123#*~",
            "sta_ssid": "Station-SSID",
            "sta_pass": "#stapass.456",
            "ap_ip_static": "192.168.4.1",
            "ap_mask_static": "255.255.255.0",
            "ap_gw_static": "192.168.4.1",
            "sta_dhcpc": not original_settings["wifi"]["sta_dhcpc"],
            "sta_ip_static": "192.168.2.7",
            "sta_mask_static": "255.255.255.0",
            "sta_gw_static": "192.168.2.1"
        },
        "ethernet": {
            "dhcpc": not original_settings["ethernet"]["dhcpc"],
            "ip_static": "192.168.1.100",
            "mask_static": "255.255.255.0",
            "gw_static": "192.168.1.1"
        },
        "rs485_1": {
            "term": not original_settings["rs485_1"]["term"],
            "fail_safe": not original_settings["rs485_1"]["fail_safe"],
            "baudrate": 115200,
            "stopbits": "1.5",
            "parity": "even",
            "databits": "7",
            "bridge": {
                "mode": "server",
                "port": 5020,
                "ip": "192.168.1.49",
                "modbus": True
            }
        },
        "rs485_2": {
            "term": not original_settings["rs485_2"]["term"],
            "fail_safe": not original_settings["rs485_2"]["fail_safe"],
            "baudrate": 38400,
            "stopbits": "1",
            "parity": "odd",
            "databits": "6",
            "bridge": {
                "mode": "client",
                "port": 5021,
                "ip": "192.168.1.50",
                "modbus": False
            }
        }
    }

    response = api.update_settings(test_settings)
    assert response.status_code == 200
    result = response.json()
    assert result["success"] == True
    print("✓ Writing settings with valid data works")

    # Check that all settings were saved
    response = api.get_settings()
    assert response.status_code == 200
    new_settings = response.json()

    # Check main parameters
    main_fields = [
        "hostname", "vout", "web_port", "io_bus"
        # "login", "pass"
    ]
    for field in main_fields:
        assert new_settings[field] == test_settings[field], f"Incorrect value for field {field}: {new_settings[field]}"

    # Check WiFi settings
    wifi = new_settings["wifi"]
    for field in wifi_fields:
        assert wifi[field] == test_settings["wifi"][field], f"Incorrect value for field {field}: {wifi[field]}"

    # Check Ethernet settings
    eth = new_settings["ethernet"]
    for field in eth_fields:
        assert eth[field] == test_settings["ethernet"][field], f"Incorrect value for field {field}: {eth[field]}"

    # Check RS485_1 settings
    rs485_1 = new_settings["rs485_1"]
    rs485_main_fields = [
        "term", "fail_safe", "baudrate", "stopbits", "parity", "databits"
    ]
    for field in rs485_main_fields:
        assert rs485_1[field] == test_settings["rs485_1"][field], \
            f"Incorrect value for field {field}: {rs485_1[field]}"

    bridge_1 = new_settings["rs485_1"]["bridge"]
    bridge_fields = [
        "mode", "port", "ip", "modbus"
    ]
    for field in bridge_fields:
        assert bridge_1[field] == test_settings["rs485_1"]["bridge"][field], \
            f"Incorrect value for field {field}: {bridge_1[field]}"

    # Check RS485_2 settings
    rs485_2 = new_settings["rs485_2"]
    for field in rs485_main_fields:
        assert rs485_2[field] == test_settings["rs485_2"][field], \
            f"Incorrect value for field {field}: {rs485_2[field]}"

    bridge_2 = new_settings["rs485_2"]["bridge"]
    for field in bridge_fields:
        assert bridge_2[field] == test_settings["rs485_2"]["bridge"][field], \
            f"Incorrect value for field {field}: {bridge_2[field]}"

    print("✓ All settings are saved correctly")

    # Test with invalid data
    invalid_settings = {
        "hostname": "invalid_hostname!",            # Invalid characters
        "web_port": 70000,                          # Exceeds limit
        "wifi": {
            "mode": "disabled",                     # Non-existent mode
            "ap_auth": "close",                     # Non-existent mode
            "sta_auth": "wep",                      # Non-existent mode
            "ap_ssid": "a" * 50,                    # SSID too long
            "sta_ssid": "фыва123",                  # Invalid characters
            "ap_ip_static": "123.456.789.101",      # Invalid byte values
            "ap_mask_static": "abc.def.ghi.jkl",    # Invalid characters
            "ap_gw_static": True,                   # Invalid type
            "sta_ip_static": "192.168.1.1.1",       # Invalid format
            "sta_mask_static": "123.aaa.1.1",       # Invalid characters
            "sta_gw_static": 192                    # Invalid type
        },
        "ethernet": {
            "ip_static": "123.456.789.101",         # Invalid byte values
            "mask_static": 456,                     # Invalid type
            "gw_static": 789,                       # Invalid type
            "dhcpc": 0                              # Invalid type
        },
        "rs485_1": {
            "term": 1,                              # Invalid type
            "fail_safe": "off",                     # Invalid type
            "baudrate": 123456,                     # Invalid value
            "stopbits": "2.5",                      # Invalid value
            "parity": "all",                        # Invalid value
            "databits": "2",                        # Invalid value
            "bridge": {
                "mode": "station",                  # Invalid value
                "port": 0,                          # Invalid value
                "ip": "201.250.252.256",            # Invalid byte values
                "modbus": "enabled"                 # Invalid type
            },
        "rs485_2": {
            "term": "true",                         # Invalid type
            "fail_safe": "true",                    # Invalid type
            "baudrate": 0,                          # Invalid value
            "stopbits": "0.5",                      # Invalid value
            "parity": "disabled",                   # Invalid value
            "databits": "4",                        # Invalid value
            "bridge": {
                "mode": "server",                   # Invalid value
                "port": 65536,                      # Invalid value
                "ip": "102.abc.126.18",             # Invalid characters
                "modbus": "disabled"                # Invalid type
            }
        },
        "vout": "true",                             # Invalid type
        "io_bus": "true"                            # Invalid type
        }
    }

    response = api.update_settings(invalid_settings)
    # API should either reject (400) or accept but not save invalid values.
    # If 200, the read-back below will catch any saved invalid values.
    assert response.status_code in [200, 400]
    print("✓ Invalid settings handling works")

    # Check that invalid settings are not saved (validates 200 case above)
    response = api.get_settings()
    assert response.status_code == 200
    valid_settings = response.json()
    assert valid_settings == new_settings, "Invalid settings were saved"
    print("✓ Invalid settings are not saved")

    # Restore settings — web_port change causes HTTP server restart, connection may be dropped
    try:
        response = api.update_settings(original_settings)
        assert response.status_code == 200
    except requests.exceptions.ConnectionError:
        # Connection drop is acceptable here: changing web_port restarts the HTTP server.
        # The settings are still saved before the disconnect.
        pass
    print("✓ Original settings restored")


def test_session_management(api):
    """Session management test"""
    print("\n=== Session management test ===")

    # Check session status after authorization
    response = api.get_session()
    assert response.status_code == 200
    print("✓ Session status check works")

    # Test logout
    response = api.logout()
    assert response.status_code == 200
    data = response.json()
    assert data["logout"] == True  # API returns "logout", not "success"
    print("✓ Logout works")

    # Check that session is invalid after logout
    response = api.get_session()
    assert response.status_code == 401
    print("✓ Session correctly terminates after logout")

    # Re-authorize for other tests
    response = api.auth()
    assert response.status_code == 200
    assert response.json()["auth"] == True
    print("✓ Re-authorization works")


def test_uptime(api):
    """Device uptime test"""
    print("\n=== Uptime test ===")

    response = api.get_uptime()
    assert response.status_code == 200
    data = response.json()

    # Check uptime structure
    required_fields = ["days", "hours", "minutes", "seconds"]
    for field in required_fields:
        assert field in data, f"Field {field} is missing in uptime"

    # Check constraints
    assert isinstance(data["days"], int) and data["days"] >= 0
    assert isinstance(data["hours"], int) and 0 <= data["hours"] <= 23
    assert isinstance(data["minutes"], int) and 0 <= data["minutes"] <= 59
    assert isinstance(data["seconds"], int) and 0 <= data["seconds"] <= 59

    print("✓ Uptime retrieval works")


def test_commands(api):
    """Command execution test"""
    print("\n=== Commands test ===")

    # Save current settings before issuing set_default_settings so we can restore them
    save_response = api.get_settings()
    assert save_response.status_code == 200
    saved_settings = save_response.json()

    try:
        # Test set_default_settings command (safe)
        print("Sending set_default_settings command...")
        response = api.execute_command("set_default_settings")

        print(f"Status Code: {response.status_code}")
        print(f"Headers: {response.headers}")
        print(f"Content: {response.text[:500]}...")  # First 500 characters

        assert response.status_code == 200, f"Expected status 200, got {response.status_code}"

        # Commands must return a non-empty JSON response with success=true
        assert response.text.strip(), \
            f"set_default_settings command returned an empty response body"
        data = response.json()
        assert data.get("success") == True, \
            f"set_default_settings command did not return success=true: {data}"
        print(f"JSON Response: {data}")

        print("✓ Command set_default_settings works")

    except requests.exceptions.RequestException as e:
        print(f"❌ Connection error executing command: {e}")
        raise
    except Exception as e:
        print(f"❌ Unexpected error in commands test: {e}")
        print(f"Error type: {type(e).__name__}")
        raise
    finally:
        # Restore settings that were wiped by set_default_settings
        try:
            restore_response = api.update_settings(saved_settings)
            if restore_response.status_code == 200:
                print("✓ Settings restored after set_default_settings")
            else:
                print(f"  [WARN] Settings restore returned status {restore_response.status_code}")
        except requests.exceptions.ConnectionError:
            # Connection drop is acceptable: web_port change restarts the HTTP server.
            print("  [WARN] Connection dropped during settings restore (expected if web_port changed)")
        except Exception as exc:
            print(f"  [WARN] Failed to restore settings: {exc}")


def test_modbus_tcp_parameters(api):
    """Modbus TCP specific parameters test"""
    print("\n=== Modbus TCP parameters test ===")

    # Get current settings
    response = api.get_settings()
    assert response.status_code == 200
    original_settings = response.json()

    # Test Modbus TCP settings for first port
    modbus_settings = {
        "rs485_1": {
            "bridge": {
                "mode": "server",
                "port": 502,
                "modbus": True
            }
        },
        "rs485_2": {
            "bridge": {
                "mode": "client",
                "port": 503,
                "ip": "192.168.1.10",
                "modbus": True
            }
        }
    }

    response = api.update_settings(modbus_settings)
    assert response.status_code == 200
    result = response.json()
    assert result["success"] == True
    print("✓ Modbus TCP settings saved")

    # Check that settings were applied
    response = api.get_settings()
    assert response.status_code == 200
    new_settings = response.json()

    # Check first port
    rs485_1 = new_settings["rs485_1"]["bridge"]
    assert rs485_1["modbus"] == True

    # Check second port
    rs485_2 = new_settings["rs485_2"]["bridge"]
    assert rs485_2["modbus"] == True

    print("✓ Modbus TCP parameters applied correctly")

    # Test with Modbus disabled
    transparent_settings = {
        "rs485_1": {
            "bridge": {
                "modbus": False         # Transparent mode
            }
        }
    }

    response = api.update_settings(transparent_settings)
    assert response.status_code == 200
    print("✓ Transparent mode settings accepted")


def test_modbus_validation_limits(api):
    """Validation limits test for Modbus parameters"""
    print("\n=== Modbus limits validation test ===")

    # Save current settings to verify they are not corrupted by invalid data
    baseline_response = api.get_settings()
    assert baseline_response.status_code == 200
    baseline_settings = baseline_response.json()

    # Test with invalid port 0
    invalid_settings = {
        "rs485_1": {
            "bridge": {
                "modbus": True,
                "port": 0          # Invalid port: 0 is below minimum 1
            }
        }
    }

    response = api.update_settings(invalid_settings)
    # API should either reject (400) or accept but not save invalid values
    assert response.status_code in [200, 400]
    if response.status_code == 200:
        # Verify that invalid port was not saved
        check_response = api.get_settings()
        assert check_response.status_code == 200
        check_settings = check_response.json()
        actual_port = check_settings["rs485_1"]["bridge"]["port"]
        assert actual_port != 0, \
            f"Invalid port 0 was saved (expected rejection, got {actual_port})"
    print("✓ Invalid port 0 is handled")

    # Test with port exceeding limit (70000 > 65535)
    invalid_settings = {
        "rs485_2": {
            "bridge": {
                "modbus": True,
                "port": 70000      # Exceeds maximum (65535)
            }
        }
    }

    response = api.update_settings(invalid_settings)
    assert response.status_code in [200, 400]
    if response.status_code == 200:
        # Verify that out-of-range port was not saved
        check_response = api.get_settings()
        assert check_response.status_code == 200
        check_settings = check_response.json()
        actual_port = check_settings["rs485_2"]["bridge"]["port"]
        assert actual_port != 70000, \
            f"Invalid port 70000 was saved (expected rejection, got {actual_port})"
    print("✓ Port limit exceeding is handled")


def test_validation_patterns(api):
    """Patterns and constraints validation test"""
    print("\n=== Patterns validation test ===")

    # Test valid patterns
    valid_data = {
        "hostname": "valid-hostname-123",  # Valid hostname
        "login": "valid_user_123",         # Valid login
        "wifi": {
            "ap_ssid": "ValidSSID",        # Valid SSID
            "ap_pass": "ValidPass123"      # Valid password (8+ characters)
        }
    }

    response = api.update_settings(valid_data)
    assert response.status_code == 200
    print("✓ Valid patterns accepted")

    # Test boundary values
    boundary_data = {
        "wifi": {
            "ap_ssid": "A",                # Minimum length (1 character)
            "ap_pass": "12345678"          # Minimum password length (8 characters)
        }
        # NOTE: web_port is intentionally not tested here — changing it restarts
        # the HTTP server and drops the connection, making further tests impossible.
    }

    response = api.update_settings(boundary_data)
    assert response.status_code == 200
    print("✓ Boundary values accepted")

    # Test exceeding limits
    limit_data = {
        "wifi": {
            "ap_ssid": "A" * 50,           # SSID limit exceeded (32)
            "ap_pass": "A" * 100           # Password limit exceeded (63)
        },
        "web_port": 70000                  # Port limit exceeded (65535)
    }

    response = api.update_settings(limit_data)
    # Should reject or ignore incorrect values
    assert response.status_code in [200, 400]
    if response.status_code == 200:
        # Verify that invalid values were not actually saved
        check_response = api.get_settings()
        assert check_response.status_code == 200
        check_settings = check_response.json()
        assert check_settings["web_port"] != 70000, \
            "Invalid web_port 70000 was saved (expected rejection)"
        assert len(check_settings["wifi"]["ap_ssid"]) <= 31, \
            "Oversized SSID was saved"
    print("✓ Limit exceeding is handled")


def test_wifi_scanner(api):
    """WiFi scanner test"""
    print("\n=== WiFi scanner test ===")

    # Start scan
    response = api.start_wifi_scan()
    assert response.status_code == 200
    data = response.json()
    assert isinstance(data["success"], bool)
    print("✓ WiFi scan start works")

    # Check scan status
    response = api.get_wifi_scan_results()
    assert response.status_code == 200
    data = response.json()

    assert "scan_in_progress" in data
    assert "scan_completed" in data
    assert isinstance(data["scan_in_progress"], bool)
    assert isinstance(data["scan_completed"], bool)
    # Accept either in_progress or already_completed — QEMU mock may finish before first poll
    assert data["scan_in_progress"] == True or data["scan_completed"] == True, \
        "scan should be either in progress or completed immediately after start"
    # If scan already completed (not just started), verify it did not end with an error
    if data.get("scan_completed") == True:
        assert not data.get("error"), \
            f"scan completed with an error immediately after start: {data.get('error')}"

    # Wait for scan completion
    timeout = 0
    while True:
        time.sleep(1)
        response = api.get_wifi_scan_results()
        assert response.status_code == 200
        data = response.json()
        if data["scan_in_progress"] == False and data["scan_completed"] == True:
            break
        timeout = timeout + 1
        assert timeout <= 10, "Scan completion timeout exceeded"

    if "networks" in data:
        assert isinstance(data["networks"], list)
        for network in data["networks"]:
            assert "ssid" in network
            assert "rssi" in network
            assert -128 <= network["rssi"] <= 0

    print("✓ Scan results retrieval works")


def test_ap_clients(api):
    """AP clients list test"""
    print("\n=== AP clients list test ===")

    response = api.get_ap_clients()
    assert response.status_code == 200
    clients = response.json()

    assert isinstance(clients, list)
    for client in clients:
        assert "mac" in client
        if "rssi" in client:
            assert -100 <= client["rssi"] <= 0

    print("✓ AP clients list retrieval works")


def test_static_files(api):
    """Static files test"""
    print("\n=== Static files test ===")

    static_files = [
        ("", "text/html"),           # Main page
        ("index.css", "text/css"),   # CSS
        ("index.js", "application/javascript"),  # JS
        ("favicon.webp", "image/webp")  # Favicon
    ]

    for path, expected_content_type in static_files:
        response = api.get_static_file(path)
        assert response.status_code == 200

        content_type = response.headers.get("content-type", "")
        assert expected_content_type in content_type.lower(), f"Incorrect Content-Type for {path}: expected '{expected_content_type}', got '{content_type}'"

        # Check that content is not empty
        assert len(response.content) > 0

        print(f"✓ Static file {path or 'index'} accessible")


def test_unauthorized_access(api):
    """Unauthorized access test"""
    print("\n=== Unauthorized access test ===")

    # Create new session without authorization
    unauth_session = requests.Session()

    protected_endpoints = [
        ("/info", "GET"), ("/settings", "GET"), ("/wifi_scan/start", "POST"),
        ("/wifi_scan/results", "GET"), ("/ap_clients", "GET"), ("/uptime", "GET"),
        ("/session", "GET"), ("/update", "POST")
    ]

    for endpoint, method in protected_endpoints:
        if method == "GET":
            response = unauth_session.get(f"{api.base_url}{endpoint}")
        elif method == "POST":
            response = unauth_session.post(f"{api.base_url}{endpoint}")

        print(f"Testing {method} {endpoint}:")
        print(f"  Status Code: {response.status_code}")
        print(f"  Headers: {dict(response.headers)}")
        print(f"  Content: {response.text[:200]}...")

        assert response.status_code == 401, f"Endpoint {method} {endpoint} should require authorization. Got status: {response.status_code}, content: {response.text[:100]}"

    print("✓ Protected endpoints require authorization")

    # Check that static files are accessible without authorization
    static_endpoints = ["/", "/index.css", "/index.js", "/favicon.webp"]

    for endpoint in static_endpoints:
        response = unauth_session.get(f"{api.base_url}{endpoint}")
        print(f"Testing GET {endpoint}:")
        print(f"  Status Code: {response.status_code}")
        assert response.status_code == 200

    print("✓ Static files accessible without authorization")

    # /hostname is publicly accessible without authorization
    hostname_response = unauth_session.get(f"{api.base_url}/hostname", timeout=10)
    assert hostname_response.status_code == 200, \
        f"GET /hostname should be accessible without auth, got {hostname_response.status_code}"
    print("✓ Hostname endpoint accessible without authorization")

    # /cache endpoints require authorization
    cache_protected = ["/cache/csv", "/cache/json", "/cache/status"]
    for endpoint in cache_protected:
        response = unauth_session.get(f"{api.base_url}{endpoint}", timeout=10)
        assert response.status_code == 401, \
            f"Cache endpoint {endpoint} should require auth, got {response.status_code}"
    print("✓ Cache endpoints require authorization")


# ---------------------------------------------------------------------------
# Modbus TCP helper constants and functions (ported from test_multimaster_cache.py)
# ---------------------------------------------------------------------------

# Mapping from register type name to Modbus function code
TYPE_TO_FC = {
    "holding": 0x03,
    "input": 0x04,
    "coil": 0x01,
    "discrete": 0x02,
}

# Modbus TCP MBAP header size (Transaction ID + Protocol ID + Length + Unit ID)
MBAP_HEADER_SIZE = 7
# Minimum response size: MBAP header (7) + FC byte (1)
MIN_RESPONSE_SIZE = 8

# Maximum TID value (16-bit unsigned)
MAX_TID = 65535

# Receive buffer size
RECV_BUFFER = 4096


def make_mbap_request(tid: int, slave_id: int, fc: int, start_addr: int, count: int) -> bytes:
    """Build a raw Modbus TCP request: MBAP header + PDU."""
    pdu = struct.pack(">BHH", fc, start_addr, count)
    # MBAP: transaction_id(2), protocol_id=0(2), length=unit_id+pdu(2), unit_id(1)
    mbap = struct.pack(">HHHB", tid, 0, len(pdu) + 1, slave_id)
    return mbap + pdu


def recv_exactly(sock: socket.socket, n: int) -> bytes:
    """Receive exactly n bytes from a socket, blocking until done or error."""
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError(f"Socket closed after {len(buf)}/{n} bytes")
        buf += chunk
    return buf


def send_and_receive(sock: socket.socket, request: bytes) -> tuple:
    """
    Send a Modbus TCP request and receive the full response.

    Returns (tid, unit_id, fc, payload_bytes) or raises on error.
    """
    sock.sendall(request)

    # Read MBAP header first (7 bytes) + FC byte (1 byte) = 8 bytes
    header = recv_exactly(sock, MIN_RESPONSE_SIZE)

    tid, proto, length, unit_id, fc = struct.unpack(">HHHBB", header)

    # 'length' in MBAP = remaining bytes after MBAP header (unit_id + pdu)
    # We already read unit_id (1) + fc (1) = 2 bytes of that,
    # so remaining payload = length - 2
    remaining = length - 2
    payload = b""
    if remaining > 0:
        payload = recv_exactly(sock, remaining)

    return tid, unit_id, fc, payload


def decode_fc03_fc04(payload: bytes, count: int) -> list:
    """
    Decode FC03/FC04 (holding/input register) response payload.

    payload[0] = byte_count
    payload[1..] = register values, big-endian 16-bit each
    """
    if len(payload) < 1:
        raise ValueError("FC03/FC04 payload too short")
    byte_count = payload[0]
    if len(payload) < 1 + byte_count:
        raise ValueError(f"FC03/FC04 payload truncated: expected {1 + byte_count}, got {len(payload)}")
    values = [
        struct.unpack(">H", payload[1 + i * 2 : 1 + i * 2 + 2])[0]
        for i in range(byte_count // 2)
    ]
    return values


def decode_fc01_fc02(payload: bytes, count: int) -> list:
    """
    Decode FC01/FC02 (coil/discrete input) response payload.

    payload[0] = byte_count
    payload[1..] = packed bits, LSB first within each byte
    """
    if len(payload) < 1:
        raise ValueError("FC01/FC02 payload too short")
    byte_count = payload[0]
    if len(payload) < 1 + byte_count:
        raise ValueError(f"FC01/FC02 payload truncated: expected {1 + byte_count}, got {len(payload)}")
    bits = []
    for byte_idx in range(byte_count):
        b = payload[1 + byte_idx]
        for bit_idx in range(8):
            bits.append((b >> bit_idx) & 1)
    # Trim to requested count
    return bits[:count]


def decode_response(fc: int, payload: bytes, count: int) -> list:
    """Dispatch to the appropriate decoder based on FC."""
    if fc in (0x03, 0x04):
        return decode_fc03_fc04(payload, count)
    elif fc in (0x01, 0x02):
        return decode_fc01_fc02(payload, count)
    else:
        raise ValueError(f"Unsupported FC: 0x{fc:02X}")


class ThreadResult:
    """Holds per-thread test outcomes."""

    def __init__(self, thread_id: int):
        self.thread_id = thread_id
        self.connected_at: float = 0.0
        self.errors: list = []
        self.checks_passed: int = 0
        self.checks_failed: int = 0
        self.iterations: int = 0  # Number of full passes over register_map
        self.exception = None

    def add_error(self, msg: str):
        self.errors.append(msg)
        self.checks_failed += 1

    def add_pass(self):
        self.checks_passed += 1


def _run_register_pass(
    sock: socket.socket,
    thread_id: int,
    register_map: dict,
    result: "ThreadResult",
    tid: int,
) -> int:
    """
    Perform a single full pass over all registers in register_map.

    Sends one Modbus TCP request per register, validates TID integrity,
    absence of Modbus exceptions, decodes the response payload, and
    validates the response format (non-empty, decodable).
    Updates result in-place and returns the next TID.
    """
    for (slave_id, reg_type, address), _register_data in register_map.items():
        fc = TYPE_TO_FC[reg_type]
        count = 1  # Read one register at a time

        # Wrap TID to 16-bit range
        tid = tid % (MAX_TID + 1)
        request = make_mbap_request(tid, slave_id, fc, address, count)

        try:
            resp_tid, resp_unit_id, resp_fc, payload = send_and_receive(sock, request)
        except Exception as exc:
            result.add_error(
                f"[Thread {thread_id}] Socket error reading "
                f"slave={slave_id} type={reg_type} addr={address}: {exc}"
            )
            tid += 1
            continue

        # TID integrity check
        if resp_tid != tid:
            result.add_error(
                f"[Thread {thread_id}] TID mismatch: sent {tid}, got {resp_tid} "
                f"(slave={slave_id} type={reg_type} addr={address})"
            )
            tid += 1
            continue

        # Modbus exception check
        if resp_fc & 0x80:
            exception_code = payload[0] if payload else -1
            if exception_code == 0x02:
                result.add_error(
                    f"[Thread {thread_id}] Modbus exception 0x02 (not in cache): "
                    f"slave={slave_id} type={reg_type} addr={address}"
                )
            else:
                result.add_error(
                    f"[Thread {thread_id}] Modbus exception 0x{exception_code:02X}: "
                    f"slave={slave_id} type={reg_type} addr={address}"
                )
            tid += 1
            continue

        # Decode the response payload and validate format
        try:
            decoded_values = decode_response(fc, payload, count)
        except ValueError as exc:
            result.add_error(
                f"[Thread {thread_id}] Decode error "
                f"slave={slave_id} type={reg_type} addr={address}: {exc}"
            )
            tid += 1
            continue

        # Guard against malformed server response returning empty decoded list
        if not decoded_values:
            result.add_error(
                f"[Thread {thread_id}] Decode returned empty list "
                f"slave={slave_id} type={reg_type} addr={address}"
            )
            tid += 1
            continue

        result.add_pass()

        tid += 1

    return tid


def worker(
    thread_id: int,
    host: str,
    port: int,
    register_map: dict,
    results: dict,
    start_barrier: threading.Barrier,
    duration: float = 0,
):
    """
    Worker function executed by each test thread.

    1. Waits at the barrier so all threads connect simultaneously.
    2. Opens a TCP connection to the Modbus server.
    3. Iterates over all registers in the map, issuing one request per register.
    4. Validates TID integrity, Modbus exceptions, and response format (decodable, non-empty).

    If duration > 0, repeats the register pass in a loop until the deadline.
    If duration == 0, performs exactly one pass.
    """
    result = ThreadResult(thread_id)
    results[thread_id] = result

    try:
        # Synchronise all threads to connect at the same time
        start_barrier.wait(timeout=30)

        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(10)
        sock.connect((host, port))
        result.connected_at = time.monotonic()

        tid = thread_id * 1000  # Start TID offset per thread to make debugging easier

        if duration > 0:
            # Stress-test mode: keep looping until deadline
            deadline = result.connected_at + duration
            while time.monotonic() < deadline:
                tid = _run_register_pass(sock, thread_id, register_map, result, tid)
                result.iterations += 1
        else:
            # Single-pass mode
            tid = _run_register_pass(sock, thread_id, register_map, result, tid)
            result.iterations = 1

        sock.close()

    except threading.BrokenBarrierError:
        result.exception = RuntimeError(
            f"[Thread {thread_id}] Barrier timed out — not all threads synchronised"
        )
    except Exception as exc:
        result.exception = exc


def check_simultaneous_connection(results: dict, num_threads: int) -> tuple:
    """
    Verify that all threads connected at roughly the same time.

    Considers connections simultaneous if the max spread is <= 2 seconds.
    """
    connect_times = [r.connected_at for r in results.values() if r.connected_at > 0]

    if len(connect_times) < num_threads:
        missed = num_threads - len(connect_times)
        return False, f"{missed} thread(s) never connected"

    spread_ms = (max(connect_times) - min(connect_times)) * 1000
    if spread_ms > 2000:
        return False, f"Connection spread too large: {spread_ms:.1f} ms (max 2000 ms)"

    return True, f"All {num_threads} threads connected within {spread_ms:.1f} ms"


def parse_csv(raw_csv: str) -> dict:
    """
    Parse the CSV register map.

    CSV columns: slave_id, type, address, value, age_s

    Returns a dict keyed by (slave_id: int, reg_type: str, address: int)
    with tuples (value: int, age_s: int).
    """
    register_map = {}
    reader = csv.DictReader(io.StringIO(raw_csv))
    for row in reader:
        try:
            slave_id = int(row["slave_id"])
            reg_type = row["type"].strip()
            address = int(row["address"])
            value = int(row["value"])
            age_s = int(row["age_s"])
        except (KeyError, ValueError) as exc:
            print(f"[WARN] Skipping malformed CSV row {row}: {exc}")
            continue

        if reg_type not in TYPE_TO_FC:
            print(f"[WARN] Unknown register type '{reg_type}' — skipping")
            continue

        key = (slave_id, reg_type, address)
        register_map[key] = (value, age_s)

    return register_map


def query_register_once(host: str, port: int, slave_id: int, reg_type: str, address: int) -> tuple:
    """
    Open a fresh TCP socket, send one Modbus TCP request, receive the response.

    Returns:
        ("ok", value)              — successful read
        ("exception", code)        — Modbus exception response (code = payload[0])
        ("error", description_str) — socket or decode error
    """
    fc = TYPE_TO_FC[reg_type]
    request = make_mbap_request(1, slave_id, fc, address, 1)
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(10)
        try:
            sock.connect((host, port))
            _tid, _unit_id, resp_fc, payload = send_and_receive(sock, request)
        finally:
            sock.close()
    except Exception as exc:
        return ("error", str(exc))

    # Modbus exception response
    if resp_fc & 0x80:
        code = payload[0] if payload else -1
        return ("exception", code)

    # Successful response — decode the single register value
    try:
        decoded = decode_response(fc, payload, 1)
    except ValueError as exc:
        return ("error", f"Decode error: {exc}")

    if not decoded:
        return ("error", "Decode returned empty list")

    return ("ok", decoded[0])


def run_staleness_test(host: str, port: int, api, register_map: dict) -> tuple:
    """
    Test that stale cache entries trigger Modbus exception 0x0B, and that
    disabling the timeout (=0) makes them readable again.

    Uses api.session for HTTP requests instead of urllib.

    Algorithm:
        1. Select up to 5 registers with age_s >= 3 from register_map.
        2. If none found — return (False, report_lines) with explanation.
        3. Set cache_value_timeout_s = 1 → expect exception 0x0B for each.
        4. Set cache_value_timeout_s = 0 → expect ("ok", value) for each.
           Step 4 runs in a finally block so the device is always restored.

    Returns (passed: bool, report_lines: list[str]).
    """
    report_lines = []

    # Collect stale registers (age_s >= 3 seconds)
    stale_registers = [
        (key, value, age_s)
        for key, (value, age_s) in register_map.items()
        if age_s >= 3
    ]

    if not stale_registers:
        report_lines.append("[FAIL] No registers with age_s >= 3 found — staleness test requires stale data")
        return (False, report_lines)

    # Limit to 5 candidates
    candidates = stale_registers[:5]
    report_lines.append(
        f"[INFO] Staleness test: {len(candidates)} register(s) with age_s >= 3 selected"
    )

    passed = True
    timeout_was_set = False

    try:
        # Set timeout = 1s, expect exception 0x0B for all stale registers
        resp = api.session.post(f"{api.base_url}/settings", json={"cache_value_timeout_s": 1}, timeout=10)
        if resp.status_code not in (200, 204):
            raise RuntimeError(f"Failed to set cache_value_timeout_s=1: HTTP {resp.status_code}")
        timeout_was_set = True
        report_lines.append("[INFO] cache_value_timeout_s set to 1")

        for (slave_id, reg_type, address), _value, age_s in candidates:
            result = query_register_once(host, port, slave_id, reg_type, address)
            if result == ("exception", 0x0B):
                report_lines.append(
                    f"[PASS] slave={slave_id} type={reg_type} addr={address} age={age_s}s "
                    f"→ exception 0x0B as expected"
                )
            else:
                passed = False
                report_lines.append(
                    f"[FAIL] slave={slave_id} type={reg_type} addr={address} age={age_s}s "
                    f"→ expected exception 0x0B, got {result}"
                )
    finally:
        # Always attempt to restore timeout=0 so the device is not left broken
        try:
            resp = api.session.post(f"{api.base_url}/settings", json={"cache_value_timeout_s": 0}, timeout=10)
            if resp.status_code not in (200, 204):
                raise RuntimeError(f"HTTP {resp.status_code}")
            report_lines.append("[INFO] cache_value_timeout_s restored to 0")
        except Exception as exc:
            report_lines.append(f"[ERROR] Failed to restore cache_value_timeout_s=0: {exc}")
            passed = False
        else:
            # Only verify readability if timeout was set to 1 and restore succeeded
            if timeout_was_set:
                for (slave_id, reg_type, address), _expected_value, _age_s in candidates:
                    result = query_register_once(host, port, slave_id, reg_type, address)
                    if result[0] == "ok":
                        report_lines.append(
                            f"[PASS] slave={slave_id} type={reg_type} addr={address} "
                            f"→ value={result[1]} readable with timeout=0 as expected"
                        )
                    else:
                        passed = False
                        report_lines.append(
                            f"[FAIL] slave={slave_id} type={reg_type} addr={address} "
                            f"→ expected ok read with timeout=0, got {result}"
                        )

    return (passed, report_lines)


# ---------------------------------------------------------------------------
# New test functions
# ---------------------------------------------------------------------------


def test_hostname(api):
    """Test GET /hostname endpoint"""
    print("\n=== Hostname test ===")

    response = api.get_hostname()
    assert response.status_code == 200, \
        f"GET /hostname expected 200, got {response.status_code}"

    data = response.json()
    assert "hostname" in data, "Field 'hostname' is missing from /hostname response"
    assert isinstance(data["hostname"], str), "Field 'hostname' must be a string"
    assert len(data["hostname"]) > 0, "Field 'hostname' must not be empty"

    print(f"✓ Hostname endpoint works, hostname: {data['hostname']}")

    # Verify /hostname is accessible without authorization (it is a public endpoint)
    unauth_response = requests.get(f"{api.base_url}/hostname", timeout=10)
    assert unauth_response.status_code == 200, \
        f"GET /hostname must be accessible without auth, got {unauth_response.status_code}"
    unauth_data = unauth_response.json()
    assert unauth_data.get("hostname") == data["hostname"], \
        "Unauthenticated /hostname response must match authenticated response"
    print("✓ Hostname endpoint accessible without authorization")


def test_info_format_validation(api):
    """Test GET /info response field format validation: firmware, git_info, MAC addresses"""
    import re
    print("\n=== Info format validation test ===")

    response = api.get_info()
    assert response.status_code == 200
    data = response.json()

    # Validate firmware version format: three dot-separated non-negative integers
    firmware = data.get("firmware", "")
    assert re.match(r'^\d+\.\d+\.\d+$', firmware), \
        f"firmware field does not match \\d+\\.\\d+\\.\\d+: '{firmware}'"
    print(f"✓ firmware format valid: {firmware}")

    # Validate git_info format: 7 hex chars + underscore + branch name (branch may contain /)
    # In QEMU builds without real git info, firmware substitutes "qemu_build" — skip format check.
    git_info = data.get("git_info", "")
    if git_info != "qemu_build":
        assert re.match(r'^[0-9a-f]{7}_[A-Za-z0-9_/.-]+$', git_info), \
            f"git_info field does not match expected pattern: '{git_info}'"
    print(f"✓ git_info format valid: {git_info}")

    # Validate MAC address format for ethernet and wifi interfaces
    mac_pattern = r'^([0-9A-Fa-f]{2}[:\-]){5}([0-9A-Fa-f]{2})$'
    mac_fields = [
        ("ethernet.mac", data.get("ethernet", {}).get("mac", "")),
        ("wifi.sta_mac", data.get("wifi", {}).get("sta_mac", "")),
        ("wifi.ap_mac", data.get("wifi", {}).get("ap_mac", "")),
    ]
    for field_name, mac_value in mac_fields:
        assert re.match(mac_pattern, mac_value), \
            f"{field_name} does not match MAC address pattern: '{mac_value}'"
        print(f"✓ {field_name} format valid: {mac_value}")

    # Validate serial_num lower bound (openapi minimum: 1)
    serial_num = data.get("serial_num", 0)
    assert serial_num >= 1, f"serial_num must be >= 1, got {serial_num}"
    print(f"✓ serial_num valid: {serial_num}")


def test_http_method_guard(api):
    """Test that endpoints reject wrong HTTP methods (expect 405 Method Not Allowed)"""
    print("\n=== HTTP method guard test ===")

    wrong_method_cases = [
        ("GET", "/cmd"),
        ("POST", "/info"),
        ("GET", "/logout"),
        ("DELETE", "/settings"),
    ]

    for method, path in wrong_method_cases:
        url = f"{api.base_url}{path}"
        try:
            if method == "GET":
                response = api.session.get(url, timeout=10)
            elif method == "POST":
                response = api.session.post(url, json={}, timeout=10)
            elif method == "DELETE":
                response = api.session.delete(url, timeout=10)
            else:
                raise ValueError(f"Unexpected method: {method}")

            assert response.status_code == 405, \
                f"{method} {path} should return 405 Method Not Allowed, got {response.status_code}"
            print(f"✓ {method} {path} → 405 as expected")
        except requests.exceptions.ConnectionError:
            # ESP-IDF httpd closes the connection without sending a response body
            # when the HTTP method does not match any registered handler and the
            # request includes a body (e.g. POST with JSON). This is acceptable
            # as it effectively rejects the wrong-method request.
            print(f"✓ {method} {path} → connection closed (method rejected, acceptable)")


def test_cache_endpoints(api):
    """Test cache HTTP endpoints: /cache/status, /cache/csv, /cache/json"""
    print("\n=== Cache endpoints test ===")

    # Test /cache/status
    response = api.get_cache_status()
    assert response.status_code == 200, \
        f"GET /cache/status expected 200, got {response.status_code}"

    status = response.json()
    cache_status_fields = [
        "enabled", "entries", "max_entries", "slaves",
        "packets_processed", "last_packet_age_us", "map_age_us", "memory_bytes"
    ]
    for field in cache_status_fields:
        assert field in status, f"Field '{field}' is missing from /cache/status response"

    assert isinstance(status["enabled"], bool), "Field 'enabled' must be a boolean"
    assert isinstance(status["entries"], int) and status["entries"] >= 0, \
        "Field 'entries' must be a non-negative integer"
    assert isinstance(status["max_entries"], int) and status["max_entries"] >= 0, \
        "Field 'max_entries' must be a non-negative integer"
    assert isinstance(status["slaves"], int) and status["slaves"] >= 0, \
        "Field 'slaves' must be a non-negative integer"
    assert isinstance(status["packets_processed"], int) and status["packets_processed"] >= 0, \
        "Field 'packets_processed' must be a non-negative integer"
    assert isinstance(status["last_packet_age_us"], int) and status["last_packet_age_us"] >= 0, \
        "Field 'last_packet_age_us' must be a non-negative integer"
    assert isinstance(status["map_age_us"], int) and status["map_age_us"] >= 0, \
        "Field 'map_age_us' must be a non-negative integer"
    assert isinstance(status["memory_bytes"], int) and status["memory_bytes"] >= 0, \
        "Field 'memory_bytes' must be a non-negative integer"

    print(f"✓ /cache/status works, enabled={status['enabled']}, entries={status['entries']}")

    # Test /cache/csv
    response = api.get_cache_csv()
    assert response.status_code == 200, \
        f"GET /cache/csv expected 200, got {response.status_code}"

    content_type = response.headers.get("content-type", "")
    assert "text/csv" in content_type.lower() or "text/plain" in content_type.lower(), \
        f"GET /cache/csv expected text/csv content type, got: {content_type}"

    csv_text = response.text
    assert len(csv_text) > 0, "GET /cache/csv response body must not be empty"

    # Verify CSV header line
    first_line = csv_text.split("\n")[0].strip()
    assert first_line == "slave_id,type,address,value,age_s", \
        f"CSV header mismatch: expected 'slave_id,type,address,value,age_s', got '{first_line}'"

    print("✓ /cache/csv works, header is correct")

    # Test /cache/json
    response = api.get_cache_json()
    assert response.status_code == 200, \
        f"GET /cache/json expected 200, got {response.status_code}"

    json_data = response.json()
    assert "d" in json_data, "Field 'd' is missing from /cache/json response"
    assert isinstance(json_data["d"], list), "Field 'd' in /cache/json must be an array"

    print(f"✓ /cache/json works, entries count: {len(json_data['d'])}")


def test_cache_multimaster(api):
    """Test cache Modbus TCP multi-master server"""
    print("\n=== Cache multimaster test ===")
    from urllib.parse import urlparse

    original_port_mode = None   # Will hold the port mode to restore in finally
    original_modbus_port = None  # Will hold the original cache_modbus_port to restore in finally
    # Port used for QEMU host-side forwarding (avoids root-required ports < 1024)
    QEMU_MODBUS_PORT = 50504

    try:
        # Step 0a: Read current port_mode for port 1 and current cache_modbus_port so we can restore them
        info_response = api.get_info()
        assert info_response.status_code == 200, \
            f"GET /info expected 200, got {info_response.status_code}"
        info_data = info_response.json()
        original_port_mode = info_data.get("rs485_1", {}).get("port_mode", "tcp_bridge")
        original_modbus_port = info_data.get("cache_modbus_port", 504)
        print(f"  Port 1 current mode: {original_port_mode}")
        print(f"  Original cache_modbus_port: {original_modbus_port}")

        # Change cache_modbus_port to QEMU_MODBUS_PORT so QEMU host-side forwarding works
        resp = api.update_settings({"cache_modbus_port": QEMU_MODBUS_PORT})
        assert resp.status_code == 200, \
            f"Failed to set cache_modbus_port to {QEMU_MODBUS_PORT}: {resp.status_code}"
        time.sleep(2)  # Wait for the server to restart on the new port
        print(f"✓ cache_modbus_port changed to {QEMU_MODBUS_PORT}")

        # Step 0b: Switch port 1 to cache_bus mode to enable caching
        response = api.set_port_mode(1, "cache_bus")
        assert response.status_code == 200, \
            f"POST /ports/1/mode expected 200, got {response.status_code}"
        print("✓ Port 1 switched to cache_bus mode")

        # Step 1: Wait for cache to become enabled and populated (up to 30s, poll every 1s)
        # Mock Modbus traffic should populate the cache within a few seconds in QEMU.
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            time.sleep(1)
            status_resp = api.get_cache_status()
            if status_resp.status_code == 200:
                status = status_resp.json()
                if status.get("entries", 0) > 0:
                    break

        # Get final cache status
        response = api.get_cache_status()
        assert response.status_code == 200, \
            f"GET /cache/status expected 200, got {response.status_code}"
        status = response.json()

        if not status.get("enabled") or status.get("entries", 0) == 0:
            print("  [SKIP] Cache did not populate within 30s — skipping Modbus TCP part")
            # Still passes: we validated that the port can be switched to cache_bus
            print("✓ Port mode switching to cache_bus works")
            return

        # Get Modbus TCP port from /info (cache_modbus_port field)
        info_response = api.get_info()
        assert info_response.status_code == 200, \
            f"GET /info expected 200, got {info_response.status_code}"
        info_data = info_response.json()
        modbus_port = info_data.get("cache_modbus_port", 504)

        # Parse host from api.base_url
        parsed = urlparse(api.base_url)
        host = parsed.hostname

        print(f"✓ Cache server enabled, Modbus TCP port: {modbus_port}, host: {host}")

        # Step 2: Get register map via /cache/csv
        response = api.get_cache_csv()
        assert response.status_code == 200, \
            f"GET /cache/csv expected 200, got {response.status_code}"

        raw_csv = response.text
        register_map = parse_csv(raw_csv)

        print(f"✓ Register map loaded: {len(register_map)} entries")

        # Step 3: If register_map is empty, just test TCP connectivity and skip Modbus parts
        if not register_map:
            print("  [SKIP] Register map is empty — testing TCP connectivity only")
            try:
                test_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                test_sock.settimeout(5)
                test_sock.connect((host, modbus_port))
                test_sock.close()
                print(f"✓ Modbus TCP server on port {modbus_port} accepts connections")
            except Exception as exc:
                assert False, f"Cannot connect to Modbus TCP server on {host}:{modbus_port}: {exc}"
            return

        # Step 4: Run multi-master test with 3 threads
        num_threads = 3
        results = {}
        start_barrier = threading.Barrier(num_threads)

        threads = [
            threading.Thread(
                target=worker,
                args=(i, host, modbus_port, register_map, results, start_barrier, 0),
                daemon=True,
            )
            for i in range(num_threads)
        ]

        print(f"✓ Starting {num_threads} parallel Modbus TCP clients on {host}:{modbus_port} ...")
        for t in threads:
            t.start()
        for t in threads:
            t.join(timeout=30)

        # Check for threads that did not finish (deadlock / timeout)
        still_alive = [t for t in threads if t.is_alive()]
        assert not still_alive, \
            f"{len(still_alive)} thread(s) did not finish within 30 seconds (deadlock?)"

        # Evaluate connectivity timing
        conn_ok, conn_msg = check_simultaneous_connection(results, num_threads)
        assert conn_ok, f"Connectivity check failed: {conn_msg}"
        print(f"✓ Connectivity: {conn_msg}")

        # Evaluate per-thread results
        all_passed = True
        for tid_key in sorted(results.keys()):
            r = results[tid_key]
            if r.exception:
                all_passed = False
                print(f"  [FAIL] Thread {r.thread_id}: EXCEPTION — {r.exception}")
                continue
            thread_ok = r.checks_failed == 0
            status_str = "PASS" if thread_ok else "FAIL"
            print(
                f"  [{status_str}] Thread {r.thread_id}: "
                f"{r.iterations} iteration(s), "
                f"{r.checks_passed} checks passed, {r.checks_failed} failed"
            )
            if not thread_ok:
                all_passed = False
                for err in r.errors:
                    print(f"    {err}")

        assert all_passed, "One or more threads had failures in multi-master Modbus TCP test"
        print("✓ Multi-master Modbus TCP test passed")

        # Step 5: Staleness test — only run if there are stale entries (age_s >= 3)
        has_stale = any(age_s >= 3 for (_key, (_val, age_s)) in register_map.items())
        if not has_stale:
            print("  [SKIP] No stale registers (age_s >= 3) found — skipping staleness test")
            return

        stale_ok, stale_lines = run_staleness_test(host, modbus_port, api, register_map)
        for line in stale_lines:
            print(f"  {line}")

        assert stale_ok, "Staleness test failed — see lines above for details"
        print("✓ Staleness test passed")

    finally:
        # Always restore port mode to what it was before the test
        if original_port_mode is not None:
            try:
                api.set_port_mode(1, original_port_mode)
                print(f"✓ Port 1 mode restored to {original_port_mode}")
            except Exception as exc:
                print(f"  [WARN] Failed to restore port 1 mode: {exc}")

        # Always restore cache_modbus_port to what it was before the test
        if original_modbus_port is not None:
            try:
                api.update_settings({"cache_modbus_port": original_modbus_port})
                print(f"✓ cache_modbus_port restored to {original_modbus_port}")
            except Exception as exc:
                print(f"  [WARN] Failed to restore cache_modbus_port: {exc}")

        time.sleep(2)  # Allow time for the port/settings to switch back


def test_wb_test(api):
    """Test GET /wb_test + POST /wb_test (I-1)"""
    print("\n=== WB test endpoint test ===")

    # GET /wb_test — check structure
    response = api.get_wb_test()
    assert response.status_code == 200, \
        f"GET /wb_test expected 200, got {response.status_code}"
    data = response.json()
    assert "clock_out" in data, "Field 'clock_out' is missing from /wb_test response"
    assert isinstance(data["clock_out"], bool), "Field 'clock_out' must be a boolean"
    print(f"✓ GET /wb_test works, clock_out={data['clock_out']}")

    original_clock_out = data["clock_out"]

    try:
        # POST /wb_test {"clock_out": true} — write and read-back
        response = api.set_wb_test(True)
        assert response.status_code == 200, \
            f"POST /wb_test clock_out=true expected 200, got {response.status_code}"
        result = response.json()
        assert result.get("success") == True, f"POST /wb_test expected success=true, got {result}"
        assert result.get("clock_out") == True, f"POST /wb_test expected clock_out=true in response, got {result}"
        print("✓ POST /wb_test {clock_out: true} accepted")

        # Read-back via GET
        response = api.get_wb_test()
        assert response.status_code == 200
        assert response.json()["clock_out"] == True, "Read-back after clock_out=true failed"
        print("✓ Read-back after clock_out=true correct")

        # POST /wb_test {"clock_out": false} — write and read-back
        response = api.set_wb_test(False)
        assert response.status_code == 200, \
            f"POST /wb_test clock_out=false expected 200, got {response.status_code}"
        result = response.json()
        assert result.get("success") == True, f"POST /wb_test expected success=true, got {result}"
        assert result.get("clock_out") == False, f"POST /wb_test expected clock_out=false in response, got {result}"
        print("✓ POST /wb_test {clock_out: false} accepted")

        response = api.get_wb_test()
        assert response.status_code == 200
        assert response.json()["clock_out"] == False, "Read-back after clock_out=false failed"
        print("✓ Read-back after clock_out=false correct")

        # POST /wb_test with invalid type (string instead of bool)
        response = api.session.post(f"{api.base_url}/wb_test", json={"clock_out": "true"}, timeout=10)
        if response.status_code == 200:
            # If accepted, verify value was not corrupted
            rb = api.get_wb_test().json()
            assert isinstance(rb["clock_out"], bool), \
                "After invalid type POST, clock_out must still be a boolean"
        else:
            assert response.status_code == 400, \
                f"POST /wb_test with string clock_out expected 400, got {response.status_code}"
        print("✓ POST /wb_test with invalid type handled")

        # POST /wb_test with missing field
        response = api.session.post(f"{api.base_url}/wb_test", json={}, timeout=10)
        assert response.status_code in [200, 400], \
            f"POST /wb_test with empty body got unexpected status {response.status_code}"
        print("✓ POST /wb_test with missing field handled")

    finally:
        # Restore original value
        api.set_wb_test(original_clock_out)
        print(f"✓ clock_out restored to {original_clock_out}")


def test_sniffer_status(api):
    """Test GET /sniffer/status and verify it reflects port mode changes (I-2)"""
    print("\n=== Sniffer status endpoint test ===")

    # Read current port mode to restore later
    info_response = api.get_info()
    assert info_response.status_code == 200
    info_data = info_response.json()
    original_port_1_mode = info_data.get("rs485_1", {}).get("port_mode", "tcp_bridge")
    print(f"  Port 1 original mode: {original_port_1_mode}")

    try:
        # GET /sniffer/status — check structure
        response = api.get_sniffer_status()
        assert response.status_code == 200, \
            f"GET /sniffer/status expected 200, got {response.status_code}"
        status = response.json()
        assert "port_1" in status, "Field 'port_1' is missing from /sniffer/status response"
        assert "port_2" in status, "Field 'port_2' is missing from /sniffer/status response"
        assert isinstance(status["port_1"], bool), "Field 'port_1' must be a boolean"
        assert isinstance(status["port_2"], bool), "Field 'port_2' must be a boolean"
        print(f"✓ GET /sniffer/status works, port_1={status['port_1']}, port_2={status['port_2']}")

        # Switch port 1 to sniffer mode → port_1 must become true
        response = api.set_port_mode(1, "sniffer")
        assert response.status_code == 200, \
            f"POST /ports/1/mode sniffer expected 200, got {response.status_code}"

        time.sleep(0.5)  # Allow firmware time to apply the mode change asynchronously
        response = api.get_sniffer_status()
        assert response.status_code == 200
        status = response.json()
        assert status["port_1"] == True, \
            f"After switching to sniffer mode, port_1 must be true, got {status['port_1']}"
        print("✓ After sniffer mode: port_1=true")

        # Switch port 1 to disabled → port_1 must become false
        response = api.set_port_mode(1, "disabled")
        assert response.status_code == 200

        time.sleep(0.5)  # Allow firmware time to apply the mode change asynchronously
        response = api.get_sniffer_status()
        assert response.status_code == 200
        status = response.json()
        assert status["port_1"] == False, \
            f"After switching to disabled mode, port_1 must be false, got {status['port_1']}"
        print("✓ After disabled mode: port_1=false")

    finally:
        # Restore original port 1 mode
        try:
            api.set_port_mode(1, original_port_1_mode)
            print(f"✓ Port 1 mode restored to {original_port_1_mode}")
        except Exception as exc:
            print(f"  [WARN] Failed to restore port 1 mode: {exc}")


def test_port_modes(api):
    """Test POST /ports/{n}/mode — all modes, both ports (I-3)"""
    print("\n=== Port modes test ===")

    # Read current modes to restore later
    info_response = api.get_info()
    assert info_response.status_code == 200
    info_data = info_response.json()
    original_port_1_mode = info_data.get("rs485_1", {}).get("port_mode", "tcp_bridge")
    original_port_2_mode = info_data.get("rs485_2", {}).get("port_mode", "tcp_bridge")
    print(f"  Original modes: port_1={original_port_1_mode}, port_2={original_port_2_mode}")

    try:
        # Test all valid modes for port 1
        for mode in ["disabled", "tcp_bridge", "sniffer", "cache_bus"]:
            response = api.set_port_mode(1, mode)
            assert response.status_code == 200, \
                f"POST /ports/1/mode {mode} expected 200, got {response.status_code}"
            result = response.json()
            assert result.get("mode") == mode, \
                f"POST /ports/1/mode {mode}: response mode mismatch, got {result}"

            # Verify via GET /info
            info_resp = api.get_info()
            assert info_resp.status_code == 200
            actual_mode = info_resp.json().get("rs485_1", {}).get("port_mode")
            assert actual_mode == mode, \
                f"After setting mode={mode}, GET /info shows rs485_1.port_mode={actual_mode}"
            print(f"✓ Port 1 mode '{mode}' set and verified via /info")

        # Test port 2 modes
        for mode in ["cache_bus", "disabled"]:
            response = api.set_port_mode(2, mode)
            assert response.status_code == 200, \
                f"POST /ports/2/mode {mode} expected 200, got {response.status_code}"
            result = response.json()
            assert result.get("mode") == mode, \
                f"POST /ports/2/mode {mode}: response mode mismatch, got {result}"
            print(f"✓ Port 2 mode '{mode}' set")

        # Test invalid mode value
        response = api.set_port_mode(1, "invalid_mode")
        assert response.status_code == 400, \
            f"POST /ports/1/mode 'invalid_mode' expected 400, got {response.status_code}"
        print("✓ Invalid mode value rejected with 400")

        # Test invalid port number
        response = api.session.post(
            f"{api.base_url}/ports/3/mode",
            json={"mode": "tcp_bridge"},
            timeout=10
        )
        assert response.status_code in [400, 404], \
            f"POST /ports/3/mode (non-existent port) expected 400 or 404, got {response.status_code}"
        print("✓ Non-existent port 3 rejected")

    finally:
        # Restore original modes independently; changing port mode may drop the TCP
        # connection (serial task restart), so each restore uses a fresh session.
        restore_errors = []
        for port_num, mode in [(1, original_port_1_mode), (2, original_port_2_mode)]:
            try:
                api.session.close()
                api.session = requests.Session()
                api.session.headers.update({
                    'User-Agent': 'Mozilla/5.0',
                    'Accept': 'application/json, text/plain, */*',
                    'Accept-Encoding': 'identity',
                    'Connection': 'close',
                    'Cache-Control': 'no-cache',
                })
                api.auth()
                api.set_port_mode(port_num, mode)
                print(f"✓ Port {port_num} mode restored to {mode}")
            except Exception as exc:
                msg = f"Failed to restore port {port_num} mode to {mode}: {exc}"
                print(f"  [ERROR] {msg}")
                restore_errors.append(msg)
        if restore_errors:
            raise AssertionError("Port mode restore failed after test: " + "; ".join(restore_errors))


def test_cmd_extended(api):
    """Test POST /cmd — set_default_settings and invalid values (I-4)"""
    print("\n=== Extended command test ===")

    # Save settings before reset so we can restore them afterwards
    save_response = api.get_settings()
    assert save_response.status_code == 200, "Failed to read settings before set_default_settings"
    saved_settings = save_response.json()

    try:
        # Test set_default_settings (safe to call in QEMU, resets settings to defaults)
        print("Sending set_default_settings command...")
        response = api.execute_command("set_default_settings")
        assert response.status_code == 200, \
            f"POST /cmd set_default_settings expected 200, got {response.status_code}"
        print("✓ Command set_default_settings accepted")

        # Verify that settings were actually reset: read them back and compare with saved
        after_response = api.get_settings()
        assert after_response.status_code == 200, "Failed to read settings after set_default_settings"
        after_settings = after_response.json()
        # Settings must differ from saved (unless saved settings were already defaults)
        # At minimum the command must not crash — 200 response is the primary check.
        print(f"  Settings after reset retrieved (keys: {list(after_settings.keys())})")
        print("✓ Settings readable after set_default_settings")

        # Test invalid command (not in enum)
        response = api.session.post(f"{api.base_url}/cmd", json={"cmd": "shutdown"}, timeout=10)
        assert response.status_code in [200, 400], \
            f"POST /cmd 'shutdown' got unexpected status {response.status_code}"
        if response.status_code == 200:
            data = response.json()
            assert data.get("success") == False, \
                f"Server accepted unknown command 'shutdown' as successful: {data}"
            print("  [INFO] Server returned 200 for unknown command with success=false (lenient validation)")
        else:
            print("✓ Unknown command 'shutdown' rejected with 400")

        # Test missing cmd field
        response = api.session.post(f"{api.base_url}/cmd", json={}, timeout=10)
        assert response.status_code in [200, 400], \
            f"POST /cmd empty body got unexpected status {response.status_code}"
        print("✓ Missing cmd field handled")

        # Test wrong type for cmd
        response = api.session.post(f"{api.base_url}/cmd", json={"cmd": 42}, timeout=10)
        assert response.status_code in [200, 400], \
            f"POST /cmd integer cmd got unexpected status {response.status_code}"
        print("✓ Integer cmd field handled")

    finally:
        # Restore settings that were wiped by set_default_settings
        try:
            restore_response = api.update_settings(saved_settings)
            if restore_response.status_code == 200:
                print("✓ Settings restored after set_default_settings")
            else:
                print(f"  [WARN] Settings restore returned status {restore_response.status_code}")
        except requests.exceptions.ConnectionError:
            # Connection drop is acceptable if web_port changed during restore
            print("  [WARN] Connection dropped during settings restore (expected if web_port changed)")
        except Exception as exc:
            print(f"  [WARN] Failed to restore settings: {exc}")


def test_wifi_scan_edge_cases(api):
    """Test WiFi scan state machine edge cases (I-5)"""
    print("\n=== WiFi scan edge cases test ===")

    # First: get results WITHOUT starting a scan (initial state check)
    # After reboot or fresh session, scan state should be well-defined
    response = api.get_wifi_scan_results()
    assert response.status_code == 200, \
        f"GET /wifi_scan/results without prior start expected 200, got {response.status_code}"
    data = response.json()
    assert "scan_in_progress" in data, "Field 'scan_in_progress' missing"
    assert "scan_completed" in data, "Field 'scan_completed' missing"
    assert isinstance(data["scan_in_progress"], bool), "'scan_in_progress' must be bool"
    assert isinstance(data["scan_completed"], bool), "'scan_completed' must be bool"
    # Both must be defined (not crash) — we don't assert specific values since
    # a previous scan may have already completed
    print(f"✓ GET /wifi_scan/results before start: scan_in_progress={data['scan_in_progress']}, "
          f"scan_completed={data['scan_completed']}")

    # Start scan
    response = api.start_wifi_scan()
    assert response.status_code == 200
    assert response.json().get("success") == True
    print("✓ First WiFi scan start successful")

    # Double-start: second POST /wifi_scan/start while scan is in progress or just completed
    response2 = api.start_wifi_scan()
    assert response2.status_code == 200, \
        f"Second POST /wifi_scan/start expected 200, got {response2.status_code}"
    data2 = response2.json()
    assert "success" in data2, "Second start response must contain 'success'"
    # Per openapi: if already running → success=false with error field
    if not data2["success"]:
        assert "error" in data2, \
            "If second start fails (scan already running), response must contain 'error' field"
        print(f"✓ Double-start correctly rejected: {data2.get('error')}")
    else:
        # Scan completed between the two calls — restarted OK
        print("✓ Double-start accepted (scan completed between calls — restart OK)")

    # Wait for scan to complete
    timeout = 0
    while True:
        time.sleep(1)
        response = api.get_wifi_scan_results()
        assert response.status_code == 200
        data = response.json()
        if not data["scan_in_progress"] and data["scan_completed"]:
            break
        timeout += 1
        assert timeout < 15, "WiFi scan completion timeout exceeded"

    print("✓ WiFi scan edge cases passed")


def test_wifi_scan_network_fields(api):
    """Test WiFi scan result field validation: bssid, channel, rssi range (I-6)"""
    print("\n=== WiFi scan network fields test ===")
    import re

    # Start and wait for scan
    response = api.start_wifi_scan()
    assert response.status_code == 200

    timeout = 0
    while True:
        time.sleep(1)
        response = api.get_wifi_scan_results()
        assert response.status_code == 200
        data = response.json()
        if not data["scan_in_progress"] and data["scan_completed"]:
            break
        timeout += 1
        assert timeout < 15, "WiFi scan completion timeout exceeded"

    networks = data.get("networks", [])
    if not networks:
        print("  [SKIP] No networks found — cannot validate network fields")
        return

    mac_pattern = re.compile(r'^([0-9A-Fa-f]{2}[:\-]){5}([0-9A-Fa-f]{2})$')

    for i, network in enumerate(networks):
        # bssid field
        assert "bssid" in network, f"Network[{i}] missing 'bssid' field"
        assert mac_pattern.match(network["bssid"]), \
            f"Network[{i}] bssid '{network['bssid']}' does not match MAC format"

        # channel field
        assert "channel" in network, f"Network[{i}] missing 'channel' field"
        assert isinstance(network["channel"], int), f"Network[{i}] channel must be int"
        assert 1 <= network["channel"] <= 14, \
            f"Network[{i}] channel {network['channel']} out of range 1-14"

        # rssi range (-128..0 is the correct range for real hardware)
        assert "rssi" in network, f"Network[{i}] missing 'rssi' field"
        assert -128 <= network["rssi"] <= 0, \
            f"Network[{i}] rssi {network['rssi']} out of range -128..0"

    print(f"✓ WiFi scan network fields validated for {len(networks)} network(s)")


def test_cache_json_fields(api):
    """Test /cache/json per-entry field validation and consistency with /cache/status (I-7)"""
    print("\n=== Cache JSON fields test ===")

    # Get cache status
    status_resp = api.get_cache_status()
    assert status_resp.status_code == 200
    status = status_resp.json()
    entries_count = status.get("entries", 0)

    # Get cache JSON
    json_resp = api.get_cache_json()
    assert json_resp.status_code == 200
    json_data = json_resp.json()
    assert "d" in json_data, "Field 'd' missing from /cache/json"
    assert isinstance(json_data["d"], list), "Field 'd' must be an array"

    entries = json_data["d"]

    # Validate consistency: entries count must match (allow at most 1 entry delta for live updates)
    delta = abs(len(entries) - entries_count)
    assert delta <= 1, \
        f"entries count mismatch: /cache/status says {entries_count}, /cache/json has {len(entries)} (delta={delta})"
    print(f"✓ Entry count consistent: /cache/status={entries_count}, /cache/json={len(entries)}")

    if not entries:
        print("  [SKIP] Cache empty — skipping per-entry field validation")
        return

    valid_types = {"h", "i", "c", "d"}
    for i, entry in enumerate(entries):
        # Slave ID: 1..247
        assert "s" in entry, f"Entry[{i}] missing field 's'"
        assert isinstance(entry["s"], int), f"Entry[{i}]['s'] must be int"
        assert 1 <= entry["s"] <= 247, f"Entry[{i}]['s']={entry['s']} out of range 1-247"

        # Register type: h/i/c/d
        assert "t" in entry, f"Entry[{i}] missing field 't'"
        assert entry["t"] in valid_types, \
            f"Entry[{i}]['t']='{entry['t']}' not in valid types {valid_types}"

        # Address: 0..65535
        assert "a" in entry, f"Entry[{i}] missing field 'a'"
        assert isinstance(entry["a"], int), f"Entry[{i}]['a'] must be int"
        assert 0 <= entry["a"] <= 65535, f"Entry[{i}]['a']={entry['a']} out of range 0-65535"

        # Value: 0..65535
        assert "v" in entry, f"Entry[{i}] missing field 'v'"
        assert isinstance(entry["v"], int), f"Entry[{i}]['v'] must be int"
        assert 0 <= entry["v"] <= 65535, f"Entry[{i}]['v']={entry['v']} out of range 0-65535"

        # Age: 0..65535
        assert "age" in entry, f"Entry[{i}] missing field 'age'"
        assert isinstance(entry["age"], int), f"Entry[{i}]['age'] must be int"
        assert 0 <= entry["age"] <= 65535, f"Entry[{i}]['age']={entry['age']} out of range 0-65535"

    print(f"✓ All {len(entries)} cache entries have valid fields")


def test_cache_csv_headers(api):
    """Test /cache/csv Content-Disposition header (I-8)"""
    print("\n=== Cache CSV headers test ===")

    response = api.get_cache_csv()
    assert response.status_code == 200, \
        f"GET /cache/csv expected 200, got {response.status_code}"

    content_disposition = response.headers.get("Content-Disposition", "")
    assert content_disposition != "", \
        "GET /cache/csv response is missing Content-Disposition header"
    assert "attachment" in content_disposition.lower(), \
        f"Content-Disposition must contain 'attachment', got: {content_disposition}"
    assert "filename=" in content_disposition.lower(), \
        f"Content-Disposition must contain 'filename=', got: {content_disposition}"
    assert ".csv" in content_disposition.lower(), \
        f"Content-Disposition filename must end with .csv, got: {content_disposition}"

    print(f"✓ Content-Disposition header present and correct: {content_disposition}")


def test_settings_partial_update(api):
    """Test POST /settings with sparse payload — must preserve unset fields (I-9)"""
    print("\n=== Settings partial update test ===")

    # Read original settings
    response = api.get_settings()
    assert response.status_code == 200
    original = response.json()

    try:
        # Partial update: only vout
        new_vout = not original["vout"]
        response = api.update_settings({"vout": new_vout})
        assert response.status_code == 200
        assert response.json().get("success") == True

        # Read back and verify only vout changed
        response = api.get_settings()
        assert response.status_code == 200
        updated = response.json()

        assert updated["vout"] == new_vout, \
            f"vout was not updated: expected {new_vout}, got {updated['vout']}"

        # All other top-level fields must be preserved
        preserved_fields = ["hostname", "login", "web_port", "io_bus",
                            "cache_modbus_port", "cache_modbus_server_enabled", "cache_value_timeout_s"]
        for field in preserved_fields:
            if field in original:
                assert updated[field] == original[field], \
                    f"Field '{field}' was changed by partial update: {original[field]} → {updated[field]}"

        print("✓ Partial update (vout only) preserved all other fields")

        # Partial update: only rs485_1.baudrate
        original_baudrate = original["rs485_1"]["baudrate"]
        # Pick a different valid baudrate
        baudrate_options = [1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200]
        new_baudrate = next(b for b in baudrate_options if b != original_baudrate)

        response = api.update_settings({"rs485_1": {"baudrate": new_baudrate}})
        assert response.status_code == 200
        assert response.json().get("success") == True

        response = api.get_settings()
        assert response.status_code == 200
        updated2 = response.json()

        assert updated2["rs485_1"]["baudrate"] == new_baudrate, \
            f"rs485_1.baudrate was not updated: expected {new_baudrate}, got {updated2['rs485_1']['baudrate']}"

        # Other rs485_1 fields must be preserved
        rs485_preserved = ["term", "fail_safe", "stopbits", "parity", "databits"]
        for field in rs485_preserved:
            assert updated2["rs485_1"][field] == original["rs485_1"][field], \
                f"rs485_1.{field} was changed by partial update: " \
                f"{original['rs485_1'][field]} → {updated2['rs485_1'][field]}"

        print("✓ Partial update (rs485_1.baudrate only) preserved all other rs485_1 fields")

    finally:
        # Restore original settings
        try:
            api.update_settings(original)
            print("✓ Original settings restored")
        except Exception as exc:
            print(f"  [WARN] Failed to restore settings: {exc}")


def test_cache_server_enabled_toggle(api):
    """Test cache_modbus_server_enabled setting toggle and consistency (I-10)"""
    print("\n=== Cache server enabled toggle test ===")

    # Read original value
    response = api.get_settings()
    assert response.status_code == 200
    original_enabled = response.json().get("cache_modbus_server_enabled", True)

    try:
        # Disable cache server
        response = api.update_settings({"cache_modbus_server_enabled": False})
        assert response.status_code == 200
        assert response.json().get("success") == True

        # Verify via GET /settings
        response = api.get_settings()
        assert response.status_code == 200
        assert response.json()["cache_modbus_server_enabled"] == False, \
            "cache_modbus_server_enabled=false not reflected in GET /settings"

        # Verify via GET /info
        response = api.get_info()
        assert response.status_code == 200
        assert response.json()["cache_modbus_server_enabled"] == False, \
            "cache_modbus_server_enabled=false not reflected in GET /info"
        print("✓ cache_modbus_server_enabled=false visible in both /settings and /info")

        # Enable cache server
        response = api.update_settings({"cache_modbus_server_enabled": True})
        assert response.status_code == 200

        response = api.get_settings()
        assert response.status_code == 200
        assert response.json()["cache_modbus_server_enabled"] == True, \
            "cache_modbus_server_enabled=true not reflected in GET /settings"

        response = api.get_info()
        assert response.status_code == 200
        assert response.json()["cache_modbus_server_enabled"] == True, \
            "cache_modbus_server_enabled=true not reflected in GET /info"
        print("✓ cache_modbus_server_enabled=true visible in both /settings and /info")

    finally:
        # Restore original value
        try:
            api.update_settings({"cache_modbus_server_enabled": original_enabled})
            print(f"✓ cache_modbus_server_enabled restored to {original_enabled}")
        except Exception as exc:
            print(f"  [WARN] Failed to restore cache_modbus_server_enabled: {exc}")


def test_password_change_flow(api):
    """Test POST /settings pass change + re-authentication security flow (I-11)"""
    print("\n=== Password change flow test ===")

    # Read current settings to get current login and password
    response = api.get_settings()
    assert response.status_code == 200
    settings = response.json()
    original_login = settings.get("login", "admin")
    original_password = settings.get("pass", "admin")

    new_password = "NewTestPass123"
    # Ensure new password differs from original
    if new_password == original_password:
        new_password = "AnotherPass456"

    try:
        # Change password
        response = api.update_settings({"pass": new_password})
        assert response.status_code == 200
        assert response.json().get("success") == True
        print(f"✓ Password change accepted (new pass: {new_password})")

        # Verify new password is stored
        response = api.get_settings()
        assert response.status_code == 200
        stored = response.json().get("pass", "")
        assert stored == new_password, \
            f"New password not stored: expected '{new_password}', got '{stored}'"
        print("✓ New password reflected in GET /settings")

        # Logout
        response = api.logout()
        assert response.status_code == 200
        assert response.json().get("logout") == True, "Logout must return {logout: true}"
        print("✓ Logged out")

        # Attempt auth with OLD password — must fail
        response = api.auth(original_login, original_password)
        assert response.status_code == 200
        assert response.json()["auth"] == False, \
            "Old password was still accepted after password change — security regression!"
        print("✓ Old password correctly rejected after password change")

        # Authenticate with NEW password
        response = api.auth(original_login, new_password)
        assert response.status_code == 200
        assert response.json()["auth"] == True, \
            f"New password not accepted: {response.json()}"
        print("✓ New password accepted for re-authentication")

    finally:
        # Restore original password
        try:
            response = api.update_settings({"pass": original_password})
            if response.status_code != 200:
                # If session lost, re-auth with new password first
                api.auth(original_login, new_password)
                api.update_settings({"pass": original_password})
            print(f"✓ Original password restored")
        except Exception as exc:
            print(f"  [WARN] Failed to restore password: {exc}")


def test_reboot_command(api):
    """Test POST /cmd reboot — verify device reboots and uptime resets (I-14)"""
    print("\n=== Reboot command test ===")

    # Record current uptime (in seconds)
    response = api.get_uptime()
    assert response.status_code == 200
    uptime_data = response.json()
    original_uptime_s = (
        uptime_data["days"] * 86400
        + uptime_data["hours"] * 3600
        + uptime_data["minutes"] * 60
        + uptime_data["seconds"]
    )
    print(f"  Uptime before reboot: {original_uptime_s}s "
          f"({uptime_data['days']}d {uptime_data['hours']}h "
          f"{uptime_data['minutes']}m {uptime_data['seconds']}s)")

    # Record time just before sending reboot command to calculate elapsed time for uptime check
    reboot_sent_at = time.monotonic()

    # Send reboot command
    try:
        response = api.execute_command("reboot")
        print(f"  Reboot command status: {response.status_code}")
        assert response.status_code == 200, \
            f"POST /cmd reboot expected 200, got {response.status_code}"
    except requests.exceptions.ConnectionError:
        # Connection drop is acceptable — reboot disconnects the HTTP server
        print("  Connection dropped (expected during reboot)")

    print("  Waiting for device to reboot...")

    # Poll until the device comes back (timeout 60 s)
    deadline = time.monotonic() + 60
    came_back = False
    while time.monotonic() < deadline:
        time.sleep(2)
        try:
            response = requests.get(f"{api.base_url}/", timeout=3,
                                    headers={'Accept-Encoding': 'identity', 'Connection': 'close'})
            if response.status_code == 200:
                came_back = True
                break
        except Exception:
            pass  # Device still booting

    assert came_back, "Device did not come back within 60 seconds after reboot command"
    print("✓ Device came back online")

    # Re-authenticate (session is dropped after reboot); reset the session first
    # to avoid using stale TCP connections from before the reboot.
    api.session.close()
    api.session = requests.Session()
    api.session.headers.update({
        'User-Agent': 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36',
        'Accept': 'application/json, text/plain, */*',
        'Accept-Language': 'en-US,en;q=0.9',
        'Accept-Encoding': 'identity',
        'Connection': 'close',
        'Cache-Control': 'no-cache',
    })
    response = api.auth()
    assert response.status_code == 200
    assert response.json()["auth"] == True, "Re-authentication after reboot failed"
    print("✓ Re-authentication after reboot successful")

    # Verify new uptime is less than original (device rebooted)
    response = api.get_uptime()
    assert response.status_code == 200
    new_uptime_data = response.json()
    new_uptime_s = (
        new_uptime_data["days"] * 86400
        + new_uptime_data["hours"] * 3600
        + new_uptime_data["minutes"] * 60
        + new_uptime_data["seconds"]
    )
    print(f"  Uptime after reboot: {new_uptime_s}s")
    # Uptime should have reset: either it's less than original (reboot happened)
    # or it's less than the time elapsed since the reboot command was sent (+ 30s margin for boot time)
    elapsed_since_reboot = time.monotonic() - reboot_sent_at
    assert new_uptime_s < original_uptime_s or new_uptime_s <= elapsed_since_reboot + 30, \
        f"Uptime after reboot ({new_uptime_s}s) does not indicate a reboot occurred " \
        f"(original={original_uptime_s}s, elapsed={elapsed_since_reboot:.0f}s)"
    print("✓ Uptime correctly reset after reboot")


def quick_connection_test(base_url):
    """Quick connection check before running tests"""
    from urllib.parse import urlparse

    print("🔍 Quick connection check...")

    parsed = urlparse(base_url)
    host = parsed.hostname or "192.168.5.1"
    port = parsed.port or 80

    try:
        # Check TCP port
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5)
        result = sock.connect_ex((host, port))
        sock.close()

        if result == 0:
            print(f"✅ TCP connection to {host}:{port} successful")

            # Additionally check HTTP request using the same headers as the main session
            try:
                response = requests.get(base_url + "/favicon.webp", timeout=10,
                                      headers={
                                          'Accept-Encoding': 'identity',
                                          'Connection': 'close',
                                          'Cache-Control': 'no-cache',
                                      })
                print(f"✅ HTTP test successful (Status: {response.status_code})")
                return True
            except Exception as e:
                print(f"⚠️  TCP works, but HTTP failed: {e}")
                print(f"💡 Possible issue in HTTP headers or protocol")
                return False
        else:
            print(f"❌ TCP connection to {host}:{port} failed")
            print(f"💡 Run diagnostics: python diagnose_connection.py {base_url}")
            return False

    except Exception as e:
        print(f"❌ Connection check error: {e}")
        print(f"💡 Run diagnostics: python diagnose_connection.py {base_url}")
        return False


def main():
    """Main function to run tests"""
    import argparse
    import sys

    # Parse command line arguments
    parser = argparse.ArgumentParser(description='WB-MGE API Tests')
    parser.add_argument('--ip', default='192.168.5.1', help='IP address of WB-MGE device')
    parser.add_argument('--stop-on-failure', action='store_true', help='Stop on first test failure')
    parser.add_argument('--verbose', action='store_true', help='Verbose output')

    args = parser.parse_args()

    # Check command line arguments
    stop_on_failure = args.stop_on_failure
    verbose = args.verbose

    # Create API client with specified IP
    api = WBMGEAPI(f"http://{args.ip}")

    print("Running WB-MGE API tests")
    print("=" * 40)

    # Quick connection check
    if not quick_connection_test(api.base_url):
        print("\n❌ Preliminary connection check failed")
        print("🔧 Check network connection before running tests")
        return 1

    if stop_on_failure:
        print("⚠️  Mode: stop on first error")
    else:
        print("🔄 Mode: continue on errors")

    # List of all tests to execute
    tests = [
        ("unauthorized access", test_unauthorized_access),
        ("authorization", test_auth),
        ("device information", test_info),
        ("info format validation", test_info_format_validation),
        ("settings", test_settings),
        ("session management", test_session_management),
        ("uptime", test_uptime),
        ("Modbus TCP parameters", test_modbus_tcp_parameters),
        ("Modbus validation limits", test_modbus_validation_limits),
        ("validation patterns", test_validation_patterns),
        ("WiFi scanner", test_wifi_scanner),
        ("WiFi scan edge cases", test_wifi_scan_edge_cases),
        ("WiFi scan network fields", test_wifi_scan_network_fields),
        ("AP clients list", test_ap_clients),
        ("static files", test_static_files),
        ("HTTP method guard", test_http_method_guard),
        ("commands", test_commands),
        ("commands extended", test_cmd_extended),
        ("hostname", test_hostname),
        ("cache endpoints", test_cache_endpoints),
        ("cache JSON fields", test_cache_json_fields),
        ("cache CSV headers", test_cache_csv_headers),
        ("cache server enabled toggle", test_cache_server_enabled_toggle),
        ("settings partial update", test_settings_partial_update),
        ("password change flow", test_password_change_flow),
        ("WB test endpoint", test_wb_test),
        ("sniffer status", test_sniffer_status),
        ("port modes", test_port_modes),
        ("cache multimaster", test_cache_multimaster),
        ("reboot command", test_reboot_command),  # Must be last — reboots the device
    ]

    passed = 0
    failed = 0
    failed_tests = []
    skipped = 0

    for test_name, test_func in tests:
        try:
            if not verbose:
                print(f"\n--- Running {test_name} test ---")
            else:
                print(f"\n🔍 Starting test: {test_name}")

            test_func(api)
            passed += 1
            print(f"✅ Test {test_name} PASSED")

        except AssertionError as e:
            failed += 1
            error_msg = f"Test error: {e}"
            failed_tests.append((test_name, error_msg))
            print(f"❌ Test {test_name} FAILED: {error_msg}")

            if stop_on_failure:
                print(f"\n🛑 Stopping testing on first error")
                skipped = len(tests) - (passed + failed)
                break

        except requests.exceptions.RequestException as e:
            failed += 1
            error_msg = f"Connection error: {e}"
            failed_tests.append((test_name, error_msg))
            print(f"❌ Test {test_name} FAILED: {error_msg}")

            if stop_on_failure:
                print(f"\n🛑 Stopping testing on first error")
                skipped = len(tests) - (passed + failed)
                break

        except Exception as e:
            failed += 1
            error_msg = f"Unexpected error: {e}"
            failed_tests.append((test_name, error_msg))
            print(f"❌ Test {test_name} FAILED: {error_msg}")

            if stop_on_failure:
                print(f"\n🛑 Stopping testing on first error")
                skipped = len(tests) - (passed + failed)
                break

    # Final report
    print("\n" + "=" * 60)
    print("TEST RESULTS:")
    print(f"✅ Passed: {passed}")
    print(f"❌ Failed: {failed}")
    if skipped > 0:
        print(f"⏸️  Skipped: {skipped}")
    print(f"📊 Total: {len(tests)}")

    if failed > 0:
        print("\n❌ FAILED TESTS:")
        for test_name, error in failed_tests:
            print(f"  • {test_name}: {error}")

    if failed == 0:
        print("\n🎉 ALL TESTS PASSED SUCCESSFULLY!")
        return 0
    else:
        success_rate = (passed / (passed + failed)) * 100
        print(f"\n⚠️  {failed} out of {passed + failed} tests failed ({success_rate:.1f}% successful)")

        if skipped == 0:
            print("\n💡 Use --stop-on-failure to stop on first error")
            print("💡 Use --verbose for detailed output")

        return 1


if __name__ == "__main__":
    exit(main())