<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted } from 'vue';
import { useI18n } from 'vue-i18n';
import { useInfo } from '@/common/info';
import { api } from '@/utils/api';
import Button from '@/components/Button.vue';
import Heading from '@/components/Heading.vue';
import Layout from '@/components/Layout.vue';
import Switch from '@/components/Switch.vue';

const { t } = useI18n();
const { info } = useInfo();

// One entry from /cache/json
interface CacheEntry { s: number; t: 'h' | 'i' | 'c' | 'd'; a: number; v: number; ts: number; }
// One register row displayed in the table
interface RegRow { addr: number; val: string; updatedAge: number; stale: boolean; ts: number; }
// One device node in the tree
interface DeviceNode { id: number; groups: string[]; lastSeenAge: number; }

const rawEntries = ref<CacheEntry[]>([]);

// Stats from GET /cache/status
const cachePackets = ref(0);
const cacheLastPacketAgeUs = ref(0);
const cacheMapAgeUs = ref(0);
const cacheMemoryBytes = ref(0);
const cacheMaxEntries = ref(0);
const cacheEntries = ref(0);
// Server-side "now" anchor in the same uint16_t-truncated seconds domain as entry timestamps.
// Used to compute ages correctly across wrap-around.
const cacheNowS = ref(0);

// Cache is considered enabled when AT LEAST ONE port is in cache_bus mode.
// Derived reactively from the info ref polled globally every 5 s by App.vue.
const cacheEnabled = computed(() => {
  if (!info.value) return false;
  return info.value.rs485_1.port_mode === 'cache_bus' ||
         info.value.rs485_2.port_mode === 'cache_bus';
});

const loading = ref(true);
const error = ref<string | null>(null);
const valueTimeout = ref(60);
const openDevices = ref<Set<number>>(new Set());
const openGroups = ref<Set<string>>(new Set());
const searchFilter = ref('');

// Local copies of which serial ports are listened to (editable in Settings panel before Save)
const listenPort1 = ref(false);
const listenPort2 = ref(false);
// TCP Modbus server is always running on port 504 — display-only
const cacheTcpPort = ref(504);
// Save-button status for the Settings panel
const settingsSaveStatus = ref<'idle' | 'saving' | 'saved' | 'error'>('idle');

let pollInterval: ReturnType<typeof setInterval> | null = null;
let statsInterval: ReturnType<typeof setInterval> | null = null;
// Timer handle for auto-resetting the save status badge
let saveStatusTimer: ReturnType<typeof setTimeout> | null = null;

// Fetch cache statistics from the device and populate stat refs
async function fetchCacheStats(): Promise<void> {
  if (!cacheEnabled.value) return;
  try {
    const s = await api<{
      enabled: boolean;
      entries: number;
      slaves: number;
      packets_processed: number;
      last_packet_age_us: number;
      map_age_us: number;
      memory_bytes: number;
      max_entries: number;
    }>('cache/status');
    cachePackets.value         = s.packets_processed;
    cacheLastPacketAgeUs.value = s.last_packet_age_us;
    cacheMapAgeUs.value        = s.map_age_us;
    cacheMemoryBytes.value     = s.memory_bytes;
    cacheMaxEntries.value      = s.max_entries;
    cacheEntries.value         = s.entries;
    // cacheNowS is now sourced from /cache/json to guarantee it is consistent
    // with the entry timestamps (both captured in the same HTTP response).
  } catch {
    // Silently ignore fetch errors
  }
}

async function fetchEntries(): Promise<void> {
  // Skip the fetch when cache is not active to avoid pointless requests.
  if (!cacheEnabled.value) return;
  try {
    // /cache/json returns { now_s, d: CacheEntry[] } so that now_s and every
    // e.ts are guaranteed to come from the same server-side moment, preventing
    // modular subtraction from producing ~65534 when the bus is active and
    // entries arrive between independent status polls.
    const data = await api<{ now_s: number; d: CacheEntry[] }>('cache/json');
    rawEntries.value = data.d;
    cacheNowS.value  = data.now_s;
    error.value = null;
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Fetch failed';
  } finally {
    loading.value = false;
  }
}

async function toggleCaching(): Promise<void> {
  try {
    if (cacheEnabled.value) {
      // Disable: switch all ports currently in cache_bus back to tcp_bridge
      if (info.value?.rs485_1.port_mode === 'cache_bus') {
        await api<void>('ports/1/mode', { method: 'POST', json: { mode: 'tcp_bridge' } });
      }
      if (info.value?.rs485_2.port_mode === 'cache_bus') {
        await api<void>('ports/2/mode', { method: 'POST', json: { mode: 'tcp_bridge' } });
      }
      rawEntries.value = [];
      // cacheEnabled will update automatically on the next info poll
    } else {
      // Enable: switch only the port selected in Settings panel (radio selection).
      // Use local variables to avoid mutating UI state as a side-effect.
      // If neither port is selected (edge case before first info poll), default to port 1.
      const enablePort1 = listenPort1.value || (!listenPort1.value && !listenPort2.value);
      const enablePort2 = listenPort2.value;
      if (enablePort1) {
        await api<void>('ports/1/mode', { method: 'POST', json: { mode: 'cache_bus' } });
      }
      if (enablePort2) {
        await api<void>('ports/2/mode', { method: 'POST', json: { mode: 'cache_bus' } });
      }
      // Fetch entries immediately so the UI shows data without waiting for the next poll
      await fetchEntries();
    }
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Action failed';
    // cacheEnabled is derived from info — no manual resync needed
  }
}

async function resetMap(): Promise<void> {
  // Abort if info is unavailable — port states cannot be determined
  const port1WasActive = info.value?.rs485_1.port_mode === 'cache_bus';
  const port2WasActive = info.value?.rs485_2.port_mode === 'cache_bus';
  if (!port1WasActive && !port2WasActive) return;
  try {
    // Disable active ports to clear the cache
    if (port1WasActive) {
      await api<void>('ports/1/mode', { method: 'POST', json: { mode: 'tcp_bridge' } });
    }
    if (port2WasActive) {
      await api<void>('ports/2/mode', { method: 'POST', json: { mode: 'tcp_bridge' } });
    }
    // Re-enable only the ports that were active
    if (port1WasActive) {
      await api<void>('ports/1/mode', { method: 'POST', json: { mode: 'cache_bus' } });
    }
    if (port2WasActive) {
      await api<void>('ports/2/mode', { method: 'POST', json: { mode: 'cache_bus' } });
    }
    rawEntries.value = [];
    // Fetch entries immediately so the UI reflects the cleared state
    await fetchEntries();
    // cacheEnabled will update automatically on the next info poll
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Reset failed';
    // cacheEnabled is derived from info — no manual resync needed
  }
}

// Sync local port selection from info only on first load.
// After initial sync, local state is the source of truth until Save is pressed.
let portsInitialized = false;
watch(() => info.value, (newInfo) => {
  if (!newInfo) return;
  if (portsInitialized) return; // skip subsequent polling updates
  listenPort1.value = newInfo.rs485_1.port_mode === 'cache_bus';
  listenPort2.value = newInfo.rs485_2.port_mode === 'cache_bus';
  // Enforce radio invariant: only one port can be selected at a time.
  // If both ports are in cache_bus mode, default selection to port 1.
  if (listenPort1.value && listenPort2.value) {
    listenPort2.value = false;
  }
  portsInitialized = true;
}, { immediate: true });

// Select a port for cache listening (radio semantics: only one port active at a time)
function selectListenPort(port: 1 | 2): void {
  listenPort1.value = port === 1;
  listenPort2.value = port === 2;
}

// Apply per-port listen changes and update save status
async function saveSettings(): Promise<void> {
  if (settingsSaveStatus.value === 'saving') return; // prevent concurrent calls
  if (saveStatusTimer !== null) { clearTimeout(saveStatusTimer); saveStatusTimer = null; }
  // Defensive: re-apply radio invariant before saving (guards against edge-case state corruption)
  if (listenPort1.value && listenPort2.value) {
    listenPort2.value = false; // both true → keep port 1
  } else if (!listenPort1.value && !listenPort2.value) {
    listenPort1.value = true;  // both false → fallback to port 1
  }
  settingsSaveStatus.value = 'saving';
  try {
    const port1TargetMode = listenPort1.value ? 'cache_bus' : 'tcp_bridge';
    const port2TargetMode = listenPort2.value ? 'cache_bus' : 'tcp_bridge';
    if (info.value?.rs485_1.port_mode !== port1TargetMode) {
      await api<void>('ports/1/mode', { method: 'POST', json: { mode: port1TargetMode } });
    }
    if (info.value?.rs485_2.port_mode !== port2TargetMode) {
      await api<void>('ports/2/mode', { method: 'POST', json: { mode: port2TargetMode } });
    }
    settingsSaveStatus.value = 'saved';
  } catch {
    settingsSaveStatus.value = 'error';
  }
  saveStatusTimer = setTimeout(() => { settingsSaveStatus.value = 'idle'; }, 3000);
}

// Format a duration in microseconds as a human-readable string (for cache/status stats)
function formatAgeUs(us: number): string {
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
function formatMemory(bytes: number): string {
  if (bytes === 0) return '—';
  if (bytes < 1024) return `${bytes} B`;
  return `${(bytes / 1024).toFixed(1)} KB`;
}

// Format a duration in seconds as a human-readable string
function formatAge(seconds: number): string {
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
function typeName(t: string): string {
  return ({ h: 'Holding', i: 'Input', c: 'Coil', d: 'Discrete' } as Record<string, string>)[t] ?? t;
}

// Open a URL in a new browser tab
function openUrl(url: string): void {
  window.open(url, '_blank', 'noopener');
}

// Toggle a device node open/closed
function toggleDevice(id: number): void {
  const s = new Set(openDevices.value);
  if (s.has(id)) s.delete(id);
  else s.add(id);
  openDevices.value = s;
}

// Toggle a register group open/closed
function toggleGroup(key: string): void {
  const s = new Set(openGroups.value);
  if (s.has(key)) s.delete(key);
  else s.add(key);
  openGroups.value = s;
}

// Expand all device and group nodes
function expandAll(): void {
  const devSet = new Set<number>();
  const grpSet = new Set<string>();
  for (const dev of devices.value) {
    devSet.add(dev.id);
    for (const g of dev.groups) {
      grpSet.add(`${dev.id}|${g}`);
    }
  }
  openDevices.value = devSet;
  openGroups.value = grpSet;
}

// Collapse all device and group nodes
function collapseAll(): void {
  openDevices.value = new Set();
  openGroups.value = new Set();
}

// Build the list of device nodes from raw entries
const devices = computed((): DeviceNode[] => {
  const TYPE_ORDER = ['Holding', 'Input', 'Coil', 'Discrete'];
  const bySlave: Record<number, CacheEntry[]> = {};
  for (const e of rawEntries.value) {
    if (!bySlave[e.s]) bySlave[e.s] = [];
    bySlave[e.s].push(e);
  }
  return Object.entries(bySlave)
    .map(([sid, entries]) => {
      const id = Number(sid);
      const typeSet = new Set(entries.map(e => typeName(e.t)));
      const groups = TYPE_ORDER.filter(name => typeSet.has(name));
      // Do NOT use Math.max(0, ...) — entries is always non-empty here (slave is only
      // created when at least one entry exists), and 0 is a valid uint16_t timestamp
      // (first second of uptime). Passing 0 as a floor would make ts=0 entries look
      // as old as the device uptime instead of showing their real age.
      const slaveMaxTs = Math.max(...entries.map(e => e.ts));
      // Modular difference using the server-provided now_s anchor (same uint16_t domain).
      // Correctly handles wrap-around: if now_s has wrapped past slaveMaxTs, the
      // modular subtraction still yields the correct positive age in seconds.
      const TS_WRAP = 65536;
      const lastSeenAge = cacheNowS.value > 0
        ? ((cacheNowS.value - slaveMaxTs + TS_WRAP) % TS_WRAP)
        : 0;
      return { id, groups, lastSeenAge };
    })
    .sort((a, b) => a.id - b.id);
});

// Build register rows keyed by "slaveId|TypeName"
const regsByKey = computed((): Record<string, RegRow[]> => {
  const result: Record<string, RegRow[]> = {};
  const TS_WRAP = 65536;
  for (const e of rawEntries.value) {
    const key = `${e.s}|${typeName(e.t)}`;
    if (!result[key]) result[key] = [];
    const val = (e.t === 'h' || e.t === 'i')
      ? '0x' + e.v.toString(16).toUpperCase().padStart(4, '0')
      : String(e.v);
    // Modular age using the server-provided now_s anchor (same uint16_t domain).
    // Correctly handles wrap-around: if now_s has wrapped past e.ts, the
    // modular subtraction still yields the correct positive age in seconds.
    const diff = (cacheNowS.value - e.ts + TS_WRAP) % TS_WRAP;
    const updatedAge = cacheNowS.value > 0 ? diff : 0;
    const stale = updatedAge > valueTimeout.value;
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
});

// Filter devices by search query (slave id decimal or hex)
const filteredDevices = computed((): DeviceNode[] => {
  if (!searchFilter.value.trim()) return devices.value;
  const q = searchFilter.value.trim().toLowerCase();
  return devices.value.filter(d =>
    d.id.toString().includes(q) ||
    d.id.toString(16).toLowerCase().includes(q)
  );
});

// { immediate: true } ensures stats are fetched on mount if cache is already enabled.
// oldVal guard: only reset displayed stats when transitioning from a confirmed-enabled
// state (oldVal === true) — prevents stats from flashing to "—" on every info-polling
// reconnect where cacheEnabled briefly becomes false because info.value is temporarily
// undefined, even though the cache is still running on the device.
watch(cacheEnabled, (val, oldVal) => {
  if (val) {
    fetchCacheStats(); // immediate fetch on enable
    statsInterval = setInterval(fetchCacheStats, 5000);
  } else {
    if (statsInterval) { clearInterval(statsInterval); statsInterval = null; }
    // Only reset stats when transitioning from a known-enabled state, not from undefined/initial
    if (oldVal === true) {
      cachePackets.value         = 0;
      cacheLastPacketAgeUs.value = 0;
      cacheMapAgeUs.value        = 0;
      cacheMemoryBytes.value     = 0;
      cacheMaxEntries.value      = 0;
      cacheEntries.value         = 0;
      cacheNowS.value            = 0;
    }
  }
}, { immediate: true });

onMounted(() => {
  fetchEntries();
  pollInterval = setInterval(() => {
    fetchEntries();
  }, 2000);
});

onUnmounted(() => {
  if (pollInterval !== null) {
    clearInterval(pollInterval);
    pollInterval = null;
  }
  if (statsInterval !== null) {
    clearInterval(statsInterval);
    statsInterval = null;
  }
  if (saveStatusTimer !== null) {
    clearTimeout(saveStatusTimer);
    saveStatusTimer = null;
  }
});
</script>

<template>
  <Layout>
    <Heading :title="t('title')" :crumbs="t('crumbs')">
      <template #default>
        <div class="rm-header-controls">
          <!-- CACHING ON/OFF block -->
          <label class="rm-caching-toggle" @click.prevent="toggleCaching()">
            <span class="rm-caching-k">Caching</span>
            <span :class="['rm-caching-state', cacheEnabled ? 'on' : 'off']">{{ cacheEnabled ? 'On' : 'Off' }}</span>
            <span class="toggle">
              <input type="checkbox" :checked="cacheEnabled" />
              <span class="track" />
              <span class="thumb" />
            </span>
          </label>
        </div>
      </template>
    </Heading>

    <div class="rm-main-body">
      <!-- Caching disabled state -->
      <div v-if="!cacheEnabled" class="rm-map-card">
        <div class="rm-off">
          <svg width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round">
            <rect x="3" y="3" width="7" height="7" rx="1" />
            <rect x="14" y="3" width="7" height="7" rx="1" />
            <rect x="3" y="14" width="7" height="7" rx="1" />
            <rect x="14" y="14" width="7" height="7" rx="1" />
          </svg>
          <div class="rm-off-title">Register map caching is disabled</div>
          <div class="rm-off-sub">Enable caching to start recording Modbus register traffic from the bus.</div>
          <Button variant="primary" @click="toggleCaching()">Enable caching</Button>
        </div>
      </div>

      <!-- First-load spinner -->
      <div v-else-if="loading" class="rm-loading-wrap">
        <svg class="rm-spinner" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
          <circle cx="12" cy="12" r="10" stroke-opacity="0.2" />
          <path d="M12 2a10 10 0 0 1 10 10" />
        </svg>
        Loading…
      </div>

      <!-- Error state -->
      <div v-else-if="error" class="rm-error-wrap">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
          <circle cx="12" cy="12" r="10" />
          <path d="M12 8v4M12 16h.01" />
        </svg>
        {{ error }}
      </div>

      <!-- Caching enabled: stats strip + 2-column layout (map + settings panel) -->
      <template v-else>
        <!-- Stats strip (full-width, no actions column) -->
        <div class="rm-stats">
          <div class="stat-block">
            <div class="stat-label">Slaves / Registers</div>
            <div class="stat-sub">seen on bus</div>
            <div class="stat-value">
              <b>{{ devices.length }}</b><span class="stat-dim"> / {{ rawEntries.length }}</span>
            </div>
          </div>
          <div class="stat-block">
            <div class="stat-label">Packets processed</div>
            <div class="stat-sub">since last reset</div>
            <div class="stat-value">{{ cachePackets }}</div>
          </div>
          <div class="stat-block">
            <div class="stat-label">Last packet</div>
            <div class="stat-sub">ago</div>
            <div class="stat-value">{{ formatAgeUs(cacheLastPacketAgeUs) }}</div>
          </div>
          <div class="stat-block">
            <div class="stat-label">Map age</div>
            <div class="stat-sub">since last reset</div>
            <div class="stat-value">{{ formatAgeUs(cacheMapAgeUs) }}</div>
          </div>
          <div class="stat-block stat-block--with-entries">
            <div class="stat-label">Memory</div>
            <div class="stat-sub">used / pool size</div>
            <div class="stat-value">{{ formatMemory(cacheMemoryBytes) }}<span class="stat-dim"> / {{ formatMemory(cacheMaxEntries * 8) }}</span></div>
            <div class="stat-entries">
              <span class="stat-entries-val">{{ cacheEntries }}</span>
              <span class="stat-entries-dim"> / {{ cacheMaxEntries > 0 ? cacheMaxEntries : '—' }} entries</span>
            </div>
          </div>
        </div>

        <!-- 2-column layout: map content on the left, settings panel on the right -->
        <div class="rm-cache-layout">
          <!-- Left column: map content (empty state or tree) -->
          <div class="rm-cache-content">
            <!-- Empty state -->
            <div v-if="devices.length === 0" class="rm-map-card">
              <div class="rm-off">
                <svg width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round">
                  <rect x="3" y="3" width="7" height="7" rx="1" />
                  <rect x="14" y="3" width="7" height="7" rx="1" />
                  <rect x="3" y="14" width="7" height="7" rx="1" />
                  <rect x="14" y="14" width="7" height="7" rx="1" />
                </svg>
                <div class="rm-off-title">No devices seen yet</div>
                <div class="rm-off-sub">Waiting for Modbus traffic on the bus…</div>
              </div>
            </div>

            <!-- Map card with tree -->
            <div v-else class="rm-map-card">
              <div class="rm-map-header">
                <div class="rm-map-title-wrap">
                  <div class="rm-map-title">Map</div>
                  <div class="rm-map-sub">Device → register type → register</div>
                </div>
                <div class="rm-map-actions">
                  <div class="rm-search">
                    <svg width="12" height="12" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><circle cx="7" cy="7" r="4"/><path d="M10 10l3.5 3.5"/></svg>
                    <input v-model="searchFilter" placeholder="Filter by slave ID…" />
                  </div>
                  <button class="rm-tb-btn" @click="expandAll()">Expand all</button>
                  <button class="rm-tb-btn" @click="collapseAll()">Collapse</button>
                </div>
              </div>

              <div class="rm-tree" role="tree">
                <!-- Device rows, filtered by searchFilter -->
                <div v-for="dev in filteredDevices" :key="dev.id" class="rm-tree-dev">
                  <!-- Device header row -->
                  <div
                    class="rm-row lvl-dev"
                    role="treeitem"
                    :aria-expanded="openDevices.has(dev.id)"
                    @click="toggleDevice(dev.id)"
                  >
                    <svg
                      class="caret"
                      :class="{ open: openDevices.has(dev.id) }"
                      width="10" height="10" viewBox="0 0 10 10"
                      fill="none" stroke="currentColor" stroke-width="1.5"
                      stroke-linecap="round" stroke-linejoin="round"
                    >
                      <path d="M2 3 L5 7 L8 3"/>
                    </svg>
                    <span class="rm-slave mono">Slave 0x{{ dev.id.toString(16).padStart(2, '0').toUpperCase() }}</span>
                    <span class="rm-meta">
                      <span class="rm-meta-item">
                        <span class="dim">last</span>
                        <span class="mono">{{ formatAge(dev.lastSeenAge) }} ago</span>
                      </span>
                    </span>
                  </div>

                  <!-- Group rows — shown when device is expanded -->
                  <template v-if="openDevices.has(dev.id)">
                    <div v-for="group in dev.groups" :key="group" class="rm-tree-grp">
                      <!-- Group header row -->
                      <div
                        class="rm-row lvl-grp"
                        role="treeitem"
                        :aria-expanded="openGroups.has(`${dev.id}|${group}`)"
                        @click="toggleGroup(`${dev.id}|${group}`)"
                      >
                        <svg
                          class="caret"
                          :class="{ open: openGroups.has(`${dev.id}|${group}`) }"
                          width="10" height="10" viewBox="0 0 10 10"
                          fill="none" stroke="currentColor" stroke-width="1.5"
                          stroke-linecap="round" stroke-linejoin="round"
                        >
                          <path d="M2 3 L5 7 L8 3"/>
                        </svg>
                        <span :class="['rm-grp-tag', 'tag-' + group.toLowerCase()]">{{ group }}</span>
                        <span class="rm-name">{{ group }} registers</span>
                        <span class="rm-meta"><span class="mono">{{ (regsByKey[`${dev.id}|${group}`] || []).length }}</span></span>
                      </div>

                      <!-- Register rows — shown when group is expanded -->
                      <div v-if="openGroups.has(`${dev.id}|${group}`)" class="rm-regs">
                        <!-- Column headers -->
                        <div class="rm-reg-head">
                          <span>Addr</span>
                          <span style="text-align:right">Raw value</span>
                          <span>Last update</span>
                          <span style="text-align:right">Responses</span>
                        </div>
                        <!-- One row per register -->
                        <div
                          v-for="reg in regsByKey[`${dev.id}|${group}`] || []"
                          :key="reg.addr"
                          :class="['rm-row', 'lvl-reg', { stale: reg.stale }]"
                          role="treeitem"
                        >
                          <span class="mono rm-reg-addr">{{ reg.addr }}</span>
                          <span class="mono rm-reg-val">{{ reg.val }}</span>
                          <span class="mono rm-reg-upd">
                            <span v-if="reg.stale" class="stale-dot" title="Older than value timeout" />
                            {{ formatAge(reg.updatedAge) }} ago
                          </span>
                          <span class="mono rm-reg-hits">—</span>
                        </div>
                      </div>
                    </div>
                  </template>
                </div>
              </div>
            </div>
          </div>

          <!-- Right column: Settings panel -->
          <div class="rm-settings-panel">
            <div class="rsp-title">Settings</div>

            <!-- CACHING section -->
            <div class="rsp-section-label">CACHING</div>

            <!-- Value timeout row -->
            <div class="rsp-row">
              <div class="rsp-control">
                  <input type="number" class="rsp-input" v-model.number="valueTimeout" min="1" max="86400" />
                  <span class="rsp-unit">seconds</span>
                </div>
              <div class="rsp-row-info">
                <div class="rsp-row-title">Value timeout</div>
                <div class="rsp-row-desc">If a register's last update is older than this, the gateway returns Modbus error 0x0B instead of the cached value.</div>
              </div>
            </div>

            <!-- Reset map row -->
            <div class="rsp-row">
              <div class="rsp-control">
                <button class="rsp-btn-reset" @click="resetMap()">↺ Reset</button>
              </div>
              <div class="rsp-row-info">
                <div class="rsp-row-title">Reset map</div>
                <div class="rsp-row-desc">Drops every observed register from RAM. Map will rebuild from new traffic.</div>
              </div>
            </div>

            <div class="rsp-divider" />

            <!-- SOURCE section -->
            <div class="rsp-section-label">SOURCE</div>

            <!-- Listen port row -->
            <div class="rsp-row">
              <div class="rsp-control rsp-control--port-tags">
                <button
                    type="button"
                    :class="['rsp-port-tag', { active: listenPort1 }]"
                    @click="selectListenPort(1)"
                  >1</button>
                  <button
                    type="button"
                    :class="['rsp-port-tag', { active: listenPort2 }]"
                    @click="selectListenPort(2)"
                  >2</button>
              </div>
              <div class="rsp-row-info">
                <div class="rsp-row-title">Listen port</div>
                <div class="rsp-row-desc">Which serial port the cache observes for register traffic.</div>
              </div>
            </div>

            <div class="rsp-divider" />

            <!-- EXPORT section -->
            <div class="rsp-section-label">EXPORT</div>

            <!-- Export row -->
            <div class="rsp-row">
              <div class="rsp-control rsp-control--btns">
                <button class="rsp-btn-export" @click="openUrl('/cache/csv')">↓ CSV</button>
                <button class="rsp-btn-export" @click="openUrl('/cache/json')">↓ JSON</button>
              </div>
              <div class="rsp-row-info">
                <div class="rsp-row-title">Download current map</div>
                <div class="rsp-row-desc">Snapshot of every observed register and its last cached value.</div>
              </div>
            </div>

            <div class="rsp-divider" />

            <!-- TCP MODBUS section -->
            <div class="rsp-section-label">TCP MODBUS</div>

            <!-- TCP serve toggle row (always ON, display-only) -->
            <div class="rsp-row">
              <div class="rsp-control">
                <Switch id="rsp-tcp-serve" :model-value="true" :disabled="true" :ariaLabel="'Serve cached values over TCP'" />
              </div>
              <div class="rsp-row-info">
                <div class="rsp-row-title">Serve cached values over TCP</div>
                <div class="rsp-row-desc">Reply to TCP Modbus reads from cache without round-tripping the bus.</div>
              </div>
            </div>

            <!-- TCP port row (display-only) -->
            <div class="rsp-row">
              <div class="rsp-control">
                <input type="number" class="rsp-input" :value="cacheTcpPort" readonly />
              </div>
              <div class="rsp-row-info">
                <div class="rsp-row-title">TCP port</div>
                <div class="rsp-row-desc">Port the gateway listens on for TCP Modbus clients.</div>
              </div>
            </div>

            <!-- Save footer -->
            <div class="rsp-footer">
              <span class="rsp-status">
                <template v-if="settingsSaveStatus === 'saved'">Saved</template>
                <template v-else-if="settingsSaveStatus === 'error'">Error saving</template>
              </span>
              <button class="rsp-btn-save" @click="saveSettings()" :disabled="settingsSaveStatus === 'saving'">
                Save changes
              </button>
            </div>
          </div>
        </div>
      </template>
    </div>
  </Layout>
</template>

<style scoped>
/* ── Heading controls ────────────────────────────────────────────── */
.rm-header-controls {
  display: flex;
  align-items: center;
  gap: 14px;
}

/* CACHING toggle block */
.rm-caching-toggle {
  display: inline-flex;
  align-items: center;
  gap: 10px;
  height: 32px;
  padding: 0 10px 0 12px;
  border: 1px solid var(--border-color);
  border-radius: var(--r-md);
  background: var(--bg-surface-subtle);
  cursor: pointer;
}

.rm-caching-k {
  font-size: 11px;
  text-transform: uppercase;
  letter-spacing: 0.06em;
  color: var(--text-muted);
  font-weight: 500;
}

.rm-caching-state {
  font-family: var(--font-mono);
  font-size: 11px;
  font-weight: 600;
  letter-spacing: 0.04em;
  text-transform: uppercase;
  padding: 1px 7px;
  border-radius: 4px;
  border: 1px solid transparent;
}

.rm-caching-state.on {
  color: var(--primary-color);
  background: color-mix(in oklch, var(--primary-color) 12%, transparent);
  border-color: color-mix(in oklch, var(--primary-color) 25%, transparent);
}

.rm-caching-state.off {
  color: var(--text-muted);
  background: color-mix(in oklch, var(--text-muted) 10%, transparent);
  border-color: var(--border-color);
}

/* Toggle switch */
.toggle {
  --w: 32px;
  --h: 18px;
  position: relative;
  width: var(--w);
  height: var(--h);
  display: inline-block;
  cursor: pointer;
  flex-shrink: 0;
}

.toggle input {
  appearance: none;
  position: absolute;
  inset: 0;
  margin: 0;
  cursor: pointer;
  opacity: 0;
}

.toggle .track {
  position: absolute;
  inset: 0;
  background: #d5d8de;
  border-radius: 999px;
  transition: background 0.15s;
}

.toggle .thumb {
  position: absolute;
  top: 2px;
  left: 2px;
  width: calc(var(--h) - 4px);
  height: calc(var(--h) - 4px);
  background: #fff;
  border-radius: 50%;
  box-shadow: 0 1px 2px rgba(0, 0, 0, 0.15);
  transition: transform 0.15s;
}

.toggle input:checked ~ .track {
  background: var(--primary-color);
}

.toggle input:checked ~ .thumb {
  transform: translateX(calc(var(--w) - var(--h)));
}

/* ── Main body layout ────────────────────────────────────────────── */
.rm-main-body {
  padding: 24px 32px 40px;
  flex: 1;
  display: flex;
  flex-direction: column;
  gap: 16px;
  min-width: 0;
}

/* ── Stats strip (full-width) ───────────────────────────────────── */
.rm-stats {
  display: grid;
  grid-template-columns: repeat(5, minmax(0, 1fr));
  background: var(--bg-surface);
  border: 1px solid var(--border-color);
  border-radius: var(--r-lg);
  overflow: hidden;
}

.stat-block {
  background: var(--bg-surface);
  padding: 10px 14px;
  display: grid;
  grid-template-rows: auto 1fr auto;
  gap: 3px;
  min-width: 0;
}

/* Extended grid for the stat block that has a 4th entries line */
.stat-block--with-entries {
  grid-template-rows: auto 1fr auto auto;
}

.stat-block + .stat-block {
  border-left: 1px solid var(--border-color);
}

.stat-label {
  font-size: 10px;
  text-transform: uppercase;
  letter-spacing: 0.08em;
  color: var(--text-muted);
  font-weight: 500;
  white-space: nowrap;
}

.stat-value {
  font-size: 18px;
  font-weight: 600;
  line-height: 1.15;
  font-family: var(--font-mono);
  letter-spacing: -0.01em;
  color: var(--text-color);
  white-space: nowrap;
}

.stat-value b {
  font-weight: 600;
  color: var(--primary-color);
}

.stat-value .stat-dim {
  color: var(--text-muted);
  font-weight: 500;
  font-size: 12px;
}

.stat-sub {
  font-size: 10.5px;
  color: var(--text-muted);
  white-space: nowrap;
}

/* Secondary entries count line in stat block */
.stat-entries {
  font-size: 10px;
  font-family: var(--font-mono);
  white-space: nowrap;
  margin-top: 2px;
}

.stat-entries-val {
  color: var(--text-secondary);
  font-weight: 500;
}

.stat-entries-dim {
  color: var(--text-muted);
}

/* ── 2-column cache layout ──────────────────────────────────────── */
.rm-cache-layout {
  display: grid;
  grid-template-columns: 1fr 360px;
  gap: 16px;
  align-items: start;
}

.rm-cache-content {
  min-width: 0;
}

/* ── Settings panel ─────────────────────────────────────────────── */
.rm-settings-panel {
  background: var(--bg-surface);
  border: 1px solid var(--border-color);
  border-radius: var(--r-lg);
  box-shadow: 0 1px 2px rgba(15, 23, 42, 0.04);
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.rsp-title {
  padding: 14px 18px;
  border-bottom: 1px solid var(--border-color);
  font-size: 13px;
  font-weight: 600;
  color: var(--text-color);
}

.rsp-section-label {
  padding: 10px 18px 4px;
  font-size: 10px;
  font-weight: 600;
  letter-spacing: 0.08em;
  text-transform: uppercase;
  color: var(--text-muted);
}

.rsp-row {
  display: flex;
  align-items: flex-start;
  gap: 12px;
  padding: 10px 18px;
  border-bottom: 1px solid color-mix(in oklch, var(--border-color) 50%, transparent);
}

.rsp-row:last-of-type {
  border-bottom: none;
}

.rsp-control {
  flex-shrink: 0;
  display: flex;
  align-items: center;
  gap: 6px;
  width: 120px; /* fixed width so all row titles align */
}

.rsp-input {
  width: 60px;
  padding: 4px 6px;
  border: 1px solid var(--border-color);
  border-radius: 4px;
  font-family: var(--font-mono);
  font-size: 13px;
  text-align: right;
  background: var(--bg-surface);
  color: var(--text-color);
}

.rsp-unit {
  font-size: 11px;
  color: var(--text-muted);
}

.rsp-row-info {
  flex: 1;
  min-width: 0;
}

.rsp-row-title {
  font-size: 13px;
  font-weight: 500;
  color: var(--text-color);
  line-height: 1.3;
}

.rsp-row-desc {
  font-size: 11.5px;
  color: var(--text-muted);
  line-height: 1.4;
  margin-top: 2px;
}

.rsp-divider {
  height: 1px;
  background: var(--border-color);
  margin: 4px 0;
}

.rsp-btn-reset {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  height: 30px;
  padding: 0 10px;
  font-size: 12px;
  font-weight: 500;
  border: 1px solid var(--danger-color);
  background: color-mix(in oklch, var(--danger-color) 8%, white);
  color: var(--danger-color);
  border-radius: var(--r-sm);
  cursor: pointer;
}

.rsp-control--btns {
  flex-direction: column;
  gap: 4px;
}

.rsp-btn-export {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  height: 28px;
  padding: 0 10px;
  font-size: 12px;
  border: 1px solid var(--border-color);
  background: var(--bg-surface);
  color: var(--text-secondary);
  border-radius: var(--r-sm);
  cursor: pointer;
  white-space: nowrap;
  width: 100%;
  justify-content: center;
}

.rsp-btn-export:hover {
  background: var(--bg-surface-subtle);
  color: var(--text-color);
}

/* Port tag buttons for the Listen port selector */
.rsp-control--port-tags {
  display: flex;
  flex-direction: row;
  gap: 6px;
  align-items: center;
  width: auto; /* override fixed width from .rsp-control */
}

.rsp-port-tag {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 32px;
  height: 32px;
  border: 1px solid var(--border-color);
  border-radius: var(--r-sm);
  background: var(--bg-surface);
  color: var(--text-secondary);
  font-family: var(--font-mono);
  font-size: 13px;
  font-weight: 500;
  cursor: pointer;
  transition: background 0.1s, border-color 0.1s, color 0.1s;
}

.rsp-port-tag.active {
  background: color-mix(in oklch, var(--primary-color) 12%, var(--bg-surface));
  border-color: color-mix(in oklch, var(--primary-color) 40%, transparent);
  color: var(--primary-color);
}

.rsp-port-tag:hover:not(.active) {
  background: var(--bg-surface-subtle);
  border-color: var(--border-strong);
}

.rsp-footer {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 12px 18px;
  border-top: 1px solid var(--border-color);
  margin-top: auto;
}

.rsp-status {
  font-size: 12px;
  color: var(--text-muted);
}

.rsp-btn-save {
  height: 32px;
  padding: 0 16px;
  font-size: 13px;
  font-weight: 500;
  background: var(--primary-color);
  color: #fff;
  border: none;
  border-radius: var(--r-sm);
  cursor: pointer;
}

.rsp-btn-save:disabled {
  opacity: 0.6;
  cursor: not-allowed;
}

.rsp-btn-save:not(:disabled):hover {
  background: color-mix(in oklch, var(--primary-color) 85%, black);
}

/* ── Map card ───────────────────────────────────────────────────── */
.rm-map-card {
  background: var(--bg-surface);
  border: 1px solid var(--border-color);
  border-radius: var(--r-lg);
  box-shadow: 0 1px 2px rgba(15, 23, 42, 0.04);
  overflow: hidden;
}

.rm-map-header {
  padding: 14px 18px;
  border-bottom: 1px solid var(--border-color);
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
}

.rm-map-title {
  font-size: 13px;
  font-weight: 600;
  letter-spacing: 0.01em;
  color: var(--text-color);
}

.rm-map-sub {
  font-size: 11px;
  color: var(--text-muted);
  margin-top: 2px;
}

.rm-map-actions {
  display: flex;
  align-items: center;
  gap: 8px;
}

.rm-search {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 4px 10px;
  background: var(--bg-surface-subtle);
  border: 1px solid var(--border-color);
  border-radius: var(--r-md);
  color: var(--text-muted);
  min-width: 240px;
}

.rm-search input {
  border: 0;
  padding: 0;
  background: transparent;
  font-size: 12px;
  outline: none;
  width: 100%;
  color: var(--text-color);
  font-family: inherit;
}

.rm-tb-btn {
  height: 28px;
  padding: 0 10px;
  font-size: 12px;
  background: var(--bg-surface);
  border: 1px solid var(--border-color);
  color: var(--text-secondary);
  border-radius: var(--r-sm);
  cursor: pointer;
}

.rm-tb-btn:hover {
  background: var(--bg-surface-subtle);
  border-color: var(--border-strong);
  color: var(--text-color);
}

/* ── State placeholders (off / loading / error) ─────────────────── */
.rm-off {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 12px;
  padding: 60px 32px;
  text-align: center;
  color: var(--text-muted);
}

.rm-off svg {
  opacity: 0.25;
  color: var(--text-color);
}

.rm-off-title {
  font-size: 14px;
  font-weight: 600;
  color: var(--text-secondary);
}

.rm-off-sub {
  font-size: 12.5px;
  color: var(--text-muted);
  max-width: 380px;
  line-height: 1.5;
}

.rm-loading-wrap {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  padding: 60px 32px;
  font-size: 13px;
  color: var(--text-muted);
}

.rm-spinner {
  animation: rm-spin 1s linear infinite;
  color: var(--primary-color);
}

@keyframes rm-spin {
  to { transform: rotate(360deg); }
}

.rm-error-wrap {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  padding: 60px 32px;
  font-size: 13px;
  color: var(--danger-color);
}

/* ── Tree ───────────────────────────────────────────────────────── */
.rm-tree {
  padding: 0;
}

/* Generic row base */
.rm-row {
  display: grid;
  align-items: center;
  gap: 12px;
  padding: 8px 16px;
  border-bottom: 1px solid color-mix(in oklch, var(--border-color) 55%, white);
  cursor: pointer;
  font-size: 13px;
  line-height: 1.4;
  min-height: 34px;
}

.rm-row:last-child {
  border-bottom: 0;
}

/* Caret icon */
.caret {
  color: var(--text-muted);
  transition: transform 0.15s ease;
  flex-shrink: 0;
}

.caret.open {
  transform: rotate(0deg);
}

.caret:not(.open) {
  transform: rotate(-90deg);
}

/* Level 1: device */
.rm-row.lvl-dev {
  grid-template-columns: 10px auto 1fr auto;
  background: color-mix(in oklch, var(--bg-surface-subtle) 40%, var(--bg-surface));
  font-weight: 500;
}

.rm-row.lvl-dev:hover {
  background: var(--bg-surface-subtle);
}

.rm-slave {
  font-family: var(--font-mono);
  font-size: 12px;
  color: var(--text-color);
  letter-spacing: 0.02em;
  white-space: nowrap;
  font-weight: 600;
}

.rm-meta {
  display: inline-flex;
  align-items: center;
  gap: 14px;
  font-size: 11.5px;
  color: var(--text-secondary);
  white-space: nowrap;
  flex-shrink: 0;
}

.rm-meta-item {
  display: inline-flex;
  gap: 5px;
  align-items: baseline;
}

.rm-meta .dim {
  color: var(--text-muted);
  font-size: 10px;
  text-transform: uppercase;
  letter-spacing: 0.06em;
}

/* Level 2: group */
.rm-row.lvl-grp {
  grid-template-columns: 10px 64px 1fr auto;
  padding-left: 46px;
  background: var(--bg-surface);
  font-size: 12.5px;
}

.rm-row.lvl-grp:hover {
  background: var(--bg-surface-subtle);
}

.rm-grp-tag {
  font-family: var(--font-mono);
  font-size: 10.5px;
  font-weight: 600;
  padding: 2px 8px;
  border-radius: 3px;
  background: color-mix(in oklch, var(--primary-color) 12%, var(--bg-surface-subtle));
  color: color-mix(in oklch, var(--primary-color) 70%, var(--text-color));
  letter-spacing: 0.04em;
  text-transform: uppercase;
  text-align: center;
}

.rm-grp-tag.tag-input {
  background: color-mix(in oklch, var(--info) 12%, var(--bg-surface-subtle));
  color: color-mix(in oklch, var(--info) 70%, var(--text-color));
}

.rm-grp-tag.tag-coil {
  background: color-mix(in oklch, var(--warn) 15%, var(--bg-surface-subtle));
  color: color-mix(in oklch, var(--warn) 60%, var(--text-color));
}

.rm-grp-tag.tag-discrete {
  background: var(--bg-surface-subtle);
  color: var(--text-secondary);
}

.rm-grp-tag.tag-holding {
  background: color-mix(in oklch, var(--primary-color) 12%, var(--bg-surface-subtle));
  color: color-mix(in oklch, var(--primary-color) 70%, var(--text-color));
}

.rm-name {
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  color: var(--text-secondary);
}

/* Level 3: registers */
.rm-regs {
  background: var(--bg-surface-subtle);
  border-top: 1px solid color-mix(in oklch, var(--border-color) 60%, white);
  border-bottom: 1px solid color-mix(in oklch, var(--border-color) 60%, white);
}

.rm-reg-head,
.rm-row.lvl-reg {
  display: grid;
  grid-template-columns: 70px minmax(100px, 1fr) 140px 110px;
  align-items: center;
  gap: 12px;
  padding: 6px 16px 6px 88px;
  font-size: 12px;
  line-height: 1.4;
  min-height: 0;
}

.rm-reg-head {
  padding-top: 8px;
  padding-bottom: 8px;
  background: color-mix(in oklch, var(--bg-surface-subtle) 50%, var(--bg-surface-subtle));
  border-bottom: 1px solid color-mix(in oklch, var(--border-color) 50%, white);
  font-size: 10px;
  text-transform: uppercase;
  letter-spacing: 0.08em;
  color: var(--text-muted);
  font-weight: 500;
  cursor: default;
}

.rm-row.lvl-reg {
  cursor: default;
  border-bottom: 1px solid color-mix(in oklch, var(--border-color) 35%, white);
}

.rm-row.lvl-reg:last-child {
  border-bottom: 0;
}

.rm-row.lvl-reg:hover {
  background: color-mix(in oklch, var(--primary-color) 4%, var(--bg-surface-subtle));
}

.rm-row.lvl-reg.stale .rm-reg-val,
.rm-row.lvl-reg.stale .rm-reg-hits {
  color: var(--text-muted);
}

.rm-reg-val {
  text-align: right;
  font-weight: 500;
  color: var(--text-color);
}

.rm-reg-hits {
  text-align: right;
  color: var(--text-secondary);
}

.rm-reg-addr {
  color: var(--text-secondary);
}

.rm-reg-upd {
  font-size: 11px;
  color: var(--text-muted);
}

/* Stale indicator dot */
.stale-dot {
  display: inline-block;
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: var(--warn);
  margin-right: 6px;
  vertical-align: middle;
}

/* ── Utility ────────────────────────────────────────────────────── */
.mono {
  font-family: var(--font-mono);
}

.dim {
  color: var(--text-muted);
}
</style>

<i18n>
{
  "en": {
    "title": "Register map",
    "crumbs": "Auto-built from bus traffic · lives in RAM, cleared on reset"
  },
  "ru": {
    "title": "Карта регистров",
    "crumbs": "Строится автоматически из трафика шины · хранится в ОЗУ, сбрасывается при перезагрузке"
  }
}
</i18n>
