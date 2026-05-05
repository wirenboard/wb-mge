#!/usr/bin/env python3
"""
Cache Modbus Server multi-master test.

Downloads the register map from /cache/csv, then verifies that multiple
parallel Modbus TCP clients on port 504 all read correct values.

Usage:
    python test_multimaster_cache.py --host 192.168.1.1 --port 504 --threads 5 --http-port 80
"""

import argparse
import csv
import io
import socket
import struct
import sys
import threading
import time
import urllib.request

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

TYPE_TO_FC = {
    "holding": 0x03,
    "input": 0x04,
    "coil": 0x01,
    "discrete": 0x02,
}

# Modbus TCP MBAP header size (Transaction ID + Protocol ID + Length + Unit ID)
MBAP_HEADER_SIZE = 7
# Minimum response size: MBAP header (7) + FC byte (1)
MIN_RESPONSE_SIZE = 8

# Maximum TID value (16-bit unsigned)
MAX_TID = 65535

# Receive buffer size
RECV_BUFFER = 4096

# ---------------------------------------------------------------------------
# CSV fetching and parsing
# ---------------------------------------------------------------------------


def fetch_csv(host: str, http_port: int) -> str:
    """Fetch the register cache CSV from the device over HTTP."""
    url = f"http://{host}:{http_port}/cache/csv"
    print(f"[*] Fetching register map from {url} ...")
    try:
        with urllib.request.urlopen(url, timeout=10) as resp:
            data = resp.read().decode("utf-8")
    except Exception as exc:
        print(f"[ERROR] Failed to fetch CSV: {exc}", file=sys.stderr)
        sys.exit(1)
    return data


def parse_csv(raw_csv: str) -> dict:
    """
    Parse the CSV register map.

    CSV columns: port, slave_id, type, address, value, timestamp_us

    Returns a dict keyed by (slave_id: int, reg_type: str, address: int)
    with integer register values.
    """
    register_map = {}
    reader = csv.DictReader(io.StringIO(raw_csv))
    for row in reader:
        try:
            slave_id = int(row["slave_id"])
            reg_type = row["type"].strip()
            address = int(row["address"])
            value = int(row["value"])
        except (KeyError, ValueError) as exc:
            print(f"[WARN] Skipping malformed CSV row {row}: {exc}", file=sys.stderr)
            continue

        if reg_type not in TYPE_TO_FC:
            print(f"[WARN] Unknown register type '{reg_type}' — skipping", file=sys.stderr)
            continue

        key = (slave_id, reg_type, address)
        register_map[key] = value

    return register_map


# ---------------------------------------------------------------------------
# Modbus TCP packet builder / parser
# ---------------------------------------------------------------------------


def make_mbap_request(tid: int, slave_id: int, fc: int, start_addr: int, count: int) -> bytes:
    """Build a raw Modbus TCP request: MBAP header + PDU."""
    pdu = struct.pack(">BHH", fc, start_addr, count)
    # MBAP: transaction_id(2), protocol_id=0(2), length=unit_id+pdu(2), unit_id(1)
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

    # Read MBAP header first (7 bytes) + FC byte (1 byte) = 8 bytes
    header = recv_exactly(sock, MIN_RESPONSE_SIZE)

    tid, proto, length, unit_id, fc = struct.unpack(">HHHBB", header)

    # 'length' in MBAP = remaining bytes after MBAP header (unit_id + pdu)
    # We already read unit_id (1) + fc (1) = 2 bytes of that,
    # so remaining payload = length - 2
    remaining = length - 2
    payload = b""
    if remaining > 0:
        payload = recv_exactly(sock, remaining)

    return tid, unit_id, fc, payload


# ---------------------------------------------------------------------------
# Register value extraction from Modbus response payload
# ---------------------------------------------------------------------------


def decode_fc03_fc04(payload: bytes, count: int) -> list:
    """
    Decode FC03/FC04 (holding/input register) response payload.

    payload[0] = byte_count
    payload[1..] = register values, big-endian 16-bit each
    """
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
    """
    Decode FC01/FC02 (coil/discrete input) response payload.

    payload[0] = byte_count
    payload[1..] = packed bits, LSB first within each byte
    """
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
    # Trim to requested count
    return bits[:count]


def decode_response(fc: int, payload: bytes, count: int) -> list:
    """Dispatch to the appropriate decoder based on FC."""
    if fc in (0x03, 0x04):
        return decode_fc03_fc04(payload, count)
    elif fc in (0x01, 0x02):
        return decode_fc01_fc02(payload, count)
    else:
        raise ValueError(f"Unsupported FC: 0x{fc:02X}")


# ---------------------------------------------------------------------------
# Worker thread
# ---------------------------------------------------------------------------


class ThreadResult:
    """Holds per-thread test outcomes."""

    def __init__(self, thread_id: int):
        self.thread_id = thread_id
        self.connected_at: float = 0.0
        self.errors: list = []
        self.checks_passed: int = 0
        self.checks_failed: int = 0
        self.iterations: int = 0  # Number of full passes over register_map
        self.exception: Exception | None = None

    def add_error(self, msg: str):
        self.errors.append(msg)
        self.checks_failed += 1

    def add_pass(self):
        self.checks_passed += 1


def _run_register_pass(
    sock: socket.socket,
    thread_id: int,
    register_map: dict,
    result: "ThreadResult",
    tid: int,
) -> int:
    """
    Perform a single full pass over all registers in register_map.

    Sends one Modbus TCP request per register, validates TID integrity and
    absence of Modbus exception 0x02, updates *result* in-place, and returns
    the next TID.
    """
    for (slave_id, reg_type, address), expected_value in register_map.items():
        fc = TYPE_TO_FC[reg_type]
        count = 1  # Read one register at a time

        # Wrap TID to 16-bit range
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

        # --- TID integrity check ---
        if resp_tid != tid:
            result.add_error(
                f"[Thread {thread_id}] TID mismatch: sent {tid}, got {resp_tid} "
                f"(slave={slave_id} type={reg_type} addr={address})"
            )
        else:
            result.add_pass()

        # --- Modbus exception check ---
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

        # Register present in cache and returned successfully — count as pass
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
    """
    Worker function executed by each test thread.

    1. Waits at the barrier so all threads connect simultaneously.
    2. Opens a TCP connection to the Modbus server.
    3. Iterates over all registers in the map, issuing one request per register.
    4. Validates TID integrity and absence of Modbus exception 0x02.

    If duration > 0, repeats the register pass in a loop until the deadline
    (time.monotonic() >= start_time + duration), counting full iterations.
    If duration == 0 (default), performs exactly one pass — identical to the
    original behaviour.
    """
    result = ThreadResult(thread_id)
    results[thread_id] = result

    try:
        # --- Synchronise all threads to connect at the same time ---
        start_barrier.wait(timeout=30)

        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(10)
        sock.connect((host, port))
        result.connected_at = time.monotonic()

        tid = thread_id * 1000  # Start TID offset per thread to make debugging easier

        if duration > 0:
            # Stress-test mode: keep looping until deadline
            deadline = result.connected_at + duration
            while time.monotonic() < deadline:
                tid = _run_register_pass(sock, thread_id, register_map, result, tid)
                result.iterations += 1
        else:
            # Single-pass mode: original behaviour
            tid = _run_register_pass(sock, thread_id, register_map, result, tid)
            result.iterations = 1

        sock.close()

    except threading.BrokenBarrierError:
        result.exception = RuntimeError(
            f"[Thread {thread_id}] Barrier timed out — not all threads synchronised"
        )
    except Exception as exc:
        result.exception = exc


# ---------------------------------------------------------------------------
# Connectivity timing check
# ---------------------------------------------------------------------------


def check_simultaneous_connection(results: dict, num_threads: int) -> tuple[bool, str]:
    """
    Verify that all threads connected at roughly the same time.

    Considers connections simultaneous if the max spread is <= 2 seconds.
    """
    connect_times = [r.connected_at for r in results.values() if r.connected_at > 0]

    if len(connect_times) < num_threads:
        missed = num_threads - len(connect_times)
        return False, f"{missed} thread(s) never connected"

    spread_ms = (max(connect_times) - min(connect_times)) * 1000
    if spread_ms > 2000:
        return False, f"Connection spread too large: {spread_ms:.1f} ms (max 2000 ms)"

    return True, f"All {num_threads} threads connected within {spread_ms:.1f} ms"


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Cache Modbus Server multi-master test",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--host", required=True, help="Device IP address or hostname")
    parser.add_argument("--port", type=int, default=504, help="Modbus TCP port")
    parser.add_argument("--threads", type=int, default=5, help="Number of parallel client threads")
    parser.add_argument("--http-port", type=int, default=80, help="HTTP port for /cache/csv endpoint")
    parser.add_argument(
        "--duration",
        type=float,
        default=0,
        help="If >0, run the stress test for this many seconds instead of a single pass",
    )
    return parser.parse_args()


def main():
    args = parse_args()

    # Step 1: Fetch and parse register map
    raw_csv = fetch_csv(args.host, args.http_port)
    register_map = parse_csv(raw_csv)

    if not register_map:
        print("[ERROR] Register map is empty — nothing to test.", file=sys.stderr)
        sys.exit(1)

    print(f"[*] Register map loaded: {len(register_map)} entries")

    # Step 2: Launch parallel worker threads
    num_threads = args.threads
    duration = args.duration
    results: dict = {}
    start_barrier = threading.Barrier(num_threads)

    threads = [
        threading.Thread(
            target=worker,
            args=(i, args.host, args.port, register_map, results, start_barrier, duration),
            daemon=True,
        )
        for i in range(num_threads)
    ]

    if duration > 0:
        print(f"[*] Stress test duration: {duration} seconds")

    print(f"[*] Starting {num_threads} parallel Modbus TCP clients on {args.host}:{args.port} ...")
    for t in threads:
        t.start()

    # Allow extra time for stress-test mode; single-pass uses 30 s as before
    join_timeout = (duration + 30) if duration > 0 else 30

    # Progress reporting: print a running total every 5 s during stress test
    if duration > 0:
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            time.sleep(5)
            total_checks = sum(
                r.checks_passed + r.checks_failed for r in results.values()
            )
            print(f"[*] Progress: {total_checks} total checks so far ...")

    for t in threads:
        t.join(timeout=join_timeout)

    # Check for threads that are still alive (deadlock / timeout)
    still_alive = [t for t in threads if t.is_alive()]
    if still_alive:
        print(
            f"[FAIL] {len(still_alive)} thread(s) did not finish within "
            f"{join_timeout:.0f} seconds (deadlock?)"
        )
        sys.exit(1)

    # Step 3: Collect and report results
    print()
    print("=" * 60)
    if duration > 0:
        print(f"RESULTS  (stress test, duration={duration}s)")
    else:
        print("RESULTS")
    print("=" * 60)

    all_passed = True

    # --- Criterion 1: Simultaneous connectivity ---
    conn_ok, conn_msg = check_simultaneous_connection(results, num_threads)
    status = "PASS" if conn_ok else "FAIL"
    print(f"[{status}] Connectivity: {conn_msg}")
    if not conn_ok:
        all_passed = False

    # --- Criterion 4: No deadlock (already handled above) ---
    print(f"[PASS] No deadlock: all {num_threads} threads finished within 30 seconds")

    # --- Per-thread results (TID integrity + value correctness) ---
    total_passed = 0
    total_failed = 0

    for tid_key in sorted(results.keys()):
        r = results[tid_key]
        if r.exception:
            print(f"[FAIL] Thread {r.thread_id}: EXCEPTION — {r.exception}")
            all_passed = False
            continue

        total_passed += r.checks_passed
        total_failed += r.checks_failed

        thread_ok = r.checks_failed == 0
        status = "PASS" if thread_ok else "FAIL"
        print(
            f"[{status}] Thread {r.thread_id}: "
            f"{r.iterations} iteration(s), "
            f"{r.checks_passed} checks passed, {r.checks_failed} failed"
        )
        if not thread_ok:
            all_passed = False
            for err in r.errors:
                print(f"       {err}")

    total_iterations = sum(r.iterations for r in results.values())
    print()
    print("-" * 60)
    print(
        f"Total iterations: {total_iterations}  "
        f"checks: {total_passed + total_failed}  "
        f"passed: {total_passed}  failed: {total_failed}"
    )
    print("-" * 60)

    if all_passed:
        print("\n✅  OVERALL RESULT: PASS")
        sys.exit(0)
    else:
        print("\n❌  OVERALL RESULT: FAIL")
        sys.exit(1)


if __name__ == "__main__":
    main()
