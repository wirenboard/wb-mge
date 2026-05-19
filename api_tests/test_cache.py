"""Cache endpoints, JSON fields, CSV headers, server toggle, and multimaster tests"""

import socket
import threading
import time
import pytest
from urllib.parse import urlparse

from modbus_helpers import (
    parse_csv, worker, check_simultaneous_connection, run_staleness_test
)


@pytest.mark.order(20)
def test_cache_endpoints(api):
    """Test cache HTTP endpoints: /cache/status, /cache/csv, /cache/json"""
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

    response = api.get_cache_csv()
    assert response.status_code == 200, \
        f"GET /cache/csv expected 200, got {response.status_code}"

    content_type = response.headers.get("content-type", "")
    assert "text/csv" in content_type.lower() or "text/plain" in content_type.lower(), \
        f"GET /cache/csv expected text/csv content type, got: {content_type}"

    csv_text = response.text
    assert len(csv_text) > 0, "GET /cache/csv response body must not be empty"

    first_line = csv_text.split("\n")[0].strip()
    assert first_line == "slave_id,type,address,value,age_s", \
        f"CSV header mismatch: expected 'slave_id,type,address,value,age_s', got '{first_line}'"

    print("✓ /cache/csv works, header is correct")

    response = api.get_cache_json()
    assert response.status_code == 200, \
        f"GET /cache/json expected 200, got {response.status_code}"

    json_data = response.json()
    assert "d" in json_data, "Field 'd' is missing from /cache/json response"
    assert isinstance(json_data["d"], list), "Field 'd' in /cache/json must be an array"

    print(f"✓ /cache/json works, entries count: {len(json_data['d'])}")


@pytest.mark.order(21)
def test_cache_json_fields(api):
    """Test /cache/json per-entry field validation and consistency with /cache/status"""
    status_resp = api.get_cache_status()
    assert status_resp.status_code == 200
    status = status_resp.json()
    entries_count = status.get("entries", 0)

    json_resp = api.get_cache_json()
    assert json_resp.status_code == 200
    json_data = json_resp.json()
    assert "d" in json_data, "Field 'd' missing from /cache/json"
    assert isinstance(json_data["d"], list), "Field 'd' must be an array"

    entries = json_data["d"]

    delta = abs(len(entries) - entries_count)
    assert delta <= 1, \
        f"entries count mismatch: /cache/status says {entries_count}, /cache/json has {len(entries)} (delta={delta})"
    print(f"✓ Entry count consistent: /cache/status={entries_count}, /cache/json={len(entries)}")

    assert entries, "Cache is empty — expected at least one entry in /cache/json"

    valid_types = {"h", "i", "c", "d"}
    for i, entry in enumerate(entries):
        assert "s" in entry, f"Entry[{i}] missing field 's'"
        assert isinstance(entry["s"], int), f"Entry[{i}]['s'] must be int"
        assert 1 <= entry["s"] <= 247, f"Entry[{i}]['s']={entry['s']} out of range 1-247"

        assert "t" in entry, f"Entry[{i}] missing field 't'"
        assert entry["t"] in valid_types, \
            f"Entry[{i}]['t']='{entry['t']}' not in valid types {valid_types}"

        assert "a" in entry, f"Entry[{i}] missing field 'a'"
        assert isinstance(entry["a"], int), f"Entry[{i}]['a'] must be int"
        assert 0 <= entry["a"] <= 65535, f"Entry[{i}]['a']={entry['a']} out of range 0-65535"

        assert "v" in entry, f"Entry[{i}] missing field 'v'"
        assert isinstance(entry["v"], int), f"Entry[{i}]['v'] must be int"
        assert 0 <= entry["v"] <= 65535, f"Entry[{i}]['v']={entry['v']} out of range 0-65535"

        assert "age" in entry, f"Entry[{i}] missing field 'age'"
        assert isinstance(entry["age"], int), f"Entry[{i}]['age'] must be int"
        assert 0 <= entry["age"] <= 65535, f"Entry[{i}]['age']={entry['age']} out of range 0-65535"

    print(f"✓ All {len(entries)} cache entries have valid fields")


@pytest.mark.order(22)
def test_cache_csv_headers(api):
    """Test /cache/csv Content-Disposition header"""
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


@pytest.mark.order(23)
def test_cache_server_enabled_toggle(api):
    """Test cache_modbus_server_enabled setting toggle and consistency"""
    response = api.get_settings()
    assert response.status_code == 200
    original_enabled = response.json().get("cache_modbus_server_enabled", True)

    try:
        response = api.update_settings({"cache_modbus_server_enabled": False})
        assert response.status_code == 200
        assert response.json().get("success") == True

        response = api.get_settings()
        assert response.status_code == 200
        assert response.json()["cache_modbus_server_enabled"] == False, \
            "cache_modbus_server_enabled=false not reflected in GET /settings"

        response = api.get_info()
        assert response.status_code == 200
        assert response.json()["cache_modbus_server_enabled"] == False, \
            "cache_modbus_server_enabled=false not reflected in GET /info"
        print("✓ cache_modbus_server_enabled=false visible in both /settings and /info")

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
        try:
            api.update_settings({"cache_modbus_server_enabled": original_enabled})
            print(f"✓ cache_modbus_server_enabled restored to {original_enabled}")
        except Exception as exc:
            raise AssertionError(f"Failed to restore cache_modbus_server_enabled: {exc}")


@pytest.mark.order(24)
def test_cache_value_timeout_setting(api):
    """Test cache_value_timeout_s setting toggle and consistency"""
    response = api.get_settings()
    assert response.status_code == 200
    original_value_timeout = response.json().get("cache_value_timeout_s", 60)

    try:
        # Set timeout to 30 seconds
        response = api.update_settings({"cache_value_timeout_s": 30})
        assert response.status_code == 200
        assert response.json().get("success") == True

        response = api.get_settings()
        assert response.status_code == 200
        assert response.json()["cache_value_timeout_s"] == 30, \
            "cache_value_timeout_s=30 not reflected in GET /settings"

        response = api.get_info()
        assert response.status_code == 200
        assert response.json()["cache_value_timeout_s"] == 30, \
            "cache_value_timeout_s=30 not reflected in GET /info"
        print("✓ cache_value_timeout_s=30 visible in both /settings and /info")

        # Set timeout to 0 (disabled — always serve cached values)
        response = api.update_settings({"cache_value_timeout_s": 0})
        assert response.status_code == 200
        assert response.json().get("success") == True

        response = api.get_settings()
        assert response.status_code == 200
        assert response.json()["cache_value_timeout_s"] == 0, \
            "cache_value_timeout_s=0 not reflected in GET /settings"

        response = api.get_info()
        assert response.status_code == 200
        assert response.json()["cache_value_timeout_s"] == 0, \
            "cache_value_timeout_s=0 not reflected in GET /info"
        print("✓ cache_value_timeout_s=0 visible in both /settings and /info")

    finally:
        try:
            api.update_settings({"cache_value_timeout_s": original_value_timeout})
            print(f"✓ cache_value_timeout_s restored to {original_value_timeout}")
        except Exception as exc:
            raise AssertionError(f"Failed to restore cache_value_timeout_s: {exc}")


@pytest.mark.timeout(120)
@pytest.mark.order(30)
def test_cache_multimaster(api):
    """Test cache Modbus TCP multi-master server"""
    original_port_mode = None
    original_modbus_port = None
    QEMU_MODBUS_PORT = 50504

    try:
        info_response = api.get_info()
        assert info_response.status_code == 200, \
            f"GET /info expected 200, got {info_response.status_code}"
        info_data = info_response.json()
        original_port_mode = info_data.get("rs485_1", {}).get("port_mode", "tcp_bridge")
        original_modbus_port = info_data.get("cache_modbus_port", 504)
        print(f"  Port 1 current mode: {original_port_mode}")
        print(f"  Original cache_modbus_port: {original_modbus_port}")

        resp = api.update_settings({"cache_modbus_port": QEMU_MODBUS_PORT})
        assert resp.status_code == 200, \
            f"Failed to set cache_modbus_port to {QEMU_MODBUS_PORT}: {resp.status_code}"
        time.sleep(2)
        print(f"✓ cache_modbus_port changed to {QEMU_MODBUS_PORT}")

        response = api.set_port_mode(1, "cache_bus")
        assert response.status_code == 200, \
            f"POST /ports/1/mode expected 200, got {response.status_code}"
        print("✓ Port 1 switched to cache_bus mode")

        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            time.sleep(1)
            status_resp = api.get_cache_status()
            if status_resp.status_code == 200:
                status = status_resp.json()
                if status.get("entries", 0) > 0:
                    break

        response = api.get_cache_status()
        assert response.status_code == 200, \
            f"GET /cache/status expected 200, got {response.status_code}"
        status = response.json()

        assert status.get("enabled"), "Cache not enabled after switching port to cache_bus"
        assert status.get("entries", 0) > 0, "Cache did not populate within 30s"

        info_response = api.get_info()
        assert info_response.status_code == 200, \
            f"GET /info expected 200, got {info_response.status_code}"
        info_data = info_response.json()
        modbus_port = info_data.get("cache_modbus_port", 504)

        parsed = urlparse(api.base_url)
        host = parsed.hostname

        print(f"✓ Cache server enabled, Modbus TCP port: {modbus_port}, host: {host}")

        response = api.get_cache_csv()
        assert response.status_code == 200, \
            f"GET /cache/csv expected 200, got {response.status_code}"

        raw_csv = response.text
        register_map = parse_csv(raw_csv)

        print(f"✓ Register map loaded: {len(register_map)} entries")

        assert register_map, "Register map CSV is empty — cache reports entries but CSV has none"

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

        still_alive = [t for t in threads if t.is_alive()]
        assert not still_alive, \
            f"{len(still_alive)} thread(s) did not finish within 30 seconds (deadlock?)"

        conn_ok, conn_msg = check_simultaneous_connection(results, num_threads)
        assert conn_ok, f"Connectivity check failed: {conn_msg}"
        print(f"✓ Connectivity: {conn_msg}")

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

        stale_ok, stale_lines = run_staleness_test(host, modbus_port, api, register_map)
        for line in stale_lines:
            print(f"  {line}")

        assert stale_ok, "Staleness test failed — see lines above for details"
        print("✓ Staleness test passed")

    finally:
        restore_errors = []
        if original_port_mode is not None:
            try:
                api.set_port_mode(1, original_port_mode)
                print(f"✓ Port 1 mode restored to {original_port_mode}")
            except Exception as exc:
                restore_errors.append(f"Failed to restore port 1 mode: {exc}")

        if original_modbus_port is not None:
            try:
                api.update_settings({"cache_modbus_port": original_modbus_port})
                print(f"✓ cache_modbus_port restored to {original_modbus_port}")
            except Exception as exc:
                restore_errors.append(f"Failed to restore cache_modbus_port: {exc}")

        time.sleep(2)

        if restore_errors:
            raise AssertionError("; ".join(restore_errors))
