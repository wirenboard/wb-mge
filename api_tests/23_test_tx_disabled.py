"""API tests for the tx_disabled setting on RS-485 ports.

When tx_disabled=True for a port:
  - The UART direction GPIO is forced LOW (RS-485 line driver physically disabled)
  - the RS-485 line driver is off, so nothing the UART transmits reaches the bus

This file covers the SETTING: that it is exposed, that it round-trips through NVS, and
that toggling it does not restart the port in a loop. The GPIO-level effect is covered
over the QEMU IO bus by 46_test_io_direction_tx_disabled.py.
"""

import time
import pytest


@pytest.fixture(scope="module", autouse=True)
def _baseline(api):
    # Plain port parameters only. The serial line is open whenever the port carries a
    # valid configuration, so writing a known baud/parity/stopbits/databits set is all
    # this file needs to bring UART1 up.
    resp = api.update_settings({
        "rs485_1": {
            "baudrate": 9600, "stopbits": "1", "parity": "none", "databits": "8",
        }
    })
    assert resp.status_code == 200, f"_baseline: update_settings failed: {resp.status_code} {resp.text}"
    # tx_disabled is set per-phase by the tests themselves.


def test_tx_disabled_field_in_settings(api):
    """Verify that GET /settings returns tx_disabled for both rs485_1 and rs485_2.

    Both fields must be present and must be booleans.
    """
    resp = api.get_settings()
    assert resp.status_code == 200, f"GET /settings returned {resp.status_code}"

    data = resp.json()

    # Verify rs485_1.tx_disabled exists and is a boolean
    assert "rs485_1" in data, "rs485_1 section is missing from /settings response"
    assert "tx_disabled" in data["rs485_1"], (
        "tx_disabled field is missing from rs485_1 in /settings response"
    )
    assert isinstance(data["rs485_1"]["tx_disabled"], bool), (
        f"rs485_1.tx_disabled must be bool, got {type(data['rs485_1']['tx_disabled'])}"
    )

    # Verify rs485_2.tx_disabled exists and is a boolean
    assert "rs485_2" in data, "rs485_2 section is missing from /settings response"
    assert "tx_disabled" in data["rs485_2"], (
        "tx_disabled field is missing from rs485_2 in /settings response"
    )
    assert isinstance(data["rs485_2"]["tx_disabled"], bool), (
        f"rs485_2.tx_disabled must be bool, got {type(data['rs485_2']['tx_disabled'])}"
    )


def test_tx_disabled_save_and_restore(api):
    """Verify that tx_disabled can be saved via POST /settings and read back.

    Reads the current value for rs485_1, flips it, writes it back, confirms
    the round-trip, then restores the original value in a finally block.
    """
    # Read current value
    resp = api.get_settings()
    assert resp.status_code == 200, f"GET /settings returned {resp.status_code}"
    settings = resp.json()

    assert "rs485_1" in settings, "rs485_1 section is missing from /settings response"
    assert "tx_disabled" in settings["rs485_1"], (
        "tx_disabled field is missing from rs485_1 in /settings response"
    )
    original_value = settings["rs485_1"]["tx_disabled"]

    new_value = not original_value

    try:
        # Write the flipped value
        write_resp = api.update_settings({"rs485_1": {"tx_disabled": new_value}})
        assert write_resp.status_code == 200, (
            f"POST /settings returned {write_resp.status_code}"
        )

        # Read back and verify
        read_resp = api.get_settings()
        assert read_resp.status_code == 200, (
            f"GET /settings returned {read_resp.status_code}"
        )
        read_back = read_resp.json()
        assert read_back["rs485_1"]["tx_disabled"] == new_value, (
            f"Expected tx_disabled={new_value} after write, "
            f"got {read_back['rs485_1']['tx_disabled']}"
        )
    finally:
        # Always restore the original value
        api.update_settings({"rs485_1": {"tx_disabled": original_value}})


@pytest.mark.qemu
def test_tx_disabled_no_port_restart_loop(api):
    """Verify that toggling tx_disabled does not cause an infinite port restart loop.

    Toggles tx_disabled twice in quick succession and verifies that the port
    remains reachable (no crash or infinite restart loop).
    """
    # Read original state
    settings_resp = api.get_settings()
    assert settings_resp.status_code == 200
    original_tx = settings_resp.json().get("rs485_1", {}).get("tx_disabled", False)

    try:
        # Rapidly toggle tx_disabled — this should not cause a restart loop
        resp = api.update_settings({"rs485_1": {"tx_disabled": True}})
        assert resp.status_code == 200, f"First tx_disabled=True: {resp.status_code}"

        resp = api.update_settings({"rs485_1": {"tx_disabled": False}})
        assert resp.status_code == 200, f"Second tx_disabled=False: {resp.status_code}"

        # Wait for any pending restart to settle
        time.sleep(2.0)

        # Verify device is still reachable — if there was a restart loop, the device would be unresponsive
        info_resp = api.get_info()
        assert info_resp.status_code == 200, (
            f"Device unreachable after tx_disabled toggle (possible restart loop): {info_resp.status_code}"
        )

        # Verify tx_disabled is now False (correctly saved)
        settings_resp = api.get_settings()
        assert settings_resp.status_code == 200
        final_tx = settings_resp.json().get("rs485_1", {}).get("tx_disabled", True)
        assert final_tx == False, f"Expected tx_disabled=False, got {final_tx}"

        print("✓ No port restart loop detected after tx_disabled toggle")

    finally:
        api.update_settings({"rs485_1": {"tx_disabled": original_tx}})
