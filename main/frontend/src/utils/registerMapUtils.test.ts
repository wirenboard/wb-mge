import { describe, it, expect } from 'vitest';
import {
  formatAgeUs,
  formatMemory,
  formatAge,
  typeName,
  resolvePortSelection,
  buildDevices,
  buildRegsByKey,
  buildExportPayload,
  type CacheEntry,
} from './registerMapUtils';

// ============================================================
// formatAgeUs
// ============================================================
describe('formatAgeUs', () => {
  it('returns em-dash for 0 µs', () => {
    expect(formatAgeUs(0)).toBe('—');
  });

  it('returns "1 s" for 1_000_000 µs (1 second)', () => {
    expect(formatAgeUs(1_000_000)).toBe('1 s');
  });

  it('returns "59 s" for 59_000_000 µs (59 seconds)', () => {
    expect(formatAgeUs(59_000_000)).toBe('59 s');
  });

  it('returns "1 min 0 s" for 60_000_000 µs (60 seconds)', () => {
    expect(formatAgeUs(60_000_000)).toBe('1 min 0 s');
  });

  it('returns "1 min 30 s" for 90_000_000 µs (90 seconds)', () => {
    expect(formatAgeUs(90_000_000)).toBe('1 min 30 s');
  });

  it('returns "1 h 0 min" for 3_600_000_000 µs (1 hour)', () => {
    expect(formatAgeUs(3_600_000_000)).toBe('1 h 0 min');
  });

  it('returns "1 h 1 min" for 3_690_000_000 µs (1 h 1.5 min — seconds are dropped in hours format)', () => {
    expect(formatAgeUs(3_690_000_000)).toBe('1 h 1 min');
  });
});

// ============================================================
// formatMemory
// ============================================================
describe('formatMemory', () => {
  it('returns em-dash for 0 bytes', () => {
    expect(formatMemory(0)).toBe('—');
  });

  it('returns "512 B" for 512 bytes', () => {
    expect(formatMemory(512)).toBe('512 B');
  });

  it('returns "1023 B" for 1023 bytes (boundary before KB)', () => {
    expect(formatMemory(1023)).toBe('1023 B');
  });

  it('returns "1.0 KB" for 1024 bytes (exactly 1 KB)', () => {
    expect(formatMemory(1024)).toBe('1.0 KB');
  });

  it('returns "2.0 KB" for 2048 bytes (exactly 2 KB)', () => {
    expect(formatMemory(2048)).toBe('2.0 KB');
  });

  it('returns "1.5 KB" for 1536 bytes (1.5 KB)', () => {
    expect(formatMemory(1536)).toBe('1.5 KB');
  });
});

// ============================================================
// formatAge
// ============================================================
describe('formatAge', () => {
  it('returns "< 1 s" for 0 seconds', () => {
    expect(formatAge(0)).toBe('< 1 s');
  });

  it('returns "< 1 s" for 0.5 seconds', () => {
    expect(formatAge(0.5)).toBe('< 1 s');
  });

  it('returns "1.0 s" for exactly 1 second', () => {
    expect(formatAge(1)).toBe('1.0 s');
  });

  it('returns "59.9 s" for 59.9 seconds', () => {
    expect(formatAge(59.9)).toBe('59.9 s');
  });

  it('returns "59.9 s" for 59.99 seconds (floor, not round)', () => {
    expect(formatAge(59.99)).toBe('59.9 s');
  });

  it('returns "1 min 0 s" for exactly 60 seconds', () => {
    expect(formatAge(60)).toBe('1 min 0 s');
  });

  it('returns "1 min 1 s" for 61 seconds', () => {
    expect(formatAge(61)).toBe('1 min 1 s');
  });

  it('returns "1 h 0 min" for exactly 3600 seconds', () => {
    expect(formatAge(3600)).toBe('1 h 0 min');
  });

  it('returns "1 h 1 min" for 3660 seconds (1 hour 1 minute)', () => {
    expect(formatAge(3660)).toBe('1 h 1 min');
  });
});

// ============================================================
// typeName
// ============================================================
describe('typeName', () => {
  it('maps "h" to "Holding"', () => {
    expect(typeName('h')).toBe('Holding');
  });

  it('maps "i" to "Input"', () => {
    expect(typeName('i')).toBe('Input');
  });

  it('maps "c" to "Coil"', () => {
    expect(typeName('c')).toBe('Coil');
  });

  it('maps "d" to "Discrete"', () => {
    expect(typeName('d')).toBe('Discrete');
  });

  it('returns the input itself for unknown type code', () => {
    expect(typeName('x')).toBe('x');
  });
});

// ============================================================
// resolvePortSelection
// ============================================================
describe('resolvePortSelection', () => {
  it('(true, true) → {p1: true, p2: false} — both selected, keep port 1', () => {
    expect(resolvePortSelection(true, true)).toEqual({ p1: true, p2: false });
  });

  it('(false, false) → {p1: true, p2: false} — none selected, fallback to port 1', () => {
    expect(resolvePortSelection(false, false)).toEqual({ p1: true, p2: false });
  });

  it('(true, false) → {p1: true, p2: false} — normal case, port 1 selected', () => {
    expect(resolvePortSelection(true, false)).toEqual({ p1: true, p2: false });
  });

  it('(false, true) → {p1: false, p2: true} — normal case, port 2 selected', () => {
    expect(resolvePortSelection(false, true)).toEqual({ p1: false, p2: true });
  });
});

// ============================================================
// buildDevices
// ============================================================
describe('buildDevices', () => {
  it('returns empty array for empty entries', () => {
    expect(buildDevices([], 0)).toEqual([]);
  });

  it('single entry slave=1, t="h", ts=100, nowS=200 → lastSeenAge=100', () => {
    const entries: CacheEntry[] = [{ s: 1, t: 'h', a: 0, v: 0, ts: 100 }];
    const result = buildDevices(entries, 200);
    expect(result).toHaveLength(1);
    expect(result[0].id).toBe(1);
    expect(result[0].groups).toEqual(['Holding']);
    expect(result[0].lastSeenAge).toBe(100);
  });

  it('multiple types for one slave respect TYPE_ORDER: Holding before Coil', () => {
    const entries: CacheEntry[] = [
      { s: 1, t: 'c', a: 0, v: 0, ts: 10 },
      { s: 1, t: 'h', a: 0, v: 0, ts: 10 },
    ];
    const result = buildDevices(entries, 100);
    expect(result[0].groups).toEqual(['Holding', 'Coil']);
  });

  it('all four register types appear in TYPE_ORDER: Holding, Input, Coil, Discrete', () => {
    // Entries provided in reverse order to verify TYPE_ORDER is enforced, not insertion order
    const entries: CacheEntry[] = [
      { s: 1, t: 'd', a: 0, v: 0, ts: 10 },
      { s: 1, t: 'c', a: 0, v: 0, ts: 10 },
      { s: 1, t: 'i', a: 0, v: 0, ts: 10 },
      { s: 1, t: 'h', a: 0, v: 0, ts: 10 },
    ];
    const result = buildDevices(entries, 100);
    expect(result[0].groups).toEqual(['Holding', 'Input', 'Coil', 'Discrete']);
  });

  it('two slaves id=1 and id=3 are sorted ascending by id', () => {
    const entries: CacheEntry[] = [
      { s: 3, t: 'h', a: 0, v: 0, ts: 10 },
      { s: 1, t: 'h', a: 0, v: 0, ts: 10 },
    ];
    const result = buildDevices(entries, 100);
    expect(result[0].id).toBe(1);
    expect(result[1].id).toBe(3);
  });

  it('nowS=0 → lastSeenAge=0 regardless of ts', () => {
    const entries: CacheEntry[] = [{ s: 1, t: 'h', a: 0, v: 0, ts: 12345 }];
    const result = buildDevices(entries, 0);
    expect(result[0].lastSeenAge).toBe(0);
  });

  it('wrap-around: nowS=3, slaveMaxTs=65534 → lastSeenAge=5', () => {
    // (3 - 65534 + 65536) % 65536 = 5
    const entries: CacheEntry[] = [{ s: 1, t: 'h', a: 0, v: 0, ts: 65534 }];
    const result = buildDevices(entries, 3);
    expect(result[0].lastSeenAge).toBe(5);
  });

  it('slaveMaxTs is max ts among entries of the slave: ts=10 and ts=50, nowS=100 → lastSeenAge=50', () => {
    // max(10, 50) = 50; (100 - 50 + 65536) % 65536 = 50
    const entries: CacheEntry[] = [
      { s: 1, t: 'h', a: 0, v: 0, ts: 10 },
      { s: 1, t: 'h', a: 1, v: 0, ts: 50 },
    ];
    const result = buildDevices(entries, 100);
    expect(result[0].lastSeenAge).toBe(50);
  });
});

// ============================================================
// buildRegsByKey
// ============================================================
describe('buildRegsByKey', () => {
  it('returns empty object for empty entries', () => {
    expect(buildRegsByKey([], 0, 60)).toEqual({});
  });

  it('FC h: key is "1|Holding", val is 0x-prefixed uppercase padded, correct updatedAge', () => {
    const entries: CacheEntry[] = [{ s: 1, t: 'h', a: 100, v: 255, ts: 190 }];
    const result = buildRegsByKey(entries, 200, 60);
    expect(result['1|Holding']).toBeDefined();
    const row = result['1|Holding'][0];
    expect(row.val).toBe('0x00FF');
    expect(row.updatedAge).toBe(10);
    expect(row.stale).toBe(false);
    expect(row.addr).toBe(100);
  });

  it('FC i: value 0x1234 → val is "0x1234" (uppercase, 4-digit padded)', () => {
    const entries: CacheEntry[] = [{ s: 1, t: 'i', a: 0, v: 0x1234, ts: 100 }];
    const result = buildRegsByKey(entries, 200, 60);
    expect(result['1|Input'][0].val).toBe('0x1234');
  });

  it('FC c (coil): value=1 → val is "1" (decimal string, not hex)', () => {
    const entries: CacheEntry[] = [{ s: 1, t: 'c', a: 0, v: 1, ts: 100 }];
    const result = buildRegsByKey(entries, 200, 60);
    expect(result['1|Coil'][0].val).toBe('1');
  });

  it('FC d (discrete): value=0 → val is "0"', () => {
    const entries: CacheEntry[] = [{ s: 1, t: 'd', a: 0, v: 0, ts: 100 }];
    const result = buildRegsByKey(entries, 200, 60);
    expect(result['1|Discrete'][0].val).toBe('0');
  });

  it('stale=true when updatedAge > valueTimeout > 0', () => {
    // updatedAge = (200 - 100 + 65536) % 65536 = 100; valueTimeout=50; 100 > 50 → stale
    const entries: CacheEntry[] = [{ s: 1, t: 'h', a: 0, v: 0, ts: 100 }];
    const result = buildRegsByKey(entries, 200, 50);
    expect(result['1|Holding'][0].stale).toBe(true);
  });

  it('stale=false when valueTimeout=0 (timeout disabled)', () => {
    // Same ages but valueTimeout=0 → stale should never be true
    const entries: CacheEntry[] = [{ s: 1, t: 'h', a: 0, v: 0, ts: 100 }];
    const result = buildRegsByKey(entries, 200, 0);
    expect(result['1|Holding'][0].stale).toBe(false);
  });

  it('stale=false when updatedAge <= valueTimeout', () => {
    // updatedAge = 10; valueTimeout = 60; 10 <= 60 → not stale
    const entries: CacheEntry[] = [{ s: 1, t: 'h', a: 0, v: 0, ts: 190 }];
    const result = buildRegsByKey(entries, 200, 60);
    expect(result['1|Holding'][0].stale).toBe(false);
  });

  it('stale=false when updatedAge exactly equals valueTimeout (strict greater-than condition)', () => {
    // stale condition is updatedAge > valueTimeout (strict), so equality must not trigger stale
    // updatedAge = (160 - 100 + 65536) % 65536 = 60; valueTimeout=60; 60 > 60 is false → stale=false
    const entries: CacheEntry[] = [{ s: 1, t: 'h', a: 0, v: 0, ts: 100 }];
    const result = buildRegsByKey(entries, 160, 60);
    expect(result['1|Holding'][0].stale).toBe(false);
  });

  it('nowS=0 → updatedAge=0, stale=false regardless of ts', () => {
    const entries: CacheEntry[] = [{ s: 1, t: 'h', a: 0, v: 0, ts: 100 }];
    const result = buildRegsByKey(entries, 0, 50);
    expect(result['1|Holding'][0].updatedAge).toBe(0);
    expect(result['1|Holding'][0].stale).toBe(false);
  });

  it('dedup normal: two entries same slave+type+addr, ts=50,v=111 and ts=100,v=222 → keep newer v=222', () => {
    const entries: CacheEntry[] = [
      { s: 1, t: 'h', a: 100, v: 111, ts: 50 },
      { s: 1, t: 'h', a: 100, v: 222, ts: 100 },
    ];
    const result = buildRegsByKey(entries, 200, 60);
    expect(result['1|Holding']).toHaveLength(1);
    expect(result['1|Holding'][0].ts).toBe(100);
    expect(result['1|Holding'][0].val).toBe('0x00DE'); // 222 = 0xDE
  });

  it('dedup wrap-around: ts=5 is newer than ts=65530 in uint16 space, ts=5 wins', () => {
    // ((5 - 65530 + 65536) % 65536) = (11) < 32768 → ts=5 is newer → entry with ts=5 replaces ts=65530
    const entries: CacheEntry[] = [
      { s: 1, t: 'h', a: 100, v: 0xAAAA, ts: 65530 }, // older in uint16 space
      { s: 1, t: 'h', a: 100, v: 0xBBBB, ts: 5 },     // newer in uint16 space (just wrapped)
    ];
    const result = buildRegsByKey(entries, 10, 60);
    expect(result['1|Holding']).toHaveLength(1);
    expect(result['1|Holding'][0].ts).toBe(5);
    expect(result['1|Holding'][0].val).toBe('0xBBBB');
  });

  it('sorts entries by addr ascending', () => {
    const entries: CacheEntry[] = [
      { s: 1, t: 'h', a: 200, v: 1, ts: 100 },
      { s: 1, t: 'h', a: 100, v: 2, ts: 100 },
    ];
    const result = buildRegsByKey(entries, 200, 60);
    expect(result['1|Holding'][0].addr).toBe(100);
    expect(result['1|Holding'][1].addr).toBe(200);
  });
});

// ============================================================
// buildExportPayload
// ============================================================
describe('buildExportPayload', () => {
  it('returns { slaves: {} } for empty entries', () => {
    expect(buildExportPayload([])).toEqual({ slaves: {} });
  });

  it('single entry slave=1, t="h", addr=100, v=255 → correct holding_registers value', () => {
    const entries: CacheEntry[] = [{ s: 1, t: 'h', a: 100, v: 255, ts: 50 }];
    const result = buildExportPayload(entries);
    expect(result.slaves['1']).toBeDefined();
    expect(result.slaves['1'].slave_id).toBe(1);
    expect(result.slaves['1'].holding_registers['100']).toBe(255);
  });

  it('all four section keys are always present even when empty', () => {
    const entries: CacheEntry[] = [{ s: 1, t: 'h', a: 0, v: 0, ts: 1 }];
    const result = buildExportPayload(entries);
    const slave = result.slaves['1'];
    expect(slave).toHaveProperty('holding_registers');
    expect(slave).toHaveProperty('input_registers');
    expect(slave).toHaveProperty('coils');
    expect(slave).toHaveProperty('discrete_inputs');
    // Sections not populated by entries should be empty objects
    expect(slave.input_registers).toEqual({});
    expect(slave.coils).toEqual({});
    expect(slave.discrete_inputs).toEqual({});
  });

  it('dedup same slave+section+addr: keep entry with newer ts', () => {
    const entries: CacheEntry[] = [
      { s: 1, t: 'h', a: 10, v: 111, ts: 50 },  // older
      { s: 1, t: 'h', a: 10, v: 222, ts: 100 }, // newer
    ];
    const result = buildExportPayload(entries);
    expect(result.slaves['1'].holding_registers['10']).toBe(222);
  });

  it('wrap-around dedup: ts=5 wins over ts=65530 for same addr', () => {
    // ((5 - 65530 + 65536) % 65536) = 11 < 32768 → ts=5 is newer
    const entries: CacheEntry[] = [
      { s: 1, t: 'h', a: 10, v: 0xAAAA, ts: 65530 }, // older in uint16 space
      { s: 1, t: 'h', a: 10, v: 0xBBBB, ts: 5 },     // newer in uint16 space (just wrapped)
    ];
    const result = buildExportPayload(entries);
    expect(result.slaves['1'].holding_registers['10']).toBe(0xBBBB);
  });

  it('two slaves are both present in slaves object', () => {
    const entries: CacheEntry[] = [
      { s: 1, t: 'h', a: 0, v: 10, ts: 1 },
      { s: 3, t: 'i', a: 5, v: 20, ts: 1 },
    ];
    const result = buildExportPayload(entries);
    expect(result.slaves['1']).toBeDefined();
    expect(result.slaves['3']).toBeDefined();
  });

  it('slave_id field matches the numeric slave ID', () => {
    const entries: CacheEntry[] = [{ s: 42, t: 'c', a: 0, v: 1, ts: 1 }];
    const result = buildExportPayload(entries);
    expect(result.slaves['42'].slave_id).toBe(42);
  });
});
