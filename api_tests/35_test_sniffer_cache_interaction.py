"""Regression tests for sniffer/cache interaction bugs.

Coverage:
GM-17  Sniffer works after cache_bus → disabled → sniffer cycle.
GM-18  Sniffer works after direct cache_bus → sniffer switch.
GM-19  WS sniffer on port 1 (sniffer mode) works after port 2 cache_bus cycle:
       port 2 enables global cache (sniffer_set_cache_active true), then is
       disabled (sniffer_set_cache_active false), then sniffer on port 1 is
       stopped and restarted via WS command.
GM-20  WS sniffer enabled WHILE port 2 is in cache_bus, then cache disabled,
       then WS sniffer restarted — verifies data still arrives.
"""

import json
import time

import pytest

from packet_injector import PacketInjector
from sniffer_helpers import _ws_connect, _collect_packets


# ===========================================================================
# GM-17: Sniffer works after cache_bus → disabled → sniffer cycle
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(60)
def test_sniffer_works_after_cache_bus_disabled_cycle(api):
    """Sniffer on port 1 produces data after cache_bus → disabled → sniffer cycle.

    Regression test for a bug where the sniffer received no data after the
    following sequence: set cache_bus, set disabled, set sniffer.

    Steps:
    1. Save original port 1 mode.
    2. Switch port 1 to cache_bus mode; sleep 0.5s.
    3. Switch port 1 to disabled mode; sleep 0.5s.
    4. Switch port 1 to sniffer mode; sleep 0.5s.
    5. Start PacketInjector to inject Modbus traffic.
    6. Connect WebSocket sniffer.
    7. Collect >=3 packets.
    8. Assert at least 3 packets were received.
    9. In finally: stop ping, send WS stop command, close WS, restore mode.
    """
    # Save original port 1 mode
    resp = api.get_info()
    assert resp.status_code == 200, f"GET /info failed: {resp.status_code}"
    original_mode = resp.json().get("rs485_1", {}).get("port_mode", "disabled")

    ws = None
    stop_ping = None
    try:
        # Step 1: switch to cache_bus and let firmware settle
        resp = api.set_port_mode(1, "cache_bus")
        assert resp.status_code == 200, \
            f"Failed to set port 1 to cache_bus: {resp.status_code}"
        time.sleep(0.5)

        # Step 2: switch to disabled to tear down the cache driver
        resp = api.set_port_mode(1, "disabled")
        assert resp.status_code == 200, \
            f"Failed to set port 1 to disabled: {resp.status_code}"
        time.sleep(0.5)

        # Step 3: switch to sniffer — this is the mode that previously broke
        resp = api.set_port_mode(1, "sniffer")
        assert resp.status_code == 200, \
            f"Failed to set port 1 to sniffer: {resp.status_code}"
        time.sleep(0.5)

        # Inject Modbus traffic so the sniffer has something to observe
        with PacketInjector(port=1, include_all_fc=False):
            ws, stop_ping, _ = _ws_connect(api, 1)

            packets = _collect_packets(
                ws,
                min_count=3,
                timeout_sec=20,
                filter_fn=lambda p: p.get("type") == "packet",
            )

        assert len(packets) >= 3, (
            f"GM-17: Expected >=3 sniffer packets after cache_bus→disabled→sniffer "
            f"cycle but got {len(packets)}. "
            "This indicates the sniffer/cache interaction bug is present."
        )
        print(
            f"✓ GM-17: Sniffer received {len(packets)} packets after "
            "cache_bus → disabled → sniffer cycle"
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
        # Restore original port mode
        resp = api.set_port_mode(1, original_mode)
        assert resp.status_code == 200, \
            f"Failed to restore port 1 mode to {original_mode!r}: {resp.status_code}"


# ===========================================================================
# GM-19: WS sniffer on port 1 works after port 2 cache_bus enable/disable cycle
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(90)
def test_sniffer_ws_works_after_other_port_cache_cycle(api):
    """WS sniffer on port 1 produces data after port 2 undergoes a cache_bus enable/disable.

    Regression test for a bug where enabling then disabling cache on one port
    affects the sniffer on another port.  When port 2 enters cache_bus mode,
    sniffer_set_cache_active(true) is called which sets the RX timeout on port 1
    (if its sniffer is disabled).  When port 2 exits cache_bus, sniffer_set_cache_active(false)
    is called which sets port 1 RX timeout to PROXY.  Then the WS start command
    re-enables the sniffer on port 1 and must work correctly.

    Steps:
    1. Save original modes for port 1 and port 2.
    2. Set port 1 to sniffer mode (sniffer active).
    3. Connect WS to port 1 sniffer and send stop — sniffer disabled.
    4. Set port 2 to cache_bus (sniffer_set_cache_active true called for port 1).
    5. Set port 2 to disabled (sniffer_set_cache_active false called for port 1).
    6. Reconnect WS and send start command — sniffer re-enabled on port 1.
    7. Inject Modbus traffic and collect >=3 packets.
    8. Assert >=3 packets received.
    """
    # Save original modes
    resp = api.get_info()
    assert resp.status_code == 200, f"GET /info failed: {resp.status_code}"
    info = resp.json()
    original_mode_p1 = info.get("rs485_1", {}).get("port_mode", "disabled")
    original_mode_p2 = info.get("rs485_2", {}).get("port_mode", "disabled")

    ws = None
    stop_ping = None
    try:
        # Step 1: put port 1 in sniffer mode
        resp = api.set_port_mode(1, "sniffer")
        assert resp.status_code == 200, f"Failed to set port 1 to sniffer: {resp.status_code}"
        time.sleep(0.5)

        # Step 2: connect WS and immediately stop the sniffer via WS command
        # This sets sniff_ctx[0].enabled = false
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

        # Step 3: enable cache on port 2 — calls sniffer_set_cache_active(true) on port 1
        resp = api.set_port_mode(2, "cache_bus")
        assert resp.status_code == 200, f"Failed to set port 2 to cache_bus: {resp.status_code}"
        time.sleep(0.5)

        # Step 4: disable port 2 — calls sniffer_set_cache_active(false) on port 1
        # This sets port 1 RX timeout to PROXY (10 symbols) since sniffer is disabled
        resp = api.set_port_mode(2, "disabled")
        assert resp.status_code == 200, f"Failed to set port 2 to disabled: {resp.status_code}"
        time.sleep(0.5)

        # Step 5: restart sniffer on port 1 via WS start command
        # sniffer_enable(0) should reset RX timeout to SNIFFER (3 symbols)
        with PacketInjector(port=1, include_all_fc=False):
            ws, stop_ping, _ = _ws_connect(api, 1)

            packets = _collect_packets(
                ws,
                min_count=3,
                timeout_sec=20,
                filter_fn=lambda p: p.get("type") == "packet",
            )

        assert len(packets) >= 3, (
            f"GM-19: Expected >=3 sniffer packets after other-port cache cycle "
            f"but got {len(packets)}. "
            "This indicates a cross-port sniffer/cache RX-timeout interaction bug."
        )
        print(
            f"✓ GM-19: Sniffer received {len(packets)} packets after "
            "port 2 cache_bus enable/disable cycle"
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
        resp = api.set_port_mode(2, original_mode_p2)
        if resp.status_code != 200:
            print(f"✗ Failed to restore port 2 mode to {original_mode_p2!r}")
        resp = api.set_port_mode(1, original_mode_p1)
        assert resp.status_code == 200, \
            f"Failed to restore port 1 mode to {original_mode_p1!r}: {resp.status_code}"


# ===========================================================================
# GM-20: WS sniffer enabled while cache active, cache disabled, sniffer restart
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(90)
def test_sniffer_ws_restart_after_cache_disable_while_active(api):
    """WS sniffer on port 1 restarts after cache was active alongside sniffer, then disabled.

    Scenario:
    1. Port 1 in sniffer mode, WS sniffer active.
    2. Port 2 set to cache_bus — cache enabled alongside running sniffer.
    3. Stop sniffer via WS (sniffer_disable: cache active so RX timeout NOT reset).
    4. Port 2 set to disabled — cache disabled, sniffer_set_cache_active(false)
       sets port 1 RX timeout to PROXY since sniffer is now disabled.
    5. Restart sniffer via WS — sniffer_enable resets RX timeout to SNIFFER.
    6. Verify data flows through the sniffer.
    """
    resp = api.get_info()
    assert resp.status_code == 200, f"GET /info failed: {resp.status_code}"
    info = resp.json()
    original_mode_p1 = info.get("rs485_1", {}).get("port_mode", "disabled")
    original_mode_p2 = info.get("rs485_2", {}).get("port_mode", "disabled")

    ws = None
    stop_ping = None
    try:
        # Step 1: port 1 in sniffer mode
        resp = api.set_port_mode(1, "sniffer")
        assert resp.status_code == 200, f"Failed to set port 1 sniffer: {resp.status_code}"
        time.sleep(0.5)

        # Step 2: connect WS and start sniffer — sniffer active
        ws, stop_ping, _ = _ws_connect(api, 1)

        # Step 3: enable cache on port 2 while sniffer is running
        resp = api.set_port_mode(2, "cache_bus")
        assert resp.status_code == 200, f"Failed to set port 2 cache_bus: {resp.status_code}"
        time.sleep(0.5)

        # Step 4: stop sniffer via WS — cache is active so RX timeout stays at SNIFFER
        ws.send(json.dumps({"cmd": "stop", "port": 1}))
        time.sleep(0.2)
        stop_ping.set()
        try:
            ws.close()
        except Exception:
            pass
        ws = None
        stop_ping = None

        # Step 5: disable port 2 — cache disabled
        # sniffer_set_cache_active(false) called, port 1 RX timeout set to PROXY
        # because sniff_ctx[0].enabled == false at this point
        resp = api.set_port_mode(2, "disabled")
        assert resp.status_code == 200, f"Failed to set port 2 disabled: {resp.status_code}"
        time.sleep(0.5)

        # Step 6: restart sniffer — sniffer_enable sets RX timeout back to SNIFFER
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
            f"but got {len(packets)}. "
            "This indicates the sniffer/cache RX-timeout bug is present."
        )
        print(
            f"✓ GM-20: Sniffer received {len(packets)} packets after "
            "cache-active stop then disable-cache then restart"
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
        resp = api.set_port_mode(2, original_mode_p2)
        if resp.status_code != 200:
            print(f"✗ Failed to restore port 2 mode to {original_mode_p2!r}")
        resp = api.set_port_mode(1, original_mode_p1)
        assert resp.status_code == 200, \
            f"Failed to restore port 1 mode to {original_mode_p1!r}: {resp.status_code}"


# ===========================================================================
# GM-18: Sniffer works after direct cache_bus → sniffer switch
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(60)
def test_sniffer_works_after_cache_bus_to_sniffer_direct_switch(api):
    """Sniffer on port 1 produces data after a direct cache_bus → sniffer switch.

    Regression test for a variant of the sniffer/cache interaction bug where
    the sniffer received no data after switching directly from cache_bus to
    sniffer without going through disabled in between.

    Steps:
    1. Save original port 1 mode.
    2. Switch port 1 to cache_bus mode; sleep 0.5s.
    3. Switch port 1 directly to sniffer mode (no disabled step); sleep 0.5s.
    4. Start PacketInjector to inject Modbus traffic.
    5. Connect WebSocket sniffer.
    6. Collect >=3 packets.
    7. Assert at least 3 packets were received.
    8. In finally: stop ping, send WS stop command, close WS, restore mode.
    """
    # Save original port 1 mode
    resp = api.get_info()
    assert resp.status_code == 200, f"GET /info failed: {resp.status_code}"
    original_mode = resp.json().get("rs485_1", {}).get("port_mode", "disabled")

    ws = None
    stop_ping = None
    try:
        # Step 1: switch to cache_bus and let firmware settle
        resp = api.set_port_mode(1, "cache_bus")
        assert resp.status_code == 200, \
            f"Failed to set port 1 to cache_bus: {resp.status_code}"
        time.sleep(0.5)

        # Step 2: switch directly to sniffer — no disabled step
        resp = api.set_port_mode(1, "sniffer")
        assert resp.status_code == 200, \
            f"Failed to set port 1 to sniffer: {resp.status_code}"
        time.sleep(0.5)

        # Inject Modbus traffic so the sniffer has something to observe
        with PacketInjector(port=1, include_all_fc=False):
            ws, stop_ping, _ = _ws_connect(api, 1)

            packets = _collect_packets(
                ws,
                min_count=3,
                timeout_sec=20,
                filter_fn=lambda p: p.get("type") == "packet",
            )

        assert len(packets) >= 3, (
            f"GM-18: Expected >=3 sniffer packets after direct cache_bus→sniffer "
            f"switch but got {len(packets)}. "
            "This indicates the sniffer/cache interaction bug is present."
        )
        print(
            f"✓ GM-18: Sniffer received {len(packets)} packets after "
            "direct cache_bus → sniffer switch"
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
        # Restore original port mode
        resp = api.set_port_mode(1, original_mode)
        assert resp.status_code == 200, \
            f"Failed to restore port 1 mode to {original_mode!r}: {resp.status_code}"


# ===========================================================================
# GM-19: WS sniffer on port 1 works after port 2 cache_bus enable/disable cycle
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(90)
def test_sniffer_ws_works_after_other_port_cache_cycle(api):
    """WS sniffer on port 1 produces data after port 2 undergoes a cache_bus enable/disable.

    Regression test for a bug where enabling then disabling cache on one port
    affects the sniffer on another port.  When port 2 enters cache_bus mode,
    sniffer_set_cache_active(true) is called which sets the RX timeout on port 1
    (if its sniffer is disabled).  When port 2 exits cache_bus, sniffer_set_cache_active(false)
    is called which sets port 1 RX timeout to PROXY.  Then the WS start command
    re-enables the sniffer on port 1 and must work correctly.

    Steps:
    1. Save original modes for port 1 and port 2.
    2. Set port 1 to sniffer mode (sniffer active).
    3. Connect WS to port 1 sniffer and send stop — sniffer disabled.
    4. Set port 2 to cache_bus (sniffer_set_cache_active true called for port 1).
    5. Set port 2 to disabled (sniffer_set_cache_active false called for port 1).
    6. Reconnect WS and send start command — sniffer re-enabled on port 1.
    7. Inject Modbus traffic and collect >=3 packets.
    8. Assert >=3 packets received.
    """
    # Save original modes
    resp = api.get_info()
    assert resp.status_code == 200, f"GET /info failed: {resp.status_code}"
    info = resp.json()
    original_mode_p1 = info.get("rs485_1", {}).get("port_mode", "disabled")
    original_mode_p2 = info.get("rs485_2", {}).get("port_mode", "disabled")

    ws = None
    stop_ping = None
    try:
        # Step 1: put port 1 in sniffer mode
        resp = api.set_port_mode(1, "sniffer")
        assert resp.status_code == 200, f"Failed to set port 1 to sniffer: {resp.status_code}"
        time.sleep(0.5)

        # Step 2: connect WS and immediately stop the sniffer via WS command
        # This sets sniff_ctx[0].enabled = false
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

        # Step 3: enable cache on port 2 — calls sniffer_set_cache_active(true) on port 1
        resp = api.set_port_mode(2, "cache_bus")
        assert resp.status_code == 200, f"Failed to set port 2 to cache_bus: {resp.status_code}"
        time.sleep(0.5)

        # Step 4: disable port 2 — calls sniffer_set_cache_active(false) on port 1
        # This sets port 1 RX timeout to PROXY (10 symbols) since sniffer is disabled
        resp = api.set_port_mode(2, "disabled")
        assert resp.status_code == 200, f"Failed to set port 2 to disabled: {resp.status_code}"
        time.sleep(0.5)

        # Step 5: restart sniffer on port 1 via WS start command
        # sniffer_enable(0) should reset RX timeout to SNIFFER (3 symbols)
        with PacketInjector(port=1, include_all_fc=False):
            ws, stop_ping, _ = _ws_connect(api, 1)

            packets = _collect_packets(
                ws,
                min_count=3,
                timeout_sec=20,
                filter_fn=lambda p: p.get("type") == "packet",
            )

        assert len(packets) >= 3, (
            f"GM-19: Expected >=3 sniffer packets after other-port cache cycle "
            f"but got {len(packets)}. "
            "This indicates a cross-port sniffer/cache RX-timeout interaction bug."
        )
        print(
            f"✓ GM-19: Sniffer received {len(packets)} packets after "
            "port 2 cache_bus enable/disable cycle"
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
        resp = api.set_port_mode(2, original_mode_p2)
        if resp.status_code != 200:
            print(f"✗ Failed to restore port 2 mode to {original_mode_p2!r}")
        resp = api.set_port_mode(1, original_mode_p1)
        assert resp.status_code == 200, \
            f"Failed to restore port 1 mode to {original_mode_p1!r}: {resp.status_code}"


# ===========================================================================
# GM-20: WS sniffer enabled while cache active, cache disabled, sniffer restart
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(90)
def test_sniffer_ws_restart_after_cache_disable_while_active(api):
    """WS sniffer on port 1 restarts after cache was active alongside sniffer, then disabled.

    Scenario:
    1. Port 1 in sniffer mode, WS sniffer active.
    2. Port 2 set to cache_bus — cache enabled alongside running sniffer.
    3. Stop sniffer via WS (sniffer_disable: cache active so RX timeout NOT reset).
    4. Port 2 set to disabled — cache disabled, sniffer_set_cache_active(false)
       sets port 1 RX timeout to PROXY since sniffer is now disabled.
    5. Restart sniffer via WS — sniffer_enable resets RX timeout to SNIFFER.
    6. Verify data flows through the sniffer.
    """
    resp = api.get_info()
    assert resp.status_code == 200, f"GET /info failed: {resp.status_code}"
    info = resp.json()
    original_mode_p1 = info.get("rs485_1", {}).get("port_mode", "disabled")
    original_mode_p2 = info.get("rs485_2", {}).get("port_mode", "disabled")

    ws = None
    stop_ping = None
    try:
        # Step 1: port 1 in sniffer mode
        resp = api.set_port_mode(1, "sniffer")
        assert resp.status_code == 200, f"Failed to set port 1 sniffer: {resp.status_code}"
        time.sleep(0.5)

        # Step 2: connect WS and start sniffer — sniffer active
        ws, stop_ping, _ = _ws_connect(api, 1)

        # Step 3: enable cache on port 2 while sniffer is running
        resp = api.set_port_mode(2, "cache_bus")
        assert resp.status_code == 200, f"Failed to set port 2 cache_bus: {resp.status_code}"
        time.sleep(0.5)

        # Step 4: stop sniffer via WS — cache is active so RX timeout stays at SNIFFER
        ws.send(json.dumps({"cmd": "stop", "port": 1}))
        time.sleep(0.2)
        stop_ping.set()
        try:
            ws.close()
        except Exception:
            pass
        ws = None
        stop_ping = None

        # Step 5: disable port 2 — cache disabled
        # sniffer_set_cache_active(false) called, port 1 RX timeout set to PROXY
        # because sniff_ctx[0].enabled == false at this point
        resp = api.set_port_mode(2, "disabled")
        assert resp.status_code == 200, f"Failed to set port 2 disabled: {resp.status_code}"
        time.sleep(0.5)

        # Step 6: restart sniffer — sniffer_enable sets RX timeout back to SNIFFER
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
            f"but got {len(packets)}. "
            "This indicates the sniffer/cache RX-timeout bug is present."
        )
        print(
            f"✓ GM-20: Sniffer received {len(packets)} packets after "
            "cache-active stop then disable-cache then restart"
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
        resp = api.set_port_mode(2, original_mode_p2)
        if resp.status_code != 200:
            print(f"✗ Failed to restore port 2 mode to {original_mode_p2!r}")
        resp = api.set_port_mode(1, original_mode_p1)
        assert resp.status_code == 200, \
            f"Failed to restore port 1 mode to {original_mode_p1!r}: {resp.status_code}"


# ===========================================================================
# GM-21: Fixed UI flow — sniffer → cache_bus → disabled → auto-switch to sniffer → WS start
# ===========================================================================

@pytest.mark.qemu
@pytest.mark.timeout(90)
def test_sniffer_after_register_map_cache_toggle(api):
    """Sniffer WS works after RegisterMap cache cycle with the Sniffer.vue auto-switch fix.

    The bug: RegisterMap disables cache by setting port to 'disabled' (not restoring
    the previous 'sniffer' mode).  The fix: Sniffer.vue startCapture() checks the current
    port mode, and if it is not 'sniffer' or 'cache_bus', it auto-switches to 'sniffer'
    before connecting the WebSocket.

    This test simulates the fixed behavior:
    1. Port 1 starts in sniffer mode (user was using Sniffer UI).
    2. RegisterMap enables cache — port 1 → cache_bus.
    3. RegisterMap disables cache — port 1 → 'disabled'.
    4. Sniffer.vue startCapture() detects port is not 'sniffer'/'cache_bus',
       auto-switches to 'sniffer' via POST /ports/1/mode.
    5. Waits 500ms for firmware port reinit.
    6. Connects WS and sends start command.
    7. Verifies data arrives.
    """
    resp = api.get_info()
    assert resp.status_code == 200, f"GET /info failed: {resp.status_code}"
    original_mode = resp.json().get("rs485_1", {}).get("port_mode", "disabled")

    ws = None
    stop_ping = None
    try:
        # Step 1: port 1 starts in sniffer mode (simulate normal sniffer use)
        resp = api.set_port_mode(1, "sniffer")
        assert resp.status_code == 200, f"Failed to set port 1 to sniffer: {resp.status_code}"
        time.sleep(0.5)

        # Baseline: confirm sniffer works initially
        with PacketInjector(port=1, include_all_fc=False):
            ws, stop_ping, _ = _ws_connect(api, 1)
            baseline_pkts = _collect_packets(
                ws, min_count=3, timeout_sec=15,
                filter_fn=lambda p: p.get("type") == "packet",
            )
            assert len(baseline_pkts) >= 3, (
                f"GM-21 baseline: expected >=3 packets in sniffer mode, got {len(baseline_pkts)}"
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
        print(f"  GM-21 baseline: {len(baseline_pkts)} packets in sniffer mode OK")

        # Step 2: RegisterMap enables cache — port 1 → cache_bus
        resp = api.set_port_mode(1, "cache_bus")
        assert resp.status_code == 200, f"Failed to set port 1 to cache_bus: {resp.status_code}"
        time.sleep(0.5)

        # Step 3: RegisterMap disables cache — port 1 → 'disabled' (NOT sniffer!)
        # This is the exact RegisterMap.toggleCache() behavior.
        resp = api.set_port_mode(1, "disabled")
        assert resp.status_code == 200, f"Failed to set port 1 to disabled: {resp.status_code}"
        time.sleep(0.5)

        # Confirm port is disabled
        info_resp = api.get_info()
        assert info_resp.status_code == 200
        current_mode = info_resp.json().get("rs485_1", {}).get("port_mode", "unknown")
        assert current_mode == "disabled", f"Expected 'disabled', got '{current_mode}'"

        # Step 4: Sniffer.vue startCapture() FIX — auto-switch port to 'sniffer' when
        # port mode is not 'sniffer' or 'cache_bus'.
        # This simulates what the fixed Sniffer.vue does before connecting WS.
        resp = api.set_port_mode(1, "sniffer")
        assert resp.status_code == 200, f"Auto-switch to sniffer failed: {resp.status_code}"
        time.sleep(0.5)  # firmware port reinit delay (matches the 500ms in Sniffer.vue fix)

        # Step 5: Now connect WS and verify data arrives — fix should make this work.
        with PacketInjector(port=1, include_all_fc=False):
            ws, stop_ping, _ = _ws_connect(api, 1)
            post_cache_pkts = _collect_packets(
                ws, min_count=3, timeout_sec=15,
                filter_fn=lambda p: p.get("type") == "packet",
            )

        assert len(post_cache_pkts) >= 3, (
            f"GM-21: Expected >=3 sniffer packets after fixed RegisterMap cache toggle flow "
            f"but got {len(post_cache_pkts)}. "
            "The auto-switch to sniffer mode before WS connect should have fixed this."
        )
        print(
            f"✓ GM-21: Sniffer received {len(post_cache_pkts)} packets after "
            "RegisterMap cache cycle with auto-switch fix"
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
        resp = api.set_port_mode(1, original_mode)
        assert resp.status_code == 200, \
            f"Failed to restore port 1 mode to {original_mode!r}: {resp.status_code}"
