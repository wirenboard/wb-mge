"""Gateway E2E dual-port simultaneous test (GW-07).

Requires QEMU with:
- UART1 exposed as TCP port 5561 and guest port 502 forwarded to host 50502
- UART2 exposed as TCP port 5562 and guest port 503 forwarded to host 50503

Two independent Modbus TCP gateways are configured simultaneously — one on
RS485-1 and one on RS485-2.  Each gateway has its own RTU slave with a
distinct fake_value so that responses can be distinguished.

Coverage:
GW-07. FC03 requests to port 50502 (RS485-1) and port 50503 (RS485-2) are
       independently served; each response contains the correct fake_value and
       does not bleed across ports.
"""

import socket
import struct
import time

import pytest

from conftest import _poll_tcp_connect
from modbus_helpers import make_mbap_request, recv_modbus_tcp_response
from rtu_slave_helpers import ModbusRtuSlaveThread


# ---------------------------------------------------------------------------
# Module-level constants
# ---------------------------------------------------------------------------

GATEWAY_HOST = "127.0.0.1"

# QEMU host-forwarded ports for both RS485 gateways
RS485_1_HOST_PORT = 50502    # guest port 502
RS485_2_HOST_PORT = 50503    # guest port 503

# QEMU UART chardev TCP ports
UART1_TCP_PORT = 5561        # UART1 (RS485-1)
UART2_TCP_PORT = 5562        # UART2 (RS485-2)

# Distinct fake values per port so responses can be verified independently
FAKE_VALUE_RS485_1 = 0x1111
FAKE_VALUE_RS485_2 = 0x2222


# ---------------------------------------------------------------------------
# Module-level baseline fixture: configure RS485-1 and RS485-2 physical params
# ---------------------------------------------------------------------------

@pytest.fixture(scope="module", autouse=True)
def _baseline(api):
    resp = api.update_settings({
        "rs485_1": {
            "tx_disabled": False,
            "baudrate": 9600,
            "stopbits": "1",
            "parity": "none",
            "databits": "8",
        },
        "rs485_2": {
            "tx_disabled": False,
            "baudrate": 9600,
            "stopbits": "1",
            "parity": "none",
            "databits": "8",
        },
    })
    assert resp.status_code == 200, (
        f"_baseline: update_settings failed: {resp.status_code} {resp.text}"
    )


# ---------------------------------------------------------------------------
# Dual-port fixture: configure both RS485-1 and RS485-2 as Modbus TCP gateways
# ---------------------------------------------------------------------------

@pytest.fixture(scope="module")
def dual_gateway_slave(api):
    """Configure both RS485 ports as Modbus TCP gateways and start RTU slaves.

    Yields (slave1, slave2) — ModbusRtuSlaveThread instances for RS485-1 and
    RS485-2 respectively.  Restores the original port configuration on teardown.
    """
    # Verify both UART chardev TCP ports are reachable before proceeding
    for uart_port in (UART1_TCP_PORT, UART2_TCP_PORT):
        probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        probe.settimeout(3.0)
        try:
            probe.connect((GATEWAY_HOST, uart_port))
            probe.close()
        except (ConnectionRefusedError, OSError, socket.timeout):
            probe.close()
            pytest.skip(
                f"Cannot connect to UART chardev TCP port {uart_port}. "
                "QEMU may not expose this UART as TCP in this configuration."
            )

    # Save original settings for full restore on teardown
    resp = api.get_settings()
    assert resp.status_code == 200, f"GET /settings failed: {resp.status_code}"
    original = resp.json()

    slave1 = None
    slave2 = None
    try:
        # Disable both ports first to release UART drivers before reconfiguring
        api.set_port_mode(1, "disabled")
        time.sleep(0.3)
        api.set_port_mode(2, "disabled")
        time.sleep(0.3)

        # Configure RS485-1: Modbus TCP gateway on guest port 502
        rs485_1_settings = dict(original.get("rs485_1", {}))
        rs485_1_settings["bridge"] = {
            "mode": "server",
            "port": 502,
            "ip": "0.0.0.0",
            "modbus": True,
        }
        resp = api.update_settings({"rs485_1": rs485_1_settings})
        assert resp.status_code == 200, (
            f"RS485-1 settings update failed: {resp.status_code} {resp.text}"
        )

        # Configure RS485-2: Modbus TCP gateway on guest port 503
        rs485_2_settings = dict(original.get("rs485_2", {}))
        rs485_2_settings["bridge"] = {
            "mode": "server",
            "port": 503,
            "ip": "0.0.0.0",
            "modbus": True,
        }
        resp = api.update_settings({"rs485_2": rs485_2_settings})
        assert resp.status_code == 200, (
            f"RS485-2 settings update failed: {resp.status_code} {resp.text}"
        )

        time.sleep(0.3)

        # Switch both ports to tcp_bridge mode
        resp = api.set_port_mode(1, "tcp_bridge")
        assert resp.status_code == 200, (
            f"set_port_mode(1, tcp_bridge) failed: {resp.status_code}"
        )
        time.sleep(0.2)
        resp = api.set_port_mode(2, "tcp_bridge")
        assert resp.status_code == 200, (
            f"set_port_mode(2, tcp_bridge) failed: {resp.status_code}"
        )

        # Wait for both gateway TCP ports to start accepting connections
        ready1 = _poll_tcp_connect(GATEWAY_HOST, RS485_1_HOST_PORT, timeout=5.0)
        assert ready1, (
            f"RS485-1 gateway did not start listening on host port "
            f"{RS485_1_HOST_PORT} within 5 s"
        )
        ready2 = _poll_tcp_connect(GATEWAY_HOST, RS485_2_HOST_PORT, timeout=5.0)
        assert ready2, (
            f"RS485-2 gateway did not start listening on host port "
            f"{RS485_2_HOST_PORT} within 5 s"
        )

        # Start RTU slaves: slave1 → UART1 (RS485-1), slave2 → UART2 (RS485-2)
        slave1 = ModbusRtuSlaveThread(
            host=GATEWAY_HOST,
            port=UART1_TCP_PORT,
            fake_value=FAKE_VALUE_RS485_1,
            connect_timeout=5.0,
        )
        slave2 = ModbusRtuSlaveThread(
            host=GATEWAY_HOST,
            port=UART2_TCP_PORT,
            fake_value=FAKE_VALUE_RS485_2,
            connect_timeout=5.0,
        )
        slave1.start()
        slave2.start()

        connected1 = slave1.wait_connected(timeout=5.0)
        assert connected1, (
            f"RTU slave 1 could not connect to UART1 chardev on port {UART1_TCP_PORT}"
        )
        connected2 = slave2.wait_connected(timeout=5.0)
        assert connected2, (
            f"RTU slave 2 could not connect to UART2 chardev on port {UART2_TCP_PORT}"
        )

        yield slave1, slave2

    finally:
        # Stop RTU slaves before reconfiguring ports
        if slave1 is not None:
            slave1.stop()
            slave1.join(timeout=3.0)
        if slave2 is not None:
            slave2.stop()
            slave2.join(timeout=3.0)

        # Disable both ports before restoring settings
        api.set_port_mode(1, "disabled")
        api.set_port_mode(2, "disabled")
        time.sleep(0.3)

        restore_resp = api.update_settings(original)
        if restore_resp.status_code != 200:
            print(f"✗ Failed to restore settings: HTTP {restore_resp.status_code}")

        # Restore original port modes
        original_mode1 = original.get("rs485_1", {}).get("port_mode", "disabled")
        original_mode2 = original.get("rs485_2", {}).get("port_mode", "disabled")
        api.set_port_mode(1, original_mode1)
        api.set_port_mode(2, original_mode2)
        time.sleep(0.3)


# ---------------------------------------------------------------------------
# GW-07: Simultaneous dual-port gateway requests
# ---------------------------------------------------------------------------

@pytest.mark.qemu
@pytest.mark.timeout(30)
def test_gateway_dual_port_simultaneous(dual_gateway_slave):
    """FC03 requests to both gateways are served independently with correct values.

    Sends sequential FC03 reads to the RS485-1 gateway (port 50502) and the
    RS485-2 gateway (port 50503).  Each response must contain the fake_value
    specific to that port's RTU slave, confirming that the two gateway instances
    do not share state or data paths.
    """
    slave1, slave2 = dual_gateway_slave

    # -------------------------------------------------------------------
    # Request to RS485-1 gateway (port 50502) — expect FAKE_VALUE_RS485_1
    # -------------------------------------------------------------------
    txid1 = 0x0701
    unit_id = 1
    fc = 0x03
    count = 1

    request1 = make_mbap_request(txid1, unit_id, fc, 0x0000, count)
    gw_sock1 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    gw_sock1.settimeout(5.0)
    try:
        gw_sock1.connect((GATEWAY_HOST, RS485_1_HOST_PORT))
        gw_sock1.sendall(request1)

        resp1 = recv_modbus_tcp_response(gw_sock1, time.monotonic() + 5.0)
    finally:
        gw_sock1.close()

    assert len(resp1) >= 11, f"RS485-1 response too short: {resp1.hex()!r}"
    resp1_txid, resp1_proto, _resp1_length = struct.unpack('>HHH', resp1[:6])
    assert resp1_txid == txid1, (
        f"RS485-1 TID mismatch: sent {txid1:#06x}, got {resp1_txid:#06x}"
    )
    assert resp1_proto == 0, f"RS485-1 protocol ID must be 0, got {resp1_proto}"

    pdu1 = resp1[6:]
    assert not (pdu1[1] & 0x80), f"RS485-1 Modbus exception FC={pdu1[1]:#04x}"
    byte_count1 = pdu1[2]
    reg_data1 = pdu1[3:3 + byte_count1]
    val1 = struct.unpack('>H', reg_data1[:2])[0]
    assert val1 == FAKE_VALUE_RS485_1, (
        f"RS485-1 register value mismatch: expected {FAKE_VALUE_RS485_1:#06x}, "
        f"got {val1:#06x}"
    )

    # -------------------------------------------------------------------
    # Request to RS485-2 gateway (port 50503) — expect FAKE_VALUE_RS485_2
    # -------------------------------------------------------------------
    txid2 = 0x0702
    request2 = make_mbap_request(txid2, unit_id, fc, 0x0000, count)
    gw_sock2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    gw_sock2.settimeout(5.0)
    try:
        gw_sock2.connect((GATEWAY_HOST, RS485_2_HOST_PORT))
        gw_sock2.sendall(request2)

        resp2 = recv_modbus_tcp_response(gw_sock2, time.monotonic() + 5.0)
    finally:
        gw_sock2.close()

    assert len(resp2) >= 11, f"RS485-2 response too short: {resp2.hex()!r}"
    resp2_txid, resp2_proto, _resp2_length = struct.unpack('>HHH', resp2[:6])
    assert resp2_txid == txid2, (
        f"RS485-2 TID mismatch: sent {txid2:#06x}, got {resp2_txid:#06x}"
    )
    assert resp2_proto == 0, f"RS485-2 protocol ID must be 0, got {resp2_proto}"

    pdu2 = resp2[6:]
    assert not (pdu2[1] & 0x80), f"RS485-2 Modbus exception FC={pdu2[1]:#04x}"
    byte_count2 = pdu2[2]
    reg_data2 = pdu2[3:3 + byte_count2]
    val2 = struct.unpack('>H', reg_data2[:2])[0]
    assert val2 == FAKE_VALUE_RS485_2, (
        f"RS485-2 register value mismatch: expected {FAKE_VALUE_RS485_2:#06x}, "
        f"got {val2:#06x}"
    )

    # Both RTU slaves must have handled at least one request each
    assert slave1.request_count >= 1, (
        f"RTU slave 1 (RS485-1) request_count={slave1.request_count} — "
        "gateway did not forward request to UART1"
    )
    assert slave2.request_count >= 1, (
        f"RTU slave 2 (RS485-2) request_count={slave2.request_count} — "
        "gateway did not forward request to UART2"
    )

    print(
        f"✓ GW-07 dual-port: "
        f"RS485-1 val={val1:#06x} (slave1_req={slave1.request_count}), "
        f"RS485-2 val={val2:#06x} (slave2_req={slave2.request_count})"
    )
