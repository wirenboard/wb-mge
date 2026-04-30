<script setup lang="ts">
import { ref, computed, watch, onUnmounted, nextTick } from 'vue';
import { useI18n } from 'vue-i18n';
import Button from '@/components/Button.vue';
import Heading from '@/components/Heading.vue';
import Layout from '@/components/Layout.vue';

const { t } = useI18n();

type SniffRow = {
  id: number
  port: number
  timestamp_us: number
  dir: 'MASTER' | 'SLAVE' | 'TIMEOUT' | 'ERR'
  slave: string
  fc: string
  fc_code: string
  pl: string
  bytes_arr: string[]
  bytes: number
  crc: 'OK' | 'ERR'
  t: string
  dt: string
  tooltip: string
}

const rows = ref<SniffRow[]>([]);
const running = ref(false);
const ws = ref<WebSocket | null>(null);
let lastTimestampUs = 0;
const wsStatus = ref<'connected' | 'disconnected' | 'reconnecting'>('disconnected');

const tableWrap = ref<HTMLElement | null>(null);
const selected = ref<number | null>(null);
const portFilter = ref('1');
const portOptions = ['1', '2'];
const selectedSlaves = ref<Set<string>>(new Set());
const selectedFcs = ref<Set<string>>(new Set());

const FC_NAMES: Record<number, string> = {
  1: 'Read Coils',
  2: 'Read Discrete Inputs',
  3: 'Read Holding Regs',
  4: 'Read Input Regs',
  5: 'Write Single Coil',
  6: 'Write Single Reg',
  15: 'Write Multiple Coils',
  16: 'Write Multiple Regs',
  70: 'Fast Modbus',
  255: 'FM Arbitration',
}

const FC_KIND: Record<number, 'read' | 'write'> = {
  1: 'read', 2: 'read', 3: 'read', 4: 'read',
  5: 'write', 6: 'write', 15: 'write', 16: 'write',
}

const SLAVE_NAMES: Record<number, string> = {
  253: 'Fast Modbus broadcast',
  255: 'Bus arbitration',
}

function formatTimestamp(us: number): string {
  const ms = Math.floor(us / 1000)
  const d = new Date(ms)
  const hh = d.getHours().toString().padStart(2, '0')
  const mm = d.getMinutes().toString().padStart(2, '0')
  const ss = d.getSeconds().toString().padStart(2, '0')
  const mmm = d.getMilliseconds().toString().padStart(3, '0')
  return `${hh}:${mm}:${ss}.${mmm}`
}

function formatDt(us: number, prevUs: number): string {
  if (prevUs === 0) return '—'
  const diff = Math.round((us - prevUs) / 1000)
  return `+${diff} ms`
}

function hexToPayloadString(raw: string): string {
  return raw.match(/.{1,2}/g)?.join(' ') ?? raw
}

const FAST_MODBUS_SUBCMDS: Record<number, string> = {
  0x01: 'FM Scan Start',
  0x02: 'FM Scan Continue',
  0x03: 'FM Scan Response',
  0x04: 'FM Scan End',
  0x08: 'FM Cmd Send',
  0x09: 'FM Cmd Response',
  0x10: 'FM Event Request',
  0x11: 'FM Event Transfer',
  0x12: 'FM Event Confirm',
  0x18: 'FM Event Config',
}

const FAST_MODBUS_TOOLTIPS: Record<number, string> = {
  0x01: 'Fast Modbus Scan Start: master resets scan status on all unscanned devices. Devices participate in arbitration; lowest serial number wins.',
  0x02: 'Fast Modbus Scan Continue: master continues scanning. Devices not yet scanned participate in arbitration.',
  0x03: 'Fast Modbus Scan Response: device responds with its serial number and data during scan arbitration.',
  0x04: 'Fast Modbus Scan End: master signals end of scan. Only devices that already responded may reply.',
  0x08: 'Fast Modbus Cmd Send: master sends a standard Modbus command via Fast Modbus addressing.',
  0x09: 'Fast Modbus Cmd Response: device responds to a Fast Modbus standard command.',
  0x10: 'Fast Modbus Event Request: master polls all devices for pending events. Devices with events participate in arbitration.',
  0x11: 'Fast Modbus Event Transfer: winning device sends its pending event data to master.',
  0x12: 'Fast Modbus Event Confirm: master acknowledges receipt of event. Ends the event polling round.',
  0x18: 'Fast Modbus Event Config: master configures event reporting settings on device.',
}

const FC_TOOLTIPS: Record<number, string> = {
  1: 'Read Coils (FC01): read 1–2000 discrete output coils. Request: addr(2) + count(2). Response: N bytes of coil values.',
  2: 'Read Discrete Inputs (FC02): read 1–2000 discrete input bits. Request: addr(2) + count(2). Response: N bytes of input values.',
  3: 'Read Holding Regs (FC03): read 1–125 holding registers. Request: addr(2) + count(2). Response: count×2 bytes of register values.',
  4: 'Read Input Regs (FC04): read 1–125 input registers. Request: addr(2) + count(2). Response: count×2 bytes of register values.',
  5: 'Write Single Coil (FC05): write one coil ON (0xFF00) or OFF (0x0000). Request: addr(2) + value(2). Response: echo of request.',
  6: 'Write Single Reg (FC06): write one holding register. Request: addr(2) + value(2). Response: echo of request.',
  15: 'Write Multiple Coils (FC15): write N coils. Request: addr(2) + count(2) + byte_count(1) + data. Response: addr+count.',
  16: 'Write Multiple Regs (FC16): write N holding registers. Request: addr(2) + count(2) + byte_count(1) + data. Response: addr+count.',
}

function parsePacket(msg: any): SniffRow | null {
  if (typeof msg.id !== 'number') return null
  if (msg.type === 'timeout') {
    const fcName = FC_NAMES[msg.function] ?? `FC${msg.function}`
    const slave = msg.slave_id.toString(16).padStart(2, '0').toUpperCase()
    const dt = formatDt(msg.timestamp_us, lastTimestampUs)
    lastTimestampUs = msg.timestamp_us
    return {
      id: msg.id,
      port: msg.port,
      timestamp_us: msg.timestamp_us,
      dir: 'TIMEOUT',
      slave,
      fc: fcName,
      fc_code: msg.function.toString(16).padStart(2, '0').toUpperCase(),
      pl: '',
      bytes_arr: [],
      bytes: 0,
      crc: 'ERR',
      t: formatTimestamp(msg.timestamp_us),
      dt,
      tooltip: `No response from slave 0x${slave} within timeout`,
    }
  }
  if (msg.type === 'packet') {
    const fcName = FC_NAMES[msg.function] ?? `FC${msg.function}`
    const slave = msg.slave_id.toString(16).padStart(2, '0').toUpperCase()
    const dir = msg.dir === 'master' ? 'MASTER' : 'SLAVE'
    const crc = msg.crc_valid ? 'OK' : 'ERR'
    const pl = hexToPayloadString(msg.raw)
    const dt = formatDt(msg.timestamp_us, lastTimestampUs)
    lastTimestampUs = msg.timestamp_us

    /* Build FC display: no numeric prefix for named functions */
    let fcDisplay = fcName
    let tooltip = FC_TOOLTIPS[msg.function] ?? ''
    if (msg.function === 0x46 && msg.raw && msg.raw.length >= 6) {
      const sub = parseInt(msg.raw.slice(4, 6), 16)
      const subName = FAST_MODBUS_SUBCMDS[sub] ?? `FC${sub.toString(16).padStart(2, '0').toUpperCase()}`
      fcDisplay = `${subName}`
      tooltip = FAST_MODBUS_TOOLTIPS[sub] ?? `Fast Modbus subcommand 0x${sub.toString(16).padStart(2, '0').toUpperCase()}`
    }
    if (msg.slave_id === 0xFF && msg.function === 0xFF) {
      tooltip = 'Fast Modbus Bus Arbitration: all devices with data transmit simultaneously. The result is a wired-AND of all responses (0xFF = no winner, or collision). Master reads this to determine if any device won arbitration.'
    }

    return {
      id: msg.id,
      port: msg.port,
      timestamp_us: msg.timestamp_us,
      dir: crc === 'ERR' ? 'ERR' : dir,
      slave,
      fc: fcDisplay,
      fc_code: msg.function.toString(16).padStart(2, '0').toUpperCase(),
      pl,
      bytes_arr: hexToPayloadString(msg.raw).split(' '),
      bytes: msg.size,
      crc,
      t: formatTimestamp(msg.timestamp_us),
      dt,
      tooltip,
    }
  }
  return null
}

function getWsUrl(): string {
  const proto = location.protocol === 'https:' ? 'wss' : 'ws'
  return `${proto}://${location.host}/sniffer/ws`
}

function sendPortStart(port: number) {
  ws.value?.send(JSON.stringify({ cmd: 'start', port }))
}

function sendPortStop(port: number) {
  ws.value?.send(JSON.stringify({ cmd: 'stop', port }))
}

function connectWs() {
  lastTimestampUs = 0
  ws.value = new WebSocket(getWsUrl())
  ws.value.onopen = () => {
    wsStatus.value = 'connected'
    sendPortStart(parseInt(portFilter.value))
  }
  ws.value.onmessage = (ev) => {
    try {
      const msg = JSON.parse(ev.data as string)
      const row = parsePacket(msg)
      if (row) {
        rows.value.push(row)
        if (rows.value.length >= 1000) {
          stopCapture()
          return
        }
        nextTick(() => {
          if (tableWrap.value) tableWrap.value.scrollTop = tableWrap.value.scrollHeight
        })
      }
    } catch (e) {
      console.warn('sniffer: failed to parse WS message', e)
    }
  }
  ws.value.onclose = () => {
    ws.value = null
    if (running.value) {
      wsStatus.value = 'reconnecting'
      setTimeout(connectWs, 2000)
    } else {
      wsStatus.value = 'disconnected'
    }
  }
}

function startCapture() {
  clearLogs()
  running.value = true
  connectWs()
}

function stopCapture() {
  running.value = false
  wsStatus.value = 'disconnected'
  sendPortStop(parseInt(portFilter.value))
  ws.value?.close()
  ws.value = null
}

watch(portFilter, (newPort, oldPort) => {
  if (!running.value || ws.value === null || wsStatus.value !== 'connected') return
  sendPortStop(parseInt(oldPort))
  sendPortStart(parseInt(newPort))
})

function clearLogs() {
  rows.value = []
  lastTimestampUs = 0
}

onUnmounted(() => stopCapture())

const errorCount = computed(() => rows.value.filter(x => x.crc === 'ERR').length);

// Rows filtered by port only (for facet stats)
const portRows = computed(() => {
  const p = parseInt(portFilter.value)
  return rows.value.filter(x => x.port === p)
})

// Slave stats for facet rail
const slaveStats = computed(() => {
  const counts: Record<string, number> = {}
  for (const r of portRows.value) {
    counts[r.slave] = (counts[r.slave] ?? 0) + 1
  }
  return counts
})

const activeSlaves = computed(() =>
  Object.keys(slaveStats.value).sort((a, b) => slaveStats.value[b] - slaveStats.value[a])
)

const maxSlaveCount = computed(() =>
  Math.max(1, ...Object.values(slaveStats.value))
)

// FC stats for facet rail
const fcStats = computed(() => {
  const counts: Record<string, number> = {}
  for (const r of portRows.value) {
    counts[r.fc_code] = (counts[r.fc_code] ?? 0) + 1
  }
  return counts
})

const activeFcs = computed(() =>
  Object.keys(fcStats.value).sort((a, b) => fcStats.value[b] - fcStats.value[a])
)

const maxFcCount = computed(() =>
  Math.max(1, ...Object.values(fcStats.value))
)

function fcCodeNum(hexCode: string): number {
  return parseInt(hexCode, 16)
}

function toggleSet(set: Set<string>, val: string): Set<string> {
  const n = new Set(set)
  n.has(val) ? n.delete(val) : n.add(val)
  return n
}

function selectFcKind(kind: 'read' | 'write') {
  const codes = activeFcs.value.filter(code => FC_KIND[fcCodeNum(code)] === kind)
  selectedFcs.value = new Set(codes)
}

const filteredRows = computed(() => {
  let r = portRows.value
  if (selectedSlaves.value.size > 0) r = r.filter(x => selectedSlaves.value.has(x.slave))
  if (selectedFcs.value.size > 0) r = r.filter(x => selectedFcs.value.has(x.fc_code))
  return r
});

const sel = computed(() =>
  selected.value !== null ? filteredRows.value.find(r => r.id === selected.value) ?? null : null
);

function dirPillClass(dir: string) {
  return 'dir-pill dir-' + dir.toLowerCase();
}

function hexByteStyle(byte: string, index: number, arr: string[]) {
  const last = arr.length - 1;
  const realEnd = arr[last] === '\u2026' ? last - 1 : last;

  if (byte === '??') return { color: '#fff', background: 'var(--mb-err)', padding: '1px 4px', borderRadius: '3px' };
  if (index === 0) return { color: '#fff', background: 'var(--mb-master)', padding: '1px 4px', borderRadius: '3px' };
  if (index === 1) return { color: '#fff', background: 'var(--mb-hex-slot)', padding: '1px 4px', borderRadius: '3px' };
  if (byte === '\u2026') return { color: 'var(--text-muted)' };
  if (index === realEnd || index === realEnd - 1) return { color: 'var(--mb-hex-crc)' };
  return { color: 'var(--mb-data)' };
}

function directionLabel(dir: string, slave: string) {
  if (dir === 'MASTER') return `Master \u2192 Slave 0x${slave}`;
  if (dir === 'SLAVE') return `Slave 0x${slave} \u2192 Master`;
  return 'Error response';
}

function exportCsv() {
  const headers = ['#', 'Time', 'Δt', 'Dir', 'Slave', 'Function code', 'Payload (HEX)', 'Bytes', 'CRC']
  const csvRows = [headers.join(',')]
  for (const r of filteredRows.value) {
    const row = [
      r.id,
      r.t,
      r.dt,
      r.dir,
      `0x${r.slave}`,
      `"${r.fc}"`,
      `"${r.pl}"`,
      r.bytes,
      r.crc,
    ]
    csvRows.push(row.join(','))
  }
  const blob = new Blob([csvRows.join('\n')], { type: 'text/csv;charset=utf-8;' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = `sniffer_port${portFilter.value}.csv`
  a.click()
  URL.revokeObjectURL(url)
}
</script>

<template>
  <Layout>
    <Heading :title="t('title')" :crumbs="t('crumbs')">
      <template #default>
        <div class="heading-stats">
          <span><b class="mono">{{ rows.length.toLocaleString() }}</b> {{ t('packets') }}</span>
          <span><b class="mono stat-err">{{ errorCount }}</b> {{ errorCount === 1 ? t('error') : t('errors') }}</span>
        </div>
        <span class="heading-sep" />
        <div class="filter-ports">
          <button
            v-for="p in portOptions" :key="p"
            :class="['port-btn', { active: portFilter === p }]"
            @click="portFilter = p"
          >Port {{ p }}</button>
        </div>
        <span class="heading-sep" />
        <Button variant="outline" @click="exportCsv()" :disabled="filteredRows.length === 0">{{ t('export_csv') }}</Button>
        <span class="heading-sep" />
        <Button variant="outline" @click="clearLogs()">{{ t('clear') }}</Button>
        <Button :variant="running ? 'danger' : 'primary'" @click="running ? stopCapture() : startCapture()">
          {{ running ? t('stop') : t('start') }}
        </Button>
      </template>
    </Heading>

    <!-- Main area: facet rail + log table -->
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
              <svg v-if="selectedSlaves.has(slave)" width="10" height="10" viewBox="0 0 10 10" fill="none">
                <path d="M1.5 5l2.3 2.3L8.5 2.5" stroke="#fff" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"/>
              </svg>
            </span>
            <span class="mono" style="font-weight:600">{{ slave }}</span>
            <span class="facet-label mono muted" style="font-size:11px">
              {{ SLAVE_NAMES[parseInt(slave, 16)] ?? `0x${slave} · ${parseInt(slave, 16)}` }}
            </span>
            <span class="facet-count">{{ slaveStats[slave] }}</span>
            <span class="facet-bar"><span :style="{ width: `${(slaveStats[slave] / maxSlaveCount) * 100}%` }"/></span>
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
              <svg v-if="selectedFcs.has(code)" width="10" height="10" viewBox="0 0 10 10" fill="none">
                <path d="M1.5 5l2.3 2.3L8.5 2.5" stroke="#fff" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"/>
              </svg>
            </span>
            <span class="mono" style="font-weight:600">{{ code }}</span>
            <span class="facet-label">
              {{ FC_NAMES[fcCodeNum(code)] || 'Unknown' }}
              <span v-if="FC_KIND[fcCodeNum(code)]" :class="`facet-kind facet-kind-${FC_KIND[fcCodeNum(code)]}`">{{ FC_KIND[fcCodeNum(code)] }}</span>
            </span>
            <span class="facet-count">{{ fcStats[code] }}</span>
            <span class="facet-bar"><span :style="{ width: `${(fcStats[code] / maxFcCount) * 100}%` }"/></span>
          </button>
        </div>
      </aside>

      <!-- Log table -->
      <div class="sniffer-body">
      <div class="sniffer-table-wrap" ref="tableWrap">
        <table class="sniffer-table">
          <thead>
            <tr>
              <th class="col-id">#</th>
              <th class="col-time">{{ t('col_time') }}</th>
              <th class="col-dt">&Delta;t</th>
              <th class="col-sender">{{ t('col_dir') }}</th>
              <th class="col-slave">Slave</th>
              <th class="col-fc">{{ t('col_fc') }}</th>
              <th class="col-payload">{{ t('col_payload') }}</th>
              <th class="col-bytes">{{ t('col_bytes') }}</th>
              <th class="col-crc">CRC</th>
            </tr>
          </thead>
          <tbody>
            <tr
              v-for="r in filteredRows" :key="r.id"
              :class="{ selected: selected === r.id, 'err-row': r.crc === 'ERR' }"
              :title="r.tooltip || undefined"
              @click="selected = r.id"
            >
              <td class="mono muted">{{ r.id }}</td>
              <td class="mono">{{ r.t }}</td>
              <td class="mono muted">{{ r.dt }}</td>
              <td><span :class="dirPillClass(r.dir)">{{ r.dir }}</span></td>
              <td class="mono" style="font-weight:500" :title="SLAVE_NAMES[parseInt(r.slave, 16)] ? `0x${r.slave} · ${SLAVE_NAMES[parseInt(r.slave, 16)]}` : `0x${r.slave} (${parseInt(r.slave, 16)})`">0x{{ r.slave }}</td>
              <td class="mono fc-cell">{{ r.fc }}</td>
              <td>
                <span class="hex-payload">
                  <span
                    v-for="(b, i) in r.bytes_arr" :key="i"
                    class="hex-byte"
                    :style="hexByteStyle(b, i, r.bytes_arr)"
                  >{{ b }}</span>
                </span>
              </td>
              <td class="mono muted">{{ r.bytes }}</td>
              <td>
                <span v-if="r.crc === 'ERR'" class="crc-err mono">ERR</span>
                <span v-else class="crc-ok mono">OK</span>
              </td>
            </tr>
          </tbody>
        </table>
      </div>

      <!-- Detail panel -->
      <div v-if="sel" class="detail-panel">
        <div class="detail-header">
          <div class="detail-header-left">
            <span class="detail-title">Packet #{{ sel.id }}</span>
            <span :class="dirPillClass(sel.dir)">{{ sel.dir }}</span>
            <span class="muted detail-dir-label">{{ directionLabel(sel.dir, sel.slave) }}</span>
          </div>
          <span class="mono muted detail-time">{{ sel.t }} &middot; &Delta;t {{ sel.dt }}</span>
        </div>

        <div class="detail-grid">
          <div>
            <div class="sub-section-label">Header</div>
            <div class="kv" style="padding:5px 0"><div class="k">Slave ID</div><div class="v mono">0x{{ sel.slave }} ({{ parseInt(sel.slave, 16) }}){{ SLAVE_NAMES[parseInt(sel.slave, 16)] ? ' · ' + SLAVE_NAMES[parseInt(sel.slave, 16)] : '' }}</div></div>
            <div class="kv" style="padding:5px 0"><div class="k">{{ t('col_fc') }}</div><div class="v mono">{{ sel.fc }}</div></div>
            <div class="kv" style="padding:5px 0;border-bottom:0"><div class="k">{{ t('col_dir') }}</div><div class="v">{{ sel.dir === 'MASTER' ? 'Request' : sel.dir === 'SLAVE' ? 'Response' : 'Error response' }}</div></div>
          </div>

          <div>
            <div class="sub-section-label">{{ t('parsed') }}</div>
            <div class="kv" style="padding:5px 0"><div class="k">CRC</div><div class="v mono" :style="{ color: sel.crc === 'ERR' ? 'var(--mb-err)' : 'var(--mb-ok)' }">{{ sel.crc === 'ERR' ? '\u2715 Mismatch' : '\u2713 Valid' }}</div></div>
            <div class="kv" style="padding:5px 0;border-bottom:0"><div class="k">{{ t('size') }}</div><div class="v mono">{{ sel.bytes }} {{ t('col_bytes').toLowerCase() }}</div></div>
          </div>

          <div>
            <div class="sub-section-label">{{ t('raw_bytes') }}</div>
            <div class="raw-bytes-box">
              <span
                v-for="(b, i) in sel.bytes_arr" :key="i"
                class="raw-byte"
                :style="hexByteStyle(b, i, sel.bytes_arr)"
              >{{ b }}</span>
            </div>
            <div class="raw-legend">
              <span><span class="legend-dot" style="background:var(--mb-master)" />Slave ID</span>
              <span><span class="legend-dot" style="background:var(--mb-hex-slot)" />FC</span>
              <span><span class="legend-dot" style="background:var(--mb-data)" />Data</span>
              <span><span class="legend-dot" style="background:var(--mb-hex-crc)" />CRC</span>
            </div>
          </div>
        </div>
      </div>
      </div><!-- /sniffer-body -->
    </div><!-- /sniffer-main -->
  </Layout>
</template>

<style scoped>
.heading-sep {
  display: inline-block;
  width: 24px;
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
  grid-template-columns: 16px 32px 1fr auto 44px;
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
  outline: 2px solid var(--primary-color);
  outline-offset: -2px;
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

.facet-label {
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  font-size: 12px;
  display: flex;
  gap: 6px;
  align-items: baseline;
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

.facet-kind {
  font-size: 9px;
  text-transform: uppercase;
  letter-spacing: 0.08em;
  padding: 1px 5px;
  border-radius: 2px;
  font-weight: 600;
  margin-left: 2px;
}

.facet-kind-read {
  color: var(--mb-master);
  background: color-mix(in oklch, var(--mb-master) 10%, white);
}

.facet-kind-write {
  color: color-mix(in oklch, var(--warn, #f59e0b) 50%, #000);
  background: color-mix(in oklch, var(--warn, #f59e0b) 15%, white);
}

.facet-kind-unknown {
  font-size: 10px;
  color: var(--text-muted);
}

.facet-fc-btns {
  padding: 0 16px 8px;
  display: flex;
  gap: 6px;
}

.facet-kind-btn {
  flex: 1;
  height: 22px;
  font-size: 11px;
  background: var(--bg-surface);
  border: 1px solid var(--border-color);
  color: var(--text-secondary);
  border-radius: var(--r-sm);
  cursor: pointer;
}

.facet-kind-btn:hover {
  background: var(--bg-surface-subtle);
  border-color: var(--border-strong);
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

.col-id { width: 56px; }
.col-time { width: 100px; }
.col-dt { width: 68px; }
.col-sender { width: 76px; }
.col-slave { width: 80px; }
.col-fc { width: 200px; }
.col-payload { max-width: 0; width: 100%; }
.col-bytes { width: 60px; }
.col-crc { width: 60px; }

.fc-cell {
  font-size: 12px;
  color: var(--text-secondary);
}

/* Direction pill */
.dir-pill {
  display: inline-block;
  font-family: var(--font-mono);
  font-size: 11px;
  font-weight: 600;
  padding: 1px 7px;
  border-radius: 4px;
  border: 1px solid;
  letter-spacing: 0.04em;
}

.dir-master {
  color: var(--mb-master);
  background: color-mix(in oklch, var(--mb-master) 8%, white);
  border-color: color-mix(in oklch, var(--mb-master) 25%, white);
}

.dir-slave {
  color: var(--mb-slave);
  background: color-mix(in oklch, var(--mb-slave) 6%, white);
  border-color: color-mix(in oklch, var(--mb-slave) 22%, white);
}

.dir-err {
  color: var(--mb-err);
  background: color-mix(in oklch, var(--mb-err) 8%, white);
  border-color: color-mix(in oklch, var(--mb-err) 25%, white);
}

.dir-timeout {
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

/* Detail panel */
.detail-panel {
  border-top: 1px solid var(--border-color);
  background: var(--bg-surface-subtle);
  padding: 16px 32px 18px;
  flex-shrink: 0;
}

.detail-header {
  display: flex;
  align-items: baseline;
  justify-content: space-between;
  margin-bottom: 10px;
}

.detail-header-left {
  display: flex;
  align-items: center;
  gap: 10px;
}

.detail-title {
  font-size: 13px;
  font-weight: 600;
}

.detail-dir-label {
  font-size: 12px;
}

.detail-time {
  font-size: 11px;
}

.detail-grid {
  display: grid;
  grid-template-columns: 1fr 1fr 1.2fr;
  gap: 24px;
}

/* Raw bytes box */
.raw-bytes-box {
  background: var(--bg-surface);
  border: 1px solid var(--border-color);
  border-radius: var(--r-md);
  padding: 10px 12px;
  font-family: var(--font-mono);
  font-size: 12.5px;
  line-height: 1.8;
}

.raw-byte {
  margin-right: 6px;
  display: inline-block;
}

.raw-legend {
  margin-top: 8px;
  display: flex;
  gap: 14px;
  font-size: 11px;
  color: var(--text-muted);
}

.legend-dot {
  display: inline-block;
  width: 8px;
  height: 8px;
  border-radius: 2px;
  margin-right: 5px;
  vertical-align: middle;
}

/* Responsive */
@media (max-width: 1024px) {
  .detail-grid {
    grid-template-columns: 1fr 1fr;
  }
}

@media (max-width: 680px) {
  .detail-panel {
    padding: 12px;
  }
  .detail-grid {
    grid-template-columns: 1fr;
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
    "col_dir": "Sender",
    "col_fc": "Function code",
    "col_payload": "Payload (HEX)",
    "col_bytes": "Bytes",
    "parsed": "Parsed",
    "start_addr": "Starting address",
    "quantity": "Quantity",
    "size": "Size",
    "raw_bytes": "Raw bytes",
    "export_csv": "Export CSV"
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
    "col_dir": "Отправитель",
    "col_fc": "Код функции",
    "col_payload": "Payload (HEX)",
    "col_bytes": "Байт",
    "parsed": "Разобрано",
    "start_addr": "Начальный адрес",
    "quantity": "Количество",
    "size": "Размер",
    "raw_bytes": "Сырые байты",
    "export_csv": "Экспорт CSV"
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
    "col_dir": "Жіберуші",
    "col_fc": "Функция коды",
    "col_payload": "Payload (HEX)",
    "col_bytes": "Байт",
    "parsed": "Талданған",
    "start_addr": "Бастапқы адрес",
    "quantity": "Саны",
    "size": "Өлшемі",
    "raw_bytes": "Шикі байттар",
    "export_csv": "CSV жүктеу"
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
    "col_dir": "Mittente",
    "col_fc": "Codice funzione",
    "col_payload": "Payload (HEX)",
    "col_bytes": "Byte",
    "parsed": "Analizzato",
    "start_addr": "Indirizzo iniziale",
    "quantity": "Quantità",
    "size": "Dimensione",
    "raw_bytes": "Byte grezzi",
    "export_csv": "Esporta CSV"
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
    "col_dir": "Absender",
    "col_fc": "Funktionscode",
    "col_payload": "Payload (HEX)",
    "col_bytes": "Bytes",
    "parsed": "Analysiert",
    "start_addr": "Startadresse",
    "quantity": "Anzahl",
    "size": "Größe",
    "raw_bytes": "Rohbytes",
    "export_csv": "CSV exportieren"
  }
}
</i18n>
