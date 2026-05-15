"""Modbus TCP helper constants, functions, and classes for cache multimaster tests"""

import csv
import io
import socket
import struct
import threading
import time


TYPE_TO_FC = {
    "holding": 0x03,
    "input": 0x04,
    "coil": 0x01,
    "discrete": 0x02,
}

MBAP_HEADER_SIZE = 7
MIN_RESPONSE_SIZE = 8
MAX_TID = 65535
RECV_BUFFER = 4096


def make_mbap_request(tid: int, slave_id: int, fc: int, start_addr: int, count: int) -> bytes:
    """Build a raw Modbus TCP request: MBAP header + PDU."""
    pdu = struct.pack(">BHH", fc, start_addr, count)
    mbap = struct.pack(">HHHB", tid, 0, len(pdu) + 1, slave_id)
    return mbap + pdu


def recv_exactly(sock: socket.socket, n: int) -> bytes:
    """Receive exactly n bytes from a socket, blocking until done or error."""
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError(f"Socket closed after {len(buf)}/{n} bytes")
        buf += chunk
    return buf


def send_and_receive(sock: socket.socket, request: bytes) -> tuple:
    """
    Send a Modbus TCP request and receive the full response.

    Returns (tid, unit_id, fc, payload_bytes) or raises on error.
    """
    sock.sendall(request)

    header = recv_exactly(sock, MIN_RESPONSE_SIZE)

    tid, proto, length, unit_id, fc = struct.unpack(">HHHBB", header)

    remaining = length - 2
    payload = b""
    if remaining > 0:
        payload = recv_exactly(sock, remaining)

    return tid, unit_id, fc, payload


def decode_fc03_fc04(payload: bytes, count: int) -> list:
    """Decode FC03/FC04 (holding/input register) response payload."""
    if len(payload) < 1:
        raise ValueError("FC03/FC04 payload too short")
    byte_count = payload[0]
    if len(payload) < 1 + byte_count:
        raise ValueError(f"FC03/FC04 payload truncated: expected {1 + byte_count}, got {len(payload)}")
    values = [
        struct.unpack(">H", payload[1 + i * 2 : 1 + i * 2 + 2])[0]
        for i in range(byte_count // 2)
    ]
    return values


def decode_fc01_fc02(payload: bytes, count: int) -> list:
    """Decode FC01/FC02 (coil/discrete input) response payload."""
    if len(payload) < 1:
        raise ValueError("FC01/FC02 payload too short")
    byte_count = payload[0]
    if len(payload) < 1 + byte_count:
        raise ValueError(f"FC01/FC02 payload truncated: expected {1 + byte_count}, got {len(payload)}")
    bits = []
    for byte_idx in range(byte_count):
        b = payload[1 + byte_idx]
        for bit_idx in range(8):
            bits.append((b >> bit_idx) & 1)
    return bits[:count]


def decode_response(fc: int, payload: bytes, count: int) -> list:
    """Dispatch to the appropriate decoder based on FC."""
    if fc in (0x03, 0x04):
        return decode_fc03_fc04(payload, count)
    elif fc in (0x01, 0x02):
        return decode_fc01_fc02(payload, count)
    else:
        raise ValueError(f"Unsupported FC: 0x{fc:02X}")


class ThreadResult:
    """Holds per-thread test outcomes."""

    def __init__(self, thread_id: int):
        self.thread_id = thread_id
        self.connected_at: float = 0.0
        self.errors: list = []
        self.checks_passed: int = 0
        self.checks_failed: int = 0
        self.iterations: int = 0
        self.exception = None

    def add_error(self, msg: str):
        self.errors.append(msg)
        self.checks_failed += 1

    def add_pass(self):
        self.checks_passed += 1


def _run_register_pass(
    sock: socket.socket,
    thread_id: int,
    register_map: dict,
    result: ThreadResult,
    tid: int,
) -> int:
    """Perform a single full pass over all registers in register_map."""
    for (slave_id, reg_type, address), _register_data in register_map.items():
        fc = TYPE_TO_FC[reg_type]
        count = 1

        tid = tid % (MAX_TID + 1)
        request = make_mbap_request(tid, slave_id, fc, address, count)

        try:
            resp_tid, resp_unit_id, resp_fc, payload = send_and_receive(sock, request)
        except Exception as exc:
            result.add_error(
                f"[Thread {thread_id}] Socket error reading "
                f"slave={slave_id} type={reg_type} addr={address}: {exc}"
            )
            tid += 1
            continue

        if resp_tid != tid:
            result.add_error(
                f"[Thread {thread_id}] TID mismatch: sent {tid}, got {resp_tid} "
                f"(slave={slave_id} type={reg_type} addr={address})"
            )
            tid += 1
            continue

        if resp_fc & 0x80:
            exception_code = payload[0] if payload else -1
            if exception_code == 0x02:
                result.add_error(
                    f"[Thread {thread_id}] Modbus exception 0x02 (not in cache): "
                    f"slave={slave_id} type={reg_type} addr={address}"
                )
            else:
                result.add_error(
                    f"[Thread {thread_id}] Modbus exception 0x{exception_code:02X}: "
                    f"slave={slave_id} type={reg_type} addr={address}"
                )
            tid += 1
            continue

        try:
            decoded_values = decode_response(fc, payload, count)
        except ValueError as exc:
            result.add_error(
                f"[Thread {thread_id}] Decode error "
                f"slave={slave_id} type={reg_type} addr={address}: {exc}"
            )
            tid += 1
            continue

        if not decoded_values:
            result.add_error(
                f"[Thread {thread_id}] Decode returned empty list "
                f"slave={slave_id} type={reg_type} addr={address}"
            )
            tid += 1
            continue

        result.add_pass()

        tid += 1

    return tid


def worker(
    thread_id: int,
    host: str,
    port: int,
    register_map: dict,
    results: dict,
    start_barrier: threading.Barrier,
    duration: float = 0,
):
    """Worker function executed by each test thread."""
    result = ThreadResult(thread_id)
    results[thread_id] = result

    try:
        start_barrier.wait(timeout=30)

        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(10)
        sock.connect((host, port))
        result.connected_at = time.monotonic()

        tid = thread_id * 1000

        if duration > 0:
            deadline = result.connected_at + duration
            while time.monotonic() < deadline:
                tid = _run_register_pass(sock, thread_id, register_map, result, tid)
                result.iterations += 1
        else:
            tid = _run_register_pass(sock, thread_id, register_map, result, tid)
            result.iterations = 1

        sock.close()

    except threading.BrokenBarrierError:
        result.exception = RuntimeError(
            f"[Thread {thread_id}] Barrier timed out — not all threads synchronised"
        )
    except Exception as exc:
        result.exception = exc


def check_simultaneous_connection(results: dict, num_threads: int) -> tuple:
    """Verify that all threads connected at roughly the same time."""
    connect_times = [r.connected_at for r in results.values() if r.connected_at > 0]

    if len(connect_times) < num_threads:
        missed = num_threads - len(connect_times)
        return False, f"{missed} thread(s) never connected"

    spread_ms = (max(connect_times) - min(connect_times)) * 1000
    if spread_ms > 2000:
        return False, f"Connection spread too large: {spread_ms:.1f} ms (max 2000 ms)"

    return True, f"All {num_threads} threads connected within {spread_ms:.1f} ms"


def parse_csv(raw_csv: str) -> dict:
    """Parse the CSV register map. Returns dict keyed by (slave_id, reg_type, address)."""
    register_map = {}
    reader = csv.DictReader(io.StringIO(raw_csv))
    for row in reader:
        try:
            slave_id = int(row["slave_id"])
            reg_type = row["type"].strip()
            address = int(row["address"])
            value = int(row["value"])
            age_s = int(row["age_s"])
        except (KeyError, ValueError) as exc:
            print(f"[WARN] Skipping malformed CSV row {row}: {exc}")
            continue

        if reg_type not in TYPE_TO_FC:
            print(f"[WARN] Unknown register type '{reg_type}' — skipping")
            continue

        key = (slave_id, reg_type, address)
        register_map[key] = (value, age_s)

    return register_map


def query_register_once(host: str, port: int, slave_id: int, reg_type: str, address: int) -> tuple:
    """Open a fresh TCP socket, send one Modbus TCP request, receive the response."""
    fc = TYPE_TO_FC[reg_type]
    request = make_mbap_request(1, slave_id, fc, address, 1)
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(10)
        try:
            sock.connect((host, port))
            _tid, _unit_id, resp_fc, payload = send_and_receive(sock, request)
        finally:
            sock.close()
    except Exception as exc:
        return ("error", str(exc))

    if resp_fc & 0x80:
        code = payload[0] if payload else -1
        return ("exception", code)

    try:
        decoded = decode_response(fc, payload, 1)
    except ValueError as exc:
        return ("error", f"Decode error: {exc}")

    if not decoded:
        return ("error", "Decode returned empty list")

    return ("ok", decoded[0])


def run_staleness_test(host: str, port: int, api, register_map: dict) -> tuple:
    """
    Test that stale cache entries trigger Modbus exception 0x0B, and that
    disabling the timeout (=0) makes them readable again.
    """
    report_lines = []

    # Read original timeout value before modification to restore it correctly in finally
    orig_resp = api.session.get(f"{api.base_url}/settings", timeout=10)
    original_timeout = orig_resp.json().get("cache_value_timeout_s", 0)

    stale_registers = [
        (key, value, age_s)
        for key, (value, age_s) in register_map.items()
        if age_s >= 3
    ]

    if not stale_registers:
        report_lines.append("[FAIL] No registers with age_s >= 3 found — staleness test requires stale data")
        return (False, report_lines)

    candidates = stale_registers[:5]
    report_lines.append(
        f"[INFO] Staleness test: {len(candidates)} register(s) with age_s >= 3 selected"
    )

    passed = True
    timeout_was_set = False

    try:
        resp = api.session.post(f"{api.base_url}/settings", json={"cache_value_timeout_s": 1}, timeout=10)
        if resp.status_code not in (200, 204):
            raise RuntimeError(f"Failed to set cache_value_timeout_s=1: HTTP {resp.status_code}")
        timeout_was_set = True
        report_lines.append("[INFO] cache_value_timeout_s set to 1")

        for (slave_id, reg_type, address), _value, age_s in candidates:
            result = query_register_once(host, port, slave_id, reg_type, address)
            if result == ("exception", 0x0B):
                report_lines.append(
                    f"[PASS] slave={slave_id} type={reg_type} addr={address} age={age_s}s "
                    f"→ exception 0x0B as expected"
                )
            else:
                passed = False
                report_lines.append(
                    f"[FAIL] slave={slave_id} type={reg_type} addr={address} age={age_s}s "
                    f"→ expected exception 0x0B, got {result}"
                )
    finally:
        try:
            resp = api.session.post(f"{api.base_url}/settings", json={"cache_value_timeout_s": original_timeout}, timeout=10)
            if resp.status_code not in (200, 204):
                raise RuntimeError(f"HTTP {resp.status_code}")
            report_lines.append(f"[INFO] cache_value_timeout_s restored to {original_timeout}")
        except Exception as exc:
            report_lines.append(f"[ERROR] Failed to restore cache_value_timeout_s={original_timeout}: {exc}")
            passed = False
        else:
            if timeout_was_set:
                for (slave_id, reg_type, address), _expected_value, _age_s in candidates:
                    result = query_register_once(host, port, slave_id, reg_type, address)
                    if result[0] == "ok":
                        report_lines.append(
                            f"[PASS] slave={slave_id} type={reg_type} addr={address} "
                            f"→ value={result[1]} readable with timeout=0 as expected"
                        )
                    else:
                        passed = False
                        report_lines.append(
                            f"[FAIL] slave={slave_id} type={reg_type} addr={address} "
                            f"→ expected ok read with timeout=0, got {result}"
                        )

    return (passed, report_lines)
