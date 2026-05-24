"""Modbus RTU slave that listens on a TCP socket (QEMU UART1 chardev).

Connects to the given host:port, reads Modbus RTU requests from the firmware
(sent by the Modbus TCP gateway when it receives TCP requests), and replies
with synthesized register values.
"""

import socket
import struct
import threading
import time


def _crc16(data: bytes) -> int:
    """Calculate Modbus CRC16."""
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if (crc & 1) else crc >> 1
    return crc


def _build_rtu_response(slave_id: int, fc: int, data: bytes) -> bytes:
    """Wrap data in a Modbus RTU response frame (header + payload + CRC)."""
    body = bytes([slave_id, fc]) + data
    crc = _crc16(body)
    return body + bytes([crc & 0xFF, crc >> 8])


class ModbusRtuSlaveThread(threading.Thread):
    """Connects to QEMU UART1 TCP chardev and responds to Modbus RTU FC01-FC04 requests.

    The slave runs in a daemon thread and can be stopped via stop().
    After stop(), call join() to wait for the thread to finish.
    """

    def __init__(self, host: str = '127.0.0.1', port: int = 5561,
                 fake_value: int = 0x1234, connect_timeout: float = 5.0):
        super().__init__(daemon=True)
        self.host = host
        self.port = port
        self.fake_value = fake_value      # register/coil value returned for any address
        self.connect_timeout = connect_timeout
        self.request_count = 0            # number of RTU requests handled
        self.connected = False            # True once TCP connection is established
        self._stop_event = threading.Event()
        self._sock = None

    # ------------------------------------------------------------------ #
    # Thread entry point                                                   #
    # ------------------------------------------------------------------ #

    def run(self) -> None:
        """Main loop: connect and serve RTU requests until stop() is called."""
        try:
            self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._sock.settimeout(self.connect_timeout)
            self._sock.connect((self.host, self.port))
            self.connected = True
            self._sock.settimeout(0.5)   # short recv timeout so we can check _stop_event
        except OSError:
            return

        buf = b''
        while not self._stop_event.is_set():
            try:
                chunk = self._sock.recv(256)
                if not chunk:
                    break
                buf += chunk
                buf = self._process_buffer(buf)
            except socket.timeout:
                continue
            except OSError:
                break

        try:
            self._sock.close()
        except OSError:
            pass

    def _process_buffer(self, buf: bytes) -> bytes:
        """Parse and respond to all complete RTU frames in buf; return leftover bytes."""
        # Minimum RTU request: slave(1) + FC(1) + addr(2) + count(2) + CRC(2) = 8 bytes
        while len(buf) >= 8:
            slave_id = buf[0]
            fc = buf[1]
            addr = (buf[2] << 8) | buf[3]
            count = (buf[4] << 8) | buf[5]
            crc_recv = (buf[7] << 8) | buf[6]   # little-endian in RTU
            crc_calc = _crc16(buf[:6])

            if crc_recv != crc_calc:
                # CRC mismatch: discard one byte and retry (re-sync)
                buf = buf[1:]
                continue

            response = self._build_response(slave_id, fc, addr, count)
            if response and self._sock:
                try:
                    self._sock.sendall(response)
                except OSError:
                    return buf
            self.request_count += 1
            buf = buf[8:]   # consume the 8-byte request

        return buf

    def _build_response(self, slave_id: int, fc: int, addr: int, count: int):
        """Build a Modbus RTU response for FC01/FC02/FC03/FC04."""
        if fc in (0x03, 0x04):
            # FC03/FC04: read holding/input registers
            values = [self.fake_value] * count
            payload = struct.pack(f'>{count}H', *values)
            data = bytes([len(payload)]) + payload
            return _build_rtu_response(slave_id, fc, data)
        if fc in (0x01, 0x02):
            # FC01/FC02: read coils/discrete inputs (pack bits LSB-first)
            byte_count = (count + 7) // 8
            coil_bytes = bytearray(byte_count)
            for i in range(count):
                if self.fake_value:
                    coil_bytes[i // 8] |= (1 << (i % 8))
            data = bytes([byte_count]) + bytes(coil_bytes)
            return _build_rtu_response(slave_id, fc, data)
        # Unsupported FC: return exception 0x01
        return _build_rtu_response(slave_id, fc | 0x80, bytes([0x01]))

    # ------------------------------------------------------------------ #
    # Control API                                                          #
    # ------------------------------------------------------------------ #

    def stop(self) -> None:
        """Signal the thread to stop and close the socket."""
        self._stop_event.set()
        if self._sock:
            try:
                self._sock.close()
            except OSError:
                pass

    def wait_connected(self, timeout: float = 5.0) -> bool:
        """Block until connected (or timeout). Returns True if connected."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.connected:
                return True
            time.sleep(0.05)
        return False
