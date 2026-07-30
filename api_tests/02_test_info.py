"""Device information tests"""

import re


def test_info(api):
    """Device information test"""
    response = api.get_info()
    assert response.status_code == 200
    data = response.json()

    required_fields = [
        "device_name", "signature", "firmware", "git_info",
        "serial_num", "system_voltage", "config_button_presses"
    ]
    for field in required_fields:
        assert field in data, f"Field {field} is missing"

    assert isinstance(data["serial_num"], int), "Field serial_num has incorrect type"
    assert isinstance(data["system_voltage"], (int, float)), "Field system_voltage has incorrect type"
    assert isinstance(data["config_button_presses"], int), "Field config_button_presses has incorrect type"

    for heap_field in ["heap_total", "heap_free", "heap_min_free"]:
        assert heap_field in data, f"Field {heap_field} is missing"
        assert isinstance(data[heap_field], int) and data[heap_field] >= 0, \
            f"Field {heap_field} must be a non-negative integer"

    assert "psram_available" in data, "Field psram_available is missing"
    assert isinstance(data["psram_available"], bool), "Field psram_available has incorrect type"
    assert "psram_size_kb" in data, "Field psram_size_kb is missing"
    assert isinstance(data["psram_size_kb"], int) and data["psram_size_kb"] >= 0, \
        "Field psram_size_kb must be a non-negative integer"

    assert "cache_modbus_port" in data, "Field cache_modbus_port is missing"
    assert isinstance(data["cache_modbus_port"], int) and 1 <= data["cache_modbus_port"] <= 65535, \
        f"Field cache_modbus_port has incorrect value: {data['cache_modbus_port']}"
    assert "cache_modbus_server_enabled" in data, "Field cache_modbus_server_enabled is missing"
    assert isinstance(data["cache_modbus_server_enabled"], bool), \
        "Field cache_modbus_server_enabled has incorrect type"
    assert "cache_modbus_active_port" in data, "Field cache_modbus_active_port is missing"
    assert isinstance(data["cache_modbus_active_port"], int) and \
        0 <= data["cache_modbus_active_port"] <= 65535, \
        f"Field cache_modbus_active_port has incorrect value: {data['cache_modbus_active_port']}"
    if data["cache_modbus_server_enabled"]:
        # A healthy device serves the cache Modbus server on the port it was configured
        # for. 0 means nothing is listening: a failed start, or the server not being up
        # yet — it is started only after the network comes up, and it is briefly down
        # while a settings update re-inits the ports. Another port means it stayed on the
        # previous one after a failed port change (a stored port <= 0 gives the same
        # mismatch, but the range assertion above has already ruled that out).
        assert data["cache_modbus_active_port"] == data["cache_modbus_port"], \
            f"Cache Modbus server is enabled on port {data['cache_modbus_port']} but " \
            f"listens on {data['cache_modbus_active_port']}"
    else:
        # Same guarantees as the branch above: 0 is the steady state, and a non-zero port
        # is either a server that failed to stop or a disable still in flight — the NVS
        # write is visible in /info immediately, while the stop is asynchronous
        # (settings_update() only spawns the task; the release runs in the task, possibly
        # behind a 1 s delay), so enabled=false next to a live port is legitimate inside
        # that window. Asserting strict equality is still safe here because the suite runs
        # sequentially (pytest.ini sets neither -n nor random order, files run in numeric
        # order) and nothing before this file writes cache Modbus settings, so no disable
        # can be in flight — moving this check into a shared helper would break that.
        assert data["cache_modbus_active_port"] == 0, \
            "Cache Modbus server is disabled but still listens on " \
            f"{data['cache_modbus_active_port']}"

    assert "cache_value_timeout_s" in data, "Field cache_value_timeout_s is missing"
    assert isinstance(data["cache_value_timeout_s"], int) and data["cache_value_timeout_s"] >= 0, \
        "Field cache_value_timeout_s must be a non-negative integer"

    assert "ethernet" in data, "Section ethernet is missing"
    eth = data["ethernet"]

    ethernet_fields = [
        "con_eth", "ip", "mask", "gw", "mac"
    ]
    for field in ethernet_fields:
        assert field in eth, f"Field {field} is missing"

    assert isinstance(eth["con_eth"], bool), "Field con_eth has incorrect type"

    assert "wifi" in data, "Section wifi is missing"
    wifi = data["wifi"]

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
    assert wifi["mode"] in ["ap", "sta", "apsta", "none"], \
        f"Field mode has unexpected value: {wifi['mode']}"

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


def test_info_format_validation(api):
    """Test GET /info response field format validation: firmware, git_info, MAC addresses"""
    response = api.get_info()
    assert response.status_code == 200
    data = response.json()

    firmware = data.get("firmware", "")
    assert re.match(r'^\d+\.\d+\.\d+$', firmware), \
        f"firmware field does not match \\d+\\.\\d+\\.\\d+: '{firmware}'"
    print(f"✓ firmware format valid: {firmware}")

    git_info = data.get("git_info", "")
    if git_info != "qemu_build":
        assert re.match(r'^[0-9a-f]{7}_[A-Za-z0-9_/.-]+$', git_info), \
            f"git_info field does not match expected pattern: '{git_info}'"
    print(f"✓ git_info format valid: {git_info}")

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

    serial_num = data.get("serial_num", 0)
    assert serial_num >= 1, f"serial_num must be >= 1, got {serial_num}"
    print(f"✓ serial_num valid: {serial_num}")
