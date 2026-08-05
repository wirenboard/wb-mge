"""Regression tests for sniffer/cache interaction.

New architecture (additive overlays):
  - The transport mode is one of: disabled / tcp_bridge / passive.
  - The cache is a per-port OVERLAY toggled via POST /ports/N/cache {"enabled": bool},
    orthogonal to the transport mode and persisted independently. /info exposes it
    as rs485_N.cache_enabled.
  - The live sniffer is an overlay too: it runs whenever a WS client is connected
    (SNIFF_REASON_DISPLAY) on ANY non-disabled transport. There is no separate
    "sniffer" transport mode anymore, so no mode auto-switch is needed.

These tests keep the spirit of the original GM-17..GM-21 regressions (the sniffer
must keep delivering data across cache enable/disable cycles and transport changes),
expressed against the new endpoints.
"""

import json
import time

import pytest

from packet_injector import PacketInjector
from sniffer_helpers import _ws_connect, _collect_packets


# A transport that keeps the serial port open so the sniffer can observe traffic.
PASSIVE = "passive"


# ===========================================================================
# GM-17: Sniffer works after a cache overlay enable → disable cycle
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(60)
def test_sniffer_works_after_cache_overlay_cycle(api):
    """Sniffer on port 1 produces data after enabling then disabling the cache overlay.

    Regression for the old cache_bus → disabled → sniffer breakage. With the new
    additive model the transport stays 'passive' the whole time; only the cache
    overlay is toggled.

    Steps:
    1. Save original port 1 transport mode.
    2. Set port 1 transport to passive.
    3. Enable the cache overlay; sleep 0.5s.
    4. Disable the cache overlay; sleep 0.5s.
    5. Inject Modbus traffic, connect the WS sniffer, collect >=3 packets.
    """
    resp = api.get_info()
    assert resp.status_code == 200, f"GET /info failed: {resp.status_code}"
    original_mode = resp.json().get("rs485_1", {}).get("port_mode", "disabled")

    ws = None
    stop_ping = None
    try:
        resp = api.set_port_mode(1, PASSIVE)
        assert resp.status_code == 200, \
            f"Failed to set port 1 to passive: {resp.status_code}"
        time.sleep(0.5)

        # Enable then disable the cache overlay (no transport change in between).
        resp = api.set_port_cache(1, True)
        assert resp.status_code == 200, \
            f"Failed to enable cache overlay on port 1: {resp.status_code}"
        time.sleep(0.5)

        resp = api.set_port_cache(1, False)
        assert resp.status_code == 200, \
            f"Failed to disable cache overlay on port 1: {resp.status_code}"
        time.sleep(0.5)

        with PacketInjector(port=1, include_all_fc=False):
            ws, stop_ping, _ = _ws_connect(api, 1)

            packets = _collect_packets(
                ws,
                min_count=3,
                timeout_sec=20,
                filter_fn=lambda p: p.get("type") == "packet",
            )

        assert len(packets) >= 3, (
            f"GM-17: Expected >=3 sniffer packets after a cache overlay enable/disable "
            f"cycle but got {len(packets)}."
        )
        print(
            f"✓ GM-17: Sniffer received {len(packets)} packets after cache overlay cycle"
        )

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
        resp = api.set_port_cache(1, False)
        resp = api.set_port_mode(1, original_mode)
        assert resp.status_code == 200, \
            f"Failed to restore port 1 mode to {original_mode!r}: {resp.status_code}"


# ===========================================================================
# GM-18: Sniffer works while the cache overlay is enabled (no transport switch)
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(60)
def test_sniffer_works_with_cache_overlay_enabled(api):
    """Sniffer on port 1 produces data with the cache overlay still enabled.

    Regression for the old direct cache_bus → sniffer switch breakage. Both the
    cache overlay and the live sniffer now run additively on the same passive
    transport at the same time.

    Steps:
    1. Save original port 1 transport mode.
    2. Set port 1 transport to passive and enable the cache overlay.
    3. Inject Modbus traffic, connect the WS sniffer (cache still on), collect >=3.
    """
    resp = api.get_info()
    assert resp.status_code == 200, f"GET /info failed: {resp.status_code}"
    original_mode = resp.json().get("rs485_1", {}).get("port_mode", "disabled")

    ws = None
    stop_ping = None
    try:
        resp = api.set_port_mode(1, PASSIVE)
        assert resp.status_code == 200, \
            f"Failed to set port 1 to passive: {resp.status_code}"
        time.sleep(0.5)

        resp = api.set_port_cache(1, True)
        assert resp.status_code == 200, \
            f"Failed to enable cache overlay on port 1: {resp.status_code}"
        time.sleep(0.5)

        # /info must report the cache overlay as enabled.
        info = api.get_info().json()
        assert info.get("rs485_1", {}).get("cache_enabled") is True, \
            "GM-18: /info rs485_1.cache_enabled must be true after enabling the overlay"

        with PacketInjector(port=1, include_all_fc=False):
            ws, stop_ping, _ = _ws_connect(api, 1)

            packets = _collect_packets(
                ws,
                min_count=3,
                timeout_sec=20,
                filter_fn=lambda p: p.get("type") == "packet",
            )

        assert len(packets) >= 3, (
            f"GM-18: Expected >=3 sniffer packets with the cache overlay enabled "
            f"but got {len(packets)}."
        )
        print(
            f"✓ GM-18: Sniffer received {len(packets)} packets with cache overlay enabled"
        )

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
        resp = api.set_port_cache(1, False)
        resp = api.set_port_mode(1, original_mode)
        assert resp.status_code == 200, \
            f"Failed to restore port 1 mode to {original_mode!r}: {resp.status_code}"


# ===========================================================================
# GM-19: WS sniffer on port 1 works after port 2 cache overlay enable/disable cycle
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(90)
def test_sniffer_ws_works_after_other_port_cache_cycle(api):
    """WS sniffer on port 1 produces data after port 2's cache overlay is cycled.

    Regression for cross-port RX-timeout coupling. In the new model the cache
    overlay only drives the sniffer (RX timeout) of the port that owns it, so
    cycling port 2's overlay must not affect port 1.

    Steps:
    1. Save original transport modes for port 1 and port 2.
    2. Put both ports in passive transport.
    3. Connect WS to port 1 and stop the live sniffer.
    4. Enable then disable the cache overlay on port 2.
    5. Reconnect WS on port 1, inject traffic, collect >=3 packets.
    """
    resp = api.get_info()
    assert resp.status_code == 200, f"GET /info failed: {resp.status_code}"
    info = resp.json()
    original_mode_p1 = info.get("rs485_1", {}).get("port_mode", "disabled")
    original_mode_p2 = info.get("rs485_2", {}).get("port_mode", "disabled")

    ws = None
    stop_ping = None
    try:
        resp = api.set_port_mode(1, PASSIVE)
        assert resp.status_code == 200, f"Failed to set port 1 passive: {resp.status_code}"
        resp = api.set_port_mode(2, PASSIVE)
        assert resp.status_code == 200, f"Failed to set port 2 passive: {resp.status_code}"
        time.sleep(0.5)

        # Connect WS on port 1 and immediately stop the live sniffer (DISPLAY reason off).
        ws, stop_ping, _ = _ws_connect(api, 1)
        ws.send(json.dumps({"cmd": "stop", "port": 1}))
        time.sleep(0.2)
        stop_ping.set()
        try:
            ws.close()
        except Exception:
            pass
        ws = None
        stop_ping = None

        # Cycle the cache overlay on port 2 (must not touch port 1's RX timeout).
        resp = api.set_port_cache(2, True)
        assert resp.status_code == 200, f"Failed to enable cache on port 2: {resp.status_code}"
        time.sleep(0.5)
        resp = api.set_port_cache(2, False)
        assert resp.status_code == 200, f"Failed to disable cache on port 2: {resp.status_code}"
        time.sleep(0.5)

        # Restart the live sniffer on port 1 via WS and verify data flows.
        with PacketInjector(port=1, include_all_fc=False):
            ws, stop_ping, _ = _ws_connect(api, 1)

            packets = _collect_packets(
                ws,
                min_count=3,
                timeout_sec=20,
                filter_fn=lambda p: p.get("type") == "packet",
            )

        assert len(packets) >= 3, (
            f"GM-19: Expected >=3 sniffer packets after port 2 cache overlay cycle "
            f"but got {len(packets)}."
        )
        print(
            f"✓ GM-19: Sniffer received {len(packets)} packets after port 2 cache cycle"
        )

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
        api.set_port_cache(2, False)
        resp = api.set_port_mode(2, original_mode_p2)
        if resp.status_code != 200:
            print(f"✗ Failed to restore port 2 mode to {original_mode_p2!r}")
        resp = api.set_port_mode(1, original_mode_p1)
        assert resp.status_code == 200, \
            f"Failed to restore port 1 mode to {original_mode_p1!r}: {resp.status_code}"


# ===========================================================================
# GM-20: WS sniffer restart after its own cache overlay was active then disabled
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(90)
def test_sniffer_ws_restart_after_cache_disable_while_active(api):
    """WS sniffer on port 1 restarts after its cache overlay was active then disabled.

    Scenario (single port, two overlapping overlays):
    1. Port 1 in passive transport, WS sniffer active (DISPLAY reason).
    2. Enable the cache overlay on port 1 (CACHE reason added alongside DISPLAY).
    3. Stop the WS sniffer (DISPLAY cleared; CACHE keeps the short RX timeout).
    4. Disable the cache overlay (CACHE cleared; RX timeout returns to PROXY).
    5. Restart the WS sniffer (DISPLAY re-added; RX timeout back to SNIFFER).
    6. Verify data flows.
    """
    resp = api.get_info()
    assert resp.status_code == 200, f"GET /info failed: {resp.status_code}"
    info = resp.json()
    original_mode_p1 = info.get("rs485_1", {}).get("port_mode", "disabled")

    ws = None
    stop_ping = None
    try:
        resp = api.set_port_mode(1, PASSIVE)
        assert resp.status_code == 200, f"Failed to set port 1 passive: {resp.status_code}"
        time.sleep(0.5)

        # Live sniffer on (DISPLAY reason).
        ws, stop_ping, _ = _ws_connect(api, 1)

        # Cache overlay on alongside the running sniffer (CACHE reason).
        resp = api.set_port_cache(1, True)
        assert resp.status_code == 200, f"Failed to enable cache on port 1: {resp.status_code}"
        time.sleep(0.5)

        # Stop the live sniffer; the CACHE reason keeps the short RX timeout.
        ws.send(json.dumps({"cmd": "stop", "port": 1}))
        time.sleep(0.2)
        stop_ping.set()
        try:
            ws.close()
        except Exception:
            pass
        ws = None
        stop_ping = None

        # Disable the cache overlay; now no reasons remain, RX timeout → PROXY.
        resp = api.set_port_cache(1, False)
        assert resp.status_code == 200, f"Failed to disable cache on port 1: {resp.status_code}"
        time.sleep(0.5)

        # Restart the live sniffer; RX timeout must return to SNIFFER and data flow.
        with PacketInjector(port=1, include_all_fc=False):
            ws, stop_ping, _ = _ws_connect(api, 1)

            packets = _collect_packets(
                ws,
                min_count=3,
                timeout_sec=20,
                filter_fn=lambda p: p.get("type") == "packet",
            )

        assert len(packets) >= 3, (
            f"GM-20: Expected >=3 sniffer packets after cache-active stop/restart "
            f"but got {len(packets)}."
        )
        print(
            f"✓ GM-20: Sniffer received {len(packets)} packets after cache-active "
            "stop then disable-cache then restart"
        )

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
        api.set_port_cache(1, False)
        resp = api.set_port_mode(1, original_mode_p1)
        assert resp.status_code == 200, \
            f"Failed to restore port 1 mode to {original_mode_p1!r}: {resp.status_code}"


# ===========================================================================
# GM-21: Sniffer works after a RegisterMap-style cache toggle (no auto-switch needed)
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(90)
def test_sniffer_after_register_map_cache_toggle(api):
    """Sniffer WS works after a RegisterMap-style cache overlay toggle.

    In the old model RegisterMap had to change the transport mode to drive the
    cache, which broke the sniffer and required a Sniffer.vue auto-switch fix.
    In the new model RegisterMap only toggles the cache OVERLAY (POST /ports/N/cache),
    leaving the transport untouched, so the sniffer keeps working with no
    mode juggling.

    Steps:
    1. Port 1 starts in passive transport; confirm the sniffer works (baseline).
    2. RegisterMap enables the cache overlay.
    3. RegisterMap disables the cache overlay.
    4. Connect WS again and verify data still arrives (no transport change).
    """
    resp = api.get_info()
    assert resp.status_code == 200, f"GET /info failed: {resp.status_code}"
    original_mode = resp.json().get("rs485_1", {}).get("port_mode", "disabled")

    ws = None
    stop_ping = None
    try:
        resp = api.set_port_mode(1, PASSIVE)
        assert resp.status_code == 200, f"Failed to set port 1 passive: {resp.status_code}"
        time.sleep(0.5)

        # Baseline: confirm the live sniffer works on passive transport.
        with PacketInjector(port=1, include_all_fc=False):
            ws, stop_ping, _ = _ws_connect(api, 1)
            baseline_pkts = _collect_packets(
                ws, min_count=3, timeout_sec=15,
                filter_fn=lambda p: p.get("type") == "packet",
            )
            assert len(baseline_pkts) >= 3, (
                f"GM-21 baseline: expected >=3 packets, got {len(baseline_pkts)}"
            )
        stop_ping.set()
        try:
            ws.send(json.dumps({"cmd": "stop", "port": 1}))
        except Exception:
            pass
        try:
            ws.close()
        except Exception:
            pass
        ws = None
        stop_ping = None
        print(f"  GM-21 baseline: {len(baseline_pkts)} packets OK")

        # RegisterMap.toggleCache(): enable then disable the cache OVERLAY only.
        resp = api.set_port_cache(1, True)
        assert resp.status_code == 200, f"Failed to enable cache overlay: {resp.status_code}"
        time.sleep(0.5)
        resp = api.set_port_cache(1, False)
        assert resp.status_code == 200, f"Failed to disable cache overlay: {resp.status_code}"
        time.sleep(0.5)

        # Transport must still be passive — no auto-switch required.
        info_resp = api.get_info()
        assert info_resp.status_code == 200
        current_mode = info_resp.json().get("rs485_1", {}).get("port_mode", "unknown")
        assert current_mode == PASSIVE, \
            f"Expected transport to remain '{PASSIVE}', got '{current_mode}'"

        # Connect WS again and verify data arrives without any mode change.
        with PacketInjector(port=1, include_all_fc=False):
            ws, stop_ping, _ = _ws_connect(api, 1)
            post_cache_pkts = _collect_packets(
                ws, min_count=3, timeout_sec=15,
                filter_fn=lambda p: p.get("type") == "packet",
            )

        assert len(post_cache_pkts) >= 3, (
            f"GM-21: Expected >=3 sniffer packets after the cache overlay toggle "
            f"but got {len(post_cache_pkts)}."
        )
        print(
            f"✓ GM-21: Sniffer received {len(post_cache_pkts)} packets after "
            "RegisterMap cache overlay toggle"
        )

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
        api.set_port_cache(1, False)
        resp = api.set_port_mode(1, original_mode)
        assert resp.status_code == 200, \
            f"Failed to restore port 1 mode to {original_mode!r}: {resp.status_code}"
