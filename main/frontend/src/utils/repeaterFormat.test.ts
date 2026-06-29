/**
 * F-1 unit tests for the pure repeater-format helpers (repeaterFormat.ts).
 *
 * Covers the boundary branches called out by the test strategy:
 *  - avgBytesPerSec: divide-by-zero guard and 0.1 rounding,
 *  - formatUptime: negative/zero clamping, fractional truncation, uncapped hours,
 *  - groupBytes: thousands grouping (separator-agnostic via whitespace normalize),
 *  - lineParams: undefined fallback and parity mapping (none/even/odd).
 */

import { describe, it, expect } from 'vitest';
import type { RsSettings } from '@/common/types';
import { avgBytesPerSec, groupBytes, formatBytes, formatUptime, lineParams } from '@/utils/repeaterFormat';

// Build a full RsSettings with a configurable parity; other fields are fixed.
function makeRs(parity: RsSettings['parity']): RsSettings {
  return {
    term: false,
    fail_safe: false,
    tx_disabled: false,
    baudrate: 9600,
    stopbits: '1',
    parity,
    databits: '8',
    bridge: { mode: 'server', ip: '0.0.0.0', port: 502, modbus: true },
  };
}

// Normalize any whitespace (incl. the U+00A0 no-break space used as the thousands
// separator) to a single regular space so the groupBytes assertions are separator-agnostic.
const norm = (s: string) => s.replace(/\s/g, ' ');

describe('avgBytesPerSec', () => {
  it('returns 0 when uptime is zero (divide-by-zero guard)', () => {
    expect(avgBytesPerSec(100, 100, 0)).toBe(0);
  });

  it('returns 0 when uptime is negative (divide-by-zero guard)', () => {
    expect(avgBytesPerSec(100, 100, -5)).toBe(0);
  });

  it('computes the average over uptime', () => {
    expect(avgBytesPerSec(10, 5, 3)).toBe(5);
  });

  it('rounds to one decimal place', () => {
    expect(avgBytesPerSec(1, 0, 3)).toBe(0.3);
    expect(avgBytesPerSec(2, 0, 3)).toBe(0.7);
  });
});

describe('formatUptime', () => {
  it('clamps negative input to 00:00:00', () => {
    expect(formatUptime(-5)).toBe('00:00:00');
  });

  it('formats zero as 00:00:00', () => {
    expect(formatUptime(0)).toBe('00:00:00');
  });

  it('formats a normal duration as HH:MM:SS', () => {
    expect(formatUptime(5047)).toBe('01:24:07');
  });

  it('truncates fractional seconds', () => {
    expect(formatUptime(5047.9)).toBe('01:24:07');
  });

  it('does not cap hours above 99', () => {
    expect(formatUptime(360000)).toBe('100:00:00');
  });
});

describe('groupBytes', () => {
  it('groups thousands with a separator', () => {
    expect(norm(groupBytes(18472))).toBe('18 472');
  });

  it('leaves single digits unchanged', () => {
    expect(norm(groupBytes(5))).toBe('5');
  });

  it('leaves three digits unchanged', () => {
    expect(norm(groupBytes(999))).toBe('999');
  });

  it('truncates fractional values before grouping', () => {
    expect(norm(groupBytes(1234.9))).toBe('1 234');
  });

  it('groups negative values', () => {
    expect(norm(groupBytes(-1234))).toBe('-1 234');
  });
});

describe('formatBytes', () => {
  it('leaves a small byte count unscaled', () => {
    expect(formatBytes(512)).toEqual({ value: '512', unit: 'B' });
  });

  it('formats zero', () => {
    expect(formatBytes(0)).toEqual({ value: '0', unit: 'B' });
  });

  it('groups the largest in-range byte value with a no-break space', () => {
    const r = formatBytes(1023);
    expect(r.unit).toBe('B');
    expect(norm(r.value)).toBe('1 023');
  });

  it('scales exactly 1024 to KB', () => {
    expect(formatBytes(1024)).toEqual({ value: '1.00', unit: 'KB' });
  });

  it('scales 2048 to KB', () => {
    expect(formatBytes(2048)).toEqual({ value: '2.00', unit: 'KB' });
  });

  it('scales into MB', () => {
    expect(formatBytes(11240325)).toEqual({ value: '10.7', unit: 'MB' });
  });

  it('scales 1024^3 to GB', () => {
    expect(formatBytes(1073741824)).toEqual({ value: '1.00', unit: 'GB' });
  });

  it('keeps one decimal for a fractional byte rate', () => {
    expect(formatBytes(534.5)).toEqual({ value: '534.5', unit: 'B' });
  });

  it('preserves the sign for negative values', () => {
    expect(formatBytes(-2048)).toEqual({ value: '-2.00', unit: 'KB' });
  });
});

describe('lineParams', () => {
  it('returns the em-dash placeholder for undefined settings', () => {
    expect(lineParams(undefined)).toBe('\u2014');
  });

  it('maps parity \'none\' to N', () => {
    expect(lineParams(makeRs('none'))).toBe('9600 \u00b7 8N1');
  });

  it('maps parity \'even\' to E', () => {
    expect(lineParams(makeRs('even'))).toContain('E');
  });

  it('maps parity \'odd\' to O', () => {
    expect(lineParams(makeRs('odd'))).toContain('O');
  });
});
