"""End-to-end tests for the additive sniffer/cache overlays on a tcp_bridge port
(part 2 — serial-layer feed). These prove the headline feature: a port can run a
tcp_bridge AND have the sniffer/cache observe traffic in parallel.

What part 2 added at the serial layer:
  - RX: every received frame is delivered to the sniffer (idle-delimited) regardless
    of whether a bridge receive_handler is set (so the sniffer works on a bridge too).
  - TX: bytes the firmware writes to the UART via serial_send() (the bridge / gateway
    forwarding the master request to the bus) are delivered to the sniffer too.

Coverage here:
  BO-01  Bridge + sniffer (RX+TX): a Modbus-gateway tcp_bridge forwards a master
         request to the bus (TX) and the slave responds (RX); the sniffer WS overlay
         observes BOTH sender=="master" AND sender=="slave".
  BO-02  Bridge + cache: with the cache overlay enabled on a tcp_bridge port, an
         FC03 request->response transaction driven through the gateway populates the
         cache, visible via GET /cache/json and readable through the cache Modbus
         TCP server (R6).
  BO-03  Regression (R3): a transparent tcp_bridge with NO sniffer/cache active still
         forwards bytes unchanged (round-trip), confirming the additive feed never
         alters the bridge data path.

Requires QEMU with the UART1 chardev on TCP, gateway guest port 502 forwarded to a host
port, and guest port 50504 forwarded to a host port (transparent bridge / cache Modbus
server). Host ports follow WB_MGE_PORT_SLOT (api_tests/qemu_ports.py).
"""

import qemu_ports
import socket
import threading
import time

import pytest

from conftest import build_gateway_fixture
from modbus_helpers import make_mbap_request, send_and_receive, query_register_once
from sniffer_helpers import _ws_connect, _collect_packets


# ---------------------------------------------------------------------------
# Constants (must match conftest.py qemu_process hostfwd mapping)
# ---------------------------------------------------------------------------
GATEWAY_HOST = qemu_ports.GATEWAY_HOST
GATEWAY_HOST_PORT = qemu_ports.GATEWAY_HOST_PORT  # QEMU hostfwd: slot host port -> guest 502 (Modbus gateway)
CACHE_MODBUS_HOST_PORT = qemu_ports.CACHE_MODBUS_HOST_PORT  # cache Modbus TCP server (slot host port -> guest 50504)
UART1_TCP_PORT = qemu_ports.UART1_TCP_PORT  # QEMU UART1 (RS485-1) chardev
SLAVE_FAKE_VALUE = 0x1234               # value the mock RTU slave returns for every register


# A Modbus-gateway tcp_bridge on RS-485 port 1 with a mock RTU slave on UART1.
# Yields the ModbusRtuSlaveThread.
gateway_p1_modbus = build_gateway_fixture(
    port_num=1,
    uart_tcp_port=UART1_TCP_PORT,
    bridge_port=qemu_ports.GATEWAY_GUEST_PORT,      # guest 502
    modbus=True,
    fake_value=SLAVE_FAKE_VALUE,
)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

class _GatewayDriver(threading.Thread):
    """Continuously issue FC03 reads through the Modbus-gateway tcp_bridge.

    Each request makes the firmware forward an RTU master request to the bus
    (exercising the TX sniffer feed) and the mock slave reply (exercising the
    RX sniffer feed), so the sniffer/cache see complete transactions.
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


# ===========================================================================
# BO-01 — Bridge + sniffer observes BOTH directions (RX + TX)
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(90)
def test_bridge_sniffer_observes_master_and_slave(api, gateway_p1_modbus):
    """A Modbus-gateway tcp_bridge + WS sniffer overlay: the sniffer observes the
    master request the firmware forwards to the bus (TX feed) AND the slave response
    it reads back (RX feed)."""
    slave = gateway_p1_modbus
    assert slave is not None, "gateway fixture must provide an RTU slave"

    ws = stop_ping = None
    driver = _GatewayDriver(GATEWAY_HOST, GATEWAY_HOST_PORT, addr=10, count=2)
    try:
        # Sniffer DISPLAY overlay on the (already tcp_bridge) port 1.
        ws, stop_ping, _ = _ws_connect(api, 1)
        time.sleep(0.3)

        # Drive request->response transactions through the gateway while collecting.
        driver.start()
        packets = _collect_packets(
            ws, min_count=6, timeout_sec=30,
            filter_fn=lambda p: p.get("type") == "packet",
        )
    finally:
        driver.stop()
        driver.join(timeout=5.0)
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

    assert not driver.errors, f"gateway driver errors: {driver.errors}"
    assert driver.ok >= 1, "no successful gateway transactions completed"

    masters = [p for p in packets if p.get("sender") == "master" and p.get("function") == 3]
    slaves = [p for p in packets if p.get("sender") == "slave"]
    assert masters, (
        f"sniffer did not observe the master request forwarded by the bridge (TX feed). "
        f"packets={packets!r}"
    )
    assert slaves, (
        f"sniffer did not observe the slave response read by the bridge (RX feed). "
        f"packets={packets!r}"
    )
    print(f"✓ BO-01: bridge sniffer saw {len(masters)} master + {len(slaves)} slave packets")


# ===========================================================================
# BO-02 — Bridge + cache: transaction populates the cache, readable end-to-end
# ===========================================================================

@pytest.mark.qemu
# 165 s, not 120 s: an item's pytest-timeout budget covers setup + call + TEARDOWN, and
# module-scoped fixtures are torn down inside the LAST item of the module. This is that
# item, so it also pays conftest's _restore_rs485_settings teardown — up to two bounded
# POST /settings plus a settle window (2 x 20.1 s + 1 s = 41.2 s, see _RS485_HTTP_TIMEOUT).
# This module defines no module-scoped fixtures of its own and gateway_p1_modbus (:52) is
# function-scoped (built by conftest.build_gateway_fixture), so the conftest restore is the
# whole module teardown. 120 s body + 45 s teardown allowance.
@pytest.mark.timeout(165)
def test_bridge_cache_populates_and_reads_back(api, gateway_p1_modbus):
    """With the cache overlay enabled on a Modbus-gateway tcp_bridge port, an FC03
    request->response transaction driven through the gateway populates the cache.
    The value is visible via GET /cache/json and readable via the cache Modbus TCP
    server (R6)."""
    slave = gateway_p1_modbus
    assert slave is not None

    # Skip if the cache Modbus server port is not reachable in this QEMU config.
    probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    probe.settimeout(3.0)
    reachable = probe.connect_ex((GATEWAY_HOST, CACHE_MODBUS_HOST_PORT)) == 0
    probe.close()
    if not reachable:
        pytest.skip(f"cache Modbus server port {CACHE_MODBUS_HOST_PORT} not reachable")

    saved = api.get_settings().json()
    orig_server = saved.get("cache_modbus_server_enabled", False)
    orig_port = saved.get("cache_modbus_port", 504)
    orig_timeout = saved.get("cache_value_timeout_s", 0)

    addr, count = 20, 2
    driver = _GatewayDriver(GATEWAY_HOST, GATEWAY_HOST_PORT, addr=addr, count=count)
    try:
        # Enable the cache Modbus TCP server on the forwarded port; long timeout so
        # entries do not expire during the test.
        resp = api.update_settings({
            "cache_modbus_server_enabled": True,
            "cache_modbus_port": qemu_ports.CACHE_MODBUS_GUEST_PORT,
            "cache_value_timeout_s": 60,
        })
        assert resp.status_code == 200, f"configure cache server failed: {resp.status_code}"
        time.sleep(1.0)

        # Enable the cache overlay on the tcp_bridge port — the headline feature.
        resp = api.set_port_cache(1, True)
        assert resp.status_code == 200, f"enable cache overlay failed: {resp.status_code}"

        # Drive transactions through the bridge until the cache has the entry.
        driver.start()
        deadline = time.monotonic() + 40
        entries = 0
        while time.monotonic() < deadline:
            st = api.get_cache_status()
            if st.status_code == 200 and st.json().get("entries", 0) >= 1:
                entries = st.json()["entries"]
                break
            time.sleep(1.0)
        driver.stop()
        driver.join(timeout=5.0)

        assert not driver.errors, f"gateway driver errors: {driver.errors}"
        assert entries >= 1, "cache never populated from bridge traffic within 40 s"

        # The cached value must be visible via GET /cache/json ({"d":[{s,t,a,v,age},...]}).
        cj = api.get_cache_json()
        assert cj.status_code == 200, f"GET /cache/json failed: {cj.status_code}"
        rows = cj.json().get("d", [])
        match = [
            r for r in rows
            if r.get("a") == addr and r.get("t") == "h" and r.get("v") == SLAVE_FAKE_VALUE
        ]
        assert match, (
            f"cached holding reg addr={addr} value=0x{SLAVE_FAKE_VALUE:04X} not in /cache/json; "
            f"rows={rows!r}"
        )

        # And it must be readable through the cache Modbus TCP server (FC03).
        result = query_register_once(
            GATEWAY_HOST, CACHE_MODBUS_HOST_PORT, slave_id=1, reg_type="holding", address=addr
        )
        assert result[0] == "ok", f"cache server read failed: {result}"
        assert result[1] == SLAVE_FAKE_VALUE, (
            f"cache server returned 0x{result[1]:04X}, expected 0x{SLAVE_FAKE_VALUE:04X}"
        )
        print(f"✓ BO-02: bridge+cache populated ({entries} entries); "
              f"reg {addr}=0x{result[1]:04X} via cache server + /cache/json")

    finally:
        driver.stop()
        driver.join(timeout=5.0)
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
# Note: the transparent-bridge byte-for-byte round-trip regression (R3) that
# formerly lived here as BO-03 has been removed. It is fully covered by step (b)
# of test_transparent_bridge_cache_toggle_roundtrip_unchanged in
# 42_test_sniffer_cache_overlays_e2e.py (cache DISABLED -> same byte-for-byte
# round-trip with the same payload and UART echo setup), so re-running the
# expensive QEMU UART-echo + TCP socket setup here added no coverage.
# ===========================================================================
