"""
Regression test for UART teardown crash bugs (01, 04, 05, 06).

Switching port mode calls serial_deinit() -> uart_driver_delete(). In ESP-IDF
v5.4 that function detaches the ISR (esp_intr_free) BEFORE disabling the UART
interrupt (uart_disable_rx/tx_intr). If an interrupt fires in that window it
falls into the default xt_unhandled_interrupt handler, which never clears the
source -> infinite re-fire -> CPU hangs ("Unhandled interrupt 9 on cpu 0!") ->
HTTP server is dead.

Additionally: resp_timer_cb() in sniffer.c (bug 06) allocates sniff_packet_t
(~280 bytes) on the Tmr Svc stack (2048 bytes), causing stack overflow under
frequent mode changes. The same heavy stack usage in sniffer_process() can
overflow the QEMU mock task (modbus_mock) if its stack is too small.

Reproduction: toggle port mode frequently (sniffer <-> tcp_bridge) — each
toggle calls uart_driver_delete — under a connected WS client with aggressive
PING (adds scheduler jitter, replicating the original captured crash). After
each toggle verify the server is still alive via HTTP. A crash kills the server
entirely, so get_info() starts timing out — that is what we detect here.

Run this test alone:
  pytest api_tests/16_test_uart_teardown_crash.py --qemu --qemu-skip-build -s
"""
import json
import threading
import time
from pathlib import Path

import pytest
import websocket
from urllib.parse import urlparse


@pytest.fixture(scope="module", autouse=True)
def _baseline(api):
    resp = api.update_settings({
        "rs485_1": {
            "tx_disabled": True,          # required for the sniffer toggle cycle in QEMU
            "bridge": {"mode": "server", "port": 502, "ip": "0.0.0.0", "modbus": False},
        }
    })
    assert resp.status_code == 200, f"_baseline: update_settings failed: {resp.status_code} {resp.text}"
    resp = api.set_port_mode(1, "tcp_bridge")
    assert resp.status_code == 200, f"_baseline: set_port_mode(1, tcp_bridge) failed: {resp.status_code} {resp.text}"

# Crash signatures in the QEMU log. Cover the full class of teardown crashes:
#   bug 01 (Unhandled interrupt), 04 (Cache disabled), 05 (PC=0 NULL-ISR),
#   06 (stack overflow), plus any Guru/Load/StoreProhibited panics.
CRASH_MARKERS = [
    "Guru Meditation",
    "Cache disabled",
    "stack overflow",
    "Unhandled interrupt",
    "PC      : 0x00000000",
    "StoreProhibited",
    "LoadProhibited",
]


TOGGLES = 60           # number of port-mode teardowns to perform
PING_INTERVAL = 0.003  # aggressive PING for scheduler jitter (seconds)
TOGGLE_GAP = 0.15      # pause between mode switches (realistic pace, not NVS storm)


def _pinger(ws, stop_evt):
    """Send PINGs continuously to add scheduler jitter during teardown."""
    while not stop_evt.is_set():
        try:
            ws.ping()
        except Exception:
            break
        time.sleep(PING_INTERVAL)


@pytest.mark.timeout(900)
def test_uart_teardown_no_crash(api):
    """
    Frequent port-mode switches (uart_driver_delete) under WS+PING load must not
    crash the firmware. Without the fixes the server hangs on Unhandled interrupt
    or stack overflow and stops responding; with the fixes it survives all toggles.
    """
    parsed = urlparse(api.base_url)
    host = parsed.hostname
    port = parsed.port or 80

    original_port_mode = None
    ws = None
    stop_evt = threading.Event()
    try:
        info = api.get_info()
        assert info.status_code == 200
        original_port_mode = info.json().get("rs485_1", {}).get("port_mode", "tcp_bridge")

        # Enable sniffer and connect WS with aggressive PING to add jitter
        r = api.set_port_mode(1, "sniffer")
        assert r.status_code == 200
        time.sleep(0.3)

        cookies = "; ".join([f"{k}={v}" for k, v in api.session.cookies.items()])
        ws = websocket.WebSocket()
        ws.settimeout(2)
        ws.connect(f"ws://{host}:{port}/sniffer/ws", cookie=cookies)
        ws.send(json.dumps({"cmd": "start", "port": 1}))
        threading.Thread(target=_pinger, args=(ws, stop_evt), daemon=True).start()

        crashed_at = None
        for i in range(TOGGLES):
            # Each mode switch = serial_deinit -> uart_driver_delete (the dangerous path)
            r1 = api.set_port_mode(1, "tcp_bridge")
            time.sleep(TOGGLE_GAP)
            r2 = api.set_port_mode(1, "sniffer")
            time.sleep(TOGGLE_GAP)
            if r1.status_code != 200 or r2.status_code != 200:
                crashed_at = i
                break
            # Health check: is the server still alive? A crash causes timeouts here.
            try:
                hi = api.get_info()
                if hi.status_code != 200:
                    crashed_at = i
                    break
            except Exception:
                crashed_at = i
                break
            if (i + 1) % 20 == 0:
                print(f"  toggled {i+1}/{TOGGLES}, server alive")

        assert crashed_at is None, (
            f"Server stopped responding after {crashed_at} port-mode switches — "
            "likely firmware crash during uart_driver_delete (bugs 01/04/05/06)."
        )
        print(f"OK: server survived {TOGGLES} UART teardowns")

        # Secondary check: QEMU log must not contain any crash markers.
        # Catches cache panic (bug 04), stack overflow (06), Unhandled interrupt (01),
        # NULL-ISR (05) even if the server managed to recover before we noticed.
        qemu_log = Path(__file__).resolve().parent.parent / "build" / "qemu_test.log"
        if qemu_log.is_file():
            text = qemu_log.read_text(errors="replace")
            hits = sorted({m for m in CRASH_MARKERS if m in text})
            assert not hits, f"QEMU log contains crash markers: {hits}"
            print("OK: QEMU log is clean — no panics, crashes, or overflows")
        else:
            print(f"  [WARN] QEMU log not found ({qemu_log}), log check skipped")

    finally:
        stop_evt.set()
        if ws is not None:
            try:
                ws.close()
            except Exception:
                pass
        if original_port_mode is not None:
            try:
                api.set_port_mode(1, original_port_mode)
            except Exception:
                pass
