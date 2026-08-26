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
  (host ports follow WB_MGE_PORT_SLOT — api_tests/qemu_ports.py; guest ports are fixed)
  - the UART1 chardev exposed on TCP (for the gateway RTU slave).
  - guest port 502   forwarded to the gateway host port.
  - guest port 50504 forwarded to the cache-Modbus host port.

Register map (unit 0xFF), confirmed against main/bridge/mb_device.c:
  FC04 input:  104-105 uptime_s (u32 MSW-first); 121 supply mV;
               200-219 model string (MEM_8: 1 char/reg, in the LOW byte);
               220-244 git info (2 chars/reg, FIRST char in the LOW byte);
               250-265 fw version string (MEM_8: 1 char/reg, in the LOW byte);
               266-267 serial generation scheme u32 (4 = from MAC);
               268-271 serial number u64 MSW-first;
               320 MAJOR, 321 MINOR, 322 PATCH, 323 SUFFIX(s16);
               324-325 numeric version LE word order (324=low);
               326-327 numeric version BE word order (326=high);
               65505..65507 RAM total/used/free, KB; 65508 reboot reason
               (65504 stays undefined: the WB stack register is not implemented);
               528-529 packets u32; 530-531 last-pkt-age u32;
               532 devices_on_bus; 533 bus poll ppm; 534 cache timeout s.
               (330-337 stay undefined: the 8-register WB bootloader-version field.)
               290-301 signature string (MEM_8: 1 char/reg, in the LOW byte).
  FC03 holding: the SAME map. FC03 and FC04 share one address space — every
               address above, the signature included, answers on both function
               codes with the same value.
  An address defined in neither map -> exception 0x02.
  Any fc not in {0x03,0x04} on unit 0xFF -> exception 0x01.
"""

import qemu_ports
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

# Host port this slot forwards to QEMU guest port 502 (the gateway).
GATEWAY_HOST_PORT = qemu_ports.GATEWAY_HOST_PORT
# UART1 chardev TCP socket (QEMU -serial tcp::<slot UART1 port>,server,nowait).
UART1_TCP_PORT = qemu_ports.UART1_TCP_PORT
# Fake register value returned by the RTU slave for any register read.
FAKE_VALUE = 0x1234

# Host port this slot forwards to the cache Modbus TCP server (guest port 50504).
QEMU_CACHE_MODBUS_PORT = qemu_ports.QEMU_CACHE_MODBUS_PORT

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


def decode_mem8(values):
    """Decode a MEM_8 string field: ONE character per register, in the LOW byte.

    This is the WB reference layout for the model (200-219), the firmware-version
    string (250-265) and the signature (290-301) — each field's register count
    equals its character count. The high byte must be 0x00; asserting that is what
    catches a field packed with the wrong layout (WB tooling would read garbage).
    Strips trailing NULs and decodes latin-1.
    """
    raw = bytearray()
    for v in values:
        assert (v >> 8) == 0x00, (
            f"MEM_8 register value 0x{v:04X} has a non-zero high byte: the field is "
            "not packed 1 character per register and WB tooling would read garbage"
        )
        raw.append(v & 0xFF)
    return raw.rstrip(b"\x00").decode("latin-1")


def decode_git_string(values):
    """Decode the git-info field (220-244): TWO characters per register, the first
    character of the pair in the LOW byte and the second in the HIGH byte.

    On the wire a register arrives as (hi, lo), i.e. [2nd char, 1st char], so the
    two bytes of every pair must be swapped back. Strips trailing NULs, latin-1.
    """
    raw = bytearray()
    for v in values:
        raw.append(v & 0xFF)         # first character of the pair
        raw.append((v >> 8) & 0xFF)  # second character of the pair
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
    uart_tcp_port=UART1_TCP_PORT,
    bridge_port=qemu_ports.GATEWAY_GUEST_PORT,      # guest 502
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
    model = decode_mem8(regs_from_payload(payload))
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
    fw = decode_mem8(regs_from_payload(payload))
    assert fw == expected, \
        f"firmware string mismatch: register={fw!r}, /info firmware={expected!r}"
    print(f"✓ self-unit firmware FC04 250-265 == /info firmware: {fw!r}")


@pytest.mark.qemu
def test_self_unit_git_info_matches_info(api, gateway_slave):
    """FC04 220..244 git-info string at unit 0xFF equals /info git_info.

    The git-info field is the one string field packed TWO characters per register,
    with the FIRST character of each pair in the LOW byte — the byte order inside a
    pair is swapped relative to the wire order. decode_git_string() undoes that; a
    naive big-endian decode would come back with every character pair transposed.
    """
    info = api.get_info().json()
    expected = info["git_info"]

    _tid, unit_id, resp_fc, payload = read_self_regs(
        "127.0.0.1", GATEWAY_HOST_PORT, 0x04, 220, 25
    )
    assert not (resp_fc & 0x80), \
        f"git info read returned exception FC=0x{resp_fc:02X}, payload={payload.hex()}"
    assert unit_id == SELF_UNIT_ID
    git_info = decode_git_string(regs_from_payload(payload))
    assert git_info == expected, \
        f"git info mismatch: register={git_info!r}, /info git_info={expected!r}"
    print(f"✓ self-unit git info FC04 220-244 == /info git_info: {git_info!r}")


@pytest.mark.qemu
def test_self_unit_serial_matches_info(api, gateway_slave):
    """Serial registers at unit 0xFF: scheme + serial value.

    266-267 (u32 MSW-first) == 4 (serial generated from the MAC).
    268-271 (u64 MSW-first) == full serial_num from /info.
    """
    info = api.get_info().json()
    serial_num = info["serial_num"]

    # Generation scheme, u32 MSW-first across 266..267.
    _tid, unit_id, resp_fc, payload = read_self_regs(
        "127.0.0.1", GATEWAY_HOST_PORT, 0x04, 266, 2
    )
    assert not (resp_fc & 0x80), \
        f"serial scheme read returned exception FC=0x{resp_fc:02X}, payload={payload.hex()}"
    assert unit_id == SELF_UNIT_ID
    scheme_regs = regs_from_payload(payload)
    scheme = (scheme_regs[0] << 16) | scheme_regs[1]
    assert scheme == 4, f"serial generation scheme mismatch: got {scheme}, expected 4"

    # Serial value, u64 MSW-first across 268..271.
    _tid, unit_id, resp_fc, payload = read_self_regs(
        "127.0.0.1", GATEWAY_HOST_PORT, 0x04, 268, 4
    )
    assert not (resp_fc & 0x80), \
        f"serial value read returned exception FC=0x{resp_fc:02X}, payload={payload.hex()}"
    assert unit_id == SELF_UNIT_ID
    val_regs = regs_from_payload(payload)
    serial_u64 = 0
    for r in val_regs:
        serial_u64 = (serial_u64 << 16) | r
    assert serial_u64 == serial_num, (
        f"serial u64 mismatch: register={serial_u64}, /info serial_num={serial_num}"
    )
    print(f"✓ self-unit serial: scheme={scheme} value={serial_u64} "
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
    signature = decode_mem8(regs_from_payload(payload))
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
def test_self_unit_ram_diagnostics_kb(api, gateway_slave):
    """FC04 121 voltage + 65505..65507 RAM diagnostics at unit 0xFF are sane KB values.

    The RAM block follows the WB common register map: 65505 total, 65506 used,
    65507 free. The test checks, in this order:
      - register 121 (supply voltage): a cheap liveness probe of the self-unit
        map before the RAM registers are read.
      - bytes->KB: RAM used to be reported in BYTES, which saturates the u16
        register at 0xFFFF on an ESP32 with hundreds of KB of internal RAM. Both
        bounds apply to 65505 only, and `< 2000` is the one that catches a
        byte-based 65505. A byte-based 65506 or 65507 has no bound of its own; it
        is caught by total > used / total > free and by the sum check.
      - arithmetic: total > used, total > free, and used + free == total within
        the rounding/churn window below. These say nothing about a used<->free
        swap: the firmware derives used as total - free, so all three survive
        swapping 65506 with 65507.

    Two further properties live in sibling tests on purpose, so that an early exit
    in one can never drop the other silently:
      - test_self_unit_ram_matches_info pins 65505 to /info heap_total exactly and
        puts 65506 and 65507 on opposite sides of half the heap — enough to
        discriminate a used<->free swap, which nothing here can. It can legitimately
        skip ITSELF (see its own docstring); a skip here would swallow every
        assertion above as well.
      - test_self_unit_65504_unmapped covers the removed WB "max used stack" slot.
    """
    # 121 supply voltage (mV). The QEMU voltage source may be a mock, so keep
    # this loose — just prove the register responds with a sane u16 value.
    _tid, unit_id, resp_fc, payload = read_self_regs(
        "127.0.0.1", GATEWAY_HOST_PORT, 0x04, 121, 1
    )
    assert not (resp_fc & 0x80), \
        f"supply voltage read returned exception FC=0x{resp_fc:02X}, payload={payload.hex()}"
    assert unit_id == SELF_UNIT_ID, \
        f"echoed unit_id mismatch: expected 0x{SELF_UNIT_ID:02X}, got 0x{unit_id:02X}"
    regs = regs_from_payload(payload)
    assert len(regs) == 1, f"expected 1 voltage register, got {regs}"
    mv = regs[0]
    assert 0 <= mv <= 65535, f"supply voltage out of u16 range: {mv}"

    # 65505..65507 RAM diagnostics (contiguous block, all in KB).
    _tid, unit_id, resp_fc, payload = read_self_regs(
        "127.0.0.1", GATEWAY_HOST_PORT, 0x04, 65505, 3
    )
    assert not (resp_fc & 0x80), \
        f"RAM read returned exception FC=0x{resp_fc:02X}, payload={payload.hex()}"
    assert unit_id == SELF_UNIT_ID, \
        f"echoed unit_id mismatch: expected 0x{SELF_UNIT_ID:02X}, got 0x{unit_id:02X}"
    regs = regs_from_payload(payload)
    assert len(regs) == 3, f"expected 3 registers (65505..65507), got {regs}"
    total_ram_kb, used_ram_kb, free_ram_kb = regs

    # --- REGRESSION GUARD (bytes -> KB) --- #
    # The lower bound is NOT the bytes->KB detector: sat_u16() in mb_device.c pins a
    # byte count to 0xFFFF, so a byte-based register comes out at the TOP of the u16
    # range and can never come out small. What `> 64` catches is zero, or some small
    # garbage value, in a register that must always carry the size of the whole ESP32
    # internal heap (measured: 425 KB under QEMU, 306 KB on an mge_v3 device).
    assert total_ram_kb > 64, (
        f"total RAM reg 65505 = {total_ram_kb} KB (expected > 64 KB); the ESP32 "
        "internal heap is far larger than that, so 65505 is not reporting it "
        "(a byte-based register would fail the upper bound below instead)"
    )
    # THIS is the bytes->KB bound, and it covers both byte-based failure modes at
    # once: 0xFFFF is the saturated u16 a byte count would pin the register to, and
    # anything from a few thousand KB upwards is already past the whole ESP32
    # internal-RAM budget. One assertion is enough — 2000 is the tighter ceiling.
    assert total_ram_kb < 2000, (
        f"total RAM reg 65505 = {total_ram_kb} KB: 0xFFFF (65535) means a byte count "
        "saturated the u16 register, and any value this large exceeds the few hundred "
        "KB of ESP32 internal RAM — either way the register looks byte-based"
    )
    assert used_ram_kb > 0, f"used RAM reg 65506 = {used_ram_kb} (expected > 0)"
    assert free_ram_kb > 0, f"free RAM reg 65507 = {free_ram_kb} (expected > 0)"

    # --- ARITHMETIC GUARD (65505 total / 65506 used / 65507 free) --- #
    # These only prove the total dominates both parts. They do NOT detect a
    # used<->free swap: the firmware derives used as total - free, so swapping the
    # 65506 and 65507 branches leaves both comparisons — and their sum below —
    # intact. test_self_unit_ram_matches_info is what discriminates the two.
    assert total_ram_kb > used_ram_kb, (
        f"total RAM {total_ram_kb} KB must exceed used RAM {used_ram_kb} KB — "
        "65505 is not reporting the size of the whole internal heap"
    )
    assert total_ram_kb > free_ram_kb, (
        f"total RAM {total_ram_kb} KB must exceed free RAM {free_ram_kb} KB — "
        "65505 is not reporting the size of the whole internal heap"
    )
    # used + free vs total, within a SYMMETRIC +/-8 KB window:
    #   * Rounding alone can only pull the sum DOWN, and by at most 1 KB, because
    #     floor((T-f)/1024) + floor(f/1024) is either floor(T/1024) or one less.
    #   * Heap churn is what can move it further, in either direction. The block is
    #     NOT an atomic snapshot: `free` is sampled twice per request — once inside
    #     the 65506 branch, which returns total - free, and again inside the 65507
    #     branch. A block freed between the two samples pushes the sum up, one
    #     allocated pushes it down.
    #   * WHO can interfere: in practice only QEMU. The gateway_slave fixture
    #     (build_gateway_fixture in api_tests/conftest.py) skips unless
    #     UART1_TCP_PORT — the QEMU UART1 chardev — accepts a connection, and the RAM
    #     reads here and in test_self_unit_ram_matches_info both go to
    #     GATEWAY_HOST, GATEWAY_HOST_PORT, the QEMU hostfwd port. (Not every register
    #     read in this file does: the cache_server tests read from the --ip host.) HTTP
    #     is a separate endpoint: api.get_info() talks to the --ip address. Under --qemu
    #     that is the SAME guest, and the argument for it is now the port SLOT rather
    #     than any fixed number: --ip defaults to localhost:qemu_ports.HTTP_HOST_PORT and
    #     conftest builds the hostfwd for that same HTTP_HOST_PORT from the same
    #     qemu_ports block, so the endpoint the HTTP client uses and the guest whose UART
    #     chardev gates this test are one QEMU by construction, for every slot. (The
    #     older argument — "conftest refuses to start if 8080 or 5561 is already taken" —
    #     no longer holds: the preflight checks this slot's block, and neither of those
    #     numbers is in it. The conclusion survives; the premise does not.)
    #     It is NOT guaranteed to be the same machine in general:
    #     `pytest --ip=<real device>` with a separately started QEMU alongside passes
    #     the chardev gate, and then the registers come from QEMU while /info comes from
    #     the hardware. (@pytest.mark.qemu itself is inert — no hook skips on it; the
    #     fixture is the real gate.)
    #   * The QEMU build is single-core (CONFIG_FREERTOS_UNICORE=y in
    #     sdkconfig.qemu_build), so only preemption of the request-handling task
    #     (modbus_tcp.c, MODBUS_TCP_TASK_PRIORITY = 4) reaches the window — e.g. lwIP
    #     at priority 18 (CONFIG_LWIP_TCPIP_TASK_PRIO) touching ~1.5 KB pbufs. It also
    #     has PSRAM (CONFIG_SPIRAM_USE_MALLOC=y, CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=
    #     16384), which routes large heap_caps_malloc_default() requests away from the
    #     internal heap these registers report — though that call falls back to
    #     internal memory when PSRAM cannot satisfy the request, and heap_caps_malloc()
    #     with explicit caps ignores the threshold outright. On mge_v3 hardware the
    #     build is dual-core with no PSRAM at all, so every allocation comes out of
    #     this same heap; the test does not run there.
    #   * MEASURED: one QEMU run of this test returned total=425 used=175 free=250 KB,
    #     a sum delta of exactly 0 KB; a direct read of this firmware on an mge_v3
    #     device returned 306 / 229 / 76 KB, delta -1 KB, the floor-rounding result.
    #     Two samples, not a bound, but neither comes near the +/-8 KB window.
    # The bound is symmetric because a tighter lower edge buys no detection power.
    # The defect worth catching here is 65507 reading heap_caps_get_minimum_free_size()
    # — the worst-case free heap since boot — instead of the call it claims. mb_device.c
    # does not use it today, but /info does, four lines from its own
    # heap_caps_get_free_size() call (main/info_handlers.c:290 and :294), so it is the
    # obvious wrong call to reach for. It reads at or below current free, and while the
    # heap sits at its all-time low the two are equal and NO bound can detect the
    # substitution. How the two shapes of it fare (gap = current free - min_free):
    #   (a) ONLY the 65507 branch changed, 65506 still returning total - free: the sum
    #       drops by roughly the gap. Small gaps — single-digit KB — can slip through
    #       this window; larger ones fail it reliably. On the mge_v3 read above /info
    #       reported heap_free 83372 B against heap_min_free 78732 B, a gap of ~4.5 KB,
    #       i.e. the slip-through end.
    #   (b) BOTH branches changed: 65506 computing total - min_free and 65507 returning
    #       min_free, the plausible "report the worst case since boot" edit. The sum is
    #       then total up to the same rounding as the correct code, for ANY gap, so the
    #       sum check does not catch this shape at all and no window width would.
    #       Neither does the exact 65505 vs /info heap_total comparison in
    #       test_self_unit_ram_matches_info, which never touches min_free. Only the
    #       nearest-match assertions can, and only once the gap grows large enough to
    #       push the register past info_total / 2 — about 38 KB on the measured QEMU
    #       run (total 425, register free 250, midpoint 212.5).
    # Shape (b) is therefore recorded here, not closed. Closing it would take a new
    # assertion comparing 65507 against /info heap_min_free, and the gap between the two
    # is a build and workload property, so that assertion would arrive with no
    # defensible threshold. Anyone hardening this later should start there rather than
    # at the +/-8 KB window.
    churn_slack_kb = 8
    sum_delta_kb = (used_ram_kb + free_ram_kb) - total_ram_kb
    assert abs(sum_delta_kb) <= churn_slack_kb, (
        f"used ({used_ram_kb}) + free ({free_ram_kb}) = {used_ram_kb + free_ram_kb} KB "
        f"vs total ({total_ram_kb}) KB: delta {sum_delta_kb} KB is outside the "
        f"+/-{churn_slack_kb} KB rounding + heap-churn window"
    )

    print("✓ self-unit RAM KB: total=%d used=%d free=%d voltage=%dmV"
          % (total_ram_kb, used_ram_kb, free_ram_kb, mv))


@pytest.mark.qemu
def test_self_unit_ram_matches_info(api, gateway_slave):
    """65505 == /info heap_total exactly; 65506/65507 on opposite sides of half the heap.

    Those two properties are the whole guarantee, and they are deliberately weaker
    than "these registers report the quantities /info says they do". Past the exact
    65505 comparison the test only decides which side of info_total / 2 each of the
    other two falls on: on the measured figures below a 65506 stuck at 0 passes both
    nearest-match assertions, because |0 - 166| < |0 - 259|. What rejects that value
    is `used_ram_kb > 0` in test_self_unit_ram_diagnostics_kb, not anything here.

    Side-of-midpoint is still the property worth pinning, because it is exactly what
    makes a used<->free swap detectable, and no assertion in
    test_self_unit_ram_diagnostics_kb can do that: the firmware derives used as
    total - free, so total > used, total > free and used + free == total all hold
    just as well with the 65506 and 65507 branches exchanged.

    /info serves the same two heap_caps_* quantities in BYTES, from the same calls on
    the same MALLOC_CAP_INTERNAL pool (main/info_handlers.c:289-292), so info_used =
    heap_total - heap_free is computable and each register can be matched against the
    NEARER of the two candidates instead of against an invented tolerance window.
    Measured under QEMU: registers total=425 used=175 free=250 KB, /info total=425
    free=259 KB, hence info_used=166 KB.

    NOTE: this test assumes the register endpoint and the HTTP endpoint are the same
    device. They are under --qemu (see the endpoint note in
    test_self_unit_ram_diagnostics_kb), but `pytest --ip=<real device>` with a QEMU
    running alongside would compare two machines' heaps and is not a supported way to
    run it.

    WHEN THIS TEST SKIPS: the nearest-match half only means anything while the two
    candidates are far enough apart, so it is guarded by a separation check that SKIPS
    instead of failing. How close the heap sits to half-used is a property of the
    environment, not of the firmware, and a red run there would be indistinguishable
    by colour from a real regression. On the two heaps measured the margin is wide:
    259 free / 166 used KB under QEMU, and 306 total / 229 used / 76 free KB read
    directly from this firmware on an mge_v3 device. The QEMU build has PSRAM and the
    hardware build does not (see the churn notes in
    test_self_unit_ram_diagnostics_kb), so neither figure guarantees the other. The
    guard exists so that losing the margin becomes visible instead of silently turning
    this test into a tautology, and it can only skip THIS test, which is why it lives
    here and not in the sibling. The threshold arithmetic is in the body.
    """
    _tid, unit_id, resp_fc, payload = read_self_regs(
        "127.0.0.1", GATEWAY_HOST_PORT, 0x04, 65505, 3
    )
    assert not (resp_fc & 0x80), \
        f"RAM read returned exception FC=0x{resp_fc:02X}, payload={payload.hex()}"
    assert unit_id == SELF_UNIT_ID, \
        f"echoed unit_id mismatch: expected 0x{SELF_UNIT_ID:02X}, got 0x{unit_id:02X}"
    regs = regs_from_payload(payload)
    assert len(regs) == 3, f"expected 3 registers (65505..65507), got {regs}"
    total_ram_kb, used_ram_kb, free_ram_kb = regs

    info_resp = api.get_info()
    assert info_resp.status_code == 200, \
        f"/info returned {info_resp.status_code}: {info_resp.text}"
    info = info_resp.json()
    info_total_kb = int(info["heap_total"]) // 1024
    info_free_kb = int(info["heap_free"]) // 1024
    # Derived, not reported: /info has no heap_used field. The KB conversion happens
    # after the subtraction on the firmware side (65506 is floor((total-free)/1024))
    # and before it here, so this can sit 1 KB off the register even when both are
    # perfectly correct — irrelevant against the separations the check works with.
    info_used_kb = info_total_kb - info_free_kb

    # The heap total is fixed once the regions are registered during startup, so
    # this comparison is exact: it fails the moment 65505 carries anything else.
    assert total_ram_kb == info_total_kb, (
        f"total RAM reg 65505 = {total_ram_kb} KB but /info heap_total = "
        f"{info_total_kb} KB ({info['heap_total']} B) — 65505 is not "
        "heap_caps_get_total_size(MALLOC_CAP_INTERNAL) in KB"
    )

    # --- PRECONDITION: the two candidates must be distinguishable --- #
    # Because info_used is DEFINED as info_total - info_free, the midpoint between
    # the two candidates is exactly info_total / 2, and "nearer to free than to used"
    # is really "on free's side of half the heap". A correct register is therefore
    # misclassified only if the free heap crossed half-total between the Modbus read
    # and the /info read — i.e. only if the drift exceeded separation / 2. The same
    # threshold, from the other side, is what a swapped register would need to escape
    # detection, so one number covers both directions.
    #
    # Measured drift, same QEMU run as the docstring: register free 250 KB vs /info
    # free 259 KB, 9 KB apart, on a separation of |259 - 166| = 93 KB, which left the
    # register well clear of the midpoint. Requiring 4x the measured drift keeps 2x of
    # it in hand: at the limit /info's free sits two drifts above the midpoint, so the
    # drift would have to double before a correct register crosses to the wrong side
    # (or a swapped one crosses back). There is no second sample to average, so this
    # is deliberately a multiple of the one measurement rather than a round number.
    measured_drift_kb = 9
    min_separation_kb = 4 * measured_drift_kb
    separation_kb = abs(info_free_kb - info_used_kb)

    # SKIP rather than fail — the full rationale (why an environment property must
    # not be reported as a firmware regression, and why the guard belongs to this
    # test rather than to its sibling) is in this function's docstring.
    if separation_kb < min_separation_kb:
        pytest.skip(
            f"used<->free cross-check not applicable in this environment: /info "
            f"heap_free = {info_free_kb} KB and heap_used = {info_used_kb} KB "
            f"(= heap_total {info_total_kb} - heap_free) are only {separation_kb} KB "
            f"apart, below the {min_separation_kb} KB needed to survive the "
            f"{measured_drift_kb} KB drift measured between the two requests. Nearer "
            "to one than to the other would stop meaning anything. Not a firmware "
            "defect; if it persists, re-measure the drift between the two reads and "
            f"set measured_drift_kb (now {measured_drift_kb}) from the new figure — "
            "min_separation_kb is derived from it as 4x, so it is not the knob"
        )

    # --- NEAREST-MATCH: each register must land on its own candidate --- #
    # Both messages name the two causes that can produce a failure here, because the
    # test cannot tell them apart on its own: either the registers really are swapped,
    # or the free heap drifted across info_total / 2 between the Modbus read and the
    # /info read, which misreports healthy firmware. The two numbers printed with each
    # failure are what separate them — the observed drift (register free vs /info
    # free, ~9 KB on the measured run) against the register's own distance from the
    # midpoint. A drift far smaller than that distance points at the registers; a drift
    # of the same order means the margin collapsed instead, and the skip guard above
    # was tuned too loosely for this environment.
    assert abs(free_ram_kb - info_free_kb) < abs(free_ram_kb - info_used_kb), (
        f"free RAM reg 65507 = {free_ram_kb} KB is nearer to /info heap_used = "
        f"{info_used_kb} KB (distance {abs(free_ram_kb - info_used_kb)}) than to "
        f"/info heap_free = {info_free_kb} KB ({info['heap_free']} B, distance "
        f"{abs(free_ram_kb - info_free_kb)}) — 65507 is not free heap. Two causes fit: "
        "65506/65507 swapped, i.e. 65507 carrying total - free (the likelier one), or "
        "the free heap drifting across the midpoint between the two reads, which would "
        "fail healthy firmware. Observed drift (reg free vs /info free) "
        f"{abs(free_ram_kb - info_free_kb)} KB; distance from reg 65507 to the midpoint "
        f"info_total/2 = {info_total_kb / 2:.1f} KB is "
        f"{abs(free_ram_kb - info_total_kb / 2):.1f} KB"
    )
    # Redundant against the assertion above under today's firmware, and kept anyway.
    # 65506 is floor((total - free)/1024) and 65507 is floor(free/1024), so "65506
    # nearer to info_used" is the same side-of-midpoint test as "65507 nearer to
    # info_free" (info_used is DEFINED as info_total - info_free); the two pass and
    # fail together up to rounding plus the churn between the two free samples. It
    # stops being redundant the moment 65506 becomes an independent computation — a
    # separate allocation counter, a different capability mask, or any source other
    # than "total minus the same heap_caps_get_free_size() call".
    assert abs(used_ram_kb - info_used_kb) < abs(used_ram_kb - info_free_kb), (
        f"used RAM reg 65506 = {used_ram_kb} KB is nearer to /info heap_free = "
        f"{info_free_kb} KB (distance {abs(used_ram_kb - info_free_kb)}) than to "
        f"/info heap_used = {info_used_kb} KB (= heap_total {info_total_kb} - "
        f"heap_free, distance {abs(used_ram_kb - info_used_kb)}) — 65506 is not used "
        "heap. Two causes fit: 65506/65507 swapped (the likelier one), or the free "
        "heap drifting across the midpoint between the two reads, which would fail "
        "healthy firmware. Observed drift (reg free vs /info free) "
        f"{abs(free_ram_kb - info_free_kb)} KB; distance from reg 65506 to the midpoint "
        f"info_total/2 = {info_total_kb / 2:.1f} KB is "
        f"{abs(used_ram_kb - info_total_kb / 2):.1f} KB"
    )

    print("✓ self-unit RAM matches /info: reg total=%d used=%d free=%d vs /info "
          "total=%d used=%d free=%d (separation %d KB, min %d)"
          % (total_ram_kb, used_ram_kb, free_ram_kb, info_total_kb, info_used_kb,
             info_free_kb, separation_kb, min_separation_kb))


@pytest.mark.qemu
def test_self_unit_65504_unmapped(api, gateway_slave):
    """FC04 65504 at unit 0xFF -> exception 0x02: the WB stack register is gone.

    Regression guard for the REMOVED WB "max used stack" slot. This firmware is
    multi-tasking and has no single stack to report, so 65504 must behave like any
    other undefined address instead of answering with a leftover value.

    It lives in its own test on purpose: it asserts a different property (an address
    is unmapped) and depends on neither /info nor any heap reading, so an early exit
    inside test_self_unit_ram_diagnostics_kb cannot drop it silently.
    """
    _tid, unit_id, resp_fc, payload = read_self_regs(
        "127.0.0.1", GATEWAY_HOST_PORT, 0x04, 65504, 1
    )
    assert resp_fc == (0x04 | 0x80), (
        "register 65504 must not be mapped: expected exception FC 0x84, got "
        f"0x{resp_fc:02X} (0x04 means FC04 answered it with a value)"
    )
    assert unit_id == SELF_UNIT_ID, \
        f"echoed unit_id mismatch: expected 0x{SELF_UNIT_ID:02X}, got 0x{unit_id:02X}"
    assert len(payload) >= 1, f"exception response has no code: {payload.hex()}"
    assert payload[0] == 0x02, \
        f"expected exception code 0x02 (ILLEGAL_ADDRESS), got 0x{payload[0]:02X}"
    print("✓ self-unit FC04 65504 (removed WB stack register) -> exception 0x02")


@pytest.mark.qemu
def test_self_unit_stats_block(api, gateway_slave):
    """FC04 528..534 statistics block at unit 0xFF responds with sane u16 values.

    In gateway-only mode the multimaster cache is typically inactive, so the
    counters may legitimately be 0. The point of this test is that the whole
    contiguous block RESPONDS without an exception and the values are sane u16s
    (devices_on_bus <= 247, cache timeout matches /settings when present).
    """
    _tid, unit_id, resp_fc, payload = read_self_regs(
        "127.0.0.1", GATEWAY_HOST_PORT, 0x04, 528, 7
    )
    assert not (resp_fc & 0x80), \
        f"stats block read returned exception FC=0x{resp_fc:02X}, payload={payload.hex()}"
    assert unit_id == SELF_UNIT_ID, \
        f"echoed unit_id mismatch: expected 0x{SELF_UNIT_ID:02X}, got 0x{unit_id:02X}"
    regs = regs_from_payload(payload)
    assert len(regs) == 7, f"expected 7 registers (528..534), got {regs}"
    pkt_hi, pkt_lo, age_hi, age_lo, devices, poll_ppm, cache_timeout = regs

    # u32 reconstructions (MSW-first). >= 0 is always true for a u16 combine —
    # the real assertion here is "the registers responded without an exception".
    packets = (pkt_hi << 16) | pkt_lo
    age = (age_hi << 16) | age_lo
    assert packets >= 0
    assert age >= 0

    assert 0 <= devices <= 247, (
        f"devices_on_bus reg 532 = {devices} out of range 0..247 (Modbus slave addresses)"
    )
    assert cache_timeout < 0xFFFF, (
        f"cache timeout reg 534 = {cache_timeout} == 0xFFFF — implausible for a u16 setting"
    )

    # Cross-check against /settings when the key is present.
    cfg_timeout = api.get_settings().json().get("cache_value_timeout_s")
    if cfg_timeout is not None:
        assert cache_timeout == cfg_timeout, (
            f"cache timeout reg 534 = {cache_timeout} != /settings "
            f"cache_value_timeout_s = {cfg_timeout}"
        )

    print("✓ self-unit stats block 528-534: timeout=%d packets=%d last_pkt_age=%d devices=%d poll_ppm=%d"
          % (cache_timeout, packets, age, devices, poll_ppm))


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
def test_self_unit_bootloader_field_not_claimed(api, gateway_slave):
    """FC03 330..337 (the WB bootloader-version field) is NOT claimed -> exception 0x02.

    WB tooling reads the current bootloader version as EIGHT holding registers from
    address 330 (`modbus_client -t0x03 -r330 -c8`), so the field spans 330..337 and
    the gateway must answer none of it. The statistics block used to start at 337,
    inside this field, and after the FC03/FC04 map was shared that register began
    answering on the very function code the bootloader read uses. It now lives at
    528..534. This is the regression guard for that.
    """
    _tid, _unit_id, resp_fc, payload = read_self_regs(
        "127.0.0.1", GATEWAY_HOST_PORT, 0x03, 330, 8
    )
    assert resp_fc & 0x80, (
        "the bootloader-version field 330..337 must not be served by the gateway, "
        f"but FC03 returned a valid response FC=0x{resp_fc:02X}"
    )
    assert payload[0] == 0x02, \
        f"expected exception code 0x02 (ILLEGAL_ADDRESS), got 0x{payload[0]:02X}"

    # 337 alone: the register the statistics block used to squat on.
    _tid, _unit_id, resp_fc, payload = read_self_regs(
        "127.0.0.1", GATEWAY_HOST_PORT, 0x03, 337, 1
    )
    assert resp_fc & 0x80, \
        f"register 337 is inside the bootloader-version field but FC03 answered it: 0x{resp_fc:02X}"
    assert payload[0] == 0x02, \
        f"expected exception code 0x02 (ILLEGAL_ADDRESS), got 0x{payload[0]:02X}"
    print("✓ self-unit FC03 330-337 (bootloader-version field) -> exception 0x02")


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
    """Enable the cache Modbus TCP server on guest port 50504 and yield (host, port).

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
            "cache_modbus_port": qemu_ports.CACHE_MODBUS_GUEST_PORT,
        })
        assert resp.status_code == 200 and resp.json().get("success") is True, \
            f"cache_server: failed to enable cache server: {resp.status_code} {resp.text}"

        # Wait for the server to start accepting on the forwarded port.
        # A failure to open the port is a real defect (the cache server did not
        # start), not a reason to skip — fail loudly instead of hiding the tests.
        ready = _poll_tcp_connect(host, QEMU_CACHE_MODBUS_PORT, timeout=10.0)
        assert ready, (
            f"Cache Modbus TCP port {QEMU_CACHE_MODBUS_PORT} did not open on {host} "
            "within 10 s — cache server failed to start"
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
    model = decode_mem8(regs_from_payload(payload))
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
    """FC04 534 (cache timeout) at unit 0xFF reflects cache_value_timeout_s.

    Sets a known cache_value_timeout_s (33), reads register 534 on the cache
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

        _tid, unit_id, resp_fc, payload = read_self_regs(host, port, 0x04, 534, 1)
        assert not (resp_fc & 0x80), \
            f"cache timeout read returned exception FC=0x{resp_fc:02X}, payload={payload.hex()}"
        assert unit_id == SELF_UNIT_ID
        regs = regs_from_payload(payload)
        assert len(regs) == 1, f"expected 1 register, got {regs}"
        assert regs[0] == KNOWN_TIMEOUT, (
            f"cache timeout reg 534 mismatch: got {regs[0]}, expected {KNOWN_TIMEOUT}"
        )
        print(f"✓ self-unit cache timeout FC04 534 == cache_value_timeout_s={KNOWN_TIMEOUT}")
    finally:
        restore = api.update_settings({"cache_value_timeout_s": original_timeout})
        # print instead of assert: an assert in teardown would mask a test failure.
        if restore.status_code != 200:
            print(f"✗ failed to restore cache_value_timeout_s={original_timeout}: "
                  f"HTTP {restore.status_code}")
