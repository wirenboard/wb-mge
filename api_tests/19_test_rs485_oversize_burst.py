"""An oversize RS-485 burst must not reboot the device, nor wedge the Modbus slave.

esp-modbus clamps a received burst to MB_BUFFER_SIZE INCLUSIVELY and then asserts
`length < MB_RTU_SER_PDU_SIZE_MAX`, which is the same constant — so a burst that fills
the whole UART RX ring used to abort the firmware and reboot the board. Line noise on an
idle, unterminated RS-485 pair reaches that size on its own, which turned a WB-MGU into a
reboot loop (a panic every 15-110 s) and made every long HTTP request look like it had
crashed the device. patches/bug07-modbus-rtu-oversize-frame-abort.patch drops such a
burst instead; this test is what notices if that patch stops being applied.

Surviving is only half of what has to hold, and "the device is still up" is on its own a
claim this test cannot make honestly: a burst that never reached the Modbus port at all
would leave the device just as alive. So each burst run is bracketed by a real RTU
exchange with the slave on that same port:

  * BEFORE — a positive control. The reply proves the chardev genuinely reaches this
    port's Modbus slave, so a later "no reboot" means the bursts were dropped rather than
    never delivered. Broken QEMU -serial wiring fails here instead of passing silently.
  * AFTER — the liveness proof the patch is actually scrutinised for. Dropping a frame
    must not leave the bus semaphore taken or an event lost; if the slave's state machine
    wedged on the discard path it stops answering, and no uptime check would show it.
"""

import time

import pytest
import qemu_ports

from conftest import require_uart_chardev
from rtu_helpers import MB_DEFAULT_UNIT_ID, is_valid_rtu_response, poll_holding_register

# CONFIG_FMB_BUFFER_SIZE. The abort needed a burst of at least this many bytes to land in
# one inter-frame gap; the sizes below straddle it so an off-by-one cannot pass unnoticed.
MB_BUFFER_SIZE = 260
BURST_SIZES = (MB_BUFFER_SIZE - 1, MB_BUFFER_SIZE, MB_BUFFER_SIZE + 1, 512)

# Long enough for the UART RX timeout to close the burst and the Modbus FSM to consume it.
BURST_SETTLE_S = 1.0

# Matches 18_test_uart_chardev: generous, because a QEMU on a loaded host is slow.
REPLY_WINDOW_S = 5.0

# Slack on the uptime-vs-wall-clock comparison below. /uptime is reported in whole seconds,
# so each of the two readings truncates by up to a second and the measured advance can be
# up to two seconds short of the real one. That is the entire error budget, because the
# wall-clock interval is taken strictly BETWEEN the two readings (see the test) and so
# excludes their round trips. Keep this well under the burst window: the slack is what a
# reboot has to hide inside, and a QEMU boot alone costs several seconds.
UPTIME_SLACK_S = 2.0


@pytest.fixture(scope="module", autouse=True)
def _baseline(api):
    # Both ports are driven here, and the slave can only answer on a port whose DE pin is
    # not parked. tx_disabled is persisted in NVS, so an earlier run could have left it set
    # — which would fail the positive control below for a reason that has nothing to do
    # with oversize frames. Same precondition 18_test_uart_chardev clears for port 1.
    resp = api.update_settings({"rs485_1": {"tx_disabled": False},
                                "rs485_2": {"tx_disabled": False}})
    assert resp.status_code == 200, f"_baseline: update_settings failed: {resp.status_code} {resp.text}"


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

    try:
        before_reply = poll_holding_register(uart_sock, REPLY_WINDOW_S)
        assert is_valid_rtu_response(before_reply), (
            f"{port_name}: the Modbus slave did not answer a valid RTU request for unit "
            f"{MB_DEFAULT_UNIT_ID} on UART TCP port {uart_tcp_port} with a complete, "
            f"CRC-valid frame within {REPLY_WINDOW_S} s, BEFORE any burst was sent "
            f"(got {len(before_reply)} bytes: {before_reply.hex() or '-'}). Without that "
            f"reply the rest of this test proves nothing: a burst that never reaches the "
            f"Modbus port cannot crash it either. Either the QEMU -serial wiring is broken "
            f"or the slave is not up on this port."
        )
        print(f"{port_name}: slave answers before the bursts "
              f"({len(before_reply)} bytes: {before_reply.hex()})")

        # The wall clock starts AFTER the first reading and stops BEFORE the second, so the
        # interval it measures is strictly contained in the one the device's uptime counts.
        # Both HTTP round trips fall outside it, which keeps the comparison's error budget
        # down to the one-second truncation of each reading (UPTIME_SLACK_S).
        uptime_before = _uptime_s(api)
        started = time.monotonic()

        # The poll above left a 0.2 s timeout on the socket, and that timeout applies to
        # sends too. These writes are small enough to never block on a healthy chardev, but
        # a fifth of a second is thin enough that host-side scheduling noise could trip it
        # and be reported as a firmware failure.
        uart_sock.settimeout(REPLY_WINDOW_S)
        for size in BURST_SIZES:
            uart_sock.sendall(b"\x01" * size)
            time.sleep(BURST_SETTLE_S)
            print(f"{port_name}: sent a {size}-byte burst")

        elapsed = time.monotonic() - started
        uptime_after = _uptime_s(api)

        # `after >= before` would miss a reboot whenever the device had been up for less
        # than the test window — exactly the case when this file is run on its own against
        # a freshly booted QEMU. An uptime that advanced by far less than the wall clock is
        # the reboot, whatever the absolute numbers are.
        advanced = uptime_after - uptime_before
        assert advanced >= (elapsed - UPTIME_SLACK_S), (
            f"{port_name}: the device restarted while receiving oversize bursts — uptime "
            f"advanced {advanced} s over {elapsed:.1f} s of wall clock "
            f"({uptime_before} s -> {uptime_after} s). The esp-modbus RTU slave aborted on "
            f"assert(length < MB_RTU_SER_PDU_SIZE_MAX); check that "
            f"patches/bug07-modbus-rtu-oversize-frame-abort.patch is applied."
        )

        after_reply = poll_holding_register(uart_sock, REPLY_WINDOW_S)
        assert is_valid_rtu_response(after_reply), (
            f"{port_name}: the device stayed up, but the Modbus slave no longer answers with "
            f"a complete, CRC-valid frame after {len(BURST_SIZES)} oversize bursts "
            f"(got {len(after_reply)} bytes: {after_reply.hex() or '-'}; it answered fine "
            f"before them). Dropping an overlong frame left its state machine wedged — a bus "
            f"semaphore still taken, or a frame event lost — which is the failure the discard "
            f"path has to avoid just as much as the abort."
        )
        print(f"{port_name}: slave still answers after the bursts "
              f"({len(after_reply)} bytes: {after_reply.hex()})")

    except OSError as exc:
        pytest.fail(f"{port_name}: the chardev socket failed mid-test, which is also what a "
                    f"reboot looks like from here: {exc!r}")
    finally:
        uart_sock.close()

    print(f"✓ {port_name} survived {len(BURST_SIZES)} oversize bursts and still serves Modbus "
          f"(uptime {uptime_before} s -> {uptime_after} s over {elapsed:.1f} s)")
