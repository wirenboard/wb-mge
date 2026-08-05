import { describe, it, expect } from 'vitest';
import {
  isPrintable,
  f32str,
  rawToRange,
  fmtVal,
  fmtCoilData,
  fmtCoilState,
  fmtRegisterData,
  fieldRanges,
  flattenNode,
  modiconStr,
} from './packetDecoderUtils';
import { decodePacket } from '../common/modbusDecoder';

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

/**
 * Helper: decode a hex packet and flatten it into a compact row array.
 * Rows with a value are returned as { depth, label, value };
 * rows without a value (section headers) as { depth, label }.
 */
function treeLabels(hex: string, dir: 'request' | 'response' = 'request'): { depth: number; label: string; value?: string }[] {
  const decoded = decodePacket(hex, dir);
  // flattenNode matches node.raw against this string, so it must be the packet hex only —
  // strip any whitespace the caller used to make the literal readable.
  const flat = hex.replace(/\s+/g, '').toUpperCase();
  return flattenNode(decoded as Record<string, unknown>, 0, flat, 0)
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
    expect(rows).toContainEqual({ depth: 3, label: 'Starting Address', value: '0x0061 (97, Modicon: 40098)' });
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
    // Register payloads are grouped one 16-bit register per group.
    expect(rows).toContainEqual({ depth: 3, label: 'Data', value: '0x0000 0x0000' });

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
    expect(rows).toContainEqual({ depth: 4, label: 'Starting Address', value: '0x0061 (97, Modicon: 40098)' });
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

  // ── Test 7 ──────────────────────────────────────────────────
  // FC05 must show a coil state, not the raw 0xFF00/0x0000 word (which used to render as
  // "0x65280 (414336)" — the number the reviewer saw).
  it('write_single_coil ON: Coil state row replaces the numeric Value row', () => {
    const rows = treeLabels('0105 00AC FF00 4C1B', 'request');

    expect(rows).toContainEqual({ depth: 3, label: 'Coil state', value: 'ON (0xFF00)' });
    expect(rows.some(r => r.label === 'Value')).toBe(false);
  });

  it('write_single_coil OFF: Coil state row shows OFF', () => {
    const rows = treeLabels('0105 00AC 0000 0DEB', 'request');
    expect(rows).toContainEqual({ depth: 3, label: 'Coil state', value: 'OFF (0x0000)' });
  });

  it('write_single_coil echo (response): Coil state row shows ON', () => {
    const rows = treeLabels('0105 00AC FF00 4C1B', 'response');
    expect(rows).toContainEqual({ depth: 3, label: 'Coil state', value: 'ON (0xFF00)' });
    expect(rows.some(r => r.label === 'Value')).toBe(false);
  });

  it('write_single_coil with an illegal word: flagged as invalid, error-styled, word still shown', () => {
    const full = flattenNode(
      decodePacket('0105 00AC 1234 6CE1', 'request') as unknown as Record<string, unknown>,
      0, '010500AC12346CE1', 0,
    );
    const stateRow = full.find(r => r.key === 'coil_state');
    expect(stateRow?.value).toBe('Invalid (expected 0x0000 or 0xFF00)');
    expect(stateRow?.isError).toBe(true);
    // Both coil fields describe the same 2 bytes of the frame (offsets 4..6).
    expect({ start: stateRow?.byteStart, end: stateRow?.byteEnd }).toEqual({ start: 4, end: 6 });

    const valueRow = full.find(r => r.key === 'coil_value');
    expect(valueRow?.value).toBe('0x1234');
    expect({ start: valueRow?.byteStart, end: valueRow?.byteEnd }).toEqual({ start: 4, end: 6 });
  });

  // ── Test 8 ──────────────────────────────────────────────────
  // FC06 carries a real register value. The decoder emits it as a NUMBER, which used to be
  // pushed through the hex-string formatter and rendered "9600" as 0x9600 (38400).
  it('write_single_register: numeric Value is rendered as hex + decimal of the same number', () => {
    const rows = treeLabels('1106 0001 2580 CD8F', 'request');
    expect(rows).toContainEqual({ depth: 3, label: 'Value', value: '0x2580 (9600)' });
  });

  // ── Test 9 ──────────────────────────────────────────────────
  // Register payloads are grouped per 16-bit register instead of one continuous blob.
  it('read_holding_registers_response: Data is grouped one register per group', () => {
    // addr=0x11, FC=0x03, byte_count=12, 6 registers 0x0032..0x004B
    const rows = treeLabels('1103 0C 0032 0037 003C 0041 0046 004B 0000', 'response');
    expect(rows).toContainEqual({
      depth: 3, label: 'Data', value: '0x0032 0x0037 0x003C 0x0041 0x0046 0x004B',
    });
  });

  it('write_multiple_registers request: Data is grouped one register per group', () => {
    // addr=0x11, FC=0x10, start=0x0001, count=2, byte_count=4, data=000A0102
    const rows = treeLabels('1110 0001 0002 04 000A 0102 0000', 'request');
    expect(rows).toContainEqual({ depth: 3, label: 'Data', value: '0x000A 0x0102' });
  });

  it('write_multiple_coils request: Data keeps the bit-packed binary rendering', () => {
    // Coil payloads are bit-packed, not registers — they must not be regrouped.
    // addr=0x11, FC=0x0F, start=0x0013, count=10, byte_count=2, data=CD01
    const rows = treeLabels('110F 0013 000A 02 CD01 0000', 'request');
    expect(rows).toContainEqual({ depth: 3, label: 'Data', value: '0xCD01  (11001101 00000001)' });
  });
});

describe('fmtCoilState', () => {
  it('"on" → "ON (0xFF00)"', () => {
    expect(fmtCoilState('on')).toBe('ON (0xFF00)');
  });

  it('"off" → "OFF (0x0000)"', () => {
    expect(fmtCoilState('off')).toBe('OFF (0x0000)');
  });

  it('"invalid" → explicit protocol-violation text', () => {
    expect(fmtCoilState('invalid')).toBe('Invalid (expected 0x0000 or 0xFF00)');
  });

  it('an unknown state is passed through unchanged rather than swallowed', () => {
    expect(fmtCoilState('something_else')).toBe('something_else');
  });
});

describe('fmtRegisterData', () => {
  it('three registers → one 0x group each', () => {
    expect(fmtRegisterData('00320037003C')).toBe('0x0032 0x0037 0x003C');
  });

  it('single register → single group', () => {
    expect(fmtRegisterData('01A4')).toBe('0x01A4');
  });

  it('lower-case input is upper-cased', () => {
    expect(fmtRegisterData('00ab00cd')).toBe('0x00AB 0x00CD');
  });

  it('empty payload → empty string (no stray "0x")', () => {
    expect(fmtRegisterData('')).toBe('');
  });

  it('odd trailing byte is emitted as its own short group, not dropped or padded', () => {
    expect(fmtRegisterData('00320037 00'.replace(/\s/g, ''))).toBe('0x0032 0x0037 0x00');
  });

  it('a single stray byte does not crash', () => {
    expect(fmtRegisterData('7F')).toBe('0x7F');
  });
});

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

describe('modiconStr', () => {
  it('wire 0 + prefix 4 → "Modicon: 40001"', () => {
    expect(modiconStr(0, 4)).toBe('Modicon: 40001');
  });
  it('wire 0 + prefix 0 → "Modicon: 00001" (zero-padded, not "Modicon: 1")', () => {
    expect(modiconStr(0, 0)).toBe('Modicon: 00001');
  });
  it('wire 19 (0x13) + prefix 0 → "Modicon: 00020"', () => {
    expect(modiconStr(0x13, 0)).toBe('Modicon: 00020');
  });
  it('wire 0 + prefix 3 → "Modicon: 30001"', () => {
    expect(modiconStr(0, 3)).toBe('Modicon: 30001');
  });
  it('wire 99 + prefix 4 → "Modicon: 40100"', () => {
    expect(modiconStr(99, 4)).toBe('Modicon: 40100');
  });
  it('wire 0 + prefix 1 → "Modicon: 10001" (discrete inputs)', () => {
    expect(modiconStr(0, 1)).toBe('Modicon: 10001');
  });
  it('wire 9998 + prefix 4 → "Modicon: 49999" (last 5-digit address)', () => {
    expect(modiconStr(9998, 4)).toBe('Modicon: 49999');
  });
  it('wire 9999 + prefix 4 → "Modicon: 410000" (first 6-digit address)', () => {
    expect(modiconStr(9999, 4)).toBe('Modicon: 410000');
  });
});

describe('flattenNode — register Modicon notation', () => {
  it('FC03 read holding registers: register 0x0000 shows Modicon 40001', () => {
    // addr=0x01, FC=0x03, reg=0x0000, count=2, CRC=computed
    const rows = treeLabels('01030000000284 0A', 'request');
    const regRow = rows.find(r => r.label === 'Starting Address');
    expect(regRow?.value).toContain('Modicon: 40001');
    expect(regRow?.value).toContain('0x0000');
  });
  it('FC03 read holding registers: register 0x0013 shows Modicon 40020', () => {
    // addr=0x01, FC=0x03, reg=0x0013, count=2, CRC=0x9403
    const rows = treeLabels('01 03 00 13 00 02 94 03', 'request');
    const regRow = rows.find(r => r.label === 'Starting Address');
    expect(regRow?.value).toContain('Modicon: 40020');
  });
  it('FC04 read input registers: register 0x0005 shows Modicon 30006', () => {
    // addr=0x03, FC=0x04, reg=0x0005, count=1, CRC=0x2920
    const rows = treeLabels('03 04 00 05 00 01 20 29', 'request');
    const regRow = rows.find(r => r.label === 'Starting Address');
    expect(regRow?.value).toContain('Modicon: 30006');
  });
  it('FC01 read coils: register 0x0013 shows Modicon 00020', () => {
    // addr=0x11, FC=0x01, reg=0x0013, count=0x13, CRC=0x928E
    const rows = treeLabels('11 01 00 13 00 13 8E 92', 'request');
    const regRow = rows.find(r => r.label === 'Starting Address');
    expect(regRow?.value).toContain('Modicon: 00020');
  });
  it('FC02 read discrete inputs: register 0x00C4 shows Modicon 10197', () => {
    // addr=0x11, FC=0x02, reg=0x00C4 (196), count=0x16, CRC=0xA9BA
    const rows = treeLabels('11 02 00 C4 00 16 BA A9', 'request');
    const regRow = rows.find(r => r.label === 'Starting Address');
    expect(regRow?.value).toContain('Modicon: 10197');
    expect(regRow?.value).toContain('0x00C4');
  });
});

describe('flattenNode — CRC status annotation', () => {
  it('crcStatus "OK" appends "(OK)" to the CRC field value', () => {
    // addr=0x83, FC=0x03, reg=0x0061, count=2, CRC=0x8BF7
    const hex = '8303006100028BF7';
    const decoded = decodePacket(hex, 'request');
    const rows = flattenNode(decoded as Record<string, unknown>, 0, hex.toUpperCase(), 0, undefined, 'OK');
    const crcRow = rows.find(r => r.key === 'crc');
    expect(crcRow?.value).toMatch(/\(OK\)$/);
  });

  it('crcStatus "ERR" appends "(ERR)" to the CRC field value', () => {
    // addr=0x83, FC=0x03, reg=0x0061, count=2, CRC=0x8BF7
    const hex = '8303006100028BF7';
    const decoded = decodePacket(hex, 'request');
    const rows = flattenNode(decoded as Record<string, unknown>, 0, hex.toUpperCase(), 0, undefined, 'ERR');
    const crcRow = rows.find(r => r.key === 'crc');
    expect(crcRow?.value).toMatch(/\(ERR\)$/);
  });

  it('without crcStatus the CRC field value has no annotation', () => {
    // addr=0x83, FC=0x03, reg=0x0061, count=2, CRC=0x8BF7
    const hex = '8303006100028BF7';
    const decoded = decodePacket(hex, 'request');
    const rows = flattenNode(decoded as Record<string, unknown>, 0, hex.toUpperCase(), 0);
    const crcRow = rows.find(r => r.key === 'crc');
    // Exact value: fmtVal('crc', '8BF7') → '0x8BF7' (HEX_FIELDS, no DEC_ALSO, no annotation)
    expect(crcRow?.value).toBe('0x8BF7');
  });

  it('Fast Modbus packet with crcStatus="OK" — exactly one crc-row, annotated (OK)', () => {
    // FD 60 03 00 06 24 66 83 C4 61 — Fast Modbus scan_start broadcast
    // CRC bytes (last two): C4 61 → raw string 'C461' → fmtVal → '0xC461'
    const hex = 'FD600300062466 83C461';
    const normalized = hex.replace(/\s/g, '').toUpperCase();
    const decoded = decodePacket(normalized, 'request');
    const rows = flattenNode(decoded as Record<string, unknown>, 0, normalized, 0, undefined, 'OK');
    const crcRows = rows.filter(r => r.key === 'crc');
    // There must be exactly one CRC row (on the rtu_frame level)
    expect(crcRows).toHaveLength(1);
    // The value must be the hex CRC annotated with (OK)
    expect(crcRows[0].value).toBe('0xC461 (OK)');
  });
});
