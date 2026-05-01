/**
 * Modbus RTU + Fast Modbus packet decoder.
 *
 * Pure functions — no DOM, no Vue, no side effects.
 * Input: hex string (with or without spaces) + optional direction.
 * Output: structured nested object describing each protocol layer.
 *
 * Structure:
 *   {type:'rtu_frame', address, crc, payload: {type:'fast_modbus'|'modbus_rtu', payload: <pdu>}}
 *   {type:'arbitration', raw}
 *   {type:'parse_error', reason, raw}
 */

// ============================================================
// Types
// ============================================================

export type Direction = 'request' | 'response';

export interface ParseError {
  type: 'parse_error';
  reason: string;
  raw: string;
  [key: string]: unknown;
}

export interface Arbitration {
  type: 'arbitration';
  raw: string;
}

export interface RtuFrame {
  type: 'rtu_frame';
  raw: string;
  address: string;
  crc: string;
  reserved_address?: true;
  payload: FastModbusPayload | ModbusRtuPayload;
}

export interface FastModbusPayload {
  type: 'fast_modbus';
  raw: string;
  ext_byte: string;
  subcommand: string;  // hex byte of the subcommand (e.g. '03' for scan_response)
  payload: FastModbusSubcommand | ParseError;
}

export interface ModbusRtuPayload {
  type: 'modbus_rtu';
  raw: string;
  payload: PduResult | ParseError;
}

export type FastModbusSubcommand =
  | { type: 'scan_start'; raw: string; }
  | { type: 'scan_continue'; raw: string; }
  | { type: 'scan_response'; raw: string; serial_number: string; modbus_address: string; }
  | { type: 'scan_end'; raw: string; }
  | { type: 'command_by_serial'; raw: string; serial_number: string; function_code: string; payload: PduResult | ParseError; }
  | { type: 'response_by_serial'; raw: string; serial_number: string; function_code: string; payload: PduResult | ParseError; }
  | ParseError;

export interface PduResult {
  type: string;
  raw: string;
  fc: string;
  [key: string]: unknown;
}

export type DecodedPacket = RtuFrame | Arbitration | ParseError;

/**
 * Semantic role of a single byte in a packet, used for colour-coding.
 * 'fm-addr' / 'fm-ext' / 'fm-subcommand' = Fast Modbus wrapper fields (fake/indirect).
 * 'address' / 'fc' / 'subcommand' = real protocol fields.
 */
export type ByteRole =
  | 'address'       // real slave address (std Modbus, or FM broadcast for non-nested FM cmds)
  | 'fc'            // real function code (std Modbus inner FC inside FM nested command)
  | 'subcommand'    // FM subcommand byte when it's a "real" leaf command (no nesting)
  | 'serial'        // FM 4-byte serial number
  | 'data'          // payload data bytes
  | 'crc'           // CRC bytes
  | 'arbitration'   // FM bus arbitration (all 0xFF)
  | 'fm-addr'       // FM wrapper address (0xFD) — fake, present only when nested command exists
  | 'fm-ext'        // FM ext_byte (0x60/0x46) — fake, present only when nested command exists
  | 'fm-subcommand' // FM subcommand byte when it wraps another command (command/response by serial)
  | 'unknown';

/**
 * Return per-byte semantic roles for a decoded packet.
 * The roles array has one entry per byte in the original raw packet.
 */
export function getByteRoles(decoded: DecodedPacket): ByteRole[] {
  if (decoded.type === 'arbitration') {
    const n = decoded.raw.length / 2;
    return new Array(n).fill('arbitration' as ByteRole);
  }

  if (decoded.type !== 'rtu_frame') {
    const n = decoded.raw ? decoded.raw.length / 2 : 0;
    return new Array(n).fill('unknown' as ByteRole);
  }

  const n = decoded.raw.length / 2;
  const roles: ByteRole[] = new Array(n).fill('unknown' as ByteRole);

  // CRC is always the last 2 bytes
  roles[n - 2] = 'crc';
  roles[n - 1] = 'crc';

  const pl = decoded.payload;

  if (pl.type === 'fast_modbus') {
    const sub = pl.payload;
    const hasNestedCommand = sub.type === 'command_by_serial' || sub.type === 'response_by_serial';

    // addr (0xFD) and ext_byte (0x60/0x46) are ALWAYS fake wrappers in FM
    roles[0] = 'fm-addr';
    roles[1] = 'fm-ext';

    if (hasNestedCommand) {
      // subcommand 0x08/0x09 is also a fake wrapper
      roles[2] = 'fm-subcommand';

      // bytes 3..6 = serial number (real device identifier)
      for (let i = 3; i <= 6 && i < n - 2; i++) roles[i] = 'serial';

      // byte 7 = real function code of the nested Modbus command
      if (7 < n - 2) roles[7] = 'fc';

      // bytes 8..n-3 = data of the nested PDU
      for (let i = 8; i < n - 2; i++) roles[i] = 'data';
    } else {
      // subcommand is real (scan_start/end/continue/response, event cmds)
      roles[2] = 'subcommand';

      if (sub.type === 'scan_response') {
        // bytes 3..6 = serial number, byte 7 = new modbus address (data)
        for (let i = 3; i <= 6 && i < n - 2; i++) roles[i] = 'serial';
        if (7 < n - 2) roles[7] = 'data';
      } else {
        // scan_start/end/continue, event cmds — everything else is data
        for (let i = 3; i < n - 2; i++) roles[i] = 'data';
      }
    }
  } else {
    // Standard Modbus RTU
    roles[0] = 'address';
    if (1 < n - 2) roles[1] = 'fc';
    for (let i = 2; i < n - 2; i++) roles[i] = 'data';
  }

  return roles;
}

// ============================================================
// Utilities
// ============================================================

function toHex(bytes: number[]): string {
  return bytes.map(b => b.toString(16).toUpperCase().padStart(2, '0')).join('');
}

/**
 * Read big-endian uint16 from byte array at offset i.
 * Returns 0 if out of bounds (callers must validate length before calling).
 */
function u16be(b: number[], i: number): number {
  if (i + 1 >= b.length) return 0;
  return ((b[i] << 8) | b[i + 1]) >>> 0;
}

export function parseHex(str: string): number[] | null {
  if (typeof str !== 'string') return null;
  const clean = str.replace(/\s+/g, '');
  if (clean.length === 0 || clean.length % 2 !== 0) return null;
  const result: number[] = [];
  for (let i = 0; i < clean.length; i += 2) {
    const b = parseInt(clean.slice(i, i + 2), 16);
    if (isNaN(b)) return null;
    result.push(b);
  }
  return result;
}

// ============================================================
// FC name table
// ============================================================

const FC_NAMES: Record<number, string> = {
  0x01: 'read_coils',
  0x02: 'read_discrete_inputs',
  0x03: 'read_holding_registers',
  0x04: 'read_input_registers',
  0x05: 'write_single_coil',
  0x06: 'write_single_register',
  0x07: 'read_exception_status',
  0x08: 'diagnostics',
  0x0b: 'get_comm_event_counter',
  0x0c: 'get_comm_event_log',
  0x0f: 'write_multiple_coils',
  0x10: 'write_multiple_registers',
  0x11: 'report_server_id',
  0x14: 'read_file_record',
  0x15: 'write_file_record',
  0x16: 'mask_write_register',
  0x17: 'read_write_multiple_registers',
  0x18: 'read_fifo_queue',
  0x2b: 'mei_transport',
};

function fcName(fc: number): string {
  return FC_NAMES[fc] ?? `fc_${toHex([fc])}`;
}

function isUserDefinedFc(fc: number): boolean {
  return (fc >= 0x41 && fc <= 0x48) || (fc >= 0x64 && fc <= 0x6e);
}

// FC 0x5A = Schneider Electric UMAS (Unified Messaging Application Services),
// FC 0x5B = Modicon legacy. Both are officially "reserved" by Modbus Organization
// but actively used by Schneider (M340, M580, Unity Pro). Treat as vendor-specific.
const VENDOR_FC = new Set([0x5a, 0x5b]);

// ============================================================
// Shared PDU helpers — used in both request and response decoders
// ============================================================

function makeError(fc: number, bytes: number[]): PduResult {
  const origFc = fc & 0x7f;
  // length already validated by caller (bytes.length >= 2)
  return {
    type: 'modbus_error',
    raw: toHex(bytes.slice(0, 2)),
    fc: toHex([fc]),
    original_fc: toHex([origFc]),
    error_code: bytes[1],
  };
}

function decodeMei(bytes: number[], isResponse: boolean): PduResult | ParseError {
  if (bytes.length < 2) return { type: 'parse_error', reason: 'pdu_too_short', raw: toHex(bytes) };
  const fc = bytes[0];
  const fcHex = toHex([fc]);
  const meiType = bytes[1];
  const meiHex = toHex([meiType]);
  const raw = toHex(bytes);

  if (meiType === 0x0e) {
    if (!isResponse) {
      if (bytes.length < 4) return { type: 'parse_error', reason: 'pdu_too_short', raw };
      return { type: 'mei_read_device_identification', raw, fc: fcHex, mei_type: meiHex, read_device_id_code: toHex([bytes[2]]), object_id: toHex([bytes[3]]) };
    }
    // response
    if (bytes.length < 7) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    const numObjects = bytes[6];
    const objects: { id: string; value: string; }[] = [];
    let off = 7;
    for (let i = 0; i < numObjects && off + 1 < bytes.length; i++) {
      const objLen = bytes[off + 1];
      if (off + 2 + objLen > bytes.length) break;
      objects.push({ id: toHex([bytes[off]]), value: toHex(bytes.slice(off + 2, off + 2 + objLen)) });
      off += 2 + objLen;
    }
    return { type: 'mei_read_device_identification_response', raw, fc: fcHex, mei_type: meiHex, read_device_id_code: toHex([bytes[2]]), conformity_level: toHex([bytes[3]]), more_follows: toHex([bytes[4]]), next_object_id: toHex([bytes[5]]), number_of_objects: numObjects, objects };
  }

  if (meiType === 0x0d) return { type: 'mei_canopen', raw, fc: fcHex, mei_type: meiHex, data: toHex(bytes.slice(2)) };
  return { type: 'mei_transport', raw, fc: fcHex, mei_type: meiHex, data: toHex(bytes.slice(2)) };
}

// ============================================================
// PDU decoder (request)
// ============================================================

export function decodeStdPduRequest(bytes: number[]): PduResult | ParseError {
  if (bytes.length < 1) return { type: 'parse_error', reason: 'pdu_empty', raw: '' };
  const fc = bytes[0];
  const fcHex = toHex([fc]);
  const raw = toHex(bytes);

  if (fc === 0x00) return { type: 'parse_error', reason: 'invalid_fc', raw, fc: fcHex };
  if (fc & 0x80) {
    if (bytes.length < 2) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    return makeError(fc, bytes);
  }

  // FC 01-04: read (fc + start(2) + count(2))
  if (fc >= 0x01 && fc <= 0x04) {
    if (bytes.length < 5) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    return { type: fcName(fc), raw: toHex(bytes.slice(0, 5)), fc: fcHex, register: toHex(bytes.slice(1, 3)), count: u16be(bytes, 3) };
  }

  // FC 05, 06: write single (fc + addr(2) + value(2))
  if (fc === 0x05 || fc === 0x06) {
    if (bytes.length < 5) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    return { type: fcName(fc), raw: toHex(bytes.slice(0, 5)), fc: fcHex, register: toHex(bytes.slice(1, 3)), value: u16be(bytes, 3) };
  }

  // FC 07, 0B, 0C, 11: single-byte request
  if (fc === 0x07 || fc === 0x0b || fc === 0x0c || fc === 0x11) {
    return { type: fcName(fc), raw: toHex(bytes.slice(0, 1)), fc: fcHex };
  }

  // FC 08: diagnostics (fc + sub(2) + data(2))
  if (fc === 0x08) {
    if (bytes.length < 5) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    return { type: fcName(fc), raw: toHex(bytes.slice(0, 5)), fc: fcHex, sub_function: toHex(bytes.slice(1, 3)), data: toHex(bytes.slice(3, 5)) };
  }

  // FC 0F, 10: write multiple (fc + start(2) + qty(2) + bytecount(1) + data)
  if (fc === 0x0f || fc === 0x10) {
    if (bytes.length < 6) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    const byteCount = bytes[5];
    if (bytes.length < 6 + byteCount) return { type: 'parse_error', reason: 'pdu_data_truncated', raw };
    return { type: fcName(fc), raw: toHex(bytes.slice(0, 6 + byteCount)), fc: fcHex, register: toHex(bytes.slice(1, 3)), count: u16be(bytes, 3), byte_count: byteCount, data: toHex(bytes.slice(6, 6 + byteCount)) };
  }

  // FC 14: read file record
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
    return { type: fcName(fc), raw: toHex(bytes.slice(0, 2 + byteCount)), fc: fcHex, byte_count: byteCount, sub_requests: subReqs };
  }

  // FC 15: write file record
  if (fc === 0x15) {
    if (bytes.length < 2) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    const dataLen = bytes[1];
    if (bytes.length < 2 + dataLen) return { type: 'parse_error', reason: 'pdu_data_truncated', raw };
    const subReqs = [];
    let off = 2;
    while (off + 7 <= 2 + dataLen) {
      const recLen = u16be(bytes, off + 5);
      const dataEnd = off + 7 + recLen * 2;
      if (dataEnd > bytes.length) return { type: 'parse_error', reason: 'pdu_data_truncated', raw };
      subReqs.push({ reference_type: toHex([bytes[off]]), file_number: u16be(bytes, off + 1), record_number: u16be(bytes, off + 3), record_length: recLen, data: toHex(bytes.slice(off + 7, dataEnd)) });
      off = dataEnd;
    }
    return { type: fcName(fc), raw: toHex(bytes.slice(0, 2 + dataLen)), fc: fcHex, request_data_length: dataLen, sub_requests: subReqs };
  }

  // FC 16: mask write register
  if (fc === 0x16) {
    if (bytes.length < 7) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    return { type: fcName(fc), raw: toHex(bytes.slice(0, 7)), fc: fcHex, register: toHex(bytes.slice(1, 3)), and_mask: toHex(bytes.slice(3, 5)), or_mask: toHex(bytes.slice(5, 7)) };
  }

  // FC 17: read/write multiple registers
  if (fc === 0x17) {
    if (bytes.length < 10) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    const writeByteCount = bytes[9];
    if (bytes.length < 10 + writeByteCount) return { type: 'parse_error', reason: 'pdu_data_truncated', raw };
    return { type: fcName(fc), raw: toHex(bytes.slice(0, 10 + writeByteCount)), fc: fcHex, read_register: toHex(bytes.slice(1, 3)), read_count: u16be(bytes, 3), write_register: toHex(bytes.slice(5, 7)), write_count: u16be(bytes, 7), write_byte_count: writeByteCount, write_data: toHex(bytes.slice(10, 10 + writeByteCount)) };
  }

  // FC 18: read FIFO queue (fc + pointer(2))
  if (fc === 0x18) {
    if (bytes.length < 3) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    return { type: fcName(fc), raw: toHex(bytes.slice(0, 3)), fc: fcHex, fifo_pointer: toHex(bytes.slice(1, 3)) };
  }

  if (fc === 0x2b) return decodeMei(bytes, false);
  if (isUserDefinedFc(fc)) return { type: 'user_defined', raw, fc: fcHex, data: toHex(bytes.slice(1)) };
  if (VENDOR_FC.has(fc)) return { type: 'vendor_specific', raw, fc: fcHex, data: toHex(bytes.slice(1)) };
  return { type: 'parse_error', reason: 'unknown_fc', raw, fc: fcHex };
}

// ============================================================
// PDU decoder (response)
// ============================================================

export function decodeStdPduResponse(bytes: number[]): PduResult | ParseError {
  if (bytes.length < 1) return { type: 'parse_error', reason: 'pdu_empty', raw: '' };
  const fc = bytes[0];
  const fcHex = toHex([fc]);
  const raw = toHex(bytes);

  if (fc === 0x00) return { type: 'parse_error', reason: 'invalid_fc', raw, fc: fcHex };
  if (fc & 0x80) {
    if (bytes.length < 2) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    return makeError(fc, bytes);
  }

  // FC 01-04: read response (fc + byte_count(1) + data)
  if (fc >= 0x01 && fc <= 0x04) {
    if (bytes.length < 2) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    const byteCount = bytes[1];
    if (bytes.length < 2 + byteCount) return { type: 'parse_error', reason: 'pdu_data_truncated', raw };
    return { type: fcName(fc) + '_response', raw: toHex(bytes.slice(0, 2 + byteCount)), fc: fcHex, byte_count: byteCount, data: toHex(bytes.slice(2, 2 + byteCount)) };
  }

  // FC 05, 06: echo (fc + addr(2) + value(2))
  if (fc === 0x05 || fc === 0x06) {
    if (bytes.length < 5) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    return { type: fcName(fc) + '_response', raw: toHex(bytes.slice(0, 5)), fc: fcHex, register: toHex(bytes.slice(1, 3)), value: u16be(bytes, 3) };
  }

  // FC 07: read exception status response (fc + 1 byte)
  if (fc === 0x07) {
    if (bytes.length < 2) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    return { type: 'read_exception_status_response', raw: toHex(bytes.slice(0, 2)), fc: fcHex, output_data: toHex([bytes[1]]) };
  }

  // FC 08: diagnostics response — same structure as request
  if (fc === 0x08) {
    if (bytes.length < 5) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    return { type: 'diagnostics_response', raw: toHex(bytes.slice(0, 5)), fc: fcHex, sub_function: toHex(bytes.slice(1, 3)), data: toHex(bytes.slice(3, 5)) };
  }

  // FC 0B: get comm event counter response (fc + status(2) + event_count(2))
  if (fc === 0x0b) {
    if (bytes.length < 5) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    return { type: 'get_comm_event_counter_response', raw: toHex(bytes.slice(0, 5)), fc: fcHex, status: toHex(bytes.slice(1, 3)), event_count: u16be(bytes, 3) };
  }

  // FC 0C: get comm event log response
  if (fc === 0x0c) {
    if (bytes.length < 2) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    const byteCount = bytes[1];
    if (bytes.length < 2 + byteCount) return { type: 'parse_error', reason: 'pdu_data_truncated', raw };
    return { type: 'get_comm_event_log_response', raw: toHex(bytes.slice(0, 2 + byteCount)), fc: fcHex, byte_count: byteCount, status: toHex(bytes.slice(2, 4)), event_count: u16be(bytes, 4), message_count: u16be(bytes, 6), events: toHex(bytes.slice(8, 2 + byteCount)) };
  }

  // FC 0F, 10: write multiple response (fc + start(2) + qty(2))
  if (fc === 0x0f || fc === 0x10) {
    if (bytes.length < 5) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    return { type: fcName(fc) + '_response', raw: toHex(bytes.slice(0, 5)), fc: fcHex, register: toHex(bytes.slice(1, 3)), count: u16be(bytes, 3) };
  }

  // FC 11: report server ID response
  if (fc === 0x11) {
    if (bytes.length < 2) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    const byteCount = bytes[1];
    if (bytes.length < 2 + byteCount) return { type: 'parse_error', reason: 'pdu_data_truncated', raw };
    return { type: 'report_server_id_response', raw: toHex(bytes.slice(0, 2 + byteCount)), fc: fcHex, byte_count: byteCount, server_id: toHex([bytes[2]]), run_indicator: toHex([bytes[3]]), additional_data: byteCount > 2 ? toHex(bytes.slice(4, 2 + byteCount)) : '' };
  }

  // FC 14: read file record response
  if (fc === 0x14) {
    if (bytes.length < 2) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    const respDataLen = bytes[1];
    if (bytes.length < 2 + respDataLen) return { type: 'parse_error', reason: 'pdu_data_truncated', raw };
    const subResps = [];
    let off = 2;
    while (off < 2 + respDataLen && off + 1 < bytes.length) {
      const fileRespLen = bytes[off];
      if (off + 1 + fileRespLen > bytes.length) break;
      subResps.push({ file_resp_length: fileRespLen, reference_type: toHex([bytes[off + 1]]), data: toHex(bytes.slice(off + 2, off + 2 + fileRespLen - 1)) });
      off += 1 + fileRespLen;
    }
    return { type: 'read_file_record_response', raw: toHex(bytes.slice(0, 2 + respDataLen)), fc: fcHex, resp_data_length: respDataLen, sub_responses: subResps };
  }

  // FC 16: mask write register response (echo)
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

  // FC 18: read FIFO queue response (fc + bytecount(2 BE) + fifo_count(2) + data)
  if (fc === 0x18) {
    if (bytes.length < 5) return { type: 'parse_error', reason: 'pdu_too_short', raw };
    const byteCount = u16be(bytes, 1); // 2-byte field, different from other FCs
    const fifoCount = u16be(bytes, 3);
    if (bytes.length < 5 + fifoCount * 2) return { type: 'parse_error', reason: 'pdu_data_truncated', raw };
    return { type: 'read_fifo_queue_response', raw: toHex(bytes.slice(0, 5 + fifoCount * 2)), fc: fcHex, byte_count: byteCount, fifo_count: fifoCount, data: toHex(bytes.slice(5, 5 + fifoCount * 2)) };
  }

  if (fc === 0x2b) return decodeMei(bytes, true);
  if (isUserDefinedFc(fc)) return { type: 'user_defined', raw, fc: fcHex, data: toHex(bytes.slice(1)) };
  if (VENDOR_FC.has(fc)) return { type: 'vendor_specific', raw, fc: fcHex, data: toHex(bytes.slice(1)) };
  return { type: 'parse_error', reason: 'unknown_fc', raw, fc: fcHex };
}

// ============================================================
// Fast Modbus subcommand layer (bytes after ext_byte, before CRC)
// ============================================================

function decodeFastModbusPayload(bytes: number[]): FastModbusSubcommand {
  if (bytes.length < 1) return { type: 'parse_error', reason: 'subcommand_missing', raw: '' };
  const sub = bytes[0];
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
    const pdu = decodeStdPduRequest(bytes.slice(5));
    const fcVal = 'fc' in pdu ? String(pdu.fc) : '??';
    return { type: 'command_by_serial', raw, serial_number: toHex(bytes.slice(1, 5)), function_code: fcVal, payload: pdu };
  }
  if (sub === 0x09) {
    if (bytes.length < 6) return { type: 'parse_error', reason: 'response_by_serial_too_short', raw };
    const pdu = decodeStdPduResponse(bytes.slice(5));
    const fcVal = 'fc' in pdu ? String(pdu.fc) : '??';
    return { type: 'response_by_serial', raw, serial_number: toHex(bytes.slice(1, 5)), function_code: fcVal, payload: pdu };
  }
  return { type: 'parse_error', reason: 'unknown_subcommand', raw, subcommand: toHex([sub]) };
}

// ============================================================
// Top-level decoder
// ============================================================

/**
 * Decode a Modbus packet.
 * @param input  hex string (spaces optional) or byte array
 * @param direction  'request' | 'response' — used for standard RTU PDU disambiguation
 */
export function decodePacket(input: string | number[], direction: Direction = 'response'): DecodedPacket {
  let bytes: number[];

  if (typeof input === 'string') {
    const parsed = parseHex(input);
    if (parsed === null) return { type: 'parse_error', reason: 'invalid_hex', raw: input };
    bytes = parsed;
  } else {
    bytes = Array.from(input);
  }

  if (bytes.length === 0) return { type: 'parse_error', reason: 'empty', raw: '' };

  const raw = toHex(bytes);

  // Arbitration: all 0xFF
  if (bytes.every(b => b === 0xff)) return { type: 'arbitration', raw };

  // Minimum RTU: address(1) + fc(1) + CRC(2) = 4 bytes
  if (bytes.length < 4) return { type: 'parse_error', reason: 'too_short', raw };

  const address = bytes[0];
  const addressHex = toHex([address]);
  const crc = toHex(bytes.slice(-2));

  // Fast Modbus: address=0xFD, ext_byte=0x60|0x46
  if (address === 0xfd && (bytes[1] === 0x60 || bytes[1] === 0x46)) {
    const extByte = bytes[1];
    const fmBytes = bytes.slice(2, bytes.length - 2);
    const subByte = fmBytes.length > 0 ? toHex([fmBytes[0]]) : '??';
    return {
      type: 'rtu_frame',
      raw,
      address: addressHex,
      crc,
      payload: {
        type: 'fast_modbus',
        raw: toHex(bytes.slice(1, bytes.length - 2)),
        ext_byte: toHex([extByte]),
        subcommand: subByte,
        payload: decodeFastModbusPayload(fmBytes),
      },
    };
  }

  // Standard Modbus RTU
  const pduBytes = bytes.slice(1, bytes.length - 2);
  const decodePdu = direction === 'request' ? decodeStdPduRequest : decodeStdPduResponse;
  const payload: ModbusRtuPayload = {
    type: 'modbus_rtu',
    raw: toHex(pduBytes),
    payload: decodePdu(pduBytes),
  };

  const result: RtuFrame = { type: 'rtu_frame', raw, address: addressHex, crc, payload };
  if (address >= 0xf8) result.reserved_address = true;

  return result;
}
