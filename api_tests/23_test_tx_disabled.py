"""E2E API tests for the tx_disabled feature on RS-485 ports.

When tx_disabled=True for a port:
  - The UART direction GPIO is forced LOW (RS-485 line driver physically disabled)
  - serial_send() returns immediately without transmitting any data

The firmware exposes RS-485 port UART chardevs over TCP in QEMU:
  - UART1 (RS-485 port 1): TCP port 5561
  - UART2 (RS-485 port 2): TCP port 5562
"""

import socket
import struct
import time
import pytest

GATEWAY_PORT_1 = 50502   # hostfwd 50502 -> QEMU:502 (modbus_bus port 1)
UART1_TCP_PORT = 5561    # UART1 chardev TCP socket (QEMU -serial tcp::5561,server,nowait)


def _build_modbus_tcp_request(txid, unit_id, fc, addr, count):
    """Build a minimal Modbus TCP request (MBAP header + PDU)."""
    pdu = struct.pack('>HH', addr, count)
    # MBAP length = unit_id(1) + FC(1) + PDU(4) = 6
    mbap = struct.pack('>HHH', txid, 0, 1 + 1 + len(pdu))
    return mbap + bytes([unit_id, fc]) + pdu


def _try_connect_tcp(host, port, timeout=3.0):
    """Try to connect to a TCP port. Returns socket or None."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    try:
        sock.connect((host, port))
        return sock
    except (ConnectionRefusedError, OSError, socket.timeout):
        sock.close()
        return None


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
def test_tx_disabled_blocks_uart_transmission(api):
    """Verify that tx_disabled=True prevents UART1 from transmitting bytes.

    Steps:
    1. Connect to the UART1 TCP chardev socket (port 5561).
    2. Save original port mode and tx_disabled value so they can be restored.
    3. Switch port 1 to tcp_bridge mode (gateway forwards Modbus TCP -> UART1 RTU).
    4. Enable tx_disabled for rs485_1 and confirm NO bytes reach UART1.
    5. Disable tx_disabled for rs485_1 and confirm bytes ARE received on UART1.
    6. Restore original tx_disabled and port mode in a finally block.
    """
    # Connect to UART1 TCP chardev BEFORE switching port mode so QEMU can buffer bytes
    uart1_sock = _try_connect_tcp("127.0.0.1", UART1_TCP_PORT, timeout=3.0)
    if uart1_sock is None:
        pytest.fail(
            f"Cannot connect to UART1 chardev TCP port {UART1_TCP_PORT}. "
            "QEMU must expose UART1 as TCP (check -serial tcp::5561,server,nowait argument)."
        )

    # Read original port mode
    info_resp = api.get_info()
    assert info_resp.status_code == 200, f"GET /info returned {info_resp.status_code}"
    original_mode = info_resp.json().get("rs485_1", {}).get("port_mode", "sniffer")

    # Read original tx_disabled value
    settings_resp = api.get_settings()
    assert settings_resp.status_code == 200, (
        f"GET /settings returned {settings_resp.status_code}"
    )
    original_tx_disabled = settings_resp.json().get("rs485_1", {}).get("tx_disabled", False)

    try:
        uart1_sock.settimeout(2.0)

        # Switch port 1 to tcp_bridge mode so the gateway forwards requests to UART1
        api.set_port_mode(1, "tcp_bridge")
        time.sleep(0.3)  # Allow mode switch to take effect

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
        resp = api.update_settings({"rs485_1": {"tx_disabled": False}})
        assert resp.status_code == 200, (
            f"POST /settings (tx_disabled=False) returned {resp.status_code}"
        )
        time.sleep(0.3)  # Allow the new setting to propagate

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
        # Restore original tx_disabled value
        api.update_settings({"rs485_1": {"tx_disabled": original_tx_disabled}})
        # Restore original port mode
        api.set_port_mode(1, original_mode)
