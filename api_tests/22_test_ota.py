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
