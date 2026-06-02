import type { RsSettings } from '@/common/types';

/** Average throughput (B/s) over the active uptime, rounded to 1 decimal.
 *  Returns 0 when uptime is non-positive (divide-by-zero guard). */
export function avgBytesPerSec(forwardBytes: number, reverseBytes: number, uptimeS: number): number {
  if (uptimeS <= 0) return 0;
  return Math.round(((forwardBytes + reverseBytes) / uptimeS) * 10) / 10;
}

/** Group large integers with a no-break space (U+00A0) as the thousands separator, e.g. 18472 -> "18 472".
 *  U+00A0 is a CSS word-separator character (so `word-spacing` can tighten the visual gap) and is
 *  non-breaking (the number won't wrap mid-group). The visual gap is tightened in the UI via CSS
 *  word-spacing on the .rep-num wrapper. */
export function groupBytes(n: number): string {
  return Math.trunc(n).toString().replace(/\B(?=(\d{3})+(?!\d))/g, ' ');
}

export interface FormattedBytes {
  value: string;
  unit: string;
}

/** Auto-scale a byte count to B/KB/MB/GB/TB using a binary (1024) base.
 *  Returns the numeric part and the unit separately so the caller can style
 *  the unit independently (e.g. wrap it in <em>). The byte range keeps full
 *  precision (grouped integer, or one decimal for fractional rates such as
 *  B/s); scaled ranges use enough decimals for ~3 significant figures. */
export function formatBytes(n: number): FormattedBytes {
  const neg = n < 0;
  let v = Math.abs(n);
  const units = ['B', 'KB', 'MB', 'GB', 'TB'];
  let i = 0;
  while (v >= 1024 && i < units.length - 1) {
    v /= 1024;
    i += 1;
  }
  let value: string;
  if (i === 0) {
    // Byte range (< 1024): integer counters render as a grouped whole number;
    // fractional inputs (e.g. average B/s) keep one decimal place.
    value = Number.isInteger(v) ? groupBytes(v) : (Math.round(v * 10) / 10).toString();
  } else {
    // Scaled range: pick decimals for roughly three significant figures.
    const decimals = v >= 100 ? 0 : v >= 10 ? 1 : 2;
    value = v.toFixed(decimals);
  }
  return { value: (neg ? '-' : '') + value, unit: units[i] };
}

/** Format a duration in seconds as HH:MM:SS (hours not padded if > 99 stay as-is). */
export function formatUptime(totalSeconds: number): string {
  const s = Math.max(0, Math.trunc(totalSeconds));
  const hh = Math.floor(s / 3600);
  const mm = Math.floor((s % 3600) / 60);
  const ss = s % 60;
  const pad = (v: number) => v.toString().padStart(2, '0');
  return `${pad(hh)}:${pad(mm)}:${pad(ss)}`;
}

/** Render a port's line params like the sidebar: "9600 · 8N1". */
export function lineParams(settings: RsSettings | undefined): string {
  if (!settings) return '—';
  const p = settings.parity === 'none' ? 'N' : settings.parity === 'even' ? 'E' : 'O';
  return `${settings.baudrate} · ${settings.databits}${p}${settings.stopbits}`;
}
