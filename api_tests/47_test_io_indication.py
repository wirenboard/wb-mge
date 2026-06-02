"""Integration tests: LED indication (indication.c) + factory-reset long-press.

Covers indication.c via the network-activity LEDs and the >5 s config-button
long-press factory reset (main.c config_button_longpress_callback ->
factory_reset() + settings_update()).

LED bit mapping on the virtual IO state bus (see leds_control.c):
    Eth LED  -> E05 (inverted in HW; we only assert it CHANGES)
    WiFi LED -> E04 (inverted in HW)
    Status   -> E07 (covered in test 44)

The factory-reset test is destructive (it wipes settings to defaults), so it is
marked @pytest.mark.reboot and is deferred to the very end of the suite by
conftest.pytest_collection_modifyitems. The other two tests are non-destructive.
"""

import time

import pytest

from io_bus_helpers import IoBus

pytestmark = pytest.mark.qemu


def test_eth_led_reacts_to_traffic(api):
    """E05 (Eth LED) must change at least once while Ethernet traffic flows.

    Ethernet is connected in QEMU (10.0.2.15). The indication task blinks the
    Eth LED on ifinoctets activity, so issuing a burst of HTTP GETs (which is
    Ethernet RX from the firmware's point of view) should toggle E05. We pump
    the bus while generating traffic and count edges on E05.
    """
    with IoBus() as bus:
        # Generate Ethernet traffic in a loop while pumping the bus to catch the
        # LED toggles. The indication task only blinks when ifinoctets changes
        # between samples, so we keep the traffic flowing throughout the window.
        start = len(bus.events)
        deadline = time.monotonic() + 6.0
        edges = 0
        while time.monotonic() < deadline:
            resp = api.get_info()
            assert resp.status_code == 200, f"GET /info returned {resp.status_code}"
            bus.pump(0.2)
            edges = sum(
                1 for (pin, _level) in bus.events[start:] if pin == "E05"
            )
            if edges >= 1:
                break
        assert edges >= 1, (
            f"Expected Eth LED (E05) to change at least once while Ethernet "
            f"traffic flowed, saw {edges} edges. indication.c eth LED control "
            "may not be running."
        )


def test_wifi_led_present(api):
    """E04 (WiFi LED) must be present in the dump with a valid level.

    WiFi is MOCKED in QEMU (wifi_qemu_mock.c): there is no real STA/AP netif
    carrying traffic, so the WiFi LED is not expected to blink and asserting it
    toggles would be flaky and meaningless. We only assert the pin exists in the
    full dump and holds a valid level (0 or 1), proving leds_control wired it.
    """
    with IoBus() as bus:
        level = bus.get("E04")
        assert level in (0, 1), (
            f"WiFi LED (E04) expected present with level 0 or 1 in the dump, "
            f"got {level!r}. Available pins: {sorted(bus.state)}"
        )


@pytest.mark.reboot
def test_factory_reset_long_press(api):
    """A >5 s config-button hold must factory-reset settings to defaults.

    Destructive: wipes all settings. Procedure:
      1. Save current settings; set a recognizable non-default value
         (rs485_1.term=False; firmware default is True) and confirm it took.
      2. Hold the config button LOW for ~6 s (exceeds the 5 s threshold) to
         trigger the long-press factory reset.
      3. Assert rs485_1.term reverted to its firmware default True — proving
         factory_reset() ran. As a secondary signal, observe an E07 fast-blink
         burst (indication_status_led_blink_n_times(200ms, 5)) during the press.
    Restores the originally-saved settings in finally (best-effort).
    """
    resp = api.get_settings()
    assert resp.status_code == 200, f"GET /settings returned {resp.status_code}"
    original_settings = resp.json()

    try:
        # Step 1: set a recognizable non-default value and confirm it took.
        resp = api.update_settings({"rs485_1": {"term": False}})
        assert resp.status_code == 200, (
            f"POST rs485_1.term=False returned {resp.status_code}"
        )
        assert resp.json().get("success") is True, (
            f"POST rs485_1.term=False not successful: {resp.text}"
        )
        resp = api.get_settings()
        assert resp.status_code == 200
        assert resp.json()["rs485_1"]["term"] is False, (
            "Precondition failed: rs485_1.term did not become False before reset"
        )

        # Step 2: long-press (~6 s LOW) triggers the factory reset. Count E07
        # edges during the hold as a secondary signal of the fast-blink burst.
        with IoBus() as bus:
            edges_start = len(bus.events)
            bus.press_button(hold_s=6.0)
            # Pump a bit more to catch the tail of the blink burst.
            bus.pump(2.0)
            e07_edges = sum(
                1 for (pin, _level) in bus.events[edges_start:] if pin == "E07"
            )

        # Step 3: authoritative assertion — the setting reverted to its default.
        # The button task samples G34 periodically and the callback runs
        # synchronously, so poll briefly for the revert.
        deadline = time.monotonic() + 8.0
        term_after = None
        while time.monotonic() < deadline:
            resp = api.get_settings()
            assert resp.status_code == 200, f"GET /settings returned {resp.status_code}"
            term_after = resp.json()["rs485_1"]["term"]
            if term_after is True:
                break
            time.sleep(0.5)
        assert term_after is True, (
            f"Expected rs485_1.term to revert to firmware default True after the "
            f"long-press factory reset, got {term_after}. factory_reset() may not "
            "have run."
        )

        # Secondary signal: the factory-reset fast-blink burst should have caused
        # several E07 edges. This is purely informational and must NOT fail a
        # correct factory reset: the E07 fast-blink is emitted over lossy QEMU
        # usermode-NAT UDP and can be missed entirely. The rs485_1.term revert
        # above is the authoritative proof.
        if e07_edges < 1:
            print(
                f"Warning: saw no E07 (Status LED) edges during/after the "
                f"factory-reset press (e07_edges={e07_edges}); E07 fast-blink "
                "can be lost over UDP and is non-authoritative."
            )
    finally:
        # Best-effort restore of the originally-saved settings so the device is
        # left configured. Do not assert here to preserve any test exception.
        restore = api.update_settings(original_settings)
        if restore.status_code != 200:
            print(f"Failed to restore settings after factory reset: HTTP {restore.status_code}")
