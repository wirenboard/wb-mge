"""Consolidated end-to-end tests for the additive sniffer + cache overlays.

This module merges seven previously-separate e2e test files (originally
42..48) into one. Every test below is behaviorally identical to its original;
this is a pure merge/dedup of imports, constants, helpers, and fixtures, not a
rewrite. The covered properties are:

  - Cache overlay survives a reboot (NVS round-trip) and re-populates live.
  - Bridge sniffer master->slave CAUSAL pairing (not just counts).
  - Sniffer + cache co-active on one tcp_bridge port across an enable/disable/
    re-enable cycle.
  - Port-merged cache pool: port-2 feed + dual-port most-recent-wins / survival.
  - Cache overlay on a TRANSPARENT tcp_bridge port (populate + non-disturbance).
  - Cache coherency when toggled mid-traffic on a Modbus-gateway tcp_bridge.
  - Cache broadcast guard: a broadcast read must never become a cached entry.

Requires QEMU with UART1 exposed as TCP 5561, UART2 as TCP 5562, the Modbus
gateway guest port 502 forwarded to host 50502, and the cache Modbus server /
transparent bridge guest port 50504 forwarded to host 50504 (see conftest.py
qemu_process hostfwd mapping).
"""

import json
import socket
import threading
import time
from urllib.parse import urlparse

import pytest

from conftest import build_gateway_fixture, _poll_tcp_connect
from rtu_slave_helpers import ModbusRtuSlaveThread
from modbus_helpers import make_mbap_request, send_and_receive, query_register_once
from sniffer_helpers import _ws_connect, _collect_packets
from packet_injector import (
    build_fc03_request,
    build_fc03_response,
    build_fc03_exchange,
    build_fc04_exchange,
    inject_bytes,
    open_uart_socket,
    UART_TCP_PORT,
)


# ===========================================================================
# Constants (must match conftest.py qemu_process hostfwd / chardev mapping)
# ===========================================================================
GATEWAY_HOST = "127.0.0.1"
GATEWAY_HOST_PORT = 50502        # QEMU hostfwd: guest 502   -> host 50502 (Modbus gateway)
GATEWAY_GUEST_PORT = 502         # the TCP port the gateway binds to inside the firmware
CACHE_MODBUS_HOST_PORT = 50504   # QEMU hostfwd: guest 50504 -> host 50504 (cache Modbus server)
TRANSPARENT_PORT1_HOST_PORT = 50504  # QEMU hostfwd: guest 50504 -> host 50504 (transparent bridge)
QEMU_CACHE_MODBUS_PORT = 50504   # cache Modbus TCP server: guest 50504 -> host 50504
UART1_TCP_PORT = 5561            # QEMU UART1 (RS485-1) chardev
CONNECT_TIMEOUT = 5.0

# Modbus identity used by the gateway / sniffer tests.
SLAVE_ID = 1
FC03 = 0x03

# Reboot-persistence test (formerly 42): its slave returns a distinct value and
# reads two registers at address 30.
SLAVE_FAKE_VALUE_REBOOT = 0x4242
READ_ADDR = 30
READ_COUNT = 2

# Bridge-sniffer / co-active tests (formerly 43, 44): mock RTU slave value.
SLAVE_FAKE_VALUE = 0x1234

# A KNOWN ordered sequence of DISTINCT FC03 reads (different start addresses).
# Distinct addresses let us identify each driven master request unambiguously in
# the sniffer stream (the address is carried in the request frame bytes) and so
# verify that the masters appear in exactly the order we drove them.
DRIVEN_ADDRS = [10, 20, 30, 40, 50]

# Dual-port-merge test (formerly 45): a distinct slave id so these entries are
# easy to single out, plus per-property probe addresses/values.
DPM_SLAVE_ID = 7

P2_FC03_ADDR = 1010    # DPM-01: FC03 driven on port 2
P2_FC03_VALUE = 0xA001
P2_FC04_ADDR = 1300    # DPM-01: FC04 driven on port 2
P2_FC04_VALUE = 0xB002

MERGE_ADDR = 1500      # DPM-02: same address observed on both ports
MERGE_VALUE_P1 = 0x1111  # observed first on port 1
MERGE_VALUE_P2 = 0x2222  # then on port 2 -> must win (most-recent)

COEXIST_ADDR_P1 = 1600   # DPM-03: only on port 1
COEXIST_VALUE_P1 = 0x3333
COEXIST_ADDR_P2 = 1601   # DPM-03: only on port 2
COEXIST_VALUE_P2 = 0x4444

SURVIVE_ADDR = 1700    # DPM-04: port-2 traffic after port-1 overlay disabled
SURVIVE_VALUE = 0x5555

# Transparent-bridge cache test (formerly 46): cache-population transaction.
CACHE_SLAVE = 1
CACHE_ADDR = 40                          # holding register address to cache
CACHE_VALUE = 0xBEEF                     # value the injected FC03 response carries

# Cache-toggle-mid-traffic test (formerly 47): register A and its two values.
HOLDING_ADDR = 40                # register A (holding register address)
TOGGLE_READ_COUNT = 1            # single-register reads keep value mapping unambiguous
VALUE1 = 0x1111                  # value the slave returns in phase 1 (cache enabled)
VALUE2 = 0x2222                  # value the slave returns in phase 3 (after re-enable)

# Broadcast-guard test (formerly 48): passive transport + distinct addresses.
PASSIVE = "passive"
BROADCAST_ADDR = 0x0140          # 320 — broadcast FC03 start address (no response)
BROADCAST_COUNT = 2
UNICAST_ADDR = 0x0150            # 336 — unicast FC03 start address (answered)
UNICAST_COUNT = 1
UNICAST_SLAVE = 1
UNICAST_VALUE = 0xABCD           # distinctive value returned by the unicast response


# ===========================================================================
# Shared fixtures (built from the conftest factory; replicated locally because
# the originals were defined inside the individual test files, not conftest).
# ===========================================================================

# A Modbus-gateway tcp_bridge on RS-485 port 1 with a mock RTU slave on UART1.
# Yields the ModbusRtuSlaveThread. Used by the sniffer-pairing and co-active
# tests (slave answers every register read with SLAVE_FAKE_VALUE).
gateway_p1_modbus = build_gateway_fixture(
    port_num=1,
    tcp_host_port=GATEWAY_HOST_PORT,
    uart_tcp_port=UART1_TCP_PORT,
    bridge_port=502,
    modbus=True,
    fake_value=SLAVE_FAKE_VALUE,
)

# A Modbus-gateway tcp_bridge on RS-485 port 1 with a mock RTU slave whose
# initial fake_value is irrelevant: the toggle test sets slave.fake_value
# explicitly before driving each phase. Kept distinct from gateway_p1_modbus
# because the factory's fake_value parameter differs.
gateway_p1_modbus_toggle = build_gateway_fixture(
    port_num=1,
    tcp_host_port=GATEWAY_HOST_PORT,
    uart_tcp_port=UART1_TCP_PORT,
    bridge_port=502,
    modbus=True,
    fake_value=VALUE1,
)

# A TRANSPARENT tcp_bridge on RS-485 port 1 (modbus disabled). Yields None.
transparent_p1 = build_gateway_fixture(
    port_num=1,
    tcp_host_port=TRANSPARENT_PORT1_HOST_PORT,
    uart_tcp_port=UART1_TCP_PORT,
    bridge_port=50504,
    modbus=False,
)


# ===========================================================================
# Shared helpers
# ===========================================================================

class _GatewayDriver(threading.Thread):
    """Continuously issue FC03 reads through the Modbus-gateway tcp_bridge.

    Each request makes the firmware forward an RTU master request to the bus
    (exercising the TX sniffer feed and the cache request side) and the mock
    slave reply (exercising the RX sniffer feed and the cache response side),
    so the sniffer and cache observe complete request/response transactions and
    can pair them into a cached value. Every caller passes addr/count explicitly.
    """

    def __init__(self, host, port, slave=1, fc=0x03, addr=10, count=2, interval=0.3):
        super().__init__(daemon=True)
        self.host = host
        self.port = port
        self.slave = slave
        self.fc = fc
        self.addr = addr
        self.count = count
        self.interval = interval
        self.ok = 0
        self.errors = []
        self._stop_event = threading.Event()  # not "_stop": that shadows threading.Thread._stop() and breaks join()
        self._sock = None

    def run(self):
        try:
            self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._sock.settimeout(5.0)
            self._sock.connect((self.host, self.port))
        except OSError as exc:
            self.errors.append(f"connect: {exc}")
            return
        tid = 1
        while not self._stop_event.is_set():
            try:
                req = make_mbap_request(tid, self.slave, self.fc, self.addr, self.count)
                _t, _u, fc, _payload = send_and_receive(self._sock, req)
                if not (fc & 0x80):
                    self.ok += 1
                tid = (tid % 0xFFFF) + 1
            except Exception as exc:  # noqa: BLE001 — surfaced via errors
                self.errors.append(str(exc))
                break
            self._stop_event.wait(self.interval)
        try:
            self._sock.close()
        except OSError:
            pass

    def stop(self):
        self._stop_event.set()


class _GatewayConn:
    """Single persistent TCP connection to the Modbus-gateway tcp_bridge.

    drive_once() issues exactly one FC03 read through the gateway, which makes
    the firmware forward an RTU master request to the bus and read the slave
    reply. The caller controls which value is on the bus by setting the mock
    slave's `fake_value` before driving. Every non-exception transaction
    increments `ok`; any socket/protocol failure is appended to `errors` so the
    test can assert the bridge stayed uninterrupted across cache toggles.
    """

    def __init__(self, host, port):
        self.host = host
        self.port = port
        self.ok = 0
        self.errors = []
        self._sock = None
        self._tid = 1

    def connect(self):
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._sock.settimeout(5.0)
        self._sock.connect((self.host, self.port))

    def drive_once(self, addr=HOLDING_ADDR, count=TOGGLE_READ_COUNT):
        """Forward one FC03 request->response through the gateway.

        Returns the decoded first register value on success, or None on a
        Modbus exception / transport error (which is also recorded in errors).
        """
        try:
            req = make_mbap_request(self._tid, SLAVE_ID, 0x03, addr, count)
            self._tid = (self._tid % 0xFFFF) + 1
            _t, _u, fc, payload = send_and_receive(self._sock, req)
            if fc & 0x80:
                self.errors.append(
                    f"gateway returned Modbus exception 0x{fc:02X} "
                    f"(code {payload[0] if payload else -1})"
                )
                return None
            # FC03 payload: [byte_count][hi][lo]...
            if len(payload) < 3:
                self.errors.append(f"short FC03 payload: {payload.hex()}")
                return None
            # Count every successful (non-exception) transaction so the test can
            # assert the bridge data path stayed up across cache toggles.
            self.ok += 1
            return (payload[1] << 8) | payload[2]
        except Exception as exc:  # noqa: BLE001 — surfaced via errors for the assertion
            self.errors.append(str(exc))
            return None

    def drive_until_value(self, expected, attempts=40, interval=0.3):
        """Drive the gateway repeatedly until the slave reports `expected`.

        Confirms the bus genuinely carries `expected` before we start polling
        the cache for it, so phase transitions are deterministic rather than
        racing the slave.fake_value reassignment.
        """
        for _ in range(attempts):
            got = self.drive_once()
            if got == expected:
                return True
            time.sleep(interval)
        return False

    def close(self):
        if self._sock is not None:
            try:
                self._sock.close()
            except OSError:
                pass


class _UartEchoThread(threading.Thread):
    """Connect to the UART1 chardev and echo every received byte back verbatim,
    so a transparent-bridge TCP client sees its own bytes returned."""

    def __init__(self, host, port):
        super().__init__(daemon=True)
        self.host = host
        self.port = port
        self.connected = False
        self._stop_event = threading.Event()  # not "_stop": that shadows threading.Thread._stop() and breaks join()
        self._sock = None

    def run(self):
        try:
            self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._sock.settimeout(5.0)
            self._sock.connect((self.host, self.port))
            self.connected = True
            self._sock.settimeout(0.3)
        except OSError:
            return
        while not self._stop_event.is_set():
            try:
                chunk = self._sock.recv(256)
                if not chunk:
                    break
                self._sock.sendall(chunk)
            except socket.timeout:
                continue
            except OSError:
                break
        try:
            self._sock.close()
        except OSError:
            pass

    def wait_connected(self, timeout=5.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.connected:
                return True
            time.sleep(0.05)
        return False

    def stop(self):
        self._stop_event.set()


def _roundtrip_once(host, tcp_port, payload, timeout=5.0):
    """Open a fresh TCP client to the transparent bridge, send `payload`, and
    return the bytes echoed back (via the UART echo thread). Caller asserts."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    try:
        sock.connect((host, tcp_port))
        sock.sendall(payload)
        received = b""
        deadline = time.monotonic() + timeout
        while len(received) < len(payload) and time.monotonic() < deadline:
            try:
                chunk = sock.recv(64)
                if not chunk:
                    break
                received += chunk
            except socket.timeout:
                break
        return received
    finally:
        try:
            sock.close()
        except OSError:
            pass


def _read_uptime_seconds(api):
    """Return the device uptime in whole seconds via GET /uptime."""
    resp = api.get_uptime()
    assert resp.status_code == 200, f"GET /uptime failed: {resp.status_code}"
    d = resp.json()
    return d["days"] * 86400 + d["hours"] * 3600 + d["minutes"] * 60 + d["seconds"]


def _addr_from_master_raw(raw: str):
    """Extract the FC03 start address from a master request's raw hex string.

    Master RTU request frame: slave(1) FC(1) addr_hi(1) addr_lo(1) count(1+1) CRC(2).
    The address is bytes [2:4] -> hex chars [4:8]. Returns None if the frame is
    too short or not parseable, so callers can simply skip non-matching frames.
    """
    if not isinstance(raw, str) or len(raw) < 8:
        return None
    try:
        return int(raw[4:8], 16)
    except ValueError:
        return None


def _drive_one_transaction(sock, tid, addr):
    """Send one FC03 read for `addr` and synchronously read the full response.

    Strictly synchronous: returns only after the complete Modbus TCP response
    frame has been received, so the next transaction never overlaps this one on
    the bus. Returns the (non-exception) function code; raises on transport error.
    """
    req = make_mbap_request(tid, SLAVE_ID, FC03, addr, READ_COUNT)
    _t, _u, fc, _payload = send_and_receive(sock, req)
    return fc


def _wait_cache_entry(api, addr, want_value, timeout_sec):
    """Poll GET /cache/json until a holding-register row for addr==addr with the
    expected value appears, or the deadline expires. Returns the matching rows."""
    deadline = time.monotonic() + timeout_sec
    while time.monotonic() < deadline:
        cj = api.get_cache_json()
        if cj.status_code == 200:
            rows = cj.json().get("d", [])
            match = [
                r for r in rows
                if r.get("a") == addr and r.get("t") == "h" and r.get("v") == want_value
            ]
            if match:
                return match
        time.sleep(0.5)
    return []


def _cache_host(api):
    """Resolve the device host from the API base URL (cache server lives there)."""
    return urlparse(api.base_url).hostname or "localhost"


def _cache_server_reachable_host(host):
    """True if the cache Modbus TCP server port accepts a TCP connection."""
    probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    probe.settimeout(CONNECT_TIMEOUT)
    try:
        return probe.connect_ex((host, QEMU_CACHE_MODBUS_PORT)) == 0
    finally:
        probe.close()


def _find_entry(api, *, t, a, s=DPM_SLAVE_ID):
    """Return the /cache/json entry matching (s, t, a), or None.

    The pool is port-merged, so there is at most one entry per (s, t, a) tuple
    regardless of which port observed it.
    """
    resp = api.get_cache_json()
    assert resp.status_code == 200, f"GET /cache/json failed: {resp.status_code}"
    rows = resp.json().get("d", [])
    for r in rows:
        if r.get("s") == s and r.get("t") == t and r.get("a") == a:
            return r
    return None


def _wait_for_value(api, *, t, a, v, s=DPM_SLAVE_ID, timeout=30.0):
    """Poll /cache/json until the (s, t, a) entry exists AND has value v.

    Returns the matching entry.  Deterministic: it waits for the specific value,
    so it cannot race ahead of the injected observation (used to order the
    most-recent-wins steps without relying on wall-clock timing).
    """
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        last = _find_entry(api, t=t, a=a, s=s)
        if last is not None and last.get("v") == v:
            return last
        time.sleep(0.5)
    pytest.fail(
        f"cache entry s={s} t={t!r} a={a} did not reach value=0x{v:04X} within "
        f"{timeout:.0f}s; last seen={last!r}"
    )


def _inject_fc03(port, addr, value):
    """Write one FC03 request+response exchange (single register=value) on a port."""
    sock = open_uart_socket(port)
    try:
        inject_bytes(port, build_fc03_exchange(DPM_SLAVE_ID, addr, 1, value), sock=sock)
    finally:
        sock.close()


def _inject_fc04(port, addr, value):
    """Write one FC04 request+response exchange (single register=value) on a port."""
    sock = open_uart_socket(port)
    try:
        inject_bytes(port, build_fc04_exchange(DPM_SLAVE_ID, addr, 1, value), sock=sock)
    finally:
        sock.close()


def _require_uart(port):
    """Skip the test if the QEMU UART chardev for the given port is unreachable."""
    uart_tcp = {1: 5561, 2: 5562}[port]
    probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    probe.settimeout(3.0)
    try:
        if probe.connect_ex(("127.0.0.1", uart_tcp)) != 0:
            pytest.skip(
                f"UART{port} chardev TCP port {uart_tcp} not reachable; "
                "QEMU may not expose this UART as TCP in this configuration."
            )
    finally:
        probe.close()


def _cache_json_holding_value(api, addr):
    """Return the cached holding-register value for `addr`, or None if absent.

    /cache/json entry shape: {"d":[{"s","t","a","v","age"}, ...]} where t=="h"
    marks a holding register. The pool is port-merged, so a single (slave,
    type, addr) key carries the most-recent value regardless of source port.
    """
    resp = api.get_cache_json()
    if resp.status_code != 200:
        return None
    for row in resp.json().get("d", []):
        if row.get("t") == "h" and row.get("a") == addr and row.get("s") == SLAVE_ID:
            return row.get("v")
    return None


def _poll_cache_json_value(api, addr, expected, deadline_s):
    """Poll /cache/json until the holding register at `addr` reads `expected`."""
    deadline = time.monotonic() + deadline_s
    while time.monotonic() < deadline:
        if _cache_json_holding_value(api, addr) == expected:
            return True
        time.sleep(0.3)
    return False


def _cache_server_reachable():
    probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    probe.settimeout(3.0)
    try:
        return probe.connect_ex((GATEWAY_HOST, CACHE_MODBUS_HOST_PORT)) == 0
    finally:
        probe.close()


def _uart_reachable(port: int) -> bool:
    """Return True if the QEMU UART chardev for the given RS-485 port accepts a TCP connection."""
    probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    probe.settimeout(3.0)
    try:
        probe.connect(("127.0.0.1", UART_TCP_PORT[port]))
        return True
    except (ConnectionRefusedError, OSError, socket.timeout):
        return False
    finally:
        probe.close()


# ===========================================================================
# Cache overlay persists across reboot (NVS round-trip) and re-populates live
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(2400)
def test_cache_overlay_persists_across_reboot(api):
    """Enable the cache on port 1, reboot, and assert the overlay is restored from
    NVS (rs485_1.cache_enabled still true) AND a fresh FC03 transaction repopulates
    GET /cache/json after reboot."""
    # Skip early if the UART1 chardev is not reachable in this QEMU config.
    probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    probe.settimeout(3.0)
    try:
        probe.connect((GATEWAY_HOST, UART1_TCP_PORT))
        probe.close()
    except (ConnectionRefusedError, OSError, socket.timeout):
        probe.close()
        pytest.skip(
            f"Cannot connect to UART1 chardev TCP port {UART1_TCP_PORT}; "
            "QEMU may not expose this UART as TCP in this configuration."
        )

    # Save full original settings so teardown can restore the device verbatim.
    resp = api.get_settings()
    assert resp.status_code == 200, f"GET /settings failed: {resp.status_code}"
    original_settings = resp.json()
    orig_mode = original_settings.get("rs485_1", {}).get("port_mode", "disabled")
    orig_value_timeout = original_settings.get("cache_value_timeout_s", 0)

    slave = None
    driver = None
    try:
        # --- Setup: configure RS-485 port 1 as a Modbus-gateway tcp_bridge -----
        # Disable the port first to release the UART driver, then write the bridge
        # sub-object and switch to tcp_bridge (mirrors build_gateway_fixture).
        resp = api.set_port_mode(1, "disabled")
        assert resp.status_code == 200, f"disable port 1 failed: {resp.status_code}"
        time.sleep(0.3)

        # Use a generous value-timeout so cached entries never expire mid-test
        # (avoids a staleness race when we poll /cache/json after reboot).
        resp = api.update_settings({"cache_value_timeout_s": 60})
        assert resp.status_code == 200, f"set cache_value_timeout_s failed: {resp.status_code}"

        port_settings = dict(original_settings.get("rs485_1", {}))
        port_settings["bridge"] = {
            "mode": "server",
            "port": GATEWAY_GUEST_PORT,
            "ip": "0.0.0.0",
            "modbus": True,
        }
        resp = api.update_settings({"rs485_1": port_settings})
        assert resp.status_code == 200, f"POST /settings (bridge) failed: {resp.status_code}"
        assert resp.json().get("success") is True, \
            f"bridge settings update not successful: {resp.json()}"
        time.sleep(0.3)

        resp = api.set_port_mode(1, "tcp_bridge")
        assert resp.status_code == 200, f"set tcp_bridge failed: {resp.status_code}"
        ready = _poll_tcp_connect(GATEWAY_HOST, GATEWAY_HOST_PORT, timeout=5.0)
        assert ready, f"gateway did not bind host port {GATEWAY_HOST_PORT} within 5 s"

        # --- Enable the cache overlay and confirm /info reflects it ------------
        resp = api.set_port_cache(1, True)
        assert resp.status_code == 200, f"enable cache overlay failed: {resp.status_code}"

        info = api.get_info()
        assert info.status_code == 200, f"GET /info failed: {info.status_code}"
        assert info.json().get("rs485_1", {}).get("cache_enabled") is True, (
            "rs485_1.cache_enabled must be true immediately after enabling the overlay"
        )
        print("✓ cache overlay enabled; /info rs485_1.cache_enabled == true (pre-reboot)")

        # --- Reboot ------------------------------------------------------------
        uptime_before = _read_uptime_seconds(api)
        # Settle briefly so the NVS write of the overlay flag has flushed.
        time.sleep(1.0)
        try:
            resp = api.execute_command("reboot")
            assert resp.status_code == 200, \
                f"POST /cmd reboot expected 200, got {resp.status_code}"
        except ConnectionError:
            # The connection may drop as the device resets — expected.
            print("  Connection dropped during reboot (expected)")

        try:
            api.wait_for_ready(timeout=1800)
        except TimeoutError:
            pytest.fail("Device did not come back within 1800 s after reboot")
        print("✓ device came back online after reboot")

        # Sanity: confirm a real reboot happened (uptime reset).
        uptime_after = _read_uptime_seconds(api)
        assert uptime_after < uptime_before, (
            f"uptime after reboot ({uptime_after}s) >= before ({uptime_before}s) — "
            "no reboot detected"
        )

        # --- CORE ASSERTION: overlay flag survived the NVS round-trip ----------
        info = api.get_info()
        assert info.status_code == 200, f"GET /info after reboot failed: {info.status_code}"
        rs485_1 = info.json().get("rs485_1", {})
        assert rs485_1.get("cache_enabled") is True, (
            "rs485_1.cache_enabled must STILL be true after reboot — the cache "
            "overlay must persist across reboot via NVS, but /info reports "
            f"cache_enabled={rs485_1.get('cache_enabled')!r}"
        )
        # The tcp_bridge transport must also have been restored from persisted config.
        assert rs485_1.get("port_mode") == "tcp_bridge", (
            "port 1 transport must be restored to tcp_bridge after reboot, got "
            f"{rs485_1.get('port_mode')!r}"
        )
        print("✓ rs485_1.cache_enabled STILL true after reboot (NVS round-trip)")

        # --- Drive a known FC03 transaction and assert the cache repopulates ---
        # The cache POOL is volatile, so after reboot it starts empty; the
        # RESTORED overlay must observe new traffic and pair it into the map.
        ready = _poll_tcp_connect(GATEWAY_HOST, GATEWAY_HOST_PORT, timeout=10.0)
        assert ready, (
            f"gateway tcp_bridge did not re-bind host port {GATEWAY_HOST_PORT} after reboot"
        )

        # Connect a fresh RTU slave to the UART chardev (the pre-reboot connection,
        # if any, was severed by the reset).
        slave = ModbusRtuSlaveThread(
            host=GATEWAY_HOST,
            port=UART1_TCP_PORT,
            fake_value=SLAVE_FAKE_VALUE_REBOOT,
            connect_timeout=5.0,
        )
        slave.start()
        assert slave.wait_connected(timeout=10.0), (
            f"RTU slave could not connect to UART chardev {UART1_TCP_PORT} after reboot"
        )

        driver = _GatewayDriver(GATEWAY_HOST, GATEWAY_HOST_PORT, addr=READ_ADDR, count=READ_COUNT)
        driver.start()

        # Poll /cache/json until the known holding register reappears with the
        # expected value (most-recent-wins on (slave_id, type, address)).
        deadline = time.monotonic() + 40
        match = []
        while time.monotonic() < deadline:
            cj = api.get_cache_json()
            if cj.status_code == 200:
                rows = cj.json().get("d", [])
                match = [
                    r for r in rows
                    if r.get("a") == READ_ADDR
                    and r.get("t") == "h"
                    and r.get("v") == SLAVE_FAKE_VALUE_REBOOT
                ]
                if match:
                    break
            time.sleep(1.0)

        driver.stop()
        driver.join(timeout=5.0)

        assert not driver.errors, f"gateway driver errors after reboot: {driver.errors}"
        assert driver.ok >= 1, "no successful gateway transactions completed after reboot"
        assert match, (
            f"cached holding reg addr={READ_ADDR} value=0x{SLAVE_FAKE_VALUE_REBOOT:04X} did not "
            "reappear in /cache/json within 40 s after reboot — the restored cache "
            "overlay is not observing/pairing bus traffic"
        )
        print(
            f"✓ post-reboot FC03 transaction repopulated /cache/json: "
            f"reg {READ_ADDR}=0x{SLAVE_FAKE_VALUE_REBOOT:04X}"
        )

    finally:
        # Stop the driver/slave threads first so nothing keeps the bus busy.
        if driver is not None:
            driver.stop()
            driver.join(timeout=5.0)
            if driver.is_alive():
                print("✗ gateway driver thread did not stop within 5 s")
        if slave is not None:
            slave.stop()
            slave.join(timeout=3.0)
            if slave.is_alive():
                print(f"✗ RTU slave thread on port {UART1_TCP_PORT} did not stop (port leak!)")

        # Disable the cache overlay (the property under test), then restore config.
        try:
            api.set_port_cache(1, False)
        except Exception as exc:  # noqa: BLE001 — never mask the real failure in teardown
            print(f"✗ failed to disable cache overlay on port 1: {exc}")

        try:
            restore = api.update_settings(original_settings)
            if restore.status_code != 200:
                print(f"✗ failed to restore settings: HTTP {restore.status_code}")
        except Exception as exc:  # noqa: BLE001
            print(f"✗ failed to restore settings: {exc}")

        try:
            api.update_settings({"cache_value_timeout_s": orig_value_timeout})
        except Exception as exc:  # noqa: BLE001
            print(f"✗ failed to restore cache_value_timeout_s: {exc}")

        try:
            api.set_port_mode(1, orig_mode)
        except Exception as exc:  # noqa: BLE001
            print(f"✗ failed to restore port 1 mode to {orig_mode!r}: {exc}")
        time.sleep(0.3)


# ===========================================================================
# Bridge sniffer master->slave causal pairing
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(120)
def test_bridge_sniffer_master_slave_causal_pairing(api, gateway_p1_modbus):
    """On a Modbus-gateway tcp_bridge + WS sniffer overlay, a known ordered sequence
    of distinct FC03 reads produces clean, causally-ordered master->slave pairs.

    Asserts (beyond 41's count-only check):
      1. The distinct master requests we drove appear in the sniffer stream in the
         exact order we drove them (by start address parsed from the raw frame).
      2. Each driven master frame is immediately followed by a slave frame with the
         SAME slave_id and function, a STRICTLY GREATER timestamp_us, and a STRICTLY
         GREATER monotonic packet id (TX precedes RX of the same transaction).
      3. No scrambled order, no two masters back-to-back without a slave between,
         and no unmatched driven master.
    """
    slave = gateway_p1_modbus
    assert slave is not None, "gateway fixture must provide an RTU slave"

    ws = stop_ping = None
    sock = None
    # Collect a generous number of packets: each driven transaction yields one
    # master (TX) + one slave (RX), plus possibly a leading orphan slave if the
    # sniffer attaches mid-exchange. min_count is a lower bound; the deadline and
    # the post-drive drain below bound the wait deterministically.
    expected_pairs = len(DRIVEN_ADDRS)
    try:
        # Sniffer DISPLAY overlay on the (already tcp_bridge) port 1.
        ws, stop_ping, _ = _ws_connect(api, 1)
        # Let the WS start command be processed before generating bus traffic.
        time.sleep(0.5)

        # One persistent TCP connection => one logical master; strictly synchronous
        # request/response keeps every transaction serialized on the RS-485 bus.
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5.0)
        sock.connect((GATEWAY_HOST, GATEWAY_HOST_PORT))

        ok = 0
        tid = 1
        for addr in DRIVEN_ADDRS:
            fc = _drive_one_transaction(sock, tid, addr)
            assert not (fc & 0x80), (
                f"gateway returned Modbus exception for addr={addr}: fc=0x{fc:02X}"
            )
            ok += 1
            tid = (tid % 0xFFFF) + 1

        assert ok == expected_pairs, (
            f"expected {expected_pairs} successful gateway transactions, got {ok}"
        )

        # Collect sniffer packets. We need at least 2 packets per driven transaction
        # (master + slave). Use min_count as a lower bound and a comfortable deadline;
        # _collect_packets stops early once min_count is reached.
        packets = _collect_packets(
            ws,
            min_count=2 * expected_pairs,
            timeout_sec=40,
            filter_fn=lambda p: p.get("type") == "packet",
        )
    finally:
        if sock is not None:
            try:
                sock.close()
            except OSError:
                pass
        if stop_ping is not None:
            stop_ping.set()
        if ws is not None:
            try:
                ws.send('{"cmd": "stop", "port": 1}')
            except Exception:
                pass
            try:
                ws.close()
            except Exception:
                pass

    # --- Sanity: the stream is well-formed before we reason about pairing. ---
    assert packets, "sniffer produced no packets while the bridge forwarded traffic"
    for i, p in enumerate(packets):
        assert p.get("sender") in ("master", "slave"), (
            f"packet[{i}] has unexpected sender={p.get('sender')!r}: {p}"
        )
        assert isinstance(p.get("timestamp_us"), int) and p["timestamp_us"] > 0, (
            f"packet[{i}] has invalid timestamp_us: {p}"
        )
        assert isinstance(p.get("id"), int), f"packet[{i}] missing integer id: {p}"

    # Packet ids must be strictly increasing in delivery order (stream invariant).
    ids = [p["id"] for p in packets]
    id_violations = [
        (i, ids[i], ids[i + 1]) for i in range(len(ids) - 1) if ids[i] >= ids[i + 1]
    ]
    assert not id_violations, f"packet id not strictly increasing at: {id_violations}"

    # --- Locate the run of driven master requests by their distinct addresses. ---
    # A leading orphan slave (sniffer attached mid-exchange) or stray retransmit
    # must not break pairing: we anchor on the masters we actually drove, matched
    # by their distinct addresses in the exact order we sent them.
    driven_master_positions = []   # indices into `packets` of our FC03 masters, in stream order
    seen_addrs = []
    for i, p in enumerate(packets):
        if p.get("sender") != "master" or p.get("function") != FC03:
            continue
        addr = _addr_from_master_raw(p.get("raw", ""))
        if addr in DRIVEN_ADDRS:
            driven_master_positions.append(i)
            seen_addrs.append(addr)

    # 1) Every distinct driven master must have been observed exactly once, in order.
    #    (Strict synchronous driving => no duplicate masters, no missing masters.)
    assert seen_addrs == DRIVEN_ADDRS, (
        "driven FC03 master requests were not observed in the exact order driven: "
        f"expected {DRIVEN_ADDRS}, sniffer saw {seen_addrs}. "
        f"all packets={packets!r}"
    )

    # 2) Causal pairing: the slave response for each master is the next 'slave'
    #    packet in the stream, with matching slave_id/function and a strictly
    #    greater timestamp and id. Because transactions are serialized, the very
    #    next packet after a driven master must be its own slave response — no
    #    other master may appear before that slave (no scrambling/interleaving).
    for k, m_idx in enumerate(driven_master_positions):
        m = packets[m_idx]
        addr = seen_addrs[k]

        # The immediately following packet must be the paired slave response,
        # not another master (that would mean a master fired before the previous
        # response was seen => scrambled/overlapping transactions).
        assert m_idx + 1 < len(packets), (
            f"master for addr={addr} (id={m['id']}) has no following packet — "
            f"its slave response is missing. packets={packets!r}"
        )
        s = packets[m_idx + 1]
        assert s.get("sender") == "slave", (
            f"transaction addr={addr}: expected the slave response immediately after "
            f"the master frame, but next packet is sender={s.get('sender')!r} "
            f"(function={s.get('function')}, raw={s.get('raw')!r}). "
            f"This indicates scrambled/overlapping master->slave pairing. packets={packets!r}"
        )

        # Same transaction identity.
        assert s.get("slave_id") == m.get("slave_id"), (
            f"transaction addr={addr}: slave_id mismatch master={m.get('slave_id')} "
            f"slave={s.get('slave_id')}"
        )
        assert s.get("function") == m.get("function") == FC03, (
            f"transaction addr={addr}: function mismatch master={m.get('function')} "
            f"slave={s.get('function')} (expected {FC03})"
        )

        # TX (master) strictly precedes RX (slave) of the same transaction.
        assert s["id"] > m["id"], (
            f"transaction addr={addr}: slave id {s['id']} is not greater than "
            f"master id {m['id']} — RX frame does not follow TX frame"
        )
        assert s["timestamp_us"] > m["timestamp_us"], (
            f"transaction addr={addr}: slave timestamp_us {s['timestamp_us']} is not "
            f"STRICTLY greater than master timestamp_us {m['timestamp_us']} — the bridge "
            f"TX frame must precede the RX slave frame of the same transaction"
        )

    # 3) Cross-transaction monotonicity: each transaction's master strictly follows
    #    the previous transaction's master in the stream (already implied by the
    #    strictly-increasing ids, but assert explicitly on the paired structure).
    for k in range(1, len(driven_master_positions)):
        prev_m = packets[driven_master_positions[k - 1]]
        cur_m = packets[driven_master_positions[k]]
        assert cur_m["id"] > prev_m["id"], (
            f"master for addr={seen_addrs[k]} (id={cur_m['id']}) does not strictly "
            f"follow master for addr={seen_addrs[k - 1]} (id={prev_m['id']})"
        )

    print(
        f"OK: bridge sniffer causal pairing verified for {expected_pairs} distinct "
        f"FC03 transactions {DRIVEN_ADDRS} — each master(TX) -> slave(RX) pair ordered "
        f"with strictly greater id and timestamp, no scrambling"
    )


# ===========================================================================
# BSC-01..03 — sniffer + cache co-active on one tcp_bridge port, across a
#              live cache enable/disable/re-enable cycle.
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(180)
def test_bridge_sniffer_cache_coactive_through_cache_cycle(api, gateway_p1_modbus):
    """Sniffer + cache active together on the same tcp_bridge port, surviving a
    live cache enable/disable/re-enable cycle with no disturbance to either the
    sniffer feed or the bridge round-trip."""
    slave = gateway_p1_modbus
    assert slave is not None, "gateway fixture must provide an RTU slave"

    addr, count = 10, 2

    # Preserve cache value-timeout so entries do not expire during the test;
    # restored in finally.
    saved = api.get_settings().json()
    orig_timeout = saved.get("cache_value_timeout_s", 0)

    ws = stop_ping = None
    driver = _GatewayDriver(GATEWAY_HOST, GATEWAY_HOST_PORT, addr=addr, count=count)
    try:
        # Keep cached values fresh for the whole run (no expiry races).
        resp = api.update_settings({"cache_value_timeout_s": 120})
        assert resp.status_code == 200, f"set cache_value_timeout_s failed: {resp.status_code}"

        # -------------------------------------------------------------------
        # Phase 1 (BSC-01): enable the cache overlay AND connect the WS sniffer
        # at the same time, then drive bridge traffic.
        # -------------------------------------------------------------------
        resp = api.set_port_cache(1, True)
        assert resp.status_code == 200, f"enable cache overlay failed: {resp.status_code}"

        # /info must report the cache overlay as enabled on port 1.
        info = api.get_info().json()
        assert info.get("rs485_1", {}).get("cache_enabled") is True, \
            "/info rs485_1.cache_enabled must be true with the cache overlay enabled"

        ws, stop_ping, _ = _ws_connect(api, 1)
        time.sleep(0.3)

        driver.start()

        # The sniffer must observe BOTH directions while the cache is also active.
        packets = _collect_packets(
            ws, min_count=6, timeout_sec=40,
            filter_fn=lambda p: p.get("type") == "packet",
        )
        masters = [p for p in packets if p.get("sender") == "master" and p.get("function") == 3]
        slaves = [p for p in packets if p.get("sender") == "slave"]
        assert masters, (
            f"Phase 1: sniffer did not observe the master request forwarded by the "
            f"bridge (TX feed) while the cache was co-active. packets={packets!r}"
        )
        assert slaves, (
            f"Phase 1: sniffer did not observe the slave response read by the bridge "
            f"(RX feed) while the cache was co-active. packets={packets!r}"
        )

        # And the value must land in the cache via GET /cache/json.
        match = _wait_cache_entry(api, addr, SLAVE_FAKE_VALUE, timeout_sec=40)
        assert match, (
            f"Phase 1: cached holding reg addr={addr} value=0x{SLAVE_FAKE_VALUE:04X} "
            f"never appeared in /cache/json while sniffer + cache were co-active"
        )
        assert not driver.errors, f"Phase 1: gateway driver errors: {driver.errors}"
        print(f"✓ BSC-01: co-active overlays — sniffer saw {len(masters)} master + "
              f"{len(slaves)} slave packets; cache populated reg {addr}=0x{SLAVE_FAKE_VALUE:04X}")

        # -------------------------------------------------------------------
        # Phase 2 (BSC-02): disable the cache overlay MID-RUN. The sniffer must
        # keep emitting and a fresh bridge round-trip must still succeed.
        # -------------------------------------------------------------------
        resp = api.set_port_cache(1, False)
        assert resp.status_code == 200, f"disable cache overlay mid-run failed: {resp.status_code}"
        info = api.get_info().json()
        assert info.get("rs485_1", {}).get("cache_enabled") is False, \
            "/info rs485_1.cache_enabled must be false after disabling the overlay mid-run"

        ok_before = driver.ok
        # Sniffer must keep delivering packets with the cache now off (driver still running).
        post_disable_pkts = _collect_packets(
            ws, min_count=4, timeout_sec=30,
            filter_fn=lambda p: p.get("type") == "packet",
        )
        assert len(post_disable_pkts) >= 4, (
            f"Phase 2: sniffer stopped emitting after the cache overlay was disabled "
            f"mid-run (got {len(post_disable_pkts)} packets); disabling the cache must "
            f"not disturb the live sniffer"
        )
        assert any(p.get("sender") == "slave" for p in post_disable_pkts), (
            f"Phase 2: sniffer saw no slave responses after cache disable; bridge RX "
            f"feed must keep flowing. packets={post_disable_pkts!r}"
        )

        # The bridge data path must be undisturbed: a fresh FC03 round-trip on an
        # independent connection still returns the correct value.
        result = query_register_once(
            GATEWAY_HOST, GATEWAY_HOST_PORT, slave_id=1, reg_type="holding", address=addr
        )
        assert result[0] == "ok", f"Phase 2: bridge round-trip failed after cache disable: {result}"
        assert result[1] == SLAVE_FAKE_VALUE, (
            f"Phase 2: bridge returned 0x{result[1]:04X}, expected 0x{SLAVE_FAKE_VALUE:04X} "
            f"— disabling the cache overlay disturbed the bridge data path"
        )
        # The continuously-running driver must have completed more transactions too.
        assert driver.ok > ok_before, (
            f"Phase 2: gateway driver made no progress after cache disable "
            f"(ok stayed at {driver.ok}); the bridge round-trip was disturbed"
        )
        assert not driver.errors, f"Phase 2: gateway driver errors: {driver.errors}"
        print(f"✓ BSC-02: cache disabled mid-run — sniffer kept emitting "
              f"({len(post_disable_pkts)} packets); bridge round-trip still 0x{result[1]:04X}")

        # -------------------------------------------------------------------
        # Phase 3 (BSC-03): re-enable the cache overlay; it must keep populating
        # from continued bridge traffic.
        # -------------------------------------------------------------------
        resp = api.set_port_cache(1, True)
        assert resp.status_code == 200, f"re-enable cache overlay failed: {resp.status_code}"
        info = api.get_info().json()
        assert info.get("rs485_1", {}).get("cache_enabled") is True, \
            "/info rs485_1.cache_enabled must be true after re-enabling the overlay"

        # Use a distinct value so we observe a FRESH population, not the stale Phase 1
        # entry: tell the slave to answer with a new value (most-recent-wins).
        new_value = 0x5A5A
        slave.fake_value = new_value
        match2 = _wait_cache_entry(api, addr, new_value, timeout_sec=40)
        assert match2, (
            f"Phase 3: cache did not repopulate reg addr={addr} with the new value "
            f"0x{new_value:04X} after the overlay was re-enabled (most-recent-wins). "
            f"current /cache/json rows={api.get_cache_json().json().get('d', [])!r}"
        )
        assert not driver.errors, f"Phase 3: gateway driver errors: {driver.errors}"
        print(f"✓ BSC-03: cache re-enabled — repopulated reg {addr}=0x{new_value:04X} "
              f"from continued bridge traffic")

    finally:
        driver.stop()
        driver.join(timeout=5.0)
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
        try:
            api.set_port_cache(1, False)
        except Exception:
            pass
        try:
            api.update_settings({"cache_value_timeout_s": orig_timeout})
        except Exception:
            pass

    # Final guard: the driver must have reported zero errors across all phases.
    assert not driver.errors, f"gateway driver errors across run: {driver.errors}"
    assert driver.ok >= 1, "no successful gateway transactions completed"


# ===========================================================================
# DPM-01 — PORT-2 cache feed populates the merged pool; readable end-to-end
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(120)
def test_port2_cache_feed_populates_pool(api):
    """With the cache overlay on PORT 2, FC03 and FC04 reads observed on UART2
    populate the shared pool and are readable via /cache/json and the cache
    Modbus TCP server (which is port-agnostic)."""
    _require_uart(2)

    host = _cache_host(api)

    # Save state we will mutate so teardown is exact.
    info = api.get_info().json()
    orig_mode_p2 = info.get("rs485_2", {}).get("port_mode", "disabled")

    settings = api.get_settings().json()
    orig_server = settings.get("cache_modbus_server_enabled", False)
    orig_port = settings.get("cache_modbus_port", 504)
    orig_timeout = settings.get("cache_value_timeout_s", 0)

    server_reachable = False
    try:
        # Configure the cache Modbus server (long value timeout so entries do not
        # expire mid-test).  Then check reachability — if it cannot be reached we
        # still verify the feed via /cache/json.
        resp = api.update_settings({
            "cache_modbus_server_enabled": True,
            "cache_modbus_port": QEMU_CACHE_MODBUS_PORT,
            "cache_value_timeout_s": 60,
        })
        assert resp.status_code == 200, f"configure cache server failed: {resp.status_code}"
        time.sleep(1.0)
        server_reachable = _cache_server_reachable_host(host)

        # Bring up PORT 2 as a passive cache-overlay port.
        resp = api.set_port_mode(2, "passive")
        assert resp.status_code == 200, f"set port 2 passive failed: {resp.status_code}"
        resp = api.set_port_cache(2, True)
        assert resp.status_code == 200, f"enable cache overlay on port 2 failed: {resp.status_code}"
        time.sleep(0.3)

        # Drive FC03 and FC04 reads on PORT 2 (observed on UART2).
        _inject_fc03(2, P2_FC03_ADDR, P2_FC03_VALUE)
        _inject_fc04(2, P2_FC04_ADDR, P2_FC04_VALUE)

        # Both must appear in /cache/json with their port-2 values.
        h = _wait_for_value(api, t="h", a=P2_FC03_ADDR, v=P2_FC03_VALUE)
        i = _wait_for_value(api, t="i", a=P2_FC04_ADDR, v=P2_FC04_VALUE)
        print(f"✓ DPM-01 /cache/json: holding {P2_FC03_ADDR}=0x{h['v']:04X}, "
              f"input {P2_FC04_ADDR}=0x{i['v']:04X} (fed from port 2)")

        # And readable through the cache Modbus TCP server (port-agnostic lookup).
        if server_reachable:
            r_h = query_register_once(host, QEMU_CACHE_MODBUS_PORT,
                                      slave_id=DPM_SLAVE_ID, reg_type="holding", address=P2_FC03_ADDR)
            assert r_h == ("ok", P2_FC03_VALUE), (
                f"cache server FC03 read of port-2 value failed: {r_h} "
                f"(expected ('ok', 0x{P2_FC03_VALUE:04X}))"
            )
            r_i = query_register_once(host, QEMU_CACHE_MODBUS_PORT,
                                      slave_id=DPM_SLAVE_ID, reg_type="input", address=P2_FC04_ADDR)
            assert r_i == ("ok", P2_FC04_VALUE), (
                f"cache server FC04 read of port-2 value failed: {r_i} "
                f"(expected ('ok', 0x{P2_FC04_VALUE:04X}))"
            )
            print(f"✓ DPM-01 cache server: holding={r_h[1]:#06x}, input={r_i[1]:#06x}")
        else:
            print("  DPM-01: cache Modbus server port not reachable — "
                  "verified via /cache/json only")

    finally:
        try:
            api.set_port_cache(2, False)
        except Exception:
            pass
        try:
            api.set_port_mode(2, orig_mode_p2)
        except Exception:
            pass
        try:
            api.update_settings({
                "cache_modbus_server_enabled": orig_server,
                "cache_modbus_port": orig_port,
                "cache_value_timeout_s": orig_timeout,
            })
        except Exception:
            pass


# ===========================================================================
# DPM-02/03/04 — dual-port merge: most-recent-wins, coexistence, pool survival
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(180)
def test_cache_single_port_takeover_moves_and_clears(api):
    """Single-port cache overlay: enabling it on a second port TAKES OVER and CLEARS.

    The cache is single-port by design (review #51): the overlay lives on exactly
    one port. Enabling it on another port hands the overlay over from the previous
    one and WIPES the pool (firmware: cache_move_locked() in
    main/bridge/port_manager.c logs "taking the cache overlay over from port N —
    the cache is single-port (review #51)" then cache_multimaster_clear()). The
    lookup is port-blind (slave_id, type, address only). This test verifies that
    real contract in both directions.

    (Rewritten from the former test_dual_port_merge_most_recent_wins_and_pool_survival,
    which asserted a port-merged pool — the architecture rejected at review #51.)
    """
    _require_uart(1)
    _require_uart(2)

    info = api.get_info().json()
    orig_mode_p1 = info.get("rs485_1", {}).get("port_mode", "disabled")
    orig_mode_p2 = info.get("rs485_2", {}).get("port_mode", "disabled")

    settings = api.get_settings().json()
    orig_timeout = settings.get("cache_value_timeout_s", 0)

    try:
        # Long value timeout so nothing expires while we step through the phases.
        resp = api.update_settings({"cache_value_timeout_s": 60})
        assert resp.status_code == 200, f"set cache_value_timeout_s failed: {resp.status_code}"

        # Both ports passive; enable the overlay on PORT 1 ONLY (single-port).
        for p in (1, 2):
            resp = api.set_port_mode(p, "passive")
            assert resp.status_code == 200, f"set port {p} passive failed: {resp.status_code}"
        resp = api.set_port_cache(1, True)
        assert resp.status_code == 200, f"enable cache overlay on port 1 failed: {resp.status_code}"
        time.sleep(0.3)

        # --- Phase 1: port 1 owns the overlay, records an observation ---------
        _inject_fc03(1, MERGE_ADDR, MERGE_VALUE_P1)
        _wait_for_value(api, t="h", a=MERGE_ADDR, v=MERGE_VALUE_P1)
        print(f"  port-1 observation recorded ({MERGE_ADDR}=0x{MERGE_VALUE_P1:04X})")

        # --- Phase 2: enabling on port 2 TAKES OVER and CLEARS the pool -------
        resp = api.set_port_cache(2, True)
        assert resp.status_code == 200, f"enable cache overlay on port 2 failed: {resp.status_code}"
        time.sleep(0.5)

        # The overlay is never torn down during the move (one port always wants it).
        st = api.get_cache_status()
        assert st.status_code == 200, f"GET /cache/status failed: {st.status_code}"
        assert st.json().get("enabled") is True, (
            "cache reported disabled after moving the overlay from port 1 to port 2; "
            "it must stay enabled throughout a single-port takeover"
        )
        # The port-1 entry must be GONE — the move wiped the pool.
        gone = _find_entry(api, t="h", a=MERGE_ADDR)
        assert gone is None, (
            f"port-1 entry survived the takeover to port 2, but the move clears the "
            f"single-port pool (review #51): {gone!r}"
        )
        print("✓ takeover port1→port2: overlay stayed enabled, pool cleared "
              "(port-1 entry gone)")

        # --- Phase 3: port 2 now owns the overlay; its traffic is recorded ----
        _inject_fc03(2, MERGE_ADDR, MERGE_VALUE_P2)
        owned = _wait_for_value(api, t="h", a=MERGE_ADDR, v=MERGE_VALUE_P2)
        rows = api.get_cache_json().json().get("d", [])
        same_key = [
            r for r in rows
            if r.get("s") == DPM_SLAVE_ID and r.get("t") == "h" and r.get("a") == MERGE_ADDR
        ]
        assert len(same_key) == 1, (
            f"single-port pool must hold exactly ONE entry for "
            f"(slave={DPM_SLAVE_ID}, holding, addr={MERGE_ADDR}); found {len(same_key)}: {same_key!r}"
        )
        assert owned["v"] == MERGE_VALUE_P2, (
            f"port-2 value not recorded after takeover: entry value 0x{owned['v']:04X}, "
            f"expected 0x{MERGE_VALUE_P2:04X}"
        )
        print(f"✓ port 2 owns the overlay: {MERGE_ADDR}=0x{owned['v']:04X} recorded")

        # --- Phase 4: port 1 no longer feeds the pool (overlay is off there) ---
        _inject_fc03(1, COEXIST_ADDR_P1, COEXIST_VALUE_P1)
        time.sleep(2.0)  # bounded wait — this observation must NOT be recorded
        leaked = _find_entry(api, t="h", a=COEXIST_ADDR_P1)
        assert leaked is None, (
            f"port-1 traffic was recorded although the overlay moved to port 2: "
            f"{leaked!r} (port 1 no longer feeds the single-port pool)"
        )
        print(f"✓ port-1 traffic ignored after takeover (addr {COEXIST_ADDR_P1} absent)")

        # --- Phase 5: hand the overlay BACK to port 1 -> clears again ---------
        resp = api.set_port_cache(1, True)
        assert resp.status_code == 200, f"re-enable cache overlay on port 1 failed: {resp.status_code}"
        time.sleep(0.5)
        cleared = _find_entry(api, t="h", a=MERGE_ADDR)
        assert cleared is None, (
            f"port-2 entry survived handing the overlay back to port 1: {cleared!r} "
            f"(the reverse move must clear the pool too)"
        )
        _inject_fc03(1, SURVIVE_ADDR, SURVIVE_VALUE)
        surv = _wait_for_value(api, t="h", a=SURVIVE_ADDR, v=SURVIVE_VALUE)
        assert surv["v"] == SURVIVE_VALUE, (
            "port-1 traffic did not update the pool after the overlay returned to it: "
            f"addr {SURVIVE_ADDR} value=0x{surv['v']:04X}, expected 0x{SURVIVE_VALUE:04X}"
        )
        print(f"✓ takeover port2→port1: pool cleared again, port-1 traffic now recorded "
              f"({SURVIVE_ADDR}=0x{surv['v']:04X})")

    finally:
        for p in (1, 2):
            try:
                api.set_port_cache(p, False)
            except Exception:
                pass
        try:
            api.set_port_mode(1, orig_mode_p1)
        except Exception:
            pass
        try:
            api.set_port_mode(2, orig_mode_p2)
        except Exception:
            pass
        try:
            api.update_settings({"cache_value_timeout_s": orig_timeout})
        except Exception:
            pass


# ===========================================================================
# TBC-01 — Cache populates through the transparent bridge path
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(120)
def test_transparent_bridge_cache_populates(api, transparent_p1):
    """A valid FC03 request/response byte pair pushed through a TRANSPARENT bridge
    populates the cache, visible via GET /cache/json.

    Direction of feeds:
      - Request bytes sent by the TCP client are forwarded UART-ward by the
        firmware (TX sniffer feed) -> cache sees the master request (address).
      - Response bytes injected into the UART chardev are forwarded TCP-ward
        (RX sniffer feed) -> cache sees the slave reply (value) and pairs them.

    The transparent bridge never reframes Modbus; the cache parses the raw bytes
    itself, which is exactly the property under test.
    """
    assert transparent_p1 is None, "transparent bridge fixture must be non-modbus (yields None)"

    # Save and extend the cache value timeout so entries cannot expire mid-test.
    saved = api.get_settings().json()
    orig_timeout = saved.get("cache_value_timeout_s", 0)

    request_frame = build_fc03_request(CACHE_SLAVE, CACHE_ADDR, 1)
    response_frame = build_fc03_response(CACHE_SLAVE, 1, CACHE_VALUE)

    tcp_sock = None
    uart_sock = None
    try:
        resp = api.update_settings({"cache_value_timeout_s": 60})
        assert resp.status_code == 200, f"set cache_value_timeout_s failed: {resp.status_code}"

        # Enable the cache overlay on the (transparent) tcp_bridge port.
        resp = api.set_port_cache(1, True)
        assert resp.status_code == 200, f"enable cache overlay failed: {resp.status_code}"

        # /info must report the overlay as enabled.
        info = api.get_info().json()
        assert info.get("rs485_1", {}).get("cache_enabled") is True, \
            "/info rs485_1.cache_enabled must be true after enabling the overlay"

        # A TCP client on the transparent bridge: its bytes become UART TX.
        ready = _poll_tcp_connect(GATEWAY_HOST, TRANSPARENT_PORT1_HOST_PORT, timeout=5.0)
        assert ready, "transparent bridge port not ready"
        tcp_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        tcp_sock.settimeout(5.0)
        tcp_sock.connect((GATEWAY_HOST, TRANSPARENT_PORT1_HOST_PORT))

        # Direct UART chardev socket for injecting the slave response (UART RX).
        uart_sock = open_uart_socket(1)

        # Drive request->response byte pairs until the cache records the entry.
        # Re-emitting each cycle makes the test robust to a single missed
        # request/response pairing without relying on timing races.
        deadline = time.monotonic() + 60
        entries = 0
        while time.monotonic() < deadline:
            # 1) Master request goes out via the transparent bridge (TX feed).
            tcp_sock.sendall(request_frame)
            # 2) Slave response comes in via the UART chardev (RX feed).
            inject_bytes(port=1, data=response_frame, sock=uart_sock)

            # Drain anything the bridge forwarded back to the TCP client so its
            # socket buffer does not fill; the value is irrelevant here.
            tcp_sock.settimeout(0.3)
            try:
                while True:
                    if not tcp_sock.recv(256):
                        break
            except socket.timeout:
                pass
            tcp_sock.settimeout(5.0)

            st = api.get_cache_status()
            if st.status_code == 200 and st.json().get("entries", 0) >= 1:
                entries = st.json()["entries"]
                break
            time.sleep(0.5)

        assert entries >= 1, "cache never populated from transparent-bridge traffic within 60 s"

        # The cached holding register must be visible in GET /cache/json with the
        # exact address from the request and the exact value from the response.
        cj = api.get_cache_json()
        assert cj.status_code == 200, f"GET /cache/json failed: {cj.status_code}"
        rows = cj.json().get("d", [])
        match = [
            r for r in rows
            if r.get("s") == CACHE_SLAVE
            and r.get("t") == "h"
            and r.get("a") == CACHE_ADDR
            and r.get("v") == CACHE_VALUE
        ]
        assert match, (
            f"cached holding reg slave={CACHE_SLAVE} addr={CACHE_ADDR} "
            f"value=0x{CACHE_VALUE:04X} not in /cache/json; rows={rows!r}"
        )
        print(f"✓ TBC-01: transparent bridge cache populated ({entries} entries); "
              f"reg {CACHE_ADDR}=0x{CACHE_VALUE:04X} in /cache/json")

    finally:
        if tcp_sock is not None:
            try:
                tcp_sock.close()
            except OSError:
                pass
        if uart_sock is not None:
            try:
                uart_sock.close()
            except OSError:
                pass
        try:
            api.set_port_cache(1, False)
        except Exception:
            pass
        try:
            api.update_settings({"cache_value_timeout_s": orig_timeout})
        except Exception:
            pass


# ===========================================================================
# TBC-02 — Toggling the cache overlay does not alter transparent forwarding
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(90)
def test_transparent_bridge_cache_toggle_roundtrip_unchanged(api, transparent_p1):
    """The byte-for-byte transparent round-trip is unaffected by cache toggling.

    A UART echo thread reflects whatever the firmware writes to the bus, so a
    TCP client connected to the transparent bridge sees its own bytes returned.
    The same fixed payload must round-trip identically:
      (a) with the cache overlay ENABLED,
      (b) after DISABLING it,
      (c) after RE-ENABLING it.
    The additive overlay must never mutate the transparent data path.
    """
    assert transparent_p1 is None, "transparent bridge fixture must be non-modbus (yields None)"

    # A representative Modbus frame doubles as an arbitrary opaque payload here;
    # the transparent bridge must return it byte-for-byte regardless of content.
    payload = bytes([0x01, 0x03, 0x00, 0x00, 0x00, 0x0A, 0xC5, 0xCD])

    echo = _UartEchoThread(GATEWAY_HOST, UART1_TCP_PORT)
    try:
        ready = _poll_tcp_connect(GATEWAY_HOST, TRANSPARENT_PORT1_HOST_PORT, timeout=5.0)
        assert ready, "transparent bridge port not ready"

        echo.start()
        assert echo.wait_connected(timeout=5.0), "echo thread could not connect to UART1"

        # (a) Cache ENABLED — round-trip must be byte-for-byte unchanged.
        resp = api.set_port_cache(1, True)
        assert resp.status_code == 200, f"enable cache overlay failed: {resp.status_code}"
        got = _roundtrip_once(GATEWAY_HOST, TRANSPARENT_PORT1_HOST_PORT, payload)
        assert got == payload, (
            f"round-trip altered with cache ENABLED: sent={payload.hex()!r} got={got.hex()!r}"
        )

        # (b) Cache DISABLED — round-trip must still be byte-for-byte unchanged.
        resp = api.set_port_cache(1, False)
        assert resp.status_code == 200, f"disable cache overlay failed: {resp.status_code}"
        got = _roundtrip_once(GATEWAY_HOST, TRANSPARENT_PORT1_HOST_PORT, payload)
        assert got == payload, (
            f"round-trip altered after DISABLING cache: sent={payload.hex()!r} got={got.hex()!r}"
        )

        # (c) Cache RE-ENABLED — round-trip must remain byte-for-byte unchanged.
        resp = api.set_port_cache(1, True)
        assert resp.status_code == 200, f"re-enable cache overlay failed: {resp.status_code}"
        got = _roundtrip_once(GATEWAY_HOST, TRANSPARENT_PORT1_HOST_PORT, payload)
        assert got == payload, (
            f"round-trip altered after RE-ENABLING cache: sent={payload.hex()!r} got={got.hex()!r}"
        )

        print("✓ TBC-02: transparent round-trip byte-for-byte unchanged across "
              "cache enable/disable/re-enable")

    finally:
        echo.stop()
        echo.join(timeout=3.0)
        try:
            api.set_port_cache(1, False)
        except Exception:
            pass


# ===========================================================================
# CTM-01 — Cache coherency when toggled mid-traffic on a gateway bridge
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(180)
def test_cache_toggle_mid_traffic_serves_fresh_value(api, gateway_p1_modbus_toggle):
    """Toggling the cache overlay off then on, while register A changes value on
    the bus, must leave the cache serving the FRESH post-re-enable value — never
    a stale pre-disable value and never a stuck pending request — and the gateway
    bridge must report zero errors throughout."""
    slave = gateway_p1_modbus_toggle
    assert slave is not None, "gateway fixture must provide an RTU slave"

    if not _cache_server_reachable():
        pytest.skip(
            f"cache Modbus server port {CACHE_MODBUS_HOST_PORT} not reachable"
        )

    # Preserve cache-server settings so teardown restores them exactly.
    saved = api.get_settings().json()
    orig_server = saved.get("cache_modbus_server_enabled", False)
    orig_port = saved.get("cache_modbus_port", 504)
    orig_timeout = saved.get("cache_value_timeout_s", 0)

    conn = _GatewayConn(GATEWAY_HOST, GATEWAY_HOST_PORT)
    try:
        # Enable the cache Modbus TCP server on the host-forwarded port with a
        # long value timeout so entries never expire mid-test (staleness here
        # would be a false negative, not the bug we are hunting).
        resp = api.update_settings({
            "cache_modbus_server_enabled": True,
            "cache_modbus_port": CACHE_MODBUS_HOST_PORT,
            "cache_value_timeout_s": 60,
        })
        assert resp.status_code == 200, f"configure cache server failed: {resp.status_code}"
        time.sleep(1.0)  # let the cache server rebind on the new port

        conn.connect()

        # ---- Phase 1: cache ENABLED, bus carries VALUE1 -> cache holds VALUE1
        resp = api.set_port_cache(1, True)
        assert resp.status_code == 200, f"enable cache overlay failed: {resp.status_code}"

        slave.fake_value = VALUE1
        assert conn.drive_until_value(VALUE1), (
            "bus never reported VALUE1 through the gateway in phase 1"
        )
        assert _poll_cache_json_value(api, HOLDING_ADDR, VALUE1, deadline_s=40), (
            f"/cache/json never showed VALUE1 (0x{VALUE1:04X}) for holding addr "
            f"{HOLDING_ADDR} while the cache was enabled in phase 1"
        )

        # Sanity: the cache Modbus server also serves VALUE1 now.
        res = query_register_once(
            GATEWAY_HOST, CACHE_MODBUS_HOST_PORT,
            slave_id=SLAVE_ID, reg_type="holding", address=HOLDING_ADDR,
        )
        assert res[0] == "ok" and res[1] == VALUE1, (
            f"cache server phase-1 read expected VALUE1 (0x{VALUE1:04X}), got {res}"
        )
        print(f"✓ phase 1: cache holds VALUE1=0x{VALUE1:04X}")

        # ---- Phase 2: cache DISABLED, bus changes to VALUE2 (must NOT be recorded)
        resp = api.set_port_cache(1, False)
        assert resp.status_code == 200, f"disable cache overlay failed: {resp.status_code}"
        time.sleep(0.5)  # let the firmware apply the overlay-disable

        slave.fake_value = VALUE2
        # Drive several real transactions while the cache is OFF. The bridge must
        # keep forwarding (these update conn.ok/errors) but the cache must not
        # record the new value — it should stay at VALUE1 (or have been torn down).
        for _ in range(6):
            assert conn.drive_once() == VALUE2, "bus did not report VALUE2 while cache disabled"
        cached_while_off = _cache_json_holding_value(api, HOLDING_ADDR)
        assert cached_while_off in (None, VALUE1), (
            f"cache updated to 0x{cached_while_off:04X} while the overlay was "
            f"DISABLED — the disabled cache must not record live traffic"
        )
        print("✓ phase 2: cache did not record traffic while disabled")

        # ---- Phase 3: cache RE-ENABLED, drive VALUE2 -> cache must serve VALUE2
        resp = api.set_port_cache(1, True)
        assert resp.status_code == 200, f"re-enable cache overlay failed: {resp.status_code}"

        # Bus still carries VALUE2; drive it again so the re-enabled cache records it.
        assert conn.drive_until_value(VALUE2), (
            "bus never reported VALUE2 through the gateway in phase 3"
        )
        assert _poll_cache_json_value(api, HOLDING_ADDR, VALUE2, deadline_s=40), (
            f"/cache/json did not refresh to VALUE2 (0x{VALUE2:04X}) for holding "
            f"addr {HOLDING_ADDR} after re-enabling the cache — the cache is "
            f"serving a STALE value or is stuck on a pending request"
        )

        # The cache Modbus TCP server must also serve the FRESH VALUE2.
        res = query_register_once(
            GATEWAY_HOST, CACHE_MODBUS_HOST_PORT,
            slave_id=SLAVE_ID, reg_type="holding", address=HOLDING_ADDR,
        )
        assert res[0] == "ok", f"cache server phase-3 read failed: {res}"
        assert res[1] == VALUE2, (
            f"cache server returned stale 0x{res[1]:04X}, expected fresh "
            f"VALUE2 (0x{VALUE2:04X}) after the cache was toggled off then on"
        )
        print(f"✓ phase 3: cache refreshed to VALUE2=0x{VALUE2:04X} (json + cache server)")

        # ---- Bridge stayed uninterrupted across every toggle.
        assert not conn.errors, f"gateway bridge errors across cache toggles: {conn.errors}"
        assert conn.ok >= 3, (
            f"too few successful gateway transactions (ok={conn.ok}); the bridge "
            f"data path may have been interrupted by a cache toggle"
        )
        print(f"✓ bridge uninterrupted: {conn.ok} successful transactions, 0 errors")

    finally:
        conn.close()
        try:
            api.set_port_cache(1, False)
        except Exception:
            pass
        try:
            api.update_settings({
                "cache_modbus_server_enabled": orig_server,
                "cache_modbus_port": orig_port,
                "cache_value_timeout_s": orig_timeout,
            })
        except Exception:
            pass


# ===========================================================================
# Cache broadcast guard — a broadcast read must never become a cached entry
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(90)
def test_broadcast_read_not_cached(api):
    """A broadcast FC03 (slave 0, unanswered) must not create any cache entry;
    a following unicast FC03 must be cached normally with its correct value."""
    if not _uart_reachable(1):
        pytest.skip(
            f"Cannot connect to UART chardev TCP port {UART_TCP_PORT[1]}. "
            "QEMU may not expose this UART as TCP in this configuration."
        )

    # Save original transport and cache-timeout settings so teardown restores them.
    info_resp = api.get_info()
    assert info_resp.status_code == 200, f"GET /info failed: {info_resp.status_code}"
    original_mode = info_resp.json().get("rs485_1", {}).get("port_mode", "disabled")

    settings_resp = api.get_settings()
    assert settings_resp.status_code == 200, f"GET /settings failed: {settings_resp.status_code}"
    original_value_timeout = settings_resp.json().get("cache_value_timeout_s", 60)

    try:
        # Long value timeout so freshly cached entries never expire mid-test.
        resp = api.update_settings({"cache_value_timeout_s": 60})
        assert resp.status_code == 200, f"Failed to set cache_value_timeout_s: {resp.status_code}"

        # Passive transport keeps the serial port open so the sniffer/cache can
        # observe injected traffic without a bridge consuming it.
        resp = api.set_port_mode(1, PASSIVE)
        assert resp.status_code == 200, f"Failed to set port 1 passive: {resp.status_code}"
        time.sleep(0.5)

        # Enable the cache overlay on port 1 (calls cache_multimaster_enable()).
        resp = api.set_port_cache(1, True)
        assert resp.status_code == 200, f"Failed to enable cache overlay on port 1: {resp.status_code}"
        info = api.get_info().json()
        assert info.get("rs485_1", {}).get("cache_enabled") is True, \
            "/info rs485_1.cache_enabled must be true after enabling the overlay"
        time.sleep(0.5)

        # Inject all traffic over one shared UART socket so byte ordering on the
        # bus is deterministic: the broadcast request lands first, then the full
        # unicast request+response exchange.
        uart_sock = open_uart_socket(1)
        try:
            # 1) Broadcast FC03 request (slave 0) — NO response follows it, exactly
            #    as a real Modbus broadcast read would appear on the wire.
            inject_bytes(
                port=1,
                data=build_fc03_request(0x00, BROADCAST_ADDR, BROADCAST_COUNT),
                sock=uart_sock,
            )
            # Small gap so the firmware's idle-delimited RX treats the broadcast as
            # a complete standalone frame before the unicast exchange arrives.
            time.sleep(0.2)

            # 2) Unicast FC03 request+response (slave 1) carrying UNICAST_VALUE.
            inject_bytes(
                port=1,
                data=build_fc03_exchange(
                    UNICAST_SLAVE, UNICAST_ADDR, UNICAST_COUNT, UNICAST_VALUE
                ),
                sock=uart_sock,
            )
        finally:
            uart_sock.close()

        # Deterministic wait: poll /cache/status until the unicast entry lands.
        # (The unicast exchange is the only traffic that can produce an entry.)
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            st = api.get_cache_status()
            if st.status_code == 200 and st.json().get("entries", 0) >= 1:
                break
            time.sleep(1.0)

        st = api.get_cache_status()
        assert st.status_code == 200, f"GET /cache/status failed: {st.status_code}"
        assert st.json().get("enabled"), "cache overlay not enabled after set_port_cache(1, True)"
        assert st.json().get("entries", 0) >= 1, \
            "cache never populated from the unicast exchange within 30 s"

        # Inspect the merged map via /cache/json: {"d":[{s,t,a,v,age},...]}.
        cj = api.get_cache_json()
        assert cj.status_code == 200, f"GET /cache/json failed: {cj.status_code}"
        rows = cj.json().get("d", [])
        assert isinstance(rows, list), "/cache/json field 'd' must be an array"

        # (a) The unicast holding-register entry must be present with the right value.
        unicast_match = [
            r for r in rows
            if r.get("s") == UNICAST_SLAVE and r.get("t") == "h"
            and r.get("a") == UNICAST_ADDR and r.get("v") == UNICAST_VALUE
        ]
        assert unicast_match, (
            f"unicast holding reg slave={UNICAST_SLAVE} addr={UNICAST_ADDR} "
            f"value=0x{UNICAST_VALUE:04X} missing from /cache/json; rows={rows!r}"
        )

        # (b) No entry may be attributable to the broadcast.  The broadcast has no
        #     response, so neither slave_id 0 nor its register address may appear.
        broadcast_slave_entries = [r for r in rows if r.get("s") == 0]
        assert not broadcast_slave_entries, (
            "broadcast (slave_id 0) created cache entries — a broadcast read must "
            f"never be cached; offending rows={broadcast_slave_entries!r}. "
            "SUSPECTED FIRMWARE BUG: an unanswered broadcast corrupts the cache map."
        )

        broadcast_addr_entries = [
            r for r in rows
            if r.get("t") == "h"
            and BROADCAST_ADDR <= r.get("a", -1) < BROADCAST_ADDR + BROADCAST_COUNT
        ]
        assert not broadcast_addr_entries, (
            f"a holding-register entry appeared at the broadcast address range "
            f"[{BROADCAST_ADDR}, {BROADCAST_ADDR + BROADCAST_COUNT}) although the "
            f"broadcast was never answered; offending rows={broadcast_addr_entries!r}. "
            "SUSPECTED FIRMWARE BUG: an unanswered broadcast created a phantom entry."
        )

        print(
            f"✓ broadcast guard: unicast slave={UNICAST_SLAVE} addr={UNICAST_ADDR} "
            f"=0x{UNICAST_VALUE:04X} cached; no broadcast (slave 0 / addr "
            f"{BROADCAST_ADDR}) entry present ({len(rows)} total rows)"
        )

    finally:
        # Always disable the cache overlay and restore transport + settings.
        try:
            api.set_port_cache(1, False)
        except Exception:
            pass
        try:
            api.set_port_mode(1, original_mode)
        except Exception:
            pass
        try:
            api.update_settings({"cache_value_timeout_s": original_value_timeout})
        except Exception:
            pass
