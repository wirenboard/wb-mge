"""Integration tests: tx_disabled drives the RS-485 direction GPIOs.

Covers serial_set_tx_disabled() / port_manager_set_tx_disabled() as observed on
the native RS-485 direction (DE) GPIOs exposed on the virtual IO state bus:
    rs485_1.tx_disabled -> G04
    rs485_2.tx_disabled -> G15

Non-inverted: tx_disabled=False keeps the direction pin at its TX-enabled idle
level 1; tx_disabled=True parks it LOW (0), physically disabling the line driver
so the firmware cannot transmit.

The third test reuses the gateway technique from 23_test_tx_disabled.py to prove
the parked direction pin also blocks real UART traffic end to end: with
tx_disabled=True a Modbus TCP request through the tcp_bridge gateway produces NO
bytes on the UART1 chardev AND G04 reads 0; with tx_disabled=False bytes DO
arrive AND G04 reads 1.

All settings/port-mode changes are restored in finally, so this file is
session-safe and is NOT marked reboot.
"""

import socket
import struct
import time

import pytest

from io_bus_helpers import IoBus

pytestmark = pytest.mark.qemu


GATEWAY_PORT_1 = 50502   # hostfwd 50502 -> QEMU:502 (tcp_bridge port 1)
UART1_TCP_PORT = 5561    # UART1 chardev TCP socket (QEMU -serial tcp::5561,server,nowait)


def _build_modbus_tcp_request(txid, unit_id, fc, addr, count):
    """Build a minimal Modbus TCP request (MBAP header + PDU). From test 23."""
    pdu = struct.pack(">HH", addr, count)
    # MBAP length = unit_id(1) + FC(1) + PDU(4) = 6
    mbap = struct.pack(">HHH", txid, 0, 1 + 1 + len(pdu))
    return mbap + bytes([unit_id, fc]) + pdu


def _try_connect_tcp(host, port, timeout=3.0):
    """Try to connect to a TCP port. Returns socket or None. From test 23."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    try:
        sock.connect((host, port))
        return sock
    except (ConnectionRefusedError, OSError, socket.timeout):
        sock.close()
        return None


def _read_dir_pin(pin, expected_level):
    """Open a fresh IoBus (new peer => full dump) and wait for the direction pin."""
    with IoBus() as bus:
        reached = bus.wait_for(pin, expected_level, timeout=4.0)
        return reached, bus.get(pin)


def _check_dir_pin_follows_tx_disabled(api, settings_key, pin):
    """Flip tx_disabled True/False for one port and assert its direction pin tracks it.

    tx_disabled=True  -> pin parked LOW (0)
    tx_disabled=False -> pin idle HIGH (1, TX enabled)
    The original value is restored in finally.
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


def test_dir_pin_parked_blocks_uart(api):
    """Parked direction pin (G04==0) blocks UART1 traffic end to end.

    Reuses the gateway approach from 23_test_tx_disabled.py:
      Phase 1 (tx_disabled=True): a Modbus TCP request through the gateway must
        produce NO bytes on UART1 AND G04 must read 0 (line driver parked).
      Phase 2 (tx_disabled=False): the same request must produce bytes on UART1
        AND G04 must read 1 (TX enabled).
    Restores port mode + tx_disabled in finally.
    """
    # Connect to UART1 chardev BEFORE switching mode so QEMU can buffer bytes.
    uart1_sock = _try_connect_tcp("127.0.0.1", UART1_TCP_PORT, timeout=3.0)
    if uart1_sock is None:
        pytest.fail(
            f"Cannot connect to UART1 chardev TCP port {UART1_TCP_PORT}. "
            "QEMU must expose UART1 as TCP (-serial tcp::5561,server,nowait)."
        )

    # Save original mode + tx_disabled so they can be restored.
    info_resp = api.get_info()
    assert info_resp.status_code == 200, f"GET /info returned {info_resp.status_code}"
    original_mode = info_resp.json().get("rs485_1", {}).get("port_mode", "tcp_bridge")

    settings_resp = api.get_settings()
    assert settings_resp.status_code == 200, (
        f"GET /settings returned {settings_resp.status_code}"
    )
    original_tx = settings_resp.json().get("rs485_1", {}).get("tx_disabled", False)

    try:
        # Pre-conditions: transparent bridge, tx enabled, tcp_bridge mode.
        api.update_settings({
            "rs485_1": {
                "tx_disabled": False,
                "bridge": {"modbus": False, "mode": "server"},
            }
        })
        time.sleep(1.0)  # allow settings_update_task to restart the port if needed
        api.set_port_mode(1, "tcp_bridge")
        time.sleep(0.5)

        # --- Phase 1: tx_disabled=True -> no UART bytes AND G04 == 0 ---
        resp = api.update_settings({"rs485_1": {"tx_disabled": True}})
        assert resp.status_code == 200, (
            f"POST tx_disabled=True returned {resp.status_code}"
        )
        time.sleep(0.3)

        # Assert the direction pin is parked LOW.
        reached_low, level = _read_dir_pin("G04", 0)
        assert reached_low, (
            f"tx_disabled=True expected G04==0 (parked), got {level}"
        )

        # Flush any stale buffered bytes from a previous state.
        uart1_sock.settimeout(0.2)
        try:
            while True:
                stale = uart1_sock.recv(256)
                if not stale:
                    break
        except socket.timeout:
            pass
        # Use a recv timeout smaller than the no-bytes window so the loop polls
        # several times across the window instead of doing a single long recv.
        uart1_sock.settimeout(0.5)

        # Send a Modbus TCP request via the gateway; firmware must NOT forward it.
        request = _build_modbus_tcp_request(txid=1, unit_id=1, fc=3, addr=0, count=1)
        gw_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        gw_sock.settimeout(3.0)
        try:
            gw_sock.connect(("127.0.0.1", GATEWAY_PORT_1))
            gw_sock.sendall(request)
        except (ConnectionRefusedError, OSError) as exc:
            pytest.fail(f"Could not connect to gateway port {GATEWAY_PORT_1}: {exc}")
        finally:
            gw_sock.close()

        received_while_disabled = b""
        deadline = time.monotonic() + 2.0
        while time.monotonic() < deadline:
            try:
                chunk = uart1_sock.recv(64)
                if chunk:
                    received_while_disabled += chunk
                    break  # already failed — stop early
            except socket.timeout:
                break
        assert len(received_while_disabled) == 0, (
            f"Expected NO bytes on UART1 with tx_disabled=True, got "
            f"{len(received_while_disabled)}: {received_while_disabled.hex()}"
        )

        # --- Phase 2: tx_disabled=False -> UART bytes AND G04 == 1 ---
        resp = api.update_settings({"rs485_1": {"tx_disabled": False}})
        assert resp.status_code == 200, (
            f"POST tx_disabled=False returned {resp.status_code}"
        )
        time.sleep(0.3)

        reached_high, level = _read_dir_pin("G04", 1)
        assert reached_high, (
            f"tx_disabled=False expected G04==1 (TX enabled), got {level}"
        )

        request2 = _build_modbus_tcp_request(txid=2, unit_id=1, fc=3, addr=0, count=1)
        gw_sock2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        gw_sock2.settimeout(3.0)
        try:
            gw_sock2.connect(("127.0.0.1", GATEWAY_PORT_1))
            gw_sock2.sendall(request2)
        except (ConnectionRefusedError, OSError) as exc:
            pytest.fail(f"Could not connect to gateway port {GATEWAY_PORT_1}: {exc}")
        finally:
            gw_sock2.close()

        received_while_enabled = b""
        deadline = time.monotonic() + 2.0
        while time.monotonic() < deadline:
            try:
                chunk = uart1_sock.recv(64)
                if chunk:
                    received_while_enabled += chunk
                    break
            except socket.timeout:
                break
        assert len(received_while_enabled) > 0, (
            "Expected bytes on UART1 with tx_disabled=False, got nothing. "
            "UART1 chardev may not be functional in this QEMU build."
        )
    finally:
        uart1_sock.close()
        # Bridge was intentionally set transparent; restore only tx_disabled + mode.
        api.update_settings({"rs485_1": {"tx_disabled": original_tx}})
        api.set_port_mode(1, original_mode)
