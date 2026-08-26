"""OTA update e2e tests against POST /update.

Covers:
- auth check (401 without session)
- Content-Type validation
- wb_app_desc validation (magic word, signature, body length)
- truncated stream handling (device must stay alive after aborted upload)
- full positive-path update (device reboots into ota_1 and comes back online)

The positive-path test runs LAST in the file — after it the boot partition is
ota_1 and the running firmware is the one we just uploaded. `make qemu-test`
rebuilds qemu_flash.bin per session, so the next run starts clean.

Valid firmware fixture: build/qemu_mge.bin, produced by `make qemu-build`.
"""

import os
import re
import socket
import struct
import time
import warnings
from pathlib import Path

import pytest
import requests

PROJECT_ROOT = Path(__file__).parent.parent
QEMU_FIRMWARE = PROJECT_ROOT / "build" / "qemu_mge.bin"

# Baudrate options for rs485 settings persistence check
_BAUDRATES = [1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200]

# Layout constants — keep in sync with main/wb_app_desc/wb_app_desc.h
ESP_IMAGE_HEADER_LEN = 24
ESP_IMAGE_SEGMENT_HEADER_LEN = 8
ESP_APP_DESC_LEN = 256
WB_APP_DESC_OFFSET = ESP_IMAGE_HEADER_LEN + ESP_IMAGE_SEGMENT_HEADER_LEN + ESP_APP_DESC_LEN
WB_APP_DESC_SIZE = 192
WB_APP_DESC_MAGIC_WORD = 0xDACBBCAB
SIGNATURE_OFFSET = WB_APP_DESC_OFFSET + 4
SIGNATURE_LEN = 12
MIN_VALID_HEAD_LEN = WB_APP_DESC_OFFSET + WB_APP_DESC_SIZE

# --- Stall limit, mirrored from main/ota_handler.c and main/http_server.h --------------------
# Mirrored rather than read back from the device on purpose: catching a change to these IS the
# point of the guest-clock check in test_ota_client_vanishes_without_closing_socket.
OTA_RECV_STALL_TIMEOUT_MS = 30000
HTTP_RECV_WAIT_TIMEOUT_S = 5
OTA_RECV_MAX_STALL_TIMEOUTS = OTA_RECV_STALL_TIMEOUT_MS // (HTTP_RECV_WAIT_TIMEOUT_S * 1000)

# Drift allowance on the guest-clock delta, and nothing more — the limit the handler prints in its
# own log line is what pins the constants, so this band is not asked to tell one budget from
# another. It must still be narrow enough that no OTHER integer receive window can hide inside it:
# with the limit pinned the delta is (limit - 1) windows, so the smallest change a different whole
# second of window can make is (limit - 1) * 1000 ms, and the band has to stay under that. Halving
# BOTH terms keeps that true for any constants, not just today's — a band of half a window alone
# would let W=4 pass against a mirrored W=5 at limit 3, quietly dropping real silence from 15 s to
# 12 s. At the shipped constants the two terms are equal at 2500 ms and neither binds, because
# limit - 1 (5) happens to equal the window in seconds (5).
# Sized against the only measurement there is: 14 ms of drift across the five windows, in a solo
# idle-host run of this file. Drift under the CI contention this check exists for has never been
# measured — the expectation that it stays small is an argument, not data: the receive timeout and
# the log timestamp both come off the FreeRTOS tick, so guest starvation moves them together.
STALL_GUEST_TOLERANCE_MS = min(HTTP_RECV_WAIT_TIMEOUT_S * 1000 // 2,
                               (OTA_RECV_MAX_STALL_TIMEOUTS - 1) * 1000 // 2)

# Host-side environment guard — NOT a claim about the firmware. Recovery costs the guest
# OTA_RECV_STALL_TIMEOUT_MS of silence plus one more receive window while the IDF purges the unsent
# body (~35 s), but this is host wall clock and also carries the guest's own slowness and two HTTP
# round trips: 62 s has been measured on an idle laptop, and a contended CI node stretches single
# requests several times over. Four times the guest figure, so only a node that is broken rather
# than merely loaded reaches it.
STALL_RECOVERY_ENV_BUDGET_S = 4 * (OTA_RECV_STALL_TIMEOUT_MS // 1000 + HTTP_RECV_WAIT_TIMEOUT_S)

# Lower bound: the device must NOT answer instantly. Without it the test passes on a firmware that
# rejects the upload outright (no stall handling at all), which is the opposite of what it checks.
# Host wall clock can only overstate the guest's ~35 s, so a loaded node cannot trip this one.
# Under --qemu the "Network timeout 1/N" line proves the same thing precisely; this is what is
# left when there is no serial to read.
STALL_RECOVERY_MIN_S = 20

# (connect, read), never a scalar. requests applies a scalar to EACH phase separately and
# api_client sets Connection: close, so every call reconnects and a scalar here would double the
# ceiling. Same reasoning and same connect budget as conftest's _RS485_HTTP_TIMEOUT: connect is a
# loopback handshake to a QEMU hostfwd port, so it is immediate or never. Deliberately above
# STALL_RECOVERY_ENV_BUDGET_S — a firmware that never aborts must reach this timeout and report a
# transport failure, rather than come back with a large elapsed time for a bound to misread.
STALL_INFO_TIMEOUT = (5, STALL_RECOVERY_ENV_BUDGET_S + 30)


def _qemu_serial_log_path():
    """Path to the live QEMU serial capture written by conftest (build/qemu_test.log)."""
    return os.path.join(os.path.dirname(__file__), "..", "build", "qemu_test.log")


def _serial_log_offset():
    """Size of the QEMU serial log right now, or None if it cannot be read.

    Callers must bind this to --qemu rather than to the file existing: conftest opens (and
    truncates) the log only when it starts QEMU itself, so under `--ip` a leftover from an
    earlier `make qemu-test` stats fine and never grows — an offset into it reads back as an
    empty tail and would be scored as "the firmware logged nothing". Same trap as in
    33_test_auth_settings.py.
    """
    try:
        return os.path.getsize(_qemu_serial_log_path())
    except OSError:
        return None


# ESP-IDF log lines carry (ms since boot) under CONFIG_LOG_TIMESTAMP_SOURCE_RTOS, which
# sdkconfig.qemu.minimal sets. Anchored on the "ota_handler:" tag because json_utils logs a
# "Network timeout during upload" line of its own for this same failure. The handler prints its
# own limit as the denominator of "Network timeout 1/6", which is what lets the caller pin the
# firmware constants instead of inferring them from elapsed time.
_STALL_RETRY_RE = re.compile(r"\((\d+)\) ota_handler: Network timeout (\d+)/(\d+)")
_STALL_GIVEUP_RE = re.compile(r"\((\d+)\) ota_handler: OTA upload stalled:")


def _stall_serial_evidence(offset, read_timeout=10.0):
    """Guest timestamps of the stall, from serial written since `offset`.

    Returns (retries, giveup_ms), where `retries` is [(timestamp_ms, index, limit), ...] in log
    order — empty, and giveup_ms None, when those lines never showed up. Every retry is kept,
    not just the first: when the delta comes out wrong, the SPACING of these lines is what says
    why. A changed receive window makes every interval land on the same wrong whole second;
    emulator drift leaves them scattered around the configured window.

    Returns None instead of a pair when the log could not be read or shrank under us — that
    says nothing about the firmware and must not collapse into "the line is missing", which is
    what the caller's assertions accuse it of.

    Retried rather than read once, for the same reason 14_test_reboot.py retries: QEMU writes
    this log from a separate process and the line can lag the device answering HTTP again.
    """
    deadline = time.monotonic() + read_timeout
    readable = False
    retries, giveup_ms = [], None
    while True:
        try:
            if os.path.getsize(_qemu_serial_log_path()) < offset:
                return None  # truncated under us — the offset no longer means anything
            with open(_qemu_serial_log_path(), "r", errors="replace") as fh:
                fh.seek(offset)
                tail = fh.read()
            readable = True
            retries = [(int(m.group(1)), int(m.group(2)), int(m.group(3)))
                       for m in _STALL_RETRY_RE.finditer(tail)]
            giveup = _STALL_GIVEUP_RE.search(tail)
            giveup_ms = int(giveup.group(1)) if giveup else None
            if retries and giveup_ms is not None:
                return retries, giveup_ms
        except OSError:
            pass
        if time.monotonic() >= deadline:
            return (retries, giveup_ms) if readable else None
        time.sleep(0.5)


@pytest.fixture(scope="module")
def firmware_bytes():
    if not QEMU_FIRMWARE.is_file():
        pytest.skip(f"QEMU firmware fixture missing: {QEMU_FIRMWARE} — run `make qemu-build`")
    data = QEMU_FIRMWARE.read_bytes()
    magic = struct.unpack_from("<I", data, WB_APP_DESC_OFFSET)[0]
    assert magic == WB_APP_DESC_MAGIC_WORD, (
        f"Unexpected magic word in {QEMU_FIRMWARE}: 0x{magic:08X}. "
        f"Layout constants may be out of date with main/wb_app_desc/wb_app_desc.h"
    )
    return data


def _post_update(api, body, content_type="application/octet-stream", timeout=60):
    headers = {"Content-Type": content_type} if content_type is not None else {}
    return api.session.post(
        f"{api.base_url}/update",
        data=body,
        headers=headers,
        timeout=timeout,
    )


def test_ota_requires_auth(api, firmware_bytes):
    """POST /update without a valid session → 401."""
    bare = requests.Session()
    bare.headers.update({"Connection": "close", "Accept-Encoding": "identity"})
    resp = bare.post(
        f"{api.base_url}/update",
        data=firmware_bytes[:256],
        headers={"Content-Type": "application/octet-stream"},
        timeout=30,
    )
    assert resp.status_code == 401, f"Expected 401, got {resp.status_code}: {resp.text!r}"


def test_ota_wrong_content_type(api, firmware_bytes):
    """Wrong Content-Type → exactly one HTTP 400 response on the raw TCP connection.

    Uses a raw socket to count how many HTTP responses the server sends.
    With the bug (ota_validate_content_type returns ESP_OK instead of ESP_FAIL):
      1. Server sends response #1 (400 — "Invalid content type")
      2. OTA handler continues, tries to receive/write 256 bytes, fails
      3. Server sends response #2 (400 — "Network timeout during upload")
    After the fix, the handler returns ESP_FAIL immediately and only one
    response is sent.  Counting b"HTTP/1.1" occurrences in the raw bytes
    detects the double-response reliably, regardless of connection pooling.
    """
    parsed = requests.utils.urlparse(api.base_url)
    host = parsed.hostname or "localhost"
    port = parsed.port or 80
    cookie_header = "; ".join(f"{k}={v}" for k, v in api.session.cookies.items())

    body_bytes = firmware_bytes[:256]
    request = (
        f"POST /update HTTP/1.1\r\n"
        f"Host: {host}:{port}\r\n"
        f"Cookie: {cookie_header}\r\n"
        f"Content-Type: text/plain\r\n"
        f"Content-Length: {len(body_bytes)}\r\n"
        f"Connection: close\r\n"
        f"\r\n"
    ).encode() + body_bytes

    sock = socket.create_connection((host, port), timeout=30)
    try:
        sock.sendall(request)
        sock.shutdown(socket.SHUT_WR)
        # Collect all bytes until the server closes the connection (EOF).
        # A 15-second timeout guards against the server stalling indefinitely.
        sock.settimeout(15)
        raw_response = b""
        try:
            while True:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                raw_response += chunk
        except socket.timeout:
            pass
    finally:
        sock.close()

    # Exactly one HTTP response must be present. Split on the header/body boundary
    # and count blocks whose first line starts with "HTTP/" — this is immune to
    # "HTTP/1.1" appearing in a response body or a future protocol version change.
    header_blocks = raw_response.split(b"\r\n\r\n")
    http_response_count = sum(1 for block in header_blocks if block.lstrip().startswith(b"HTTP/"))
    assert http_response_count == 1, (
        f"Expected exactly 1 HTTP response, got {http_response_count}. "
        f"Double-response bug detected. Raw response:\n{raw_response!r}"
    )
    # The single response must be a 400.
    first_status_line = raw_response.split(b"\r\n")[0]
    assert b"400" in first_status_line, (
        f"Expected HTTP 400, got: {first_status_line!r}"
    )
    # The body must mention the content-type error.
    assert b"content type" in raw_response.lower(), (
        f"Expected 'content type' in response body. Raw response:\n{raw_response!r}"
    )


def test_ota_macbinary_content_type_accepted(api, firmware_bytes):
    """Content-Type 'application/macbinary' must NOT be rejected as unsupported.

    Regression for ota_handler.c ota_is_valid_content_type(), which accepts both
    'application/octet-stream' and 'application/macbinary' (macOS browsers may
    send the latter when downloading a .bin firmware blob). A regression that
    dropped macbinary support would reject the upload with the "Invalid content
    type" error.

    To avoid actually flashing, we send a body shorter than the wb_app_desc head.
    With macbinary accepted, the request progresses PAST the content-type check
    and is instead aborted later for the short body — so the response error must
    be the short-body/upload error, NOT "Invalid content type". This proves the
    content-type gate let macbinary through.
    """
    short = firmware_bytes[: MIN_VALID_HEAD_LEN - 1]
    resp = _post_update(api, short, content_type="application/macbinary")
    assert resp.status_code == 400, f"Expected 400, got {resp.status_code}: {resp.text!r}"
    body = resp.json()
    assert body.get("success") is False
    # The defining assertion: macbinary was accepted, so the failure reason is
    # NOT the content-type rejection.
    error = body.get("error", "").lower()
    assert "content type" not in error, (
        f"macbinary Content-Type was wrongly rejected as unsupported: {body.get('error')!r}"
    )


def test_ota_short_body_no_app_desc(api, firmware_bytes):
    """Body smaller than 480 bytes (WB_APP_DESC_OFFSET + WB_APP_DESC_SIZE) → error.

    main/ota_handler.c requires the first recv chunk to be large enough to
    contain the wb_app_desc struct; otherwise it logs
    'App descriptor not received' and aborts.
    """
    short = firmware_bytes[: MIN_VALID_HEAD_LEN - 1]
    resp = _post_update(api, short)
    assert resp.status_code == 400, f"Expected 400, got {resp.status_code}: {resp.text!r}"
    body = resp.json()
    assert body.get("success") is False


def test_ota_bad_magic_word(api, firmware_bytes):
    """Magic word mismatch → 'Invalid OTA firmware'."""
    payload = bytearray(firmware_bytes[: MIN_VALID_HEAD_LEN + 1024])
    struct.pack_into("<I", payload, WB_APP_DESC_OFFSET, 0xDEADBEEF)
    resp = _post_update(api, bytes(payload))
    assert resp.status_code == 400, f"Expected 400, got {resp.status_code}: {resp.text!r}"
    body = resp.json()
    assert body.get("success") is False
    assert "invalid ota firmware" in body.get("error", "").lower(), (
        f"Unexpected error message: {body.get('error')!r}"
    )


def test_ota_bad_signature(api, firmware_bytes):
    """Signature mismatch → 'Invalid OTA firmware'."""
    payload = bytearray(firmware_bytes[: MIN_VALID_HEAD_LEN + 1024])
    fake_signature = b"hacker_v9\x00\x00\x00"
    assert len(fake_signature) == SIGNATURE_LEN
    struct.pack_into(f"{SIGNATURE_LEN}s", payload, SIGNATURE_OFFSET, fake_signature)
    resp = _post_update(api, bytes(payload))
    assert resp.status_code == 400, f"Expected 400, got {resp.status_code}: {resp.text!r}"
    body = resp.json()
    assert body.get("success") is False
    assert "invalid ota firmware" in body.get("error", "").lower(), (
        f"Unexpected error message: {body.get('error')!r}"
    )


def test_ota_truncated_stream(api, firmware_bytes):
    """Promise more bytes via Content-Length than we send → server must
    abort cleanly and stay responsive.

    Uses a raw socket because requests always sends Content-Length matching
    the actual body length. esp_ota_abort should release the partition; no
    boot-partition switch happens, so /info must still work.
    """
    parsed = requests.utils.urlparse(api.base_url)
    host = parsed.hostname or "localhost"
    port = parsed.port or 80
    cookie_header = "; ".join(f"{k}={v}" for k, v in api.session.cookies.items())
    promised_length = max(len(firmware_bytes), 1_000_000)
    sent_chunk = firmware_bytes[: MIN_VALID_HEAD_LEN + 4096]
    request = (
        f"POST /update HTTP/1.1\r\n"
        f"Host: {host}:{port}\r\n"
        f"Cookie: {cookie_header}\r\n"
        f"Content-Type: application/octet-stream\r\n"
        f"Content-Length: {promised_length}\r\n"
        f"Connection: close\r\n"
        f"\r\n"
    ).encode() + sent_chunk

    sock = socket.create_connection((host, port), timeout=30)
    try:
        sock.sendall(request)
        sock.shutdown(socket.SHUT_WR)
        sock.settimeout(10)
        try:
            while True:
                chunk = sock.recv(4096)
                if not chunk:
                    break
        except socket.timeout:
            pass
    finally:
        sock.close()

    api.reconnect()
    api.auth()
    resp = api.get_info()
    assert resp.status_code == 200, (
        f"Server unresponsive after truncated OTA: {resp.status_code} {resp.text!r}"
    )


# 480 s, over the 180 s default in pytest.ini. The timeouts this item serialises, all of them
# per-phase because api_client sends Connection: close (see STALL_INFO_TIMEOUT):
#   30 s   socket.create_connection to start the stalled upload
#   175 s  GET /info — STALL_INFO_TIMEOUT, 5 s connect + 170 s read
#   10 s   the serial scan (_stall_serial_evidence's read_timeout)
#   15 s   draining the stalled OTA socket
#   20 s   the final api.get_info() — a SCALAR 10 in api_client, so 10 connect + 10 read
#   = 250 s, plus ~4 s for conftest's function-scoped _uart_leak_guard teardown (2 ports x 2 s).
# conftest's module-scoped rs485 restore is NOT in this budget — it tears down after the LAST item
# of the file, test_ota_full_update.
@pytest.mark.timeout(480)
def test_ota_client_vanishes_without_closing_socket(api, firmware_bytes, pytestconfig):
    """Client disappears mid-upload WITHOUT closing the socket → the device must
    give up on its own and keep serving HTTP.

    Unlike test_ota_truncated_stream, nothing tells the device the client is gone:
    no FIN, no RST, TCP keep-alive is off. The socket just goes quiet, so
    httpd_req_recv() only ever reports timeouts. Without the stall limit in
    ota_receive_and_write() (main/ota_handler.c) the handler retries forever, and
    since the IDF web server is single-threaded, every other endpoint dies with it
    until the device is power-cycled.

    /info answering at all is what proves the web interface came back. HOW LONG the
    firmware waited before giving up is asserted on the guest's own log timestamps, not
    on host wall clock — the host figure carries emulator slowness and two HTTP round
    trips, and a bound on it accuses the firmware for a loaded node. A device that never
    recovers is caught by the /info request timeout, not by any bound here.
    """
    parsed = requests.utils.urlparse(api.base_url)
    host = parsed.hostname or "localhost"
    port = parsed.port or 80
    cookie_header = "; ".join(f"{k}={v}" for k, v in api.session.cookies.items())
    promised_length = max(len(firmware_bytes), 1_000_000)
    sent_chunk = firmware_bytes[: MIN_VALID_HEAD_LEN + 4096]
    request = (
        f"POST /update HTTP/1.1\r\n"
        f"Host: {host}:{port}\r\n"
        f"Cookie: {cookie_header}\r\n"
        f"Content-Type: application/octet-stream\r\n"
        f"Content-Length: {promised_length}\r\n"
        f"Connection: close\r\n"
        f"\r\n"
    ).encode() + sent_chunk

    # Snapshot before the POST so the scan below sees only serial from THIS stall; a scan from 0
    # would match the markers of an earlier one and pass unconditionally. Bound to --qemu, not to
    # the log being statable — see _serial_log_offset.
    serial_offset = _serial_log_offset() if pytestconfig.getoption("--qemu") else None

    sock = socket.create_connection((host, port), timeout=30)
    try:
        sock.sendall(request)
        # From here on the client is "gone": the socket stays open and silent.
        stall_start = time.monotonic()

        # A separate connection, because the stalled one is the one under test. The
        # request is queued behind the OTA handler and can only be served once that
        # handler gives up, so it returning at all is the recovery. A firmware that never gives
        # up cannot answer and surfaces as the ReadTimeout in STALL_INFO_TIMEOUT, not as a bound.
        info_resp = api.session.get(f"{api.base_url}/info", timeout=STALL_INFO_TIMEOUT)
        info_elapsed = time.monotonic() - stall_start

        assert info_resp.status_code == 200, (
            f"Server unresponsive while an OTA client is silent: "
            f"{info_resp.status_code} {info_resp.text!r}"
        )

        # --- The product property, measured by the guest on the guest's clock ---------------
        # What the handler enforces is exactly `limit` receive windows of silence; the configured
        # OTA_RECV_STALL_TIMEOUT_MS enters only through that integer division, so a change to it
        # that leaves the limit at OTA_RECV_MAX_STALL_TIMEOUTS (anything in 30000..34999 at the
        # current window) changes no behaviour and is correctly not flagged. Pinning the two
        # factors therefore pins the behaviour: the limit comes off the log line itself, and the
        # window length from the delta between the first retry and the give-up.
        guest_check_passed = False
        evidence = _stall_serial_evidence(serial_offset) if serial_offset is not None else None
        if evidence is None:
            warnings.warn(
                "test_ota_client_vanishes_without_closing_socket: no QEMU serial to read (no "
                "--qemu, or build/qemu_test.log became unreadable), so the guest-time check on "
                f"the {OTA_RECV_STALL_TIMEOUT_MS} ms stall budget was skipped. Only the host-side "
                "bounds below ran, and those cannot tell a changed budget from a slow node.",
                stacklevel=1,
            )
        else:
            retries, giveup_ms = evidence
            assert retries, (
                f"/info came back after {info_elapsed:.1f}s but the guest never logged 'Network "
                f"timeout 1/{OTA_RECV_MAX_STALL_TIMEOUTS}': ota_receive_and_write() never entered "
                f"its retry loop, so the upload was refused outright instead of waited out and "
                f"nothing here exercised the stall limit"
            )
            assert giveup_ms is not None, (
                f"the guest retried the stalled receive but never logged 'OTA upload stalled', "
                f"yet /info was served after {info_elapsed:.1f}s — the handler left its receive "
                f"loop by some route other than the stall limit"
            )
            first_retry_ms, _, observed_limit = retries[0]
            enforced_ms = giveup_ms - first_retry_ms
            # The INTERVALS, not the absolute stamps: the intervals are the discriminator, and
            # making the reader subtract hides it. One per gap, ending at the give-up line.
            stamps = [ts for ts, _idx, _lim in retries] + [giveup_ms]
            interval_dump = ", ".join(str(b - a) for a, b in zip(stamps, stamps[1:]))
            # The window the guest actually used, so the message below can quote the silence the
            # device now tolerates rather than only the limit — that is what separates a real
            # change (W=4 rounds the budget down to 28 s) from a re-parameterisation that lands on
            # the same 30 s. Guarded because the delta spans limit-1 windows.
            observed_silence_s = (
                observed_limit * enforced_ms / (observed_limit - 1) / 1000
                if observed_limit > 1 else float("nan")
            )

            assert observed_limit == OTA_RECV_MAX_STALL_TIMEOUTS, (
                f"the handler counted up to {observed_limit} consecutive timeouts, not "
                f"{OTA_RECV_MAX_STALL_TIMEOUTS}: OTA_RECV_STALL_TIMEOUT_MS "
                f"(main/ota_handler.c) or HTTP_RECV_WAIT_TIMEOUT_S (main/http_server.h) changed "
                f"and the mirrors at the top of this file are stale. The device now tolerates "
                f"{observed_limit} windows of silence, {observed_silence_s:.1f}s in total against "
                f"the {OTA_RECV_STALL_TIMEOUT_MS / 1000:.1f}s mirrored here — update the mirrors "
                f"if that was intended. Intervals between the guest's own log lines, first retry "
                f"@{first_retry_ms}ms then one per gap to the give-up: {interval_dump} ms"
            )
            # From the OBSERVED limit — the assert above has already pinned it to the mirror, so
            # this is the same number, written this way to keep the two claims separate: that one
            # is about the limit, this one is about the windows really being that long.
            expected_ms = (observed_limit - 1) * HTTP_RECV_WAIT_TIMEOUT_S * 1000
            assert abs(enforced_ms - expected_ms) <= STALL_GUEST_TOLERANCE_MS, (
                f"the guest spent {enforced_ms} ms between its first retry and giving up; "
                f"expected {expected_ms} ± {STALL_GUEST_TOLERANCE_MS} ms, i.e. the "
                f"{observed_limit - 1} windows of HTTP_RECV_WAIT_TIMEOUT_S="
                f"{HTTP_RECV_WAIT_TIMEOUT_S}s (main/http_server.h) that separate those two lines "
                f"at OTA_RECV_STALL_TIMEOUT_MS={OTA_RECV_STALL_TIMEOUT_MS} "
                f"(main/ota_handler.c). Either the receive window changed or the emulator "
                f"drifted — both accumulate over the windows, so the intervals are what separate "
                f"them: a changed window puts every one of them on the same wrong whole second, "
                f"drift scatters them around {HTTP_RECV_WAIT_TIMEOUT_S * 1000}. First retry "
                f"@{first_retry_ms}ms, then one per gap to the give-up: {interval_dump} ms"
            )
            guest_check_passed = True
            print(f"✓ Guest enforced {observed_limit} windows / {enforced_ms} ms of stall budget "
                  f"(expected {expected_ms} ± {STALL_GUEST_TOLERANCE_MS} ms)")

        # --- Host-side bounds: one on the environment, one on the device answering too fast ---
        env_note = (
            f"GET /info took {info_elapsed:.1f}s to come back after the OTA client went silent, "
            f"over the {STALL_RECOVERY_ENV_BUDGET_S}s environment budget. This is host wall clock "
            f"— it carries the emulator's own slowness and two HTTP round trips on top of the "
            f"~35 s the guest needs — so it is a statement about this node, not about the "
            f"firmware, which did abort the upload and answer. Check what else loaded the host"
        )
        if guest_check_passed:
            # Warn, do not fail: on host wall clock this bound cannot tell a slow node from a slow
            # device, and accusing the firmware for the node is what this test was rewritten to
            # stop doing. Note what that trade actually costs. The guest evidence closes the WAIT
            # LOOP only — the limit and the window. It says nothing about the segment from
            # "OTA upload stalled" to /info being served: leaving ota_receive_and_write(),
            # esp_ota_abort(), answering the stalled connection, freeing the single-threaded
            # server. That segment is exactly what info_elapsed covered beyond the guest budget,
            # and it is now bounded only by STALL_INFO_TIMEOUT's read timeout — a regression that
            # added 100 s there would warn, not fail. Deliberate, and the warning is the trail.
            if info_elapsed >= STALL_RECOVERY_ENV_BUDGET_S:
                warnings.warn(
                    f"test_ota_client_vanishes_without_closing_socket: {env_note}",
                    stacklevel=1,
                )
        else:
            # No guest evidence: this bound is all that separates "recovered, slowly" from "this
            # node is broken", so without it the only remaining check is that /info answered.
            assert info_elapsed < STALL_RECOVERY_ENV_BUDGET_S, env_note

        assert info_elapsed >= STALL_RECOVERY_MIN_S, (
            f"GET /info was served after only {info_elapsed:.1f}s: the device did not sit through the "
            f"stall timeout at all, so this test proves nothing about the receive loop. Either the "
            f"upload was refused outright, or the request was not queued behind the OTA handler"
        )

        # The stalled upload itself must be over too: either an error response
        # arrived, or the device closed the connection.
        sock.settimeout(15)
        raw_response = b""
        try:
            while True:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                raw_response += chunk
        except socket.timeout:
            pass
        assert raw_response == b"" or raw_response.lstrip().startswith(b"HTTP/"), (
            f"Unexpected data on the stalled OTA connection: {raw_response[:200]!r}"
        )
    finally:
        sock.close()

    resp = api.get_info()
    assert resp.status_code == 200, (
        f"Server unresponsive after the stalled OTA was aborted: "
        f"{resp.status_code} {resp.text!r}"
    )


# --- Positive path: MUST stay last — reboots into ota_1 ----------------------

@pytest.mark.timeout(2400)
def test_ota_full_update(api, firmware_bytes):
    """Upload the full QEMU firmware → 200 with bytes_written == size →
    device reboots → /info responds again.
    Also verifies that NVS settings (hostname, vout, rs485_1 baudrate and term)
    are preserved across the OTA reboot.
    """
    # Step 1: Read original settings as a baseline for restore in finally block
    orig_resp = api.get_settings()
    assert orig_resp.status_code == 200, (
        f"Failed to read settings before OTA: {orig_resp.status_code} {orig_resp.text!r}"
    )
    original_settings = orig_resp.json()
    print(f"  Original settings read: hostname={original_settings.get('hostname')!r}, "
          f"vout={original_settings.get('vout')}, "
          f"rs485_1.baudrate={original_settings.get('rs485_1', {}).get('baudrate')}, "
          f"rs485_1.term={original_settings.get('rs485_1', {}).get('term')}")

    try:
        # Step 2: Compute test settings — only network-safe fields to keep the device reachable
        new_vout = not original_settings["vout"]
        current_baudrate = original_settings["rs485_1"]["baudrate"]
        new_baudrate = next(b for b in _BAUDRATES if b != current_baudrate)
        new_term = not original_settings["rs485_1"]["term"]

        test_settings = {
            "hostname": "ota-persist-test",
            "vout": new_vout,
            "rs485_1": {
                **original_settings["rs485_1"],
                "baudrate": new_baudrate,
                "term": new_term,
            },
        }
        print(f"  Test settings to write: hostname='ota-persist-test', vout={new_vout}, "
              f"rs485_1.baudrate={new_baudrate}, rs485_1.term={new_term}")

        # Step 3: Write test settings
        set_resp = api.update_settings(test_settings)
        assert set_resp.status_code == 200, (
            f"Failed to write test settings: {set_resp.status_code} {set_resp.text!r}"
        )
        set_body = set_resp.json()
        assert set_body.get("success") is True, (
            f"POST /settings reported failure: {set_body}"
        )
        print("  ✓ Test settings written")

        # Step 4: Read back and verify all four fields were accepted
        rb_resp = api.get_settings()
        assert rb_resp.status_code == 200, (
            f"Failed to read back settings: {rb_resp.status_code} {rb_resp.text!r}"
        )
        rb = rb_resp.json()
        assert rb.get("hostname") == "ota-persist-test", (
            f"Read-back hostname mismatch: expected 'ota-persist-test', got {rb.get('hostname')!r}"
        )
        assert rb.get("vout") == new_vout, (
            f"Read-back vout mismatch: expected {new_vout}, got {rb.get('vout')}"
        )
        assert rb.get("rs485_1", {}).get("baudrate") == new_baudrate, (
            f"Read-back rs485_1.baudrate mismatch: expected {new_baudrate}, "
            f"got {rb.get('rs485_1', {}).get('baudrate')}"
        )
        assert rb.get("rs485_1", {}).get("term") == new_term, (
            f"Read-back rs485_1.term mismatch: expected {new_term}, "
            f"got {rb.get('rs485_1', {}).get('term')}"
        )
        print("  ✓ Read-back verification passed")

        # Step 5: Perform OTA firmware upload
        fw_size = len(firmware_bytes)
        print(f"  Uploading {fw_size} bytes to /update...")
        resp = _post_update(api, firmware_bytes, timeout=180)
        assert resp.status_code == 200, f"Expected 200, got {resp.status_code}: {resp.text!r}"
        body = resp.json()
        assert body.get("success") is True, f"OTA reported failure: {body}"
        assert body.get("bytes_written") == fw_size, (
            f"Expected bytes_written={fw_size}, got {body.get('bytes_written')}"
        )
        print(f"✓ Firmware accepted ({fw_size} bytes), device will reboot shortly")

        # Step 5b: a second upload before the reboot must NOT be accepted — it would erase the
        # image that was just written and the update would silently not happen. Best effort by
        # design: the reboot is scheduled 100 ms after the response in the QEMU build
        # (REBOOT_DELAY_MS in main/cmd_handler.c), so the connection may simply be gone by now.
        # Anything except a second "success" passes; the deterministic check lives in
        # unittests/ota_handler/.
        try:
            second = _post_update(api, firmware_bytes[: MIN_VALID_HEAD_LEN + 4096], timeout=30)
        except requests.exceptions.RequestException as exc:
            print(f"  Second POST /update did not complete ({exc.__class__.__name__}) — "
                  "the device was already rebooting")
        else:
            assert second.status_code != 200, (
                "A second POST /update before the reboot was accepted: it erased the firmware that "
                f"had just been written. Response: {second.text!r}"
            )
            print(f"  ✓ Second POST /update refused with {second.status_code}")

        # Step 6: Wait for device to come back online after reboot
        # Bumped from 90s → 180s: under host CPU contention, QEMU boot can take longer
        # than 90 s even though the firmware itself reboots in <10 s on real hardware.
        try:
            api.wait_for_ready(timeout=1800)
        except TimeoutError:
            pytest.fail("Device did not come back online within 1800s after OTA reboot")
        print("✓ Device back online after OTA reboot")

        # Step 7: Verify /info is responsive on the new firmware
        info = api.get_info()
        assert info.status_code == 200, (
            f"Device unhealthy after OTA reboot: {info.status_code} {info.text!r}"
        )
        print("✓ /info responsive on the post-OTA firmware")

        # Step 8: Read settings after reboot
        post_ota_resp = api.get_settings()
        assert post_ota_resp.status_code == 200, (
            f"Failed to read settings after OTA reboot: "
            f"{post_ota_resp.status_code} {post_ota_resp.text!r}"
        )
        post_ota_settings = post_ota_resp.json()
        print(f"  Post-OTA settings: hostname={post_ota_settings.get('hostname')!r}, "
              f"vout={post_ota_settings.get('vout')}, "
              f"rs485_1.baudrate={post_ota_settings.get('rs485_1', {}).get('baudrate')}, "
              f"rs485_1.term={post_ota_settings.get('rs485_1', {}).get('term')}")

        # Step 9: Assert all four fields survived the OTA reboot (NVS persistence check)
        assert post_ota_settings.get("hostname") == "ota-persist-test", (
            f"NVS persistence failed for hostname: "
            f"expected 'ota-persist-test', got {post_ota_settings.get('hostname')!r}"
        )
        assert post_ota_settings.get("vout") == new_vout, (
            f"NVS persistence failed for vout: "
            f"expected {new_vout}, got {post_ota_settings.get('vout')}"
        )
        assert post_ota_settings.get("rs485_1", {}).get("baudrate") == new_baudrate, (
            f"NVS persistence failed for rs485_1.baudrate: "
            f"expected {new_baudrate}, got {post_ota_settings.get('rs485_1', {}).get('baudrate')}"
        )
        assert post_ota_settings.get("rs485_1", {}).get("term") == new_term, (
            f"NVS persistence failed for rs485_1.term: "
            f"expected {new_term}, got {post_ota_settings.get('rs485_1', {}).get('term')}"
        )
        print("✓ All settings persisted across OTA reboot")

    finally:
        # Restore original settings regardless of test outcome — no asserts here
        try:
            restore_resp = api.update_settings(original_settings)
            print(f"  Settings restored (status={restore_resp.status_code})")
        except Exception as exc:  # noqa: BLE001
            print(f"  WARNING: Failed to restore original settings: {exc}")
