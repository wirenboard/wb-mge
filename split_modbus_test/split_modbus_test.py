#!/usr/bin/env python3
"""
Modbus RTU stream splitter — Variant 3 (context + length table + CRC scan fallback).

Reads errors.csv, takes ERR rows (glued packets), attempts to split them into
valid individual Modbus RTU frames.

Strategy (3-level):
  1. Context: if we know the previous request (slave_id + func), compute the
     expected response length first. Slice it off, verify CRC, then continue.
  2. Length table: for known function codes, compute the exact expected frame
     length from the PDU content and slice it off.
  3. CRC scan fallback: try every possible length from 4 to remaining bytes,
     take the first CRC match (with plausibility filter on slave_id / func).
"""

import csv
import struct
import sys
from dataclasses import dataclass, field
from typing import Optional

# ---------------------------------------------------------------------------
# CRC-16 Modbus RTU
# ---------------------------------------------------------------------------

def crc16(data: bytes) -> int:
    """Compute Modbus RTU CRC-16 (same algorithm as modbus_crc16() in C)."""
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc  # low byte first when appended to frame


def crc_ok(frame: bytes) -> bool:
    """Return True if the last 2 bytes of frame are the correct CRC."""
    if len(frame) < 4:
        return False
    calc = crc16(frame[:-2])
    lo = calc & 0xFF
    hi = (calc >> 8) & 0xFF
    return frame[-2] == lo and frame[-1] == hi


# ---------------------------------------------------------------------------
# PDU length calculator (Level 2 — deterministic length table)
# ---------------------------------------------------------------------------

# Known request lengths (fixed-size request PDUs including slave_id + CRC)
#   FC01/02/03/04/05/06: 8 bytes (slave + fc + 2-byte addr + 2-byte count/value + 2 CRC)
#   FC15/16: variable (8 + byte_count, byte_count in buf[6])
#   FM 0x46/0x60: variable by subcommand
FIXED_REQUEST_LEN = {
    0x01: 8, 0x02: 8, 0x03: 8, 0x04: 8,
    0x05: 8, 0x06: 8,
}


def expected_len(buf: bytes, is_response: bool = False) -> Optional[int]:
    """
    Given a buffer starting at the beginning of a Modbus RTU frame,
    return the expected total byte length of that frame (including slave_id and CRC),
    or None if we cannot determine it from available bytes.

    is_response: hint that this is expected to be a response frame
    """
    if len(buf) < 3:
        return None

    fc = buf[1]

    # Fast Modbus: slave 0xFD, fc 0x46 or 0x60
    if buf[0] == 0xFD and fc in (0x46, 0x60):
        return _fm_expected_len(buf)

    # Modbus exception response: fc has high bit set → 5 bytes
    if fc & 0x80:
        return 5  # slave + fc|0x80 + exception_code + 2 CRC

    if is_response:
        return _response_expected_len(buf)
    else:
        return _request_expected_len(buf)


def _request_expected_len(buf: bytes) -> Optional[int]:
    fc = buf[1]
    if fc in FIXED_REQUEST_LEN:
        return FIXED_REQUEST_LEN[fc]
    # FC15 (Write Multiple Coils): 7 + byte_count + 2 CRC, byte_count at buf[6]
    if fc == 0x0F and len(buf) >= 7:
        return 7 + buf[6] + 2
    # FC16 (Write Multiple Regs): same layout
    if fc == 0x10 and len(buf) >= 7:
        return 7 + buf[6] + 2
    # FC08 (Diagnostics): fixed 8
    if fc == 0x08:
        return 8
    return None


def _response_expected_len(buf: bytes) -> Optional[int]:
    fc = buf[1]
    # FC01/02 response: 3 + byte_count + 2, byte_count at buf[2]
    if fc in (0x01, 0x02) and len(buf) >= 3:
        return 3 + buf[2] + 2
    # FC03/04 response: 3 + byte_count + 2, byte_count at buf[2]
    if fc in (0x03, 0x04) and len(buf) >= 3:
        return 3 + buf[2] + 2
    # FC05/06 response: echo of request = 8 bytes
    if fc in (0x05, 0x06):
        return 8
    # FC15/16 response: fixed 8 bytes
    if fc in (0x0F, 0x10):
        return 8
    # FC08 response: fixed 8
    if fc == 0x08:
        return 8
    return None


def _fm_expected_len(buf: bytes) -> Optional[int]:
    """Fast Modbus length by subcommand."""
    if len(buf) < 3:
        return None
    fc = buf[1]
    subcmd = buf[2]

    if fc == 0x46:
        lengths = {
            0x01: 9,   # FM Scan Start
            0x02: 9,   # FM Scan Continue
            0x03: 10,  # FM Scan Response (serial 4 + modbus addr 1 + 2 CRC = 10)
            0x04: 9,   # FM Scan End
            0x08: None,  # FM Cmd Send: variable
            0x09: None,  # FM Cmd Response: variable
            0x10: 9,   # FM Event Request
            0x11: None,  # FM Event Transfer: variable
            0x12: 5,   # FM Event Confirm
            0x18: None,  # FM Event Config: variable
        }
    else:  # 0x60 legacy
        lengths = {
            0x01: 5,  # Scan Start
            0x02: 5,  # Scan Continue
            0x03: 10, # Scan Response
            0x04: 5,  # Scan End
            0x08: None,  # FM Cmd Send
            0x09: None,  # FM Cmd Response
            0x10: 9,
            0x11: None,
            0x12: 5,
            0x18: None,
        }

    l = lengths.get(subcmd)
    if l is not None:
        return l

    # FM Cmd Send/Response (subcmd 0x08/0x09): serial(4) + inner modbus PDU
    # Variable — fall through to CRC scan
    return None


# ---------------------------------------------------------------------------
# Plausibility filter (used in CRC scan to reject false positives)
# ---------------------------------------------------------------------------

KNOWN_FC = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x08, 0x0F, 0x10, 0x46, 0x60}
RESERVED_SLAVE_LOW = {0x00}  # broadcast — keep it, master can send to 0
RESERVED_SLAVE_HIGH = set(range(0xF8, 0xFF))  # 0xF8-0xFE reserved; 0xFF sometimes used


def plausible(frame: bytes) -> bool:
    """Return True if this frame looks like a valid Modbus RTU frame beyond just CRC."""
    if len(frame) < 4:
        return False
    slave = frame[0]
    fc = frame[1] & 0x7F  # strip exception flag

    # 0xFD is FM broadcast — allowed
    if slave == 0xFD:
        return True

    # Reject obviously bad slave ids
    if slave in RESERVED_SLAVE_HIGH:
        return False

    # Function code must be known or at least in a plausible range
    if fc not in KNOWN_FC and not (0x01 <= fc <= 0x7F):
        return False

    # For FC03/04 responses: byte_count must be even and ≤ 250
    if fc in (0x03, 0x04) and len(frame) >= 3:
        bc = frame[2]
        if bc % 2 != 0 or bc > 250:
            # Could be a request — that's fine, requests don't have byte_count at [2]
            pass

    return True


# ---------------------------------------------------------------------------
# Core splitting logic
# ---------------------------------------------------------------------------

@dataclass
class SplitFrame:
    data: bytes
    method: str  # 'context', 'length_table', 'crc_scan'
    crc_valid: bool


def split_stream(buf: bytes, context_fc: Optional[int] = None,
                 context_slave: Optional[int] = None) -> list[SplitFrame]:
    """
    Split a byte stream into individual Modbus RTU frames.

    context_fc / context_slave: from the previous known request; used to
    compute the expected length of the first response frame (Level 1).
    """
    frames = []
    pos = 0

    first = True  # first frame in stream may be a response to context_fc

    while pos < len(buf):
        remaining = buf[pos:]
        if len(remaining) < 4:
            # Too short for any valid frame — give up
            break

        frame = None

        # --- Level 1: Context (only for first frame if we have context) ---
        if first and context_fc is not None and context_slave is not None:
            first = False
            # Try to determine response length based on known fc
            trial_buf = bytes([context_slave, context_fc]) + remaining[2:]
            # Actually buf[pos] should already start with slave/fc matching context
            # Use the actual buffer but with is_response=True hint
            exp = _response_expected_len(remaining) if remaining[1] == context_fc else None
            if exp is None:
                # Try with context fc at buf[1]
                trial = bytes([context_slave, context_fc]) + bytes(256)
                exp = _response_expected_len(trial)
            if exp is not None and exp <= len(remaining):
                candidate = remaining[:exp]
                if crc_ok(candidate) and plausible(candidate):
                    frame = SplitFrame(data=candidate, method='context', crc_valid=True)
        else:
            first = False

        # --- Level 2: Length table ---
        if frame is None:
            # Try request length first
            exp_req = _request_expected_len(remaining)
            if exp_req is not None and exp_req <= len(remaining):
                candidate = remaining[:exp_req]
                if crc_ok(candidate) and plausible(candidate):
                    frame = SplitFrame(data=candidate, method='length_table', crc_valid=True)

            # Try response length
            if frame is None:
                exp_res = _response_expected_len(remaining)
                if exp_res is not None and exp_res <= len(remaining) and exp_res != exp_req:
                    candidate = remaining[:exp_res]
                    if crc_ok(candidate) and plausible(candidate):
                        frame = SplitFrame(data=candidate, method='length_table', crc_valid=True)

            # FM length
            if frame is None and remaining[0] == 0xFD:
                exp_fm = _fm_expected_len(remaining)
                if exp_fm is not None and exp_fm <= len(remaining):
                    candidate = remaining[:exp_fm]
                    if crc_ok(candidate) and plausible(candidate):
                        frame = SplitFrame(data=candidate, method='length_table', crc_valid=True)

        # --- Level 3: CRC scan ---
        if frame is None:
            for trial_len in range(4, len(remaining) + 1):
                candidate = remaining[:trial_len]
                if crc_ok(candidate) and plausible(candidate):
                    frame = SplitFrame(data=candidate, method='crc_scan', crc_valid=True)
                    break

        if frame is None:
            # Cannot split further — emit rest as a single broken frame
            frame = SplitFrame(data=remaining, method='unknown', crc_valid=False)
            frames.append(frame)
            break

        frames.append(frame)
        pos += len(frame.data)

    return frames


# ---------------------------------------------------------------------------
# CSV processing
# ---------------------------------------------------------------------------

def parse_hex(hex_str: str) -> bytes:
    """Parse 'AA BB CC' style hex string to bytes."""
    hex_str = hex_str.strip()
    if not hex_str:
        return b''
    return bytes(int(x, 16) for x in hex_str.split())


FC_NAMES = {
    0x01: 'Read Coils',
    0x02: 'Read Discrete Inputs',
    0x03: 'Read Holding Regs',
    0x04: 'Read Input Regs',
    0x05: 'Write Single Coil',
    0x06: 'Write Single Reg',
    0x0F: 'Write Multiple Coils',
    0x10: 'Write Multiple Regs',
    0x46: 'Fast Modbus',
    0x60: 'Fast Modbus (legacy)',
}


def fc_name(fc: int) -> str:
    raw = fc & 0x7F
    exc = ' [EXCEPTION]' if fc & 0x80 else ''
    return FC_NAMES.get(raw, f'FC{raw:02X}') + exc


def describe_frame(f: bytes) -> str:
    if len(f) < 2:
        return f'  [{f.hex(" ")}] (too short)'
    slave = f[0]
    fc = f[1]
    crc = 'CRC OK' if crc_ok(f) else 'CRC ERR'
    raw = ' '.join(f'{b:02X}' for b in f)
    return f'  slave=0x{slave:02X} fc=0x{fc:02X}({fc_name(fc)}) len={len(f)} {crc} | {raw}'


def main():
    csv_path = 'errors.csv'
    total_err = 0
    total_split_ok = 0
    total_split_fail = 0

    # Track last known good request context
    last_req_slave: Optional[int] = None
    last_req_fc: Optional[int] = None

    with open(csv_path, newline='', encoding='utf-8') as f:
        reader = csv.DictReader(f)
        for row in reader:
            idx = row['#']
            sender = row['Sender'].strip()
            crc_col = row['CRC'].strip()
            raw_hex = row['Payload (HEX)'].strip()

            if sender in ('MASTER', 'SLAVE') and crc_col == 'OK':
                # Update context with the last known good request
                data = parse_hex(raw_hex)
                if sender == 'MASTER' and len(data) >= 2:
                    last_req_slave = data[0]
                    last_req_fc = data[1]
                continue

            if sender == 'ERR' or crc_col == 'ERR':
                total_err += 1
                data = parse_hex(raw_hex)
                if len(data) < 4:
                    print(f'[{idx}] ERR (too short, {len(data)}b): {raw_hex}')
                    total_split_fail += 1
                    continue

                frames = split_stream(data,
                                      context_fc=last_req_fc,
                                      context_slave=last_req_slave)

                all_ok = all(f.crc_valid for f in frames)
                reconstructed_len = sum(len(f.data) for f in frames)
                match_len = reconstructed_len == len(data)

                status = '✓ SPLIT OK' if (all_ok and match_len) else '✗ SPLIT PARTIAL'
                if all_ok and match_len:
                    total_split_ok += 1
                else:
                    total_split_fail += 1

                ctx_slave = f'{last_req_slave:02X}' if last_req_slave is not None else '??'
                ctx_fc    = f'{last_req_fc:02X}'    if last_req_fc    is not None else '??'
                print(f'[{idx}] {status}  (context: slave=0x{ctx_slave} fc=0x{ctx_fc})')
                for sf in frames:
                    crc_mark = 'CRC_OK' if sf.crc_valid else 'CRC_ERR'
                    raw = ' '.join(f'{b:02X}' for b in sf.data)
                    print(f'    [{sf.method:12s}] {crc_mark} len={len(sf.data):3d} | {raw}')

    print()
    print(f'=== SUMMARY ===')
    print(f'Total ERR rows:   {total_err}')
    print(f'Split OK:         {total_split_ok} ({100*total_split_ok//total_err if total_err else 0}%)')
    print(f'Split partial:    {total_split_fail}')


if __name__ == '__main__':
    main()
