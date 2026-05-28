// Pure utility functions for register map logic.
// No Vue imports — no reactive state — fully unit-testable.

// ── Interfaces ────────────────────────────────────────────────────────────────

// One entry from /cache/json
export interface CacheEntry {
 s: number; t: 'h' | 'i' | 'c' | 'd'; a: number; v: number; age: number;
}
// One register row displayed in the table
export interface RegRow {
 addr: number; val: string; updatedAge: number; stale: boolean;
}
// One device node in the tree
export interface DeviceNode {
 id: number; groups: string[]; lastSeenAge: number;
}

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
  if (seconds >= 65535) return '>18h'; // saturated — value not updated for 18+ hours
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
  if (p1 && p2) return { p1: true, p2: false }; // both true → keep port 1
  if (!p1 && !p2) return { p1: true, p2: false }; // both false → fallback to port 1
  return { p1, p2 }; // normal case
}

// ── Device list builder ───────────────────────────────────────────────────────

// Build the list of device nodes from raw cache entries.
// lastSeenAge is the minimum age among all entries for the slave
// (smallest age = most recently updated register = last time we saw the device).
export function buildDevices(entries: CacheEntry[]): DeviceNode[] {
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
      // Minimum age among slave entries = the most recently updated register = lastSeen.
      // age_s is maintained server-side as a saturating counter; no modular arithmetic needed.
      const lastSeenAge = Math.min(...slaveEntries.map(e => e.age));
      return { id, groups, lastSeenAge };
    })
    .sort((a, b) => a.id - b.id);
}

// ── Device filter ─────────────────────────────────────────────────────────────

/**
 * Filter a list of devices by a search query.
 * The query is trimmed and lowercased before matching.
 * A device matches if its slave ID appears as a decimal substring OR
 * as a lowercase hex substring (e.g. query "a" matches slave 10 because toString(16) = "a").
 * Returns all devices when the trimmed query is empty.
 */
export function filterDevices(devices: DeviceNode[], query: string): DeviceNode[] {
  const q = query.trim().toLowerCase();
  if (!q) return devices;
  return devices.filter(d =>
    d.id.toString().includes(q) ||
    d.id.toString(16).toLowerCase().includes(q)
  );
}

// ── Register rows builder ─────────────────────────────────────────────────────

// Returns true if newAge indicates a fresher (more recently updated) entry than existingAge.
// Smaller age_s value means the entry was updated more recently.
function isFresherEntry(newAge: number, existingAge: number): boolean {
  return newAge < existingAge;
}

// Build register rows keyed by "slaveId|TypeName" from raw cache entries.
// valueTimeout is the stale threshold in seconds (0 = disabled).
export function buildRegsByKey(
  entries: CacheEntry[],
  valueTimeout: number,
): Record<string, RegRow[]> {
  const result: Record<string, RegRow[]> = {};
  for (const e of entries) {
    const key = `${e.s}|${typeName(e.t)}`;
    if (!result[key]) result[key] = [];
    const val = (e.t === 'h' || e.t === 'i')
      ? '0x' + e.v.toString(16).toUpperCase().padStart(4, '0')
      : String(e.v);
    // age_s is maintained server-side as a saturating counter — use directly.
    const updatedAge = e.age;
    // When valueTimeout is 0, the timeout is disabled — never mark entries as stale.
    const stale = valueTimeout > 0 && updatedAge > valueTimeout;
    // Deduplicate: keep only the fresher entry for each address.
    // Entry with smaller age_s is more recently updated (fresher).
    const existingIndex = result[key].findIndex(r => r.addr === e.a);
    if (existingIndex === -1) {
      result[key].push({ addr: e.a, val, updatedAge, stale });
    } else {
      const isNewer = isFresherEntry(e.age, result[key][existingIndex].updatedAge);
      if (isNewer) {
        result[key][existingIndex] = { addr: e.a, val, updatedAge, stale };
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

  // Intermediate structure: slaveId -> sectionKey -> address -> { v, age }
  const dedup: Record<number, Record<string, Record<number, { v: number; age: number }>>> = {};

  for (const e of entries) {
    if (!dedup[e.s]) dedup[e.s] = {};
    const slave = dedup[e.s];
    const section = typeKeyMap[e.t];
    if (!section) continue;
    if (!slave[section]) slave[section] = {};
    const existing = slave[section][e.a];
    if (existing === undefined) {
      slave[section][e.a] = { v: e.v, age: e.age };
    } else {
      // Entry with smaller age_s is more recently updated (fresher) — keep it.
      const isNewer = isFresherEntry(e.age, existing.age);
      if (isNewer) slave[section][e.a] = { v: e.v, age: e.age };
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
