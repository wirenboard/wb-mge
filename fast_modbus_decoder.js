'use strict';

// ============================================================
// Utilities
// ============================================================

function toHex(bytes) {
  return bytes.map(b => b.toString(16).toUpperCase().padStart(2, '0')).join('');
}

function u16be(b, i) { return ((b[i] << 8) | b[i + 1]) >>> 0; }

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
// FC classification
// ============================================================

const FC_NAMES = {
  0x01: 'read_coils',
  0x02: 'read_discrete_inputs',
  0x03: 'read_holding_registers',
  0x04: 'read_input_registers',
  0x05: 'write_single_coil',
  0x06: 'write_single_register',
  0x07: 'read_exception_status',
  0x08: 'diagnostics',
  0x0B: 'get_comm_event_counter',
  0x0C: 'get_comm_event_log',
  0x0F: 'write_multiple_coils',
  0x10: 'write_multiple_registers',
  0x11: 'report_server_id',
  0x14: 'read_file_record',
  0x15: 'write_file_record',
  0x16: 'mask_write_register',
  0x17: 'read_write_multiple_registers',
  0x18: 'read_fifo_queue',
  0x2B: 'mei_transport',
};

// User-defined FC ranges per spec: 65-72 (0x41-0x48), 100-110 (0x64-0x6E)
function isUserDefinedFc(fc) {
  return (fc >= 0x41 && fc <= 0x48) || (fc >= 0x64 && fc <= 0x6E);
}

// Known vendor-specific FCs (Schneider UMAS etc.)
const VENDOR_FC = new Set([0x5A, 0x5B]);

// ============================================================
// Standard Modbus PDU decoder (request)
// ============================================================

function decodeStdPduRequest(bytes) {
  if (bytes.length < 1) return { type: 'parse_error', reason: 'pdu_empty', raw: '' };
  const fc = bytes[0];
  const fcHex = toHex([fc]);
  const raw = toHex(bytes);

  if (fc === 0x00) return { type: 'parse_error', reason: 'invalid_fc', raw, fc: fcHex };

  // Exception response — shouldn't appear in request but handle it
  if (fc & 0x80) {
    if (bytes.length < 2) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    return { type: 'modbus_error', raw: toHex(bytes.slice(0, 2)), fc: fcHex, original_fc: toHex([fc & 0x7F]), error_code: bytes[1] };
  }

  // FC 01-04: read (fc + start(2) + count(2))
  if (fc >= 0x01 && fc <= 0x04) {
    if (bytes.length < 5) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    return { type: FC_NAMES[fc], raw: toHex(bytes.slice(0, 5)), fc: fcHex, register: toHex(bytes.slice(1, 3)), count: u16be(bytes, 3) };
  }

  // FC 05, 06: write single (fc + addr(2) + value(2))
  if (fc === 0x05 || fc === 0x06) {
    if (bytes.length < 5) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    return { type: FC_NAMES[fc], raw: toHex(bytes.slice(0, 5)), fc: fcHex, register: toHex(bytes.slice(1, 3)), value: u16be(bytes, 3) };
  }

  // FC 07, 0B, 0C, 11: single-byte request (just FC)
  if (fc === 0x07 || fc === 0x0B || fc === 0x0C || fc === 0x11) {
    return { type: FC_NAMES[fc], raw: toHex(bytes.slice(0, 1)), fc: fcHex };
  }

  // FC 08: diagnostics (fc + sub(2) + data(2))
  if (fc === 0x08) {
    if (bytes.length < 5) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    return { type: FC_NAMES[fc], raw: toHex(bytes.slice(0, 5)), fc: fcHex, sub_function: toHex(bytes.slice(1, 3)), data: toHex(bytes.slice(3, 5)) };
  }

  // FC 0F, 10: write multiple (fc + start(2) + qty(2) + bytecount(1) + data)
  if (fc === 0x0F || fc === 0x10) {
    if (bytes.length < 6) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    const byteCount = bytes[5];
    if (bytes.length < 6 + byteCount) return { type: 'parse_error', reason: 'pdu_data_truncated', raw };
    return {
      type: FC_NAMES[fc], raw: toHex(bytes.slice(0, 6 + byteCount)), fc: fcHex,
      register: toHex(bytes.slice(1, 3)), count: u16be(bytes, 3), byte_count: byteCount, data: toHex(bytes.slice(6, 6 + byteCount)),
    };
  }

  // FC 14: read file record (fc + bytecount(1) + sub-requests)
  if (fc === 0x14) {
    if (bytes.length < 2) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    const byteCount = bytes[1];
    if (bytes.length < 2 + byteCount) return { type: 'parse_error', reason: 'pdu_data_truncated', raw };
    const subReqs = [];
    let off = 2;
    while (off + 7 <= 2 + byteCount) {
      subReqs.push({ reference_type: toHex([bytes[off]]), file_number: u16be(bytes, off + 1), record_number: u16be(bytes, off + 3), record_length: u16be(bytes, off + 5) });
      off += 7;
    }
    return { type: FC_NAMES[fc], raw: toHex(bytes.slice(0, 2 + byteCount)), fc: fcHex, byte_count: byteCount, sub_requests: subReqs };
  }

  // FC 15: write file record (fc + data_len(1) + sub-requests with data)
  if (fc === 0x15) {
    if (bytes.length < 2) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    const dataLen = bytes[1];
    if (bytes.length < 2 + dataLen) return { type: 'parse_error', reason: 'pdu_data_truncated', raw };
    const subReqs = [];
    let off = 2;
    while (off + 7 <= 2 + dataLen) {
      const recLen = u16be(bytes, off + 5);
      const data = bytes.slice(off + 7, off + 7 + recLen * 2);
      subReqs.push({ reference_type: toHex([bytes[off]]), file_number: u16be(bytes, off + 1), record_number: u16be(bytes, off + 3), record_length: recLen, data: toHex(data) });
      off += 7 + recLen * 2;
    }
    return { type: FC_NAMES[fc], raw: toHex(bytes.slice(0, 2 + dataLen)), fc: fcHex, request_data_length: dataLen, sub_requests: subReqs };
  }

  // FC 16: mask write register (fc + addr(2) + and(2) + or(2))
  if (fc === 0x16) {
    if (bytes.length < 7) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    return { type: FC_NAMES[fc], raw: toHex(bytes.slice(0, 7)), fc: fcHex, register: toHex(bytes.slice(1, 3)), and_mask: toHex(bytes.slice(3, 5)), or_mask: toHex(bytes.slice(5, 7)) };
  }

  // FC 17: read/write multiple registers (fc + read_start(2) + read_qty(2) + write_start(2) + write_qty(2) + write_bc(1) + data)
  if (fc === 0x17) {
    if (bytes.length < 10) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    const writeByteCount = bytes[9];
    if (bytes.length < 10 + writeByteCount) return { type: 'parse_error', reason: 'pdu_data_truncated', raw };
    return {
      type: FC_NAMES[fc], raw: toHex(bytes.slice(0, 10 + writeByteCount)), fc: fcHex,
      read_register: toHex(bytes.slice(1, 3)), read_count: u16be(bytes, 3),
      write_register: toHex(bytes.slice(5, 7)), write_count: u16be(bytes, 7),
      write_byte_count: writeByteCount, write_data: toHex(bytes.slice(10, 10 + writeByteCount)),
    };
  }

  // FC 18: read FIFO queue (fc + pointer(2))
  if (fc === 0x18) {
    if (bytes.length < 3) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    return { type: FC_NAMES[fc], raw: toHex(bytes.slice(0, 3)), fc: fcHex, fifo_pointer: toHex(bytes.slice(1, 3)) };
  }

  // FC 0x2B: MEI transport
  if (fc === 0x2B) {
    if (bytes.length < 2) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    const meiType = bytes[1];
    const meiHex = toHex([meiType]);
    if (meiType === 0x0E) {
      if (bytes.length < 4) return { type: 'parse_error', reason: 'pdu_too_short', raw };
      return { type: 'mei_read_device_identification', raw, fc: fcHex, mei_type: meiHex, read_device_id_code: toHex([bytes[2]]), object_id: toHex([bytes[3]]) };
    }
    if (meiType === 0x0D) return { type: 'mei_canopen', raw, fc: fcHex, mei_type: meiHex, data: toHex(bytes.slice(2)) };
    return { type: 'mei_transport', raw, fc: fcHex, mei_type: meiHex, data: toHex(bytes.slice(2)) };
  }

  if (isUserDefinedFc(fc)) return { type: 'user_defined', raw, fc: fcHex, data: toHex(bytes.slice(1)) };
  if (VENDOR_FC.has(fc)) return { type: 'vendor_specific', raw, fc: fcHex, data: toHex(bytes.slice(1)) };

  return { type: 'parse_error', reason: 'unknown_fc', raw, fc: fcHex };
}

// ============================================================
// Standard Modbus PDU decoder (response)
// ============================================================

function decodeStdPduResponse(bytes) {
  if (bytes.length < 1) return { type: 'parse_error', reason: 'pdu_empty', raw: '' };
  const fc = bytes[0];
  const fcHex = toHex([fc]);
  const raw = toHex(bytes);

  if (fc === 0x00) return { type: 'parse_error', reason: 'invalid_fc', raw, fc: fcHex };

  // Exception response (bit 7 set)
  if (fc & 0x80) {
    if (bytes.length < 2) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    return { type: 'modbus_error', raw: toHex(bytes.slice(0, 2)), fc: fcHex, original_fc: toHex([fc & 0x7F]), error_code: bytes[1] };
  }

  // FC 01-04: read response (fc + byte_count(1) + data)
  if (fc >= 0x01 && fc <= 0x04) {
    if (bytes.length < 2) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    const byteCount = bytes[1];
    if (bytes.length < 2 + byteCount) return { type: 'parse_error', reason: 'pdu_data_truncated', raw };
    return { type: FC_NAMES[fc] + '_response', raw: toHex(bytes.slice(0, 2 + byteCount)), fc: fcHex, byte_count: byteCount, data: toHex(bytes.slice(2, 2 + byteCount)) };
  }

  // FC 05, 06: echo (fc + addr(2) + value(2))
  if (fc === 0x05 || fc === 0x06) {
    if (bytes.length < 5) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    return { type: FC_NAMES[fc] + '_response', raw: toHex(bytes.slice(0, 5)), fc: fcHex, register: toHex(bytes.slice(1, 3)), value: u16be(bytes, 3) };
  }

  // FC 07: read exception status response (fc + 1 byte)
  if (fc === 0x07) {
    if (bytes.length < 2) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    return { type: 'read_exception_status_response', raw: toHex(bytes.slice(0, 2)), fc: fcHex, output_data: toHex([bytes[1]]) };
  }

  // FC 08: diagnostics response (fc + sub(2) + data(2))
  if (fc === 0x08) {
    if (bytes.length < 5) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    return { type: 'diagnostics_response', raw: toHex(bytes.slice(0, 5)), fc: fcHex, sub_function: toHex(bytes.slice(1, 3)), data: toHex(bytes.slice(3, 5)) };
  }

  // FC 0B: get comm event counter response (fc + status(2) + event_count(2))
  if (fc === 0x0B) {
    if (bytes.length < 5) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    return { type: 'get_comm_event_counter_response', raw: toHex(bytes.slice(0, 5)), fc: fcHex, status: toHex(bytes.slice(1, 3)), event_count: u16be(bytes, 3) };
  }

  // FC 0C: get comm event log response (fc + bytecount(1) + status(2) + event_count(2) + message_count(2) + events)
  if (fc === 0x0C) {
    if (bytes.length < 2) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    const byteCount = bytes[1];
    if (bytes.length < 2 + byteCount) return { type: 'parse_error', reason: 'pdu_data_truncated', raw };
    const events = bytes.slice(8, 2 + byteCount);
    return {
      type: 'get_comm_event_log_response', raw: toHex(bytes.slice(0, 2 + byteCount)), fc: fcHex,
      byte_count: byteCount, status: toHex(bytes.slice(2, 4)), event_count: u16be(bytes, 4),
      message_count: u16be(bytes, 6), events: toHex(events),
    };
  }

  // FC 0F, 10: write multiple response (fc + start(2) + qty(2))
  if (fc === 0x0F || fc === 0x10) {
    if (bytes.length < 5) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    return { type: FC_NAMES[fc] + '_response', raw: toHex(bytes.slice(0, 5)), fc: fcHex, register: toHex(bytes.slice(1, 3)), count: u16be(bytes, 3) };
  }

  // FC 11: report server ID response (fc + bytecount(1) + server_id(1) + run(1) + additional)
  if (fc === 0x11) {
    if (bytes.length < 2) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    const byteCount = bytes[1];
    if (bytes.length < 2 + byteCount) return { type: 'parse_error', reason: 'pdu_data_truncated', raw };
    return {
      type: 'report_server_id_response', raw: toHex(bytes.slice(0, 2 + byteCount)), fc: fcHex,
      byte_count: byteCount, server_id: toHex([bytes[2]]), run_indicator: toHex([bytes[3]]),
      additional_data: byteCount > 2 ? toHex(bytes.slice(4, 2 + byteCount)) : '',
    };
  }

  // FC 14: read file record response (fc + resp_data_len(1) + sub-responses)
  if (fc === 0x14) {
    if (bytes.length < 2) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    const respDataLen = bytes[1];
    if (bytes.length < 2 + respDataLen) return { type: 'parse_error', reason: 'pdu_data_truncated', raw };
    const subResps = [];
    let off = 2;
    while (off < 2 + respDataLen) {
      const fileRespLen = bytes[off];
      const refType = toHex([bytes[off + 1]]);
      const dataLen = fileRespLen - 1;
      subResps.push({ file_resp_length: fileRespLen, reference_type: refType, data: toHex(bytes.slice(off + 2, off + 2 + dataLen)) });
      off += 1 + fileRespLen;
    }
    return { type: 'read_file_record_response', raw: toHex(bytes.slice(0, 2 + respDataLen)), fc: fcHex, resp_data_length: respDataLen, sub_responses: subResps };
  }

  // FC 16: mask write register response (echo: fc + addr(2) + and(2) + or(2))
  if (fc === 0x16) {
    if (bytes.length < 7) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    return { type: 'mask_write_register_response', raw: toHex(bytes.slice(0, 7)), fc: fcHex, register: toHex(bytes.slice(1, 3)), and_mask: toHex(bytes.slice(3, 5)), or_mask: toHex(bytes.slice(5, 7)) };
  }

  // FC 17: read/write multiple response (fc + bytecount(1) + data)
  if (fc === 0x17) {
    if (bytes.length < 2) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    const byteCount = bytes[1];
    if (bytes.length < 2 + byteCount) return { type: 'parse_error', reason: 'pdu_data_truncated', raw };
    return { type: 'read_write_multiple_registers_response', raw: toHex(bytes.slice(0, 2 + byteCount)), fc: fcHex, byte_count: byteCount, data: toHex(bytes.slice(2, 2 + byteCount)) };
  }

  // FC 18: read FIFO queue response (fc + bytecount(2 BE!) + fifo_count(2) + data)
  if (fc === 0x18) {
    if (bytes.length < 5) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    const byteCount = u16be(bytes, 1); // 2-byte field
    const fifoCount = u16be(bytes, 3);
    if (bytes.length < 5 + fifoCount * 2) return { type: 'parse_error', reason: 'pdu_data_truncated', raw };
    return { type: 'read_fifo_queue_response', raw: toHex(bytes.slice(0, 5 + fifoCount * 2)), fc: fcHex, byte_count: byteCount, fifo_count: fifoCount, data: toHex(bytes.slice(5, 5 + fifoCount * 2)) };
  }

  // FC 0x2B: MEI response
  if (fc === 0x2B) {
    if (bytes.length < 2) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    const meiType = bytes[1];
    const meiHex = toHex([meiType]);
    if (meiType === 0x0E) {
      if (bytes.length < 7) return { type: 'parse_error', reason: 'pdu_too_short', raw };
      const numObjects = bytes[6];
      const objects = [];
      let off = 7;
      for (let i = 0; i < numObjects && off + 2 <= bytes.length; i++) {
        const objId = toHex([bytes[off]]);
        const objLen = bytes[off + 1];
        objects.push({ id: objId, value: toHex(bytes.slice(off + 2, off + 2 + objLen)) });
        off += 2 + objLen;
      }
      return {
        type: 'mei_read_device_identification_response', raw, fc: fcHex, mei_type: meiHex,
        read_device_id_code: toHex([bytes[2]]), conformity_level: toHex([bytes[3]]),
        more_follows: toHex([bytes[4]]), next_object_id: toHex([bytes[5]]),
        number_of_objects: numObjects, objects,
      };
    }
    if (meiType === 0x0D) return { type: 'mei_canopen', raw, fc: fcHex, mei_type: meiHex, data: toHex(bytes.slice(2)) };
    return { type: 'mei_transport', raw, fc: fcHex, mei_type: meiHex, data: toHex(bytes.slice(2)) };
  }

  if (isUserDefinedFc(fc)) return { type: 'user_defined', raw, fc: fcHex, data: toHex(bytes.slice(1)) };
  if (VENDOR_FC.has(fc)) return { type: 'vendor_specific', raw, fc: fcHex, data: toHex(bytes.slice(1)) };

  return { type: 'parse_error', reason: 'unknown_fc', raw, fc: fcHex };
}

// ============================================================
// Fast Modbus subcommand layer (bytes after ext_byte, before CRC)
// ============================================================

function decodeFastModbusPayload(bytes) {
  if (bytes.length < 1) return { type: 'parse_error', reason: 'subcommand_missing', raw: '' };
  const sub = bytes[0];
  const subHex = toHex([sub]);
  const raw = toHex(bytes);

  if (sub === 0x01) return { type: 'scan_start', raw };
  if (sub === 0x02) return { type: 'scan_continue', raw };

  if (sub === 0x03) {
    if (bytes.length < 6) return { type: 'parse_error', reason: 'scan_response_too_short', raw };
    return { type: 'scan_response', raw, serial_number: toHex(bytes.slice(1, 5)), modbus_address: toHex([bytes[5]]) };
  }

  if (sub === 0x04) return { type: 'scan_end', raw };

  if (sub === 0x08) {
    if (bytes.length < 6) return { type: 'parse_error', reason: 'command_by_serial_too_short', raw };
    return { type: 'command_by_serial', raw, serial_number: toHex(bytes.slice(1, 5)), payload: decodeStdPduRequest(bytes.slice(5)) };
  }

  if (sub === 0x09) {
    if (bytes.length < 6) return { type: 'parse_error', reason: 'response_by_serial_too_short', raw };
    return { type: 'response_by_serial', raw, serial_number: toHex(bytes.slice(1, 5)), payload: decodeStdPduResponse(bytes.slice(5)) };
  }

  return { type: 'parse_error', reason: 'unknown_subcommand', raw, subcommand: subHex };
}

// ============================================================
// Top-level decoder
// ============================================================

/**
 * Decode a Modbus packet from hex string or byte array.
 *
 * @param {string|number[]} input  hex string (spaces optional) or byte array
 * @param {string} [direction]     'request' | 'response' (optional, default 'response')
 *
 * Output types:
 *   arbitration   — all 0xFF bytes
 *   rtu_frame     — valid RTU frame:
 *     payload.type = 'fast_modbus' for address=0xFD + ext_byte 0x60|0x46
 *     payload.type = 'modbus_rtu' for standard Modbus RTU
 *   parse_error   — could not parse
 *
 * rtu_frame.payload.payload contains the decoded PDU/subcommand.
 */
function decodePacket(input, direction) {
  let bytes;
  if (typeof input === 'string') {
    bytes = parseHex(input);
    if (bytes === null) return { type: 'parse_error', reason: 'invalid_hex', raw: input };
  } else if (Array.isArray(input) || (input && typeof input === 'object' && input.constructor && input.constructor.name === 'Uint8Array')) {
    bytes = Array.from(input);
  } else {
    return { type: 'parse_error', reason: 'invalid_input', raw: String(input) };
  }

  if (bytes.length === 0) return { type: 'parse_error', reason: 'empty', raw: '' };

  const raw = toHex(bytes);

  // Arbitration: all bytes are 0xFF
  if (bytes.every(b => b === 0xFF)) return { type: 'arbitration', raw };

  // Minimum RTU frame: address(1) + fc(1) + CRC(2) = 4 bytes
  if (bytes.length < 4) return { type: 'parse_error', reason: 'too_short', raw };

  const address = bytes[0];
  const addressHex = toHex([address]);
  const crc = toHex(bytes.slice(-2));

  // Fast Modbus: address=0xFD, second byte is ext_byte (0x60 or 0x46)
  if (address === 0xFD && (bytes[1] === 0x60 || bytes[1] === 0x46)) {
    const extByte = bytes[1];
    const fmBytes = bytes.slice(2, bytes.length - 2);
    return {
      type: 'rtu_frame', raw, address: addressHex, crc,
      payload: {
        type: 'fast_modbus',
        raw: toHex(bytes.slice(1, bytes.length - 2)),
        ext_byte: toHex([extByte]),
        payload: decodeFastModbusPayload(fmBytes),
      },
    };
  }

  // Standard Modbus RTU frame
  const result = { type: 'rtu_frame', raw, address: addressHex, crc };

  // Reserved slave address range 0xF8-0xFF (248-255)
  if (address >= 0xF8) result.reserved_address = true;

  const pduBytes = bytes.slice(1, bytes.length - 2);
  const decodePdu = direction === 'request' ? decodeStdPduRequest : decodeStdPduResponse;

  result.payload = {
    type: 'modbus_rtu',
    raw: toHex(pduBytes),
    payload: decodePdu(pduBytes),
  };

  return result;
}

// ============================================================
// Export
// ============================================================

if (typeof module !== 'undefined' && module.exports) {
  module.exports = { decodePacket, parseHex };
}
