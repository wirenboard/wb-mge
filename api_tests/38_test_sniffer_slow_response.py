"""E2E tests for sniffer response timing behaviour.

The sniffer in main/bridge/sniffer.c has a hardcoded SNIFFER_RESP_TIMEOUT_MS=200.
When a master request arrives it is emitted immediately over WebSocket.
The firmware still maintains an internal timeout timer (used by cache_multimaster),
but {type:"timeout"} packets are no longer forwarded to WebSocket clients.

Coverage:
SN-SR-01  Fast slave response (10 ms) — sniffer emits MASTER+SLAVE pair, no timeout.
SN-SR-02  Slow slave response (400 ms) — MASTER emitted immediately, then orphan SLAVE; no TIMEOUT.
SN-SR-03  No slave response (dead slave) — sniffer emits only MASTER, no TIMEOUT, no SLAVE.
SN-SR-04  Master emitted immediately — before slave responds.
SN-SR-05  No response → only MASTER arrives (no TIMEOUT, no SLAVE).
SN-SR-06  Slow response → MASTER + SLAVE (orphan, late) — no TIMEOUT in between.
SN-SR-07  Dead slave polled twice — both requests appear as MASTER packets; no TIMEOUT, no SLAVE.
"""

import json
import time

import pytest

from packet_injector import (
    build_fc03_request,
    build_fc03_response,
    inject_bytes,
    open_uart_socket,
)
from sniffer_helpers import _ws_connect, _collect_packets


# ---------------------------------------------------------------------------
# Module-level constants
# ---------------------------------------------------------------------------

SNIFFER_RESP_TIMEOUT_MS = 200       # Must match firmware sniffer.c constant
TEST_SLAVE_ID = 0x15                # Slave address used across all timing tests (21 decimal)


# ---------------------------------------------------------------------------
# Module-level baseline fixture
# ---------------------------------------------------------------------------

@pytest.fixture(scope="module", autouse=True)
def _baseline(api):
    """Set rs485_1 to a known state before any test in this module.

    tx_disabled=True is required for QEMU sniffer mode so that firmware does
    not try to drive the RS-485 TX line (see 12_test_sniffer_ws.py).
    The port is left in tcp_bridge mode; each individual test switches to
    sniffer mode and restores afterwards.
    """
    resp = api.update_settings({
        "rs485_1": {
            "tx_disabled": True,    # required for QEMU sniffer mode (see 12_test_sniffer_ws.py)
            "baudrate": 9600,
            "stopbits": "1",
            "parity": "none",
            "databits": "8",
        }
    })
    assert resp.status_code == 200, (
        f"_baseline: update_settings failed: {resp.status_code} {resp.text}"
    )
    resp = api.set_port_mode(1, "tcp_bridge")
    assert resp.status_code == 200, (
        f"_baseline: set_port_mode(1, tcp_bridge) failed: {resp.status_code} {resp.text}"
    )


# ---------------------------------------------------------------------------
# Shared helper: run a sniffer timing test with controlled request/response gap
# ---------------------------------------------------------------------------

def _run_sniffer_timing_test(api, delay_ms, collect_timeout_sec=5.0, min_count=1):
    """Set up sniffer on port 1, inject request + optional delayed response, collect packets.

    Parameters
    ----------
    api :
        pytest fixture — the API client.
    delay_ms : int | None
        Milliseconds to wait between injecting the request and injecting the
        response.  Pass None to skip injecting a response entirely (dead slave).
    collect_timeout_sec : float
        Total wall-clock seconds to collect WebSocket packets after injection.
    min_count : int
        _collect_packets stops early once this many matching packets arrive.

    Returns
    -------
    list[dict]
        All packets received from the sniffer WebSocket during collection.

    Notes
    -----
    - The method controls timing by sleeping between the two inject_bytes calls;
      the firmware sees them as separate UART frames because the inter-frame gap
      exceeds the RTU idle threshold.
    - Both request and response use slave=TEST_SLAVE_ID, FC03, addr=0x0000,
      count=1 register (value 0x4988).
    - Cleanup restores port 1 to its original mode unconditionally in a finally
      block, matching the pattern used in SN-02 and SN-05 in 32_test_transparent_sniffer.py.
    """
    # Save original port mode for cleanup
    resp = api.get_info()
    assert resp.status_code == 200, f"GET /info failed: {resp.status_code}"
    original_mode = resp.json().get("rs485_1", {}).get("port_mode", "disabled")

    ws = None
    stop_ping = None
    uart_sock = None

    try:
        # Open serial (passive transport) for the WS sniffer overlay
        resp = api.set_port_mode(1, "passive")
        assert resp.status_code == 200, (
            f"Failed to set port 1 to sniffer: {resp.status_code}"
        )
        time.sleep(0.5)  # allow sniffer task to initialise

        # Connect WebSocket and start sniffer stream
        ws, stop_ping, _ = _ws_connect(api, 1)
        time.sleep(0.2)  # allow WS handshake to complete

        # Open UART socket for raw byte injection
        try:
            uart_sock = open_uart_socket(port=1)
        except OSError as exc:
            pytest.skip(f"UART1 chardev not reachable: {exc}")

        # Build Modbus frames
        req = build_fc03_request(slave=TEST_SLAVE_ID, start_addr=0x0000, reg_count=1)
        resp_frame = build_fc03_response(slave=TEST_SLAVE_ID, reg_count=1, base_value=0x4988)

        # Inject the master request
        inject_bytes(port=1, data=req, sock=uart_sock)

        if delay_ms is not None:
            # Sleep between request and response; the firmware sees them as
            # separate UART frames because the gap is longer than the RTU
            # inter-frame idle threshold.
            time.sleep(delay_ms / 1000.0)
            # Inject the slave response
            inject_bytes(port=1, data=resp_frame, sock=uart_sock)

        # Collect all packets emitted during the observation window.
        # No filter_fn here — callers inspect the full list themselves.
        packets = _collect_packets(
            ws,
            min_count=min_count,
            timeout_sec=collect_timeout_sec,
            filter_fn=None,
        )
        return packets

    finally:
        # Close UART socket
        if uart_sock is not None:
            try:
                uart_sock.close()
            except OSError:
                pass
        # Stop ping thread
        if stop_ping is not None:
            stop_ping.set()
        # Close WebSocket with stop command
        if ws is not None:
            try:
                ws.send(json.dumps({"cmd": "stop", "port": 1}))
            except Exception:
                pass
            try:
                ws.close()
            except Exception:
                pass
        # Restore original port mode
        restore_resp = api.set_port_mode(1, original_mode)
        assert restore_resp.status_code == 200, (
            f"Failed to restore port 1 mode to {original_mode!r}: "
            f"{restore_resp.status_code}"
        )


# ===========================================================================
# SN-SR-01: Fast slave response (10 ms) — MASTER+SLAVE pair, no timeout
# ===========================================================================

@pytest.mark.qemu
# 45 s, not 20 s: an item's pytest-timeout budget covers SETUP as well as the call, and
# this is the first item of the module, so it pays the module-scoped _baseline above
# (POST /settings + POST /ports/1/mode). When this file is run on its own it additionally
# pays conftest's once-per-session rs485 snapshot (one bounded GET /settings, 20.1 s, see
# _RS485_HTTP_TIMEOUT) — in a full-suite run that lands on the very first item of the
# session instead. 20 s body + 20.1 s snapshot + slack.
@pytest.mark.timeout(45)
def test_sniffer_fast_response_master_slave_pair(api):
    """Fast slave response (10 ms) — sniffer emits MASTER+SLAVE pair, no timeout.

    Injects FC03 request for slave=0x15 then, after only 10 ms (well inside
    SNIFFER_RESP_TIMEOUT_MS=200), injects the matching FC03 response.

    Expected outcome:
    - At least one {type:"packet", sender:"master", slave_id:0x15, function:3}
    - At least one {type:"packet", sender:"slave",  slave_id:0x15, function:3}
    - No {type:"timeout"} packet for slave_id==0x15

    With the immediate-master behavior the sniffer emits the master packet as
    soon as the request arrives; the slave packet is emitted when the response
    comes in. Both arrive well within the 3-second collection window.
    """
    # 10 ms delay — well before the 200 ms sniffer timeout
    # collect up to 3.0 s, stop as soon as 2 packets (master + slave) arrive
    packets = _run_sniffer_timing_test(api, delay_ms=10, collect_timeout_sec=3.0, min_count=2)

    # Filter for our slave ID
    our_pkts = [p for p in packets if p.get("slave_id") == TEST_SLAVE_ID]

    master_pkts = [
        p for p in our_pkts
        if p.get("type") == "packet" and p.get("sender") == "master" and p.get("function") == 3
    ]
    slave_pkts = [
        p for p in our_pkts
        if p.get("type") == "packet" and p.get("sender") == "slave" and p.get("function") == 3
    ]
    timeout_pkts = [
        p for p in our_pkts
        if p.get("type") == "timeout"
    ]

    assert len(master_pkts) >= 1, (
        f"SN-SR-01: expected >=1 master packet for slave_id=0x{TEST_SLAVE_ID:02X} "
        f"but got 0. All packets: {packets}"
    )
    assert len(slave_pkts) >= 1, (
        f"SN-SR-01: expected >=1 slave packet for slave_id=0x{TEST_SLAVE_ID:02X} "
        f"but got 0. All packets: {packets}"
    )
    assert len(timeout_pkts) == 0, (
        f"SN-SR-01: expected no timeout packet for slave_id=0x{TEST_SLAVE_ID:02X} "
        f"(response was fast, 10 ms < {SNIFFER_RESP_TIMEOUT_MS} ms) "
        f"but got: {timeout_pkts}"
    )
    print(
        f"✓ SN-SR-01: fast response (10 ms): master packet + slave packet emitted, "
        f"no timeout (SNIFFER_RESP_TIMEOUT_MS={SNIFFER_RESP_TIMEOUT_MS} ms)"
    )


# ===========================================================================
# SN-SR-02: Slow slave response (400 ms) — TIMEOUT then orphan SLAVE
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(20)
def test_sniffer_slow_response_timeout_then_orphan_slave(api):
    """Slow slave response (400 ms) — MASTER emitted immediately, then orphan SLAVE; no TIMEOUT.

    Injects FC03 request for slave=0x15 then waits 400 ms (2× SNIFFER_RESP_TIMEOUT_MS)
    before injecting the matching FC03 response.

    Expected outcome:
    - At least one {type:"packet", sender:"master", slave_id:0x15, function:3}
      (emitted immediately when the request arrives)
    - At least one {type:"packet", sender:"slave", slave_id:0x15, function:3}
      (the late orphan response, emitted standalone after the internal timeout)
    - No {type:"timeout"} packets (firmware no longer forwards timeout events over WebSocket)
    """
    # 400 ms delay — well past the 200 ms sniffer timeout
    # collect for 5.0 s to capture both events: MASTER + orphan SLAVE
    packets = _run_sniffer_timing_test(api, delay_ms=400, collect_timeout_sec=5.0, min_count=2)

    # Filter for our slave ID
    our_pkts = [p for p in packets if p.get("slave_id") == TEST_SLAVE_ID]

    master_pkts = [
        p for p in our_pkts
        if p.get("type") == "packet" and p.get("sender") == "master"
    ]
    orphan_slave_pkts = [
        p for p in our_pkts
        if p.get("type") == "packet" and p.get("sender") == "slave" and p.get("function") == 3
    ]

    assert len(master_pkts) >= 1, (
        f"SN-SR-02: expected >=1 master packet for slave_id=0x{TEST_SLAVE_ID:02X} "
        f"(master is emitted immediately) "
        f"but got 0. All packets: {packets}"
    )
    assert len(orphan_slave_pkts) >= 1, (
        f"SN-SR-02: expected >=1 orphan slave packet for slave_id=0x{TEST_SLAVE_ID:02X}, "
        f"function=3 (the late response after 400 ms) "
        f"but got 0. All packets: {packets}"
    )
    print(
        f"✓ SN-SR-02: slow response (400 ms): master emitted immediately + orphan slave, "
        f"no timeout packet (SNIFFER_RESP_TIMEOUT_MS={SNIFFER_RESP_TIMEOUT_MS} ms)"
    )


# ===========================================================================
# SN-SR-03: No slave response (dead slave) — only TIMEOUT, no slave packet
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(20)
def test_sniffer_no_response_only_timeout(api):
    """No slave response (dead slave) — sniffer emits only MASTER; no TIMEOUT, no SLAVE.

    Injects an FC03 request for slave=0x15 and never sends a response.

    Expected outcome:
    - At least one {type:"packet", sender:"master", slave_id:0x15, function:3}
      (emitted immediately when the request arrives)
    - No {type:"timeout"} packets (firmware no longer forwards timeout events over WebSocket)
    - No {type:"packet", sender:"slave", slave_id:0x15} at any point

    The collection window is SNIFFER_RESP_TIMEOUT_MS/1000 + 1 s to allow any
    spurious timeout event to arrive if the firmware incorrectly sends one.
    """
    # Never inject a response; collect for (timeout + 1 s) to allow detection of any unwanted events
    collect_sec = (SNIFFER_RESP_TIMEOUT_MS / 1000.0) + 1.0
    packets = _run_sniffer_timing_test(api, delay_ms=None, collect_timeout_sec=collect_sec, min_count=1)

    # Filter for our slave ID
    our_pkts = [p for p in packets if p.get("slave_id") == TEST_SLAVE_ID]

    master_pkts = [
        p for p in our_pkts
        if p.get("type") == "packet" and p.get("sender") == "master" and p.get("function") == 3
    ]
    timeout_pkts = [
        p for p in our_pkts
        if p.get("type") == "timeout"
    ]
    slave_pkts = [
        p for p in our_pkts
        if p.get("type") == "packet" and p.get("sender") == "slave"
    ]

    assert len(master_pkts) >= 1, (
        f"SN-SR-03: expected >=1 master packet for slave_id=0x{TEST_SLAVE_ID:02X} "
        f"(master is emitted immediately) "
        f"but got 0. All packets: {packets}"
    )
    assert len(timeout_pkts) == 0, (
        f"SN-SR-03: expected NO timeout packets over WebSocket "
        f"(firmware no longer forwards timeout events) "
        f"but got: {timeout_pkts}"
    )
    assert len(slave_pkts) == 0, (
        f"SN-SR-03: expected NO slave packet for slave_id=0x{TEST_SLAVE_ID:02X} "
        f"(no response was injected) "
        f"but got: {slave_pkts}"
    )
    print(
        f"✓ SN-SR-03: no response: master emitted immediately, no timeout packet, no slave packet "
        f"(SNIFFER_RESP_TIMEOUT_MS={SNIFFER_RESP_TIMEOUT_MS} ms)"
    )


# ===========================================================================
# SN-SR-04: [DESIRED] Master packet appears immediately (before response)
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(20)
def test_sniffer_master_emitted_immediately(api):
    """[DESIRED] Master request emitted immediately upon receipt, before any response.

    This test verifies the DESIRED (not yet implemented) behavior:
    the sniffer must emit {type:"packet", sender:"master"} as soon as it
    receives the master request — before waiting for a slave response.

    Collection window is only 150 ms (less than SNIFFER_RESP_TIMEOUT_MS=200 ms)
    so the timeout timer cannot fire during collection.  With current firmware
    the master is buffered and nothing arrives in those 150 ms → FAIL.
    With the desired firmware the master packet arrives almost immediately → PASS.

    Currently FAILS because the sniffer buffers master requests.
    """
    # Save original port mode for cleanup
    resp = api.get_info()
    assert resp.status_code == 200, f"GET /info failed: {resp.status_code}"
    original_mode = resp.json().get("rs485_1", {}).get("port_mode", "disabled")

    ws = None
    stop_ping = None
    uart_sock = None

    try:
        # Open serial (passive transport) for the WS sniffer overlay
        resp = api.set_port_mode(1, "passive")
        assert resp.status_code == 200, (
            f"SN-SR-04: failed to set port 1 to sniffer: {resp.status_code}"
        )
        time.sleep(0.5)  # allow sniffer task to initialise

        # Connect WebSocket and start sniffer stream
        ws, stop_ping, _ = _ws_connect(api, 1)
        time.sleep(0.2)  # allow WS handshake to complete

        # Open UART socket for raw byte injection
        try:
            uart_sock = open_uart_socket(port=1)
        except OSError as exc:
            pytest.skip(f"UART1 chardev not reachable: {exc}")

        # Build only the request — no response is ever injected
        req = build_fc03_request(slave=TEST_SLAVE_ID, start_addr=0x0000, reg_count=1)
        inject_bytes(port=1, data=req, sock=uart_sock)

        # Collect for 150 ms — strictly before the 200 ms timer fires.
        # Desired: MASTER packet arrives almost instantly.
        # Current: nothing arrives (master is buffered) → assertion below fails.
        packets = _collect_packets(ws, min_count=1, timeout_sec=0.15, filter_fn=None)

        our_pkts = [p for p in packets if p.get("slave_id") == TEST_SLAVE_ID]
        master_pkts = [
            p for p in our_pkts
            if p.get("type") == "packet" and p.get("sender") == "master" and p.get("function") == 3
        ]

        assert len(master_pkts) >= 1, (
            f"SN-SR-04: expected MASTER packet for slave_id=0x{TEST_SLAVE_ID:02X} "
            f"immediately (within 150 ms, before the {SNIFFER_RESP_TIMEOUT_MS} ms timer) "
            f"but got nothing. All packets: {packets}"
        )
        print(
            f"✓ SN-SR-04: master packet emitted immediately "
            f"(SNIFFER_RESP_TIMEOUT_MS={SNIFFER_RESP_TIMEOUT_MS} ms)"
        )

    finally:
        if uart_sock is not None:
            try:
                uart_sock.close()
            except OSError:
                pass
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
        restore_resp = api.set_port_mode(1, original_mode)
        assert restore_resp.status_code == 200, (
            f"SN-SR-04: failed to restore port 1 mode to {original_mode!r}: "
            f"{restore_resp.status_code}"
        )


# ===========================================================================
# SN-SR-05: [DESIRED] No response → MASTER + TIMEOUT (2 separate events)
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(30)
def test_sniffer_no_response_master_then_timeout(api):
    """No slave response → sniffer emits only MASTER (no TIMEOUT over WebSocket).

    When no slave responds, the sniffer emits:
    1. {type:"packet", sender:"master"} immediately when request arrives
    The firmware still fires the internal 200 ms timer but does NOT forward
    {type:"timeout"} events over WebSocket.
    """
    # Save original port mode for cleanup
    resp = api.get_info()
    assert resp.status_code == 200, f"GET /info failed: {resp.status_code}"
    original_mode = resp.json().get("rs485_1", {}).get("port_mode", "disabled")

    ws = None
    stop_ping = None
    uart_sock = None

    try:
        # Open serial (passive transport) for the WS sniffer overlay
        resp = api.set_port_mode(1, "passive")
        assert resp.status_code == 200, (
            f"SN-SR-05: failed to set port 1 to sniffer: {resp.status_code}"
        )
        time.sleep(0.5)  # allow sniffer task to initialise

        # Connect WebSocket and start sniffer stream
        ws, stop_ping, _ = _ws_connect(api, 1)
        time.sleep(0.2)  # allow WS handshake to complete

        # Open UART socket for raw byte injection
        try:
            uart_sock = open_uart_socket(port=1)
        except OSError as exc:
            pytest.skip(f"UART1 chardev not reachable: {exc}")

        # Inject only the request — no response ever follows
        req = build_fc03_request(slave=TEST_SLAVE_ID, start_addr=0x0000, reg_count=1)
        inject_bytes(port=1, data=req, sock=uart_sock)

        # Collect for 2 s — long enough for MASTER (immediate) and any spurious
        # TIMEOUT event (after 200 ms) to arrive so we can assert it is absent.
        packets = _collect_packets(ws, min_count=1, timeout_sec=2.0, filter_fn=None)

        our_pkts = [p for p in packets if p.get("slave_id") == TEST_SLAVE_ID]
        master_pkts = [
            p for p in our_pkts
            if p.get("type") == "packet" and p.get("sender") == "master" and p.get("function") == 3
        ]
        timeout_pkts = [
            p for p in our_pkts
            if p.get("type") == "timeout"
        ]
        slave_pkts = [
            p for p in our_pkts
            if p.get("type") == "packet" and p.get("sender") == "slave"
        ]

        assert len(master_pkts) >= 1, (
            f"SN-SR-05: expected >=1 MASTER packet for slave_id=0x{TEST_SLAVE_ID:02X}, "
            f"function=3 but got 0. All packets: {packets}"
        )
        assert len(timeout_pkts) == 0, (
            f"SN-SR-05: expected NO TIMEOUT packets over WebSocket "
            f"(firmware no longer forwards timeout events) "
            f"but got: {timeout_pkts}"
        )
        assert len(slave_pkts) == 0, (
            f"SN-SR-05: expected NO slave packet (no response was injected) "
            f"but got: {slave_pkts}"
        )
        print(
            f"✓ SN-SR-05: no response: only MASTER emitted (no TIMEOUT, no SLAVE) "
            f"(SNIFFER_RESP_TIMEOUT_MS={SNIFFER_RESP_TIMEOUT_MS} ms)"
        )

    finally:
        if uart_sock is not None:
            try:
                uart_sock.close()
            except OSError:
                pass
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
        restore_resp = api.set_port_mode(1, original_mode)
        assert restore_resp.status_code == 200, (
            f"SN-SR-05: failed to restore port 1 mode to {original_mode!r}: "
            f"{restore_resp.status_code}"
        )


# ===========================================================================
# SN-SR-06: [DESIRED] Slow response → MASTER + TIMEOUT + SLAVE (3 events)
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(30)
def test_sniffer_slow_response_three_events(api):
    """Slow slave response (400 ms) → MASTER + orphan SLAVE; no TIMEOUT over WebSocket.

    Behavior:
    1. {type:"packet", sender:"master"} emitted immediately when request arrives
    2. {type:"packet", sender:"slave"} when the late response arrives (orphan)
    No {type:"timeout"} is forwarded over WebSocket (firmware suppresses it).
    """
    # Save original port mode for cleanup
    resp = api.get_info()
    assert resp.status_code == 200, f"GET /info failed: {resp.status_code}"
    original_mode = resp.json().get("rs485_1", {}).get("port_mode", "disabled")

    ws = None
    stop_ping = None
    uart_sock = None

    try:
        # Open serial (passive transport) for the WS sniffer overlay
        resp = api.set_port_mode(1, "passive")
        assert resp.status_code == 200, (
            f"SN-SR-06: failed to set port 1 to sniffer: {resp.status_code}"
        )
        time.sleep(0.5)  # allow sniffer task to initialise

        # Connect WebSocket and start sniffer stream
        ws, stop_ping, _ = _ws_connect(api, 1)
        time.sleep(0.2)  # allow WS handshake to complete

        # Open UART socket for raw byte injection
        try:
            uart_sock = open_uart_socket(port=1)
        except OSError as exc:
            pytest.skip(f"UART1 chardev not reachable: {exc}")

        # Build both frames
        req = build_fc03_request(slave=TEST_SLAVE_ID, start_addr=0x0000, reg_count=1)
        resp_frame = build_fc03_response(slave=TEST_SLAVE_ID, reg_count=1, base_value=0x4988)

        # Inject request, wait 400 ms (2× timeout), then inject late response
        inject_bytes(port=1, data=req, sock=uart_sock)
        time.sleep(0.4)  # 400 ms — timer fires after 200 ms, response arrives after 400 ms
        inject_bytes(port=1, data=resp_frame, sock=uart_sock)

        # Collect for 3 s — enough for both events: MASTER (immediate) and
        # orphan SLAVE (400 ms). Also captures any spurious TIMEOUT.
        packets = _collect_packets(ws, min_count=2, timeout_sec=3.0, filter_fn=None)

        our_pkts = [p for p in packets if p.get("slave_id") == TEST_SLAVE_ID]
        master_pkts = [
            p for p in our_pkts
            if p.get("type") == "packet" and p.get("sender") == "master" and p.get("function") == 3
        ]
        timeout_pkts = [
            p for p in our_pkts
            if p.get("type") == "timeout"
        ]
        slave_pkts = [
            p for p in our_pkts
            if p.get("type") == "packet" and p.get("sender") == "slave" and p.get("function") == 3
        ]

        assert len(master_pkts) >= 1, (
            f"SN-SR-06: expected >=1 MASTER packet for slave_id=0x{TEST_SLAVE_ID:02X}, "
            f"function=3 but got 0. All packets: {packets}"
        )
        assert len(timeout_pkts) == 0, (
            f"SN-SR-06: expected NO TIMEOUT packets over WebSocket "
            f"(firmware no longer forwards timeout events) "
            f"but got: {timeout_pkts}"
        )
        assert len(slave_pkts) >= 1, (
            f"SN-SR-06: expected >=1 orphan SLAVE packet for slave_id=0x{TEST_SLAVE_ID:02X}, "
            f"function=3 (late response after 400 ms) "
            f"but got 0. All packets: {packets}"
        )
        print(
            f"✓ SN-SR-06: slow response (400 ms): MASTER + orphan SLAVE emitted, no TIMEOUT "
            f"(SNIFFER_RESP_TIMEOUT_MS={SNIFFER_RESP_TIMEOUT_MS} ms)"
        )

    finally:
        if uart_sock is not None:
            try:
                uart_sock.close()
            except OSError:
                pass
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
        restore_resp = api.set_port_mode(1, original_mode)
        assert restore_resp.status_code == 200, (
            f"SN-SR-06: failed to restore port 1 mode to {original_mode!r}: "
            f"{restore_resp.status_code}"
        )


# ===========================================================================
# SN-SR-07: Dead slave polled twice — both requests appear as MASTER packets
# ===========================================================================

@pytest.mark.qemu
# 75 s, not 30 s: an item's pytest-timeout budget covers setup + call + TEARDOWN, and
# module-scoped fixtures are torn down inside the LAST item of the module. This is that
# item, so it also pays conftest's _restore_rs485_settings teardown — up to two bounded
# POST /settings plus a settle window (2 x 20.1 s + 1 s = 41.2 s, see _RS485_HTTP_TIMEOUT).
# 30 s body + 45 s teardown allowance.
@pytest.mark.timeout(75)
def test_sniffer_dead_slave_polled_twice(api):
    """Dead slave polled twice — both requests appear as MASTER packets; no TIMEOUT, no SLAVE.

    Scenario (scenario 4 from scripts/slow_sniffer_demo.py):
    1. Inject FC03 master request for TEST_SLAVE_ID.
    2. Wait 600 ms — longer than the 200 ms internal timer; no response injected.
    3. Inject a second FC03 master request for the same slave.
    4. Wait 300 ms — allows the second internal 200 ms timeout to fire and be buffered
       before collection ends (so the assert `timeout_pkts == 0` covers both cycles).

    Expected outcome:
    - At least 2 {type:"packet", sender:"master", slave_id:TEST_SLAVE_ID, function:3}
      packets — one per request cycle.
    - No {type:"timeout"} packets over WebSocket (firmware suppresses them).
    - No {type:"packet", sender:"slave"} packets (no response was ever injected).

    Rationale:
    After the internal 200 ms timeout fires the sniffer state machine must reset
    and accept the next master frame correctly.  The second request must be
    classified as a fresh MASTER packet, not silently dropped or misclassified.
    """
    # Save original port mode for cleanup
    resp = api.get_info()
    assert resp.status_code == 200, f"GET /info failed: {resp.status_code}"
    original_mode = resp.json().get("rs485_1", {}).get("port_mode", "disabled")

    ws = None
    stop_ping = None
    uart_sock = None

    try:
        # Open serial (passive transport) for the WS sniffer overlay
        resp = api.set_port_mode(1, "passive")
        assert resp.status_code == 200, (
            f"SN-SR-07: failed to set port 1 to sniffer: {resp.status_code}"
        )
        time.sleep(0.5)  # allow sniffer task to initialise

        # Connect WebSocket and start sniffer stream
        ws, stop_ping, _ = _ws_connect(api, 1)
        time.sleep(0.2)  # allow WS handshake to complete

        # Open UART socket for raw byte injection
        try:
            uart_sock = open_uart_socket(port=1)
        except OSError as exc:
            pytest.skip(f"UART1 chardev not reachable: {exc}")

        # Build the request frame — reused for both poll cycles
        req = build_fc03_request(slave=TEST_SLAVE_ID, start_addr=0x0000, reg_count=1)

        # First poll: inject request and wait longer than the internal timer (600 ms > 200 ms)
        inject_bytes(port=1, data=req, sock=uart_sock)
        time.sleep(0.6)  # no response — internal timeout fires at ~200 ms

        # Second poll: inject another request, then wait long enough for the second
        # internal timeout (~200 ms) to fire and reach the WS buffer before collection
        # ends — this ensures the timeout_pkts == 0 assert covers both poll cycles.
        inject_bytes(port=1, data=req, sock=uart_sock)
        time.sleep(0.3)  # allow second internal timeout (~200 ms) to fire and be buffered

        # Collect for 4 s; both MASTER packets should already be in the WS buffer.
        packets = _collect_packets(ws, min_count=2, timeout_sec=4.0, filter_fn=None)

        our_pkts = [p for p in packets if p.get("slave_id") == TEST_SLAVE_ID]
        master_pkts = [
            p for p in our_pkts
            if p.get("type") == "packet" and p.get("sender") == "master" and p.get("function") == 3
        ]
        timeout_pkts = [
            p for p in our_pkts
            if p.get("type") == "timeout"
        ]
        slave_pkts = [
            p for p in our_pkts
            if p.get("type") == "packet" and p.get("sender") == "slave"
        ]

        assert len(master_pkts) >= 2, (
            f"SN-SR-07: expected >=2 MASTER packets for slave_id=0x{TEST_SLAVE_ID:02X}, "
            f"function=3 (one per poll cycle) but got {len(master_pkts)}. "
            f"All packets: {packets}"
        )
        assert len(timeout_pkts) == 0, (
            f"SN-SR-07: expected NO TIMEOUT packets over WebSocket "
            f"(firmware no longer forwards timeout events) "
            f"but got: {timeout_pkts}"
        )
        assert len(slave_pkts) == 0, (
            f"SN-SR-07: expected NO slave packets (no response was injected) "
            f"but got: {slave_pkts}"
        )
        print(
            f"✓ SN-SR-07: dead slave polled twice: {len(master_pkts)} MASTER packets emitted, "
            f"no TIMEOUT, no SLAVE "
            f"(SNIFFER_RESP_TIMEOUT_MS={SNIFFER_RESP_TIMEOUT_MS} ms)"
        )

    finally:
        if uart_sock is not None:
            try:
                uart_sock.close()
            except OSError:
                pass
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
        restore_resp = api.set_port_mode(1, original_mode)
        assert restore_resp.status_code == 200, (
            f"SN-SR-07: failed to restore port 1 mode to {original_mode!r}: "
            f"{restore_resp.status_code}"
        )
