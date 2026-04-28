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
  pl: string
  bytes_arr: string[]
  bytes: number
  crc: 'OK' | 'ERR'
  t: string
  dt: string
}

const rows = ref<SniffRow[]>([]);
const running = ref(false);
const ws = ref<WebSocket | null>(null);
let lastTimestampUs = 0;
const wsStatus = ref<'connected' | 'disconnected' | 'reconnecting'>('disconnected');

const tableWrap = ref<HTMLElement | null>(null);
const selected = ref<number | null>(null);
const filter = ref('');
const onlyErrors = ref(false);
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
      fc: `${msg.function.toString(16).padStart(2, '0').toUpperCase()} ${fcName}`,
      pl: '',
      bytes_arr: [],
      bytes: 0,
      crc: 'ERR',
      t: formatTimestamp(msg.timestamp_us),
      dt,
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
    return {
      id: msg.id,
      port: msg.port,
      timestamp_us: msg.timestamp_us,
      dir: crc === 'ERR' ? 'ERR' : dir,
      slave,
      fc: `${msg.function.toString(16).padStart(2, '0').toUpperCase()} ${fcName}`,
      pl,
      bytes_arr: hexToPayloadString(msg.raw).split(' '),
      bytes: msg.size,
      crc,
      t: formatTimestamp(msg.timestamp_us),
      dt,
    }
  }
  return null
}

function getWsUrl(): string {
  const proto = location.protocol === 'https:' ? 'wss' : 'ws'
  return `${proto}://${location.host}/sniffer/ws`
}

function portFilterToWsPort(p: string): number {
  return parseInt(p)
}

function sendPortStart(port: string | number) {
  ws.value?.send(JSON.stringify({ cmd: 'start', port }))
}

function sendPortStop(port: string | number) {
  ws.value?.send(JSON.stringify({ cmd: 'stop', port }))
}

function connectWs() {
  lastTimestampUs = 0
  ws.value = new WebSocket(getWsUrl())
  ws.value.onopen = () => {
    wsStatus.value = 'connected'
    sendPortStart(portFilterToWsPort(portFilter.value))
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
  sendPortStop(portFilterToWsPort(portFilter.value))
  ws.value?.close()
  ws.value = null
}

watch(portFilter, (newPort, oldPort) => {
  if (!running.value || ws.value === null || wsStatus.value !== 'connected') return
  sendPortStop(portFilterToWsPort(oldPort))
  sendPortStart(portFilterToWsPort(newPort))
})

function clearLogs() {
  rows.value = []
  lastTimestampUs = 0
}

onUnmounted(() => stopCapture())

const errorCount = computed(() => rows.value.filter(x => x.crc === 'ERR').length);

const filteredRows = computed(() => {
  let r = rows.value
  const p = parseInt(portFilter.value)
  r = r.filter(x => x.port === p)
  if (onlyErrors.value) r = r.filter(x => x.crc === 'ERR')
  if (filter.value.trim()) {
    const f = filter.value.toLowerCase()
    r = r.filter(x =>
      x.slave.toLowerCase().includes(f) ||
      x.fc.toLowerCase().includes(f) ||
      x.pl.toLowerCase().includes(f)
    )
  }
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
</script>

<template>
  <Layout>
    <Heading :title="t('title')" :crumbs="t('crumbs')">
      <template #default>
        <Button :variant="running ? 'danger' : 'primary'" @click="running ? stopCapture() : startCapture()">
          {{ running ? t('stop') : t('start') }}
        </Button>
        <Button variant="outline" @click="clearLogs()">{{ t('clear') }}</Button>
      </template>
    </Heading>
    <span v-if="wsStatus !== 'connected'" class="ws-status">{{ wsStatus === 'reconnecting' ? 'Reconnecting…' : 'Disconnected' }}</span>

    <!-- Filter bar -->
    <div class="filter-bar">
      <div class="filter-search">
        <svg class="filter-search-icon" width="13" height="13" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">
          <circle cx="7" cy="7" r="5" /><path d="m11 11 3 3" />
        </svg>
        <input
          v-model="filter"
          type="text"
          :placeholder="t('filter_placeholder')"
          class="filter-input"
        />
      </div>

      <div class="filter-ports">
        <button
          v-for="p in portOptions" :key="p"
          :class="['port-btn', { active: portFilter === p }]"
          @click="portFilter = p"
        >
          Port {{ p }}
        </button>
      </div>

      <label class="filter-errors">
        <input type="checkbox" v-model="onlyErrors" />
        {{ t('errors_only') }}
      </label>

      <div class="filter-spacer" />

      <div class="filter-stats">
        <span><b class="mono">{{ rows.length.toLocaleString() }}</b> {{ t('packets') }}</span>
        <span><b class="mono stat-err">{{ errorCount }}</b> {{ errorCount === 1 ? t('error') : t('errors') }}</span>
      </div>
    </div>

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
              @click="selected = r.id"
            >
              <td class="mono muted">{{ r.id }}</td>
              <td class="mono">{{ r.t }}</td>
              <td class="mono muted">{{ r.dt }}</td>
              <td><span :class="dirPillClass(r.dir)">{{ r.dir }}</span></td>
              <td class="mono" style="font-weight:500" :title="`0x${r.slave} (${parseInt(r.slave, 16)})`">0x{{ r.slave }}</td>
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
            <div class="kv" style="padding:5px 0"><div class="k">Slave ID</div><div class="v mono">0x{{ sel.slave }} ({{ parseInt(sel.slave, 16) }})</div></div>
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
    </div>
  </Layout>
</template>

<style scoped>
/* Filter bar */
.filter-bar {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 10px 32px;
  background: var(--bg-surface);
  border-bottom: 1px solid var(--border-color);
}

.filter-search {
  position: relative;
  flex: 1;
  max-width: 340px;
}

.filter-search-icon {
  position: absolute;
  left: 9px;
  top: 50%;
  transform: translateY(-50%);
  color: var(--text-muted);
  pointer-events: none;
}

.filter-input {
  padding-left: 28px !important;
}

.filter-ports {
  display: flex;
  gap: 4px;
}

.port-btn {
  height: 26px;
  padding: 0 10px;
  font-size: 12px;
  background: var(--bg-surface);
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

.filter-errors {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 12px;
  color: var(--text-secondary);
  cursor: pointer;
  white-space: nowrap;
}

.filter-errors input {
  margin: 0;
  accent-color: var(--danger-color);
}

.filter-spacer {
  flex: 1;
}

.filter-stats {
  display: flex;
  gap: 14px;
  font-size: 12px;
  color: var(--text-muted);
  white-space: nowrap;
}

.filter-stats b {
  font-weight: 500;
  color: var(--text-color);
}

.stat-err {
  color: var(--mb-err) !important;
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

.ws-status {
  display: block;
  font-size: 12px;
  color: var(--text-muted);
  padding: 4px 32px;
  background: color-mix(in oklch, var(--text-muted) 6%, var(--bg-surface));
  border-bottom: 1px solid var(--border-color);
}

/* Hex payload */
.hex-payload {
  font-family: var(--font-mono);
  font-size: 12px;
  letter-spacing: 0.02em;
  font-weight: 500;
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
  .filter-bar {
    flex-wrap: wrap;
    padding: 10px 12px;
  }
  .filter-search {
    max-width: none;
  }
  .filter-stats {
    flex-wrap: wrap;
    gap: 8px;
  }
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
    "filter_placeholder": "Filter by Slave ID / FC / payload\u2026",
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
    "raw_bytes": "Raw bytes"
  },
  "ru": {
    "title": "Sniffer",
    "crumbs": "Перехват пакетов Modbus",
    "start": "Старт",
    "stop": "Стоп",
    "clear": "Очистить",
    "filter_placeholder": "Фильтр по Slave ID / FC / payload…",
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
    "raw_bytes": "Сырые байты"
  },
  "kk": {
    "title": "Sniffer",
    "crumbs": "Modbus пакеттерін ұстау",
    "start": "Бастау",
    "stop": "Тоқтату",
    "clear": "Тазалау",
    "filter_placeholder": "Slave ID / FC / payload бойынша сүзу…",
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
    "raw_bytes": "Шикі байттар"
  },
  "it": {
    "title": "Sniffer",
    "crumbs": "Cattura pacchetti Modbus",
    "start": "Avvia",
    "stop": "Ferma",
    "clear": "Cancella",
    "filter_placeholder": "Filtra per Slave ID / FC / payload…",
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
    "raw_bytes": "Byte grezzi"
  },
  "de": {
    "title": "Sniffer",
    "crumbs": "Modbus-Paketerfassung",
    "start": "Start",
    "stop": "Stopp",
    "clear": "Löschen",
    "filter_placeholder": "Filtern nach Slave-ID / FC / Payload…",
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
    "raw_bytes": "Rohbytes"
  }
}
</i18n>
