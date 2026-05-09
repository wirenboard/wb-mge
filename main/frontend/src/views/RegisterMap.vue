<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted } from 'vue';
import { useI18n } from 'vue-i18n';
import { useInfo } from '@/common/info';
import { api } from '@/utils/api';
import Button from '@/components/Button.vue';
import Heading from '@/components/Heading.vue';
import Layout from '@/components/Layout.vue';
import Switch from '@/components/Switch.vue';
import {
  type CacheEntry, type RegRow, type DeviceNode,
  formatAgeUs, formatMemory, formatAge, typeName,
  resolvePortSelection,
  buildDevices, buildRegsByKey, buildExportPayload, filterDevices,
} from '@/utils/registerMapUtils';

const { t } = useI18n();
const { info } = useInfo();

const rawEntries = ref<CacheEntry[]>([]);

// Stats from GET /cache/status
const cachePackets = ref(0);
const cacheLastPacketAgeUs = ref(0);
const cacheMapAgeUs = ref(0);
const cacheMemoryBytes = ref(0);
const cacheMaxEntries = ref(0);
const cacheEntries = ref(0);
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
// TCP Modbus server port (editable, loaded from device on first info poll)
const cacheTcpPort = ref(504);
// Whether the TCP Modbus server is enabled (editable, loaded from info on first poll)
const tcpServeEnabled = ref(true);
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
  } catch {
    // Silently ignore fetch errors
  }
}

async function fetchEntries(): Promise<void> {
  // Skip the fetch when cache is not active to avoid pointless requests.
  if (!cacheEnabled.value) return;
  try {
    const data = await api<{ d: CacheEntry[] }>('cache/json');
    rawEntries.value = data.d;
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
      // Disable: switch all ports currently in cache_bus back to disabled
      if (info.value?.rs485_1.port_mode === 'cache_bus') {
        await api<void>('ports/1/mode', { method: 'POST', json: { mode: 'disabled' } });
      }
      if (info.value?.rs485_2.port_mode === 'cache_bus') {
        await api<void>('ports/2/mode', { method: 'POST', json: { mode: 'disabled' } });
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
      await api<void>('ports/1/mode', { method: 'POST', json: { mode: 'disabled' } });
    }
    if (port2WasActive) {
      await api<void>('ports/2/mode', { method: 'POST', json: { mode: 'disabled' } });
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
  // Guard against double-true: if both ports read as cache_bus on first poll,
  // default to port 1. The (F,F) case is intentionally left as-is — it reflects
  // a genuine "neither port is in cache_bus mode" state from the device.
  if (listenPort1.value && listenPort2.value) {
    listenPort2.value = false;
  }
  cacheTcpPort.value = newInfo.cache_modbus_port ?? 504;
  tcpServeEnabled.value = newInfo.cache_modbus_server_enabled ?? true;
  valueTimeout.value = newInfo.cache_value_timeout_s ?? 60;
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
  const resolved = resolvePortSelection(listenPort1.value, listenPort2.value);
  listenPort1.value = resolved.p1;
  listenPort2.value = resolved.p2;
  settingsSaveStatus.value = 'saving';
  try {
    const port1TargetMode = listenPort1.value ? 'cache_bus' : 'disabled';
    const port2TargetMode = listenPort2.value ? 'cache_bus' : 'disabled';
    if (info.value?.rs485_1.port_mode !== port1TargetMode) {
      await api<void>('ports/1/mode', { method: 'POST', json: { mode: port1TargetMode } });
    }
    if (info.value?.rs485_2.port_mode !== port2TargetMode) {
      await api<void>('ports/2/mode', { method: 'POST', json: { mode: port2TargetMode } });
    }
    if (info.value && cacheTcpPort.value !== info.value.cache_modbus_port) {
      await api<void>('settings', { method: 'POST', json: { cache_modbus_port: cacheTcpPort.value } });
    }
    if (info.value && tcpServeEnabled.value !== info.value.cache_modbus_server_enabled) {
      await api<void>('settings', { method: 'POST', json: { cache_modbus_server_enabled: tcpServeEnabled.value } });
    }
    if (info.value && valueTimeout.value !== info.value.cache_value_timeout_s) {
      await api<void>('settings', { method: 'POST', json: { cache_value_timeout_s: valueTimeout.value } });
    }
    settingsSaveStatus.value = 'saved';
  } catch {
    settingsSaveStatus.value = 'error';
  }
  saveStatusTimer = setTimeout(() => { settingsSaveStatus.value = 'idle'; }, 3000);
}

// Open a URL in a new browser tab
function openUrl(url: string): void {
  window.open(url, '_blank', 'noopener');
}

// Build a nested human-readable JSON from rawEntries and trigger a browser file download
function downloadJsonExport(): void {
  const payload = buildExportPayload(rawEntries.value);
  const fullPayload = { exported_at: new Date().toISOString(), ...payload };

  // Generate filename suffix from local time: YYYY-MM-DDTHH-mm-ss
  const now = new Date();
  const pad = (n: number) => String(n).padStart(2, '0');
  const suffix =
    `${now.getFullYear()}-${pad(now.getMonth() + 1)}-${pad(now.getDate())}` +
    `T${pad(now.getHours())}-${pad(now.getMinutes())}-${pad(now.getSeconds())}`;
  const filename = `register-map-${suffix}.json`;

  // Trigger browser download via a temporary anchor element
  // The element must be appended to the DOM before .click() for Firefox compatibility
  const blob = new Blob([JSON.stringify(fullPayload, null, 2)], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  a.style.display = 'none';
  document.body.appendChild(a);
  a.click();
  a.remove();
  URL.revokeObjectURL(url);
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
const devices = computed((): DeviceNode[] => buildDevices(rawEntries.value));

// Build register rows keyed by "slaveId|TypeName"
const regsByKey = computed((): Record<string, RegRow[]> =>
  buildRegsByKey(rawEntries.value, valueTimeout.value));

// Filter devices by search query (slave id decimal or hex)
const filteredDevices = computed((): DeviceNode[] => filterDevices(devices.value, searchFilter.value));

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
            <span class="rm-caching-k">{{ t('caching') }}</span>
            <span :class="['rm-caching-state', cacheEnabled ? 'on' : 'off']">{{ cacheEnabled ? t('on') : t('off') }}</span>
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
          <div class="rm-off-title">{{ t('caching_disabled_title') }}</div>
          <div class="rm-off-sub">{{ t('caching_disabled_sub') }}</div>
          <Button variant="primary" @click="toggleCaching()">{{ t('enable_caching') }}</Button>
        </div>
      </div>

      <!-- First-load spinner -->
      <div v-else-if="loading" class="rm-loading-wrap">
        <svg class="rm-spinner" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
          <circle cx="12" cy="12" r="10" stroke-opacity="0.2" />
          <path d="M12 2a10 10 0 0 1 10 10" />
        </svg>
        {{ t('loading') }}
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
            <div class="stat-label">{{ t('stat_slaves_registers') }}</div>
            <div class="stat-sub">{{ t('stat_seen_on_bus') }}</div>
            <div class="stat-value">
              <b>{{ devices.length }}</b><span class="stat-dim"> / {{ rawEntries.length }}</span>
            </div>
          </div>
          <div class="stat-block">
            <div class="stat-label">{{ t('stat_packets') }}</div>
            <div class="stat-sub">{{ t('stat_since_reset') }}</div>
            <div class="stat-value">{{ cachePackets }}</div>
          </div>
          <div class="stat-block">
            <div class="stat-label">{{ t('stat_last_packet') }}</div>
            <div class="stat-sub">{{ t('stat_ago') }}</div>
            <div class="stat-value">{{ formatAgeUs(cacheLastPacketAgeUs) }}</div>
          </div>
          <div class="stat-block">
            <div class="stat-label">{{ t('stat_map_age') }}</div>
            <div class="stat-sub">{{ t('stat_since_reset') }}</div>
            <div class="stat-value">{{ formatAgeUs(cacheMapAgeUs) }}</div>
          </div>
          <div class="stat-block stat-block--with-entries">
            <div class="stat-label">{{ t('stat_memory') }}</div>
            <div class="stat-sub">{{ t('stat_pool_size') }}</div>
            <div class="stat-value">{{ formatMemory(cacheMemoryBytes) }}<span class="stat-dim"> / {{ formatMemory(cacheMaxEntries * 8) }}</span></div>
            <div class="stat-entries">
              <span class="stat-entries-val">{{ cacheEntries }}</span>
              <span class="stat-entries-dim"> / {{ cacheMaxEntries > 0 ? cacheMaxEntries : '—' }} {{ t('entries') }}</span>
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
                <div class="rm-off-title">{{ t('no_devices_title') }}</div>
                <div class="rm-off-sub">{{ t('no_devices_sub') }}</div>
              </div>
            </div>

            <!-- Map card with tree -->
            <div v-else class="rm-map-card">
              <div class="rm-map-header">
                <div class="rm-map-title-wrap">
                  <div class="rm-map-title">{{ t('map_title') }}</div>
                  <div class="rm-map-sub">{{ t('map_sub') }}</div>
                </div>
                <div class="rm-map-actions">
                  <div class="rm-search">
                    <svg width="12" height="12" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><circle cx="7" cy="7" r="4"/><path d="M10 10l3.5 3.5"/></svg>
                    <input v-model="searchFilter" :placeholder="t('filter_placeholder')" />
                  </div>
                  <button class="rm-tb-btn" @click="expandAll()">{{ t('expand_all') }}</button>
                  <button class="rm-tb-btn" @click="collapseAll()">{{ t('collapse') }}</button>
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
                          <span class="dim">{{ t('last') }}</span>
                          <span class="mono">{{ formatAge(dev.lastSeenAge) }} {{ t('ago') }}</span>
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
                        <span class="rm-name">{{ group }} {{ t('registers') }}</span>
                        <span class="rm-meta"><span class="mono">{{ (regsByKey[`${dev.id}|${group}`] || []).length }}</span></span>
                      </div>

                      <!-- Register rows — shown when group is expanded -->
                      <div v-if="openGroups.has(`${dev.id}|${group}`)" class="rm-regs">
                        <!-- Column headers -->
                        <div class="rm-reg-head">
                          <span>{{ t('col_addr') }}</span>
                          <span style="text-align:right">{{ t('col_value') }}</span>
                          <span>{{ t('col_last_update') }}</span>
                          <span style="text-align:right">{{ t('col_responses') }}</span>
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
                            <span v-if="reg.stale" class="stale-dot" :title="t('stale_dot_title')" />
                            {{ formatAge(reg.updatedAge) }} {{ t('ago') }}
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
            <div class="rsp-title">{{ t('settings_title') }}</div>

            <!-- CACHING section -->
            <div class="rsp-section-label">{{ t('section_caching') }}</div>

            <!-- Value timeout row -->
            <div class="rsp-row">
              <div class="rsp-control rsp-control--stacked">
                <input type="number" class="rsp-input" v-model.number="valueTimeout" min="0" max="86400" />
                <span class="rsp-unit">{{ t('seconds') }}</span>
              </div>
              <div class="rsp-row-info">
                <div class="rsp-row-title">{{ t('value_timeout_title') }}</div>
                <div class="rsp-row-desc">{{ t('value_timeout_desc') }}</div>
              </div>
            </div>

            <!-- Reset map row -->
            <div class="rsp-row">
              <div class="rsp-control">
                <button class="rsp-btn-reset" @click="resetMap()">{{ t('reset_btn') }}</button>
              </div>
              <div class="rsp-row-info">
                <div class="rsp-row-title">{{ t('reset_map_title') }}</div>
                <div class="rsp-row-desc">{{ t('reset_map_desc') }}</div>
              </div>
            </div>

            <div class="rsp-divider" />

            <!-- SOURCE section -->
            <div class="rsp-section-label">{{ t('section_source') }}</div>

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
                <div class="rsp-row-title">{{ t('listen_port_title') }}</div>
                <div class="rsp-row-desc">{{ t('listen_port_desc') }}</div>
              </div>
            </div>

            <div class="rsp-divider" />

            <!-- EXPORT section -->
            <div class="rsp-section-label">{{ t('section_export') }}</div>

            <!-- Export row -->
            <div class="rsp-row">
              <div class="rsp-control rsp-control--btns">
                <button class="rsp-btn-export" @click="openUrl('/cache/csv')">{{ t('export_csv') }}</button>
                <button class="rsp-btn-export" @click="downloadJsonExport()">{{ t('export_json') }}</button>
              </div>
              <div class="rsp-row-info">
                <div class="rsp-row-title">{{ t('export_title') }}</div>
                <div class="rsp-row-desc">{{ t('export_desc') }}</div>
              </div>
            </div>

            <div class="rsp-divider" />

            <!-- TCP MODBUS section -->
            <div class="rsp-section-label">{{ t('section_tcp') }}</div>

            <!-- TCP serve toggle row -->
            <div class="rsp-row">
              <div class="rsp-control">
                <Switch id="rsp-tcp-serve" v-model="tcpServeEnabled" :ariaLabel="t('tcp_serve_title')" />
              </div>
              <div class="rsp-row-info">
                <div class="rsp-row-title">{{ t('tcp_serve_title') }}</div>
                <div class="rsp-row-desc">{{ t('tcp_serve_desc') }}</div>
              </div>
            </div>

            <!-- TCP port row -->
            <div class="rsp-row">
              <div class="rsp-control">
                <input type="number" class="rsp-input" v-model.number="cacheTcpPort" min="1" max="65535" />
              </div>
              <div class="rsp-row-info">
                <div class="rsp-row-title">{{ t('tcp_port_title') }}</div>
                <div class="rsp-row-desc">{{ t('tcp_port_desc') }}</div>
              </div>
            </div>

            <!-- Save footer -->
            <div class="rsp-footer">
              <span class="rsp-status">
                <template v-if="settingsSaveStatus === 'saved'">{{ t('saved') }}</template>
                <template v-else-if="settingsSaveStatus === 'error'">{{ t('save_error') }}</template>
              </span>
              <button class="rsp-btn-save" @click="saveSettings()" :disabled="settingsSaveStatus === 'saving'">
                {{ t('save_changes') }}
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
  align-items: center; /* controls are vertically centered vs description */
  gap: 12px;
  padding: 10px 18px;
}

.rsp-control {
  flex-shrink: 0;
  display: flex;
  align-items: center;
  justify-content: center; /* center compact controls (switch, inputs) horizontally */
  gap: 6px;
  width: 70px; /* fixed width so all row titles align */
}

.rsp-input {
  width: 100%; /* fill the full 70px control column */
  box-sizing: border-box; /* prevent padding from overflowing */
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
  justify-content: center; /* center text within full-width button */
  gap: 4px;
  width: 100%; /* fill the full 70px control column */
  box-sizing: border-box;
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

.rsp-btn-reset:hover:not(:disabled) {
  background: color-mix(in oklch, var(--danger-color) 30%, white);
  border-color: var(--danger-color);
}

/* Stack input and unit label vertically (e.g. for the "seconds" unit below input) */
.rsp-control--stacked {
  flex-direction: column;
  align-items: center; /* center the "seconds" label under the input */
  gap: 2px;
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
    "crumbs": "Auto-built from bus traffic · lives in RAM, cleared on reset",
    "caching_disabled_title": "Register map caching is disabled",
    "caching_disabled_sub": "Enable caching to start recording Modbus register traffic from the bus.",
    "enable_caching": "Enable caching",
    "loading": "Loading…",
    "caching": "Caching",
    "on": "On",
    "off": "Off",
    "stat_slaves_registers": "Slaves / Registers",
    "stat_seen_on_bus": "seen on bus",
    "stat_packets": "Packets processed",
    "stat_since_reset": "since last reset",
    "stat_last_packet": "Last packet",
    "stat_ago": "ago",
    "stat_map_age": "Map age",
    "stat_memory": "Memory",
    "stat_pool_size": "used / pool size",
    "no_devices_title": "No devices seen yet",
    "no_devices_sub": "Waiting for Modbus traffic on the bus…",
    "map_title": "Map",
    "map_sub": "Device → register type → register",
    "filter_placeholder": "Filter by slave ID…",
    "expand_all": "Expand all",
    "collapse": "Collapse",
    "col_addr": "Addr",
    "col_value": "Raw value",
    "col_last_update": "Last update",
    "col_responses": "Responses",
    "stale_dot_title": "Older than value timeout",
    "ago": "ago",
    "last": "last",
    "registers": "registers",
    "entries": "entries",
    "settings_title": "Settings",
    "section_caching": "CACHING",
    "value_timeout_title": "Value timeout",
    "seconds": "seconds",
    "value_timeout_desc": "If a register's last update is older than this, the gateway returns Modbus error 0x0B instead of the cached value. Set to 0 to disable the timeout (always serve cached values).",
    "reset_map_title": "Reset map",
    "reset_btn": "↺ Reset",
    "reset_map_desc": "Drops every observed register from RAM. Map will rebuild from new traffic.",
    "section_source": "SOURCE",
    "listen_port_title": "Listen port",
    "listen_port_desc": "Which serial port the cache observes for register traffic.",
    "section_export": "EXPORT",
    "export_csv": "↓ CSV",
    "export_json": "↓ JSON",
    "export_title": "Download current map",
    "export_desc": "Snapshot of every observed register and its last cached value.",
    "section_tcp": "TCP MODBUS",
    "tcp_serve_title": "Serve cached values over TCP",
    "tcp_serve_desc": "Reply to TCP Modbus reads from cache without round-tripping the bus.",
    "tcp_port_title": "TCP port",
    "tcp_port_desc": "Port the gateway listens on for TCP Modbus clients.",
    "saved": "Saved",
    "save_error": "Error saving",
    "save_changes": "Save changes"
  },
  "ru": {
    "title": "Карта регистров",
    "crumbs": "Строится автоматически из трафика шины · хранится в ОЗУ, сбрасывается при перезагрузке",
    "caching_disabled_title": "Кэширование карты регистров отключено",
    "caching_disabled_sub": "Включите кэширование, чтобы начать запись трафика Modbus-регистров с шины.",
    "enable_caching": "Включить кэширование",
    "loading": "Загрузка…",
    "caching": "Кэширование",
    "on": "Вкл",
    "off": "Выкл",
    "stat_slaves_registers": "Устройства / Регистры",
    "stat_seen_on_bus": "замечено на шине",
    "stat_packets": "Пакетов обработано",
    "stat_since_reset": "с последнего сброса",
    "stat_last_packet": "Последний пакет",
    "stat_ago": "назад",
    "stat_map_age": "Возраст карты",
    "stat_memory": "Память",
    "stat_pool_size": "занято / размер пула",
    "no_devices_title": "Устройства ещё не найдены",
    "no_devices_sub": "Ожидание трафика Modbus на шине…",
    "map_title": "Карта",
    "map_sub": "Устройство → тип регистра → регистр",
    "filter_placeholder": "Фильтр по адресу slave…",
    "expand_all": "Развернуть всё",
    "collapse": "Свернуть",
    "col_addr": "Адрес",
    "col_value": "Значение",
    "col_last_update": "Последнее обновление",
    "col_responses": "Ответы",
    "stale_dot_title": "Старше таймаута значения",
    "ago": "назад",
    "last": "последний",
    "registers": "регистры",
    "entries": "записей",
    "settings_title": "Настройки",
    "section_caching": "КЭШИРОВАНИЕ",
    "value_timeout_title": "Таймаут значения",
    "seconds": "секунд",
    "value_timeout_desc": "Если последнее обновление регистра старше этого значения, шлюз вернёт Modbus 0x0B вместо значения. Установите 0, чтобы отключить (всегда отдавать кэшированные значения).",
    "reset_map_title": "Сброс карты",
    "reset_btn": "↺ Сброс",
    "reset_map_desc": "Удаляет все наблюдавшиеся регистры из ОЗУ. Карта будет перестроена из нового трафика.",
    "section_source": "ИСТОЧНИК",
    "listen_port_title": "Порт прослушивания",
    "listen_port_desc": "Последовательный порт, который кэш отслеживает для трафика регистров.",
    "section_export": "ЭКСПОРТ",
    "export_csv": "↓ CSV",
    "export_json": "↓ JSON",
    "export_title": "Скачать текущую карту",
    "export_desc": "Снимок всех наблюдавшихся регистров и их последних кэшированных значений.",
    "section_tcp": "TCP MODBUS",
    "tcp_serve_title": "Отдавать кэшированные значения по TCP",
    "tcp_serve_desc": "Отвечать на запросы TCP Modbus из кэша без обращения к шине.",
    "tcp_port_title": "TCP-порт",
    "tcp_port_desc": "Порт, на котором шлюз принимает подключения TCP Modbus клиентов.",
    "saved": "Сохранено",
    "save_error": "Ошибка сохранения",
    "save_changes": "Сохранить изменения"
  },
  "kk": {
    "title": "Тіркеу картасы",
    "crumbs": "Шина трафигінен автоматты түрде жасалады · ЖЖҚ-да сақталады, қалпына келтіру кезінде тазаланады",
    "caching_disabled_title": "Тіркеу картасын кэштеу өшірілген",
    "caching_disabled_sub": "Шинадан Modbus тіркеу трафигін жазуды бастау үшін кэштеуді қосыңыз.",
    "enable_caching": "Кэштеуді қосу",
    "loading": "Жүктелуде…",
    "caching": "Кэштеу",
    "on": "Қосулы",
    "off": "Өшірулі",
    "stat_slaves_registers": "Құрылғылар / Тіркеулер",
    "stat_seen_on_bus": "шинада байқалды",
    "stat_packets": "Өңделген пакеттер",
    "stat_since_reset": "соңғы қалпына келтіруден бері",
    "stat_last_packet": "Соңғы пакет",
    "stat_ago": "бұрын",
    "stat_map_age": "Картаның жасы",
    "stat_memory": "Жады",
    "stat_pool_size": "пайдаланылған / пул өлшемі",
    "no_devices_title": "Құрылғылар әлі табылмады",
    "no_devices_sub": "Шинадан Modbus трафигін күтуде…",
    "map_title": "Карта",
    "map_sub": "Құрылғы → тіркеу түрі → тіркеу",
    "filter_placeholder": "Slave ID бойынша сүзу…",
    "expand_all": "Барлығын жайу",
    "collapse": "Жию",
    "col_addr": "Мекенжай",
    "col_value": "Мән",
    "col_last_update": "Соңғы жаңарту",
    "col_responses": "Жауаптар",
    "stale_dot_title": "Мән таймаутынан ескі",
    "ago": "бұрын",
    "last": "соңғы",
    "registers": "тіркеулер",
    "entries": "жазбалар",
    "settings_title": "Параметрлер",
    "section_caching": "КЭШТЕУ",
    "value_timeout_title": "Мән таймауты",
    "seconds": "секунд",
    "value_timeout_desc": "Тіркеудің соңғы жаңартуы осы мәннен ескі болса, шлюз кэштелген мән орнына Modbus 0x0B қатесін қайтарады. Таймаутты өшіру үшін 0 орнатыңыз.",
    "reset_map_title": "Картаны қалпына келтіру",
    "reset_btn": "↺ Қалпына келтіру",
    "reset_map_desc": "Барлық байқалған тіркеулерді ЖЖҚ-дан жояды. Карта жаңа трафиктен қалпына келтіріледі.",
    "section_source": "ДЕРЕККӨЗ",
    "listen_port_title": "Тыңдау порты",
    "listen_port_desc": "Кэш тіркеу трафигін бақылайтын сериялық порт.",
    "section_export": "ЭКСПОРТ",
    "export_csv": "↓ CSV",
    "export_json": "↓ JSON",
    "export_title": "Ағымдағы картаны жүктеу",
    "export_desc": "Барлық байқалған тіркеулер мен олардың соңғы кэштелген мәндерінің суреті.",
    "section_tcp": "TCP MODBUS",
    "tcp_serve_title": "Кэштелген мәндерді TCP арқылы беру",
    "tcp_serve_desc": "TCP Modbus сұрауларына шинаға бармай кэштен жауап беру.",
    "tcp_port_title": "TCP порты",
    "tcp_port_desc": "Шлюздің TCP Modbus клиенттерін қабылдайтын порты.",
    "saved": "Сақталды",
    "save_error": "Сақтау қатесі",
    "save_changes": "Өзгерістерді сақтау"
  },
  "it": {
    "title": "Mappa registri",
    "crumbs": "Costruita automaticamente dal traffico bus · vive in RAM, cancellata al reset",
    "caching_disabled_title": "La cache della mappa registri è disabilitata",
    "caching_disabled_sub": "Abilita la cache per iniziare a registrare il traffico dei registri Modbus dal bus.",
    "enable_caching": "Abilita cache",
    "loading": "Caricamento…",
    "caching": "Cache",
    "on": "On",
    "off": "Off",
    "stat_slaves_registers": "Slave / Registri",
    "stat_seen_on_bus": "visti sul bus",
    "stat_packets": "Pacchetti elaborati",
    "stat_since_reset": "dall'ultimo reset",
    "stat_last_packet": "Ultimo pacchetto",
    "stat_ago": "fa",
    "stat_map_age": "Età mappa",
    "stat_memory": "Memoria",
    "stat_pool_size": "usata / dim. pool",
    "no_devices_title": "Nessun dispositivo ancora rilevato",
    "no_devices_sub": "In attesa di traffico Modbus sul bus…",
    "map_title": "Mappa",
    "map_sub": "Dispositivo → tipo registro → registro",
    "filter_placeholder": "Filtra per slave ID…",
    "expand_all": "Espandi tutto",
    "collapse": "Comprimi",
    "col_addr": "Indirizzo",
    "col_value": "Valore raw",
    "col_last_update": "Ultimo agg.",
    "col_responses": "Risposte",
    "stale_dot_title": "Più vecchio del timeout valore",
    "ago": "fa",
    "last": "ultimo",
    "registers": "registri",
    "entries": "voci",
    "settings_title": "Impostazioni",
    "section_caching": "CACHE",
    "value_timeout_title": "Timeout valore",
    "seconds": "secondi",
    "value_timeout_desc": "Se l'ultimo aggiornamento di un registro è più vecchio di questo valore, il gateway restituisce l'errore Modbus 0x0B invece del valore in cache. Imposta 0 per disabilitare il timeout.",
    "reset_map_title": "Azzera mappa",
    "reset_btn": "↺ Azzera",
    "reset_map_desc": "Rimuove tutti i registri osservati dalla RAM. La mappa verrà ricostruita dal nuovo traffico.",
    "section_source": "SORGENTE",
    "listen_port_title": "Porta di ascolto",
    "listen_port_desc": "Quale porta seriale la cache monitora per il traffico dei registri.",
    "section_export": "ESPORTA",
    "export_csv": "↓ CSV",
    "export_json": "↓ JSON",
    "export_title": "Scarica mappa corrente",
    "export_desc": "Snapshot di tutti i registri osservati e del loro ultimo valore in cache.",
    "section_tcp": "TCP MODBUS",
    "tcp_serve_title": "Servi valori in cache via TCP",
    "tcp_serve_desc": "Rispondi alle letture TCP Modbus dalla cache senza accedere al bus.",
    "tcp_port_title": "Porta TCP",
    "tcp_port_desc": "Porta su cui il gateway ascolta i client TCP Modbus.",
    "saved": "Salvato",
    "save_error": "Errore salvataggio",
    "save_changes": "Salva modifiche"
  },
  "de": {
    "title": "Register-Karte",
    "crumbs": "Automatisch aus Bus-Verkehr aufgebaut · lebt im RAM, wird beim Reset gelöscht",
    "caching_disabled_title": "Register-Karten-Cache ist deaktiviert",
    "caching_disabled_sub": "Cache aktivieren, um Modbus-Register-Verkehr vom Bus aufzuzeichnen.",
    "enable_caching": "Cache aktivieren",
    "loading": "Wird geladen…",
    "caching": "Cache",
    "on": "Ein",
    "off": "Aus",
    "stat_slaves_registers": "Slaves / Register",
    "stat_seen_on_bus": "auf Bus gesehen",
    "stat_packets": "Verarbeitete Pakete",
    "stat_since_reset": "seit letztem Reset",
    "stat_last_packet": "Letztes Paket",
    "stat_ago": "vor",
    "stat_map_age": "Kartenalter",
    "stat_memory": "Speicher",
    "stat_pool_size": "belegt / Pool-Größe",
    "no_devices_title": "Noch keine Geräte gesehen",
    "no_devices_sub": "Warte auf Modbus-Verkehr auf dem Bus…",
    "map_title": "Karte",
    "map_sub": "Gerät → Registertyp → Register",
    "filter_placeholder": "Nach Slave-ID filtern…",
    "expand_all": "Alle erweitern",
    "collapse": "Einklappen",
    "col_addr": "Adresse",
    "col_value": "Rohwert",
    "col_last_update": "Letztes Update",
    "col_responses": "Antworten",
    "stale_dot_title": "Älter als Wert-Timeout",
    "ago": "her",
    "last": "zuletzt",
    "registers": "Register",
    "entries": "Einträge",
    "settings_title": "Einstellungen",
    "section_caching": "CACHE",
    "value_timeout_title": "Wert-Timeout",
    "seconds": "Sekunden",
    "value_timeout_desc": "Wenn das letzte Update eines Registers älter als dieser Wert ist, gibt das Gateway den Modbus-Fehler 0x0B zurück. 0 setzen, um das Timeout zu deaktivieren.",
    "reset_map_title": "Karte zurücksetzen",
    "reset_btn": "↺ Zurücksetzen",
    "reset_map_desc": "Entfernt alle beobachteten Register aus dem RAM. Die Karte wird aus neuem Verkehr neu aufgebaut.",
    "section_source": "QUELLE",
    "listen_port_title": "Abhörport",
    "listen_port_desc": "Welcher serielle Port vom Cache auf Register-Verkehr überwacht wird.",
    "section_export": "EXPORT",
    "export_csv": "↓ CSV",
    "export_json": "↓ JSON",
    "export_title": "Aktuelle Karte herunterladen",
    "export_desc": "Snapshot aller beobachteten Register und ihres letzten gecachten Wertes.",
    "section_tcp": "TCP MODBUS",
    "tcp_serve_title": "Gecachte Werte über TCP bereitstellen",
    "tcp_serve_desc": "TCP-Modbus-Leseanfragen aus dem Cache beantworten, ohne den Bus zu nutzen.",
    "tcp_port_title": "TCP-Port",
    "tcp_port_desc": "Port, auf dem das Gateway auf TCP-Modbus-Clients wartet.",
    "saved": "Gespeichert",
    "save_error": "Speicherfehler",
    "save_changes": "Änderungen speichern"
  }
}
</i18n>
