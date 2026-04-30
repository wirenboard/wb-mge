'use strict';

// ============================================================
// Utilities
// ============================================================

function toHex(bytes) {
  return bytes.map(b => b.toString(16).toUpperCase().padStart(2, '0')).join('');
}

function u16be(b, i) { return ((b[i] << 8) | b[i + 1]) >>> 0; }
function u32be(b, i) { return ((b[i] << 24) | (b[i + 1] << 16) | (b[i + 2] << 8) | b[i + 3]) >>> 0; }

/**
 * Parse hex string (with or without spaces) into byte array.
 * Returns null on error.
 */
function parseHex(str) {
  if (typeof str !== 'string') return null;
  const clean = str.replace(/\s+/g, '');
  if (clean.length === 0 || clean.length % 2 !== 0) return null;
  const result = [];
  for (let i = 0; i < clean.length; i += 2) {
    const b = parseInt(clean.slice(i, i + 2), 16);
    if (isNaN(b)) return null;
    result.push(b);
  }
  return result;
}

// ============================================================
// PDU layer decoders
// ============================================================

const FC_NAMES = {
  1: 'read_coils',
  2: 'read_discrete_inputs',
  3: 'read_holding_registers',
  4: 'read_input_registers',
  5: 'write_single_coil',
  6: 'write_single_register',
  15: 'write_multiple_coils',
  16: 'write_multiple_registers',
};

/**
 * Decode Modbus PDU request (inside command_by_serial, bytes starting at fc byte).
 * Returns structured object or parse_error.
 */
function decodePduRequest(bytes) {
  if (bytes.length < 1) return { type: 'parse_error', reason: 'pdu_empty', raw: '' };
  const fc = bytes[0];
  const fcHex = toHex([fc]);

  // Error response (bit 7 set)
  if (fc & 0x80) {
    const origFc = fc & 0x7F;
    if (bytes.length < 2) return { type: 'parse_error', reason: 'pdu_too_short', raw: toHex(bytes) };
    return {
      type: 'modbus_error',
      raw: toHex(bytes),
      fc: fcHex,
      original_fc: toHex([origFc]),
      error_code: bytes[1],
    };
  }

  // Read registers (fc 1,2,3,4): fc reg_hi reg_lo cnt_hi cnt_lo
  if (fc >= 1 && fc <= 4) {
    if (bytes.length < 5) return { type: 'parse_error', reason: 'pdu_too_short', raw: toHex(bytes) };
    return {
      type: FC_NAMES[fc],
      raw: toHex(bytes.slice(0, 5)),
      fc: fcHex,
      register: toHex(bytes.slice(1, 3)),
      count: u16be(bytes, 3),
    };
  }

  // Write single coil/register (fc 5,6): fc reg_hi reg_lo val_hi val_lo
  if (fc === 5 || fc === 6) {
    if (bytes.length < 5) return { type: 'parse_error', reason: 'pdu_too_short', raw: toHex(bytes) };
    return {
      type: FC_NAMES[fc],
      raw: toHex(bytes.slice(0, 5)),
      fc: fcHex,
      register: toHex(bytes.slice(1, 3)),
      value: u16be(bytes, 3),
    };
  }

  // Write multiple (fc 15,16): fc reg_hi reg_lo cnt_hi cnt_lo byte_count data...
  if (fc === 15 || fc === 16) {
    if (bytes.length < 6) return { type: 'parse_error', reason: 'pdu_too_short', raw: toHex(bytes) };
    const byteCount = bytes[5];
    if (bytes.length < 6 + byteCount) return { type: 'parse_error', reason: 'pdu_data_truncated', raw: toHex(bytes) };
    const data = bytes.slice(6, 6 + byteCount);
    return {
      type: FC_NAMES[fc],
      raw: toHex(bytes.slice(0, 6 + byteCount)),
      fc: fcHex,
      register: toHex(bytes.slice(1, 3)),
      count: u16be(bytes, 3),
      byte_count: byteCount,
      data: toHex(data),
    };
  }

  return { type: 'parse_error', reason: 'unknown_fc', raw: toHex(bytes), fc: fcHex };
}

/**
 * Decode Modbus PDU response (inside response_by_serial, bytes starting at fc byte).
 */
function decodePduResponse(bytes) {
  if (bytes.length < 1) return { type: 'parse_error', reason: 'pdu_empty', raw: '' };
  const fc = bytes[0];
  const fcHex = toHex([fc]);

  // Error response (bit 7 set)
  if (fc & 0x80) {
    const origFc = fc & 0x7F;
    if (bytes.length < 2) return { type: 'parse_error', reason: 'pdu_too_short', raw: toHex(bytes) };
    return {
      type: 'modbus_error',
      raw: toHex(bytes.slice(0, 2)),
      fc: fcHex,
      original_fc: toHex([origFc]),
      error_code: bytes[1],
    };
  }

  // Read response (fc 1,2,3,4): fc byte_count data...
  if (fc >= 1 && fc <= 4) {
    if (bytes.length < 2) return { type: 'parse_error', reason: 'pdu_too_short', raw: toHex(bytes) };
    const byteCount = bytes[1];
    if (bytes.length < 2 + byteCount) return { type: 'parse_error', reason: 'pdu_data_truncated', raw: toHex(bytes) };
    const data = bytes.slice(2, 2 + byteCount);
    return {
      type: FC_NAMES[fc] + '_response',
      raw: toHex(bytes.slice(0, 2 + byteCount)),
      fc: fcHex,
      byte_count: byteCount,
      data: toHex(data),
    };
  }

  // Write single coil/register echo (fc 5,6): fc reg_hi reg_lo val_hi val_lo
  if (fc === 5 || fc === 6) {
    if (bytes.length < 5) return { type: 'parse_error', reason: 'pdu_too_short', raw: toHex(bytes) };
    return {
      type: FC_NAMES[fc] + '_response',
      raw: toHex(bytes.slice(0, 5)),
      fc: fcHex,
      register: toHex(bytes.slice(1, 3)),
      value: u16be(bytes, 3),
    };
  }

  // Write multiple echo (fc 15,16): fc reg_hi reg_lo cnt_hi cnt_lo
  if (fc === 15 || fc === 16) {
    if (bytes.length < 5) return { type: 'parse_error', reason: 'pdu_too_short', raw: toHex(bytes) };
    return {
      type: FC_NAMES[fc] + '_response',
      raw: toHex(bytes.slice(0, 5)),
      fc: fcHex,
      register: toHex(bytes.slice(1, 3)),
      count: u16be(bytes, 3),
    };
  }

  return { type: 'parse_error', reason: 'unknown_fc', raw: toHex(bytes), fc: fcHex };
}

// ============================================================
// Fast Modbus subcommand layer (bytes after ext_byte)
// ============================================================

/**
 * Decode fast modbus payload: bytes starting at subcommand byte (index 0 = subcommand).
 * rtuPayloadBytes = all bytes after address and ext_byte, including subcommand and CRC.
 * crcBytes = last 2 bytes of RTU frame (already extracted).
 */
function decodeFastModbusPayload(bytes) {
  // bytes = everything after address + ext_byte, up to (not including) CRC
  if (bytes.length < 1) return { type: 'parse_error', reason: 'subcommand_missing', raw: '' };
  const sub = bytes[0];
  const subHex = toHex([sub]);
  const raw = toHex(bytes);

  // scan_start: just subcommand byte
  if (sub === 0x01) {
    return { type: 'scan_start', raw };
  }

  // scan_continue: just subcommand byte
  if (sub === 0x02) {
    return { type: 'scan_continue', raw };
  }

  // scan_response: sub(1) + serial(4) + modbus_address(1) = 6 bytes
  if (sub === 0x03) {
    if (bytes.length < 6) return { type: 'parse_error', reason: 'scan_response_too_short', raw };
    return {
      type: 'scan_response',
      raw,
      serial_number: toHex(bytes.slice(1, 5)),
      modbus_address: toHex([bytes[5]]),
    };
  }

  // scan_end: just subcommand byte
  if (sub === 0x04) {
    return { type: 'scan_end', raw };
  }

  // command_by_serial (0x08): sub(1) + serial(4) + PDU
  if (sub === 0x08) {
    if (bytes.length < 6) return { type: 'parse_error', reason: 'command_by_serial_too_short', raw };
    const serial = toHex(bytes.slice(1, 5));
    const pduBytes = bytes.slice(5);
    return {
      type: 'command_by_serial',
      raw,
      serial_number: serial,
      payload: decodePduRequest(pduBytes),
    };
  }

  // response_by_serial (0x09): sub(1) + serial(4) + PDU
  if (sub === 0x09) {
    if (bytes.length < 6) return { type: 'parse_error', reason: 'response_by_serial_too_short', raw };
    const serial = toHex(bytes.slice(1, 5));
    const pduBytes = bytes.slice(5);
    return {
      type: 'response_by_serial',
      raw,
      serial_number: serial,
      payload: decodePduResponse(pduBytes),
    };
  }

  return { type: 'parse_error', reason: 'unknown_subcommand', raw, subcommand: subHex };
}

// ============================================================
// Top-level decoder
// ============================================================

/**
 * Decode a Fast Modbus packet from hex string or byte array.
 *
 * Input: hex string (with or without spaces) OR Uint8Array/Array of bytes
 * Output: structured JSON object
 *
 * Top-level types:
 *   arbitration   — all-FF bytes
 *   rtu_frame     — valid RTU frame (address + fast_modbus payload + CRC)
 *   parse_error   — could not parse
 */
function decodePacket(input) {
  let bytes;
  if (typeof input === 'string') {
    bytes = parseHex(input);
    if (bytes === null) return { type: 'parse_error', reason: 'invalid_hex', raw: input };
  } else if (Array.isArray(input) || (input && typeof input === 'object' && input.constructor && input.constructor.name === 'Uint8Array')) {
    bytes = Array.from(input);
  } else {
    return { type: 'parse_error', reason: 'invalid_input', raw: String(input) };
  }

  if (bytes.length === 0) {
    return { type: 'parse_error', reason: 'empty', raw: '' };
  }

  const raw = toHex(bytes);

  // Arbitration: all bytes are 0xFF
  if (bytes.every(b => b === 0xFF)) {
    return { type: 'arbitration', raw };
  }

  // Minimum RTU frame: address(1) + ext_byte(1) + subcommand(1) + CRC(2) = 5 bytes
  if (bytes.length < 5) {
    return { type: 'parse_error', reason: 'too_short', raw };
  }

  const address = bytes[0];
  if (address !== 0xFD) {
    return { type: 'parse_error', reason: 'wrong_address', raw, address: toHex([address]) };
  }

  const extByte = bytes[1];
  if (extByte !== 0x60 && extByte !== 0x46) {
    return { type: 'parse_error', reason: 'wrong_ext_byte', raw, ext_byte: toHex([extByte]) };
  }

  const crc = toHex(bytes.slice(-2));
  // payload between ext_byte and CRC
  const fmBytes = bytes.slice(2, bytes.length - 2);

  return {
    type: 'rtu_frame',
    raw,
    address: toHex([address]),
    crc,
    payload: {
      type: 'fast_modbus',
      raw: toHex(bytes.slice(1, bytes.length - 2)),
      ext_byte: toHex([extByte]),
      payload: decodeFastModbusPayload(fmBytes),
    },
  };
}

// ============================================================
// Export
// ============================================================

if (typeof module !== 'undefined' && module.exports) {
  module.exports = { decodePacket, parseHex };
}
