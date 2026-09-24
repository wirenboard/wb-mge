"""Diagnostic test: verify UART1 is exposed as a TCP socket and carries data.

WHAT MAKES THE FIRMWARE TALK HERE. There is no gateway and no bridge any more, so
nothing forwards host traffic onto the UART. The only thing that still transmits on
RS-485 port 1 is the Modbus slave answering a request addressed to it — so the
stimulus is an RTU request written INTO the chardev, and the bytes this test looks
for are the slave's reply. That exercises the chardev in both directions.

With the gateway, bridge and sniffer files gone, the tests that still open a UART
chardev are this one, 19_test_rs485_oversize_burst and 05_test_modbus_tcp_restart —
between them they are what would notice the QEMU -serial wiring breaking, which
conftest's _uart_leak_guard assumes is real. The RTU helpers this file and 19 share
live in rtu_helpers.py.
"""

import qemu_ports
import pytest

from conftest import require_uart_chardev
from rtu_helpers import MB_DEFAULT_UNIT_ID, build_rtu_request, drain, read_reply


@pytest.fixture(scope="module", autouse=True)
def _baseline(api):
    # The RS-485 ports are opened at boot, unconditionally and before the network comes
    # up, so there is no bring-up step to perform here. The one precondition that is not
    # automatic is tx_disabled: it parks the DE pin and is persisted in NVS, so an earlier
    # run could have left it set.
    resp = api.update_settings({"rs485_1": {"tx_disabled": False}})
    assert resp.status_code == 200, f"_baseline: update_settings failed: {resp.status_code} {resp.text}"


UART1_TCP_PORT = qemu_ports.UART1_TCP_PORT  # UART1 chardev TCP socket (QEMU -serial tcp::<slot UART1 port>,server,nowait)


@pytest.mark.qemu
def test_uart1_chardev_receives_bytes(api, is_qemu):
    """Verify that the UART1 TCP chardev carries bytes in both directions.

    This is a diagnostic test: it proves that QEMU -serial tcp::<UART1 port>,server,nowait
    exposes UART1 on a host TCP socket — host writes reach the guest UART, and guest
    transmissions reach the host. If it fails, no other test that drives UART1 can be
    trusted.
    """
    # The probe socket IS the socket this test uses — no close/reconnect handoff on a
    # single-client chardev. Fails when the QEMU is ours (a leak), skips against a
    # remote device.
    uart1_sock = require_uart_chardev(UART1_TCP_PORT, is_qemu, timeout=3.0)

    try:
        drain(uart1_sock)

        # Read holding register 0 from the RS-485 slave on port 1.
        uart1_sock.sendall(build_rtu_request(MB_DEFAULT_UNIT_ID, 3, 0, 1))
        received = read_reply(uart1_sock, window_s=5.0)

        assert len(received) > 0, (
            f"No bytes received on UART1 TCP port {UART1_TCP_PORT} within 5 s of writing a "
            f"Modbus RTU request for unit {MB_DEFAULT_UNIT_ID} into it. Either the UART1 "
            f"chardev is not functional in this QEMU build, or the RS-485 Modbus slave is "
            f"not answering on port 1."
        )
        print(f"UART1 chardev works: received {len(received)} bytes: {received.hex()}")

    finally:
        uart1_sock.close()
