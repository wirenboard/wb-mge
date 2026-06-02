import type { RsSettings } from '@/common/types';

/** Average throughput (B/s) over the active uptime, rounded to 1 decimal.
 *  Returns 0 when uptime is non-positive (divide-by-zero guard). */
export function avgBytesPerSec(forwardBytes: number, reverseBytes: number, uptimeS: number): number {
  if (uptimeS <= 0) return 0;
  return Math.round(((forwardBytes + reverseBytes) / uptimeS) * 10) / 10;
}

/** Group large integers with a thin space as the thousands separator, e.g. 18472 -> "18 472". */
export function groupBytes(n: number): string {
  return Math.trunc(n).toString().replace(/\B(?=(\d{3})+(?!\d))/g, ' ');
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
