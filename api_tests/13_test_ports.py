"""RS-485 port parameters and the factory wb_test endpoint"""

import time

import pytest

from io_bus_helpers import IoBus


@pytest.fixture(scope="module", autouse=True)
def _baseline(api):
    # Defense in depth: explicitly clear tx_disabled on both ports, so this file's
    # "DE idles HIGH" premise (test_clock_out_keeps_rs485_2_de_low) cannot be silently
    # broken by a tx_disabled=True leaked from an earlier test file. Writing it re-inits
    # both ports, so their UARTs come up reading the cleared NVS and the shim drives DE
    # HIGH.
    resp = api.update_settings({
        "rs485_1": {"tx_disabled": False},
        "rs485_2": {"tx_disabled": False},
    })
    assert resp.status_code == 200, f"_baseline: clear tx_disabled failed: {resp.status_code} {resp.text}"
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
    bypasses the gpio shim); they are verified on real hardware. The RS-485-2 path is
    covered instead by test_clock_out_keeps_rs485_2_de_low, which watches the DE line
    over the same bus.
    """
    original = api.get_wb_test().json()["clock_out"]

    with IoBus() as bus:
        bus.pump(0.5)
        # clock_out=false restores V-out to its configured state (not
        # unconditionally off), so capture the baseline before the test.
        vout_baseline = bus.get("E06")

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

            response = api.set_wb_test(False)
            assert response.status_code == 200, \
                f"POST /wb_test clock_out=false expected 200, got {response.status_code}"

            # Symmetry: V-out must return to its pre-test configured state.
            assert bus.wait_for("E06", vout_baseline, timeout=5.0), \
                f"clock_out=false must restore V-out (E06) to baseline {vout_baseline}"
            print(f"✓ clock_out=false restored V-out (E06) to baseline {vout_baseline}")

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
        UART's OUTPUT. Port 2's serial is open here, so the exit path re-inits it and
        uart_set_pin() puts the line back at its idle HIGH — that final G15 == 1 is the
        UART's doing. Had the port stayed closed, the pin would simply have stayed LOW,
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

    try:
        # Both serial ports are open (the module _baseline wrote their line parameters),
        # so their DE pins are RTS-attached and idle HIGH. That is what makes the
        # assertion below meaningful: G15 starts at 1 and only the firmware can bring
        # it down.
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

            # Exit hands the parked pin OVER, it never releases it: port 2 comes back up
            # from NVS and uart_set_pin() re-attaches its RTS, which is the only
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

