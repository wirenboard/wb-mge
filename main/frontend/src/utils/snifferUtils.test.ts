import { describe, it, expect } from 'vitest';
import {
  formatTimestamp,
  formatDt,
  hexToPayloadString,
  toggleSet,
  parsePacket,
  computeVirtualWindow,
  trimToCap,
  type SniffRow,
} from './snifferUtils';

// ============================================================
// formatTimestamp
// ============================================================
describe('formatTimestamp', () => {
  it('returns string matching HH:MM:SS.mmm format for 0 µs', () => {
    const result = formatTimestamp(0);
    // 0 µs → 0 ms → epoch time in local timezone, just check format
    expect(result).toMatch(/^\d{2}:\d{2}:\d{2}\.\d{3}$/);
  });

  it('formats a known timestamp correctly', () => {
    // Build a known time: Jan 1, 2024, 12:34:56.789 in local time
    const date = new Date(2024, 0, 1, 12, 34, 56, 789);
    const us = date.getTime() * 1000;
    const result = formatTimestamp(us);
    expect(result).toBe('12:34:56.789');
  });

  it('pads single-digit values with zeros', () => {
    // 01:02:03.004
    const date = new Date(2024, 0, 1, 1, 2, 3, 4);
    const us = date.getTime() * 1000;
    const result = formatTimestamp(us);
    expect(result).toBe('01:02:03.004');
  });
});

// ============================================================
// formatDt
// ============================================================
describe('formatDt', () => {
  it('returns em-dash when prevUs is 0', () => {
    expect(formatDt(1000000, 0)).toBe('—');
  });

  it('formats delta of 1 second (2000000 - 1000000 µs)', () => {
    expect(formatDt(2000000, 1000000)).toBe('+1000 ms');
  });

  it('formats delta of 500 ms', () => {
    expect(formatDt(1500000, 1000000)).toBe('+500 ms');
  });

  it('formats delta of 0 ms when timestamps are equal', () => {
    expect(formatDt(1000000, 1000000)).toBe('+0 ms');
  });
});

// ============================================================
// hexToPayloadString
// ============================================================
describe('hexToPayloadString', () => {
  it('returns empty string for empty input', () => {
    expect(hexToPayloadString('')).toBe('');
  });

  it('spaces two bytes', () => {
    expect(hexToPayloadString('AABB')).toBe('AA BB');
  });

  it('spaces three bytes', () => {
    expect(hexToPayloadString('AABBCC')).toBe('AA BB CC');
  });

  it('spaces four bytes', () => {
    expect(hexToPayloadString('AABBCCDD')).toBe('AA BB CC DD');
  });
});

// ============================================================
// toggleSet
// ============================================================
describe('toggleSet', () => {
  it('adds a value not in the set', () => {
    const result = toggleSet(new Set(['a', 'b']), 'c');
    expect(result).toEqual(new Set(['a', 'b', 'c']));
  });

  it('removes a value already in the set', () => {
    const result = toggleSet(new Set(['a', 'b', 'c']), 'b');
    expect(result).toEqual(new Set(['a', 'c']));
  });

  it('adds to an empty set', () => {
    const result = toggleSet(new Set(), 'x');
    expect(result).toEqual(new Set(['x']));
  });

  it('does not mutate the original set', () => {
    const original = new Set(['a', 'b']);
    toggleSet(original, 'c');
    expect(original).toEqual(new Set(['a', 'b']));
  });
});

// ============================================================
// parsePacket
// ============================================================
describe('parsePacket', () => {
  it('returns {row: null, timestamp: prevTimestampUs} for message without id', () => {
    const result = parsePacket({ type: 'packet', raw: 'AABB' }, 12345);
    expect(result.row).toBeNull();
    expect(result.timestamp).toBe(12345);
  });

  it('returns {row: null, timestamp: prevTimestampUs} for message with non-number id', () => {
    const result = parsePacket({ id: 'abc', type: 'packet', raw: 'AABB' }, 99);
    expect(result.row).toBeNull();
    expect(result.timestamp).toBe(99);
  });

  it('parses a timeout message into a TIMEOUT row', () => {
    const msg = {
      id: 1,
      port: 1,
      type: 'timeout',
      timestamp_us: 2000000,
      function: 3,
      slave_id: 1,
    };
    const { row, timestamp } = parsePacket(msg, 1000000);
    expect(row).not.toBeNull();
    const r = row as SniffRow;
    expect(r.sender).toBe('TIMEOUT');
    expect(r.crc).toBe('ERR');
    expect(r.isArbitration).toBe(false);
    expect(r.slave).toBe('01');
    expect(r.fc).toBe('Read Holding Regs');
    expect(r.bytes).toBe(0);
    expect(r.dt).toBe('+1000 ms');
    expect(timestamp).toBe(2000000);
  });

  it('timeout row with prevTimestampUs=0 shows em-dash for dt', () => {
    const msg = { id: 1, port: 1, type: 'timeout', timestamp_us: 1000000, function: 3, slave_id: 1 };
    const { row } = parsePacket(msg, 0);
    expect((row as SniffRow).dt).toBe('—');
  });

  it('parses an arbitration packet (all-FF raw) into a SLAVE row with crc=N/A', () => {
    const raw = 'FFFFFFFFFF'; // 5 × FF
    const msg = {
      id: 2,
      port: 1,
      type: 'packet',
      timestamp_us: 3000000,
      raw,
      size: 5,
      function: 0xFF,
      slave_id: 0xFF,
      sender: 'slave',
      crc_valid: false,
    };
    const { row, timestamp } = parsePacket(msg, 2000000);
    expect(row).not.toBeNull();
    const r = row as SniffRow;
    expect(r.sender).toBe('SLAVE');
    expect(r.isArbitration).toBe(true);
    expect(r.crc).toBe('N/A');
    expect(r.fc).toBe('FM Arbitration');
    expect(timestamp).toBe(3000000);
  });

  it('parses a normal master packet with crc_valid=true → sender MASTER, crc OK', () => {
    // FC=03, slave=0x01, simple read holding registers request
    const raw = '01030000000285CA'; // standard read request (real CRC not checked here)
    const msg = {
      id: 3,
      port: 1,
      type: 'packet',
      timestamp_us: 4000000,
      raw,
      size: 8,
      function: 3,
      slave_id: 1,
      sender: 'master',
      crc_valid: true,
    };
    const { row } = parsePacket(msg, 3000000);
    expect(row).not.toBeNull();
    const r = row as SniffRow;
    expect(r.sender).toBe('MASTER');
    expect(r.crc).toBe('OK');
    expect(r.slave).toBe('01');
    expect(r.isArbitration).toBe(false);
  });

  it('parses a packet with crc_valid=false → sender ERR', () => {
    const raw = 'AABBCCDD';
    const msg = {
      id: 4,
      port: 1,
      type: 'packet',
      timestamp_us: 5000000,
      raw,
      size: 4,
      function: 3,
      slave_id: 1,
      sender: 'master',
      crc_valid: false,
    };
    const { row } = parsePacket(msg, 0);
    expect(row).not.toBeNull();
    expect((row as SniffRow).sender).toBe('ERR');
    expect((row as SniffRow).crc).toBe('ERR');
  });

  it('parses a fast modbus packet (function=0x46) with known subcmd byte → shows subcmd name', () => {
    // raw: addr(FD) ext(46) subcmd(01=Scan Start) ...
    const raw = 'FD460109F0';
    const msg = {
      id: 5,
      port: 1,
      type: 'packet',
      timestamp_us: 6000000,
      raw,
      size: 5,
      function: 0x46,
      slave_id: 0xFD,
      sender: 'master',
      crc_valid: true,
    };
    const { row } = parsePacket(msg, 0);
    expect(row).not.toBeNull();
    const r = row as SniffRow;
    // subcmd byte = raw[4:6] = '01' → 'FM Scan Start'
    expect(r.fc).toBe('FM Scan Start');
  });

  it('returns {row: null} for unknown message type', () => {
    const msg = { id: 10, type: 'unknown', timestamp_us: 1000000 };
    const { row, timestamp } = parsePacket(msg, 500000);
    expect(row).toBeNull();
    expect(timestamp).toBe(500000);
  });

  it('FM Cmd packet (function=0x46, subcmd=0x08) appends inner FC name to display', () => {
    // Fast Modbus Cmd packet: addr=FD, ext=46, subcmd=08, serial=00062466, inner FC=03 (Read Holding Regs)
    // Structure: FD 46 08 <serial:00062466> <inner PDU: 03 00C8 0014> <crc:0000>
    const raw = 'FD4608000624660300C800140000';
    const msg = {
      id: 6,
      port: 1,
      type: 'packet',
      timestamp_us: 7000000,
      raw,
      size: 14,
      function: 0x46,
      slave_id: 0xFD,
      sender: 'master',
      crc_valid: true,
    };
    const { row } = parsePacket(msg, 0);
    expect(row).not.toBeNull();
    const r = row as SniffRow;
    // fc should be "FM Cmd (Read Holding Regs)" — subcmd name + inner FC name in parens
    expect(r.fc).toContain('FM Cmd');
    expect(r.fc).toContain('Read Holding Regs');
  });
});

// ============================================================
// computeVirtualWindow
// ============================================================
describe('computeVirtualWindow', () => {
  it('top of list: scrollTop=0 → window starts at 0, full visibleCount, only bottom spacer', () => {
    // visibleCount = ceil(290/29) + 10*2 = 10 + 20 = 30
    const w = computeVirtualWindow(0, 290, 29, 1000, 10);
    expect(w.visibleCount).toBe(30);
    expect(w.startIndex).toBe(0);
    expect(w.endIndex).toBe(30);
    expect(w.padTop).toBe(0);
    expect(w.padBottom).toBe((1000 - 30) * 29);
  });

  it('mid-list: scrollTop maps to row 100 → window backs off by overscan', () => {
    // scrollTop=2900 → 2900/29 = 100; startIndex = 100 - overscan(10) = 90
    const w = computeVirtualWindow(2900, 290, 29, 1000, 10);
    expect(w.startIndex).toBe(90);
    expect(w.endIndex).toBe(120); // 90 + visibleCount(30)
  });

  it('stale-large scrollTop after shrink: window is clamped to the last page (no blank gap)', () => {
    // scrollTop is huge (stale from before the list shrank). visibleCount=30,
    // maxStartIndex = max(0, 50-30) = 20, so startIndex clamps to 20 and the slice is non-empty.
    const w = computeVirtualWindow(290000, 290, 29, 50, 10);
    expect(w.visibleCount).toBe(30);
    expect(w.startIndex).toBe(20);
    expect(w.endIndex).toBe(50);
    expect(w.endIndex).toBeGreaterThan(w.startIndex); // slice [20,50) is NON-EMPTY
    expect(w.padTop).toBe(20 * 29);
    expect(w.padBottom).toBe(0);
  });

  it('totalRows < visibleCount: all rows render, no spacers', () => {
    const w = computeVirtualWindow(0, 290, 29, 5, 10);
    expect(w.startIndex).toBe(0);
    expect(w.endIndex).toBe(5);
    expect(w.padTop).toBe(0);
    expect(w.padBottom).toBe(0);
  });

  it('empty list: everything is zero', () => {
    const w = computeVirtualWindow(0, 290, 29, 0, 10);
    expect(w.startIndex).toBe(0);
    expect(w.endIndex).toBe(0);
    expect(w.padTop).toBe(0);
    expect(w.padBottom).toBe(0);
  });

  it('rowHeight=0 guard: does not throw or produce NaN (uses safeRowHeight)', () => {
    const w = computeVirtualWindow(100, 290, 0, 1000, 10);
    expect(Number.isFinite(w.startIndex)).toBe(true);
    expect(Number.isFinite(w.endIndex)).toBe(true);
    expect(Number.isFinite(w.visibleCount)).toBe(true);
    expect(Number.isFinite(w.padTop)).toBe(true);
    expect(Number.isFinite(w.padBottom)).toBe(true);
    // padTop/padBottom use the real (zero) rowHeight, so both are 0 here.
    expect(w.padTop).toBe(0);
    expect(w.padBottom).toBe(0);
  });

  it('invariant: padTop + slice*rowHeight + padBottom === totalRows*rowHeight (parametrized)', () => {
    const cases: Array<[number, number, number, number, number]> = [
      // [scrollTop, viewportH, rowHeight, totalRows, overscan]
      [0, 290, 29, 1000, 10],
      [2900, 290, 29, 1000, 10],
      [290000, 290, 29, 50, 10],
      [0, 290, 29, 5, 10],
      [0, 290, 29, 0, 10],
      [5000, 500, 20, 300, 5],
      [123456, 768, 18, 9999, 8],
    ];
    for (const [scrollTop, viewportH, rowHeight, totalRows, overscan] of cases) {
      const w = computeVirtualWindow(scrollTop, viewportH, rowHeight, totalRows, overscan);
      const sliceHeight = (w.endIndex - w.startIndex) * rowHeight;
      expect(w.padTop + sliceHeight + w.padBottom).toBe(totalRows * rowHeight);
      expect(w.padBottom).toBeGreaterThanOrEqual(0);
      expect(w.startIndex).toBeGreaterThanOrEqual(0);
      expect(w.endIndex).toBeLessThanOrEqual(totalRows);
    }
  });
});

// ============================================================
// trimToCap
// ============================================================
describe('trimToCap', () => {
  it('within cap (length < cap): returns 0, array unchanged', () => {
    const arr = [1, 2, 3];
    const removed = trimToCap(arr, 6);
    expect(removed).toBe(0);
    expect(arr).toEqual([1, 2, 3]);
  });

  it('exactly at cap (length === cap): returns 0, array unchanged', () => {
    const arr = [1, 2, 3, 4, 5, 6];
    const removed = trimToCap(arr, 6);
    expect(removed).toBe(0);
    expect(arr).toEqual([1, 2, 3, 4, 5, 6]);
  });

  it('over cap: returns overflow, drops the OLDEST elements, length === cap', () => {
    const arr = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
    const removed = trimToCap(arr, 6);
    expect(removed).toBe(4);
    expect(arr).toHaveLength(6);
    // Oldest (1..4) dropped; the most recent six remain in order.
    expect(arr).toEqual([5, 6, 7, 8, 9, 10]);
  });
});
