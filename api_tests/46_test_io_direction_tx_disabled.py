"""Integration tests: tx_disabled drives the RS-485 direction GPIOs.

Covers mb_slave_set_tx_disabled() as observed on the native RS-485 direction (DE)
GPIOs exposed on the virtual IO state bus:
    rs485_1.tx_disabled -> G04
    rs485_2.tx_disabled -> G15

Non-inverted: tx_disabled=False keeps the direction pin at its TX-enabled idle
level 1; tx_disabled=True parks it LOW (0), physically disabling the line driver
so the firmware cannot transmit.

All settings changes are restored in finally, so this file is session-safe and is
NOT marked reboot.

WHY THERE IS NO END-TO-END "PARKED PIN BLOCKS UART TRAFFIC" TEST HERE ANY MORE.
A third test used to assert exactly that, by driving a Modbus TCP request through
the tcp_bridge gateway and checking that no bytes reached the UART1 chardev. It is
gone with the gateway, and it cannot be rebuilt against the DIY firmware — not for
want of a stimulus (writing an RTU request into the chardev makes the slave answer)
but because the property is no longer observable in QEMU. The old firmware gated
transmission in SOFTWARE: its serial_send() checked tx_disabled and returned without
touching the UART, which QEMU could see. mb_slave_set_tx_disabled() does not gate
anything; it takes the DE/RE pin away from the UART and drives it LOW, so the block
is electrical — it happens inside the RS-485 transceiver, which QEMU does not model.
The UART TX pin keeps transmitting, and the chardev keeps carrying those bytes,
whatever tx_disabled says. The GPIO half of the contract is what the two tests below
assert; the transceiver half needs real hardware.
"""

import time

import pytest

from io_bus_helpers import IoBus

pytestmark = pytest.mark.qemu


def _read_dir_pin(pin, expected_level):
    """Open a fresh IoBus (new peer => full dump) and wait for the direction pin."""
    with IoBus() as bus:
        reached = bus.wait_for(pin, expected_level, timeout=4.0)
        return reached, bus.get(pin)


def _check_dir_pin_follows_tx_disabled(api, settings_key, pin):
    """Flip tx_disabled True/False for one port and assert its direction pin tracks it.

    tx_disabled=True  -> pin parked LOW (0)
    tx_disabled=False -> pin idle HIGH (1, TX enabled)

    No bring-up step: the Modbus slave opens both RS-485 ports at boot, so the DE pin is
    already RTS-attached and there is no disabled->active reinit transient to settle out.
    The original tx_disabled value is restored in finally.
    """
    resp = api.get_settings()
    assert resp.status_code == 200, f"GET /settings returned {resp.status_code}"
    original = resp.json()[settings_key]["tx_disabled"]

    try:
        # tx_disabled=True -> direction pin LOW
        resp = api.update_settings({settings_key: {"tx_disabled": True}})
        assert resp.status_code == 200, (
            f"POST {settings_key}.tx_disabled=True returned {resp.status_code}"
        )
        time.sleep(0.3)
        reached_low, level = _read_dir_pin(pin, 0)
        assert reached_low, (
            f"{settings_key}.tx_disabled=True expected {pin}==0 (parked), got {level}"
        )

        # tx_disabled=False -> direction pin HIGH (TX enabled idle)
        resp = api.update_settings({settings_key: {"tx_disabled": False}})
        assert resp.status_code == 200, (
            f"POST {settings_key}.tx_disabled=False returned {resp.status_code}"
        )
        time.sleep(0.3)
        reached_high, level = _read_dir_pin(pin, 1)
        assert reached_high, (
            f"{settings_key}.tx_disabled=False expected {pin}==1 (TX enabled), got {level}"
        )
    finally:
        api.update_settings({settings_key: {"tx_disabled": original}})


def test_dir_pin_follows_tx_disabled_port1(api):
    """rs485_1.tx_disabled must drive G04: True->0, False->1."""
    _check_dir_pin_follows_tx_disabled(api, "rs485_1", "G04")


def test_dir_pin_follows_tx_disabled_port2(api):
    """rs485_2.tx_disabled must drive G15: True->0, False->1."""
    _check_dir_pin_follows_tx_disabled(api, "rs485_2", "G15")

