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
                 fake_value: int = 0x1234, connect_timeout: float = 5.0,
                 exception_fc: dict = None, drop_count: int = 0):
        super().__init__(daemon=True)
        self.host = host
        self.port = port
        self.fake_value = fake_value      # register/coil value returned for any address
        self.connect_timeout = connect_timeout
        self.exception_fc = exception_fc or {}   # FC -> exception_code mapping
        self.drop_count = drop_count             # silently drop next N requests
        self.request_count = 0            # number of RTU requests handled
        self.connected = False            # True once TCP connection is established
        self._stop_event = threading.Event()
        self._sock = None
        # FC16 write tracking — set when a valid FC16 frame is received
        self.last_write_addr = None       # starting address of the last FC16 write
        self.last_write_qty = None        # quantity of registers in the last FC16 write
        self.last_write_values = None     # list of register values from the last FC16 write

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
        while True:
            # Need at least slave_id + FC to determine frame type
            if len(buf) < 4:
                break

            fc = buf[1]

            if fc == 0x10:
                # FC16 (Write Multiple Registers): variable-length frame
                # Frame: slave_id(1) + FC(1) + start_addr(2) + qty(2) + byte_count(1)
                #        + data(byte_count) + CRC(2)
                if len(buf) < 7:
                    break   # need at least 7 bytes to read byte_count field
                byte_count = buf[6]
                req_len = 9 + byte_count  # 7-byte header + data + 2-byte CRC
                if len(buf) < req_len:
                    break   # incomplete FC16 frame — wait for more data
                # Validate CRC over the complete frame (excluding 2-byte CRC suffix)
                crc_recv = (buf[req_len - 1] << 8) | buf[req_len - 2]
                crc_calc = _crc16(buf[:req_len - 2])
                if crc_recv != crc_calc:
                    # CRC mismatch: discard one byte and retry (re-sync)
                    buf = buf[1:]
                    continue
                # Parse FC16 fields
                slave_id = buf[0]
                addr = (buf[2] << 8) | buf[3]
                qty = (buf[4] << 8) | buf[5]
                # Store write data for test verification (only when lengths match)
                if byte_count == qty * 2 and qty > 0:
                    self.last_write_addr = addr
                    self.last_write_qty = qty
                    self.last_write_values = list(
                        struct.unpack(f'>{qty}H', buf[7:7 + byte_count])
                    )
                # Silent drop: simulate RTU slave not responding (for timeout tests)
                if self.drop_count > 0:
                    self.drop_count -= 1
                    self.request_count += 1
                    buf = buf[req_len:]
                    continue
                response = self._build_response(slave_id, fc, addr, qty)
                if response and self._sock:
                    try:
                        self._sock.sendall(response)
                    except OSError:
                        return buf
                self.request_count += 1
                buf = buf[req_len:]
                continue

            # Standard fixed-length request: slave(1) + FC(1) + addr(2) + count(2) + CRC(2) = 8 bytes
            if len(buf) < 8:
                break
            slave_id = buf[0]
            addr = (buf[2] << 8) | buf[3]
            count = (buf[4] << 8) | buf[5]
            crc_recv = (buf[7] << 8) | buf[6]   # little-endian in RTU
            crc_calc = _crc16(buf[:6])

            if crc_recv != crc_calc:
                # CRC mismatch: discard one byte and retry (re-sync)
                buf = buf[1:]
                continue

            # Silent drop: simulate RTU slave not responding (for timeout tests)
            if self.drop_count > 0:
                self.drop_count -= 1
                self.request_count += 1
                buf = buf[8:]
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
        """Build a Modbus RTU response for FC01/FC02/FC03/FC04/FC16, or exception if configured."""
        # Return configured exception if this FC is in the exception map
        if fc in self.exception_fc:
            exc_code = self.exception_fc[fc]
            return _build_rtu_response(slave_id, fc | 0x80, bytes([exc_code]))

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
        if fc == 0x10:
            # FC16 (Write Multiple Registers): echo response = slave_id + FC + start_addr + qty
            return _build_rtu_response(slave_id, fc, struct.pack('>HH', addr, count))
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


class _UartNoEchoThread(threading.Thread):
    """Connects to QEMU UART chardev and reads data without ever responding.

    Simulates a silent RS-485 line (no device replies). Used for SN-02 tests
    that verify the sniffer emits a {type: "timeout"} WebSocket packet when
    the RTU slave does not respond within the configured timeout.

    Usage:
        with _UartNoEchoThread(host="127.0.0.1", port=5561) as t:
            t.wait_connected(timeout=5.0)
            # ... trigger a Modbus request that will time out ...
    """
    def __init__(self, host: str = '127.0.0.1', port: int = 5561,
                 connect_timeout: float = 5.0):
        super().__init__(daemon=True)
        self.host = host
        self.port = port
        self.connect_timeout = connect_timeout
        self.connected = False
        self.bytes_received = 0       # count of bytes absorbed (for diagnostics)
        self._stop_event = threading.Event()
        self._sock = None

    def run(self) -> None:
        """Connect and drain data without sending any response."""
        try:
            self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._sock.settimeout(self.connect_timeout)
            self._sock.connect((self.host, self.port))
            self.connected = True
            self._sock.settimeout(0.5)  # short recv timeout to check stop event
        except OSError:
            return

        while not self._stop_event.is_set():
            try:
                chunk = self._sock.recv(256)
                if not chunk:
                    break
                self.bytes_received += len(chunk)
            except socket.timeout:
                continue
            except OSError:
                break

        try:
            self._sock.close()
        except OSError:
            pass

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

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, exc_type, exc, tb):
        self.stop()
        self.join(timeout=3.0)
        return False
