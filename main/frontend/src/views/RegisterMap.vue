<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted } from 'vue';
import { useI18n } from 'vue-i18n';
import { useInfo } from '@/common/info';
import { api } from '@/utils/api';
import Button from '@/components/Button.vue';
import Heading from '@/components/Heading.vue';
import Layout from '@/components/Layout.vue';

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
// Server-side "now" anchor in the same uint16_t-truncated seconds domain as entry timestamps.
// Used to compute ages correctly across wrap-around.
const cacheNowS = ref(0);

// Cache is considered enabled when ALL ports are in cache_bus mode.
// Derived reactively from the info ref polled globally every 5 s by App.vue.
const cacheEnabled = computed(() => {
  if (!info.value) return false;
  return info.value.rs485_1.port_mode === 'cache_bus' &&
         info.value.rs485_2.port_mode === 'cache_bus';
});

const loading = ref(true);
const error = ref<string | null>(null);
const valueTimeout = ref(60);
const openDevices = ref<Set<number>>(new Set());
const openGroups = ref<Set<string>>(new Set());
const searchFilter = ref('');

let pollInterval: ReturnType<typeof setInterval> | null = null;
let statsInterval: ReturnType<typeof setInterval> | null = null;

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
      now_s: number;
    }>('cache/status');
    cachePackets.value         = s.packets_processed;
    cacheLastPacketAgeUs.value = s.last_packet_age_us;
    cacheMapAgeUs.value        = s.map_age_us;
    cacheMemoryBytes.value     = s.memory_bytes;
    cacheMaxEntries.value      = s.max_entries;
    cacheNowS.value            = s.now_s;
  } catch {
    // Silently ignore fetch errors
  }
}

async function fetchEntries(): Promise<void> {
  // Skip the fetch when cache is not active to avoid pointless requests.
  if (!cacheEnabled.value) return;
  try {
    const data = await api<CacheEntry[]>('cache/json');
    rawEntries.value = data;
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
      // Disable: switch both ports back to tcp_bridge
      await api<void>('ports/1/mode', { method: 'POST', json: { mode: 'tcp_bridge' } });
      await api<void>('ports/2/mode', { method: 'POST', json: { mode: 'tcp_bridge' } });
      rawEntries.value = [];
      // cacheEnabled will update automatically on the next info poll
    } else {
      // Enable: switch both ports to cache_bus
      await api<void>('ports/1/mode', { method: 'POST', json: { mode: 'cache_bus' } });
      await api<void>('ports/2/mode', { method: 'POST', json: { mode: 'cache_bus' } });
      // Fetch entries immediately so the UI shows data without waiting for the next poll
      await fetchEntries();
    }
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Action failed';
    // cacheEnabled is derived from info — no manual resync needed
  }
}

async function resetMap(): Promise<void> {
  try {
    // Disable both ports, then re-enable to clear the cache
    await api<void>('ports/1/mode', { method: 'POST', json: { mode: 'tcp_bridge' } });
    await api<void>('ports/2/mode', { method: 'POST', json: { mode: 'tcp_bridge' } });
    await api<void>('ports/1/mode', { method: 'POST', json: { mode: 'cache_bus' } });
    await api<void>('ports/2/mode', { method: 'POST', json: { mode: 'cache_bus' } });
    rawEntries.value = [];
    // Fetch entries immediately so the UI reflects the cleared state
    await fetchEntries();
    // cacheEnabled will update automatically on the next info poll
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Reset failed';
    // cacheEnabled is derived from info — no manual resync needed
  }
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
      const slaveMaxTs = Math.max(0, ...entries.map(e => e.ts));
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
});
</script>

<template>
  <Layout>
    <Heading :title="t('title')" :crumbs="t('crumbs')">
      <template #default>
        <div class="rm-header-controls">
          <!-- VALUE TIMEOUT block -->
          <label class="rm-timeout">
            <span class="rm-timeout-k">Value timeout</span>
            <input
              class="rm-timeout-input"
              type="number"
              v-model.number="valueTimeout"
              min="1"
              max="86400"
              :disabled="!cacheEnabled"
            />
            <span class="rm-timeout-unit">s</span>
            <span class="rm-help" title="If a register's last update is older than this, the gateway returns Modbus error 0x0B instead of the cached value.">?</span>
          </label>
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

      <!-- Caching enabled: stats strip + tree -->
      <template v-else>
        <!-- Stats strip -->
        <div class="rm-strip">
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
            <div class="stat-block">
              <div class="stat-label">Memory</div>
              <div class="stat-sub">used / pool size</div>
              <div class="stat-value">{{ formatMemory(cacheMemoryBytes) }}<span class="stat-dim"> / {{ formatMemory(cacheMaxEntries * 8) }}</span></div>
            </div>
          </div>
          <div class="rm-actions">
            <Button variant="outline" @click="openUrl('/cache/csv')">
              <svg width="12" height="12" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M8 2v8m0 0l-3-3m3 3l3-3M2 12v1a1 1 0 0 0 1 1h10a1 1 0 0 0 1-1v-1"/></svg>
              Export CSV
            </Button>
            <Button variant="outline" @click="openUrl('/cache/json')">
              <svg width="12" height="12" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M8 2v8m0 0l-3-3m3 3l3-3M2 12v1a1 1 0 0 0 1 1h10a1 1 0 0 0 1-1v-1"/></svg>
              Export JSON
            </Button>
            <Button variant="danger" @click="resetMap()">
              <svg width="12" height="12" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M14 8a6 6 0 1 1-2-4.5M14 2v4h-4"/></svg>
              Reset map
            </Button>
          </div>
        </div>

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

/* VALUE TIMEOUT block */
.rm-timeout {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  height: 32px;
  padding: 0 6px 0 10px;
  border: 1px solid var(--border-color);
  border-radius: var(--r-md);
  background: var(--bg-surface-subtle);
  font-size: 12px;
  color: var(--text-secondary);
  cursor: default;
}

.rm-timeout-k {
  font-size: 11px;
  text-transform: uppercase;
  letter-spacing: 0.06em;
  color: var(--text-muted);
  font-weight: 500;
}

.rm-timeout-input {
  width: 48px;
  padding: 3px 6px;
  border: 1px solid var(--border-color);
  border-radius: 4px;
  background: #fff;
  font-family: var(--font-mono);
  font-size: 12px;
  color: var(--text-color);
  text-align: right;
}

.rm-timeout-input:focus {
  border-color: var(--primary-color);
  outline: none;
  box-shadow: var(--input-focus-shadow);
}

.rm-timeout-unit {
  font-family: var(--font-mono);
  font-size: 11px;
  color: var(--text-muted);
}

.rm-help {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 16px;
  height: 16px;
  border-radius: 50%;
  border: 1px solid var(--border-color);
  font-size: 10px;
  font-weight: 600;
  color: var(--text-muted);
  cursor: help;
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

/* ── Stats strip ────────────────────────────────────────────────── */
.rm-strip {
  display: grid;
  grid-template-columns: 1fr auto;
  gap: 12px;
  align-items: stretch;
}

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

.rm-actions {
  display: flex;
  flex-direction: column;
  gap: 6px;
  justify-content: flex-end;
  align-self: stretch;
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
