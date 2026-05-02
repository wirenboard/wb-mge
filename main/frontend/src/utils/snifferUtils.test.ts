import { describe, it, expect } from 'vitest';
import {
  formatTimestamp,
  formatDt,
  hexToPayloadString,
  toggleSet,
  parsePacket,
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
    const raw = 'FD460800062466010300000002';
    const msg = {
      id: 6,
      port: 1,
      type: 'packet',
      timestamp_us: 7000000,
      raw,
      size: 13,
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
