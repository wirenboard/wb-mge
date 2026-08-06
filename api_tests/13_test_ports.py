"""Port modes, sniffer status, and WB test endpoint tests"""

import json
import time

import pytest
import requests
from urllib.parse import urlparse

from sniffer_helpers import _ws_connect
from io_bus_helpers import IoBus


@pytest.fixture(scope="module", autouse=True)
def _baseline(api):
    resp = api.set_port_mode(1, "tcp_bridge")    # first assertion: sniffer.port_1 == False
    assert resp.status_code == 200, f"_baseline: set_port_mode(1, tcp_bridge) failed: {resp.status_code} {resp.text}"
    resp = api.set_port_mode(2, "tcp_bridge")    # first assertion: sniffer.port_2 == False
    assert resp.status_code == 200, f"_baseline: set_port_mode(2, tcp_bridge) failed: {resp.status_code} {resp.text}"
    resp = api.set_wb_test(False)                # test expects a known baseline for clock_out
    assert resp.status_code == 200, f"_baseline: set_wb_test(False) failed: {resp.status_code} {resp.text}"


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


@pytest.mark.qemu
def test_wb_test_leds_coupling(api):
    """clock_out factory test lights all front-panel LEDs and drives V-out.

    Over the QEMU virtual IO bus we can observe the expander-driven LEDs:
    E06 = V-out, E04 = WiFi LED (inverted, on == level 0), E05 = Eth LED
    (inverted, on == 0), E07 = Status LED (non-inverted, on == 1).

    The RS-485-1/RS-485-2 activity LEDs are tapped in hardware from the UART1/UART2
    TX lines and are NOT observable over the QEMU IO bus (the 100 kHz LEDC signal
    bypasses the gpio shim); they are verified on real hardware. As the observable
    proxy for the RS-485-2 path, this test asserts that bridge port 2 (RS-485-2) is
    switched to "disabled" while clock_out is on (freeing its UART2 TX pin) and is
    restored to its baseline mode afterwards.
    """
    original = api.get_wb_test().json()["clock_out"]

    with IoBus() as bus:
        bus.pump(0.5)
        # clock_out=false restores V-out to its configured state (not
        # unconditionally off), so capture the baseline before the test.
        vout_baseline = bus.get("E06")

        # Baseline RS-485-2 port mode; clock_out must disable this port (to free
        # its UART2 TX pin for the LEDC output) and restore it afterwards.
        port2_baseline = api.get_info().json().get("rs485_2", {}).get("port_mode")

        try:
            response = api.set_wb_test(True)
            assert response.status_code == 200, \
                f"POST /wb_test clock_out=true expected 200, got {response.status_code}"

            # Factory-test coupling: V-out (E06) and all indicator LEDs light up.
            assert bus.wait_for("E06", 1, timeout=5.0), "clock_out=true must turn V-out (E06) on"
            assert bus.wait_for("E07", 1, timeout=5.0), "clock_out=true must turn Status LED (E07) on"
            assert bus.wait_for("E04", 0, timeout=5.0), "clock_out=true must turn WiFi LED (E04) on"
            assert bus.wait_for("E05", 0, timeout=5.0), "clock_out=true must turn Eth LED (E05) on"
            print("✓ clock_out=true lit V-out + indicator LEDs")

            # clock_out frees the RS-485-2 UART2 TX pin (GPIO14) by disabling the port.
            port2_during = api.get_info().json().get("rs485_2", {}).get("port_mode")
            assert port2_during == "disabled", \
                f"clock_out=true must disable RS-485-2 port, got {port2_during!r}"
            print(f"✓ clock_out=true disabled RS-485-2 port (was {port2_baseline!r})")

            response = api.set_wb_test(False)
            assert response.status_code == 200, \
                f"POST /wb_test clock_out=false expected 200, got {response.status_code}"

            # Symmetry: V-out must return to its pre-test configured state.
            assert bus.wait_for("E06", vout_baseline, timeout=5.0), \
                f"clock_out=false must restore V-out (E06) to baseline {vout_baseline}"
            print(f"✓ clock_out=false restored V-out (E06) to baseline {vout_baseline}")

            # RS-485-2 port mode must be restored to its pre-test baseline.
            port2_after = api.get_info().json().get("rs485_2", {}).get("port_mode")
            assert port2_after == port2_baseline, \
                f"clock_out=false must restore RS-485-2 port to {port2_baseline!r}, got {port2_after!r}"
            print(f"✓ clock_out=false restored RS-485-2 port to {port2_baseline!r}")

        finally:
            api.set_wb_test(original)
            print(f"✓ clock_out restored to {original}")


@pytest.mark.qemu
def test_clock_out_keeps_rs485_2_de_low(api):
    """clock_out must never enable the RS-485-2 transceiver driver.

    This is the regression test for the decision behind review comment #30 ("emit the
    100 kHz on the second RS-485 too, i.e. raise GPIO15"), which was DECLINED: the
    RS-485-2 pair is shared with the MIO transceiver and wired out to the external
    RS-485-2 terminals, so the factory meander must not reach it.

    Holding that line low is the firmware's job, not the hardware's. Disabling port 2
    only deletes the UART driver — it does not release the dir pin, which stays a
    push-pull output at the level UART2 RTS left there (HIGH = TX enabled), and a weak
    external pulldown cannot pull down a driven pad. So wb_test.c parks the port-2 DE
    line (G15 = SERIAL_IO_PIN_2, GPIO15 on WB-MGE) LOW itself for the whole test.

    Asserted over the QEMU IO bus:
      * baseline: both DE lines idle HIGH (RTS-attached, TX-enabled);
      * during the test: G15 drops to 0, as a driven OUTPUT (D15 == 1), and no ("G15", 1)
        record appears afterwards — the driver is off for the whole run, not just at the end;
      * positive control: G04 (port-1 DE) is HIGH, i.e. the RS-485-1 driver IS enabled
        and the meander really does reach that bus. The asymmetry is the point;
      * on exit: the pin is HANDED OVER, not released. wb_test.c deliberately does not
        gpio_reset_pin() it — a reset pad carries the internal pull-up, which would tug the
        DE line towards "driver enabled" for the whole port re-init window (an NVS read plus
        a UART init), and WB-MGU has no external pulldown to fight it. So no ("D15", 0)
        record may appear after the park: the pin goes straight from our driven LOW to the
        UART's OUTPUT. Port 2 is configured tcp_bridge here, so the exit path re-inits it and
        uart_set_pin() puts the line back at its idle HIGH — that final G15 == 1 is the
        UART's doing. Had the port stayed disabled, the pin would simply have stayed LOW,
        which is equally correct: DE=0 is receive mode, i.e. the bus is not driven.

    Both windows below are anchored at the ("G15", 0) parking record itself, not at the
    request and not at "wherever the event list happened to stand after the asserts above".
    Entry captures the pin with gpio_reset_pin(), and the QEMU model reports that capture
    as a fresh INPUT: a ("D15", 0) plus an idle-HIGH ("G15", 1) record (virtual_io_qemu.c).
    Both are artifacts of taking the pin — the G record re-states the level the UART had
    already left on the line, it is not a rise this test drives — and both land before the
    park record, so anchoring there excludes them. Everything from the park onwards must be
    a flat, driven 0.
    """
    original_clock_out = api.get_wb_test().json()["clock_out"]
    info = api.get_info().json()
    original_1 = info.get("rs485_1", {}).get("port_mode", "tcp_bridge")
    original_2 = info.get("rs485_2", {}).get("port_mode", "tcp_bridge")

    try:
        # Both ports must be in an active transport, so their DE pins are RTS-attached
        # and idle HIGH. That is what makes the assertion below meaningful: G15 starts
        # at 1 and only the firmware can bring it down.
        assert api.set_port_mode(1, "tcp_bridge").status_code == 200
        assert api.set_port_mode(2, "tcp_bridge").status_code == 200
        time.sleep(1.0)

        with IoBus() as bus:
            assert bus.wait_for("G04", 1, timeout=5.0), \
                f"Baseline: RS-485-1 DE (G04) expected idle HIGH, got {bus.get('G04')}"
            assert bus.wait_for("G15", 1, timeout=5.0), \
                f"Baseline: RS-485-2 DE (G15) expected idle HIGH, got {bus.get('G15')}"

            response = api.set_wb_test(True)
            assert response.status_code == 200, \
                f"POST /wb_test clock_out=true expected 200, got {response.status_code}"

            try:
                # The test takes the port-2 DE line and drives it LOW (receive mode).
                assert bus.wait_for("G15", 0, timeout=5.0), \
                    f"clock_out=true must park the RS-485-2 DE line (G15) LOW, got {bus.get('G15')}"
                # ...as a driven output, not as a released/floating pin.
                assert bus.get("D15") == 1, \
                    f"RS-485-2 DE (G15) must be a driven OUTPUT while parked, D15 == {bus.get('D15')}"
                print("✓ clock_out=true parked the RS-485-2 DE line low (G15 == 0, driven)")

                # Positive control: the RS-485-1 driver IS enabled, so the waveform
                # reaches that bus. Only the port-2 pair is kept silent.
                assert bus.wait_for("G04", 1, timeout=5.0), \
                    f"clock_out=true must raise the RS-485-1 DE line (G04), got {bus.get('G04')}"
                print("✓ clock_out=true raised the RS-485-1 DE line (G04 == 1)")

                # It must stay low for the WHOLE test, not just settle low: watch the
                # event stream, since even a momentary G15 -> 1 would key the RS-485-2
                # driver and put the meander on a bus we do not own.
                #
                # Anchor the window at the parking record itself, not at the current end of
                # the event list: the asserts above ran a wait_for() and a get(), and the
                # records that arrived while they did (the park's own D15 -> 1, the G04 rise)
                # would otherwise fall into no window at all. The park is the last ("G15", 0)
                # seen so far — the wait_for("G15", 0) above guarantees there is one, and no
                # rise can have re-armed a second one.
                parked_at = len(bus.events) - 1 - bus.events[::-1].index(("G15", 0))
                bus.pump(2.0)
                assert ("G15", 1) not in bus.events[parked_at:], \
                    "RS-485-2 DE (G15) went HIGH during clock_out: the port-2 driver was keyed"
                assert bus.get("G15") == 0, \
                    f"RS-485-2 DE (G15) must stay LOW for the whole test, got {bus.get('G15')}"
                print("✓ RS-485-2 DE line stayed low for the whole clock_out test")

            finally:
                response = api.set_wb_test(False)
                assert response.status_code == 200, \
                    f"POST /wb_test clock_out=false expected 200, got {response.status_code}"

            # Exit hands the parked pin OVER, it never releases it: port 2 comes up from
            # NVS (tcp_bridge) and uart_set_pin() re-attaches its RTS, which is the only
            # thing that puts the DE line back at the UART's TX-enabled idle level.
            assert bus.wait_for("G15", 1, timeout=5.0), \
                f"port 2 coming back up must hand the RS-485-2 DE line to the UART, got {bus.get('G15')}"
            assert bus.get("D15") == 1, \
                f"RS-485-2 DE (G15) must be an OUTPUT once the UART owns it, D15 == {bus.get('D15')}"

            # The invariant: once parked, the pin is never released back to a pad.
            # wb_test.c must not gpio_reset_pin() it on the way out — that would put it in
            # GPIO_MODE_DISABLE with the internal pull-up on, weakly asserting DE (= keying
            # the driver on a bus we do not own) for the whole re-init window (an NVS read
            # plus a UART init, tens of ms), with no external pulldown on WB-MGU to fight it.
            # A released pad shows up on the bus as ("D15", 0), so from the parking record
            # to the moment the UART owns it there must be none.
            #
            # "Once parked" is the honest bound, not "never": taking the pin in the first
            # place goes through gpio_reset_pin() (de_pin_latch_low_output()), which does
            # release the pad to its internal pull-up — but only for the handful of register
            # writes until the direction is set back to OUTPUT, and that ("D15", 0) lands
            # BEFORE the ("G15", 0) anchor this window starts at.
            assert ("D15", 0) not in bus.events[parked_at:], \
                "RS-485-2 DE (G15) was released to a pulled-up pad instead of being held low " \
                "until the UART took it back"
            print("✓ RS-485-2 DE line stayed driven until the UART took it back (no D15 -> 0)")

    finally:
        api.set_wb_test(original_clock_out)
        restore_errors = []
        for port_num, mode in [(1, original_1), (2, original_2)]:
            resp = api.set_port_mode(port_num, mode)
            if resp.status_code != 200:
                restore_errors.append(f"port {port_num} -> {mode}: {resp.status_code}")
        if restore_errors:
            raise AssertionError("Port mode restore failed: " + "; ".join(restore_errors))


def test_clock_out_freezes_port_mode(api):
    """clock_out freezes the ports: mode changes are rejected and NVS is untouched.

    While the test runs it owns the TX and DE pins of both ports (the LEDC drives the two
    TX lines; the two DE lines are driven as plain GPIOs), so a persisting mode change
    (POST /ports/N/mode) must not go through: the firmware rejects it with 409 Conflict
    instead of handing the pins back to the UART. The runtime mode is
    DISABLED for the duration, but that is deliberately NOT persisted — GET /settings
    reads straight from NVS and must still show the mode configured before the test,
    both during it and after it. On exit both ports come back up from NVS.

    The baseline mode is "passive" (not the default "tcp_bridge"), so a rejected write
    that leaked into NVS anyway would be visible instead of matching the default.
    """
    info = api.get_info().json()
    original_1 = info.get("rs485_1", {}).get("port_mode", "tcp_bridge")
    original_2 = info.get("rs485_2", {}).get("port_mode", "tcp_bridge")
    original_clock_out = api.get_wb_test().json()["clock_out"]

    try:
        resp = api.set_port_mode(1, "passive")
        assert resp.status_code == 200, \
            f"Baseline set_port_mode(1, passive) expected 200, got {resp.status_code}"
        nvs_before = api.get_settings().json().get("rs485_1", {}).get("port_mode")
        assert nvs_before == "passive", \
            f"Baseline: NVS rs485_1.port_mode expected 'passive', got {nvs_before!r}"

        response = api.set_wb_test(True)
        assert response.status_code == 200, \
            f"POST /wb_test clock_out=true expected 200, got {response.status_code}"

        try:
            # (a) Both ports are frozen: a mode change is a conflict, not an error.
            for port in (1, 2):
                resp = api.set_port_mode(port, "repeater")
                assert resp.status_code == 409, (
                    f"POST /ports/{port}/mode during clock_out expected 409, "
                    f"got {resp.status_code}: {resp.text}"
                )
            print("✓ POST /ports/{1,2}/mode during clock_out rejected with 409")

            # The runtime mode is DISABLED while the LEDC drives the TX pins...
            info_during = api.get_info().json()
            assert info_during.get("rs485_1", {}).get("port_mode") == "disabled", \
                f"clock_out=true must disable port 1, got {info_during.get('rs485_1')}"

            # ...but NVS still holds the configured mode: neither the transient DISABLED
            # nor the rejected "repeater" may reach it.
            nvs_during = api.get_settings().json().get("rs485_1", {}).get("port_mode")
            assert nvs_during == "passive", (
                f"NVS rs485_1.port_mode must stay 'passive' during clock_out, "
                f"got {nvs_during!r}"
            )
            print("✓ NVS port_mode untouched while clock_out is active")

        finally:
            response = api.set_wb_test(False)
            assert response.status_code == 200, \
                f"POST /wb_test clock_out=false expected 200, got {response.status_code}"

        time.sleep(0.5)

        # (b) The mode in NVS is unchanged and the port is restored from it.
        nvs_after = api.get_settings().json().get("rs485_1", {}).get("port_mode")
        assert nvs_after == "passive", \
            f"After clock_out, NVS rs485_1.port_mode must still be 'passive', got {nvs_after!r}"
        mode_after = api.get_info().json().get("rs485_1", {}).get("port_mode")
        assert mode_after == "passive", \
            f"After clock_out, port 1 must be restored from NVS to 'passive', got {mode_after!r}"
        print("✓ port_mode restored from NVS after clock_out (409 write never persisted)")

    finally:
        api.set_wb_test(original_clock_out)
        restore_errors = []
        for port_num, mode in [(1, original_1), (2, original_2)]:
            resp = api.set_port_mode(port_num, mode)
            if resp.status_code != 200:
                restore_errors.append(f"port {port_num} -> {mode}: {resp.status_code}")
        if restore_errors:
            raise AssertionError("Port mode restore failed: " + "; ".join(restore_errors))


@pytest.mark.qemu
def test_clock_out_leaves_io_bus_alone(api):
    """clock_out must not disturb the MIO controller, and must not gate the io_bus setting.

    The clock_out test drives the logic-side TX (DI) line of RS-485-2 so that LED2 blinks,
    but it never enables that transceiver's driver: it holds SERIAL_IO_PIN_2 (U4.DE) LOW
    itself for the whole run (see test_clock_out_keeps_rs485_2_de_low). The RS-485-2 pair
    the MIO controller shares therefore stays silent, so the test has no reason to touch
    the I/O bus — its reset line (E08) must keep whatever the io_bus setting put there,
    all the way through the test.

    And because the test does not own the I/O bus, an io_bus written via POST /settings
    while the test runs must reach the hardware immediately, not be deferred to test exit
    (the exit path does not re-apply it).
    """
    original_clock_out = api.get_wb_test().json()["clock_out"]
    original_io_bus = api.get_settings().json().get("io_bus")

    with IoBus() as bus:
        bus.pump(0.5)
        try:
            # Baseline: the I/O bus is ON, so any reset pulse by the test would show up as
            # an E08 -> 0 event.
            resp = api.update_settings({"io_bus": True})
            assert resp.status_code == 200, \
                f"Baseline POST /settings io_bus=true expected 200, got {resp.status_code}"
            assert bus.wait_for("E08", 1, timeout=5.0), \
                f"Baseline: io_bus=true must drive E08 high, got {bus.get('E08')}"

            first_new_event = len(bus.events)
            response = api.set_wb_test(True)
            assert response.status_code == 200, \
                f"POST /wb_test clock_out=true expected 200, got {response.status_code}"

            # The test must not reset MIO. Watch the event stream, not just the final
            # level: even a momentary E08 -> 0 would drop the I/O bus the test has no
            # business touching.
            bus.pump(1.0)
            assert ("E08", 0) not in bus.events[first_new_event:], \
                "clock_out=true must not reset the MIO controller (E08 went low)"
            assert bus.get("E08") == 1, \
                f"I/O bus must stay enabled during clock_out, E08 == {bus.get('E08')}"
            print("✓ clock_out=true left the I/O bus alone (E08 == 1)")

            # The io_bus setting is not frozen by the test: it still reaches the hardware.
            resp = api.update_settings({"io_bus": False})
            assert resp.status_code == 200, \
                f"POST /settings io_bus=false during clock_out expected 200, got {resp.status_code}"
            assert bus.wait_for("E08", 0, timeout=5.0), \
                f"io_bus=false during clock_out must drive E08 low, got {bus.get('E08')}"
            print("✓ POST /settings io_bus=false during clock_out reached the hardware")

            resp = api.update_settings({"io_bus": True})
            assert resp.status_code == 200, \
                f"POST /settings io_bus=true during clock_out expected 200, got {resp.status_code}"
            assert bus.wait_for("E08", 1, timeout=5.0), \
                f"io_bus=true during clock_out must drive E08 high, got {bus.get('E08')}"

            first_new_event = len(bus.events)
            response = api.set_wb_test(False)
            assert response.status_code == 200, \
                f"POST /wb_test clock_out=false expected 200, got {response.status_code}"

            # Leaving the test must not touch the I/O bus either.
            bus.pump(1.0)
            assert ("E08", 0) not in bus.events[first_new_event:], \
                "clock_out=false must not reset the MIO controller (E08 went low)"
            assert bus.get("E08") == 1, \
                f"I/O bus must stay enabled after clock_out, E08 == {bus.get('E08')}"
            print("✓ clock_out=false left the I/O bus alone (E08 == 1)")

        finally:
            api.set_wb_test(original_clock_out)
            if original_io_bus is not None:
                api.update_settings({"io_bus": original_io_bus})
                print(f"✓ io_bus restored to {original_io_bus}")


def test_sniffer_status(api):
    """Test GET /sniffer/status and verify it reflects the live WS sniffer overlay.

    The sniffer is now a display overlay driven by the WS start/stop commands, not
    a port transport mode. The port just needs its serial open (passive transport)
    so the overlay has something to sniff.
    """
    info_response = api.get_info()
    assert info_response.status_code == 200
    info_data = info_response.json()
    original_port_1_mode = info_data.get("rs485_1", {}).get("port_mode", "tcp_bridge")
    print(f"  Port 1 original mode: {original_port_1_mode}")

    ws = None
    stop_ping = None
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

        # Open serial (passive transport) and activate the live sniffer overlay via WS.
        response = api.set_port_mode(1, "passive")
        assert response.status_code == 200, \
            f"POST /ports/1/mode passive expected 200, got {response.status_code}"
        time.sleep(0.5)

        ws, stop_ping, _ = _ws_connect(api, 1)
        time.sleep(0.5)
        response = api.get_sniffer_status()
        assert response.status_code == 200
        status = response.json()
        assert status["port_1"] == True, \
            f"After starting the WS sniffer overlay, port_1 must be true, got {status['port_1']}"
        print("✓ After WS sniffer start: port_1=true")

        # Stop the live sniffer overlay; the status must clear.
        ws.send(json.dumps({"cmd": "stop", "port": 1}))
        time.sleep(0.5)
        response = api.get_sniffer_status()
        assert response.status_code == 200
        status = response.json()
        assert status["port_1"] == False, \
            f"After stopping the WS sniffer overlay, port_1 must be false, got {status['port_1']}"
        print("✓ After WS sniffer stop: port_1=false")

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
        try:
            api.set_port_mode(1, original_port_1_mode)
            print(f"✓ Port 1 mode restored to {original_port_1_mode}")
        except Exception as exc:
            raise AssertionError(f"Failed to restore port 1 mode: {exc}")


def test_port_modes(api):
    """Test POST /ports/{n}/mode — all modes, both ports"""
    info_response = api.get_info()
    assert info_response.status_code == 200
    info_data = info_response.json()
    original_port_1_mode = info_data.get("rs485_1", {}).get("port_mode", "tcp_bridge")
    original_port_2_mode = info_data.get("rs485_2", {}).get("port_mode", "tcp_bridge")
    print(f"  Original modes: port_1={original_port_1_mode}, port_2={original_port_2_mode}")

    try:
        for mode in ["disabled", "tcp_bridge", "passive", "repeater"]:
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

        # The cache is now an orthogonal overlay (POST /ports/N/cache), not a
        # transport mode. Toggle it on port 1 (now passive) and verify /info.
        response = api.set_port_cache(1, True)
        assert response.status_code == 200, \
            f"POST /ports/1/cache enabled=true expected 200, got {response.status_code}"
        info_resp = api.get_info()
        assert info_resp.status_code == 200
        assert info_resp.json().get("rs485_1", {}).get("cache_enabled") is True, \
            "After enabling the cache overlay, rs485_1.cache_enabled must be true"
        print("✓ Port 1 cache overlay enabled and verified via /info")
        response = api.set_port_cache(1, False)
        assert response.status_code == 200, \
            f"POST /ports/1/cache enabled=false expected 200, got {response.status_code}"
        info_resp = api.get_info()
        assert info_resp.status_code == 200
        assert info_resp.json().get("rs485_1", {}).get("cache_enabled") is False, \
            "After disabling the cache overlay, rs485_1.cache_enabled must be false"
        print("✓ Port 1 cache overlay disabled and verified via /info")

        for mode in ["passive", "disabled"]:
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

        # The removed transport modes must now be rejected.
        for removed in ["sniffer", "cache_bus"]:
            response = api.set_port_mode(1, removed)
            assert response.status_code == 400, \
                f"POST /ports/1/mode '{removed}' (removed mode) expected 400, got {response.status_code}"
        print("✓ Removed modes 'sniffer'/'cache_bus' rejected with 400")

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


def test_port_cache_invalid_body_returns_400(api):
    """POST /ports/{n}/cache must reject malformed bodies with HTTP 400.

    Drives the error branches of port_set_cache_handler that the happy-path
    toggle in test_port_modes does not exercise: a non-bool 'enabled' value, a
    body with no 'enabled' key, and a syntactically invalid JSON body. These are
    pure error-path requests; none of them should change the port state.
    """
    # Non-bool 'enabled' (string instead of bool) -> 400.
    response = api.session.post(
        f"{api.base_url}/ports/1/cache", json={"enabled": "yes"}, timeout=10
    )
    assert response.status_code == 400, (
        f"POST /ports/1/cache with string 'enabled' expected 400, "
        f"got {response.status_code}: {response.text}"
    )
    print("✓ POST /ports/1/cache with string 'enabled' rejected with 400")

    # Missing 'enabled' key entirely -> 400.
    response = api.session.post(f"{api.base_url}/ports/1/cache", json={}, timeout=10)
    assert response.status_code == 400, (
        f"POST /ports/1/cache with no 'enabled' key expected 400, "
        f"got {response.status_code}: {response.text}"
    )
    print("✓ POST /ports/1/cache with missing 'enabled' key rejected with 400")

    # Invalid JSON body -> 400.
    response = api.session.post(
        f"{api.base_url}/ports/1/cache",
        data="{not json",
        headers={"Content-Type": "application/json"},
        timeout=10,
    )
    assert response.status_code == 400, (
        f"POST /ports/1/cache with invalid JSON body expected 400, "
        f"got {response.status_code}: {response.text}"
    )
    print("✓ POST /ports/1/cache with invalid JSON body rejected with 400")


# ---------------------------------------------------------------------------
# Group 3: /sniffer/status endpoint
# ---------------------------------------------------------------------------

def test_sniffer_status_response_shape_and_content_type(api):
    """GET /sniffer/status must return 200 with application/json and keys port_1/port_2."""
    # No live WS sniffer overlay is active in this test, so the status must read False.
    info = api.get_info()
    assert info.status_code == 200
    info_data = info.json()
    port1_mode = info_data.get("rs485_1", {}).get("port_mode", "tcp_bridge")
    port2_mode = info_data.get("rs485_2", {}).get("port_mode", "tcp_bridge")

    restored_port1 = False
    restored_port2 = False

    try:
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


def test_sniffer_status_reflects_start_command(api):
    """/sniffer/status must report port_1==True after WS start command for port 1."""
    original_port_mode = None
    ws = None
    stop_ping = None

    try:
        info = api.get_info()
        assert info.status_code == 200
        original_port_mode = info.json().get("rs485_1", {}).get("port_mode", "tcp_bridge")

        r = api.set_port_mode(1, "passive")
        assert r.status_code == 200, f"Failed to set passive mode: {r.status_code}"
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


def test_sniffer_status_reflects_stop_command(api):
    """/sniffer/status must report port_1==False after WS stop command."""
    original_port_mode = None
    ws = None
    stop_ping = None

    try:
        info = api.get_info()
        assert info.status_code == 200
        original_port_mode = info.json().get("rs485_1", {}).get("port_mode", "tcp_bridge")

        r = api.set_port_mode(1, "passive")
        assert r.status_code == 200, f"Failed to set passive mode: {r.status_code}"
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


def test_sniffer_status_both_ports_independent(api):
    """Per-port sniffer state (port 1 vs port 2) must be independently controllable.

    The sniffer WebSocket is a SINGLE client slot per device by design — a second
    connection evicts the first ("one client, the newest wins"; see the comment in
    main/bridge/sniffer.c, and the frontend Sniffer.vue which opens exactly one
    socket and multiplexes both ports over it via {cmd,port} frames). Per-port
    capture state, however, is genuinely independent (sniff_ctx[0]/[1]). This test
    therefore drives BOTH ports over ONE socket and verifies that independence —
    the earlier two-socket version was wrong: the second connect evicted the first,
    so a later stop on the dead socket was silently dropped.
    """
    original_mode_1 = None
    original_mode_2 = None
    ws = None
    stop_ping = None

    try:
        info = api.get_info()
        assert info.status_code == 200
        info_data = info.json()
        original_mode_1 = info_data.get("rs485_1", {}).get("port_mode", "tcp_bridge")
        original_mode_2 = info_data.get("rs485_2", {}).get("port_mode", "tcp_bridge")

        # Set port 1 to sniffer and start it (the single socket sends {start,port:1}).
        r = api.set_port_mode(1, "passive")
        assert r.status_code == 200, f"Failed to set passive mode for port 1: {r.status_code}"
        time.sleep(0.3)

        ws, stop_ping, _ = _ws_connect(api, 1)
        time.sleep(0.5)

        body = api.get_sniffer_status().json()
        assert body.get("port_1") is True, f"Expected port_1==True, got {body}"
        assert body.get("port_2") is False, f"Expected port_2==False, got {body}"

        # Start port 2 over the SAME socket (do not open a second one — it would
        # evict this session).
        r2 = api.set_port_mode(2, "passive")
        assert r2.status_code == 200, \
            f"set_port_mode(2, 'passive') expected 200, got {r2.status_code}"
        time.sleep(0.3)

        ws.send(json.dumps({"cmd": "start", "port": 2}))
        time.sleep(0.5)

        body = api.get_sniffer_status().json()
        assert body.get("port_1") is True, f"Expected port_1==True, got {body}"
        assert body.get("port_2") is True, f"Expected port_2==True, got {body}"

        # Stop only port 1 over the same socket; port 2 must stay up (independence).
        ws.send(json.dumps({"cmd": "stop", "port": 1}))
        time.sleep(0.3)

        body = api.get_sniffer_status().json()
        assert body.get("port_1") is False, (
            f"Expected port_1==False after stop, got {body}"
        )
        assert body.get("port_2") is True, (
            f"Expected port_2==True (unaffected by stopping port 1), got {body}"
        )
        print("✓ port 1 and port 2 sniffer states are independent over one socket")

    finally:
        if stop_ping is not None:
            stop_ping.set()
        if ws is not None:
            try:
                ws.send(json.dumps({"cmd": "stop", "port": 1}))
                ws.send(json.dumps({"cmd": "stop", "port": 2}))
            except Exception:
                pass
            try:
                ws.close()
            except Exception:
                pass
        if original_mode_1 is not None:
            r = api.set_port_mode(1, original_mode_1)
            assert r.status_code == 200, f"Failed to restore port 1 mode: {r.status_code}"
        if original_mode_2 is not None:
            r = api.set_port_mode(2, original_mode_2)
            assert r.status_code == 200, f"Failed to restore port 2 mode: {r.status_code}"


def test_sniffer_status_post_method_rejected(api):
    """POST /sniffer/status must return HTTP 405 (method not allowed)."""
    response = api.session.post(f"{api.base_url}/sniffer/status", timeout=10)
    assert response.status_code == 405, (
        f"Expected HTTP 405 for POST /sniffer/status, got {response.status_code}"
    )
    print("✓ POST /sniffer/status returns 405")


def test_info_repeater_object_shape(api):
    """C-1: GET /info must expose a well-formed 'repeater' object.

    Validates the info_handlers.c repeater block against the openapi.yaml schema:
    the object is present, all six keys exist, 'active' is a bool, and the five
    counters/uptime are non-negative ints. Catches a dropped/renamed field or a
    type drift (e.g. a counter serialized as a string) between firmware and API.
    """
    resp = api.get_info()
    assert resp.status_code == 200, f"GET /info expected 200, got {resp.status_code}"
    data = resp.json()
    assert "repeater" in data, f"'repeater' object missing from /info: keys={list(data.keys())}"
    rep = data["repeater"]
    assert isinstance(rep, dict), f"'repeater' must be an object, got {type(rep)}"

    assert "active" in rep, f"'active' missing from repeater: {rep}"
    assert isinstance(rep["active"], bool), f"'active' must be bool, got {type(rep['active'])}"

    for key in ("uptime_ms", "bytes_1to2", "bytes_2to1", "dropped_1", "dropped_2"):
        assert key in rep, f"'{key}' missing from repeater: {rep}"
        val = rep[key]
        # In Python bool is a subclass of int — reject bools explicitly for the counters.
        assert isinstance(val, int) and not isinstance(val, bool), \
            f"'{key}' must be an integer, got {type(val)}: {val!r}"
        assert val >= 0, f"'{key}' must be >= 0, got {val}"
    print("✓ /info.repeater shape validated (6 keys, correct types)")
