import { decodePacket, getByteRoles, type ByteRole, type Direction } from '@/common/modbusDecoder';

export type { ByteRole };

export type SniffRow = {
  id: number;
  port: number;
  timestamp_us: number;
  sender: 'MASTER' | 'SLAVE' | 'TIMEOUT' | 'ERR';
  slave: string;
  fc: string;
  fc_code: string;
  pl: string;
  bytes: number;
  crc: 'OK' | 'ERR' | 'N/A';
  isArbitration: boolean;
  direction: Direction;
  t: string;
  dt: string;
  tooltip: string;
};

export const FC_NAMES: Record<number, string> = {
  1: 'Read Coils',
  2: 'Read Discrete Inputs',
  3: 'Read Holding Regs',
  4: 'Read Input Regs',
  5: 'Write Single Coil',
  6: 'Write Single Reg',
  15: 'Write Multiple Coils',
  16: 'Write Multiple Regs',
  70: 'Fast Modbus',
  96: 'Fast Modbus (legacy)',
  255: 'FM Arbitration',
};

export const SLAVE_NAMES: Record<number, string> = {
  253: 'Fast Modbus broadcast',
  255: 'FM Bus Arbitration',
};

export const FAST_MODBUS_SUBCMDS: Record<number, string> = {
  0x01: 'FM Scan Start',
  0x02: 'FM Scan Continue',
  0x03: 'FM Scan Response',
  0x04: 'FM Scan End',
  0x08: 'FM Cmd',
  0x09: 'FM Cmd',
  0x10: 'FM Event Request',
  0x11: 'FM Event Transfer',
  0x12: 'FM No Events',
  0x18: 'FM Event Config',
};

export const FAST_MODBUS_TOOLTIPS: Record<number, string> = {
  0x01: 'Fast Modbus Scan Start: master resets scan status on all unscanned devices. Devices participate in arbitration; lowest serial number wins.',
  0x02: 'Fast Modbus Scan Continue: master continues scanning. Devices not yet scanned participate in arbitration.',
  0x03: 'Fast Modbus Scan Response: device responds with its serial number and data during scan arbitration.',
  0x04: 'Fast Modbus Scan End: master signals end of scan. Only devices that already responded may reply.',
  0x08: 'Fast Modbus Cmd Send: master sends a standard Modbus command via Fast Modbus addressing.',
  0x09: 'Fast Modbus Cmd Response: device responds to a Fast Modbus standard command.',
  0x10: 'Fast Modbus Event Request: master polls all devices for pending events. Devices with events participate in arbitration.',
  0x11: 'Fast Modbus Event Transfer: winning device sends its pending event data to master.',
  0x12: 'Fast Modbus No Events: device that wins arbitration reports that no devices on the bus have pending events. Prevents master from waiting for timeout.',
  0x18: 'Fast Modbus Event Config: master configures event reporting settings on device.',
};

export const FC_TOOLTIPS: Record<number, string> = {
  1: 'Read Coils (FC01): read 1–2000 discrete output coils. Request: addr(2) + count(2). Response: N bytes of coil values.',
  2: 'Read Discrete Inputs (FC02): read 1–2000 discrete input bits. Request: addr(2) + count(2). Response: N bytes of input values.',
  3: 'Read Holding Regs (FC03): read 1–125 holding registers. Request: addr(2) + count(2). Response: count×2 bytes of register values.',
  4: 'Read Input Regs (FC04): read 1–125 input registers. Request: addr(2) + count(2). Response: count×2 bytes of register values.',
  5: 'Write Single Coil (FC05): write one coil ON (0xFF00) or OFF (0x0000). Request: addr(2) + value(2). Response: echo of request.',
  6: 'Write Single Reg (FC06): write one holding register. Request: addr(2) + value(2). Response: echo of request.',
  15: 'Write Multiple Coils (FC15): write N coils. Request: addr(2) + count(2) + byte_count(1) + data. Response: addr+count.',
  16: 'Write Multiple Regs (FC16): write N holding registers. Request: addr(2) + count(2) + byte_count(1) + data. Response: addr+count.',
};

/** Update the wall-clock<->device-uptime offset (ms vs Unix epoch).
 *  deviceUs = packet timestamp_us (µs since boot); recvWallMs = Date.now() at receipt.
 *  Network jitter only delays arrival, so the MIN candidate is the best anchor.
 *  Pass prevOffsetMs=null to (re)anchor. */
export function updateWallOffsetMs(prevOffsetMs: number | null, deviceUs: number, recvWallMs: number): number {
  const candidate = recvWallMs - deviceUs / 1000;
  return prevOffsetMs === null ? candidate : Math.min(prevOffsetMs, candidate);
}

/** Format a device-uptime µs timestamp as real wall-clock HH:MM:SS.mmm (browser-local TZ).
 *  offsetMs anchors device uptime to the Unix epoch; the sub-second part comes from the
 *  device clock so packets arriving in a burst keep their true relative milliseconds. */
export function formatTimestamp(us: number, offsetMs: number): string {
  const ms = offsetMs + us / 1000;
  const d = new Date(ms);
  const hh = d.getHours().toString().padStart(2, '0');
  const mm = d.getMinutes().toString().padStart(2, '0');
  const ss = d.getSeconds().toString().padStart(2, '0');
  const mmm = d.getMilliseconds().toString().padStart(3, '0');
  return `${hh}:${mm}:${ss}.${mmm}`;
}

/** Format delta time between two microsecond timestamps. Returns '—' when prevUs is 0. */
export function formatDt(us: number, prevUs: number): string {
  if (prevUs === 0) return '—';
  const diff = Math.round((us - prevUs) / 1000);
  return `+${diff} ms`;
}

export type SniffFilter = {
  port: number;
  hideErrors: boolean;
  selectedSlaves: Set<string>;
  selectedFcs: Set<string>;
};

/** True if a row passes the active sniffer filters: port + hide-errors + slave/FC facets. */
export function rowMatchesFilter(row: SniffRow, f: SniffFilter): boolean {
  if (row.port !== f.port) return false;
  if (f.hideErrors && row.crc === 'ERR') return false;
  if (f.selectedSlaves.size > 0 && !f.selectedSlaves.has(row.slave)) return false;
  if (f.selectedFcs.size > 0 && !f.selectedFcs.has(row.fc_code)) return false;
  return true;
}

/** Convert a compact hex string to a space-separated hex string (e.g. 'AABB' → 'AA BB') */
export function hexToPayloadString(raw: string): string {
  return raw.match(/.{1,2}/g)?.join(' ') ?? raw;
}

/** Split a payload string ('AA BB CC') into its byte-string array. Empty payload → []. */
export function getRowBytes(pl: string): string[] {
  return pl ? pl.split(' ') : [];
}

/**
 * Recompute per-byte semantic roles for a row's payload on demand. Empty payload → [].
 * Deliberately NOT stored on every SniffRow (it ballooned per-row memory); only the rows
 * currently visible in the virtual-scroll window need roles, and they are cheap to redo.
 */
export function getRowByteRoles(pl: string, direction: Direction): ByteRole[] {
  if (!pl) return [];
  return getByteRoles(decodePacket(pl, direction));
}

/** Toggle set membership: add val if absent, remove if present. Returns a new Set. */
export function toggleSet(set: Set<string>, val: string): Set<string> {
  const n = new Set(set);
  n.has(val) ? n.delete(val) : n.add(val);
  return n;
}

export type VirtualWindow = {
  startIndex: number;
  endIndex: number;
  visibleCount: number;
  padTop: number;
  padBottom: number;
};

/**
 * Compute the virtual-scroll window: the [startIndex, endIndex) range of rows to render plus
 * the top/bottom spacer heights (px). Pure and defensive: the start index is clamped to the
 * last page so a stale scrollTop (e.g. after a filter change shrinks the list with no scroll
 * event) can never push the window past the end and render an empty slice under an oversized
 * top spacer (a blank gap). Invariant: padTop + (endIndex-startIndex)*rowHeight + padBottom
 * === totalRows*rowHeight, and padBottom >= 0, startIndex >= 0, endIndex <= totalRows.
 */
export function computeVirtualWindow(
  scrollTop: number,
  viewportH: number,
  rowHeight: number,
  totalRows: number,
  overscan: number,
): VirtualWindow {
  // Guard against a zero/negative rowHeight (never measured yet) to avoid divide-by-zero.
  const safeRowHeight = rowHeight > 0 ? rowHeight : 1;
  const visibleCount = Math.ceil(viewportH / safeRowHeight) + overscan * 2;
  const maxStartIndex = Math.max(0, totalRows - visibleCount);
  const startIndex = Math.min(
    maxStartIndex,
    Math.max(0, Math.floor(scrollTop / safeRowHeight) - overscan),
  );
  const endIndex = Math.min(totalRows, startIndex + visibleCount);
  const padTop = startIndex * rowHeight;
  const padBottom = Math.max(0, (totalRows - endIndex) * rowHeight);
  return { startIndex, endIndex, visibleCount, padTop, padBottom };
}

/**
 * Ring buffer: drop the oldest elements in place so the array length never exceeds `cap`.
 * Mutates `arr` (splice in place to preserve Vue reactivity on the same ref) and returns the
 * removed elements (empty array if already within the cap) so callers can inspect what was dropped.
 */
export function trimToCap<T>(arr: T[], cap: number): T[] {
  const overflow = arr.length - cap;
  if (overflow > 0) {
    return arr.splice(0, overflow);
  }
  return [];
}

// parsePacket — pure, accepts prevTimestampUs and returns new timestamp

/**
 * Parse a raw WebSocket sniffer message into a SniffRow.
 *
 * @param msg - Raw message object from WebSocket JSON.
 * @param prevTimestampUs - The timestamp of the previously received packet (microseconds).
 *   Pass 0 for the first packet so that the delta time displays as '—'.
 * @param offsetMs - The wall-clock<->device-uptime offset (ms vs Unix epoch) used to render
 *   the real wall-clock Time column. See updateWallOffsetMs / formatTimestamp.
 * @returns An object containing the parsed row (or null if the message is invalid)
 *   and the updated timestamp to carry forward as prevTimestampUs for the next call.
 */
export function parsePacket(msg: any, prevTimestampUs: number, offsetMs: number): { row: SniffRow | null; timestamp: number } {
  if (typeof msg.id !== 'number') return { row: null, timestamp: prevTimestampUs };

  // DEPRECATED: the firmware no longer sends {type:"timeout"} packets over WebSocket.
  // Since master requests are now emitted immediately on receipt, the absence of a
  // following slave packet is sufficient to infer a timeout. This branch is kept for
  // backward compatibility with older firmware versions.
  if (msg.type === 'timeout') {
    const fcName = FC_NAMES[msg.function] ?? `FC${msg.function}`;
    const slave = msg.slave_id.toString(16).padStart(2, '0').toUpperCase();
    const dt = formatDt(msg.timestamp_us, prevTimestampUs);
    return {
      row: Object.freeze({
        id: msg.id,
        port: msg.port,
        timestamp_us: msg.timestamp_us,
        sender: 'TIMEOUT',
        slave,
        fc: fcName,
        fc_code: msg.function.toString(16).padStart(2, '0').toUpperCase(),
        pl: '',
        bytes: 0,
        crc: 'ERR',
        isArbitration: false,
        direction: 'request',
        t: formatTimestamp(msg.timestamp_us, offsetMs),
        dt,
        tooltip: `No response from slave 0x${slave} within timeout`,
      }),
      timestamp: msg.timestamp_us,
    };
  }

  if (msg.type === 'packet') {
    const pl = hexToPayloadString(msg.raw);
    const dt = formatDt(msg.timestamp_us, prevTimestampUs);

    /* Detect Fast Modbus arbitration: all bytes are 0xFF */
    const isArbitration = msg.raw && /^(FF)+$/i.test(msg.raw);

    if (isArbitration) {
      return {
        row: Object.freeze({
          id: msg.id,
          port: msg.port,
          timestamp_us: msg.timestamp_us,
          sender: 'SLAVE',
          slave: '—',
          fc: 'FM Arbitration',
          fc_code: 'FF',
          pl,
          bytes: msg.size,
          crc: 'N/A',
          isArbitration: true,
          direction: 'response',
          t: formatTimestamp(msg.timestamp_us, offsetMs),
          dt,
          tooltip: 'Fast Modbus Bus Arbitration: devices simultaneously assert 0xFF bytes to resolve which one responds. No CRC — not a Modbus frame.',
        }),
        timestamp: msg.timestamp_us,
      };
    }

    const fcName = FC_NAMES[msg.function] ?? `FC${msg.function}`;
    const slave = msg.slave_id.toString(16).padStart(2, '0').toUpperCase();
    const sender = msg.sender === 'master' ? 'MASTER' : 'SLAVE';
    const crc = msg.crc_valid ? 'OK' : 'ERR';
    const direction: Direction = msg.sender === 'master' ? 'request' : 'response';

    /* Build FC display: no numeric prefix for named functions */
    let fcDisplay = fcName;
    let tooltip = FC_TOOLTIPS[msg.function] ?? '';
    if ((msg.function === 0x46 || msg.function === 0x60) && msg.raw && msg.raw.length >= 6) {
      const sub = parseInt(msg.raw.slice(4, 6), 16);
      const subName = FAST_MODBUS_SUBCMDS[sub] ?? `FM subcmd 0x${sub.toString(16).padStart(2, '0').toUpperCase()}`;
      fcDisplay = `${subName}`;
      tooltip = FAST_MODBUS_TOOLTIPS[sub] ?? `Fast Modbus subcommand 0x${sub.toString(16).padStart(2, '0').toUpperCase()}`;
    }

    const decoded = decodePacket(pl, direction);

    /* For FM Cmd (command/response_by_serial), append nested function name in parens */
    if (decoded.type === 'rtu_frame' && decoded.payload.type === 'fast_modbus') {
      const fmSub = decoded.payload.payload;
      if ((fmSub.type === 'command_by_serial' || fmSub.type === 'response_by_serial') && 'function_code' in fmSub) {
        const innerFcNum = parseInt(fmSub.function_code as string, 16);
        const innerFcName = FC_NAMES[innerFcNum] ?? `FC${fmSub.function_code}`;
        fcDisplay = `${fcDisplay} (${innerFcName})`;
      }
    }

    return {
      row: Object.freeze({
        id: msg.id,
        port: msg.port,
        timestamp_us: msg.timestamp_us,
        sender: crc === 'ERR' ? 'ERR' : sender,
        slave,
        fc: fcDisplay,
        fc_code: msg.function.toString(16).padStart(2, '0').toUpperCase(),
        pl,
        bytes: msg.size,
        crc,
        isArbitration: false,
        direction,
        t: formatTimestamp(msg.timestamp_us, offsetMs),
        dt,
        tooltip,
      }),
      timestamp: msg.timestamp_us,
    };
  }

  return { row: null, timestamp: prevTimestampUs };
}
