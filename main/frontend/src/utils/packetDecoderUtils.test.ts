import { describe, it, expect } from 'vitest';
import {
  isPrintable,
  f32str,
  rawToRange,
  fmtVal,
  fieldRanges,
} from './packetDecoderUtils';

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
