import { describe, it, expect, vi, afterEach } from 'vitest';
import {
  modbusCrc16,
  appendCrc,
  buildReadFrame,
  buildWriteSingleCoil,
  buildWriteSingleRegister,
  buildWriteMultipleRegisters,
  buildWriteMultipleCoils,
  buildPreviewFrame,
  frameToPreviewParts,
  sendPacketToPort,
  parseValueList,
  parseStrictInt,
} from './modbusUtils';

// ─────────────────────────────────────────────────────────────────────────────
// 1. modbusCrc16
// ─────────────────────────────────────────────────────────────────────────────
describe('modbusCrc16', () => {
  it('computes correct CRC for a known FC03 request', () => {
    // [0x01, 0x03, 0x00, 0x00, 0x00, 0x0A] → CRC = 0xC5CD (lo=0xC5, hi=0xCD)
    const crc = modbusCrc16(new Uint8Array([0x01, 0x03, 0x00, 0x00, 0x00, 0x0a]));
    expect(crc & 0xff).toBe(0xc5); // low byte
    expect((crc >> 8) & 0xff).toBe(0xcd); // high byte
  });

  it('returns 0xFFFF for an empty array (no iterations, initial value returned)', () => {
    expect(modbusCrc16(new Uint8Array([]))).toBe(0xffff);
  });

  it('returns a number in [0, 0xFFFF] for a single byte', () => {
    const result = modbusCrc16(new Uint8Array([0x01]));
    expect(typeof result).toBe('number');
    expect(result).toBeGreaterThanOrEqual(0);
    expect(result).toBeLessThanOrEqual(0xffff);
    expect(result).not.toBe(0xffff);
  });
});

// ─────────────────────────────────────────────────────────────────────────────
// 2. appendCrc
// ─────────────────────────────────────────────────────────────────────────────
describe('appendCrc', () => {
  it('appends exactly 2 bytes to the input', () => {
    const input = new Uint8Array([0x01, 0x03, 0x00, 0x00, 0x00, 0x0a]);
    const result = appendCrc(input);
    expect(result.length).toBe(input.length + 2);
  });

  it('appended bytes match lo/hi of computed CRC', () => {
    const input = new Uint8Array([0x01, 0x03, 0x00, 0x00, 0x00, 0x0a]);
    const crc = modbusCrc16(input);
    const result = appendCrc(input);
    expect(result[result.length - 2]).toBe(crc & 0xff);
    expect(result[result.length - 1]).toBe((crc >> 8) & 0xff);
  });

  it('does not modify the input array', () => {
    const input = new Uint8Array([0x01, 0x03]);
    const copy = new Uint8Array(input);
    appendCrc(input);
    expect(input).toEqual(copy);
  });
});

// ─────────────────────────────────────────────────────────────────────────────
// 3. buildReadFrame
// ─────────────────────────────────────────────────────────────────────────────
describe('buildReadFrame', () => {
  it('builds exact bytes for FC03 slave=1 addr=0 count=10', () => {
    const frame = buildReadFrame(1, 0x03, 0x0000, 10);
    expect(Array.from(frame)).toEqual([0x01, 0x03, 0x00, 0x00, 0x00, 0x0a, 0xc5, 0xcd]);
  });

  it('encodes address in big-endian at bytes[2..3] (FC01 addr=0x0100)', () => {
    const frame = buildReadFrame(1, 0x01, 0x0100, 1);
    expect(frame[2]).toBe(0x01);
    expect(frame[3]).toBe(0x00);
  });

  it('always produces 8 bytes total', () => {
    const frame = buildReadFrame(5, 0x04, 0x1234, 100);
    expect(frame.length).toBe(8);
  });
});

// ─────────────────────────────────────────────────────────────────────────────
// 4. buildWriteSingleCoil
// ─────────────────────────────────────────────────────────────────────────────
describe('buildWriteSingleCoil', () => {
  it('value=1 → bytes[4]=0xFF bytes[5]=0x00 (coil ON)', () => {
    const frame = buildWriteSingleCoil(1, 0, 1);
    expect(frame[4]).toBe(0xff);
    expect(frame[5]).toBe(0x00);
  });

  it('value=0 → bytes[4]=0x00 bytes[5]=0x00 (coil OFF)', () => {
    const frame = buildWriteSingleCoil(1, 0, 0);
    expect(frame[4]).toBe(0x00);
    expect(frame[5]).toBe(0x00);
  });

  it('any non-zero value → same as value=1 (0xFF00)', () => {
    const on = buildWriteSingleCoil(1, 0, 1);
    const nonzero = buildWriteSingleCoil(1, 0, 255);
    expect(nonzero[4]).toBe(on[4]);
    expect(nonzero[5]).toBe(on[5]);
  });
});

// ─────────────────────────────────────────────────────────────────────────────
// 5. buildWriteSingleRegister
// ─────────────────────────────────────────────────────────────────────────────
describe('buildWriteSingleRegister', () => {
  it('builds exact bytes for slave=1 addr=0x0010 value=100', () => {
    const frame = buildWriteSingleRegister(1, 0x0010, 100);
    expect(Array.from(frame)).toEqual([0x01, 0x06, 0x00, 0x10, 0x00, 0x64, 0x89, 0xe4]);
  });

  it('value=0xFFFF → bytes[4]=0xFF bytes[5]=0xFF', () => {
    const frame = buildWriteSingleRegister(1, 0, 0xffff);
    expect(frame[4]).toBe(0xff);
    expect(frame[5]).toBe(0xff);
  });
});

// ─────────────────────────────────────────────────────────────────────────────
// 6. buildWriteMultipleRegisters
// ─────────────────────────────────────────────────────────────────────────────
describe('buildWriteMultipleRegisters', () => {
  it('single value [42] → quantity=1, byte_count=2, total 11 bytes', () => {
    const frame = buildWriteMultipleRegisters(1, 0, [42]);
    expect(frame[4]).toBe(0x00);
    expect(frame[5]).toBe(0x01);
    expect(frame[6]).toBe(0x02);
    expect(frame.length).toBe(11);
  });

  it('single value encoded in big-endian at positions 7-8', () => {
    const frame = buildWriteMultipleRegisters(1, 0, [0x1234]);
    expect(frame[7]).toBe(0x12);
    expect(frame[8]).toBe(0x34);
  });

  it('three values → quantity=3, byte_count=6, all registers big-endian, total 15 bytes', () => {
    const frame = buildWriteMultipleRegisters(1, 0x0000, [0x1234, 0x5678, 0xabcd]);
    // slave, fc, addr(2), quantity(2), byte_count(1), data(6), crc(2) = 15
    expect(frame.length).toBe(15);
    expect(frame[4]).toBe(0x00); // quantity hi
    expect(frame[5]).toBe(0x03); // quantity lo = 3
    expect(frame[6]).toBe(0x06); // byte_count = 2 * 3
    expect(Array.from(frame.slice(7, 13))).toEqual([0x12, 0x34, 0x56, 0x78, 0xab, 0xcd]);
  });

  it('two values → exact frame bytes including CRC', () => {
    const frame = buildWriteMultipleRegisters(1, 0x0000, [0x1234, 0x5678]);
    const body = [0x01, 0x10, 0x00, 0x00, 0x00, 0x02, 0x04, 0x12, 0x34, 0x56, 0x78];
    const crc = appendCrc(new Uint8Array(body));
    expect(Array.from(frame)).toEqual(Array.from(crc));
  });
});

// ─────────────────────────────────────────────────────────────────────────────
// 7. buildWriteMultipleCoils
// ─────────────────────────────────────────────────────────────────────────────
describe('buildWriteMultipleCoils', () => {
  it('single coil [1] → quantity=1, byte_count=1, coilByte=0x01, total 10 bytes', () => {
    const frame = buildWriteMultipleCoils(1, 0, [1]);
    expect(frame[4]).toBe(0x00);
    expect(frame[5]).toBe(0x01);
    expect(frame[6]).toBe(0x01); // byte_count = ceil(1/8) = 1
    expect(frame[7]).toBe(0x01);
    expect(frame.length).toBe(10);
  });

  it('single coil [0] → coilByte=0x00', () => {
    const frame = buildWriteMultipleCoils(1, 0, [0]);
    expect(frame[7]).toBe(0x00);
  });

  it('eight coils pack LSB-first into one data byte', () => {
    // coils: index0..7 = 1,0,1,1,0,0,0,1 → bit0,bit2,bit3,bit7 set → 0b10001101 = 0x8D
    const frame = buildWriteMultipleCoils(1, 0, [1, 0, 1, 1, 0, 0, 0, 1]);
    expect(frame[4]).toBe(0x00);
    expect(frame[5]).toBe(0x08); // quantity = 8
    expect(frame[6]).toBe(0x01); // byte_count = ceil(8/8) = 1
    expect(frame[7]).toBe(0x8d);
    expect(frame.length).toBe(10); // 7 header + 1 data + 2 crc
  });

  it('nine coils span two data bytes (LSB-first), byte_count=2', () => {
    // coils index0..8 = 1,0,1,1,0,0,0,0,1
    // byte0: bit0,bit2,bit3 → 0b00001101 = 0x0D ; byte1: bit0 → 0x01
    const frame = buildWriteMultipleCoils(1, 0, [1, 0, 1, 1, 0, 0, 0, 0, 1]);
    expect(frame[5]).toBe(0x09); // quantity = 9
    expect(frame[6]).toBe(0x02); // byte_count = ceil(9/8) = 2
    expect(frame[7]).toBe(0x0d);
    expect(frame[8]).toBe(0x01);
    expect(frame.length).toBe(11); // 7 header + 2 data + 2 crc
  });
});

// ─────────────────────────────────────────────────────────────────────────────
// 8. buildPreviewFrame boundary / parseModbusAddress tests
// ─────────────────────────────────────────────────────────────────────────────
describe('buildPreviewFrame', () => {
  it('slaveStr="01" (decimal) → valid frame', () => {
    expect(buildPreviewFrame('01', '03', '0', '10', 'read')).not.toBeNull();
  });

  it('slaveStr="0x01" (hex) → same as "01"', () => {
    const dec = buildPreviewFrame('01', '03', '0', '10', 'read');
    const hex = buildPreviewFrame('0x01', '03', '0', '10', 'read');
    expect(hex).not.toBeNull();
    expect(Array.from(hex!)).toEqual(Array.from(dec!));
  });

  it('slaveStr="0" → null (slave < 1)', () => {
    expect(buildPreviewFrame('0', '03', '0', '10', 'read')).toBeNull();
  });

  it('slaveStr="248" → null (slave > 247)', () => {
    expect(buildPreviewFrame('248', '03', '0', '10', 'read')).toBeNull();
  });

  it('slaveStr="247" → valid frame (boundary)', () => {
    expect(buildPreviewFrame('247', '03', '0', '10', 'read')).not.toBeNull();
  });

  it('addrStr="0xFFFF" → addr=65535, valid frame', () => {
    expect(buildPreviewFrame('1', '03', '0xFFFF', '10', 'read')).not.toBeNull();
  });

  it('addrStr="65536" → null (addr > 0xFFFF)', () => {
    expect(buildPreviewFrame('1', '03', '65536', '10', 'read')).toBeNull();
  });

  it('read mode count=0 → null', () => {
    expect(buildPreviewFrame('1', '03', '0', '0', 'read')).toBeNull();
  });

  it('read mode count=2000 → valid (boundary)', () => {
    expect(buildPreviewFrame('1', '03', '0', '2000', 'read')).not.toBeNull();
  });

  it('read mode count=2001 → null (count > 2000)', () => {
    expect(buildPreviewFrame('1', '03', '0', '2001', 'read')).toBeNull();
  });

  it('read mode with invalid FC ("07") → null', () => {
    expect(buildPreviewFrame('1', '07', '0', '10', 'read')).toBeNull();
  });

  it('FC06 write val=65536 → null (> 0xFFFF)', () => {
    expect(buildPreviewFrame('1', '06', '0', '65536', 'write')).toBeNull();
  });

  it('FC06 write val=-1 → null (< 0)', () => {
    expect(buildPreviewFrame('1', '06', '0', '-1', 'write')).toBeNull();
  });

  it('FC06 write val=65535 → not null (boundary)', () => {
    expect(buildPreviewFrame('1', '06', '0', '65535', 'write')).not.toBeNull();
  });

  it('FC06 write val=0 → not null (boundary)', () => {
    expect(buildPreviewFrame('1', '06', '0', '0', 'write')).not.toBeNull();
  });

  it('FC10 write val=70000 → null (regression: silent truncation 70000 → 4464)', () => {
    expect(buildPreviewFrame('1', '10', '0', '70000', 'write')).toBeNull();
  });

  it('FC05 coil write val=2 → null (coil must be 0 or 1)', () => {
    expect(buildPreviewFrame('1', '05', '0', '2', 'write')).toBeNull();
  });

  it('FC05 coil write val=1 → not null', () => {
    expect(buildPreviewFrame('1', '05', '0', '1', 'write')).not.toBeNull();
  });

  it('FC05 coil write val=0 → not null', () => {
    expect(buildPreviewFrame('1', '05', '0', '0', 'write')).not.toBeNull();
  });

  // ── Multi-value FC16 (write multiple registers) ────────────────────────────
  it('FC16 write list "10,20,30" → valid, quantity=3, byte_count=6', () => {
    const frame = buildPreviewFrame('1', '10', '0', '10,20,30', 'write');
    expect(frame).not.toBeNull();
    expect(frame![5]).toBe(0x03); // quantity lo
    expect(frame![6]).toBe(0x06); // byte_count
  });

  it('FC16 write list accepts space and comma separators "10 20, 30"', () => {
    const frame = buildPreviewFrame('1', '10', '0', '10 20, 30', 'write');
    expect(frame).not.toBeNull();
    expect(frame![5]).toBe(0x03);
  });

  it('FC16 write empty list "" → null', () => {
    expect(buildPreviewFrame('1', '10', '0', '', 'write')).toBeNull();
  });

  it('FC16 write list with an out-of-range value "10,70000" → null', () => {
    expect(buildPreviewFrame('1', '10', '0', '10,70000', 'write')).toBeNull();
  });

  it('FC16 write list with a non-numeric token "10,x" → null', () => {
    expect(buildPreviewFrame('1', '10', '0', '10,x', 'write')).toBeNull();
  });

  it('FC16 write list count=123 → valid (boundary)', () => {
    const vals = Array.from({ length: 123 }, () => '1').join(',');
    expect(buildPreviewFrame('1', '10', '0', vals, 'write')).not.toBeNull();
  });

  it('FC16 write list count=124 → null (> 123)', () => {
    const vals = Array.from({ length: 124 }, () => '1').join(',');
    expect(buildPreviewFrame('1', '10', '0', vals, 'write')).toBeNull();
  });

  // ── Multi-value FC15 (write multiple coils) ────────────────────────────────
  it('FC15 write list "1,0,1" → valid, quantity=3, byte_count=1', () => {
    const frame = buildPreviewFrame('1', '0f', '0', '1,0,1', 'write');
    expect(frame).not.toBeNull();
    expect(frame![5]).toBe(0x03); // quantity lo
    expect(frame![6]).toBe(0x01); // byte_count = ceil(3/8)
  });

  it('FC15 write list with value 2 "1,2" → null (coil must be 0 or 1)', () => {
    expect(buildPreviewFrame('1', '0f', '0', '1,2', 'write')).toBeNull();
  });

  it('FC15 write empty list "" → null', () => {
    expect(buildPreviewFrame('1', '0f', '0', '', 'write')).toBeNull();
  });

  it('FC15 write list count=1968 → valid (boundary)', () => {
    const vals = Array.from({ length: 1968 }, () => '1').join(',');
    expect(buildPreviewFrame('1', '0f', '0', vals, 'write')).not.toBeNull();
  });

  it('FC15 write list count=1969 → null (> 1968)', () => {
    const vals = Array.from({ length: 1969 }, () => '1').join(',');
    expect(buildPreviewFrame('1', '0f', '0', vals, 'write')).toBeNull();
  });

  // ── Strict decimal parsing of value/count fields (address/slave keep 0x support) ────
  it('read count "1.5" → null (float rejected)', () => {
    expect(buildPreviewFrame('1', '03', '0', '1.5', 'read')).toBeNull();
  });

  it('read count "10abc" → null (trailing garbage rejected)', () => {
    expect(buildPreviewFrame('1', '03', '0', '10abc', 'read')).toBeNull();
  });

  it('FC06 write value "5x" → null (partial token rejected)', () => {
    expect(buildPreviewFrame('1', '06', '0', '5x', 'write')).toBeNull();
  });

  it('FC06 write value "0x10" → null (hex not allowed in decimal value field)', () => {
    expect(buildPreviewFrame('1', '06', '0', '0x10', 'write')).toBeNull();
  });

  it('FC16 write list "10,1.5,30" → null (one float token rejected)', () => {
    expect(buildPreviewFrame('1', '10', '0', '10,1.5,30', 'write')).toBeNull();
  });

  it('FC16 write list "10,0x10" → null (hex token rejected)', () => {
    expect(buildPreviewFrame('1', '10', '0', '10,0x10', 'write')).toBeNull();
  });

  it('address still accepts 0x prefix (unchanged) while value stays decimal', () => {
    expect(buildPreviewFrame('1', '06', '0x0010', '256', 'write')).not.toBeNull();
  });
});

// ─────────────────────────────────────────────────────────────────────────────
// 8b. parseValueList
// ─────────────────────────────────────────────────────────────────────────────
describe('parseValueList', () => {
  it('splits on commas', () => {
    expect(parseValueList('10,20,30')).toEqual([10, 20, 30]);
  });

  it('splits on whitespace and commas, dropping empties', () => {
    expect(parseValueList(' 10 , 20,  30 ')).toEqual([10, 20, 30]);
  });

  it('empty / whitespace-only string → []', () => {
    expect(parseValueList('')).toEqual([]);
    expect(parseValueList('   ')).toEqual([]);
  });

  it('non-numeric tokens become NaN', () => {
    const result = parseValueList('10,x,30');
    expect(result.length).toBe(3);
    expect(result[0]).toBe(10);
    expect(Number.isNaN(result[1])).toBe(true);
    expect(result[2]).toBe(30);
  });

  it('rejects partial/float/hex tokens as NaN (strict integers only)', () => {
    // "1.5" → 1, "5x" → 5, "0x10" → 0, "10abc" → 10 under parseInt; strict parse gives NaN.
    const result = parseValueList('1.5,5x,0x10,10abc');
    expect(result.length).toBe(4);
    for (const v of result) {
      expect(Number.isNaN(v)).toBe(true);
    }
  });

  it('keeps valid signed integers intact', () => {
    expect(parseValueList('0, 1, 42, -3')).toEqual([0, 1, 42, -3]);
  });
});

// ─────────────────────────────────────────────────────────────────────────────
// 8c. parseStrictInt
// ─────────────────────────────────────────────────────────────────────────────
describe('parseStrictInt', () => {
  it('parses whole signed integers', () => {
    expect(parseStrictInt('0')).toBe(0);
    expect(parseStrictInt(' 42 ')).toBe(42);
    expect(parseStrictInt('-7')).toBe(-7);
  });

  it('rejects float / partial / hex / garbage tokens as NaN', () => {
    expect(Number.isNaN(parseStrictInt('1.5'))).toBe(true);
    expect(Number.isNaN(parseStrictInt('5x'))).toBe(true);
    expect(Number.isNaN(parseStrictInt('0x10'))).toBe(true);
    expect(Number.isNaN(parseStrictInt('10abc'))).toBe(true);
    expect(Number.isNaN(parseStrictInt(''))).toBe(true);
  });
});

// ─────────────────────────────────────────────────────────────────────────────
// 9. frameToPreviewParts
// ─────────────────────────────────────────────────────────────────────────────
describe('frameToPreviewParts', () => {
  it('8-byte frame → last 2 entries isCrc=true, first 6 isCrc=false', () => {
    const frame = buildReadFrame(1, 0x03, 0, 10); // 8 bytes
    const parts = frameToPreviewParts(frame);
    expect(parts.length).toBe(8);
    for (let i = 0; i < 6; i++) {
      expect(parts[i].isCrc).toBe(false);
    }
    expect(parts[6].isCrc).toBe(true);
    expect(parts[7].isCrc).toBe(true);
  });

  it('empty array (length < 2) → returns []', () => {
    expect(frameToPreviewParts(new Uint8Array([]))).toEqual([]);
  });
});

// ─────────────────────────────────────────────────────────────────────────────
// 10. sendPacketToPort (fetch integration)
// ─────────────────────────────────────────────────────────────────────────────
describe('sendPacketToPort', () => {
  afterEach(() => {
    vi.unstubAllGlobals();
  });

  it('fetch 200 {sent:8} → resolves to {sent: 8}', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({ sent: 8 }),
    }));
    await expect(sendPacketToPort('1', '01030000000AC5CD')).resolves.toEqual({ sent: 8 });
  });

  it('fetch 400 {error:"bad hex"} → rejects with Error("bad hex")', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue({
      ok: false,
      statusText: 'Bad Request',
      json: async () => ({ error: 'bad hex' }),
    }));
    await expect(sendPacketToPort('1', '010')).rejects.toThrow('bad hex');
  });

  it('fetch 500 with non-JSON body → rejects with Error(statusText)', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue({
      ok: false,
      statusText: 'Internal Server Error',
      json: async () => {
 throw new Error('not json');
},
    }));
    await expect(sendPacketToPort('1', 'AABB')).rejects.toThrow('Internal Server Error');
  });

  it('fetch 401 {error:"Unauthorized"} → rejects with Error("Unauthorized")', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue({
      ok: false,
      statusText: 'Unauthorized',
      json: async () => ({ error: 'Unauthorized' }),
    }));
    await expect(sendPacketToPort('1', 'AABB')).rejects.toThrow('Unauthorized');
  });
});
