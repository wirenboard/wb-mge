"""Port modes, sniffer status, and WB test endpoint tests"""

import json
import time

import pytest
import requests
from urllib.parse import urlparse

from sniffer_helpers import _ws_connect


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


# ---------------------------------------------------------------------------
# Group 3: /sniffer/status endpoint
# ---------------------------------------------------------------------------

@pytest.mark.order(45)
def test_sniffer_status_response_shape_and_content_type(api):
    """GET /sniffer/status must return 200 with application/json and keys port_1/port_2."""
    # Ensure both ports are not in sniffer mode before testing
    info = api.get_info()
    assert info.status_code == 200
    info_data = info.json()
    port1_mode = info_data.get("rs485_1", {}).get("port_mode", "tcp_bridge")
    port2_mode = info_data.get("rs485_2", {}).get("port_mode", "tcp_bridge")

    restored_port1 = False
    restored_port2 = False

    try:
        if port1_mode == "sniffer":
            api.set_port_mode(1, "tcp_bridge")
            restored_port1 = True
        if port2_mode == "sniffer":
            api.set_port_mode(2, "tcp_bridge")
            restored_port2 = True

        response = api.get_sniffer_status()
        assert response.status_code == 200, (
            f"Expected HTTP 200, got {response.status_code}"
        )

        content_type = response.headers.get("Content-Type", "")
        assert "application/json" in content_type, (
            f"Expected Content-Type to contain 'application/json', got {content_type!r}"
        )

        body = response.json()
        assert "port_1" in body, f"Key 'port_1' missing from response: {body}"
        assert "port_2" in body, f"Key 'port_2' missing from response: {body}"
        # Keys must not use zero-based indexing
        assert "port_0" not in body, f"Unexpected zero-based key 'port_0' in response: {body}"
        assert "port_3" not in body, f"Unexpected key 'port_3' in response: {body}"

        assert body["port_1"] is False, (
            f"Expected port_1==False (both ports non-sniffer), got {body['port_1']}"
        )
        assert body["port_2"] is False, (
            f"Expected port_2==False (both ports non-sniffer), got {body['port_2']}"
        )
        print("✓ /sniffer/status shape and content-type validated")

    finally:
        # Restore any modes we changed
        if restored_port1:
            r = api.set_port_mode(1, port1_mode)
            assert r.status_code == 200, f"Failed to restore port 1 mode: {r.status_code}"
        if restored_port2:
            r = api.set_port_mode(2, port2_mode)
            assert r.status_code == 200, f"Failed to restore port 2 mode: {r.status_code}"


@pytest.mark.order(46)
def test_sniffer_status_reflects_start_command(api):
    """/sniffer/status must report port_1==True after WS start command for port 1."""
    original_port_mode = None
    ws = None
    stop_ping = None

    try:
        info = api.get_info()
        assert info.status_code == 200
        original_port_mode = info.json().get("rs485_1", {}).get("port_mode", "tcp_bridge")

        r = api.set_port_mode(1, "sniffer")
        assert r.status_code == 200, f"Failed to set sniffer mode: {r.status_code}"
        time.sleep(0.5)

        ws, stop_ping, _ = _ws_connect(api, 1)
        time.sleep(0.5)

        status_resp = api.get_sniffer_status()
        assert status_resp.status_code == 200
        body = status_resp.json()
        assert body.get("port_1") is True, (
            f"Expected port_1==True after start, got {body.get('port_1')}"
        )
        assert body.get("port_2") is False, (
            f"Expected port_2==False, got {body.get('port_2')}"
        )
        print("✓ /sniffer/status reflects start command for port 1")

    finally:
        if stop_ping is not None:
            stop_ping.set()
        if ws is not None:
            try:
                ws.send(json.dumps({"cmd": "stop", "port": 1}))
            except Exception:
                pass
            try:
                ws.close()
            except Exception:
                pass
        if original_port_mode is not None:
            r = api.set_port_mode(1, original_port_mode)
            assert r.status_code == 200, f"Failed to restore port mode: {r.status_code}"


@pytest.mark.order(47)
def test_sniffer_status_reflects_stop_command(api):
    """/sniffer/status must report port_1==False after WS stop command."""
    original_port_mode = None
    ws = None
    stop_ping = None

    try:
        info = api.get_info()
        assert info.status_code == 200
        original_port_mode = info.json().get("rs485_1", {}).get("port_mode", "tcp_bridge")

        r = api.set_port_mode(1, "sniffer")
        assert r.status_code == 200, f"Failed to set sniffer mode: {r.status_code}"
        time.sleep(0.5)

        ws, stop_ping, _ = _ws_connect(api, 1)
        time.sleep(0.5)

        # Verify it is True first
        status_resp = api.get_sniffer_status()
        assert status_resp.status_code == 200
        body = status_resp.json()
        assert body.get("port_1") is True, (
            f"Precondition failed: expected port_1==True after start, got {body.get('port_1')}"
        )

        # Stop the sniffer
        ws.send(json.dumps({"cmd": "stop", "port": 1}))
        time.sleep(0.3)

        status_resp = api.get_sniffer_status()
        assert status_resp.status_code == 200
        body = status_resp.json()
        assert body.get("port_1") is False, (
            f"Expected port_1==False after stop, got {body.get('port_1')}"
        )
        print("✓ /sniffer/status reflects stop command for port 1")

    finally:
        if stop_ping is not None:
            stop_ping.set()
        if ws is not None:
            try:
                ws.send(json.dumps({"cmd": "stop", "port": 1}))
            except Exception:
                pass
            try:
                ws.close()
            except Exception:
                pass
        if original_port_mode is not None:
            r = api.set_port_mode(1, original_port_mode)
            assert r.status_code == 200, f"Failed to restore port mode: {r.status_code}"


@pytest.mark.order(48)
def test_sniffer_status_unauthenticated(api):
    """GET /sniffer/status without auth must return HTTP 401."""
    parsed = urlparse(api.base_url)
    base_url = f"http://{parsed.hostname}:{parsed.port or 80}"

    # Use a fresh session with no cookies
    unauth_session = requests.Session()
    response = unauth_session.get(f"{base_url}/sniffer/status", timeout=10)

    assert response.status_code == 401, (
        f"Expected HTTP 401 for unauthenticated request, got {response.status_code}"
    )
    print("✓ /sniffer/status returns 401 for unauthenticated requests")


@pytest.mark.order(49)
def test_sniffer_status_both_ports_independent(api):
    """Starting/stopping port 1 and port 2 independently must be reflected in status."""
    original_mode_1 = None
    original_mode_2 = None
    ws1 = None
    ws2 = None
    stop_ping1 = None
    stop_ping2 = None

    try:
        info = api.get_info()
        assert info.status_code == 200
        info_data = info.json()
        original_mode_1 = info_data.get("rs485_1", {}).get("port_mode", "tcp_bridge")
        original_mode_2 = info_data.get("rs485_2", {}).get("port_mode", "tcp_bridge")

        # Set port 1 to sniffer and start it
        r = api.set_port_mode(1, "sniffer")
        assert r.status_code == 200, f"Failed to set sniffer mode for port 1: {r.status_code}"
        time.sleep(0.3)

        ws1, stop_ping1, _ = _ws_connect(api, 1)
        time.sleep(0.5)

        body = api.get_sniffer_status().json()
        assert body.get("port_1") is True, f"Expected port_1==True, got {body}"
        assert body.get("port_2") is False, f"Expected port_2==False, got {body}"

        r2 = api.set_port_mode(2, "sniffer")
        assert r2.status_code == 200, \
            f"set_port_mode(2, 'sniffer') expected 200, got {r2.status_code}"
        time.sleep(0.3)

        ws2, stop_ping2, _ = _ws_connect(api, 2)
        time.sleep(0.5)

        body = api.get_sniffer_status().json()
        assert body.get("port_1") is True, f"Expected port_1==True, got {body}"
        assert body.get("port_2") is True, f"Expected port_2==True, got {body}"

        # Stop port 1
        if ws1 is not None:
            ws1.send(json.dumps({"cmd": "stop", "port": 1}))
            time.sleep(0.3)

            body = api.get_sniffer_status().json()
            assert body.get("port_1") is False, (
                f"Expected port_1==False after stop, got {body}"
            )
        print("✓ port 1 and port 2 sniffer states are independent")

    finally:
        if stop_ping1 is not None:
            stop_ping1.set()
        if stop_ping2 is not None:
            stop_ping2.set()
        if ws1 is not None:
            try:
                ws1.send(json.dumps({"cmd": "stop", "port": 1}))
            except Exception:
                pass
            try:
                ws1.close()
            except Exception:
                pass
        if ws2 is not None:
            try:
                ws2.send(json.dumps({"cmd": "stop", "port": 2}))
            except Exception:
                pass
            try:
                ws2.close()
            except Exception:
                pass
        if original_mode_1 is not None:
            r = api.set_port_mode(1, original_mode_1)
            assert r.status_code == 200, f"Failed to restore port 1 mode: {r.status_code}"
        if original_mode_2 is not None:
            r = api.set_port_mode(2, original_mode_2)
            assert r.status_code == 200, f"Failed to restore port 2 mode: {r.status_code}"


@pytest.mark.order(50)
def test_sniffer_status_post_method_rejected(api):
    """POST /sniffer/status must return HTTP 405 (method not allowed)."""
    response = api.session.post(f"{api.base_url}/sniffer/status", timeout=10)
    assert response.status_code == 405, (
        f"Expected HTTP 405 for POST /sniffer/status, got {response.status_code}"
    )
    print("✓ POST /sniffer/status returns 405")
