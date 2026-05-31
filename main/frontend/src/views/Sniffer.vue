<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted, nextTick } from 'vue';
import { useI18n } from 'vue-i18n';
import Button from '@/components/Button.vue';
import Heading from '@/components/Heading.vue';
import Layout from '@/components/Layout.vue';
import PacketDecoder from '@/components/PacketDecoder.vue';
import PacketSenderPopup from '@/components/PacketSenderPopup.vue';
import CheckmarkIcon from '@/assets/checkmarkIcon.svg?component';
import { useSettings } from '@/common/settings';
import { useInfo } from '@/common/info';
import { api } from '@/utils/api';
import {
  type SniffRow,
  type ByteRole,
  FC_NAMES,
  SLAVE_NAMES,
  parsePacket,
  toggleSet,
  computeVirtualWindow,
  trimToCap,
} from '@/utils/snifferUtils';

const { t } = useI18n();

const { data: settings, refresh: refreshSettings } = useSettings();
const { info, fetchInfo } = useInfo();
const senderOpen = ref(false);

const txDisabledForCurrentPort = computed(() => {
  if (portFilter.value === '1') return settings.value?.rs485_1?.tx_disabled ?? false;
  return settings.value?.rs485_2?.tx_disabled ?? false;
});

const rows = ref<SniffRow[]>([]);
const running = ref(false);
const ws = ref<WebSocket | null>(null);
// Timer handle for the WS reconnect delay — stored so it can be cleared in stopCapture().
let reconnectTimer: ReturnType<typeof setTimeout> | null = null;
let lastTimestampUs = 0;
const wsStatus = ref<'connected' | 'disconnected' | 'reconnecting'>('disconnected');

// Virtualization / ring buffer constants and state.
const MAX_ROWS = 50000; // ring buffer cap — oldest rows are dropped beyond this
const ROW_HEIGHT_FALLBACK = 29; // fallback row height in px until a real row is measured
const OVERSCAN = 10; // extra rows rendered above/below the viewport
const rowHeight = ref(ROW_HEIGHT_FALLBACK); // measured at runtime
const scrollTop = ref(0);
const viewportH = ref(0);
const autoScroll = ref(true); // follow-tail flag
let resizeObserver: ResizeObserver | null = null;

const tableWrap = ref<HTMLElement | null>(null);
const selected = ref<number | null>(null);
const portFilter = ref('1');
const portOptions = ['1', '2'];
const selectedSlaves = ref<Set<string>>(new Set());
const selectedFcs = ref<Set<string>>(new Set());
const hideErrors = ref(false);

function getWsUrl(): string {
  const proto = location.protocol === 'https:' ? 'wss' : 'ws';
  return `${proto}://${location.host}/sniffer/ws`;
}

function sendPortStart(port: number) {
  ws.value?.send(JSON.stringify({ cmd: 'start', port }));
}

function sendPortStop(port: number) {
  ws.value?.send(JSON.stringify({ cmd: 'stop', port }));
}

// Scroll handler — rAF-throttled so rapid scroll events do not spam reactive updates.
let scrollRaf: number | null = null;
function onScroll() {
  if (scrollRaf !== null) return;
  scrollRaf = requestAnimationFrame(() => {
    scrollRaf = null;
    const el = tableWrap.value;
    if (!el) return;
    scrollTop.value = el.scrollTop;
    viewportH.value = el.clientHeight;
    if (!rowMeasured) measureRowHeight();
    // Follow-tail: re-enable auto-scroll only when the user is within ~2 rows of the bottom.
    const distanceFromBottom = el.scrollHeight - el.scrollTop - el.clientHeight;
    autoScroll.value = distanceFromBottom <= rowHeight.value * 2;
  });
}

// Row height measurement: border-collapse makes a CSS-fixed height unreliable, so measure a
// real data row once. All data rows are identical height (single line, nowrap, same font),
// so measuring one is enough.
let rowMeasured = false;
function measureRowHeight() {
  const el = tableWrap.value?.querySelector('tr.sniff-row') as HTMLElement | null;
  if (el && el.offsetHeight > 0) {
    rowHeight.value = el.offsetHeight;
    rowMeasured = true;
  }
}

// Batched ingestion: parsing stays in onmessage, but the reactive array mutation is
// coalesced into a single rAF flush so high packet rates do not thrash Vue's reactivity.
let pending: SniffRow[] = [];
let flushRaf: number | null = null;
function scheduleFlush() {
  if (flushRaf !== null) return;
  flushRaf = requestAnimationFrame(flushPending);
}
function flushPending() {
  flushRaf = null;
  if (pending.length === 0) return;
  // Append buffered rows. Use a loop (not spread) so a large backlog — e.g. after the
  // tab was backgrounded and rAF was paused — cannot overflow the call-stack arg limit.
  for (const r of pending) rows.value.push(r);
  pending = [];
  // Ring buffer: drop the oldest rows once the cap is exceeded.
  const overflow = trimToCap(rows.value, MAX_ROWS);
  if (overflow > 0 && !autoScroll.value) {
    // When the user has scrolled up (not following the tail), dropping the oldest rows shifts
    // all remaining content up by overflow*rowHeight. Compensate the scroll position so the
    // view stays anchored, and resync scrollTop.value with the DOM — otherwise the window
    // would render the wrong slice until the next scroll event.
    nextTick(() => {
      const el = tableWrap.value;
      if (el) {
        el.scrollTop = Math.max(0, el.scrollTop - overflow * rowHeight.value);
        scrollTop.value = el.scrollTop;
      }
    });
  }
  if (!rowMeasured) nextTick(measureRowHeight);
  if (autoScroll.value) {
    nextTick(() => {
      const el = tableWrap.value;
      if (el) el.scrollTop = el.scrollHeight;
    });
  }
}

function connectWs() {
  lastTimestampUs = 0;
  ws.value = new WebSocket(getWsUrl());
  ws.value.onopen = () => {
    wsStatus.value = 'connected';
    sendPortStart(parseInt(portFilter.value));
  };
  ws.value.onmessage = (ev) => {
    try {
      const msg = JSON.parse(ev.data as string);
      const { row, timestamp } = parsePacket(msg, lastTimestampUs);
      if (row) {
        lastTimestampUs = timestamp;
        pending.push(row);
        scheduleFlush();
      }
    } catch (e) {
      console.warn('sniffer: failed to parse WS message', e);
    }
  };
  ws.value.onclose = () => {
    ws.value = null;
    if (running.value) {
      wsStatus.value = 'reconnecting';
      // Capture the timer handle so it can be cleared in stopCapture() / onUnmounted().
      reconnectTimer = setTimeout(connectWs, 2000);
    } else {
      wsStatus.value = 'disconnected';
    }
  };
}

/** Delay after switching port mode to allow firmware to complete serial port reinit. */
const PORT_MODE_SWITCH_DELAY_MS = 500;

async function startCapture() {
  // Set running immediately to prevent concurrent calls while async steps execute.
  // The button switches to "Stop" at once, so a second click calls stopCapture() instead.
  running.value = true;
  // Keep previously captured packets and append new ones (no clear on start). Resume
  // following the tail so incoming packets stream into view; the manual "Clear" button
  // remains the way to reset the buffer.
  autoScroll.value = true;

  // Fetch fresh port mode info before deciding whether to switch — the cached
  // info.value may be stale (polled every 5 s) or undefined (not yet loaded).
  try {
    await fetchInfo();
  } catch {
    // If the fetch fails, proceed with whatever is cached (graceful degradation)
  }

  // Guard: user may have clicked Stop while fetchInfo was in flight
  if (!running.value) return;

  if (info.value !== undefined) {
    const portNum = parseInt(portFilter.value);
    // Map port number to the corresponding rs485 key in the Info object
    const rsKeyMap: Partial<Record<number, 'rs485_1' | 'rs485_2'>> = { 1: 'rs485_1', 2: 'rs485_2' };
    const rsKey = rsKeyMap[portNum];
    if (rsKey !== undefined) {
      const currentMode = info.value[rsKey]?.port_mode;
      // The live sniffer is a display overlay over the WebSocket; it only needs the
      // serial port to be open. 'tcp_bridge' and 'passive' already have serial open,
      // so do NOT switch them. Only 'disabled' needs the serial port opened — switch
      // it to the 'passive' transport (serial open, no TCP) before connecting the WS.
      if (currentMode === 'disabled') {
        try {
          await api<void>(`ports/${portNum}/mode`, { method: 'POST', json: { mode: 'passive' } });
        } catch {
          // If the mode switch fails, proceed anyway — WS connect will fail or produce no data
        }
        // Guard: user may have clicked Stop during the api call
        if (!running.value) return;
        // Wait for firmware to complete serial port reinit before connecting
        await new Promise(resolve => setTimeout(resolve, PORT_MODE_SWITCH_DELAY_MS));
        // Guard: user may have clicked Stop during the 500 ms reinit delay
        if (!running.value) return;
        // Refresh info immediately so the sidebar reflects the updated port mode.
        fetchInfo('low').catch(() => {});
      }
    }
  }
  connectWs();
}

function stopCapture() {
  // Cancel the pending flush rAF and flush synchronously so the last buffered packets
  // are not lost when capture stops.
  if (flushRaf !== null) {
    cancelAnimationFrame(flushRaf); flushRaf = null;
  }
  flushPending();
  // Clear any pending reconnect timer before closing the WebSocket.
  if (reconnectTimer !== null) {
    clearTimeout(reconnectTimer); reconnectTimer = null;
  }
  running.value = false;
  wsStatus.value = 'disconnected';
  sendPortStop(parseInt(portFilter.value));
  ws.value?.close();
  ws.value = null;
  // Refresh info immediately so the sidebar reflects the updated port mode without
  // waiting for the next polling cycle (which runs every 5 s).
  fetchInfo('low').catch(() => {});
}

watch(portFilter, (newPort, oldPort) => {
  if (!running.value || ws.value === null || wsStatus.value !== 'connected') return;
  sendPortStop(parseInt(oldPort));
  sendPortStart(parseInt(newPort));
});

// Keep the virtual-scroll state consistent when a filter/port change resizes filteredRows.
// No scroll event fires on such changes, so scrollTop.value would otherwise stay stale and
// desync from the DOM (the browser auto-clamps el.scrollTop to the new content height).
// Re-read it on nextTick and recompute autoScroll with the same rule as onScroll().
watch([portFilter, selectedSlaves, selectedFcs, hideErrors], () => {
  nextTick(() => {
    const el = tableWrap.value;
    if (el === null) {
      return;
    }
    scrollTop.value = el.scrollTop;
    const distanceFromBottom = el.scrollHeight - el.scrollTop - el.clientHeight;
    autoScroll.value = distanceFromBottom <= rowHeight.value * 2;
  });
});

function clearLogs() {
  if (flushRaf !== null) {
    cancelAnimationFrame(flushRaf); flushRaf = null;
  }
  pending = [];
  rows.value = [];
  lastTimestampUs = 0;
  scrollTop.value = 0;
  autoScroll.value = true;
  if (tableWrap.value) tableWrap.value.scrollTop = 0;
}

onMounted(() => {
  refreshSettings();
  const el = tableWrap.value;
  if (el) {
    viewportH.value = el.clientHeight;
    // happy-dom (test env) has no ResizeObserver — guard so the integration tests do not throw.
    if (typeof ResizeObserver !== 'undefined') {
      resizeObserver = new ResizeObserver(() => {
        if (tableWrap.value) viewportH.value = tableWrap.value.clientHeight;
        measureRowHeight();
      });
      resizeObserver.observe(el);
    }
  }
});
onUnmounted(() => {
  stopCapture();
  resizeObserver?.disconnect();
  resizeObserver = null;
  if (scrollRaf !== null) {
    cancelAnimationFrame(scrollRaf); scrollRaf = null;
  }
});

const errorCount = computed(() => rows.value.filter(x => x.crc === 'ERR').length);

function byteRoleStyle(role: ByteRole) {
  switch (role) {
    case 'address': return { color: '#fff', background: 'var(--mb-master)', padding: '1px 4px', borderRadius: '3px' };
    case 'fc': return { color: '#fff', background: 'var(--mb-hex-slot)', padding: '1px 4px', borderRadius: '3px' };
    case 'subcommand': return { color: '#fff', background: 'var(--mb-hex-slot)', padding: '1px 4px', borderRadius: '3px' };
    case 'serial': return { color: '#fff', background: 'var(--mb-master)', padding: '1px 4px', borderRadius: '3px' };
    case 'crc': return { color: 'var(--mb-hex-crc)' };
    case 'data': return { color: 'var(--mb-data)' };
    case 'arbitration': return { color: 'var(--text-muted)', fontWeight: '400' };
    // FM wrapper "not real" fields — same hue but paler background
    case 'fm-addr': return { color: '#fff', background: 'color-mix(in oklch, var(--mb-master) 45%, transparent)', padding: '1px 4px', borderRadius: '3px' };
    case 'fm-ext': return { color: '#fff', background: 'color-mix(in oklch, var(--mb-hex-slot) 45%, transparent)', padding: '1px 4px', borderRadius: '3px' };
    case 'fm-subcommand': return { color: '#fff', background: 'color-mix(in oklch, var(--mb-hex-slot) 45%, transparent)', padding: '1px 4px', borderRadius: '3px' };
    default: return { color: 'var(--mb-data)' };
  }
}

// Rows filtered by port only (for facet stats)
const portRows = computed(() => {
  const p = parseInt(portFilter.value);
  return rows.value.filter(x => x.port === p);
});

// Slave stats for facet rail — arbitration packets have no real slave address, skip them
const slaveStats = computed(() => {
  const counts: Record<string, number> = {};
  for (const r of portRows.value) {
    if (r.isArbitration) continue;
    counts[r.slave] = (counts[r.slave] ?? 0) + 1;
  }
  return counts;
});

const activeSlaves = computed(() =>
  Object.keys(slaveStats.value).sort((a, b) => slaveStats.value[b] - slaveStats.value[a])
);

const maxSlaveCount = computed(() =>
  Math.max(1, ...Object.values(slaveStats.value))
);

// FC stats for facet rail
const fcStats = computed(() => {
  const counts: Record<string, number> = {};
  for (const r of portRows.value) {
    counts[r.fc_code] = (counts[r.fc_code] ?? 0) + 1;
  }
  return counts;
});

const activeFcs = computed(() =>
  Object.keys(fcStats.value).sort((a, b) => fcStats.value[b] - fcStats.value[a])
);

const maxFcCount = computed(() =>
  Math.max(1, ...Object.values(fcStats.value))
);

function fcCodeNum(hexCode: string): number {
  return parseInt(hexCode, 16);
}

const filteredRows = computed(() => {
  let r = portRows.value;
  if (hideErrors.value) r = r.filter(x => x.crc !== 'ERR');
  if (selectedSlaves.value.size > 0) r = r.filter(x => selectedSlaves.value.has(x.slave));
  if (selectedFcs.value.size > 0) r = r.filter(x => selectedFcs.value.has(x.fc_code));
  return r;
});

// Virtualization: render only the visible window of rows. We intentionally do NOT subtract
// the sticky thead height from the index math — the OVERSCAN (10 rows ≈ 290px) absorbs the
// ~35px header overlap, so there is never a blank gap.
const virtualWindow = computed(() =>
  computeVirtualWindow(scrollTop.value, viewportH.value, rowHeight.value, filteredRows.value.length, OVERSCAN),
);
const visibleRows = computed(() =>
  filteredRows.value.slice(virtualWindow.value.startIndex, virtualWindow.value.endIndex),
);
const padTop = computed(() => virtualWindow.value.padTop);
const padBottom = computed(() => virtualWindow.value.padBottom);

const sel = computed(() =>
  selected.value !== null ? filteredRows.value.find(r => r.id === selected.value) ?? null : null
);

function senderPillClass(sender: string) {
  return 'sender-pill sender-' + sender.toLowerCase();
}

function exportCsv() {
  const headers = ['#', 'Time', 'Δt', 'Sender', 'Slave', 'Function code', 'Payload (HEX)', 'Bytes', 'CRC'];
  const csvRows = [headers.join(',')];
  for (const r of filteredRows.value) {
    const row = [
      r.id,
      r.t,
      r.dt,
      r.sender,
      `0x${r.slave}`,
      `"${r.fc}"`,
      `"${r.pl}"`,
      r.bytes,
      r.crc,
    ];
    csvRows.push(row.join(','));
  }
  const blob = new Blob([csvRows.join('\n')], { type: 'text/csv;charset=utf-8;' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = `sniffer_port${portFilter.value}.csv`;
  a.click();
  URL.revokeObjectURL(url);
}
</script>

<template>
  <Layout>
    <Heading :title="t('title')" :crumbs="t('crumbs')">
      <template #default>
        <div class="sniffer-toolbar">
          <div class="sniffer-toolbar-group toolbar-stats">
            <div class="heading-stats">
              <span><b class="mono">{{ rows.length.toLocaleString() }}</b> {{ t('packets') }}</span>
              <span><b class="mono stat-err">{{ errorCount }}</b> {{ errorCount === 1 ? t('error') : t('errors') }}</span>
              <label v-if="errorCount > 0" class="hide-errors-toggle">
                <input v-model="hideErrors" type="checkbox" />
                {{ t('hide_errors') }}
              </label>
            </div>
          </div>

          <div class="sniffer-toolbar-group toolbar-capture">
            <div class="filter-ports">
              <button
                v-for="p in portOptions" :key="p"
                :class="['port-btn', { active: portFilter === p }]"
                @click="portFilter = p"
              >
Port {{ p }}
</button>
            </div>
            <Button :variant="running ? 'danger' : 'primary'" @click="running ? stopCapture() : startCapture()">
              {{ running ? t('stop') : t('start') }}
            </Button>
          </div>

          <div class="sniffer-toolbar-group toolbar-data">
            <Button variant="outline" :disabled="txDisabledForCurrentPort" @click="senderOpen = true">▶ {{ t('send_packet') }}</Button>
            <Button variant="outline" @click="clearLogs()">{{ t('clear') }}</Button>
            <Button variant="outline" :disabled="filteredRows.length === 0" @click="exportCsv()">{{ t('export_csv') }}</Button>
          </div>
        </div>
      </template>
    </Heading>

    <!-- Main area: facet rail + log table (position: relative for popup anchor) -->
    <div class="sniffer-content-wrap">
    <div class="sniffer-main">
      <!-- Facet rail -->
      <aside class="facet-rail">
        <!-- Slave ID section -->
        <div class="facet-section">
          <div class="facet-section-header">
            <div>
              <div class="facet-section-title">Slave ID</div>
              <div class="facet-section-hint">{{ activeSlaves.length }} seen · {{ selectedSlaves.size || 'all' }} selected</div>
            </div>
            <button class="facet-clear" :style="{ visibility: selectedSlaves.size > 0 ? 'visible' : 'hidden' }" @click="selectedSlaves = new Set()">clear</button>
          </div>
          <button
            v-for="slave in activeSlaves" :key="slave"
            class="facet-row"
            :data-on="selectedSlaves.has(slave) ? 'true' : 'false'"
            @click="selectedSlaves = toggleSet(selectedSlaves, slave)"
          >
            <span class="facet-check">
              <CheckmarkIcon v-if="selectedSlaves.has(slave)" />
            </span>
            <span class="facet-id-label">
              <span class="mono facet-idMono">{{ slave }}</span>
              <span class="facet-label facet-labelSmall mono muted">
                {{ SLAVE_NAMES[parseInt(slave, 16)] ?? (isNaN(parseInt(slave, 16)) ? slave : `0x${slave} · ${parseInt(slave, 16)}`) }}
              </span>
            </span>
            <span class="facet-count">{{ slaveStats[slave] }}</span>
            <span class="facet-bar"><span :style="{ width: `${(slaveStats[slave] / maxSlaveCount) * 100}%` }" /></span>
          </button>
        </div>

        <!-- Function code section -->
        <div class="facet-section">
          <div class="facet-section-header">
            <div>
              <div class="facet-section-title">Function code</div>
              <div class="facet-section-hint">{{ activeFcs.length }} seen · {{ selectedFcs.size || 'all' }} selected</div>
            </div>
            <button class="facet-clear" :style="{ visibility: selectedFcs.size > 0 ? 'visible' : 'hidden' }" @click="selectedFcs = new Set()">clear</button>
          </div>
          <button
            v-for="code in activeFcs" :key="code"
            class="facet-row"
            :data-on="selectedFcs.has(code) ? 'true' : 'false'"
            @click="selectedFcs = toggleSet(selectedFcs, code)"
          >
            <span class="facet-check">
              <CheckmarkIcon v-if="selectedFcs.has(code)" />
            </span>
            <span class="facet-id-label">
              <span class="mono facet-idMono">{{ code }}</span>
              <span class="facet-label">
                {{ FC_NAMES[fcCodeNum(code)] || 'Unknown' }}
              </span>
            </span>
            <span class="facet-count">{{ fcStats[code] }}</span>
            <span class="facet-bar"><span :style="{ width: `${(fcStats[code] / maxFcCount) * 100}%` }" /></span>
          </button>
        </div>
      </aside>

      <!-- Log table -->
      <div class="sniffer-body">
      <div ref="tableWrap" class="sniffer-table-wrap" @scroll="onScroll">
        <table class="sniffer-table">
          <thead>
            <tr>
              <th class="col-id">#</th>
              <th class="col-time">Time</th>
              <th class="col-dt">&Delta;t</th>
              <th class="col-sender">{{ t('col_sender') }}</th>
              <th class="col-slave">Slave</th>
              <th class="col-fc">Function</th>
              <th class="col-payload">Payload</th>
              <th class="col-bytes">Bytes</th>
              <th class="col-crc">CRC</th>
            </tr>
          </thead>
          <tbody>
            <tr v-if="padTop > 0" class="sniff-spacer" aria-hidden="true"><td :colspan="9" :style="{ height: padTop + 'px' }"></td></tr>
            <tr
              v-for="r in visibleRows" :key="r.id"
              :class="['sniff-row', { selected: selected === r.id, 'err-row': r.crc === 'ERR' && !r.isArbitration }]"
              @click="selected = r.id"
            >
              <td class="mono muted">{{ r.id }}</td>
              <td class="mono">{{ r.t }}</td>
              <td class="mono muted">{{ r.dt }}</td>
              <td><span :class="senderPillClass(r.sender)">{{ r.sender }}</span></td>
              <td class="mono col-slave-cell" :title="r.isArbitration ? undefined : (SLAVE_NAMES[parseInt(r.slave, 16)] ? `0x${r.slave} · ${SLAVE_NAMES[parseInt(r.slave, 16)]}` : `0x${r.slave} (${parseInt(r.slave, 16)})`)">
                <span v-if="r.isArbitration" class="muted">—</span>
                <span v-else>0x{{ r.slave }}</span>
              </td>
              <td class="mono fc-cell" :title="r.tooltip || undefined">{{ r.fc }}</td>
              <td>
                <span class="hex-payload">
                  <span
                    v-for="(b, i) in r.bytes_arr" :key="i"
                    class="hex-byte"
                    :style="byteRoleStyle(r.byte_roles[i] ?? 'unknown')"
                  >{{ b }}</span>
                </span>
              </td>
              <td class="mono muted">{{ r.bytes }}</td>
              <td>
                <span v-if="r.crc === 'ERR'" class="crc-err mono">ERR</span>
                <span v-else-if="r.crc === 'N/A'" class="muted mono">—</span>
                <span v-else class="crc-ok mono">OK</span>
              </td>
            </tr>
            <tr v-if="padBottom > 0" class="sniff-spacer" aria-hidden="true"><td :colspan="9" :style="{ height: padBottom + 'px' }"></td></tr>
          </tbody>
        </table>
      </div>

      <!-- Detail panel -->
      <PacketDecoder v-if="sel" :packet="sel" />
      </div><!-- /sniffer-body -->
    </div><!-- /sniffer-main -->

    <!-- Floating send packet popup — anchored to sniffer-content-wrap -->
    <PacketSenderPopup
      v-if="senderOpen"
      :port-num="portFilter"
      :tx-disabled="txDisabledForCurrentPort"
      @close="senderOpen = false"
    />
    </div><!-- /sniffer-content-wrap -->
  </Layout>
</template>

<style scoped>
/* Grouped toolbar: stats | capture controls | data actions */
.sniffer-toolbar {
  display: flex;
  flex-wrap: wrap;
  gap: 8px 20px;
  align-items: center;
  width: 100%;
}

.sniffer-toolbar-group {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
}

.toolbar-stats { margin-right: auto; }

@media (max-width: 560px) {
  .sniffer-toolbar { gap: 8px; }
  .toolbar-stats { margin-right: 0; width: 100%; }
  .sniffer-toolbar-group { width: 100%; }
  .toolbar-capture { justify-content: space-between; }
  .toolbar-data { justify-content: flex-start; }
  .toolbar-capture > button[class*="port"] { flex: 0 0 auto; }
}

/* Heading stats counter */
.heading-stats {
  display: flex;
  gap: 12px;
  font-size: 12px;
  color: var(--text-muted);
  white-space: nowrap;
  align-items: center;
}

.heading-stats b {
  font-weight: 500;
  color: var(--text-color);
}

.stat-err {
  color: var(--mb-err) !important;
}

.hide-errors-toggle {
  display: flex;
  align-items: center;
  gap: 5px;
  cursor: pointer;
  font-size: 12px;
  color: var(--text-muted);
  user-select: none;
}

.hide-errors-toggle input {
  cursor: pointer;
  accent-color: var(--primary-color);
}

/* Port selector in heading */
.filter-ports {
  display: flex;
  gap: 4px;
}

.port-btn {
  height: 32px;
  padding: 0 12px;
  font-size: 13px;
  background: transparent;
  border: 1px solid var(--border-color);
  color: var(--text-secondary);
  border-radius: var(--r-sm);
  cursor: pointer;
  transition: background 0.12s, border-color 0.12s, color 0.12s;
}

.port-btn:hover {
  background: var(--bg-surface-subtle);
  border-color: var(--border-strong);
}

.port-btn.active {
  background: var(--primary-color);
  border-color: var(--primary-color);
  color: #fff;
}

/* Wrapper providing the positioning context for the floating popup */
.sniffer-content-wrap {
  flex: 1;
  display: flex;
  flex-direction: column;
  min-height: 0;
  position: relative;
}

/* Sniffer main layout */
.sniffer-main {
  flex: 1;
  display: flex;
  min-height: 0;
}

/* Facet rail */
.facet-rail {
  width: 280px;
  flex-shrink: 0;
  background: var(--bg-surface-subtle);
  border-right: 1px solid var(--border-color);
  overflow-y: auto;
  padding: 6px 0 14px;
}

.facet-section {
  padding: 14px 0 4px;
}

.facet-section-header {
  padding: 0 16px 8px;
  display: flex;
  align-items: baseline;
  justify-content: space-between;
}

.facet-section-title {
  font-size: 10.5px;
  text-transform: uppercase;
  letter-spacing: 0.1em;
  color: var(--text-muted);
  font-weight: 600;
}

.facet-section-hint {
  font-size: 10.5px;
  color: var(--text-muted);
  margin-top: 2px;
}

.facet-clear {
  border: 0;
  background: transparent;
  color: var(--text-muted);
  font-size: 11px;
  cursor: pointer;
  padding: 0;
}

.facet-row {
  width: 100%;
  appearance: none;
  border-top: 0;
  border-right: 0;
  border-bottom: 0;
  border-left: 2px solid transparent;
  border-radius: 0;
  outline: none;
  background: transparent;
  display: grid;
  grid-template-columns: 16px 1fr auto 44px;
  align-items: center;
  gap: 10px;
  padding: 6px 16px;
  cursor: pointer;
  font-size: 12px;
  color: var(--text-secondary);
  text-align: left;
}

.facet-row:hover {
  border-top: 0;
  border-right: 0;
  border-bottom: 0;
  background: var(--bg-surface);
  color: var(--text-color);
}

.facet-row:focus-visible {
  outline: none;
}

.facet-row[data-on="true"] {
  background: color-mix(in oklch, var(--primary-color) 6%, white);
  color: var(--text-color);
  border-left-color: var(--primary-color);
}

.facet-row[data-on="true"] .facet-count {
  color: var(--primary-color);
  font-weight: 600;
}

.facet-check {
  width: 14px;
  height: 14px;
  border-radius: 3px;
  border: 1.2px solid var(--border-strong);
  background: #fff;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.facet-row[data-on="true"] .facet-check {
  background: var(--primary-color);
  border-color: var(--primary-color);
}

.facet-id-label {
  display: flex;
  flex-direction: column;
  min-width: 0;
  gap: 1px;
}

.facet-label {
  min-width: 0;
  overflow: hidden;
  display: -webkit-box;
  -webkit-line-clamp: 2;
  -webkit-box-orient: vertical;
  font-size: 12px;
}

.facet-idMono {
  font-weight: 600;
}

.facet-labelSmall {
  font-size: 11px;
}

.col-slave-cell {
  font-weight: 500;
}

.facet-count {
  font-family: var(--font-mono);
  font-size: 11px;
  color: var(--text-muted);
  text-align: right;
}

.facet-bar {
  height: 3px;
  border-radius: 2px;
  background: color-mix(in oklch, var(--border-color) 60%, white);
  overflow: hidden;
}

.facet-bar span {
  display: block;
  height: 100%;
  background: color-mix(in oklch, var(--primary-color) 65%, white);
}

.facet-row[data-on="true"] .facet-bar span {
  background: var(--primary-color);
}

/* Sniffer body */
.sniffer-body {
  flex: 1;
  display: flex;
  flex-direction: column;
  min-height: 0;
  background: var(--bg-surface);
}

.sniffer-table-wrap {
  flex: 1;
  overflow: auto;
  /* Right-edge fade hints there is content to scroll horizontally — only when overflowing */
  background:
    linear-gradient(to right, var(--bg-surface) 30%, rgba(255, 255, 255, 0)) left center,
    linear-gradient(to right, rgba(255, 255, 255, 0), var(--bg-surface) 70%) right center,
    radial-gradient(farthest-side at 0 50%, rgba(15, 23, 42, 0.10), rgba(0, 0, 0, 0)) left center,
    radial-gradient(farthest-side at 100% 50%, rgba(15, 23, 42, 0.10), rgba(0, 0, 0, 0)) right center;
  background-repeat: no-repeat;
  background-size: 24px 100%, 24px 100%, 12px 100%, 12px 100%;
  background-attachment: local, local, scroll, scroll;
}

/* Table */
.sniffer-table {
  width: 100%;
  border-collapse: collapse;
  font-size: 12.5px;
  table-layout: fixed;
}

.sniffer-table thead {
  position: sticky;
  top: 0;
  z-index: 1;
}

.sniffer-table th {
  background: var(--bg-surface-subtle);
  font-size: 10.5px;
  text-transform: uppercase;
  letter-spacing: 0.06em;
  color: var(--text-muted);
  font-weight: 500;
  padding: 7px 12px;
  text-align: left;
  border-bottom: 1px solid var(--border-color);
  border-right: none;
  border-left: none;
  white-space: nowrap;
}

.sniffer-table td {
  padding: 6px 12px;
  border-bottom: 1px solid var(--border-color);
  border-right: none;
  border-left: none;
  white-space: nowrap;
}

.sniffer-table tbody tr {
  cursor: pointer;
  transition: background 0.08s;
}

.sniffer-table tbody tr:hover {
  background: var(--bg-surface-subtle);
}

.sniffer-table tbody tr.selected {
  background: color-mix(in oklch, var(--primary-color) 6%, var(--bg-surface));
}

.sniffer-table tbody tr.err-row {
  background: color-mix(in oklch, var(--mb-err) 4%, var(--bg-surface));
}

.sniffer-table tbody tr.err-row.selected {
  background: color-mix(in oklch, var(--mb-err) 8%, var(--bg-surface));
}

/* Virtualization spacer rows: occupy the height of the off-screen rows above/below the window */
.sniffer-table tbody tr.sniff-spacer {
  cursor: default;
}
.sniffer-table tbody tr.sniff-spacer:hover {
  background: transparent;
}
.sniffer-table tbody tr.sniff-spacer td {
  padding: 0;
  border: 0;
}

.col-id { width: 56px; }
.col-time { width: 100px; }
.col-dt { width: 68px; }
.col-sender { width: 76px; }
.col-slave { width: 54px; }
.col-fc { width: 240px; }
.col-payload { max-width: 0; width: 100%; }
.col-bytes { width: 60px; }
.col-crc { width: 50px; }

.fc-cell {
  font-size: 12px;
  color: var(--text-secondary);
}

/* Sender pill */
.sender-pill {
  display: inline-block;
  font-family: var(--font-mono);
  font-size: 11px;
  font-weight: 600;
  padding: 1px 7px;
  border-radius: 4px;
  border: 1px solid;
  letter-spacing: 0.04em;
}

.sender-master {
  color: var(--mb-master);
  background: color-mix(in oklch, var(--mb-master) 8%, white);
  border-color: color-mix(in oklch, var(--mb-master) 25%, white);
}

.sender-slave {
  color: var(--mb-slave);
  background: color-mix(in oklch, var(--mb-slave) 6%, white);
  border-color: color-mix(in oklch, var(--mb-slave) 22%, white);
}

.sender-err {
  color: var(--mb-err);
  background: color-mix(in oklch, var(--mb-err) 8%, white);
  border-color: color-mix(in oklch, var(--mb-err) 25%, white);
}

.sender-timeout {
  color: var(--text-muted);
  background: color-mix(in oklch, var(--text-muted) 6%, var(--bg-surface));
  border-color: color-mix(in oklch, var(--text-muted) 20%, var(--bg-surface));
}

/* Hex payload */
.hex-payload {
  font-family: var(--font-mono);
  font-size: 12px;
  letter-spacing: 0.02em;
  font-weight: 500;
  display: block;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  max-width: 100%;
}

.hex-byte {
  margin-right: 4px;
  display: inline-block;
}

/* CRC */
.crc-err {
  color: var(--mb-err);
  font-weight: 600;
  font-size: 11px;
}

.crc-ok {
  color: var(--mb-ok);
  font-weight: 500;
  font-size: 11px;
}

/* Responsive */
@media (max-width: 680px) {
  .sniffer-main {
    flex-direction: column;
  }
  .facet-rail {
    width: 100%;
    border-right: 0;
    border-bottom: 1px solid var(--border-color);
    max-height: 220px;
  }
  /* Hide low-priority columns on mobile; horizontal scroll still works for the rest */
  .sniffer-table th.col-id,
  .sniffer-table td:nth-child(1),
  .sniffer-table th.col-dt,
  .sniffer-table td:nth-child(3),
  .sniffer-table th.col-bytes,
  .sniffer-table td:nth-child(8),
  .sniffer-table th.col-crc,
  .sniffer-table td:nth-child(9) {
    display: none;
  }
}
</style>

<i18n>
{
  "en": {
    "title": "Sniffer",
    "crumbs": "Modbus packet capture",
    "start": "Start",
    "stop": "Stop",
    "clear": "Clear",
    "errors_only": "Errors only",
    "packets": "packets",
    "error": "error",
    "errors": "errors",
    "col_time": "Time",
    "col_sender": "Sender",
    "col_fc": "Function code",
    "col_payload": "Payload (HEX)",
    "col_bytes": "Bytes",
    "parsed": "Parsed",
    "start_addr": "Starting address",
    "quantity": "Quantity",
    "size": "Size",
    "raw_bytes": "Raw bytes",
    "export_csv": "Export CSV",
    "hide_errors": "Hide errors",
    "send_packet": "Send packet"
  },
  "ru": {
    "title": "Sniffer",
    "crumbs": "Перехват пакетов Modbus",
    "start": "Старт",
    "stop": "Стоп",
    "clear": "Очистить",
    "errors_only": "Только ошибки",
    "packets": "пакетов",
    "error": "ошибка",
    "errors": "ошибок",
    "col_time": "Время",
    "col_sender": "Отправитель",
    "col_fc": "Код функции",
    "col_payload": "Payload (HEX)",
    "col_bytes": "Байт",
    "parsed": "Разобрано",
    "start_addr": "Начальный адрес",
    "quantity": "Количество",
    "size": "Размер",
    "raw_bytes": "Сырые байты",
    "export_csv": "Экспорт CSV",
    "hide_errors": "Скрывать ошибки",
    "send_packet": "Отправить пакет"
  },
  "kk": {
    "title": "Sniffer",
    "crumbs": "Modbus пакеттерін ұстау",
    "start": "Бастау",
    "stop": "Тоқтату",
    "clear": "Тазалау",
    "errors_only": "Тек қателер",
    "packets": "пакет",
    "error": "қате",
    "errors": "қате",
    "col_time": "Уақыт",
    "col_sender": "Жіберуші",
    "col_fc": "Функция коды",
    "col_payload": "Payload (HEX)",
    "col_bytes": "Байт",
    "parsed": "Талданған",
    "start_addr": "Бастапқы адрес",
    "quantity": "Саны",
    "size": "Өлшемі",
    "raw_bytes": "Шикі байттар",
    "export_csv": "CSV жүктеу",
    "hide_errors": "Қателерді жасыру",
    "send_packet": "Пакет жіберу"
  },
  "it": {
    "title": "Sniffer",
    "crumbs": "Cattura pacchetti Modbus",
    "start": "Avvia",
    "stop": "Ferma",
    "clear": "Cancella",
    "errors_only": "Solo errori",
    "packets": "pacchetti",
    "error": "errore",
    "errors": "errori",
    "col_time": "Ora",
    "col_sender": "Mittente",
    "col_fc": "Codice funzione",
    "col_payload": "Payload (HEX)",
    "col_bytes": "Byte",
    "parsed": "Analizzato",
    "start_addr": "Indirizzo iniziale",
    "quantity": "Quantità",
    "size": "Dimensione",
    "raw_bytes": "Byte grezzi",
    "export_csv": "Esporta CSV",
    "hide_errors": "Nascondi errori",
    "send_packet": "Invia pacchetto"
  },
  "de": {
    "title": "Sniffer",
    "crumbs": "Modbus-Paketerfassung",
    "start": "Start",
    "stop": "Stopp",
    "clear": "Löschen",
    "errors_only": "Nur Fehler",
    "packets": "Pakete",
    "error": "Fehler",
    "errors": "Fehler",
    "col_time": "Zeit",
    "col_sender": "Absender",
    "col_fc": "Funktionscode",
    "col_payload": "Payload (HEX)",
    "col_bytes": "Bytes",
    "parsed": "Analysiert",
    "start_addr": "Startadresse",
    "quantity": "Anzahl",
    "size": "Größe",
    "raw_bytes": "Rohbytes",
    "export_csv": "CSV exportieren",
    "hide_errors": "Fehler ausblenden",
    "send_packet": "Paket senden"
  }
}
</i18n>
