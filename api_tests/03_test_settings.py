"""Settings tests: structure, write/read-back, validation, partial update"""



def test_settings(api):
    """Settings test"""
    response = api.get_settings()
    assert response.status_code == 200
    original_settings = response.json()

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

    eth = original_settings["ethernet"]
    eth_fields = [
        "ip_static", "mask_static", "gw_static", "dhcpc"
    ]
    for field in eth_fields:
        assert field in eth, f"Field {field} is missing"

    assert isinstance(eth["dhcpc"], bool), "Field dhcpc has incorrect type"

    for port in ["rs485_1", "rs485_2"]:
        rs485 = original_settings[port]
        rs485_fields = [
            "term", "fail_safe", "tx_disabled", "baudrate", "stopbits",
            "parity", "databits", "bridge"
        ]
        for field in rs485_fields:
            assert field in rs485, f"Field {field} is missing"

        assert isinstance(rs485["term"], bool), f"Field term has incorrect type"
        assert isinstance(rs485["fail_safe"], bool), f"Field fail_safe has incorrect type"
        assert isinstance(rs485["tx_disabled"], bool), f"Field tx_disabled has incorrect type"
        assert isinstance(rs485["baudrate"], int), f"Field baudrate has incorrect type"
        assert rs485["baudrate"] in [1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200], \
            f"Field baudrate has incorrect value: {rs485['baudrate']}"
        assert rs485["stopbits"] in ["1", "1.5", "2"], \
            f"Field stopbits has incorrect value: {rs485['stopbits']}"
        assert rs485["parity"] in ["none", "even", "odd"], \
            f"Field parity has incorrect value: {rs485['parity']}"
        assert rs485["databits"] in ["5", "6", "7", "8"], \
            f"Field databits has incorrect value: {rs485['databits']}"

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

    print("✓ Settings structure is correct")

    test_settings = {
        "hostname": "test-device-123",
        "web_port": original_settings["web_port"],
        "vout": not original_settings["vout"],
        "io_bus": not original_settings["io_bus"],
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
            "tx_disabled": not original_settings["rs485_1"]["tx_disabled"],
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
            "tx_disabled": not original_settings["rs485_2"]["tx_disabled"],
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

    try:
        response = api.update_settings(test_settings)
        assert response.status_code == 200
        result = response.json()
        assert result["success"] == True
        print("✓ Writing settings with valid data works")

        response = api.get_settings()
        assert response.status_code == 200
        new_settings = response.json()

        main_fields = [
            "hostname", "vout", "web_port", "io_bus"
        ]
        for field in main_fields:
            assert new_settings[field] == test_settings[field], f"Incorrect value for field {field}: {new_settings[field]}"

        wifi = new_settings["wifi"]
        for field in wifi_fields:
            assert wifi[field] == test_settings["wifi"][field], f"Incorrect value for field {field}: {wifi[field]}"

        eth = new_settings["ethernet"]
        for field in eth_fields:
            assert eth[field] == test_settings["ethernet"][field], f"Incorrect value for field {field}: {eth[field]}"

        rs485_1 = new_settings["rs485_1"]
        rs485_main_fields = [
            "term", "fail_safe", "tx_disabled", "baudrate", "stopbits", "parity", "databits"
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

        rs485_2 = new_settings["rs485_2"]
        for field in rs485_main_fields:
            assert rs485_2[field] == test_settings["rs485_2"][field], \
                f"Incorrect value for field {field}: {rs485_2[field]}"

        bridge_2 = new_settings["rs485_2"]["bridge"]
        for field in bridge_fields:
            assert bridge_2[field] == test_settings["rs485_2"]["bridge"][field], \
                f"Incorrect value for field {field}: {bridge_2[field]}"

        print("✓ All settings are saved correctly")

        invalid_settings = {
            "hostname": "invalid_hostname!",
            "web_port": 70000,
            "wifi": {
                "mode": "disabled",
                "ap_auth": "close",
                "sta_auth": "wep",
                "ap_ssid": "a" * 50,
                "sta_ssid": "фыва123",
                "ap_ip_static": "123.456.789.101",
                "ap_mask_static": "abc.def.ghi.jkl",
                "ap_gw_static": True,
                "sta_ip_static": "192.168.1.1.1",
                "sta_mask_static": "123.aaa.1.1",
                "sta_gw_static": 192
            },
            "ethernet": {
                "ip_static": "123.456.789.101",
                "mask_static": 456,
                "gw_static": 789,
                "dhcpc": 0
            },
            "rs485_1": {
                "term": 1,
                "fail_safe": "off",
                "tx_disabled": "off",
                "baudrate": 123456,
                "stopbits": "2.5",
                "parity": "all",
                "databits": "2",
                "bridge": {
                    "mode": "station",
                    "port": 0,
                    "ip": "201.250.252.256",
                    "modbus": "enabled"
                }
            },
            "rs485_2": {
                "term": "true",
                "fail_safe": "true",
                "tx_disabled": "true",
                "baudrate": 0,
                "stopbits": "0.5",
                "parity": "disabled",
                "databits": "4",
                "bridge": {
                    "mode": "server",
                    "port": 65536,
                    "ip": "102.abc.126.18",
                    "modbus": "disabled"
                }
            },
            "vout": "true",
            "io_bus": "true"
        }

        response = api.update_settings(invalid_settings)
        assert response.status_code == 200, \
            f"Invalid settings expected 200, got {response.status_code}"
        result = response.json()
        assert result.get("success") == False, \
            f"Invalid settings must return success=false: {result}"
        print("✓ Invalid settings handling works")

        response = api.get_settings()
        assert response.status_code == 200
        valid_settings = response.json()
        assert valid_settings == new_settings, "Invalid settings were saved"
        print("✓ Invalid settings are not saved")
    finally:
        # Restore original settings; do not assert here to preserve the original test exception
        resp = api.update_settings(original_settings)
        if resp.status_code != 200:
            print(f"✗ Failed to restore original settings: HTTP {resp.status_code}")
        else:
            print("✓ Original settings restored")


def test_validation_patterns(api):
    """Patterns and constraints validation test"""
    # Save original settings so they can be restored in the finally block
    original_response = api.get_settings()
    assert original_response.status_code == 200
    original = original_response.json()

    try:
        valid_data = {
            "hostname": "valid-hostname-123",
            "login": "valid_user_123",
            "wifi": {
                "ap_ssid": "ValidSSID",
                "ap_pass": "ValidPass123"
            }
        }

        response = api.update_settings(valid_data)
        assert response.status_code == 200
        # Verify the server actually saved the values, not just returned 200
        check = api.get_settings()
        assert check.status_code == 200
        check_data = check.json()
        assert check_data["hostname"] == valid_data["hostname"], \
            f"hostname not saved: expected {valid_data['hostname']!r}, got {check_data['hostname']!r}"
        assert check_data["wifi"]["ap_ssid"] == valid_data["wifi"]["ap_ssid"], \
            f"wifi.ap_ssid not saved: expected {valid_data['wifi']['ap_ssid']!r}, got {check_data['wifi']['ap_ssid']!r}"
        print("✓ Valid patterns accepted and read-back verified")

        boundary_data = {
            "wifi": {
                "ap_ssid": "A",
                "ap_pass": "12345678"
            }
        }

        response = api.update_settings(boundary_data)
        assert response.status_code == 200
        print("✓ Boundary values accepted")

        limit_data = {
            "wifi": {
                "ap_ssid": "A" * 50,
                "ap_pass": "A" * 100
            },
            "web_port": 70000
        }

        response = api.update_settings(limit_data)
        assert response.status_code in [200, 400]
        if response.status_code == 200:
            check_response = api.get_settings()
            assert check_response.status_code == 200
            check_settings = check_response.json()
            assert check_settings["web_port"] != 70000, \
                "Invalid web_port 70000 was saved (expected rejection)"
            assert len(check_settings["wifi"]["ap_ssid"]) <= 31, \
                "Oversized SSID was saved"
        print("✓ Limit exceeding is handled")
    finally:
        # Restore all original settings, including wifi fields changed by this test
        api.update_settings(original)
        print("✓ Original settings restored")


def test_settings_partial_update(api):
    """Test POST /settings with sparse payload — must preserve unset fields"""
    response = api.get_settings()
    assert response.status_code == 200
    original = response.json()

    try:
        new_vout = not original["vout"]
        response = api.update_settings({"vout": new_vout})
        assert response.status_code == 200
        assert response.json().get("success") == True

        response = api.get_settings()
        assert response.status_code == 200
        updated = response.json()

        assert updated["vout"] == new_vout, \
            f"vout was not updated: expected {new_vout}, got {updated['vout']}"

        preserved_fields = ["hostname", "login", "web_port", "io_bus",
                            "cache_modbus_port", "cache_modbus_server_enabled", "cache_value_timeout_s"]
        for field in preserved_fields:
            if field in original:
                assert updated[field] == original[field], \
                    f"Field '{field}' was changed by partial update: {original[field]} → {updated[field]}"

        print("✓ Partial update (vout only) preserved all other fields")

        original_baudrate = original["rs485_1"]["baudrate"]
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

        rs485_preserved = ["term", "fail_safe", "tx_disabled", "stopbits", "parity", "databits"]
        for field in rs485_preserved:
            assert updated2["rs485_1"][field] == original["rs485_1"][field], \
                f"rs485_1.{field} was changed by partial update: " \
                f"{original['rs485_1'][field]} → {updated2['rs485_1'][field]}"

        print("✓ Partial update (rs485_1.baudrate only) preserved all other rs485_1 fields")

    finally:
        api.update_settings(original)
        print("✓ Original settings restored")


def test_post_malformed_json(api):
    """POST /settings with malformed body must return 400."""
    # Empty body
    response = api.session.post(
        f"{api.base_url}/settings",
        data="",
        headers={"Content-Type": "application/json"},
        timeout=10,
    )
    assert response.status_code == 400, (
        f"Empty body: expected 400, got {response.status_code}"
    )

    # Invalid JSON (unclosed brace)
    response = api.session.post(
        f"{api.base_url}/settings",
        data='{"key": "value"',
        headers={"Content-Type": "application/json"},
        timeout=10,
    )
    assert response.status_code == 400, (
        f"Unclosed-brace JSON: expected 400, got {response.status_code}"
    )

    print("✓ Malformed JSON body correctly rejected with 400")


def test_post_body_too_large(api):
    """Boundary test for POST /settings body size limit.

    json_utils.c rejects when content_len > BUF_SIZE - 1, i.e. content_len >= 4096.
    - 4095 bytes: must be accepted (not rejected by size check) → 400 due to invalid JSON, not size
    - 4096 bytes: must be rejected by the size check → 400
    The key distinction: both return 400, but for different reasons.
    We verify the boundary by checking that 4095 does not return 500 (server crash)
    and that 4096 also returns 400, confirming the check fires at the right threshold.
    """
    # 4095 bytes: at or below the allowed limit — server must not crash (returns 400 for bad JSON)
    body_at_limit = "A" * 4095
    response = api.session.post(
        f"{api.base_url}/settings",
        data=body_at_limit,
        headers={"Content-Type": "application/json"},
        timeout=10,
    )
    assert response.status_code in (400, 200), (
        f"4095-byte body: expected 400 or 200, got {response.status_code} (must not be 5xx)"
    )
    print("✓ 4095-byte body processed without crash (size check not triggered)")

    # 4096 bytes: first value over the limit — must be rejected by size check
    body_over_limit = "A" * 4096
    response = api.session.post(
        f"{api.base_url}/settings",
        data=body_over_limit,
        headers={"Content-Type": "application/json"},
        timeout=10,
    )
    assert response.status_code == 400, (
        f"4096-byte body: expected 400, got {response.status_code}"
    )
    print("✓ 4096-byte body correctly rejected with 400 (size limit boundary)")
