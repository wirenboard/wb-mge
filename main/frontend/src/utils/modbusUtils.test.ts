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
  it('quantity bytes at positions 4-5 are always [0x00, 0x01]', () => {
    const frame = buildWriteMultipleRegisters(1, 0, 42);
    expect(frame[4]).toBe(0x00);
    expect(frame[5]).toBe(0x01);
  });

  it('byte_count at position 6 is always 0x02', () => {
    const frame = buildWriteMultipleRegisters(1, 0, 42);
    expect(frame[6]).toBe(0x02);
  });

  it('value encoded in big-endian at positions 7-8', () => {
    const frame = buildWriteMultipleRegisters(1, 0, 0x1234);
    expect(frame[7]).toBe(0x12);
    expect(frame[8]).toBe(0x34);
  });

  it('total length is 11 bytes', () => {
    const frame = buildWriteMultipleRegisters(1, 0, 10);
    expect(frame.length).toBe(11);
  });
});

// ─────────────────────────────────────────────────────────────────────────────
// 7. buildWriteMultipleCoils
// ─────────────────────────────────────────────────────────────────────────────
describe('buildWriteMultipleCoils', () => {
  it('quantity bytes at positions 4-5 are always [0x00, 0x01]', () => {
    const frame = buildWriteMultipleCoils(1, 0, 1);
    expect(frame[4]).toBe(0x00);
    expect(frame[5]).toBe(0x01);
  });

  it('byte_count at position 6 is 0x01 (NOT 0x02 — 1 byte shorter than FC16)', () => {
    const frame = buildWriteMultipleCoils(1, 0, 1);
    expect(frame[6]).toBe(0x01);
  });

  it('coilByte at position 7 is 0x01 when value=1', () => {
    const frame = buildWriteMultipleCoils(1, 0, 1);
    expect(frame[7]).toBe(0x01);
  });

  it('coilByte at position 7 is 0x00 when value=0', () => {
    const frame = buildWriteMultipleCoils(1, 0, 0);
    expect(frame[7]).toBe(0x00);
  });

  it('total length is 10 bytes (not 11)', () => {
    const frame = buildWriteMultipleCoils(1, 0, 1);
    expect(frame.length).toBe(10);
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
