"""Integration tests for the QEMU virtual IO state bus (guest UDP port 5570).

These are the first integration tests to cover the firmware "hardware" modules
that were previously excluded from the QEMU build and had zero coverage:
indication.c, leds_control.c, rs485_control.c, mio_control.c and
config_button.c. The QEMU build now runs them for real and mirrors their
GPIO/expander/button state onto a small UDP bus, which the IoBus helper drives
and observes.

Coverage map:
    - Full initial dump            -> the bus wiring + all tracked signals.
    - Status LED blink             -> indication.c + leds_control.c.
    - RS-485 direction pins idle   -> rs485_control.c default (TX enabled).
    - Config button short press    -> config_button.c debounce_filter +
                                      config_button_task press path.

All tests require QEMU (use --qemu). Each test creates and closes its own
IoBus so it gets a fresh full dump (a fresh socket = a new NAT peer). No test
reboots or mutates persisted settings: the button test does a SHORT press only
(a >5 s hold would trigger a destructive factory-reset long-press).
"""

import time

import pytest

from io_bus_helpers import IoBus

pytestmark = pytest.mark.qemu


def test_initial_dump_contains_all_signals(api):
    """Subscribing must yield a full dump of every tracked signal.

    A fresh IoBus socket is a new NAT peer, so the guest immediately dumps the
    current level of every tracked pin. Assert E00..E08 and the three native
    GPIOs are all present. E09..E15 are unused (always 0) and not required.
    """
    with IoBus() as bus:
        for index in range(0, 9):  # E00..E08
            pin = f"E{index:02d}"
            assert pin in bus.state, (
                f"Expander pin {pin} missing from initial dump; "
                f"got {sorted(bus.state)}"
            )
        for pin in ("G04", "G15", "G34"):
            assert pin in bus.state, (
                f"Native GPIO {pin} missing from initial dump; "
                f"got {sorted(bus.state)}"
            )


def test_status_led_blinks(api):
    """E07 (Status LED) must toggle, proving indication.c/leds_control.c run.

    The status LED blinks at ~1 Hz, so within ~6 s we expect at least two
    edges. Each emitted E07 record is one real level change (the firmware only
    sends on change). The window is widened so a couple of dropped QEMU UDP
    datagrams cannot flake a correct firmware.
    """
    with IoBus() as bus:
        edges = bus.count_edges("E07", 6.0)
        assert edges >= 2, (
            f"Expected Status LED (E07) to change at least 2 times in 6 s, "
            f"saw {edges}. indication.c/leds_control.c may not be running."
        )


def test_rs485_direction_pins_idle_high(api):
    """Both RS-485 direction GPIOs must idle HIGH (TX-enabled default).

    G04 (RS485-1 dir) and G15 (RS485-2 dir) default to 1 in rs485_control.c
    when no transmission is in progress.

    Precondition: both ports MUST be in an active transport so their DE pins are
    RTS-attached and idle HIGH. A prior test may have left a port disabled or
    tx_disabled (DE pin parked LOW), so we force tcp_bridge on both ports and let
    the disabled->active reinit transient settle before polling for the level.
    """
    info_resp = api.get_info()
    assert info_resp.status_code == 200, f"GET /info returned {info_resp.status_code}"
    info = info_resp.json()
    original_mode_1 = info.get("rs485_1", {}).get("port_mode", "tcp_bridge")
    original_mode_2 = info.get("rs485_2", {}).get("port_mode", "tcp_bridge")

    try:
        # Force both ports into an active transport so the DE pins are RTS-attached
        # and idle HIGH, then let the disabled->active reinit transient
        # (gpio_reset_pin Pullup->HIGH) fully settle BEFORE reading the pins.
        api.set_port_mode(1, "tcp_bridge")
        api.set_port_mode(2, "tcp_bridge")
        time.sleep(1.0)

        with IoBus() as bus:
            reached = bus.wait_for("G04", 1, timeout=4.0)
            assert reached, (
                f"RS485-1 direction (G04) expected idle HIGH, got {bus.get('G04')}"
            )
            reached = bus.wait_for("G15", 1, timeout=4.0)
            assert reached, (
                f"RS485-2 direction (G15) expected idle HIGH, got {bus.get('G15')}"
            )
    finally:
        api.set_port_mode(1, original_mode_1)
        api.set_port_mode(2, original_mode_2)


def test_config_button_short_press_increments_counter(api):
    """A short config-button press must increment config_button_presses by 1.

    Drives G34 LOW (pressed) for ~0.4 s then HIGH (released), exercising the
    debounce_filter and the config_button_task press path in config_button.c.
    The counter is exposed at GET /info; poll for a few seconds because the
    button task samples G34 periodically.
    """
    before_resp = api.get_info()
    assert before_resp.status_code == 200, (
        f"GET /info before press returned {before_resp.status_code}"
    )
    before = before_resp.json()["config_button_presses"]

    with IoBus() as bus:
        bus.press_button(hold_s=0.3)

    # The button task polls G34, so the counter may lag the release. Poll
    # /info for up to ~5 s and expect exactly one additional press.
    deadline = time.monotonic() + 5.0
    after = before
    while time.monotonic() < deadline:
        resp = api.get_info()
        assert resp.status_code == 200, f"GET /info returned {resp.status_code}"
        after = resp.json()["config_button_presses"]
        if after >= before + 1:
            break
        time.sleep(0.3)

    # The strict "== before + 1" assertion assumes exclusive access to the
    # config button (G34) within the session: no other actor presses it
    # concurrently between the before/after reads.
    assert after == before + 1, (
        f"Expected config_button_presses to increase by exactly 1 "
        f"({before} -> {before + 1}), but got {after}. "
        "config_button.c press path may not be working."
    )
