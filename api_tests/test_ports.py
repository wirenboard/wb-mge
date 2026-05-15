"""Port modes, sniffer status, and WB test endpoint tests"""

import time
import pytest


@pytest.mark.order(27)
def test_wb_test(api):
    """Test GET /wb_test + POST /wb_test"""
    response = api.get_wb_test()
    assert response.status_code == 200, \
        f"GET /wb_test expected 200, got {response.status_code}"
    data = response.json()
    assert "clock_out" in data, "Field 'clock_out' is missing from /wb_test response"
    assert isinstance(data["clock_out"], bool), "Field 'clock_out' must be a boolean"
    print(f"✓ GET /wb_test works, clock_out={data['clock_out']}")

    original_clock_out = data["clock_out"]

    try:
        response = api.set_wb_test(True)
        assert response.status_code == 200, \
            f"POST /wb_test clock_out=true expected 200, got {response.status_code}"
        result = response.json()
        assert result.get("success") == True, f"POST /wb_test expected success=true, got {result}"
        assert result.get("clock_out") == True, f"POST /wb_test expected clock_out=true in response, got {result}"
        print("✓ POST /wb_test {clock_out: true} accepted")

        response = api.get_wb_test()
        assert response.status_code == 200
        assert response.json()["clock_out"] == True, "Read-back after clock_out=true failed"
        print("✓ Read-back after clock_out=true correct")

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

        response = api.session.post(f"{api.base_url}/wb_test", json={"clock_out": "true"}, timeout=10)
        if response.status_code == 200:
            rb = api.get_wb_test().json()
            assert isinstance(rb["clock_out"], bool), \
                "After invalid type POST, clock_out must still be a boolean"
        else:
            assert response.status_code == 400, \
                f"POST /wb_test with string clock_out expected 400, got {response.status_code}"
        print("✓ POST /wb_test with invalid type handled")

        response = api.session.post(f"{api.base_url}/wb_test", json={}, timeout=10)
        assert response.status_code in [200, 400], \
            f"POST /wb_test with empty body got unexpected status {response.status_code}"
        print("✓ POST /wb_test with missing field handled")

    finally:
        api.set_wb_test(original_clock_out)
        print(f"✓ clock_out restored to {original_clock_out}")


@pytest.mark.order(28)
def test_sniffer_status(api):
    """Test GET /sniffer/status and verify it reflects port mode changes"""
    info_response = api.get_info()
    assert info_response.status_code == 200
    info_data = info_response.json()
    original_port_1_mode = info_data.get("rs485_1", {}).get("port_mode", "tcp_bridge")
    print(f"  Port 1 original mode: {original_port_1_mode}")

    try:
        response = api.get_sniffer_status()
        assert response.status_code == 200, \
            f"GET /sniffer/status expected 200, got {response.status_code}"
        status = response.json()
        assert "port_1" in status, "Field 'port_1' is missing from /sniffer/status response"
        assert "port_2" in status, "Field 'port_2' is missing from /sniffer/status response"
        assert isinstance(status["port_1"], bool), "Field 'port_1' must be a boolean"
        assert isinstance(status["port_2"], bool), "Field 'port_2' must be a boolean"
        print(f"✓ GET /sniffer/status works, port_1={status['port_1']}, port_2={status['port_2']}")

        response = api.set_port_mode(1, "sniffer")
        assert response.status_code == 200, \
            f"POST /ports/1/mode sniffer expected 200, got {response.status_code}"

        time.sleep(0.5)
        response = api.get_sniffer_status()
        assert response.status_code == 200
        status = response.json()
        assert status["port_1"] == True, \
            f"After switching to sniffer mode, port_1 must be true, got {status['port_1']}"
        print("✓ After sniffer mode: port_1=true")

        response = api.set_port_mode(1, "disabled")
        assert response.status_code == 200

        time.sleep(0.5)
        response = api.get_sniffer_status()
        assert response.status_code == 200
        status = response.json()
        assert status["port_1"] == False, \
            f"After switching to disabled mode, port_1 must be false, got {status['port_1']}"
        print("✓ After disabled mode: port_1=false")

    finally:
        try:
            api.set_port_mode(1, original_port_1_mode)
            print(f"✓ Port 1 mode restored to {original_port_1_mode}")
        except Exception as exc:
            raise AssertionError(f"Failed to restore port 1 mode: {exc}")


@pytest.mark.order(29)
def test_port_modes(api):
    """Test POST /ports/{n}/mode — all modes, both ports"""
    info_response = api.get_info()
    assert info_response.status_code == 200
    info_data = info_response.json()
    original_port_1_mode = info_data.get("rs485_1", {}).get("port_mode", "tcp_bridge")
    original_port_2_mode = info_data.get("rs485_2", {}).get("port_mode", "tcp_bridge")
    print(f"  Original modes: port_1={original_port_1_mode}, port_2={original_port_2_mode}")

    try:
        for mode in ["disabled", "tcp_bridge", "sniffer", "cache_bus"]:
            response = api.set_port_mode(1, mode)
            assert response.status_code == 200, \
                f"POST /ports/1/mode {mode} expected 200, got {response.status_code}"
            result = response.json()
            assert result.get("mode") == mode, \
                f"POST /ports/1/mode {mode}: response mode mismatch, got {result}"

            info_resp = api.get_info()
            assert info_resp.status_code == 200
            actual_mode = info_resp.json().get("rs485_1", {}).get("port_mode")
            assert actual_mode == mode, \
                f"After setting mode={mode}, GET /info shows rs485_1.port_mode={actual_mode}"
            print(f"✓ Port 1 mode '{mode}' set and verified via /info")

        for mode in ["cache_bus", "disabled"]:
            response = api.set_port_mode(2, mode)
            assert response.status_code == 200, \
                f"POST /ports/2/mode {mode} expected 200, got {response.status_code}"
            result = response.json()
            assert result.get("mode") == mode, \
                f"POST /ports/2/mode {mode}: response mode mismatch, got {result}"
            print(f"✓ Port 2 mode '{mode}' set")

        response = api.set_port_mode(1, "invalid_mode")
        assert response.status_code == 400, \
            f"POST /ports/1/mode 'invalid_mode' expected 400, got {response.status_code}"
        print("✓ Invalid mode value rejected with 400")

        response = api.session.post(
            f"{api.base_url}/ports/3/mode",
            json={"mode": "tcp_bridge"},
            timeout=10
        )
        assert response.status_code in [400, 404], \
            f"POST /ports/3/mode (non-existent port) expected 400 or 404, got {response.status_code}"
        print("✓ Non-existent port 3 rejected")

    finally:
        restore_errors = []
        for port_num, mode in [(1, original_port_1_mode), (2, original_port_2_mode)]:
            try:
                api.reconnect()
                api.auth()
                api.set_port_mode(port_num, mode)
                print(f"✓ Port {port_num} mode restored to {mode}")
            except Exception as exc:
                msg = f"Failed to restore port {port_num} mode to {mode}: {exc}"
                restore_errors.append(msg)
        if restore_errors:
            raise AssertionError("Port mode restore failed: " + "; ".join(restore_errors))
