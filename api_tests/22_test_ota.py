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

import socket
import struct
from pathlib import Path

import pytest
import requests

PROJECT_ROOT = Path(__file__).parent.parent
QEMU_FIRMWARE = PROJECT_ROOT / "build" / "qemu_mge.bin"

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


def test_ota_short_body_no_app_desc(api, firmware_bytes, is_qemu):
    """Body smaller than 480 bytes (WB_APP_DESC_OFFSET + WB_APP_DESC_SIZE) → error.

    main/ota_handler.c requires the first recv chunk to be large enough to
    contain the wb_app_desc struct; otherwise it logs
    'App descriptor not received' and aborts.
    """
    if is_qemu:
        pytest.skip("QEMU uses single-app partition table — no OTA partition available")
    short = firmware_bytes[: MIN_VALID_HEAD_LEN - 1]
    resp = _post_update(api, short)
    assert resp.status_code == 400, f"Expected 400, got {resp.status_code}: {resp.text!r}"
    body = resp.json()
    assert body.get("success") is False


def test_ota_bad_magic_word(api, firmware_bytes, is_qemu):
    """Magic word mismatch → 'Invalid OTA firmware'."""
    if is_qemu:
        pytest.skip("QEMU uses single-app partition table — no OTA partition available")
    payload = bytearray(firmware_bytes[: MIN_VALID_HEAD_LEN + 1024])
    struct.pack_into("<I", payload, WB_APP_DESC_OFFSET, 0xDEADBEEF)
    resp = _post_update(api, bytes(payload))
    assert resp.status_code == 400, f"Expected 400, got {resp.status_code}: {resp.text!r}"
    body = resp.json()
    assert body.get("success") is False
    assert "invalid ota firmware" in body.get("error", "").lower(), (
        f"Unexpected error message: {body.get('error')!r}"
    )


def test_ota_bad_signature(api, firmware_bytes, is_qemu):
    """Signature mismatch → 'Invalid OTA firmware'."""
    if is_qemu:
        pytest.skip("QEMU uses single-app partition table — no OTA partition available")
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


def test_ota_truncated_stream(api, firmware_bytes, is_qemu):
    """Promise more bytes via Content-Length than we send → server must
    abort cleanly and stay responsive.

    Uses a raw socket because requests always sends Content-Length matching
    the actual body length. esp_ota_abort should release the partition; no
    boot-partition switch happens, so /info must still work.
    """
    if is_qemu:
        pytest.skip("QEMU uses single-app partition table — no OTA partition available")
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


# --- Positive path: MUST stay last — reboots into ota_1 ----------------------

@pytest.mark.timeout(240)
def test_ota_full_update(api, firmware_bytes, is_qemu):
    """Upload the full QEMU firmware → 200 with bytes_written == size →
    device reboots → /info responds again.
    """
    if is_qemu:
        pytest.skip("QEMU uses single-app partition table — no OTA partition available")
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

    try:
        api.wait_for_ready(timeout=90)
    except TimeoutError:
        pytest.fail("Device did not come back online within 90s after OTA reboot")
    print("✓ Device back online after OTA reboot")

    info = api.get_info()
    assert info.status_code == 200, (
        f"Device unhealthy after OTA reboot: {info.status_code} {info.text!r}"
    )
    print("✓ /info responsive on the post-OTA firmware")
