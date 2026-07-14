"""Integration tests: native GPIO direction model + conflict enforcement.

In the QEMU build the native GPIO direction/level model is driven SOLELY by the
firmware's real ESP-IDF GPIO calls, transparently intercepted by the linker
--wrap shim (main/qemu/gpio_shim_qemu.c). There are NO hardcoded per-pin
defaults: a pin only gains a direction once the firmware actually configures it.
So these tests double as proof that the real firmware config drives the model:
    G34 (config button) -> INPUT  : config_button.c calls gpio_config(INPUT).
    G04 (RS485-1 DE)     -> OUTPUT : serial.c calls uart_set_pin(...rts=G04...).
    G15 (RS485-2 DE)     -> OUTPUT : serial.c calls uart_set_pin(...rts=G15...).

The model enforces three rules NON-fatally (see main/qemu/virtual_io_qemu.c):
    - HOST writing an OUTPUT pin (raw G<NN>) is illegal   -> V<NN>/0, rejected.
    - FIRMWARE configuring an input-only pad (ESP32 GPIO34..39, which has no
      output driver) as an OUTPUT is illegal              -> V<NN>/1, rejected.
    - HOST operating an UNCONFIGURED pin                  -> V<NN>/2, rejected.
On a violation the pin level (or the direction change) is NOT applied and a V
record is emitted; the bus helper records it in ``bus.violations`` as
``(gpio_num:int, code:int)``.

A firmware ``gpio_set_level()`` on a pin that is not currently an output is NOT
a violation: as on real silicon it writes the pad's output latch, which reaches
the line only once the pad is switched to OUTPUT. serial.c relies on exactly
that to force the RS-485 DE pin LOW without a glitch.

Non-destructive (all settings changed are restored in finally); NOT marked
reboot.
"""

import time

import pytest

from io_bus_helpers import IoBus

pytestmark = pytest.mark.qemu


def test_native_directions_from_real_config(api):
    """Native pin directions must come from the firmware's REAL gpio config.

    These values are not seeded anywhere: they exist only because the firmware
    actually configured the pins. G34 INPUT proves config_button's gpio_config
    ran; D04/D15 OUTPUT prove serial's uart_set_pin ran.
    """
    with IoBus() as bus:
        # The constructor already absorbed the full dump; pump a touch more so a
        # dropped D record gets a second chance.
        bus.pump(0.5)
        assert bus.get("D34") == 0, (
            f"Expected G34 (config button) direction INPUT (D34==0) from "
            f"config_button.c gpio_config(GPIO_MODE_INPUT), got {bus.get('D34')!r}. "
            f"Dump keys: {sorted(bus.state)}"
        )
        assert bus.get("D04") == 1, (
            f"Expected G04 (RS485-1 DE) direction OUTPUT (D04==1) from serial.c "
            f"uart_set_pin, got {bus.get('D04')!r}. Dump keys: {sorted(bus.state)}"
        )
        assert bus.get("D15") == 1, (
            f"Expected G15 (RS485-2 DE) direction OUTPUT (D15==1) from serial.c "
            f"uart_set_pin, got {bus.get('D15')!r}. Dump keys: {sorted(bus.state)}"
        )


@pytest.mark.parametrize(
    "gpio_pin,dir_pin,gpio_num",
    [
        ("G04", "D04", 4),
        ("G15", "D15", 15),
    ],
    ids=["G04", "G15"],
)
def test_host_cannot_drive_output(api, gpio_pin, dir_pin, gpio_num):
    """A host write to an OUTPUT native pin must be rejected with a violation.

    Sends a raw ``G<NN>/<x>`` datagram (the host trying to drive a firmware-owned
    OUTPUT). The firmware must (a) emit a ``V<NN>/0`` violation and (b) leave the
    pin level unchanged.
    """
    with IoBus() as bus:
        bus.pump(0.5)
        assert bus.get(dir_pin) == 1, (
            f"Precondition: {dir_pin} expected OUTPUT (1), got {bus.get(dir_pin)!r}"
        )
        prior_level = bus.get(gpio_pin)
        assert prior_level is not None, (
            f"Precondition: {gpio_pin} level missing from dump"
        )

        # Host attempts to drive the OUTPUT pin to the opposite of its current
        # level so a (buggy) accepted write would be unmistakable.
        target = 0 if prior_level == 1 else 1
        for _ in range(3):  # burst to beat UDP loss
            bus.send_raw(f"{gpio_pin}/{target}")
            time.sleep(0.02)
        bus.pump(1.5)

        assert (gpio_num, 0) in bus.violations, (
            f"Expected a host-drove-OUTPUT violation ({gpio_num}, 0) in "
            f"bus.violations, got {bus.violations}. The firmware must reject a "
            f"host write to OUTPUT pin {gpio_pin}."
        )
        assert bus.get(gpio_pin) == prior_level, (
            f"{gpio_pin} level changed from {prior_level} to {bus.get(gpio_pin)} "
            "after a rejected host write; the violation must NOT change the level."
        )


def test_operate_uninitialized_pin(api):
    """Operating a native pin the firmware never configured -> V/2.

    GPIO13 is not used by the firmware, so it has no direction in the model (no
    D13 in the dump). A host write to it is "operate uninitialized": the model
    must reject it with a ``V13/2`` violation.
    """
    with IoBus() as bus:
        bus.pump(0.5)
        # Confirm the pin is genuinely unconfigured: no D record in the dump.
        # If firmware ever starts using GPIO13 this is a broken test assumption,
        # not a firmware bug, so skip rather than hard-fail.
        if bus.get("D13") is not None:
            pytest.skip("GPIO13 is now configured by firmware; pick another firmware-unused native GPIO for the operate-uninitialized test")

        for _ in range(3):  # burst to beat UDP loss
            bus.send_raw("G13/1")
            time.sleep(0.02)
        bus.pump(1.5)

        assert (13, 2) in bus.violations, (
            f"Expected an operate-uninitialized violation (13, 2) in "
            f"bus.violations, got {bus.violations}. A host write to an "
            f"unconfigured native pin must be rejected with cause 2."
        )


def test_no_violations_during_normal_operation(api):
    """Legitimate flows must produce NO direction violations.

    Exercises three legal write paths:
      - host driving the INPUT button (press_button drives G34) — allowed;
      - firmware driving the OUTPUT DE pins (toggling rs485_1.tx_disabled) —
        allowed;
      - an expander setting change (no direction model) — never a violation.
    Any violation here means the direction model is broken. Restores settings.
    """
    settings_resp = api.get_settings()
    assert settings_resp.status_code == 200, (
        f"GET /settings returned {settings_resp.status_code}"
    )
    settings = settings_resp.json()
    original_tx = settings.get("rs485_1", {}).get("tx_disabled", False)
    original_term = settings.get("rs485_1", {}).get("term", True)

    with IoBus() as bus:
        bus.pump(0.5)
        try:
            # 1) Host drives the INPUT config button — legal (G34 is INPUT).
            bus.press_button(hold_s=0.4)
            bus.pump(0.5)

            # 2) Firmware drives the OUTPUT DE pin (G04) — legal both ways.
            resp = api.update_settings({"rs485_1": {"tx_disabled": True}})
            assert resp.status_code == 200, (
                f"POST tx_disabled=True returned {resp.status_code}"
            )
            bus.pump(0.8)
            resp = api.update_settings({"rs485_1": {"tx_disabled": False}})
            assert resp.status_code == 200, (
                f"POST tx_disabled=False returned {resp.status_code}"
            )
            bus.pump(0.8)

            # 3) An expander setting change (no direction model) — never illegal.
            resp = api.update_settings({"rs485_1": {"term": not original_term}})
            assert resp.status_code == 200, (
                f"POST term toggle returned {resp.status_code}"
            )
            bus.pump(0.8)

            assert bus.violations == [], (
                f"Expected NO direction violations during legitimate operation, "
                f"got {bus.violations}. A native pin's direction model is wrong: "
                "the host wrongly drove an OUTPUT, the firmware wrongly drove an "
                "INPUT, or an unconfigured pin was operated."
            )
        finally:
            api.update_settings({"rs485_1": {"tx_disabled": original_tx,
                                             "term": original_term}})
