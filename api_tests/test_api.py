#!/usr/bin/env python3
"""
Simple tests for WB-MGE HTTP API
"""

import requests
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
        return self.session.post(f"{self.base_url}/wifi_scan/start")

    def get_wifi_scan_results(self):
        """Get WiFi scan results"""
        return self.session.get(f"{self.base_url}/wifi_scan/results")

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
        return self.session.get(f"{self.base_url}/uptime")

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
    assert 1 <= wifi["ap_channel"] <= 13, f"Field ap_channel has incorrect value: {wifi['ap_channel']}"

    # Check rs485 ports structure
    for port in ["rs485_1", "rs485_2"]:
        assert port in data, f"Section {port} is missing"
        rs485 = data[port]

        assert "is_busy" in rs485, "Field is_busy is missing"
        assert "error_percentage" in rs485, "Field error_percentage is missing"
        assert "server_connections_count" in rs485, "Field server_connections_count is missing"

        assert isinstance(rs485["is_busy"], bool), "Field is_busy has incorrect type"
        assert isinstance(rs485["error_percentage"], int), "Field error_percentage has incorrect type"
        assert isinstance(rs485["server_connections_count"], int), "Field server_connections_count has incorrect type"

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

    assert wifi["mode"] in ["ap", "sta", "apsta", "none"], f"Field mode has incorrect value: {wifi['mode']}"
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
        "web_port": 8080,               # Valid port
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
    # API should either reject (400) or accept but not save invalid values
    assert response.status_code in [200, 400]
    print("✓ Invalid settings handling works")

    # Check that invalid settings are not saved
    response = api.get_settings()
    assert response.status_code == 200
    valid_settings = response.json()
    assert valid_settings == new_settings, "Invalid settings were saved"
    print("✓ Invalid settings are not saved")

    # Restore settings
    response = api.update_settings(original_settings)
    assert response.status_code == 200
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

    try:
        # Test set_default_settings command (safe)
        print("Sending set_default_settings command...")
        response = api.execute_command("set_default_settings")

        print(f"Status Code: {response.status_code}")
        print(f"Headers: {response.headers}")
        print(f"Content: {response.text[:500]}...")  # First 500 characters

        assert response.status_code == 200, f"Expected status 200, got {response.status_code}"

        # According to HTTP server code, commands return empty response, not JSON
        if response.text.strip():
            try:
                data = response.json()
                print(f"JSON Response: {data}")
            except Exception as e:
                print(f"Failed to parse JSON: {e}")
                print(f"Raw response: {response.text}")
        else:
            print("Empty response received (expected for commands)")

        print("✓ Command set_default_settings works")

        # NOT testing reboot (dangerous for auto-tests)
        print("✓ Dangerous commands skipped for safety")

    except requests.exceptions.RequestException as e:
        print(f"❌ Connection error executing command: {e}")
        raise
    except Exception as e:
        print(f"❌ Unexpected error in commands test: {e}")
        print(f"Error type: {type(e).__name__}")
        raise


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

    # Test with invalid ports
    invalid_settings = {
        "rs485_1": {
            "bridge": {
                "modbus": True,
                "port": 0          # Invalid port
            }
        }
    }

    response = api.update_settings(invalid_settings)
    # API should either reject or correct values
    assert response.status_code in [200, 400]
    print("✓ Invalid ports are handled")

    # Test with port exceeding limit
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
        },
        "web_port": 1                      # Minimum port
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
    assert data["scan_in_progress"] == True, "scan_in_progress should be true"
    assert data["scan_completed"] == False, "scan_completed should be false"

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
        assert timeout < 10, "Scan completion timeout exceeded"

    if "networks" in data:
        assert isinstance(data["networks"], list)
        for network in data["networks"]:
            assert "ssid" in network
            assert "rssi" in network
            assert -100 <= network["rssi"] <= 0

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


def quick_connection_test(base_url):
    """Quick connection check before running tests"""
    import socket
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

            # Additionally check HTTP request
            try:
                import requests
                response = requests.get(base_url + "/favicon.webp", timeout=5,
                                      headers={'Connection': 'close'})
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
    stop_on_failure = args.stop_on_failure or "--stop-on-failure" in sys.argv
    verbose = args.verbose or "--verbose" in sys.argv

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
        ("settings", test_settings),
        ("session management", test_session_management),
        ("uptime", test_uptime),
        ("Modbus TCP parameters", test_modbus_tcp_parameters),
        ("Modbus validation limits", test_modbus_validation_limits),
        ("validation patterns", test_validation_patterns),
        ("WiFi scanner", test_wifi_scanner),
        ("AP clients list", test_ap_clients),
        ("static files", test_static_files),
        ("commands", test_commands),
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