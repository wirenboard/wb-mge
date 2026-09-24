"""An oversize RS-485 burst must not reboot the device.

esp-modbus clamps a received burst to MB_BUFFER_SIZE INCLUSIVELY and then asserts
`length < MB_RTU_SER_PDU_SIZE_MAX`, which is the same constant — so a burst that fills
the whole UART RX ring used to abort the firmware and reboot the board. Line noise on an
idle, unterminated RS-485 pair reaches that size on its own, which turned a WB-MGU into a
reboot loop (a panic every 15-110 s) and made every long HTTP request look like it had
crashed the device. patches/bug07-modbus-rtu-oversize-frame-abort.patch drops such a
burst instead; this test is what notices if that patch stops being applied.
"""

import socket
import time

import pytest
import qemu_ports

from conftest import require_uart_chardev

# CONFIG_FMB_BUFFER_SIZE. The abort needed a burst of at least this many bytes to land in
# one inter-frame gap; the sizes below straddle it so an off-by-one cannot pass unnoticed.
MB_BUFFER_SIZE = 260
BURST_SIZES = (MB_BUFFER_SIZE - 1, MB_BUFFER_SIZE, MB_BUFFER_SIZE + 1, 512)

# Long enough for the UART RX timeout to close the burst and the Modbus FSM to consume it.
BURST_SETTLE_S = 1.0


def _uptime_s(api):
    response = api.get_uptime()
    assert response.status_code == 200, f"/uptime failed: {response.status_code}"
    data = response.json()
    return ((((data["days"] * 24) + data["hours"]) * 60) + data["minutes"]) * 60 + data["seconds"]


@pytest.mark.qemu
@pytest.mark.parametrize("uart_tcp_port,port_name", [
    (qemu_ports.UART1_TCP_PORT, "RS485-1"),
    (qemu_ports.UART2_TCP_PORT, "RS485-2"),
])
def test_oversize_burst_does_not_reboot(api, is_qemu, uart_tcp_port, port_name):
    """Bursts at and above the Modbus buffer size must be dropped, not aborted on."""
    uart_sock = require_uart_chardev(uart_tcp_port, is_qemu, timeout=3.0)
    before = _uptime_s(api)

    try:
        for size in BURST_SIZES:
            uart_sock.sendall(b"\x01" * size)
            time.sleep(BURST_SETTLE_S)
            print(f"{port_name}: sent a {size}-byte burst")
    except (socket.error, OSError) as exc:
        pytest.fail(f"{port_name}: the chardev died while sending a burst, "
                    f"which is what a reboot looks like from here: {exc!r}")
    finally:
        uart_sock.close()

    after = _uptime_s(api)
    assert after >= before, (
        f"{port_name}: the device rebooted while receiving oversize bursts "
        f"(uptime {before} s -> {after} s). The esp-modbus RTU slave aborted on "
        f"assert(length < MB_RTU_SER_PDU_SIZE_MAX); check that "
        f"patches/bug07-modbus-rtu-oversize-frame-abort.patch is applied."
    )
    print(f"✓ {port_name} survived {len(BURST_SIZES)} oversize bursts "
          f"(uptime {before} s -> {after} s)")
