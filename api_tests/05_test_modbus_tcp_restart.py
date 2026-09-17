"""Moving mb_tcp_port restarts the Modbus slave without losing a half.

This is the end-to-end guard for the defect that has now been fixed twice in
main/mb_slave/mb_slave.c: a stop/start cycle that brings back fewer halves than the device
is supposed to be running, permanently and with nothing reporting it. Both times the RS-485
pair was the casualty of a restart driven by something else entirely.

Every settings write that changes mb_tcp_port runs mb_slave_stop() followed by
mb_slave_start() over ALL THREE instances — RTU on port 1, RTU on port 2, Modbus TCP — so
each move below is a full teardown of the serial halves as well. The assertions after each
one are therefore the point of the test: the TCP listener must be on the port that was just
asked for, and BOTH RS-485 ports must still answer a Modbus RTU request afterwards.

WHAT THIS TEST CANNOT REACH, and why it is not written the way it was first proposed. The
original failure needs a half that is DOWN while the device still wants it up
(mb_*_wanted && !mb_*_running), and the obvious way to arrange that — point mb_tcp_port at
the web server's port so the listener cannot bind — is not reachable through this API:
validate_port_collisions() in main/settings_manager.c refuses that request outright, so
nothing is written and the TCP half never goes down. Step 2 below asserts exactly that,
because it is the property that makes the precondition unreachable, and a regression there
would silently turn this file into a test of something else. Provoking a genuinely failed
start needs an instrumented firmware and lives with the unit tests instead.
"""

import socket
import struct
import time

import pytest

import qemu_ports
from conftest import require_uart_chardev

# The slave answers this unit id by default (mb_slave_id, 1..247) and holding register 0
# ("scratch_0") is in its descriptor table, so a reply here means the instance is really
# serving, not merely listening.
MB_UNIT_ID = 1

# Guest-side ports. The firmware setting takes the GUEST value; the host connects through
# the matching hostfwd. 8081 is the only other guest TCP port this QEMU forwards, which is
# what makes a move observable at all — see qemu_ports.
MB_PORT_DEFAULT_GUEST = qemu_ports.MB_TCP_GUEST_PORT     # 502  -> MB_TCP_HOST_PORT
MB_PORT_ALT_GUEST = qemu_ports.ALT_PORT_GUEST            # 8081 -> ALT_WEB_HOST_PORT

# How long the async apply may take to move the listener. settings_update_task restarts the
# Modbus slave with no response delay in front of it (that one is only for the branches that
# release a socket the POST reply travels over), so this is generous.
APPLY_TIMEOUT_S = 15.0


def _crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if (crc & 1) else crc >> 1
    return crc


def _rtu_request(unit_id, fc, addr, count):
    body = bytes([unit_id, fc]) + struct.pack(">HH", addr, count)
    crc = _crc16(body)
    return body + bytes([crc & 0xFF, crc >> 8])


def _rs485_answers(uart_tcp_port, is_qemu, window_s=5.0):
    """True when the RTU slave on this UART chardev answers a read of holding register 0."""
    sock = require_uart_chardev(uart_tcp_port, is_qemu, timeout=3.0)
    try:
        # Discard whatever the previous test left buffered, so the bytes counted below are
        # the answer to OUR request.
        sock.settimeout(0.2)
        try:
            while sock.recv(256):
                pass
        except socket.timeout:
            pass

        sock.sendall(_rtu_request(MB_UNIT_ID, 3, 0, 1))
        sock.settimeout(0.5)
        deadline = time.monotonic() + window_s
        while time.monotonic() < deadline:
            try:
                chunk = sock.recv(64)
            except socket.timeout:
                continue
            if chunk:
                return True
        return False
    finally:
        # The chardev accepts exactly ONE client, so this must come back whatever happened.
        sock.close()


def _modbus_tcp_answers(host_port, window_s=3.0):
    """True when a Modbus TCP slave on this host port answers a read of holding register 0."""
    try:
        sock = socket.create_connection((qemu_ports.HOST, host_port), timeout=3.0)
    except OSError:
        return False
    try:
        sock.settimeout(window_s)
        # MBAP header (transaction, protocol, length, unit) + PDU (FC3, address 0, 1 reg).
        frame = struct.pack(">HHHB", 1, 0, 6, MB_UNIT_ID) + bytes([3]) + struct.pack(">HH", 0, 1)
        sock.sendall(frame)
        try:
            return len(sock.recv(64)) > 0
        except (socket.timeout, OSError):
            return False
    finally:
        sock.close()


def _wait_modbus_tcp(host_port, expected, timeout=APPLY_TIMEOUT_S):
    """Poll until Modbus TCP on host_port matches `expected` (answering / silent)."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if _modbus_tcp_answers(host_port) == expected:
            return True
        time.sleep(0.5)
    return False


def _set_mb_tcp_port(api, guest_port):
    resp = api.update_settings({"mb_tcp_port": guest_port})
    assert resp.status_code == 200, \
        f"POST /settings mb_tcp_port={guest_port}: expected 200, got {resp.status_code} {resp.text}"
    return resp.json()


@pytest.mark.qemu
def test_mb_tcp_port_move_keeps_every_half(api, is_qemu):
    """A mb_tcp_port change restarts all three instances and loses none of them."""
    web_port = api.get_settings().json().get("web_port")
    assert web_port is not None, "GET /settings did not report web_port"

    try:
        # 1. Baseline. All three instances are serving; the RS-485 assertions here are what
        #    make the ones after each move meaningful.
        assert _modbus_tcp_answers(qemu_ports.MB_TCP_HOST_PORT), \
            f"Baseline: Modbus TCP must answer on guest {MB_PORT_DEFAULT_GUEST}"
        assert _rs485_answers(qemu_ports.UART1_TCP_PORT, is_qemu), "Baseline: RS-485-1 must answer"
        assert _rs485_answers(qemu_ports.UART2_TCP_PORT, is_qemu), "Baseline: RS-485-2 must answer"
        print("✓ baseline: Modbus TCP + both RS-485 ports answering")

        # 2. The one request that could take the TCP listener down is refused before
        #    anything is written, so nothing goes down. Rejection is reported as HTTP 200
        #    with success=false (main/settings_manager.c), not as a 4xx.
        resp = api.update_settings({"mb_tcp_port": web_port})
        assert resp.status_code == 200, \
            f"POST /settings with a colliding mb_tcp_port: expected 200, got {resp.status_code}"
        body = resp.json()
        assert body.get("success") is False, \
            f"mb_tcp_port == web_port ({web_port}) must be refused, got {body}"
        print(f"✓ mb_tcp_port == web_port ({web_port}) refused: {body.get('error')}")

        # A refused write changes nothing: no restart, so all three are still up.
        assert _modbus_tcp_answers(qemu_ports.MB_TCP_HOST_PORT), \
            "A refused settings write must not take the Modbus TCP listener down"
        assert _rs485_answers(qemu_ports.UART1_TCP_PORT, is_qemu), \
            "A refused settings write must not take RS-485-1 down"
        assert _rs485_answers(qemu_ports.UART2_TCP_PORT, is_qemu), \
            "A refused settings write must not take RS-485-2 down"
        print("✓ nothing went down on the refused write")

        # 3. A real move. This is a full stop/start of all three instances.
        _set_mb_tcp_port(api, MB_PORT_ALT_GUEST)
        assert _wait_modbus_tcp(qemu_ports.ALT_WEB_HOST_PORT, True), (
            f"Modbus TCP did not come up on guest {MB_PORT_ALT_GUEST} within "
            f"{APPLY_TIMEOUT_S} s of the settings write")
        assert _wait_modbus_tcp(qemu_ports.MB_TCP_HOST_PORT, False), \
            f"Modbus TCP must have left guest {MB_PORT_DEFAULT_GUEST}"
        print(f"✓ Modbus TCP moved to guest {MB_PORT_ALT_GUEST}")

        # The whole point: the serial halves were torn down by that restart too, and they
        # have to come back. This is the assertion the two fixed defects would have failed.
        assert _rs485_answers(qemu_ports.UART1_TCP_PORT, is_qemu), \
            "RS-485-1 must still answer after the Modbus slave was restarted"
        assert _rs485_answers(qemu_ports.UART2_TCP_PORT, is_qemu), \
            "RS-485-2 must still answer after the Modbus slave was restarted"
        print("✓ both RS-485 ports survived the restart")

        # 4. Move back, and assert the same again — a second restart in the same session is
        #    where a record of "what is supposed to be running" that decays per cycle shows
        #    up, and both defects needed exactly two writes to become permanent.
        _set_mb_tcp_port(api, MB_PORT_DEFAULT_GUEST)
        assert _wait_modbus_tcp(qemu_ports.MB_TCP_HOST_PORT, True), (
            f"Modbus TCP did not come back on guest {MB_PORT_DEFAULT_GUEST} within "
            f"{APPLY_TIMEOUT_S} s")
        assert _rs485_answers(qemu_ports.UART1_TCP_PORT, is_qemu), \
            "RS-485-1 must still answer after the second restart"
        assert _rs485_answers(qemu_ports.UART2_TCP_PORT, is_qemu), \
            "RS-485-2 must still answer after the second restart"
        print(f"✓ Modbus TCP back on guest {MB_PORT_DEFAULT_GUEST}, both RS-485 ports alive")

    finally:
        # Leave the port where every other file expects it. 40_test_web_port.py moves the
        # web server onto guest 8081, and the collision validator would refuse that write
        # for as long as the Modbus slave sits there.
        try:
            api.update_settings({"mb_tcp_port": MB_PORT_DEFAULT_GUEST})
        except Exception as exc:      # pragma: no cover - teardown diagnostics only
            print(f"teardown: restoring mb_tcp_port failed: {exc}")
