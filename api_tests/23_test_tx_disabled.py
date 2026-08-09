"""E2E API tests for the tx_disabled feature on RS-485 ports.

When tx_disabled=True for a port:
  - The UART direction GPIO is forced LOW (RS-485 line driver physically disabled)
  - serial_send() returns immediately without transmitting any data

The firmware exposes RS-485 port UART chardevs over TCP in QEMU:
  - UART1 (RS-485 port 1): qemu_ports.UART1_TCP_PORT
  - UART2 (RS-485 port 2): qemu_ports.UART2_TCP_PORT
"""

import qemu_ports
import socket
import struct
import time
import pytest

from conftest import require_uart_chardev


@pytest.fixture(scope="module", autouse=True)
def _baseline(api):
    resp = api.update_settings({
        "rs485_1": {
            "bridge": {"mode": "server", "port": 502, "ip": "0.0.0.0", "modbus": False},
        }
    })
    assert resp.status_code == 200, f"_baseline: update_settings failed: {resp.status_code} {resp.text}"
    resp = api.set_port_mode(1, "tcp_bridge")
    assert resp.status_code == 200, f"_baseline: set_port_mode(1, tcp_bridge) failed: {resp.status_code} {resp.text}"
    # tx_disabled is set per-phase by the test itself: false → true


GATEWAY_PORT_1 = qemu_ports.GATEWAY_HOST_PORT  # hostfwd: slot gateway host port -> guest 502 (tcp_bridge port 1)
UART1_TCP_PORT = qemu_ports.UART1_TCP_PORT  # UART1 chardev TCP socket (QEMU -serial tcp::<slot UART1 port>,server,nowait)


def _build_modbus_tcp_request(txid, unit_id, fc, addr, count):
    """Build a minimal Modbus TCP request (MBAP header + PDU)."""
    pdu = struct.pack('>HH', addr, count)
    # MBAP length = unit_id(1) + FC(1) + PDU(4) = 6
    mbap = struct.pack('>HHH', txid, 0, 1 + 1 + len(pdu))
    return mbap + bytes([unit_id, fc]) + pdu


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
def test_tx_disabled_blocks_uart_transmission(api, is_qemu):
    """Verify that tx_disabled=True prevents UART1 from transmitting bytes.

    Steps:
    1. Connect to the UART1 TCP chardev socket (UART1_TCP_PORT).
    2. Save original port mode and tx_disabled value so they can be restored.
    3. Switch port 1 to tcp_bridge mode (gateway forwards Modbus TCP -> UART1 RTU).
    4. Enable tx_disabled for rs485_1 and confirm NO bytes reach UART1.
    5. Disable tx_disabled for rs485_1 and confirm bytes ARE received on UART1.
    6. Restore original tx_disabled and port mode in a finally block.

    NOTE: this test is NOT a guard for the uart_set_pin() pin-release regression, even
    though it walks the same scenario. Under QEMU the chardev UART carries the bytes
    directly and does not model the GPIO matrix, so which pins the UART is routed to
    makes no difference and step 5 passes even with the broken code. (__wrap_uart_set_pin()
    in main/qemu/gpio_shim_qemu.c does forward tx/rx to the real call; it only leaves them
    out of its virtual-bus model.) That regression is covered by the unit test in
    unittests/serial/serial_test.c.
    """
    # Connect to UART1 TCP chardev BEFORE switching port mode so QEMU can buffer bytes.
    # The returned socket IS the one this test reads from — no close/reconnect handoff.
    # Unreachable fails under --qemu and skips against real hardware, decided in one
    # place (conftest.require_uart_chardev) rather than per file.
    uart1_sock = require_uart_chardev(UART1_TCP_PORT, is_qemu, timeout=3.0)

    # Same fallbacks the reads below use when the key is absent. They exist because the
    # reads now run INSIDE the try: a ReadTimeout on either of them used to escape before
    # the try was entered and leak this socket — the chardev's ONLY accept slot — turning
    # every later test that needs UART1 into a failure. Enter the try immediately after
    # acquiring the socket (the shape 18_test_uart_chardev.py already uses); the price is
    # that finally may restore these defaults if the reads never completed.
    original_mode = "tcp_bridge"
    original_tx_disabled = False

    try:
        uart1_sock.settimeout(2.0)

        # Read original state so it can be restored after the test
        info_resp = api.get_info()
        assert info_resp.status_code == 200, f"GET /info returned {info_resp.status_code}"
        original_mode = info_resp.json().get("rs485_1", {}).get("port_mode", "tcp_bridge")

        settings_resp = api.get_settings()
        assert settings_resp.status_code == 200, (
            f"GET /settings returned {settings_resp.status_code}"
        )
        original_tx_disabled = settings_resp.json().get("rs485_1", {}).get(
            "tx_disabled", False)

        # Force required pre-conditions regardless of what previous tests may have changed:
        # Step 1: set bridge to transparent mode (modbus=False) and clear tx_disabled.
        # This may trigger a port restart via settings_update_task — wait for it to settle.
        api.update_settings({
            "rs485_1": {
                "tx_disabled": False,
                "bridge": {"modbus": False, "mode": "server"},
            }
        })
        time.sleep(1.0)  # Allow settings_update_task to finish restarting the port if needed

        # Step 2: Switch port 1 to tcp_bridge mode explicitly
        api.set_port_mode(1, "tcp_bridge")
        time.sleep(0.5)  # Allow mode switch to take effect

        # --- Phase 1: tx_disabled=True — expect NO bytes on UART1 ---
        resp = api.update_settings({"rs485_1": {"tx_disabled": True}})
        assert resp.status_code == 200, (
            f"POST /settings (tx_disabled=True) returned {resp.status_code}"
        )
        time.sleep(0.3)  # Allow the new setting to propagate

        # Flush any stale bytes that may have been buffered from a previous state
        uart1_sock.settimeout(0.2)
        try:
            while True:
                stale = uart1_sock.recv(256)
                if not stale:
                    break
        except socket.timeout:
            pass  # No more stale data — buffer is clean
        uart1_sock.settimeout(2.0)

        # Send a Modbus TCP request via the gateway — firmware should NOT forward it to UART1
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

        # Poll UART1 for 2 seconds — must receive nothing
        received_while_disabled = b''
        deadline = time.monotonic() + 2.0
        while time.monotonic() < deadline:
            try:
                chunk = uart1_sock.recv(64)
                if chunk:
                    received_while_disabled += chunk
                    break  # Already failed — stop polling early
            except socket.timeout:
                break

        assert len(received_while_disabled) == 0, (
            f"Expected NO bytes on UART1 with tx_disabled=True, "
            f"but received {len(received_while_disabled)} bytes: "
            f"{received_while_disabled.hex()}"
        )

        # --- Phase 2: tx_disabled=False — expect bytes on UART1 ---
        # After the restart-loop fix, update_settings(tx_disabled=False) applies the flag
        # directly without triggering a port restart.
        resp = api.update_settings({"rs485_1": {"tx_disabled": False}})
        assert resp.status_code == 200, (
            f"POST /settings (tx_disabled=False) returned {resp.status_code}"
        )
        time.sleep(0.3)  # Allow the flag to propagate

        # Send the same Modbus TCP request again
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

        # Poll UART1 for 2 seconds — must receive at least one byte
        received_while_enabled = b''
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
            f"Expected bytes on UART1 with tx_disabled=False, but received nothing. "
            "UART1 chardev may not be functional in this QEMU build."
        )
        print(
            f"✓ tx_disabled works: received {len(received_while_enabled)} bytes "
            f"after re-enabling TX: {received_while_enabled.hex()}"
        )

    finally:
        uart1_sock.close()
        # Restore only tx_disabled — bridge config was intentionally set to transparent
        # as part of the test preconditions and is acceptable to leave in that state
        api.update_settings({"rs485_1": {"tx_disabled": original_tx_disabled}})
        # Restore original port mode
        api.set_port_mode(1, original_mode)


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
