"""E2E tests for the cache Modbus TCP server: FC type separation, exception codes,
   bit packing, and cache-disabled guard. Tests CM-01 through CM-07."""

import qemu_ports
import socket
import struct
import time
from urllib.parse import urlparse

import pytest

from api_client import WBMGEAPI
from modbus_helpers import (
    make_mbap_request, recv_exactly, decode_fc01_fc02, decode_fc03_fc04,
    MIN_RESPONSE_SIZE
)
from packet_injector import (
    build_fc01_exchange, build_fc02_exchange, build_fc03_exchange, build_fc04_exchange,
    inject_bytes, open_uart_socket
)

# ---------------------------------------------------------------------------
# Module-level constants
# ---------------------------------------------------------------------------

# Host port this slot forwards to the cache Modbus TCP server (guest port 50504).
QEMU_CACHE_MODBUS_PORT = qemu_ports.QEMU_CACHE_MODBUS_PORT

# Timeout for opening TCP connections to the cache server.
CONNECT_TIMEOUT = 5.0


# ---------------------------------------------------------------------------
# Module-level helper
# ---------------------------------------------------------------------------

def _recv_one_response(sock: socket.socket) -> tuple:
    """Receive one complete Modbus TCP response from *sock*.

    *sock* must already have a timeout set so the test does not block forever.

    Returns (tid, fc, payload) where:
      tid     — transaction identifier echoed by the server (int)
      fc      — function code (int, 0x80 | original_fc on exception)
      payload — bytes after the MBAP+FC header (may be empty)
    """
    # Read the first MIN_RESPONSE_SIZE bytes: tid(2) + proto(2) + len(2) + uid(1) + fc(1) = 8
    header = recv_exactly(sock, MIN_RESPONSE_SIZE)
    tid, _proto, length, _uid, fc = struct.unpack(">HHHBB", header)

    # The MBAP length field counts uid + fc + data; uid and fc are already consumed.
    remaining = length - 2
    payload = recv_exactly(sock, remaining) if remaining > 0 else b""
    return tid, fc, payload


# ---------------------------------------------------------------------------
# Module-scoped baseline fixture (autouse)
# ---------------------------------------------------------------------------

@pytest.fixture(scope="module", autouse=True)
def _baseline(api: WBMGEAPI):
    """Set a deterministic initial baseline before any cache_server_e2e tests run.

    Enables the cache Modbus server and switches port 1 to tcp_bridge so the
    cache_server_e2e fixture always starts from a known clean state.
    """
    resp = api.update_settings({
        "cache_modbus_server_enabled": True,
        "cache_value_timeout_s": 60,
    })
    assert resp.status_code == 200, \
        f"_baseline: update_settings failed: {resp.status_code} {resp.text}"
    # Start in tcp_bridge so cache_server_e2e can switch to passive + cache overlay cleanly.
    resp = api.set_port_mode(1, "tcp_bridge")
    assert resp.status_code == 200, \
        f"_baseline: set_port_mode tcp_bridge failed: {resp.status_code} {resp.text}"


# ---------------------------------------------------------------------------
# Module-scoped fixture: set up server and populate cache
# ---------------------------------------------------------------------------

@pytest.fixture(scope="module")
def cache_server_e2e(api: WBMGEAPI):
    """Set up the cache Modbus TCP server on guest port 50504 and populate the cache.

    Steps:
      1. Skip guard: probe the cache host port; skip if not reachable.
      2. Save original rs485_1 port_mode and cache_modbus_port.
      3. Enable cache server, set its port to guest 50504, set timeout to 60 s.
      4. Sleep 1 s to let the server rebind.
      5. Set port 1 to passive transport and enable the cache overlay.
      6. Inject specific FC01/FC02/FC03/FC04 traffic via UART port 1.
      7. Poll up to 30 s for cache entries to appear.
      8. Yield (host, cache host port).
      9. Restore original settings in finally.

    Injected traffic (all via a single shared UART socket):
      FC01(slave=1, addr=100, count=1, value=1)  — coil  100 = ON
      FC01(slave=1, addr=101, count=1, value=0)  — coil  101 = OFF
      FC02(slave=1, addr=200, count=1, value=1)  — discrete 200 = 1
      FC03(slave=1, addr=10,  count=5, base=1000) — holding regs 10-14 = 1000-1004
      FC04(slave=1, addr=300, count=3, base=3000) — input regs  300-302 = 3000-3002
    """
    host = urlparse(api.base_url).hostname or "localhost"

    # Skip guard: verify the QEMU port is accessible.
    probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    probe.settimeout(CONNECT_TIMEOUT)
    try:
        result = probe.connect_ex((host, QEMU_CACHE_MODBUS_PORT))
    finally:
        probe.close()
    if result != 0:
        pytest.skip(
            f"Cache Modbus TCP port {QEMU_CACHE_MODBUS_PORT} not reachable on {host} "
            f"(connect_ex returned {result}) — skipping cache Modbus server E2E tests"
        )

    # Save original settings for teardown.
    info_resp = api.get_info()
    assert info_resp.status_code == 200, \
        f"GET /info failed before fixture setup: HTTP {info_resp.status_code}"
    info = info_resp.json()
    original_port_mode = info.get("rs485_1", {}).get("port_mode", "tcp_bridge")
    original_modbus_port = info.get("cache_modbus_port", 504)

    try:
        # Configure cache server settings.
        resp = api.update_settings({
            "cache_modbus_server_enabled": True,
            # GUEST port (fixed 50504) — what this slot's hostfwd forwards to; the host connects to
            # the dynamic QEMU_CACHE_MODBUS_PORT which reaches it through the forward.
            "cache_modbus_port": qemu_ports.CACHE_MODBUS_GUEST_PORT,
            "cache_value_timeout_s": 60,
        })
        assert resp.status_code == 200, \
            f"Failed to configure cache server settings: HTTP {resp.status_code}"

        # Wait for the server to rebind on the new port.
        time.sleep(1)

        # Open serial (passive) and enable the cache overlay — this calls
        # cache_multimaster_enable().
        resp = api.set_port_mode(1, "passive")
        assert resp.status_code == 200, \
            f"Failed to set port 1 to passive: HTTP {resp.status_code}"
        resp = api.set_port_cache(1, True)
        assert resp.status_code == 200, \
            f"Failed to enable cache overlay on port 1: HTTP {resp.status_code}"

        # Inject all required traffic over a single shared UART socket.
        uart_sock = open_uart_socket(1)
        try:
            # Coil 100 = ON, Coil 101 = OFF — used by CM-01 (LSB packing: byte = 0x01).
            inject_bytes(port=1, data=build_fc01_exchange(1, 100, 1, 1), sock=uart_sock)
            inject_bytes(port=1, data=build_fc01_exchange(1, 101, 1, 0), sock=uart_sock)
            # Discrete input 200 = 1 — used by CM-02 (type separation test).
            inject_bytes(port=1, data=build_fc02_exchange(1, 200, 1, 1), sock=uart_sock)
            # Holding registers 10-14 = 1000-1004 — used by CM-03 and CM-05.
            inject_bytes(port=1, data=build_fc03_exchange(1, 10, 5, 1000), sock=uart_sock)
            # Input registers 300-302 = 3000-3002 — used by CM-03.
            inject_bytes(port=1, data=build_fc04_exchange(1, 300, 3, 3000), sock=uart_sock)
        finally:
            uart_sock.close()

        # Poll until all expected cache entries appear (up to 30 s).
        # Expected: 2 coils + 1 discrete + 5 holding + 3 input = 11 entries.
        # Using >= 11 prevents tests from starting before all injected packets
        # have been processed by the firmware.
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            time.sleep(1)
            st = api.get_cache_status()
            if st.status_code == 200 and st.json().get("entries", 0) >= 11:
                break
        else:
            pytest.fail(
                "Cache did not populate within 30 s — no entries visible via /cache/status"
            )

        yield (host, QEMU_CACHE_MODBUS_PORT)

    finally:
        restore_errors = []

        try:
            api.update_settings({"cache_modbus_port": original_modbus_port})
        except Exception as exc:
            restore_errors.append(f"Failed to restore cache_modbus_port: {exc}")

        try:
            api.set_port_cache(1, False)
        except Exception as exc:
            restore_errors.append(f"Failed to disable cache overlay on port 1: {exc}")

        try:
            api.set_port_mode(1, original_port_mode)
        except Exception as exc:
            restore_errors.append(f"Failed to restore port 1 mode to {original_port_mode}: {exc}")

        if restore_errors:
            raise AssertionError("; ".join(restore_errors))


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

# 105 s, not 30 s: an item's pytest-timeout budget covers SETUP as well as the call, and
# this is the FIRST item of the module, so it pays the setup of both module-scoped fixtures
# before its own body starts. 30 s was structurally unfittable — smaller than the setup's
# own documented ceiling — and that is what CI reported as
# "failed on setup with Timeout (>30.0s)".
#
# Two contributors dominate:
#   - cache_server_e2e (:83) polls for the cache to fill on a loop bounded at 30 s (:169-178)
#     before it gives up with pytest.fail, plus a 1 s rebind sleep, three HTTP calls and the
#     UART injection;
#   - conftest's once-per-session rs485 snapshot (one bounded GET /settings, 20.1 s, see
#     _RS485_HTTP_TIMEOUT), which lands here when this file is run on its own; in a
#     full-suite run it is charged to the very first item of the session instead.
# _baseline (:60) adds two more writes (POST /settings + POST /ports/1/mode).
# 30 s body + 30 s cache-fill poll + 20.1 s snapshot + slack.
@pytest.mark.timeout(105)
def test_cm01_fc01_coil_response_lsb_bit_packing(cache_server_e2e):
    """FC01 response uses LSB-first bit packing: coil[addr] → bit 0 of byte 0.

    Injected: coil 100 = ON (1), coil 101 = OFF (0).
    Querying count=2 from addr=100 → packed byte = 0x01:
      bit 0 = coil 100 = 1, bit 1 = coil 101 = 0.
    """
    host, port = cache_server_e2e

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(CONNECT_TIMEOUT)
    try:
        sock.connect((host, port))
        sock.settimeout(5.0)

        req = make_mbap_request(tid=1, slave_id=1, fc=0x01, start_addr=100, count=2)
        sock.sendall(req)
        tid, fc, payload = _recv_one_response(sock)

        assert tid == 1, f"TID mismatch: expected 1, got {tid}"
        assert not (fc & 0x80), \
            f"Server returned exception for FC01: FC=0x{fc:02X}, payload={payload.hex()}"
        assert fc == 0x01, f"FC mismatch: expected 0x01, got 0x{fc:02X}"

        # payload[0] is the byte count; payload[1] is the packed coil byte.
        assert len(payload) >= 2, f"FC01 payload too short: {payload.hex()}"
        byte_count = payload[0]
        assert byte_count == 1, \
            f"Expected byte_count=1 for 2 coils, got {byte_count}"

        packed_byte = payload[1]
        assert packed_byte == 0x01, \
            f"Expected packed byte 0x01 (coil100=1, coil101=0), got 0x{packed_byte:02X}"

        # Verify via decode helper: should be [1, 0].
        decoded = decode_fc01_fc02(payload, 2)
        assert decoded == [1, 0], \
            f"decode_fc01_fc02 returned {decoded}, expected [1, 0]"

        print(f"✓ CM-01 FC01 LSB bit packing: payload={payload.hex()}, decoded={decoded}")
    finally:
        sock.close()


@pytest.mark.timeout(30)
def test_cm02_fc02_discrete_vs_coil_type_separation(cache_server_e2e):
    """FC02 and FC01 are distinct register types — cache entries are not shared.

    Injected: discrete 200 = 1 (FC02), coil 200 was NOT injected.
    FC02 addr=200 → success (value=1).
    FC01 addr=200 → exception 0x02 (ILLEGAL_ADDRESS / cache miss).
    """
    host, port = cache_server_e2e

    # --- FC02 addr=200: expect success, value=1 ---
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(CONNECT_TIMEOUT)
    try:
        sock.connect((host, port))
        sock.settimeout(5.0)

        req = make_mbap_request(tid=2, slave_id=1, fc=0x02, start_addr=200, count=1)
        sock.sendall(req)
        tid, fc, payload = _recv_one_response(sock)

        assert tid == 2, f"FC02: TID mismatch: expected 2, got {tid}"
        assert not (fc & 0x80), \
            f"FC02 addr=200 returned exception: FC=0x{fc:02X}, payload={payload.hex()}"
        assert fc == 0x02, f"FC02: FC mismatch: expected 0x02, got 0x{fc:02X}"

        decoded = decode_fc01_fc02(payload, 1)
        assert decoded == [1], \
            f"FC02 addr=200: expected [1], got {decoded}"

        print(f"✓ CM-02 FC02 addr=200: success, decoded={decoded}")
    finally:
        sock.close()

    # --- FC01 addr=200: expect exception 0x02 (coil 200 not in cache) ---
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(CONNECT_TIMEOUT)
    try:
        sock.connect((host, port))
        sock.settimeout(5.0)

        req = make_mbap_request(tid=3, slave_id=1, fc=0x01, start_addr=200, count=1)
        sock.sendall(req)
        tid, fc, payload = _recv_one_response(sock)

        assert tid == 3, f"FC01 addr=200: TID mismatch: expected 3, got {tid}"
        assert fc == (0x01 | 0x80), \
            f"FC01 addr=200 should return exception 0x82, got FC=0x{fc:02X}"
        assert len(payload) >= 1, f"Exception response has no exception code: {payload.hex()}"
        assert payload[0] == 0x02, \
            f"Expected exception code 0x02 (cache miss), got 0x{payload[0]:02X}"

        print(f"✓ CM-02 FC01 addr=200 (type separation): exception 0x02 as expected")
    finally:
        sock.close()


@pytest.mark.timeout(30)
def test_cm03_fc04_input_vs_holding_type_separation(cache_server_e2e):
    """FC04 and FC03 are distinct register types — cache entries are not shared.

    Injected: input regs 300-302 = 3000-3002 (FC04), holding reg 300 was NOT injected.
    FC04 addr=300, count=3 → success, values=[3000, 3001, 3002].
    FC03 addr=300, count=1 → exception 0x02 (ILLEGAL_ADDRESS / cache miss).
    """
    host, port = cache_server_e2e

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(CONNECT_TIMEOUT)
    try:
        sock.connect((host, port))
        sock.settimeout(5.0)

        # --- FC04 addr=300, count=3: expect success, values=[3000, 3001, 3002] ---
        req = make_mbap_request(tid=4, slave_id=1, fc=0x04, start_addr=300, count=3)
        sock.sendall(req)
        tid, fc, payload = _recv_one_response(sock)

        assert tid == 4, f"FC04: TID mismatch: expected 4, got {tid}"
        assert not (fc & 0x80), \
            f"FC04 addr=300 returned exception: FC=0x{fc:02X}, payload={payload.hex()}"
        assert fc == 0x04, f"FC04: FC mismatch: expected 0x04, got 0x{fc:02X}"

        decoded = decode_fc03_fc04(payload, 3)
        assert decoded == [3000, 3001, 3002], \
            f"FC04 addr=300 count=3: expected [3000, 3001, 3002], got {decoded}"

        print(f"✓ CM-03 FC04 addr=300 count=3: success, values={decoded}")

        # --- FC03 addr=300, count=1: expect exception 0x02 ---
        req = make_mbap_request(tid=5, slave_id=1, fc=0x03, start_addr=300, count=1)
        sock.sendall(req)
        tid, fc, payload = _recv_one_response(sock)

        assert tid == 5, f"FC03 addr=300: TID mismatch: expected 5, got {tid}"
        assert fc == (0x03 | 0x80), \
            f"FC03 addr=300 should return exception 0x83, got FC=0x{fc:02X}"
        assert len(payload) >= 1, f"Exception response has no exception code: {payload.hex()}"
        assert payload[0] == 0x02, \
            f"Expected exception code 0x02 (cache miss), got 0x{payload[0]:02X}"

        print(f"✓ CM-03 FC03 addr=300 (type separation): exception 0x02 as expected")
    finally:
        sock.close()


@pytest.mark.timeout(30)
def test_cm04_unsupported_fc_returns_illegal_function(cache_server_e2e):
    """FC05 (Write Single Coil) is not supported by the cache server → exception 0x01.

    The cache server only handles FC01/02/03/04.  Any other FC must return
    exception code 0x01 (ILLEGAL_FUNCTION).
    """
    host, port = cache_server_e2e

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(CONNECT_TIMEOUT)
    try:
        sock.connect((host, port))
        sock.settimeout(5.0)

        # FC05 Write Single Coil: addr=100, value=0xFF00 (ON)
        # make_mbap_request builds [FC][2-byte-addr][2-byte-data] which is the correct FC05 PDU.
        req = make_mbap_request(tid=1, slave_id=1, fc=0x05, start_addr=100, count=0xFF00)
        sock.sendall(req)
        tid, fc, payload = _recv_one_response(sock)

        assert tid == 1, f"FC05: TID mismatch: expected 1, got {tid}"
        assert fc == (0x05 | 0x80), \
            f"FC05 should return exception 0x85, got FC=0x{fc:02X}, payload={payload.hex()}"
        assert len(payload) >= 1, f"Exception response has no exception code: {payload.hex()}"
        assert payload[0] == 0x01, \
            f"Expected exception code 0x01 (ILLEGAL_FUNCTION), got 0x{payload[0]:02X}"

        print(f"✓ CM-04 FC05 unsupported → exception 0x01 (ILLEGAL_FUNCTION)")
    finally:
        sock.close()


@pytest.mark.timeout(30)
def test_cm05_fc03_count_boundary_illegal_data_value(cache_server_e2e):
    """FC03 count validation: count=0 and count=126 → exception 0x03 (ILLEGAL_DATA_VALUE).

    The cache server enforces 1 ≤ count ≤ 125 for FC03/FC04.
    count=0 → exception 0x03.
    count=126 → exception 0x03.
    count=125 → must NOT be exception 0x03 (may be 0x02 for cache miss, but never 0x03).
    """
    host, port = cache_server_e2e

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(CONNECT_TIMEOUT)
    try:
        sock.connect((host, port))
        sock.settimeout(5.0)

        # --- count=0: expect exception 0x03 ---
        req = make_mbap_request(tid=10, slave_id=1, fc=0x03, start_addr=10, count=0)
        sock.sendall(req)
        tid, fc, payload = _recv_one_response(sock)

        assert tid == 10, f"FC03 count=0: TID mismatch: expected 10, got {tid}"
        assert fc == (0x03 | 0x80), \
            f"FC03 count=0 should return exception 0x83, got FC=0x{fc:02X}"
        assert len(payload) >= 1, f"Exception response has no exception code: {payload.hex()}"
        assert payload[0] == 0x03, \
            f"FC03 count=0: expected exception 0x03, got 0x{payload[0]:02X}"

        print(f"✓ CM-05 FC03 count=0 → exception 0x03")

        # --- count=126: expect exception 0x03 ---
        req = make_mbap_request(tid=11, slave_id=1, fc=0x03, start_addr=10, count=126)
        sock.sendall(req)
        tid, fc, payload = _recv_one_response(sock)

        assert tid == 11, f"FC03 count=126: TID mismatch: expected 11, got {tid}"
        assert fc == (0x03 | 0x80), \
            f"FC03 count=126 should return exception 0x83, got FC=0x{fc:02X}"
        assert len(payload) >= 1, f"Exception response has no exception code: {payload.hex()}"
        assert payload[0] == 0x03, \
            f"FC03 count=126: expected exception 0x03, got 0x{payload[0]:02X}"

        print(f"✓ CM-05 FC03 count=126 → exception 0x03")

        # --- count=125: must NOT be exception 0x03 ---
        req = make_mbap_request(tid=12, slave_id=1, fc=0x03, start_addr=10, count=125)
        sock.sendall(req)
        tid, fc, payload = _recv_one_response(sock)

        assert tid == 12, f"FC03 count=125: TID mismatch: expected 12, got {tid}"
        # If an exception, verify it is NOT 0x03 (may be 0x02 for partial cache miss).
        if fc & 0x80:
            assert len(payload) >= 1, f"Exception response has no code: {payload.hex()}"
            assert payload[0] != 0x03, \
                f"FC03 count=125 must not return exception 0x03 (ILLEGAL_DATA_VALUE), got 0x{payload[0]:02X}"

        print(f"✓ CM-05 FC03 count=125 → FC=0x{fc:02X} (not exception 0x03)")
    finally:
        sock.close()


@pytest.mark.timeout(30)
def test_cm07_cache_miss_returns_illegal_address(cache_server_e2e):
    """FC03 for a register address never injected → exception 0x02 (ILLEGAL_ADDRESS).

    Address 9990 was never put into the cache, so the lookup returns
    CACHE_LOOKUP_NOT_FOUND → exception code 0x02.
    """
    host, port = cache_server_e2e

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(CONNECT_TIMEOUT)
    try:
        sock.connect((host, port))
        sock.settimeout(5.0)

        req = make_mbap_request(tid=7, slave_id=1, fc=0x03, start_addr=9990, count=1)
        sock.sendall(req)
        tid, fc, payload = _recv_one_response(sock)

        assert tid == 7, f"FC03 addr=9990: TID mismatch: expected 7, got {tid}"
        assert fc == (0x03 | 0x80), \
            f"FC03 addr=9990 should return exception 0x83, got FC=0x{fc:02X}"
        assert len(payload) >= 1, f"Exception response has no exception code: {payload.hex()}"
        assert payload[0] == 0x02, \
            f"Expected exception code 0x02 (cache miss), got 0x{payload[0]:02X}"

        print(f"✓ CM-07 FC03 addr=9990 (cache miss) → exception 0x02")
    finally:
        sock.close()


# 105 s, not 60 s: an item's pytest-timeout budget covers setup + call + TEARDOWN, and
# module-scoped fixtures are torn down inside the LAST item of the module. This is that
# item, so it also pays conftest's _restore_rs485_settings teardown — up to two bounded
# POST /settings plus a settle window (2 x 20.1 s + 1 s = 41.2 s, see _RS485_HTTP_TIMEOUT)
# — on top of this module's own module-scoped cache_server_e2e teardown. 60 s body + 45 s
# teardown allowance.
@pytest.mark.timeout(105)
def test_cm06_cache_disabled_returns_illegal_address(api: WBMGEAPI):
    """When the cache overlay is disabled, FC03 → exception 0x02.

    This test runs LAST because it temporarily clears the cache by disabling the
    cache overlay on port 1.  It does NOT depend on cache_server_e2e.

    Steps:
      1. Verify the cache host port is reachable (skip if not).
      2. Ensure cache_modbus_server_enabled=True and cache_modbus_port=50504 (guest).
      3. Disable the cache overlay on port 1 → calls cache_multimaster_disable() → s_cache_enabled=false.
      4. Sleep 0.5 s for firmware to process the change.
      5. Connect to the cache host port (server still running).
      6. Send FC03: slave=1, addr=10, count=1 → assert exception 0x02.
      7. Finally: restore port 1 to the baseline transport.
    """
    host = urlparse(api.base_url).hostname or "localhost"

    # Save original settings so teardown can restore them exactly.
    settings_resp = api.get_settings()
    assert settings_resp.status_code == 200, \
        f"CM-06: GET /settings failed: HTTP {settings_resp.status_code}"
    orig_settings = settings_resp.json()
    original_modbus_port = orig_settings.get("cache_modbus_port", 504)
    original_server_enabled = orig_settings.get("cache_modbus_server_enabled", True)

    # Ensure the cache server is enabled and on the correct port before the skip guard,
    # so that a fresh run without prior tests can still reach the cache server.
    resp = api.update_settings({
        "cache_modbus_server_enabled": True,
        "cache_modbus_port": qemu_ports.CACHE_MODBUS_GUEST_PORT,  # guest port (hostfwd target)
    })
    assert resp.status_code == 200, \
        f"CM-06: Failed to configure cache server: HTTP {resp.status_code}"
    time.sleep(1)  # allow the server to rebind on the new port if needed

    # Skip guard: verify port is accessible after configuration.
    probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    probe.settimeout(CONNECT_TIMEOUT)
    try:
        result = probe.connect_ex((host, QEMU_CACHE_MODBUS_PORT))
    finally:
        probe.close()
    if result != 0:
        pytest.skip(
            f"Cache Modbus TCP port {QEMU_CACHE_MODBUS_PORT} not reachable on {host} "
            f"(connect_ex returned {result}) — skipping CM-06"
        )

    try:
        # Disable the cache overlay on port 1 — this calls cache_multimaster_disable(),
        # setting s_cache_enabled = false inside the firmware.
        resp = api.set_port_cache(1, False)
        assert resp.status_code == 200, \
            f"CM-06: Failed to disable cache overlay on port 1: HTTP {resp.status_code}"

        # Allow firmware to process the change.
        time.sleep(0.5)

        # Connect to the cache Modbus TCP server (it is still running).
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(CONNECT_TIMEOUT)
        try:
            sock.connect((host, QEMU_CACHE_MODBUS_PORT))
            sock.settimeout(5.0)

            req = make_mbap_request(tid=6, slave_id=1, fc=0x03, start_addr=10, count=1)
            sock.sendall(req)
            tid, fc, payload = _recv_one_response(sock)

            assert tid == 6, f"CM-06: TID mismatch: expected 6, got {tid}"
            assert fc == (0x03 | 0x80), \
                f"CM-06: expected exception 0x83 (cache disabled), got FC=0x{fc:02X}"
            assert len(payload) >= 1, f"CM-06: Exception response has no code: {payload.hex()}"
            assert payload[0] == 0x02, \
                f"CM-06: expected exception 0x02 (cache disabled), got 0x{payload[0]:02X}"

            print(f"✓ CM-06 cache disabled → FC03 returns exception 0x02")
        finally:
            sock.close()

    finally:
        # Restore port 1 to tcp_bridge (the _baseline fixture's starting state).
        api.set_port_mode(1, "tcp_bridge")
        # Restore cache server settings to their original values.
        api.update_settings({
            "cache_modbus_server_enabled": original_server_enabled,
            "cache_modbus_port": original_modbus_port,
        })
