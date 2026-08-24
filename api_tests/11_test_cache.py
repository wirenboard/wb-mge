"""Cache endpoints, JSON fields, CSV headers, server toggle, and multimaster tests"""

import socket
import threading
import time
import pytest
from urllib.parse import urlparse

import qemu_ports

from modbus_helpers import (
    parse_csv, worker, check_simultaneous_connection, run_staleness_test
)
from packet_injector import PacketInjector


@pytest.fixture(scope="module", autouse=True)
def _baseline(api):
    # Snapshot the per-port cache overlays before arming our own. The overlay is persisted to
    # NVS and, once on, costs a 32 KB cache pool plus a sniffer on the port
    # (SNIFF_REASON_CACHE) — state this module must not hand to the next one.
    resp = api.get_info()
    assert resp.status_code == 200, f"_baseline: get_info failed: {resp.status_code} {resp.text}"
    info = resp.json()
    original_overlays = {
        port: bool(info.get(f"rs485_{port}", {}).get("cache_enabled", False))
        for port in (1, 2)
    }

    resp = api.update_settings({
        "cache_modbus_server_enabled": True,   # tests expect the server to be enabled
        # Distinct from the RS-485 bridge gateway ports (502/503): the firmware now
        # rejects a cache_modbus_port that collides with a bridge port, and sharing one
        # leaves the cache server and the gateway fighting over the same TCP port across
        # a long no-reboot run. 504 is the cache default; the multimaster test changes it to
        # the forwarded guest port (qemu_ports.CACHE_MODBUS_GUEST_PORT).
        "cache_modbus_port": 504,
        "cache_value_timeout_s": 60,           # large enough so that entries do not expire
    })
    assert resp.status_code == 200, f"_baseline: update_settings failed: {resp.status_code} {resp.text}"
    resp = api.set_port_mode(1, "tcp_bridge")  # deterministic start; multimaster test will switch to passive + enable the cache overlay
    assert resp.status_code == 200, f"_baseline: set_port_mode(1, tcp_bridge) failed: {resp.status_code} {resp.text}"

    # The cache POOL is gated by the per-port cache overlay, NOT by
    # cache_modbus_server_enabled (that one only governs the Modbus TCP server).
    # Without an overlay on some port cache_multimaster_is_enabled() stays false and
    # GET /cache/csv answers 409 by design. Tests below assert the cache-active
    # contract, so arm the overlay here; test_cache_csv_conflict_when_disabled turns
    # it off explicitly to cover the other half. The overlay is orthogonal to the
    # transport mode, so it coexists with tcp_bridge above.
    resp = api.set_port_cache(1, True)
    assert resp.status_code == 200, f"_baseline: set_port_cache(1, True) failed: {resp.status_code} {resp.text}"

    yield

    # Undo the overlay this fixture armed. It used to be cleared only as a side effect of the
    # finally blocks in test_cache_multimaster / test_cache_json_fields, so running a subset
    # (-k test_cache_endpoints), or having those two fail or skip, left the overlay on and
    # leaked the cache pool plus the port-1 sniffer into every module that ran afterwards.
    for port, was_enabled in original_overlays.items():
        resp = api.set_port_cache(port, was_enabled)
        assert resp.status_code == 200, \
            f"_baseline teardown: set_port_cache({port}, {was_enabled}) failed: {resp.status_code} {resp.text}"
    print(f"✓ _baseline: cache overlays restored to {original_overlays}")


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
    assert status["enabled"] is True, \
        "_baseline arms the port-1 cache overlay, so the cache pool must report enabled"
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


def test_cache_csv_conflict_when_disabled(api):
    """GET /cache/csv must answer 409 when the cache pool is off.

    The pool is enabled iff at least one port carries the cache overlay, so dropping
    every overlay disables it. Handing the user a .csv file containing nothing but a
    header row is worse than an explicit error, hence 409 + text/plain.

    /cache/status and /cache/json stay 200: the UI polls both, and "cache is off" is a
    normal state there ({"d":[]} is its correct representation), not a failed request.
    """
    response = api.get_info()
    assert response.status_code == 200, \
        f"GET /info expected 200, got {response.status_code}"
    info = response.json()
    original = {
        port: bool(info.get(f"rs485_{port}", {}).get("cache_enabled", False))
        for port in (1, 2)
    }

    try:
        for port in (1, 2):
            response = api.set_port_cache(port, False)
            assert response.status_code == 200, \
                f"POST /ports/{port}/cache expected 200, got {response.status_code}"

        response = api.get_cache_status()
        assert response.status_code == 200, \
            f"GET /cache/status expected 200, got {response.status_code}"
        assert response.json()["enabled"] is False, \
            "cache must report disabled once no port carries the overlay"

        response = api.get_cache_csv()
        assert response.status_code == 409, \
            f"GET /cache/csv with the cache off expected 409, got {response.status_code}"

        content_type = response.headers.get("content-type", "")
        assert "text/plain" in content_type.lower(), \
            f"the 409 body is a plain-text message, not a CSV file; got: {content_type}"
        assert "attachment" not in response.headers.get("Content-Disposition", "").lower(), \
            "a 409 must not offer the browser a file to download"

        print("✓ /cache/csv returns 409 while the cache is disabled")

        # The polled endpoints must stay usable while the cache is off.
        response = api.get_cache_json()
        assert response.status_code == 200, \
            f"GET /cache/json with the cache off expected 200, got {response.status_code}"
        assert response.json().get("d") == [], \
            "with the cache off /cache/json must return an empty entry list, not an error"

        print("✓ /cache/json stays 200 with an empty list while the cache is disabled")

    finally:
        for port, was_enabled in original.items():
            try:
                api.set_port_cache(port, was_enabled)
            except Exception as exc:
                raise AssertionError(f"Failed to restore cache overlay on port {port}: {exc}")
        print(f"✓ Cache overlays restored to {original}")


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
def test_cache_multimaster(api):
    """Test cache Modbus TCP multi-master server"""
    original_port_mode = None
    original_modbus_port = None
    # Firmware cache_modbus_port SETTING = fixed guest port (what the hostfwd forwards to);
    # the TCP clients below connect to the dynamic HOST port that forwards to it.
    QEMU_MODBUS_PORT = qemu_ports.CACHE_MODBUS_GUEST_PORT

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

        response = api.set_port_mode(1, "passive")
        assert response.status_code == 200, \
            f"POST /ports/1/mode expected 200, got {response.status_code}"
        response = api.set_port_cache(1, True)
        assert response.status_code == 200, \
            f"POST /ports/1/cache expected 200, got {response.status_code}"
        print("✓ Port 1 set to passive transport with the cache overlay enabled")

        # The firmware no longer fabricates Modbus traffic on its own — pytest
        # drives the bus via POST /qemu/inject through PacketInjector so the
        # cache has something to record.
        with PacketInjector(port=1):
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

            assert status.get("enabled"), "Cache not enabled after enabling the cache overlay on port 1"
            assert status.get("entries", 0) > 0, "Cache did not populate within 30s"

            info_response = api.get_info()
            assert info_response.status_code == 200, \
                f"GET /info expected 200, got {info_response.status_code}"
            info_data = info_response.json()
            guest_modbus_port = info_data.get("cache_modbus_port", 504)
            # The read-back is a REAL check, not decoration: the connection below goes to
            # the host end of the hostfwd, which reaches the guest port the rule was built
            # for whether or not the firmware actually moved its server there. Without this
            # assert, a settings write that was silently ignored would leave the test
            # connecting to a stale-but-open server and passing.
            assert guest_modbus_port == QEMU_MODBUS_PORT, (
                f"firmware reports cache_modbus_port={guest_modbus_port}, but the "
                f"hostfwd for {qemu_ports.CACHE_MODBUS_HOST_PORT} forwards to guest "
                f"{QEMU_MODBUS_PORT} — the earlier settings write did not take effect"
            )

            parsed = urlparse(api.base_url)
            host = parsed.hostname

            # Connect to the dynamic HOST port that forwards to the firmware's guest
            # cache_modbus_port (asserted equal just above); the guest value is only
            # reachable from the host through this forward.
            modbus_port = qemu_ports.CACHE_MODBUS_HOST_PORT
            print(f"✓ Cache server enabled, firmware guest port: {guest_modbus_port}, "
                  f"host connect port: {modbus_port}, host: {host}")

            response = api.get_cache_csv()
            assert response.status_code == 200, \
                f"GET /cache/csv expected 200, got {response.status_code}"

            raw_csv = response.text
            register_map = parse_csv(raw_csv)

            print(f"✓ Register map loaded: {len(register_map)} entries")

            assert register_map, "Register map CSV is empty — cache reports entries but CSV has none"

            # Log which FC types are covered so gaps are visible in CI output
            covered_fc_types = {reg_type for (_sid, reg_type, _addr) in register_map}
            all_fc_types = {"holding", "input", "coil", "discrete"}
            missing_fc_types = all_fc_types - covered_fc_types
            print(f"  FC types in cache: {sorted(covered_fc_types)}")
            if missing_fc_types:
                print(f"  [WARN] FC types not covered: {sorted(missing_fc_types)}")
            assert covered_fc_types == all_fc_types, (
                f"Cache register map missing FC types: {sorted(missing_fc_types)}. "
                f"PacketInjector must inject FC01/FC02/FC03/FC04 exchanges."
            )

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
        try:
            api.set_port_cache(1, False)
        except Exception as exc:
            restore_errors.append(f"Failed to disable cache overlay on port 1: {exc}")
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


@pytest.mark.timeout(120)
def test_cache_json_fields(api):
    """Test /cache/json per-entry field validation and consistency with /cache/status"""
    original_port_mode = None

    try:
        info_resp = api.get_info()
        assert info_resp.status_code == 200
        original_port_mode = info_resp.json().get("rs485_1", {}).get("port_mode", "tcp_bridge")

        r = api.set_port_mode(1, "passive")
        assert r.status_code == 200, f"Failed to set passive mode: {r.status_code}"
        r = api.set_port_cache(1, True)
        assert r.status_code == 200, f"Failed to enable cache overlay: {r.status_code}"

        # Drive Modbus traffic via the QEMU inject endpoint while we wait for
        # the cache to populate.  Without an active injector the cache stays
        # empty and the entry-shape assertions below have nothing to check.
        with PacketInjector(port=1):
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                time.sleep(1)
                st = api.get_cache_status()
                if st.status_code == 200 and st.json().get("entries", 0) > 0:
                    break

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

    finally:
        api.set_port_cache(1, False)
        if original_port_mode is not None:
            api.set_port_mode(1, original_port_mode)
            print(f"✓ Port 1 mode restored to {original_port_mode}")


def test_cache_bridge_port_collision_rejected(api):
    """The firmware must reject any settings change that would put the cache Modbus
    server and an RS-485 bridge gateway on the SAME TCP port, and must stay healthy
    after the rejection. Regression for the cache(502)/bridge(502) collision that, in a
    long no-reboot run, left one service unable to bind (listen() -> EADDRINUSE) and a
    stuck listen socket. Reboots used to mask it by resetting cache_modbus_port to 504.
    """
    try:
        # Known, non-colliding baseline: RS-485 port-1 bridge gateway on 1502, cache on 1504.
        r = api.update_settings({"rs485_1": {"bridge": {"port": 1502}}, "cache_modbus_port": 1504})
        assert r.status_code == 200 and r.json().get("success") is True, \
            f"baseline setup failed: {r.status_code} {r.text}"

        # 1) Setting cache_modbus_port to the bridge port must be rejected.
        r = api.update_settings({"cache_modbus_port": 1502})
        assert r.json().get("success") is False, \
            "cache_modbus_port equal to the RS-485 bridge port must be rejected"

        # 2) Setting the bridge port to the current cache port must be rejected.
        r = api.update_settings({"rs485_1": {"bridge": {"port": 1504}}})
        assert r.json().get("success") is False, \
            "RS-485 bridge port equal to cache_modbus_port must be rejected"

        # 3) Setting both to the same value in one request must be rejected.
        r = api.update_settings({"rs485_1": {"bridge": {"port": 1600}}, "cache_modbus_port": 1600})
        assert r.json().get("success") is False, \
            "cache_modbus_port and bridge port set to the same value in one request must be rejected"

        # 4) None of the rejected requests changed anything.
        s = api.get_settings().json()
        assert s.get("cache_modbus_port") == 1504, \
            f"cache_modbus_port must be unchanged after rejections, got {s.get('cache_modbus_port')}"
        assert s.get("rs485_1", {}).get("bridge", {}).get("port") == 1502, \
            f"bridge port must be unchanged after rejections, got {s.get('rs485_1', {}).get('bridge', {}).get('port')}"

        # 5) The device stays healthy after the rejected collisions.
        assert api.get_info().status_code == 200, "device must stay healthy after rejected collisions"

        # 6) A distinct, non-colliding change still works (positive control).
        r = api.update_settings({"cache_modbus_port": 1505})
        assert r.json().get("success") is True, "a non-colliding cache_modbus_port change must be accepted"

        print("✓ Cache/bridge port collisions rejected; non-colliding change accepted; device healthy")
    finally:
        # Restore defaults in a collision-free order (bridge first, then cache).
        api.update_settings({"rs485_1": {"bridge": {"port": 502}}})
        api.update_settings({"cache_modbus_port": 504})
