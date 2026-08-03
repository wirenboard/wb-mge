import { describe, it, expect } from 'vitest';
import {
  formatTimestamp,
  updateWallOffsetMs,
  formatDt,
  hexToPayloadString,
  getRowBytes,
  getRowByteRoles,
  toggleSet,
  fcLabel,
  fcFacetCode,
  parsePacket,
  computeVirtualWindow,
  trimToCap,
  rowMatchesFilter,
  type SniffRow,
  type SniffFilter,
} from './snifferUtils';

describe('formatTimestamp', () => {
  // Determinism note: we build the anchor offset from LOCAL date components
  // (new Date(y, m, d, ...).getTime()). Rendering reads back local components
  // (getHours/etc.), so the round-trip is timezone-independent — no fixed TZ needed.
  it('returns string matching HH:MM:SS.mmm format for 0 µs at the anchor', () => {
    // offsetMs = epoch-ms anchor; 0 µs since boot → wall time == anchor.
    const result = formatTimestamp(0, Date.now());
    expect(result).toMatch(/^\d{2}:\d{2}:\d{2}\.\d{3}$/);
  });

  it('renders the offset anchor exactly when us=0', () => {
    // Build a known local time: Jan 1, 2024, 12:34:56.789.
    const offsetMs = new Date(2024, 0, 1, 12, 34, 56, 789).getTime();
    const result = formatTimestamp(0, offsetMs);
    expect(result).toBe('12:34:56.789');
  });

  it('adds the device-uptime µs to the anchor (sub-second comes from the device)', () => {
    // Anchor at 12:34:56.000, then +789 ms of device uptime → 12:34:56.789.
    const offsetMs = new Date(2024, 0, 1, 12, 34, 56, 0).getTime();
    const result = formatTimestamp(789_000, offsetMs);
    expect(result).toBe('12:34:56.789');
  });

  it('pads single-digit values with zeros', () => {
    // 01:02:03.004
    const offsetMs = new Date(2024, 0, 1, 1, 2, 3, 4).getTime();
    const result = formatTimestamp(0, offsetMs);
    expect(result).toBe('01:02:03.004');
  });
});

describe('updateWallOffsetMs', () => {
  it('returns the candidate on the first call (prev=null) -> (re)anchor', () => {
    // candidate = recvWallMs - deviceUs/1000 = 10000 - 5 = 9995
    expect(updateWallOffsetMs(null, 5_000, 10_000)).toBe(9995);
  });

  it('a more-delayed packet does NOT raise the offset (min wins)', () => {
    const prev = updateWallOffsetMs(null, 5_000, 10_000); // 9995
    // Same device time but arrives 50 ms later -> candidate 10045, must keep 9995.
    expect(updateWallOffsetMs(prev, 5_000, 10_050)).toBe(9995);
  });

  it('a less-delayed packet lowers the offset', () => {
    const prev = updateWallOffsetMs(null, 5_000, 10_000); // 9995
    // Same device time but arrives 30 ms earlier -> candidate 9965, must lower to 9965.
    expect(updateWallOffsetMs(prev, 5_000, 9_970)).toBe(9965);
  });

  it('passing prev=null re-anchors, discarding the previous (lower) offset', () => {
    const prev = updateWallOffsetMs(null, 5_000, 10_000); // 9995
    expect(prev).toBe(9995);
    // Re-anchor: candidate = 20000 - 5 = 19995, ignoring the old 9995.
    expect(updateWallOffsetMs(null, 5_000, 20_000)).toBe(19995);
  });
});

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

describe('getRowBytes', () => {
  it('splits a space-separated payload into byte strings', () => {
    expect(getRowBytes('AA BB CC')).toEqual(['AA', 'BB', 'CC']);
  });

  it('returns an empty array for an empty payload', () => {
    expect(getRowBytes('')).toEqual([]);
  });
});

describe('getRowByteRoles', () => {
  it('returns an empty array for an empty payload', () => {
    expect(getRowByteRoles('', 'request')).toEqual([]);
  });

  it('assigns address/fc/data/crc roles for a standard Modbus frame', () => {
    // FC03 read holding regs request: addr(1) fc(1) data(4) crc(2)
    expect(getRowByteRoles('01 03 00 00 00 02 85 CA', 'request')).toEqual([
      'address', 'fc', 'data', 'data', 'data', 'data', 'crc', 'crc',
    ]);
  });

  it('marks an all-FF payload as arbitration regardless of direction', () => {
    expect(getRowByteRoles('FF FF FF FF FF', 'response')).toEqual([
      'arbitration', 'arbitration', 'arbitration', 'arbitration', 'arbitration',
    ]);
  });
});

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

describe('fcLabel / fcFacetCode', () => {
  it('names a plain function code and keys its facet by its own hex', () => {
    expect(fcLabel(0x03)).toBe('Read Holding Regs');
    expect(fcFacetCode(0x03)).toBe('03');
    expect(fcLabel(0x10)).toBe('Write Multiple Regs');
    expect(fcFacetCode(0x10)).toBe('10');
  });

  it('strips the exception bit: names the original function and keys the facet by it', () => {
    expect(fcLabel(0x90)).toBe('Error: Write Multiple Regs');
    expect(fcFacetCode(0x90)).toBe('10');
    expect(fcLabel(0x83)).toBe('Error: Read Holding Regs');
    expect(fcFacetCode(0x83)).toBe('03');
  });

  it('falls back to the original numeric code when the function has no name', () => {
    expect(fcLabel(0xc5)).toBe('Error: FC69');
    expect(fcFacetCode(0xc5)).toBe('45');
  });

  it('0xFF stays Fast Modbus arbitration — it is a known code, not an exception to FC 0x7F', () => {
    expect(fcLabel(0xff)).toBe('FM Arbitration');
    expect(fcFacetCode(0xff)).toBe('FF');
  });
});

describe('parsePacket', () => {
  // Fixed wall-clock offset for deterministic tests. None of the parsePacket assertions
  // check the rendered `t` (wall-clock Time) string, so a constant 0 is sufficient here.
  const OFFSET = 0;

  it('returns {row: null, timestamp: prevTimestampUs} for message without id', () => {
    const result = parsePacket({ type: 'packet', raw: 'AABB' }, 12345, OFFSET);
    expect(result.row).toBeNull();
    expect(result.timestamp).toBe(12345);
  });

  it('returns {row: null, timestamp: prevTimestampUs} for message with non-number id', () => {
    const result = parsePacket({ id: 'abc', type: 'packet', raw: 'AABB' }, 99, OFFSET);
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
    const { row, timestamp } = parsePacket(msg, 1000000, OFFSET);
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
    const { row } = parsePacket(msg, 0, OFFSET);
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
    const { row, timestamp } = parsePacket(msg, 2000000, OFFSET);
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
    const { row } = parsePacket(msg, 3000000, OFFSET);
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
    const { row } = parsePacket(msg, 0, OFFSET);
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
    const { row } = parsePacket(msg, 0, OFFSET);
    expect(row).not.toBeNull();
    const r = row as SniffRow;
    // subcmd byte = raw[4:6] = '01' → 'FM Scan Start'
    expect(r.fc).toBe('FM Scan Start');
  });

  // An exception reply carries the request FC with bit 7 set (0x90 answers FC16). FC_NAMES has
  // no key >= 128, so this used to render as the literal "FC144" with a facet code of "90"
  // (labelled "Unknown" in the rail). The row must name the ORIGINAL function, flag the
  // exception, and group in the facet under the plain FC it answers.
  it('parses an exception reply (function=0x90) → labelled with the original FC and flagged', () => {
    // 01 90 02 CDC1 — exception 02 (illegal data address) in reply to FC16.
    const msg = {
      id: 7,
      port: 1,
      type: 'packet',
      timestamp_us: 8000000,
      raw: '019002CDC1',
      size: 5,
      function: 0x90,
      slave_id: 1,
      sender: 'slave',
      crc_valid: true,
    };
    const { row } = parsePacket(msg, 0, OFFSET);
    expect(row).not.toBeNull();
    const r = row as SniffRow;
    // Never the raw decimal byte.
    expect(r.fc).not.toContain('144');
    expect(r.fc).toContain('Write Multiple Regs');
    expect(r.isException).toBe(true);
    // Facet groups with a plain FC16 reply, so filtering by "Write Multiple Regs" finds it.
    expect(r.fc_code).toBe('10');
    expect(r.tooltip).not.toBe('');
  });

  it('an exception row is distinguishable from the successful reply it shares a facet with', () => {
    const base = {
      port: 1,
      type: 'packet',
      timestamp_us: 8000000,
      size: 8,
      slave_id: 1,
      sender: 'slave',
      crc_valid: true,
    };
    const ok = parsePacket({ ...base, id: 8, raw: '011000000001', function: 0x10 }, 0, OFFSET).row as SniffRow;
    const err = parsePacket({ ...base, id: 9, raw: '019002CDC1', function: 0x90 }, 0, OFFSET).row as SniffRow;
    // Same facet bucket...
    expect(err.fc_code).toBe(ok.fc_code);
    // ...but the row itself reads differently and carries the flag the table styles on.
    expect(ok.isException).toBe(false);
    expect(err.isException).toBe(true);
    expect(err.fc).not.toBe(ok.fc);
  });

  it('an exception on a function with no name still resolves to the original code', () => {
    // 0xC5 = exception to FC 0x45 (69), which has no FC_NAMES entry.
    const msg = {
      id: 10,
      port: 1,
      type: 'packet',
      timestamp_us: 9000000,
      raw: '01C502ABCD',
      size: 5,
      function: 0xc5,
      slave_id: 1,
      sender: 'slave',
      crc_valid: true,
    };
    const r = parsePacket(msg, 0, OFFSET).row as SniffRow;
    expect(r.fc).toBe('Error: FC69');
    expect(r.isException).toBe(true);
    expect(r.fc_code).toBe('45');
  });

  it('an exception inside a Fast Modbus response is named and flags the row', () => {
    // FD 46 09 <serial:00062466> <inner PDU: 90 02 = exception to FC16> <crc:0000>
    const msg = {
      id: 12,
      port: 1,
      type: 'packet',
      timestamp_us: 9700000,
      raw: 'FD46090006246690020000',
      size: 11,
      function: 0x46,
      slave_id: 0xfd,
      sender: 'slave',
      crc_valid: true,
    };
    const r = parsePacket(msg, 0, OFFSET).row as SniffRow;
    expect(r.fc).toContain('FM Cmd');
    expect(r.fc).toContain('Error: Write Multiple Regs');
    // The wrapper FC 0x46 is not an exception, but the exchange failed — the row says so.
    expect(r.isException).toBe(true);
    // The facet still groups the packet by its Fast Modbus wrapper FC.
    expect(r.fc_code).toBe('46');
    // Both explanations survive: an exception buried inside an FM wrapper is exactly where the
    // user needs it spelled out, and the subcommand tooltip must not be traded away for it.
    expect(r.tooltip).toContain('Exception reply for Write Multiple Regs');
    expect(r.tooltip).toContain('Fast Modbus Cmd Response');
  });

  it('a plain function code is untouched: no exception flag, facet code unchanged', () => {
    const msg = {
      id: 11,
      port: 1,
      type: 'packet',
      timestamp_us: 9500000,
      raw: '01030000000285CA',
      size: 8,
      function: 3,
      slave_id: 1,
      sender: 'master',
      crc_valid: true,
    };
    const r = parsePacket(msg, 0, OFFSET).row as SniffRow;
    expect(r.fc).toBe('Read Holding Regs');
    expect(r.isException).toBe(false);
    expect(r.fc_code).toBe('03');
  });

  it('returns {row: null} for unknown message type', () => {
    const msg = { id: 10, type: 'unknown', timestamp_us: 1000000 };
    const { row, timestamp } = parsePacket(msg, 500000, OFFSET);
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
    const { row } = parsePacket(msg, 0, OFFSET);
    expect(row).not.toBeNull();
    const r = row as SniffRow;
    // fc should be "FM Cmd (Read Holding Regs)" — subcmd name + inner FC name in parens
    expect(r.fc).toContain('FM Cmd');
    expect(r.fc).toContain('Read Holding Regs');
  });
});

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

describe('trimToCap', () => {
  it('within cap (length < cap): returns [], array unchanged', () => {
    const arr = [1, 2, 3];
    const removed = trimToCap(arr, 6);
    expect(removed).toEqual([]);
    expect(arr).toEqual([1, 2, 3]);
  });

  it('exactly at cap (length === cap): returns [], array unchanged', () => {
    const arr = [1, 2, 3, 4, 5, 6];
    const removed = trimToCap(arr, 6);
    expect(removed).toEqual([]);
    expect(arr).toEqual([1, 2, 3, 4, 5, 6]);
  });

  it('over cap: returns the removed OLDEST elements, drops them, length === cap', () => {
    const arr = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
    const removed = trimToCap(arr, 6);
    // The four oldest (1..4) are removed and returned in order.
    expect(removed).toEqual([1, 2, 3, 4]);
    expect(removed).toHaveLength(4);
    expect(arr).toHaveLength(6);
    // Oldest (1..4) dropped; the most recent six remain in order.
    expect(arr).toEqual([5, 6, 7, 8, 9, 10]);
  });
});

describe('rowMatchesFilter', () => {
  // Minimal SniffRow factory: only port/crc/slave/fc_code matter to the predicate; the rest
  // are dummy values to satisfy the SniffRow type.
  function makeRow(over: Partial<SniffRow>): SniffRow {
    return {
      id: 1,
      port: 1,
      timestamp_us: 0,
      sender: 'MASTER',
      slave: '01',
      fc: 'Read Holding Regs',
      fc_code: '03',
      pl: '',
      bytes: 0,
      crc: 'OK',
      isArbitration: false,
      isException: false,
      direction: 'request',
      t: '',
      dt: '',
      tooltip: '',
      ...over,
    };
  }

  function makeFilter(over: Partial<SniffFilter> = {}): SniffFilter {
    return {
      port: 1,
      hideErrors: false,
      selectedSlaves: new Set(),
      selectedFcs: new Set(),
      ...over,
    };
  }

  it('port mismatch → false', () => {
    expect(rowMatchesFilter(makeRow({ port: 2 }), makeFilter({ port: 1 }))).toBe(false);
  });

  it('port match with empty facets → true', () => {
    expect(rowMatchesFilter(makeRow({ port: 1 }), makeFilter({ port: 1 }))).toBe(true);
  });

  it('hideErrors=true drops crc=ERR but keeps crc=OK / crc=N/A', () => {
    const f = makeFilter({ hideErrors: true });
    expect(rowMatchesFilter(makeRow({ crc: 'ERR' }), f)).toBe(false);
    expect(rowMatchesFilter(makeRow({ crc: 'OK' }), f)).toBe(true);
    expect(rowMatchesFilter(makeRow({ crc: 'N/A' }), f)).toBe(true);
  });

  it('hideErrors=false keeps crc=ERR', () => {
    expect(rowMatchesFilter(makeRow({ crc: 'ERR' }), makeFilter({ hideErrors: false }))).toBe(true);
  });

  it('selectedSlaves non-empty: keeps only matching slave', () => {
    const f = makeFilter({ selectedSlaves: new Set(['01', '02']) });
    expect(rowMatchesFilter(makeRow({ slave: '01' }), f)).toBe(true);
    expect(rowMatchesFilter(makeRow({ slave: '03' }), f)).toBe(false);
  });

  it('selectedSlaves empty: no slave filtering', () => {
    const f = makeFilter({ selectedSlaves: new Set() });
    expect(rowMatchesFilter(makeRow({ slave: '07' }), f)).toBe(true);
  });

  it('selectedFcs non-empty: keeps only matching fc_code', () => {
    const f = makeFilter({ selectedFcs: new Set(['03', '06']) });
    expect(rowMatchesFilter(makeRow({ fc_code: '03' }), f)).toBe(true);
    expect(rowMatchesFilter(makeRow({ fc_code: '10' }), f)).toBe(false);
  });

  it('selectedFcs empty: no FC filtering', () => {
    const f = makeFilter({ selectedFcs: new Set() });
    expect(rowMatchesFilter(makeRow({ fc_code: 'FF' }), f)).toBe(true);
  });

  it('combination: a row must pass ALL active conditions', () => {
    const f = makeFilter({
      port: 1,
      hideErrors: true,
      selectedSlaves: new Set(['01']),
      selectedFcs: new Set(['03']),
    });
    // Right port + not-error + selected slave + selected FC → true.
    expect(rowMatchesFilter(makeRow({ port: 1, crc: 'OK', slave: '01', fc_code: '03' }), f)).toBe(true);
    // Flip the port → false.
    expect(rowMatchesFilter(makeRow({ port: 2, crc: 'OK', slave: '01', fc_code: '03' }), f)).toBe(false);
    // Flip to an error row → false.
    expect(rowMatchesFilter(makeRow({ port: 1, crc: 'ERR', slave: '01', fc_code: '03' }), f)).toBe(false);
    // Flip the slave → false.
    expect(rowMatchesFilter(makeRow({ port: 1, crc: 'OK', slave: '02', fc_code: '03' }), f)).toBe(false);
    // Flip the FC → false.
    expect(rowMatchesFilter(makeRow({ port: 1, crc: 'OK', slave: '01', fc_code: '06' }), f)).toBe(false);
  });
});
