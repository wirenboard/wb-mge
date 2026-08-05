"""Integration tests: HTTP settings drive the RS-485/MIO expander bits.

Covers rs485_control.c (term, fail_safe, VOut) and mio_control.c (IO bus / MIO
enable). A successful POST /settings runs settings_update() synchronously in the
firmware, which calls update_rs485_control() + update_io_bus_control(); those in
turn drive the virtual GPIO expander shadow (virtual_io_qemu.c) that the IoBus
helper observes over the UDP state bus.

Bit mapping (all non-inverted: raw expander pin level == boolean setting):
    rs485_1.term      -> E00
    rs485_2.term      -> E01
    rs485_1.fail_safe -> E02
    rs485_2.fail_safe -> E03
    vout (top-level)  -> E06
    io_bus (top-level)-> E08

Each test sets a setting to False (asserts the pin reads 0) and to True (asserts
the pin reads 1), so the assertion exercises the firmware logic in both
directions and cannot pass merely by reading a default. The original value is
always restored in a finally block, so this file is session-safe and is NOT
marked reboot.
"""

import time

import pytest

from io_bus_helpers import IoBus

pytestmark = pytest.mark.qemu


# (human-readable id, settings-write builder, current-value reader, expander pin)
# The write builder takes a bool and returns a partial settings dict; the reader
# takes the full GET /settings JSON and returns the current bool value.
CASES = [
    (
        "rs485_1.term",
        lambda v: {"rs485_1": {"term": v}},
        lambda s: s["rs485_1"]["term"],
        "E00",
    ),
    (
        "rs485_2.term",
        lambda v: {"rs485_2": {"term": v}},
        lambda s: s["rs485_2"]["term"],
        "E01",
    ),
    (
        "rs485_1.fail_safe",
        lambda v: {"rs485_1": {"fail_safe": v}},
        lambda s: s["rs485_1"]["fail_safe"],
        "E02",
    ),
    (
        "rs485_2.fail_safe",
        lambda v: {"rs485_2": {"fail_safe": v}},
        lambda s: s["rs485_2"]["fail_safe"],
        "E03",
    ),
    (
        "vout",
        lambda v: {"vout": v},
        lambda s: s["vout"],
        "E06",
    ),
    (
        "io_bus",
        lambda v: {"io_bus": v},
        lambda s: s["io_bus"],
        "E08",
    ),
]


def _read_pin_after_change(pin, expected_level):
    """Open a fresh IoBus (a new NAT peer => full dump) and wait for the pin.

    A fresh socket always triggers a full state dump reflecting the current
    expander shadow, so this reliably reads the pin's level after the HTTP
    write has already driven it. wait_for() tolerates the pin already being at
    the expected level (it is, in the dump).
    """
    with IoBus() as bus:
        reached = bus.wait_for(pin, expected_level, timeout=4.0)
        return reached, bus.get(pin)


@pytest.mark.parametrize(
    "name,make_payload,read_value,pin",
    CASES,
    ids=[c[0] for c in CASES],
)
def test_setting_drives_expander_bit(api, name, make_payload, read_value, pin):
    """Toggling the setting via HTTP must drive its expander pin both ways.

    False -> pin 0, True -> pin 1. The original value is restored in finally.
    """
    resp = api.get_settings()
    assert resp.status_code == 200, f"GET /settings returned {resp.status_code}"
    original = read_value(resp.json())

    try:
        # --- Drive LOW: setting False must yield pin level 0 ---
        resp = api.update_settings(make_payload(False))
        assert resp.status_code == 200, (
            f"POST /settings {name}=False returned {resp.status_code}"
        )
        assert resp.json().get("success") is True, (
            f"POST /settings {name}=False not successful: {resp.text}"
        )
        time.sleep(0.3)  # settle: settings_update() runs synchronously, but be safe
        reached_low, level_low = _read_pin_after_change(pin, 0)
        assert reached_low, (
            f"{name}=False expected {pin}==0, got {level_low}. "
            "Firmware did not drive the expander LOW."
        )

        # --- Drive HIGH: setting True must yield pin level 1 ---
        resp = api.update_settings(make_payload(True))
        assert resp.status_code == 200, (
            f"POST /settings {name}=True returned {resp.status_code}"
        )
        assert resp.json().get("success") is True, (
            f"POST /settings {name}=True not successful: {resp.text}"
        )
        time.sleep(0.3)
        reached_high, level_high = _read_pin_after_change(pin, 1)
        assert reached_high, (
            f"{name}=True expected {pin}==1, got {level_high}. "
            "Firmware did not drive the expander HIGH."
        )
    finally:
        api.update_settings(make_payload(original))
