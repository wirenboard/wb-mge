"""End-to-end tests for the gateway's OWN Modbus unit (Unit ID 255 / 0xFF).

The firmware intercepts Modbus requests addressed to its own unit ID (0xFF) and
answers them locally from the device-info / statistics register map implemented
in main/bridge/mb_device.c — WITHOUT forwarding them to the RS-485 bus. This is
served in BOTH operating modes:

  (A) Modbus TCP gateway mode  (modbus_tcp.c, tcp_bridge + modbus=true).
  (B) Cache TCP server mode    (cache_modbus_server.c) — even with an empty or
      disabled cache, because the self-unit interception happens BEFORE the
      cache-enabled guard.

Requires QEMU launched with:
  - UART1 exposed as TCP port 5561 (for the gateway RTU slave).
  - guest port 502  forwarded to host port 50502 (gateway).
  - guest port 50504 forwarded to host port 50504 (cache Modbus server).

Register map (unit 0xFF), confirmed against main/bridge/mb_device.c:
  FC04 input:  104-105 uptime_s (u32 MSW-first); 121 supply mV;
               200-219 model string; 220-244 git info; 250-265 fw version;
               266-269 serial ext u64 MSW-first; 270-271 serial low u32 MSW-first;
               320 MAJOR, 321 MINOR, 322 PATCH, 323 SUFFIX(s16);
               324-325 numeric version LE word order (324=low);
               326-327 numeric version BE word order (326=high);
               65504..65508 RAM/stack/reboot diagnostics;
               336 cache timeout s; 337-338 packets u32; 339-340 last-pkt-age u32;
               341 devices_on_bus; 342 bus poll ppm.
  FC03 holding: 290-301 signature string. Other holding addrs -> exception 0x02.
  Any fc not in {0x03,0x04} on unit 0xFF -> exception 0x01.
"""

import socket
import struct
import time
from urllib.parse import urlparse

import pytest

from api_client import WBMGEAPI
from conftest import build_gateway_fixture, _poll_tcp_connect
from modbus_helpers import make_mbap_request, send_and_receive


# --------------------------------------------------------------------------- #
# Module-level constants                                                       #
# --------------------------------------------------------------------------- #

# The gateway's own Modbus unit ID (MB_DEVICE_UNIT_ID).
SELF_UNIT_ID = 0xFF

# Gateway port forwarded from QEMU guest port 502 to host port 50502.
GATEWAY_HOST_PORT = 50502
# UART1 chardev TCP socket (QEMU -serial tcp::5561,server,nowait).
UART1_TCP_PORT = 5561
# Fake register value returned by the RTU slave for any register read.
FAKE_VALUE = 0x1234

# QEMU host-forwarded port for the cache Modbus TCP server (guest port 50504).
QEMU_CACHE_MODBUS_PORT = 50504

CONNECT_TIMEOUT = 5.0


# --------------------------------------------------------------------------- #
# Module-level helpers                                                         #
# --------------------------------------------------------------------------- #

def read_self_regs(host, port, fc, start, count, timeout=5.0):
    """Read N registers from host:port at unit 0xFF, opening a fresh socket.

    Returns (tid, unit_id, resp_fc, payload). For FC03/FC04 the payload is
    [byte_count][reg_hi][reg_lo]...; for an exception resp_fc has the 0x80 bit
    set and payload[0] is the exception code.
    """
    req = make_mbap_request(0x0042, SELF_UNIT_ID, fc, start, count)
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    try:
        sock.connect((host, port))
        tid, unit_id, resp_fc, payload = send_and_receive(sock, req)
    finally:
        sock.close()
    return tid, unit_id, resp_fc, payload


def decode_reg_string(values):
    """Decode a packed register string: each 16-bit reg -> [hi, lo] bytes,
    big-endian; concatenate, strip trailing NULs, decode latin-1.
    """
    raw = bytearray()
    for v in values:
        raw.append((v >> 8) & 0xFF)
        raw.append(v & 0xFF)
    return raw.rstrip(b"\x00").decode("latin-1")


def regs_from_payload(payload):
    """Decode an FC03/FC04 success payload into a list of 16-bit register values."""
    assert len(payload) >= 1, f"register payload too short: {payload.hex()}"
    byte_count = payload[0]
    assert len(payload) >= 1 + byte_count, (
        f"register payload truncated: expected {1 + byte_count}, got {len(payload)}"
    )
    return [
        struct.unpack(">H", payload[1 + i * 2: 1 + i * 2 + 2])[0]
        for i in range(byte_count // 2)
    ]


def expected_numeric_version(firmware):
    """Recompute the WB numeric firmware version from the version string.

    Mirrors parse_fw_version() in main/bridge/mb_device.c:
      - parse leading MAJOR.MINOR.PATCH (sscanf "%u.%u.%u" semantics)
      - +wbN -> suffix=+N ; -rcN -> suffix=-N ; otherwise suffix=0
      - enc = suffix + 128 if suffix >= 0 else -1 - suffix
      - version = (MAJOR<<24)|(MINOR<<16)|(PATCH<<8)|enc  (each field & 0xFF)
    Returns (major, minor, patch, suffix, version).
    """
    # sscanf "%u.%u.%u" parses leading unsigned ints separated by dots and
    # stops at the first non-matching char. Replicate by taking the leading
    # numeric run for each of the three fields.
    parts = firmware.split(".")

    def lead_uint(s):
        digits = ""
        for ch in s:
            if ch.isdigit():
                digits += ch
            else:
                break
        return int(digits) if digits else 0

    major = lead_uint(parts[0]) if len(parts) > 0 else 0
    minor = lead_uint(parts[1]) if len(parts) > 1 else 0
    patch = lead_uint(parts[2]) if len(parts) > 2 else 0

    suffix = 0
    if "+wb" in firmware:
        suffix = lead_uint(firmware.split("+wb", 1)[1])
    elif "-rc" in firmware:
        suffix = -lead_uint(firmware.split("-rc", 1)[1])

    enc = (suffix + 128) if suffix >= 0 else (-1 - suffix)
    version = (((major & 0xFF) << 24) | ((minor & 0xFF) << 16)
               | ((patch & 0xFF) << 8) | (enc & 0xFF))
    return major, minor, patch, suffix, version


# --------------------------------------------------------------------------- #
# Group A — Modbus TCP gateway mode                                            #
# --------------------------------------------------------------------------- #

@pytest.fixture(scope="module", autouse=True)
def _baseline(api):
    resp = api.update_settings({
        "rs485_1": {
            "tx_disabled": False,     # gateway must forward bytes to UART
            "baudrate": 9600,
            "stopbits": "1",
            "parity": "none",
            "databits": "8",
        }
    })
    assert resp.status_code == 200, \
        f"_baseline: update_settings failed: {resp.status_code} {resp.text}"
    # bridge.* and port_mode are set by the `gateway_slave` fixture


# Shared gateway fixture from conftest: tcp_bridge + modbus=true on port 1.
gateway_slave = build_gateway_fixture(
    port_num=1,
    tcp_host_port=GATEWAY_HOST_PORT,
    uart_tcp_port=UART1_TCP_PORT,
    bridge_port=502,
    modbus=True,
    fake_value=FAKE_VALUE,
)


@pytest.mark.qemu
def test_self_unit_responds_uptime(api, gateway_slave):
    """FC04 104..105 at unit 0xFF on the gateway is answered LOCALLY (not forwarded).

    Asserts a valid (non-exception) FC04 response with the echoed unit 0xFF and a
    4-byte (two-register) payload, AND that the RTU slave request_count did NOT
    increase — proving the request was served by mb_device.c, not the RS-485 bus.
    """
    requests_before = gateway_slave.request_count

    tid, unit_id, resp_fc, payload = read_self_regs(
        "127.0.0.1", GATEWAY_HOST_PORT, 0x04, 104, 2
    )

    assert not (resp_fc & 0x80), \
        f"self-unit uptime returned an exception FC=0x{resp_fc:02X}, payload={payload.hex()}"
    assert unit_id == SELF_UNIT_ID, \
        f"echoed unit_id mismatch: expected 0x{SELF_UNIT_ID:02X}, got 0x{unit_id:02X}"
    assert resp_fc == 0x04, f"FC mismatch: expected 0x04, got 0x{resp_fc:02X}"
    assert len(payload) >= 1, f"empty FC04 payload: {payload.hex()}"
    assert payload[0] == 4, f"byte_count mismatch: expected 4, got {payload[0]}"

    regs = regs_from_payload(payload)
    assert len(regs) == 2, f"expected 2 uptime registers, got {regs}"

    # The request must NOT have been forwarded to the RTU slave.
    time.sleep(0.3)  # give any (erroneous) forwarded frame time to arrive
    assert gateway_slave.request_count == requests_before, (
        "self-unit request was forwarded to the RS-485 bus: "
        f"slave request_count went {requests_before} -> {gateway_slave.request_count}"
    )

    uptime_s = (regs[0] << 16) | regs[1]
    print(f"✓ self-unit uptime FC04 104-105: regs={regs} uptime≈{uptime_s}s, "
          f"NOT forwarded (slave_requests stayed {requests_before})")


@pytest.mark.qemu
def test_self_unit_model_matches_info(api, gateway_slave):
    """FC04 200..219 model string at unit 0xFF equals /info device_name."""
    info = api.get_info().json()
    expected = info["device_name"]

    _tid, unit_id, resp_fc, payload = read_self_regs(
        "127.0.0.1", GATEWAY_HOST_PORT, 0x04, 200, 20
    )
    assert not (resp_fc & 0x80), \
        f"model read returned exception FC=0x{resp_fc:02X}, payload={payload.hex()}"
    assert unit_id == SELF_UNIT_ID
    model = decode_reg_string(regs_from_payload(payload))
    assert model == expected, \
        f"model string mismatch: register={model!r}, /info device_name={expected!r}"
    print(f"✓ self-unit model FC04 200-219 == /info device_name: {model!r}")


@pytest.mark.qemu
def test_self_unit_firmware_matches_info(api, gateway_slave):
    """FC04 250..265 firmware-version string at unit 0xFF equals /info firmware."""
    info = api.get_info().json()
    expected = info["firmware"]

    _tid, unit_id, resp_fc, payload = read_self_regs(
        "127.0.0.1", GATEWAY_HOST_PORT, 0x04, 250, 16
    )
    assert not (resp_fc & 0x80), \
        f"firmware read returned exception FC=0x{resp_fc:02X}, payload={payload.hex()}"
    assert unit_id == SELF_UNIT_ID
    fw = decode_reg_string(regs_from_payload(payload))
    assert fw == expected, \
        f"firmware string mismatch: register={fw!r}, /info firmware={expected!r}"
    print(f"✓ self-unit firmware FC04 250-265 == /info firmware: {fw!r}")


@pytest.mark.qemu
def test_self_unit_serial_matches_info(api, gateway_slave):
    """Serial registers at unit 0xFF reconstruct /info serial_num.

    270-271 (u32 MSW-first) == serial_num & 0xFFFFFFFF.
    266-269 (u64 MSW-first) == full serial_num.
    """
    info = api.get_info().json()
    serial_num = info["serial_num"]

    # Low u32, MSW-first across 270..271.
    _tid, unit_id, resp_fc, payload = read_self_regs(
        "127.0.0.1", GATEWAY_HOST_PORT, 0x04, 270, 2
    )
    assert not (resp_fc & 0x80), \
        f"serial low read returned exception FC=0x{resp_fc:02X}, payload={payload.hex()}"
    assert unit_id == SELF_UNIT_ID
    lo_regs = regs_from_payload(payload)
    serial_u32 = (lo_regs[0] << 16) | lo_regs[1]
    assert serial_u32 == (serial_num & 0xFFFFFFFF), (
        f"serial low u32 mismatch: register=0x{serial_u32:08X}, "
        f"expected 0x{serial_num & 0xFFFFFFFF:08X}"
    )

    # Full u64, MSW-first across 266..269.
    _tid, unit_id, resp_fc, payload = read_self_regs(
        "127.0.0.1", GATEWAY_HOST_PORT, 0x04, 266, 4
    )
    assert not (resp_fc & 0x80), \
        f"serial ext read returned exception FC=0x{resp_fc:02X}, payload={payload.hex()}"
    assert unit_id == SELF_UNIT_ID
    ext_regs = regs_from_payload(payload)
    serial_u64 = 0
    for r in ext_regs:
        serial_u64 = (serial_u64 << 16) | r
    assert serial_u64 == serial_num, (
        f"serial u64 mismatch: register={serial_u64}, /info serial_num={serial_num}"
    )
    print(f"✓ self-unit serial: u32=0x{serial_u32:08X} u64={serial_u64} "
          f"matches /info serial_num={serial_num}")


@pytest.mark.qemu
def test_self_unit_fw_numeric_consistent(api, gateway_slave):
    """FC04 320..327 numeric version is consistent with the firmware string.

    320/321/322 == MAJOR/MINOR/PATCH; the LE word order (324=low, 325=high) and
    the BE word order (326=high, 327=low) must both reconstruct to the WB-encoded
    numeric version computed from /info firmware.
    """
    info = api.get_info().json()
    major, minor, patch, suffix, version = expected_numeric_version(info["firmware"])

    _tid, unit_id, resp_fc, payload = read_self_regs(
        "127.0.0.1", GATEWAY_HOST_PORT, 0x04, 320, 8
    )
    assert not (resp_fc & 0x80), \
        f"numeric version read returned exception FC=0x{resp_fc:02X}, payload={payload.hex()}"
    assert unit_id == SELF_UNIT_ID
    regs = regs_from_payload(payload)
    assert len(regs) == 8, f"expected 8 registers (320..327), got {regs}"

    reg_major, reg_minor, reg_patch = regs[0], regs[1], regs[2]
    le_lo, le_hi = regs[4], regs[5]   # 324 low, 325 high
    be_hi, be_lo = regs[6], regs[7]   # 326 high, 327 low

    assert reg_major == major, f"MAJOR mismatch: reg={reg_major}, expected {major}"
    assert reg_minor == minor, f"MINOR mismatch: reg={reg_minor}, expected {minor}"
    assert reg_patch == patch, f"PATCH mismatch: reg={reg_patch}, expected {patch}"

    # Assert each register word at its specific address so a swapped word order
    # is actually caught (reconstructing then comparing would be tautological:
    # the firmware emits mirror-image LE/BE pairs by construction).
    exp_lo = version & 0xFFFF
    exp_hi = (version >> 16) & 0xFFFF
    assert le_lo == exp_lo, f"reg324 (LE low) = 0x{le_lo:04X}, expected 0x{exp_lo:04X}"
    assert le_hi == exp_hi, f"reg325 (LE high) = 0x{le_hi:04X}, expected 0x{exp_hi:04X}"
    assert be_hi == exp_hi, f"reg326 (BE high) = 0x{be_hi:04X}, expected 0x{exp_hi:04X}"
    assert be_lo == exp_lo, f"reg327 (BE low) = 0x{be_lo:04X}, expected 0x{exp_lo:04X}"
    print(f"✓ self-unit numeric version FC04 320-327: "
          f"{major}.{minor}.{patch} suffix={suffix} version=0x{version:08X} "
          f"(LE and BE both consistent)")


@pytest.mark.qemu
def test_self_unit_signature_fc03(api, gateway_slave):
    """FC03 290..301 signature string at unit 0xFF equals /info signature."""
    info = api.get_info().json()
    expected = info["signature"]

    _tid, unit_id, resp_fc, payload = read_self_regs(
        "127.0.0.1", GATEWAY_HOST_PORT, 0x03, 290, 12
    )
    assert not (resp_fc & 0x80), \
        f"signature read returned exception FC=0x{resp_fc:02X}, payload={payload.hex()}"
    assert unit_id == SELF_UNIT_ID
    assert resp_fc == 0x03, f"FC mismatch: expected 0x03, got 0x{resp_fc:02X}"
    signature = decode_reg_string(regs_from_payload(payload))
    assert signature == expected, \
        f"signature mismatch: register={signature!r}, /info signature={expected!r}"
    print(f"✓ self-unit signature FC03 290-301 == /info signature: {signature!r}")


@pytest.mark.qemu
def test_self_unit_reboot_reason_plausible(api, gateway_slave):
    """FC04 65508 reboot reason at unit 0xFF is a plausible WB reason code (0..6)."""
    _tid, unit_id, resp_fc, payload = read_self_regs(
        "127.0.0.1", GATEWAY_HOST_PORT, 0x04, 65508, 1
    )
    assert not (resp_fc & 0x80), \
        f"reboot reason read returned exception FC=0x{resp_fc:02X}, payload={payload.hex()}"
    assert unit_id == SELF_UNIT_ID
    regs = regs_from_payload(payload)
    assert len(regs) == 1, f"expected 1 register, got {regs}"
    reason = regs[0]
    # Valid WB reboot-reason codes produced by map_reboot_reason(): 0 (NONE/unknown)
    # .. 6 (PIN). See main/bridge/mb_device.c.
    assert 0 <= reason <= 6, f"reboot reason code out of range 0..6: {reason}"
    print(f"✓ self-unit reboot reason FC04 65508: code={reason}")


@pytest.mark.qemu
def test_self_unit_undefined_addr_exception(api, gateway_slave):
    """FC04 at an undefined address (500) at unit 0xFF -> exception 0x02."""
    _tid, unit_id, resp_fc, payload = read_self_regs(
        "127.0.0.1", GATEWAY_HOST_PORT, 0x04, 500, 1
    )
    assert resp_fc & 0x80, \
        f"undefined address should return an exception, got FC=0x{resp_fc:02X}"
    assert resp_fc == (0x04 | 0x80), \
        f"expected exception FC 0x84, got 0x{resp_fc:02X}"
    assert len(payload) >= 1, f"exception response has no code: {payload.hex()}"
    assert payload[0] == 0x02, \
        f"expected exception code 0x02 (ILLEGAL_ADDRESS), got 0x{payload[0]:02X}"
    print("✓ self-unit FC04 addr=500 (undefined) -> exception 0x02")


@pytest.mark.qemu
def test_self_unit_bad_fc_exception(api, gateway_slave):
    """An unsupported FC (FC01 coils) at unit 0xFF -> exception 0x01."""
    _tid, unit_id, resp_fc, payload = read_self_regs(
        "127.0.0.1", GATEWAY_HOST_PORT, 0x01, 0, 1
    )
    assert resp_fc & 0x80, \
        f"FC01 on self-unit should return an exception, got FC=0x{resp_fc:02X}"
    assert resp_fc == (0x01 | 0x80), \
        f"expected exception FC 0x81, got 0x{resp_fc:02X}"
    assert len(payload) >= 1, f"exception response has no code: {payload.hex()}"
    assert payload[0] == 0x01, \
        f"expected exception code 0x01 (ILLEGAL_FUNCTION), got 0x{payload[0]:02X}"
    print("✓ self-unit FC01 (unsupported) -> exception 0x01")


# --------------------------------------------------------------------------- #
# Group B — Cache TCP server mode                                              #
# --------------------------------------------------------------------------- #

@pytest.fixture(scope="module")
def cache_server(api: WBMGEAPI):
    """Enable the cache Modbus TCP server on port 50504 and yield (host, port).

    The cache stays empty/disabled here on purpose: the self-unit (0xFF)
    interception in cache_modbus_server.c happens BEFORE the cache-enabled guard,
    so device-info registers are served regardless of cache contents. Restores
    the original settings on teardown.
    """
    host = urlparse(api.base_url).hostname or "localhost"

    settings_resp = api.get_settings()
    assert settings_resp.status_code == 200, \
        f"cache_server: GET /settings failed: HTTP {settings_resp.status_code}"
    orig = settings_resp.json()
    original_port = orig.get("cache_modbus_port", 504)
    original_enabled = orig.get("cache_modbus_server_enabled", False)

    try:
        resp = api.update_settings({
            "cache_modbus_server_enabled": True,
            "cache_modbus_port": QEMU_CACHE_MODBUS_PORT,
        })
        assert resp.status_code == 200 and resp.json().get("success") is True, \
            f"cache_server: failed to enable cache server: {resp.status_code} {resp.text}"

        # Wait for the server to start accepting on the forwarded port.
        ready = _poll_tcp_connect(host, QEMU_CACHE_MODBUS_PORT, timeout=10.0)
        if not ready:
            pytest.skip(
                f"Cache Modbus TCP port {QEMU_CACHE_MODBUS_PORT} did not open on {host} "
                "within 10 s — skipping cache-server self-unit tests"
            )

        yield (host, QEMU_CACHE_MODBUS_PORT)

    finally:
        restore = api.update_settings({
            "cache_modbus_server_enabled": original_enabled,
            "cache_modbus_port": original_port,
        })
        # print instead of assert: an assert in teardown would mask a test failure.
        if restore.status_code != 200:
            print(f"✗ cache_server: failed to restore settings: HTTP {restore.status_code}")


@pytest.mark.qemu
def test_self_unit_via_cache_server_model(api, cache_server):
    """FC04 200..219 model string at unit 0xFF on the cache server == /info device_name.

    Proves the cache server serves device-info registers regardless of cache
    contents (the cache is empty here).
    """
    host, port = cache_server
    expected = api.get_info().json()["device_name"]

    _tid, unit_id, resp_fc, payload = read_self_regs(host, port, 0x04, 200, 20)
    assert not (resp_fc & 0x80), \
        f"cache-server model read returned exception FC=0x{resp_fc:02X}, payload={payload.hex()}"
    assert unit_id == SELF_UNIT_ID
    model = decode_reg_string(regs_from_payload(payload))
    assert model == expected, \
        f"cache-server model mismatch: register={model!r}, /info device_name={expected!r}"
    print(f"✓ self-unit via cache server: model FC04 200-219 == /info device_name: {model!r}")


@pytest.mark.qemu
def test_self_unit_via_cache_server_uptime(api, cache_server):
    """FC04 104..105 uptime at unit 0xFF on the cache server is a valid response."""
    host, port = cache_server

    _tid, unit_id, resp_fc, payload = read_self_regs(host, port, 0x04, 104, 2)
    assert not (resp_fc & 0x80), \
        f"cache-server uptime returned exception FC=0x{resp_fc:02X}, payload={payload.hex()}"
    assert unit_id == SELF_UNIT_ID, \
        f"echoed unit_id mismatch: expected 0x{SELF_UNIT_ID:02X}, got 0x{unit_id:02X}"
    assert resp_fc == 0x04, f"FC mismatch: expected 0x04, got 0x{resp_fc:02X}"
    regs = regs_from_payload(payload)
    assert len(regs) == 2, f"expected 2 uptime registers, got {regs}"
    uptime_s = (regs[0] << 16) | regs[1]
    print(f"✓ self-unit via cache server: uptime FC04 104-105 regs={regs} uptime≈{uptime_s}s")


@pytest.mark.qemu
def test_self_unit_cache_timeout_reg(api, cache_server):
    """FC04 336 (cache timeout) at unit 0xFF reflects cache_value_timeout_s.

    Sets a known cache_value_timeout_s (33), reads register 336 on the cache
    server, asserts they match, then restores the original timeout.
    """
    host, port = cache_server

    settings_resp = api.get_settings()
    assert settings_resp.status_code == 200, \
        f"GET /settings failed: HTTP {settings_resp.status_code}"
    original_timeout = settings_resp.json().get("cache_value_timeout_s", 0)

    KNOWN_TIMEOUT = 33
    try:
        resp = api.update_settings({"cache_value_timeout_s": KNOWN_TIMEOUT})
        assert resp.status_code == 200 and resp.json().get("success") is True, \
            f"failed to set cache_value_timeout_s={KNOWN_TIMEOUT}: {resp.status_code} {resp.text}"

        _tid, unit_id, resp_fc, payload = read_self_regs(host, port, 0x04, 336, 1)
        assert not (resp_fc & 0x80), \
            f"cache timeout read returned exception FC=0x{resp_fc:02X}, payload={payload.hex()}"
        assert unit_id == SELF_UNIT_ID
        regs = regs_from_payload(payload)
        assert len(regs) == 1, f"expected 1 register, got {regs}"
        assert regs[0] == KNOWN_TIMEOUT, (
            f"cache timeout reg 336 mismatch: got {regs[0]}, expected {KNOWN_TIMEOUT}"
        )
        print(f"✓ self-unit cache timeout FC04 336 == cache_value_timeout_s={KNOWN_TIMEOUT}")
    finally:
        restore = api.update_settings({"cache_value_timeout_s": original_timeout})
        # print instead of assert: an assert in teardown would mask a test failure.
        if restore.status_code != 200:
            print(f"✗ failed to restore cache_value_timeout_s={original_timeout}: "
                  f"HTTP {restore.status_code}")
