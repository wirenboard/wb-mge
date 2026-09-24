"""Talking Modbus RTU to the firmware over a QEMU UART chardev.

Shared by the tests that drive an RS-485 port through its `-serial tcp::<port>` socket:
18_test_uart_chardev (is the chardev wired at all) and 19_test_rs485_oversize_burst (does
the slave survive, and keep answering after, a burst too long to be a frame).

The functions live here rather than in either test because importing one test module from
another makes pytest collect it twice; io_bus_helpers.py sets the same precedent, and
pytest.ini's `python_files = [0-9]*_test_*.py` keeps this file out of collection.
"""

import socket
import struct
import time

# The Modbus slave answers this unit id by default (mb_slave_id, 1..247, default 1) and
# holding register 0 ("scratch_0") is in its descriptor table. A wrong address would still
# produce bytes — an exception frame is a frame — but asking for a real register keeps the
# stimulus honest.
MB_DEFAULT_UNIT_ID = 1


def crc16(data: bytes) -> int:
    """Modbus RTU CRC16."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if (crc & 1) else crc >> 1
    return crc


def build_rtu_request(unit_id, fc, addr, count):
    """A Modbus RTU request frame: address + PDU + CRC16."""
    body = bytes([unit_id, fc]) + struct.pack('>HH', addr, count)
    crc = crc16(body)
    return body + bytes([crc & 0xFF, crc >> 8])


def drain(sock, window_s=0.2):
    """Discard whatever is already buffered on the chardev."""
    sock.settimeout(window_s)
    try:
        while sock.recv(256):
            pass
    except socket.timeout:
        pass


def read_reply(sock, window_s):
    """Read from the chardev until ANY bytes arrive or the window expires.

    Deliberately loose: 18_test_uart_chardev asks only whether the wire carries anything
    at all, and there even a malformed frame is a positive answer. For "did the slave
    answer properly", use read_rtu_response() instead.
    """
    sock.settimeout(0.5)
    received = b''
    deadline = time.monotonic() + window_s
    while time.monotonic() < deadline:
        try:
            chunk = sock.recv(64)
        except socket.timeout:
            continue
        if chunk:
            received += chunk
            break
    return received


# Address + function + CRC16: the shortest frame Modbus RTU can put on the wire.
MB_RTU_MIN_FRAME_LEN = 4


def is_valid_rtu_response(frame, unit_id=MB_DEFAULT_UNIT_ID):
    """True when frame is a complete RTU frame sent by unit_id.

    A Modbus CRC is built so that the checksum computed over the frame INCLUDING its own
    CRC bytes comes out zero, which is what makes "is this a whole, intact frame" a single
    test rather than a length table per function code. An exception response (5 bytes)
    passes it as readily as a normal read response (7 bytes) — both prove the slave's
    state machine ran a full receive-decide-transmit cycle, which is all that is being
    asked here.
    """
    return (len(frame) >= MB_RTU_MIN_FRAME_LEN
            and frame[0] == unit_id
            and crc16(frame) == 0)


def read_rtu_response(sock, window_s, unit_id=MB_DEFAULT_UNIT_ID):
    """Accumulate bytes until they form a valid RTU frame from unit_id, or time out.

    Unlike read_reply() this does not stop at the first chunk: under QEMU a reply routinely
    arrives one TCP segment at a time, so stopping early would report a single byte and
    call that an answer.
    """
    sock.settimeout(0.2)
    received = b''
    deadline = time.monotonic() + window_s
    while time.monotonic() < deadline:
        try:
            chunk = sock.recv(64)
        except socket.timeout:
            chunk = b''
        if chunk:
            received += chunk
            if is_valid_rtu_response(received, unit_id):
                break
    return received


def poll_holding_register(sock, window_s, unit_id=MB_DEFAULT_UNIT_ID):
    """Ask the slave for holding register 0 and return the frame it answers with.

    The caller checks it with is_valid_rtu_response(). Empty or partial bytes mean the
    slave did not complete an answer: either the chardev does not reach this port, or its
    state machine is no longer running the poll loop.
    """
    drain(sock)
    sock.sendall(build_rtu_request(unit_id, 3, 0, 1))
    return read_rtu_response(sock, window_s=window_s, unit_id=unit_id)
