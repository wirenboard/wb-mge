"""WiFi scanner and AP clients tests"""

import re
import time
import pytest


@pytest.mark.order(11)
def test_wifi_scanner(api):
    """WiFi scanner test"""
    response = api.start_wifi_scan()
    assert response.status_code == 200
    data = response.json()
    assert isinstance(data["success"], bool)
    print("✓ WiFi scan start works")

    response = api.get_wifi_scan_results()
    assert response.status_code == 200
    data = response.json()

    assert "scan_in_progress" in data
    assert "scan_completed" in data
    assert isinstance(data["scan_in_progress"], bool)
    assert isinstance(data["scan_completed"], bool)
    assert data["scan_in_progress"] == True or data["scan_completed"] == True, \
        "scan should be either in progress or completed immediately after start"
    if data.get("scan_completed") == True:
        assert not data.get("error"), \
            f"scan completed with an error immediately after start: {data.get('error')}"

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


@pytest.mark.order(12)
def test_wifi_scan_edge_cases(api):
    """Test WiFi scan state machine edge cases"""
    response = api.get_wifi_scan_results()
    assert response.status_code == 200, \
        f"GET /wifi_scan/results without prior start expected 200, got {response.status_code}"
    data = response.json()
    assert "scan_in_progress" in data, "Field 'scan_in_progress' missing"
    assert "scan_completed" in data, "Field 'scan_completed' missing"
    assert isinstance(data["scan_in_progress"], bool), "'scan_in_progress' must be bool"
    assert isinstance(data["scan_completed"], bool), "'scan_completed' must be bool"
    print(f"✓ GET /wifi_scan/results before start: scan_in_progress={data['scan_in_progress']}, "
          f"scan_completed={data['scan_completed']}")

    response = api.start_wifi_scan()
    assert response.status_code == 200
    assert response.json().get("success") == True
    print("✓ First WiFi scan start successful")

    response2 = api.start_wifi_scan()
    assert response2.status_code == 200, \
        f"Second POST /wifi_scan/start expected 200, got {response2.status_code}"
    data2 = response2.json()
    assert "success" in data2, "Second start response must contain 'success'"
    if not data2["success"]:
        assert "error" in data2, \
            "If second start fails (scan already running), response must contain 'error' field"
        print(f"✓ Double-start correctly rejected: {data2.get('error')}")
    else:
        print("✓ Double-start accepted (scan completed between calls — restart OK)")

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


@pytest.mark.order(13)
def test_wifi_scan_network_fields(api):
    """Test WiFi scan result field validation: bssid, channel, rssi range"""
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
        assert "bssid" in network, f"Network[{i}] missing 'bssid' field"
        assert mac_pattern.match(network["bssid"]), \
            f"Network[{i}] bssid '{network['bssid']}' does not match MAC format"

        assert "channel" in network, f"Network[{i}] missing 'channel' field"
        assert isinstance(network["channel"], int), f"Network[{i}] channel must be int"
        assert 1 <= network["channel"] <= 14, \
            f"Network[{i}] channel {network['channel']} out of range 1-14"

        assert "rssi" in network, f"Network[{i}] missing 'rssi' field"
        assert -128 <= network["rssi"] <= 0, \
            f"Network[{i}] rssi {network['rssi']} out of range -128..0"

    print(f"✓ WiFi scan network fields validated for {len(networks)} network(s)")


@pytest.mark.order(14)
def test_ap_clients(api):
    """AP clients list test"""
    response = api.get_ap_clients()
    assert response.status_code == 200
    clients = response.json()

    assert isinstance(clients, list)
    for client in clients:
        assert "mac" in client
        if "rssi" in client:
            assert -100 <= client["rssi"] <= 0

    print("✓ AP clients list retrieval works")
