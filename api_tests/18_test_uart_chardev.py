"""Diagnostic test: verify UART1/2 are exposed as TCP sockets and receive data."""

import socket
import struct
import time
import pytest

from conftest import require_uart_chardev


@pytest.fixture(scope="module", autouse=True)
def _baseline(api):
    resp = api.update_settings({
        "rs485_1": {
            "tx_disabled": False,     # gateway must forward bytes to UART
            "bridge": {"mode": "server", "port": 502, "ip": "0.0.0.0", "modbus": True},
        }
    })
    assert resp.status_code == 200, f"_baseline: update_settings failed: {resp.status_code} {resp.text}"
    resp = api.set_port_mode(1, "tcp_bridge")    # CRITICAL: TCP listener on port 502 only opens in tcp_bridge mode
    assert resp.status_code == 200, f"_baseline: set_port_mode(1, tcp_bridge) failed: {resp.status_code} {resp.text}"

GATEWAY_PORT_1 = 50502   # hostfwd 50502 -> QEMU:502 (tcp_bridge port 1)
UART1_TCP_PORT = 5561    # UART1 chardev TCP socket (QEMU -serial tcp::5561,server,nowait)
UART2_TCP_PORT = 5562    # UART2 chardev TCP socket (QEMU -serial tcp::5562,server,nowait)


def _build_modbus_tcp_request(txid, unit_id, fc, addr, count):
    """Build a minimal Modbus TCP request (MBAP header + PDU)."""
    pdu = struct.pack('>HH', addr, count)
    # MBAP length = unit_id(1) + FC(1) + PDU(4) = 6
    mbap = struct.pack('>HHH', txid, 0, 1 + 1 + len(pdu))
    return mbap + bytes([unit_id, fc]) + pdu


@pytest.mark.qemu
def test_uart1_chardev_receives_bytes(api, is_qemu):
    """Verify that UART1 TCP chardev (port 5561) receives bytes when tcp_bridge is active.

    This is a diagnostic test: it proves that QEMU -serial tcp::5561,server,nowait
    correctly exposes UART1 TX data on the host TCP socket. If this test passes,
    the full gateway RTU-slave test (bug 07 fix for modbus_tcp.c) is feasible.
    If it fails, UART1 chardev is not functional in this QEMU build.
    """
    # Save original port mode so we can restore it
    info = api.get_info().json()
    original_mode = info.get("rs485_1", {}).get("port_mode", "tcp_bridge")

    # Connect to UART1 TCP socket BEFORE switching mode (QEMU server must have a
    # client connected to buffer any bytes that UART1 transmits)
    # The probe socket IS the socket this test uses — no close/reconnect handoff on a
    # single-client chardev. Fails when the QEMU is ours (a leak), skips against a
    # remote device.
    uart1_sock = require_uart_chardev(UART1_TCP_PORT, is_qemu, timeout=3.0)

    try:
        uart1_sock.settimeout(2.0)

        # Switch port 1 to tcp_bridge: firmware forwards Modbus TCP requests to UART1 as RTU
        resp = api.set_port_mode(1, "tcp_bridge")
        assert resp.status_code == 200, f"Failed to set tcp_bridge mode: {resp.status_code}"
        time.sleep(0.5)  # allow mode switch to complete

        # Send a Modbus TCP request to the gateway — firmware will forward it to UART1 as RTU
        request = _build_modbus_tcp_request(txid=1, unit_id=1, fc=3, addr=0, count=1)
        gw_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        gw_sock.settimeout(3.0)
        try:
            gw_sock.connect(("127.0.0.1", GATEWAY_PORT_1))
            gw_sock.sendall(request)
        except (ConnectionRefusedError, OSError) as e:
            pytest.fail(f"Could not connect to gateway port {GATEWAY_PORT_1}: {e}")
        finally:
            gw_sock.close()

        # Try to read bytes from UART1 TCP socket.
        # The firmware serializes the RTU request and sends it via uart_write_bytes.
        received = b''
        deadline = time.monotonic() + 3.0
        while time.monotonic() < deadline:
            try:
                chunk = uart1_sock.recv(64)
                if chunk:
                    received += chunk
                    break
            except socket.timeout:
                break

        assert len(received) > 0, (
            f"No bytes received on UART1 TCP port {UART1_TCP_PORT} after sending a "
            "gateway request. UART1 chardev may not be functional in this QEMU build."
        )
        print(f"✓ UART1 chardev works: received {len(received)} bytes: {received.hex()}")

    finally:
        uart1_sock.close()
        api.set_port_mode(1, original_mode)
