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

  it('returns ">18h" for seconds >= 65535 (saturated age)', () => {
    expect(formatAge(65535)).toBe('>18h');
    expect(formatAge(65536)).toBe('>18h');
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
    expect(buildDevices([])).toEqual([]);
  });

  it('single entry slave=1, t="h", age=100 → lastSeenAge=100', () => {
    const entries: CacheEntry[] = [{ s: 1, t: 'h', a: 0, v: 0, age: 100 }];
    const result = buildDevices(entries);
    expect(result).toHaveLength(1);
    expect(result[0].id).toBe(1);
    expect(result[0].groups).toEqual(['Holding']);
    expect(result[0].lastSeenAge).toBe(100);
  });

  it('multiple types for one slave respect TYPE_ORDER: Holding before Coil', () => {
    const entries: CacheEntry[] = [
      { s: 1, t: 'c', a: 0, v: 0, age: 10 },
      { s: 1, t: 'h', a: 0, v: 0, age: 10 },
    ];
    const result = buildDevices(entries);
    expect(result[0].groups).toEqual(['Holding', 'Coil']);
  });

  it('all four register types appear in TYPE_ORDER: Holding, Input, Coil, Discrete', () => {
    // Entries provided in reverse order to verify TYPE_ORDER is enforced, not insertion order
    const entries: CacheEntry[] = [
      { s: 1, t: 'd', a: 0, v: 0, age: 10 },
      { s: 1, t: 'c', a: 0, v: 0, age: 10 },
      { s: 1, t: 'i', a: 0, v: 0, age: 10 },
      { s: 1, t: 'h', a: 0, v: 0, age: 10 },
    ];
    const result = buildDevices(entries);
    expect(result[0].groups).toEqual(['Holding', 'Input', 'Coil', 'Discrete']);
  });

  it('two slaves id=1 and id=3 are sorted ascending by id', () => {
    const entries: CacheEntry[] = [
      { s: 3, t: 'h', a: 0, v: 0, age: 10 },
      { s: 1, t: 'h', a: 0, v: 0, age: 10 },
    ];
    const result = buildDevices(entries);
    expect(result[0].id).toBe(1);
    expect(result[1].id).toBe(3);
  });

  it('age=0 entry → lastSeenAge=0 (freshly updated register)', () => {
    const entries: CacheEntry[] = [{ s: 1, t: 'h', a: 0, v: 0, age: 0 }];
    const result = buildDevices(entries);
    expect(result[0].lastSeenAge).toBe(0);
  });

  it('saturated age: age=65535 → lastSeenAge=65535', () => {
    // When age is saturated (not updated for 18+ h), lastSeenAge reflects that
    const entries: CacheEntry[] = [{ s: 1, t: 'h', a: 0, v: 0, age: 65535 }];
    const result = buildDevices(entries);
    expect(result[0].lastSeenAge).toBe(65535);
  });

  it('lastSeenAge is min age among entries of the slave: age=10 and age=50 → lastSeenAge=10', () => {
    // min(10, 50) = 10; most recently seen register has age=10
    const entries: CacheEntry[] = [
      { s: 1, t: 'h', a: 0, v: 0, age: 10 },
      { s: 1, t: 'h', a: 1, v: 0, age: 50 },
    ];
    const result = buildDevices(entries);
    expect(result[0].lastSeenAge).toBe(10);
  });
});

// ============================================================
// buildRegsByKey
// ============================================================
describe('buildRegsByKey', () => {
  it('returns empty object for empty entries', () => {
    expect(buildRegsByKey([], 60)).toEqual({});
  });

  it('FC h: key is "1|Holding", val is 0x-prefixed uppercase padded, correct updatedAge', () => {
    const entries: CacheEntry[] = [{ s: 1, t: 'h', a: 100, v: 255, age: 10 }];
    const result = buildRegsByKey(entries, 60);
    expect(result['1|Holding']).toBeDefined();
    const row = result['1|Holding'][0];
    expect(row.val).toBe('0x00FF');
    expect(row.updatedAge).toBe(10);
    expect(row.stale).toBe(false);
    expect(row.addr).toBe(100);
  });

  it('FC i: value 0x1234 → val is "0x1234" (uppercase, 4-digit padded)', () => {
    const entries: CacheEntry[] = [{ s: 1, t: 'i', a: 0, v: 0x1234, age: 5 }];
    const result = buildRegsByKey(entries, 60);
    expect(result['1|Input'][0].val).toBe('0x1234');
  });

  it('FC c (coil): value=1 → val is "1" (decimal string, not hex)', () => {
    const entries: CacheEntry[] = [{ s: 1, t: 'c', a: 0, v: 1, age: 5 }];
    const result = buildRegsByKey(entries, 60);
    expect(result['1|Coil'][0].val).toBe('1');
  });

  it('FC d (discrete): value=0 → val is "0"', () => {
    const entries: CacheEntry[] = [{ s: 1, t: 'd', a: 0, v: 0, age: 5 }];
    const result = buildRegsByKey(entries, 60);
    expect(result['1|Discrete'][0].val).toBe('0');
  });

  it('stale=true when updatedAge > valueTimeout > 0', () => {
    // age=100, valueTimeout=50; 100 > 50 → stale
    const entries: CacheEntry[] = [{ s: 1, t: 'h', a: 0, v: 0, age: 100 }];
    const result = buildRegsByKey(entries, 50);
    expect(result['1|Holding'][0].stale).toBe(true);
  });

  it('stale=false when valueTimeout=0 (timeout disabled)', () => {
    // age=100 but valueTimeout=0 → stale should never be true
    const entries: CacheEntry[] = [{ s: 1, t: 'h', a: 0, v: 0, age: 100 }];
    const result = buildRegsByKey(entries, 0);
    expect(result['1|Holding'][0].stale).toBe(false);
  });

  it('stale=false when updatedAge <= valueTimeout', () => {
    // age=10; valueTimeout=60; 10 <= 60 → not stale
    const entries: CacheEntry[] = [{ s: 1, t: 'h', a: 0, v: 0, age: 10 }];
    const result = buildRegsByKey(entries, 60);
    expect(result['1|Holding'][0].stale).toBe(false);
  });

  it('stale=false when updatedAge exactly equals valueTimeout (strict greater-than condition)', () => {
    // stale condition is updatedAge > valueTimeout (strict), so equality must not trigger stale
    // age=60; valueTimeout=60; 60 > 60 is false → stale=false
    const entries: CacheEntry[] = [{ s: 1, t: 'h', a: 0, v: 0, age: 60 }];
    const result = buildRegsByKey(entries, 60);
    expect(result['1|Holding'][0].stale).toBe(false);
  });

  it('age=0 → updatedAge=0, stale=false (freshly updated register)', () => {
    const entries: CacheEntry[] = [{ s: 1, t: 'h', a: 0, v: 0, age: 0 }];
    const result = buildRegsByKey(entries, 50);
    expect(result['1|Holding'][0].updatedAge).toBe(0);
    expect(result['1|Holding'][0].stale).toBe(false);
  });

  it('dedup normal: two entries same slave+type+addr, age=100,v=111 and age=50,v=222 → keep fresher (lower age) v=222', () => {
    // age=50 < age=100 → age=50 entry is fresher → v=222 wins
    const entries: CacheEntry[] = [
      { s: 1, t: 'h', a: 100, v: 111, age: 100 },
      { s: 1, t: 'h', a: 100, v: 222, age: 50 },
    ];
    const result = buildRegsByKey(entries, 60);
    expect(result['1|Holding']).toHaveLength(1);
    expect(result['1|Holding'][0].updatedAge).toBe(50);
    expect(result['1|Holding'][0].val).toBe('0x00DE'); // 222 = 0xDE
  });

  it('dedup: entry with smaller age wins — age=5 wins over age=100 for same addr', () => {
    // age=5 < age=100 → entry with age=5 (v=0xBBBB) is fresher → it wins
    const entries: CacheEntry[] = [
      { s: 1, t: 'h', a: 100, v: 0xAAAA, age: 100 }, // older
      { s: 1, t: 'h', a: 100, v: 0xBBBB, age: 5 },   // fresher
    ];
    const result = buildRegsByKey(entries, 60);
    expect(result['1|Holding']).toHaveLength(1);
    expect(result['1|Holding'][0].updatedAge).toBe(5);
    expect(result['1|Holding'][0].val).toBe('0xBBBB');
  });

  it('sorts entries by addr ascending', () => {
    const entries: CacheEntry[] = [
      { s: 1, t: 'h', a: 200, v: 1, age: 10 },
      { s: 1, t: 'h', a: 100, v: 2, age: 10 },
    ];
    const result = buildRegsByKey(entries, 60);
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
    const entries: CacheEntry[] = [{ s: 1, t: 'h', a: 100, v: 255, age: 50 }];
    const result = buildExportPayload(entries);
    expect(result.slaves['1']).toBeDefined();
    expect(result.slaves['1'].slave_id).toBe(1);
    expect(result.slaves['1'].holding_registers['100']).toBe(255);
  });

  it('all four section keys are always present even when empty', () => {
    const entries: CacheEntry[] = [{ s: 1, t: 'h', a: 0, v: 0, age: 1 }];
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

  it('dedup same slave+section+addr: keep entry with smaller age (fresher)', () => {
    // age=50 is older than age=10; entry with age=10 (v=222) wins
    const entries: CacheEntry[] = [
      { s: 1, t: 'h', a: 10, v: 111, age: 50 },  // older
      { s: 1, t: 'h', a: 10, v: 222, age: 10 },  // fresher
    ];
    const result = buildExportPayload(entries);
    expect(result.slaves['1'].holding_registers['10']).toBe(222);
  });

  it('dedup: age=5 wins over age=100 for same addr (smaller age = fresher)', () => {
    // age=5 < age=100 → entry with age=5 (v=0xBBBB) is fresher → it wins
    const entries: CacheEntry[] = [
      { s: 1, t: 'h', a: 10, v: 0xAAAA, age: 100 }, // older
      { s: 1, t: 'h', a: 10, v: 0xBBBB, age: 5 },   // fresher
    ];
    const result = buildExportPayload(entries);
    expect(result.slaves['1'].holding_registers['10']).toBe(0xBBBB);
  });

  it('two slaves are both present in slaves object', () => {
    const entries: CacheEntry[] = [
      { s: 1, t: 'h', a: 0, v: 10, age: 1 },
      { s: 3, t: 'i', a: 5, v: 20, age: 1 },
    ];
    const result = buildExportPayload(entries);
    expect(result.slaves['1']).toBeDefined();
    expect(result.slaves['3']).toBeDefined();
  });

  it('slave_id field matches the numeric slave ID', () => {
    const entries: CacheEntry[] = [{ s: 42, t: 'c', a: 0, v: 1, age: 1 }];
    const result = buildExportPayload(entries);
    expect(result.slaves['42'].slave_id).toBe(42);
  });
});
