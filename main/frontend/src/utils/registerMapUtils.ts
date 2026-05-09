// Pure utility functions for register map logic.
// No Vue imports — no reactive state — fully unit-testable.

// Modular wrap constant for uint16_t timestamp arithmetic
const TS_WRAP = 65536;

// ── Interfaces ────────────────────────────────────────────────────────────────

// One entry from /cache/json
export interface CacheEntry { s: number; t: 'h' | 'i' | 'c' | 'd'; a: number; v: number; ts: number; }
// One register row displayed in the table
export interface RegRow { addr: number; val: string; updatedAge: number; stale: boolean; ts: number; }
// One device node in the tree
export interface DeviceNode { id: number; groups: string[]; lastSeenAge: number; }

// ── Format helpers ────────────────────────────────────────────────────────────

// Format a duration in microseconds as a human-readable string (for cache/status stats)
export function formatAgeUs(us: number): string {
  if (us === 0) return '—';
  const seconds = Math.floor(us / 1_000_000);
  if (seconds < 60) return `${seconds} s`;
  const minutes = Math.floor(seconds / 60);
  const secs = seconds % 60;
  if (minutes < 60) return `${minutes} min ${secs} s`;
  const hours = Math.floor(minutes / 60);
  const mins = minutes % 60;
  return `${hours} h ${mins} min`;
}

// Format a memory size in bytes as a human-readable string
export function formatMemory(bytes: number): string {
  if (bytes === 0) return '—';
  if (bytes < 1024) return `${bytes} B`;
  return `${(bytes / 1024).toFixed(1)} KB`;
}

// Format a duration in seconds as a human-readable string
export function formatAge(seconds: number): string {
  if (seconds < 1) return '< 1 s';
  const floored = Math.floor(seconds * 10) / 10; // one decimal, no rounding up to 60
  if (floored < 60) return `${floored.toFixed(1)} s`;
  const m = Math.floor(seconds / 60);
  const s = Math.floor(seconds % 60);
  if (m < 60) return `${m} min ${s} s`;
  const h = Math.floor(m / 60);
  const rm = m % 60;
  return `${h} h ${rm} min`;
}

// Map single-char type code to human-readable type name
export function typeName(t: string): string {
  return ({ h: 'Holding', i: 'Input', c: 'Coil', d: 'Discrete' } as Record<string, string>)[t] ?? t;
}

// ── Port selection ────────────────────────────────────────────────────────────

// Enforce radio invariant for port selection: exactly one port must be selected.
// If both are true → keep port 1; if both are false → fall back to port 1.
export function resolvePortSelection(p1: boolean, p2: boolean): { p1: boolean; p2: boolean } {
  if (p1 && p2) return { p1: true, p2: false };   // both true → keep port 1
  if (!p1 && !p2) return { p1: true, p2: false };  // both false → fallback to port 1
  return { p1, p2 };                                // normal case
}

// ── Device list builder ───────────────────────────────────────────────────────

// Build the list of device nodes from raw cache entries.
// nowS is the server-provided now_s anchor in the same uint16_t-truncated seconds domain.
export function buildDevices(entries: CacheEntry[], nowS: number): DeviceNode[] {
  const TYPE_ORDER = ['Holding', 'Input', 'Coil', 'Discrete'];
  const bySlave: Record<number, CacheEntry[]> = {};
  for (const e of entries) {
    if (!bySlave[e.s]) bySlave[e.s] = [];
    bySlave[e.s].push(e);
  }
  return Object.entries(bySlave)
    .map(([sid, slaveEntries]) => {
      const id = Number(sid);
      const typeSet = new Set(slaveEntries.map(e => typeName(e.t)));
      const groups = TYPE_ORDER.filter(name => typeSet.has(name));
      // Do NOT use Math.max(0, ...) — entries is always non-empty here (slave is only
      // created when at least one entry exists), and 0 is a valid uint16_t timestamp
      // (first second of uptime). Passing 0 as a floor would make ts=0 entries look
      // as old as the device uptime instead of showing their real age.
      const slaveMaxTs = Math.max(...slaveEntries.map(e => e.ts));
      // Modular difference using the server-provided now_s anchor (same uint16_t domain).
      // Correctly handles wrap-around: if now_s has wrapped past slaveMaxTs, the
      // modular subtraction still yields the correct positive age in seconds.
      const lastSeenAge = nowS > 0
        ? ((nowS - slaveMaxTs + TS_WRAP) % TS_WRAP)
        : 0;
      return { id, groups, lastSeenAge };
    })
    .sort((a, b) => a.id - b.id);
}

// ── Register rows builder ─────────────────────────────────────────────────────

// Build register rows keyed by "slaveId|TypeName" from raw cache entries.
// nowS is the server-provided now_s anchor; valueTimeout is the stale threshold in seconds.
export function buildRegsByKey(
  entries: CacheEntry[],
  nowS: number,
  valueTimeout: number,
): Record<string, RegRow[]> {
  const result: Record<string, RegRow[]> = {};
  for (const e of entries) {
    const key = `${e.s}|${typeName(e.t)}`;
    if (!result[key]) result[key] = [];
    const val = (e.t === 'h' || e.t === 'i')
      ? '0x' + e.v.toString(16).toUpperCase().padStart(4, '0')
      : String(e.v);
    // Modular age using the server-provided now_s anchor (same uint16_t domain).
    // Correctly handles wrap-around: if now_s has wrapped past e.ts, the
    // modular subtraction still yields the correct positive age in seconds.
    const diff = (nowS - e.ts + TS_WRAP) % TS_WRAP;
    const updatedAge = nowS > 0 ? diff : 0;
    // When valueTimeout is 0, the timeout is disabled — never mark entries as stale.
    const stale = valueTimeout > 0 && updatedAge > valueTimeout;
    // Deduplicate: keep only the newer entry for each address.
    // Uses modular comparison to handle uint16_t wrap-around:
    // a signed difference < TS_WRAP/2 means e.ts is strictly newer.
    const existingIndex = result[key].findIndex(r => r.addr === e.a);
    if (existingIndex === -1) {
      result[key].push({ addr: e.a, val, updatedAge, stale, ts: e.ts });
    } else {
      const tsNewer = ((e.ts - result[key][existingIndex].ts + TS_WRAP) % TS_WRAP) < (TS_WRAP / 2);
      if (tsNewer) {
        result[key][existingIndex] = { addr: e.a, val, updatedAge, stale, ts: e.ts };
      }
    }
  }
  // Sort each group by address ascending
  for (const key of Object.keys(result)) {
    result[key].sort((a, b) => a.addr - b.addr);
  }
  return result;
}

// ── Export payload builder ────────────────────────────────────────────────────

export interface ExportSlave {
  slave_id: number;
  holding_registers: Record<string, number>;
  input_registers: Record<string, number>;
  coils: Record<string, number>;
  discrete_inputs: Record<string, number>;
}

export interface ExportPayload {
  slaves: Record<string, ExportSlave>;
}

// Build a nested human-readable JSON structure from raw cache entries.
// Does NOT include exported_at or any DOM operations — pure data transformation.
export function buildExportPayload(entries: CacheEntry[]): ExportPayload {
  // Map single-char type code to section key name
  const typeKeyMap: Record<string, keyof Omit<ExportSlave, 'slave_id'>> = {
    h: 'holding_registers',
    i: 'input_registers',
    c: 'coils',
    d: 'discrete_inputs',
  };

  // Intermediate structure: slaveId -> sectionKey -> address -> { v, ts }
  const dedup: Record<number, Record<string, Record<number, { v: number; ts: number }>>> = {};

  for (const e of entries) {
    if (!dedup[e.s]) dedup[e.s] = {};
    const slave = dedup[e.s];
    const section = typeKeyMap[e.t];
    if (!section) continue;
    if (!slave[section]) slave[section] = {};
    const existing = slave[section][e.a];
    if (existing === undefined) {
      slave[section][e.a] = { v: e.v, ts: e.ts };
    } else {
      // Keep entry with newer ts using modular uint16_t arithmetic (wrap = TS_WRAP)
      const isNewer = ((e.ts - existing.ts + TS_WRAP) % TS_WRAP) < 32768;
      if (isNewer) slave[section][e.a] = { v: e.v, ts: e.ts };
    }
  }

  // Build the output object
  const slaves: Record<string, ExportSlave> = {};
  for (const [slaveIdStr, sections] of Object.entries(dedup)) {
    const slaveId = Number(slaveIdStr);
    // Use a mutable intermediate type to allow dynamic section assignment
    const slaveOut: ExportSlave & Record<string, unknown> = {
      slave_id: slaveId,
      holding_registers: {},
      input_registers: {},
      coils: {},
      discrete_inputs: {},
    };
    for (const [section, regs] of Object.entries(sections)) {
      const regOut: Record<string, number> = {};
      for (const [addrStr, entry] of Object.entries(regs)) {
        regOut[addrStr] = entry.v;
      }
      slaveOut[section] = regOut;
    }
    slaves[slaveIdStr] = slaveOut;
  }

  return { slaves };
}
