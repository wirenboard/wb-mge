import { describe, it, expect } from 'vitest';
import {
  isPrintable,
  f32str,
  rawToRange,
  fmtVal,
  fmtCoilData,
  fieldRanges,
  flattenNode,
} from './packetDecoderUtils';
import { decodePacket } from '../common/modbusDecoder';

// ============================================================
// isPrintable
// ============================================================
describe('isPrintable', () => {
  it('0x20 (space) is printable', () => {
    expect(isPrintable(0x20)).toBe(true);
  });

  it('0x7E (~) is printable', () => {
    expect(isPrintable(0x7E)).toBe(true);
  });

  it('0x7F (DEL) is not printable', () => {
    expect(isPrintable(0x7F)).toBe(false);
  });

  it('0x1F (control) is not printable', () => {
    expect(isPrintable(0x1F)).toBe(false);
  });

  it('0x41 (A) is printable', () => {
    expect(isPrintable(0x41)).toBe(true);
  });
});

// ============================================================
// f32str
// ============================================================
describe('f32str', () => {
  it('0x3F800000 → float 1.0 → "1"', () => {
    expect(f32str(0x3F800000)).toBe('1');
  });

  it('0x00000000 → float 0.0 → "0"', () => {
    expect(f32str(0x00000000)).toBe('0');
  });

  it('0xFFC00000 → NaN → "NaN"', () => {
    expect(f32str(0xFFC00000)).toBe('NaN');
  });

  it('0x3DCCCCCD → approximately 0.1', () => {
    const result = f32str(0x3DCCCCCD);
    // IEEE 754 float 0.1 is approximately 0.1000000015
    expect(parseFloat(result)).toBeCloseTo(0.1, 5);
  });
});

// ============================================================
// rawToRange
// ============================================================
describe('rawToRange', () => {
  it('finds AABB at start of AABBCC', () => {
    expect(rawToRange('AABB', 'AABBCC', 0)).toEqual({ start: 0, end: 2 });
  });

  it('finds BBCC at offset 1 in AABBCC', () => {
    expect(rawToRange('BBCC', 'AABBCC', 0)).toEqual({ start: 1, end: 3 });
  });

  it('empty nodeRaw → {start: parentStart, end: parentStart}', () => {
    expect(rawToRange('', 'AABB', 0)).toEqual({ start: 0, end: 0 });
  });

  it('nodeRaw not found → {start: parentStart, end: parentStart}', () => {
    expect(rawToRange('DDEE', 'AABBCC', 0)).toEqual({ start: 0, end: 0 });
  });

  it('nodeRaw not found with non-zero parentStart', () => {
    expect(rawToRange('DDEE', 'AABBCC', 2)).toEqual({ start: 2, end: 2 });
  });
});

// ============================================================
// fmtVal
// ============================================================
describe('fmtVal', () => {
  it('function_code 03 → Read Holding Registers', () => {
    expect(fmtVal('function_code', '03')).toBe('0x03 (Read Holding Registers)');
  });

  it('function_code FF → Error: Unknown (0xFF has bit 7 set, treated as exception FC)', () => {
    expect(fmtVal('function_code', 'FF')).toBe('0xFF (Error: Unknown)');
  });

  it('ext_byte 46 → Extended function command', () => {
    expect(fmtVal('ext_byte', '46')).toBe('0x46 (Extended function command)');
  });

  it('subcommand 01 → Scan Start', () => {
    expect(fmtVal('subcommand', '01')).toBe('0x01 (Scan Start)');
  });

  it('address 00 → 0x00 (0 broadcast)', () => {
    expect(fmtVal('address', '00')).toBe('0x00 (0 broadcast)');
  });

  it('address 01 → 0x01 (1) with no note', () => {
    expect(fmtVal('address', '01')).toBe('0x01 (1)');
  });

  it('crc field (HEX_FIELDS but not DEC_ALSO) → 0xABCD', () => {
    expect(fmtVal('crc', 'ABCD')).toBe('0xABCD');
  });

  it('count field (not in HEX_FIELDS) → plain string', () => {
    expect(fmtVal('count', '5')).toBe('5');
  });
});

// ============================================================
// fieldRanges
// ============================================================
describe('fieldRanges', () => {
  it('read_coils request at offset 0: fc={0,1} register={1,3} count={3,5}', () => {
    const obj: Record<string, unknown> = { type: 'read_coils', fc: '01', register: '0013', count: 19 };
    const r = fieldRanges(obj, 0);
    expect(r.fc).toEqual({ start: 0, end: 1 });
    expect(r.register).toEqual({ start: 1, end: 3 });
    expect(r.count).toEqual({ start: 3, end: 5 });
  });

  it('read_holding_registers_response with byte_count=4 at offset 1', () => {
    const obj: Record<string, unknown> = {
      type: 'read_holding_registers_response',
      fc: '03',
      byte_count: 4,
      data: '01F40064',
    };
    const r = fieldRanges(obj, 1);
    expect(r.fc).toEqual({ start: 1, end: 2 });
    expect(r.byte_count).toEqual({ start: 2, end: 3 });
    expect(r.data).toEqual({ start: 3, end: 7 });
  });

  it('write_single_register at offset 0: fc={0,1} register={1,3} value={3,5}', () => {
    const obj: Record<string, unknown> = { type: 'write_single_register', fc: '06', register: '0003', value: 9600 };
    const r = fieldRanges(obj, 0);
    expect(r.fc).toEqual({ start: 0, end: 1 });
    expect(r.register).toEqual({ start: 1, end: 3 });
    expect(r.value).toEqual({ start: 3, end: 5 });
  });

  it('modbus_error at offset 0: fc={0,1} error_code={1,2}', () => {
    const obj: Record<string, unknown> = { type: 'modbus_error', fc: '83', error_code: 2 };
    const r = fieldRanges(obj, 0);
    expect(r.fc).toEqual({ start: 0, end: 1 });
    expect(r.error_code).toEqual({ start: 1, end: 2 });
  });

  it('scan_response at offset 1: serial_number={2,6} modbus_address={6,7}', () => {
    const obj: Record<string, unknown> = {
      type: 'scan_response',
      serial_number: '00062466',
      modbus_address: '83',
    };
    const r = fieldRanges(obj, 1);
    expect(r.serial_number).toEqual({ start: 2, end: 6 });
    expect(r.modbus_address).toEqual({ start: 6, end: 7 });
  });
});

// ============================================================
// flattenNode
// ============================================================

/**
 * Helper: decode a hex packet and flatten it into a compact row array.
 * Rows with a value are returned as { depth, label, value };
 * rows without a value (section headers) as { depth, label }.
 */
function treeLabels(hex: string, dir: 'request' | 'response' = 'request'): { depth: number; label: string; value?: string }[] {
  const decoded = decodePacket(hex, dir);
  return flattenNode(decoded as Record<string, unknown>, 0, hex.toUpperCase(), 0)
    .map(r => r.value !== undefined ? { depth: r.depth, label: r.label, value: r.value } : { depth: r.depth, label: r.label });
}

describe('flattenNode', () => {
  // ── Test 1 ──────────────────────────────────────────────────
  // Standard Modbus RTU read-holding-registers request.
  // The fc field must appear exactly once, at the Modbus RTU level (depth 2).
  // It must NOT be repeated inside the PDU section.
  it('RTU request: Function code appears exactly once (at modbus_rtu level, not duplicated in PDU)', () => {
    // addr=0x83, FC=0x03, reg=0x0061, count=2, CRC=0x8BF7
    const rows = treeLabels('8303006100028BF7', 'request');

    // Top-level structure checks
    expect(rows).toContainEqual({ depth: 0, label: 'RTU Frame' });
    expect(rows).toContainEqual({ depth: 1, label: 'Slave address', value: '0x83 (131)' });
    expect(rows).toContainEqual({ depth: 1, label: 'CRC', value: '0x8BF7' });
    expect(rows).toContainEqual({ depth: 1, label: 'Modbus RTU' });

    // Function code must be present at depth 2 under Modbus RTU
    expect(rows).toContainEqual({ depth: 2, label: 'Function code', value: '0x03 (Read Holding Registers)' });

    // PDU section header
    expect(rows).toContainEqual({ depth: 2, label: 'Read Holding Registers' });

    // PDU fields — fc must NOT appear here (depth 3)
    expect(rows).toContainEqual({ depth: 3, label: 'Starting Address', value: '0x0061 (97)' });
    expect(rows).toContainEqual({ depth: 3, label: 'Count', value: '2' });

    // Assert exactly ONE row with label 'Function code'
    const fcRows = rows.filter(r => r.label === 'Function code');
    expect(fcRows).toHaveLength(1);
  });

  // ── Test 2 ──────────────────────────────────────────────────
  // Standard Modbus RTU read-holding-registers response.
  // fc must appear once at the modbus_rtu level, not inside the PDU.
  it('RTU response: Function code appears exactly once (at modbus_rtu level, not duplicated in PDU)', () => {
    // addr=0x83, FC=0x03, byte_count=4, data=00000000, CRC=0x39A3
    const rows = treeLabels('8303040000000039A3', 'response');

    // Function code at Modbus RTU level (depth 2)
    expect(rows).toContainEqual({ depth: 2, label: 'Function code', value: '0x03 (Read Holding Registers)' });

    // PDU response section at depth 2
    expect(rows).toContainEqual({ depth: 2, label: 'Read Holding Registers Response' });

    // PDU fields at depth 3 — fc suppressed here
    expect(rows).toContainEqual({ depth: 3, label: 'Byte count', value: '4' });
    expect(rows).toContainEqual({ depth: 3, label: 'Data', value: '0x00000000' });

    // Exactly one 'Function code' row in the whole tree
    const fcRows = rows.filter(r => r.label === 'Function code');
    expect(fcRows).toHaveLength(1);
  });

  // ── Test 3 ──────────────────────────────────────────────────
  // Invalid function code (FC=0x00) causes a parse_error PDU.
  // The fc=0x00 shown at modbus_rtu level must not be repeated inside the parse_error node.
  it('parse_error (invalid FC=0x00): Function code shown at modbus_rtu level only, not inside Parse Error', () => {
    // addr=0x01, FC=0x00 (invalid), CRC=0x66F0 (may fail CRC check but decodes)
    const rows = treeLabels('010066F0', 'request');

    // Function code at modbus_rtu level (depth 2)
    expect(rows).toContainEqual({ depth: 2, label: 'Function code', value: '0x00 (Unknown)' });

    // Parse Error section at depth 2
    expect(rows).toContainEqual({ depth: 2, label: 'Parse Error' });

    // Reason field inside Parse Error at depth 3
    expect(rows).toContainEqual({ depth: 3, label: 'Reason', value: 'invalid_fc' });

    // Exactly one 'Function code' row — not duplicated inside parse_error
    const fcRows = rows.filter(r => r.label === 'Function code');
    expect(fcRows).toHaveLength(1);
  });

  // ── Test 4 ──────────────────────────────────────────────────
  // Fast Modbus command_by_serial wrapping a read_holding_registers PDU.
  // The parent command_by_serial already exposes 'function_code' (label: 'Function'),
  // so the PDU's own fc field must be suppressed.
  it('Fast Modbus command_by_serial: function_code shown at command level, fc suppressed in nested PDU', () => {
    // FD=Fast Modbus broadcast, 46=ext, 08=command_by_serial,
    // serial=0x12345678, FC=0x03, reg=0x0061, count=2
    const rows = treeLabels('FD4608123456780300610002D9C3', 'request');

    // Fast Modbus wrapper fields at depth 2
    expect(rows).toContainEqual({ depth: 2, label: 'FM Command', value: '0x46 (Extended function command)' });
    expect(rows).toContainEqual({ depth: 2, label: 'FM Subcommand', value: '0x08 (Command by Serial)' });

    // command_by_serial fields at depth 3
    expect(rows).toContainEqual({ depth: 3, label: 'Serial number', value: '0x12345678 (305419896)' });

    // function_code field (keyed 'function_code', label 'Function') at depth 3
    expect(rows).toContainEqual({ depth: 3, label: 'Function', value: '0x03 (Read Holding Registers)' });

    // Nested PDU section at depth 3 (no value)
    expect(rows).toContainEqual({ depth: 3, label: 'Read Holding Registers' });

    // PDU fields at depth 4
    expect(rows).toContainEqual({ depth: 4, label: 'Starting Address', value: '0x0061 (97)' });
    expect(rows).toContainEqual({ depth: 4, label: 'Count', value: '2' });

    // The 'fc' field inside the PDU must be suppressed — no row with label 'Function code'
    const fcRows = rows.filter(r => r.label === 'Function code');
    expect(fcRows).toHaveLength(0);
  });

  // ── Test 5 ──────────────────────────────────────────────────
  // Fast Modbus event_request has no nested PDU at all,
  // so there is no fc field anywhere in the tree.
  it('Fast Modbus event_request: no Function code row present anywhere', () => {
    // FD=FM broadcast, 46=ext, 10=event_request, addr=0x00, flag=0x4F, unacked=0x0000
    const rows = treeLabels('FD4610004F00007DC9', 'request');

    // Fast Modbus section present
    expect(rows.some(r => r.label === 'Fast Modbus')).toBe(true);

    // FM command and subcommand present
    expect(rows).toContainEqual({ depth: 2, label: 'FM Command', value: '0x46 (Extended function command)' });
    expect(rows).toContainEqual({ depth: 2, label: 'FM Subcommand', value: '0x10 (Event Request)' });

    // Event Request section present
    expect(rows.some(r => r.label === 'Event Request')).toBe(true);

    // No 'Function code' row anywhere in the tree
    const fcRows = rows.filter(r => r.label === 'Function code');
    expect(fcRows).toHaveLength(0);
  });

  // ── Test 6 ──────────────────────────────────────────────────
  // read_coils_response: the Data field must use binary coil formatting.
  // Packet: 11 01 03 CD 6B 05 40 12
  //   addr=0x11, FC=0x01, byte_count=3, data=CD6B05, CRC=0x1240 (stored LE: 40 12)
  it('read_coils_response: Data field shows binary coil representation', () => {
    const rows = treeLabels('110103CD6B054012', 'response');

    // Data row value must include hex and binary bytes
    const dataRow = rows.find(r => r.label === 'Data');
    expect(dataRow).toBeDefined();
    expect(dataRow?.value).toBe('0xCD6B05  (11001101 01101011 00000101)');
  });
});

// ============================================================
// fmtCoilData
// ============================================================
describe('fmtCoilData', () => {
  it('single byte 08 → "0x08  (00001000)"', () => {
    expect(fmtCoilData('08')).toBe('0x08  (00001000)');
  });

  it('two bytes 0800 → "0x0800  (00001000 00000000)"', () => {
    expect(fmtCoilData('0800')).toBe('0x0800  (00001000 00000000)');
  });

  it('three bytes CD6B05 → "0xCD6B05  (11001101 01101011 00000101)"', () => {
    expect(fmtCoilData('CD6B05')).toBe('0xCD6B05  (11001101 01101011 00000101)');
  });

  it('empty string → "0x" (edge case: no binary suffix)', () => {
    expect(fmtCoilData('')).toBe('0x');
  });
});
