"""
Modbus RTU packet builders and traffic injector for QEMU UART chardev tests.

Background
----------
Prior to this module the QEMU firmware shipped a background task
(modbus_mock_qemu.c) that fabricated FC03 request/response traffic on its
own and periodically injected a packet with a deliberately corrupted CRC,
so every sniffer- and cache-related pytest implicitly depended on packets
no one in the test had asked for.

The firmware mock is gone.  QEMU now exposes UART1 and UART2 as TCP
chardev sockets (5561 / 5562 — see conftest.qemu_process), so pytest
generates exactly the bytes each test needs and writes them directly into
the UART RX FIFO of the active RS-485 port.  This exercises the real
serial RX path (UART event task → serial.c receive_handler →
sniffer_receive_cb_N → sniffer_process), not a back-door hook.

Design choice — concatenated request+response per cycle
-------------------------------------------------------
The original firmware mock concatenated each request and its response
into a single sniff_handler() call (no inter-frame gap) to exercise the
stream_splitter path in sniffer_process().  This module preserves that
shape so the existing sniffer assertions that check master/slave
separation keep working.
"""

import socket
import threading
import time
from typing import Optional


# ---------------------------------------------------------------------------
# UART chardev port mapping (matches api_tests/conftest.py qemu_process)
# ---------------------------------------------------------------------------

# Port 1 → UART1 chardev → host TCP 5561
# Port 2 → UART2 chardev → host TCP 5562
UART_HOST = "127.0.0.1"
UART_TCP_PORT = {1: 5561, 2: 5562}


# ---------------------------------------------------------------------------
# Modbus RTU primitives
# ---------------------------------------------------------------------------

def modbus_crc16(data: bytes) -> int:
    """CRC-16/Modbus: poly 0xA001, init 0xFFFF, LSB-first.

    Use to_bytes(2, 'little') to serialise — the Modbus wire format puts
    the low byte first.
    """
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc


def append_crc(payload: bytes) -> bytes:
    """Return payload followed by its little-endian CRC-16/Modbus."""
    return payload + modbus_crc16(payload).to_bytes(2, "little")


# ---------------------------------------------------------------------------
# Frame builders
# ---------------------------------------------------------------------------

def build_fc03_request(slave: int, start_addr: int, reg_count: int) -> bytes:
    """Build an FC03 Read Holding Registers request frame (8 bytes incl. CRC)."""
    pdu = bytes([
        slave & 0xFF,
        0x03,
        (start_addr >> 8) & 0xFF, start_addr & 0xFF,
        (reg_count >> 8) & 0xFF, reg_count & 0xFF,
    ])
    return append_crc(pdu)


def build_fc03_response(slave: int, reg_count: int, base_value: int) -> bytes:
    """Build an FC03 response frame; register[i] = base_value + i."""
    byte_count = reg_count * 2
    body = bytearray([slave & 0xFF, 0x03, byte_count])
    for i in range(reg_count):
        value = (base_value + i) & 0xFFFF
        body.append((value >> 8) & 0xFF)
        body.append(value & 0xFF)
    return append_crc(bytes(body))


def build_fc03_exchange(slave: int, start_addr: int, reg_count: int,
                        base_value: int) -> bytes:
    """Concatenated request+response, as the firmware mock used to emit.

    Sending request and response back-to-back exercises the stream_splitter
    path in sniffer_process(); the split into master/slave packets is the
    sniffer's responsibility, not ours.
    """
    return build_fc03_request(slave, start_addr, reg_count) + \
        build_fc03_response(slave, reg_count, base_value)


# Static bad-CRC packet identical to the old mock's choice:
# slave=1, FC03, addr=0x0000, count=0x0001, CRC bytes 0xFF 0xFF (correct
# would be 0x0A 0x84).  Kept as a constant so multiple tests share the same
# golden bad-CRC frame.
BAD_CRC_PACKET: bytes = bytes([0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0xFF, 0xFF])


# ---------------------------------------------------------------------------
# Raw UART socket injection
# ---------------------------------------------------------------------------

def open_uart_socket(port: int, connect_timeout: float = 5.0) -> socket.socket:
    """Open a TCP connection to the QEMU UART chardev for the given RS-485 port.

    `port` is 1 or 2 (matches the user-facing /ports/N URLs and the rs485_N
    fields in /info).  The socket is returned in blocking mode; the caller
    is responsible for closing it.
    """
    if port not in UART_TCP_PORT:
        raise ValueError(f"unknown RS-485 port {port}; expected 1 or 2")
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(connect_timeout)
    s.connect((UART_HOST, UART_TCP_PORT[port]))
    s.settimeout(None)  # switch to true blocking mode after connect
    return s


def inject_bytes(port: int, data: bytes,
                 sock: Optional[socket.socket] = None) -> None:
    """Write `data` into the UART RX of the given RS-485 port.

    Pass an already-open `sock` to reuse one connection across many calls;
    omit it for one-shot injections (a fresh socket is opened and closed).
    """
    if sock is not None:
        sock.sendall(data)
        return
    s = open_uart_socket(port)
    try:
        s.sendall(data)
    finally:
        s.close()


# ---------------------------------------------------------------------------
# PacketInjector — context manager that reproduces the old mock's traffic
# ---------------------------------------------------------------------------

# Mock parameter constants chosen to match modbus_mock_qemu.c so existing test
# expectations (e.g. number of unique register addresses, the bad-CRC cadence)
# carry over unchanged.
DEFAULT_SLAVE        = 1
DEFAULT_LIVE_START   = 0
DEFAULT_LIVE_COUNT   = 5
DEFAULT_LIVE_BASE    = 1000
DEFAULT_STATIC_START = 5
DEFAULT_STATIC_COUNT = 5
DEFAULT_STATIC_BASE  = 2000
DEFAULT_INTERVAL     = 0.5
DEFAULT_BAD_CRC_PERIOD = 5


class PacketInjector:
    """Background traffic generator that mirrors the old firmware mock.

    Usage:

        with PacketInjector(port=1) as inj:
            ...  # collect packets, query cache, etc.

    Each cycle (default 500 ms) writes one concatenated FC03 request+response
    pair for the live register block to UART1.  Every Nth cycle (default 5)
    it also writes the BAD_CRC packet so tests that look for invalid CRC
    packets still find them within their timeout window.

    The static register block is sent once at start so cache tests that
    expect both live and static cache entries continue to work.

    The injector also pre-sends one live exchange before the periodic loop
    starts so tests that race the first packet (within ~1 s) don't fail.

    The underlying TCP socket is kept open for the injector's lifetime; the
    QEMU UART chardev simply delivers the bytes byte-by-byte into the UART
    RX FIFO of the real ESP-IDF UART driver.
    """

    def __init__(
        self,
        port: int = 1,
        *,
        slave: int = DEFAULT_SLAVE,
        live_start: int = DEFAULT_LIVE_START,
        live_count: int = DEFAULT_LIVE_COUNT,
        live_base: int = DEFAULT_LIVE_BASE,
        static_start: Optional[int] = DEFAULT_STATIC_START,
        static_count: int = DEFAULT_STATIC_COUNT,
        static_base: int = DEFAULT_STATIC_BASE,
        interval: float = DEFAULT_INTERVAL,
        bad_crc_period: int = DEFAULT_BAD_CRC_PERIOD,
        include_bad_crc: bool = True,
    ):
        self.port = port
        self.slave = slave
        self.live_start = live_start
        self.live_count = live_count
        self.live_base = live_base
        self.static_start = static_start
        self.static_count = static_count
        self.static_base = static_base
        self.interval = interval
        self.bad_crc_period = bad_crc_period
        self.include_bad_crc = include_bad_crc

        self._stop = threading.Event()
        self._thread: Optional[threading.Thread] = None
        self._error: Optional[BaseException] = None
        self._sock: Optional[socket.socket] = None

    # -- context manager --------------------------------------------------

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, exc_type, exc, tb):
        self.stop()
        return False

    # -- lifecycle --------------------------------------------------------

    def start(self) -> None:
        if self._thread is not None:
            raise RuntimeError("PacketInjector already started")

        # One TCP socket per injector, reused for every cycle.  Closed on
        # stop() (and on start() failure, see the try/except below).
        self._sock = open_uart_socket(self.port)

        try:
            # One-shot static block — gives cache tests a stable set of
            # entries that age naturally rather than being refreshed every
            # cycle.
            if self.static_start is not None and self.static_count > 0:
                self._safe_inject(build_fc03_exchange(
                    self.slave,
                    self.static_start,
                    self.static_count,
                    self.static_base + self.static_start,
                ))

            # One live exchange immediately so tests can see traffic without
            # waiting for the first interval tick.
            self._safe_inject(self._build_live_exchange())

            self._thread = threading.Thread(target=self._run, daemon=True)
            self._thread.start()
        except BaseException:
            try:
                self._sock.close()
            finally:
                self._sock = None
            raise

    def stop(self) -> None:
        if self._thread is None:
            if self._sock is not None:
                try:
                    self._sock.close()
                finally:
                    self._sock = None
            return
        self._stop.set()
        self._thread.join(timeout=5)
        if self._thread.is_alive():
            # Thread is wedged in sendall (e.g. firmware UART driver stalled).
            # Leak the socket rather than close it under the active thread,
            # and surface the hang.
            self._thread = None
            raise RuntimeError(
                "PacketInjector background thread did not exit within 5s"
            )
        self._thread = None
        if self._sock is not None:
            try:
                self._sock.close()
            finally:
                self._sock = None
        # Re-raise the first background failure so the test fails loudly
        # instead of passing while the injector silently died.
        if self._error is not None:
            err = self._error
            self._error = None
            raise err

    # -- internal ---------------------------------------------------------

    def _build_live_exchange(self) -> bytes:
        return build_fc03_exchange(
            self.slave,
            self.live_start,
            self.live_count,
            self.live_base + self.live_start,
        )

    def _safe_inject(self, data: bytes) -> None:
        """Inject and capture errors instead of letting the thread die silently."""
        try:
            inject_bytes(self.port, data, sock=self._sock)
        except BaseException as exc:  # noqa: BLE001 — propagate from stop()
            self._error = exc
            raise

    def _run(self) -> None:
        cycle = 0
        try:
            while not self._stop.is_set():
                cycle += 1
                if self.include_bad_crc and (cycle % self.bad_crc_period) == 0:
                    self._safe_inject(BAD_CRC_PACKET)

                self._safe_inject(self._build_live_exchange())

                # Sleep in small chunks so stop() returns promptly.  Clamp at
                # 0 — the deadline can slip past between the while-check and
                # the subtract on a slow scheduler.
                end = time.monotonic() + self.interval
                while not self._stop.is_set() and time.monotonic() < end:
                    time.sleep(max(0.0, min(0.05, end - time.monotonic())))
        except BaseException as exc:  # noqa: BLE001
            if self._error is None:
                self._error = exc
