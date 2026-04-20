<script setup lang="ts">
import { ref, computed } from 'vue';
import { useI18n } from 'vue-i18n';
import Button from '@/components/Button.vue';
import Heading from '@/components/Heading.vue';
import Layout from '@/components/Layout.vue';

const { t } = useI18n();

const LOGS = [
  { t: '17:31:04.112', dt: '\u2014',       dir: 'MASTER', slave: '01', fc: '03 Read Holding Regs',     pl: '01 03 00 00 00 0A CD CB', bytes: 8,  crc: 'OK', parsed: { start: 0, qty: 10 } },
  { t: '17:31:04.115', dt: '+3 ms',   dir: 'SLAVE',  slave: '01', fc: '03 Read Holding Regs',     pl: '01 03 14 00 DC 00 01 00 00 00 64 00 00 00 00 00 \u2026', bytes: 23, crc: 'OK' },
  { t: '17:31:03.442', dt: '+673 ms', dir: 'MASTER', slave: '01', fc: '06 Write Single Reg',      pl: '01 06 00 10 00 64 88 E4', bytes: 8,  crc: 'OK' },
  { t: '17:31:03.446', dt: '+4 ms',   dir: 'SLAVE',  slave: '01', fc: '06 Write Single Reg',      pl: '01 06 00 10 00 64 88 E4', bytes: 8,  crc: 'OK' },
  { t: '17:31:02.881', dt: '+561 ms', dir: 'MASTER', slave: '02', fc: '03 Read Holding Regs',     pl: '02 03 00 00 00 08 04 5E', bytes: 8,  crc: 'OK' },
  { t: '17:31:02.885', dt: '+4 ms',   dir: 'SLAVE',  slave: '02', fc: '03 Read Holding Regs',     pl: '02 03 10 00 01 00 00 00 64 41 4D 85 1F \u2026', bytes: 21, crc: 'OK' },
  { t: '17:31:02.441', dt: '+444 ms', dir: 'MASTER', slave: '01', fc: '01 Read Coils',            pl: '01 01 00 00 00 10 FD CF', bytes: 8,  crc: 'OK' },
  { t: '17:31:02.444', dt: '+3 ms',   dir: 'SLAVE',  slave: '01', fc: '01 Read Coils',            pl: '01 01 02 FF 00 A4 38',    bytes: 7,  crc: 'OK' },
  { t: '17:31:01.997', dt: '+553 ms', dir: 'MASTER', slave: '05', fc: '04 Read Input Regs',       pl: '05 04 00 00 00 04 F1 C9', bytes: 8,  crc: 'OK' },
  { t: '17:31:01.999', dt: '+2 ms',   dir: 'SLAVE',  slave: '05', fc: '04 Read Input Regs',       pl: '05 04 08 43 6C CC CD 41 4D 85 1F B2 2C', bytes: 13, crc: 'OK' },
  { t: '17:31:01.330', dt: '+669 ms', dir: 'MASTER', slave: '02', fc: '10 Write Multiple Regs',   pl: '02 10 00 20 00 03 06 00 12 00 34 01 2C 01 F4', bytes: 15, crc: 'OK' },
  { t: '17:31:01.334', dt: '+4 ms',   dir: 'SLAVE',  slave: '02', fc: '10 Write Multiple Regs',   pl: '02 10 00 20 00 03 0C', bytes: 8,  crc: 'OK' },
  { t: '17:31:00.881', dt: '+547 ms', dir: 'MASTER', slave: '04', fc: '03 Read Holding Regs',     pl: '04 03 00 00 00 06 C4 4A', bytes: 8,  crc: 'OK' },
  { t: '17:31:00.887', dt: '+6 ms',   dir: 'SLAVE',  slave: '04', fc: '03 Read Holding Regs',     pl: '04 03 0C 00 01 00 64 77 33', bytes: 15, crc: 'OK' },
  { t: '17:31:00.441', dt: '+554 ms', dir: 'MASTER', slave: '01', fc: '03 Read Holding Regs',     pl: '01 03 00 0A 00 05 94 0A', bytes: 8,  crc: 'OK' },
  { t: '17:31:00.444', dt: '+3 ms',   dir: 'SLAVE',  slave: '01', fc: '03 Read Holding Regs',     pl: '01 03 0A 00 01 00 00 00 64 46 D1', bytes: 15, crc: 'OK' },
  { t: '17:30:59.998', dt: '+446 ms', dir: 'MASTER', slave: '06', fc: '06 Write Single Reg',      pl: '06 06 00 05 00 01 59 E3', bytes: 8,  crc: 'OK' },
  { t: '17:30:59.001', dt: '+997 ms', dir: 'SLAVE',  slave: '06', fc: '06 Write Single Reg',      pl: '06 06 00 05 00 01 59 E3', bytes: 8,  crc: 'OK' },
  { t: '17:30:59.013', dt: '+12 ms',  dir: 'MASTER', slave: '05', fc: '04 Read Input Regs',       pl: '05 04 00 00 00 04 F1 C9', bytes: 8,  crc: 'OK' },
  { t: '17:30:59.016', dt: '+3 ms',   dir: 'ERR',    slave: '05', fc: '04 Read Input Regs',       pl: '05 04 86 43 6C CC CD 41 4D 85 1F ?? ??', bytes: 13, crc: 'ERR' },
  { t: '17:30:58.441', dt: '+425 ms', dir: 'MASTER', slave: '01', fc: '03 Read Holding Regs',     pl: '01 03 00 00 00 0A CD CB', bytes: 8,  crc: 'OK' },
  { t: '17:30:58.444', dt: '+3 ms',   dir: 'SLAVE',  slave: '01', fc: '03 Read Holding Regs',     pl: '01 03 14 00 DC 00 01 00 00 00 64 00 00 00 \u2026', bytes: 23, crc: 'OK' },
].map((r, i) => ({ ...r, id: 1267 - i }));

const running = ref(true);
const selected = ref(1267);
const filter = ref('');
const onlyErrors = ref(false);
const portFilter = ref('all');
const portOptions = ['all', 'A', 'B'];

const rows = computed(() => {
  let r = LOGS;
  if (onlyErrors.value) r = r.filter(x => x.dir === 'ERR' || x.crc === 'ERR');
  if (filter.value.trim()) {
    const f = filter.value.toLowerCase();
    r = r.filter(x =>
      x.slave.includes(f) || x.fc.toLowerCase().includes(f) || x.pl.toLowerCase().includes(f)
    );
  }
  return r;
});

const errorCount = computed(() => LOGS.filter(x => x.dir === 'ERR' || x.crc === 'ERR').length);

const sel = computed(() => rows.value.find(r => r.id === selected.value) || rows.value[0]);

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
  if (dir === 'MASTER') return `Master \u2192 Slave ${slave}`;
  if (dir === 'SLAVE') return `Slave ${slave} \u2192 Master`;
  return 'Error response';
}
</script>

<template>
  <Layout>
    <Heading :title="t('title')">
      <template #default>
        <Button :variant="running ? 'danger' : 'primary'" @click="running = !running">
          {{ running ? t('stop') : t('start') }}
        </Button>
        <Button variant="outline">{{ t('clear') }}</Button>
      </template>
    </Heading>

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
          {{ p === 'all' ? t('all_ports') : 'Port ' + p }}
        </button>
      </div>

      <label class="filter-errors">
        <input type="checkbox" v-model="onlyErrors" />
        {{ t('errors_only') }}
      </label>

      <div class="filter-spacer" />

      <div class="filter-stats">
        <span><b class="mono">{{ LOGS.length.toLocaleString() }}</b> {{ t('packets') }}</span>
        <span><b class="mono stat-err">{{ errorCount }}</b> {{ errorCount === 1 ? t('error') : t('errors') }}</span>
        <span><b class="mono">0.31 kB/s</b></span>
      </div>
    </div>

    <!-- Log table -->
    <div class="sniffer-body">
      <div class="sniffer-table-wrap">
        <table class="sniffer-table">
          <thead>
            <tr>
              <th class="col-id">#</th>
              <th class="col-time">{{ t('col_time') }}</th>
              <th class="col-dt">&Delta;t</th>
              <th class="col-dir">{{ t('col_dir') }}</th>
              <th class="col-slave">Slave</th>
              <th class="col-fc">{{ t('col_fc') }}</th>
              <th class="col-payload">{{ t('col_payload') }}</th>
              <th class="col-bytes">{{ t('col_bytes') }}</th>
              <th class="col-crc">CRC</th>
            </tr>
          </thead>
          <tbody>
            <tr
              v-for="r in rows" :key="r.id"
              :class="{ selected: selected === r.id, 'err-row': r.crc === 'ERR' }"
              @click="selected = r.id"
            >
              <td class="mono muted">{{ r.id }}</td>
              <td class="mono">{{ r.t }}</td>
              <td class="mono muted">{{ r.dt }}</td>
              <td><span :class="dirPillClass(r.dir)">{{ r.dir }}</span></td>
              <td class="mono" style="font-weight:500">{{ r.slave }}</td>
              <td class="mono fc-cell">{{ r.fc }}</td>
              <td>
                <span class="hex-payload">
                  <span
                    v-for="(b, i) in r.pl.split(' ')" :key="i"
                    class="hex-byte"
                    :style="hexByteStyle(b, i, r.pl.split(' '))"
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
            <div class="kv" style="padding:5px 0"><div class="k">Slave ID</div><div class="v mono">{{ sel.slave }} ({{ parseInt(sel.slave, 16) }})</div></div>
            <div class="kv" style="padding:5px 0"><div class="k">{{ t('col_fc') }}</div><div class="v mono">{{ sel.fc }}</div></div>
            <div class="kv" style="padding:5px 0;border-bottom:0"><div class="k">{{ t('col_dir') }}</div><div class="v">{{ sel.dir === 'MASTER' ? 'Request' : sel.dir === 'SLAVE' ? 'Response' : 'Error response' }}</div></div>
          </div>

          <div>
            <div class="sub-section-label">{{ t('parsed') }}</div>
            <template v-if="sel.fc.startsWith('03')">
              <div class="kv" style="padding:5px 0"><div class="k">{{ t('start_addr') }}</div><div class="v mono">0x0000 (0)</div></div>
              <div class="kv" style="padding:5px 0"><div class="k">{{ t('quantity') }}</div><div class="v mono">10 registers</div></div>
            </template>
            <div class="kv" style="padding:5px 0"><div class="k">CRC</div><div class="v mono" :style="{ color: sel.crc === 'ERR' ? 'var(--mb-err)' : 'var(--mb-ok)' }">{{ sel.crc === 'ERR' ? '\u2715 Mismatch' : '\u2713 Valid' }}</div></div>
            <div class="kv" style="padding:5px 0;border-bottom:0"><div class="k">{{ t('size') }}</div><div class="v mono">{{ sel.bytes }} {{ t('col_bytes').toLowerCase() }}</div></div>
          </div>

          <div>
            <div class="sub-section-label">{{ t('raw_bytes') }}</div>
            <div class="raw-bytes-box">
              <span
                v-for="(b, i) in sel.pl.split(' ')" :key="i"
                class="raw-byte"
                :style="hexByteStyle(b, i, sel.pl.split(' '))"
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
.col-dir { width: 76px; }
.col-slave { width: 70px; }
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
    "all_ports": "All ports",
    "errors_only": "Errors only",
    "packets": "packets",
    "error": "error",
    "errors": "errors",
    "col_time": "Time",
    "col_dir": "Direction",
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
    "all_ports": "Все порты",
    "errors_only": "Только ошибки",
    "packets": "пакетов",
    "error": "ошибка",
    "errors": "ошибок",
    "col_time": "Время",
    "col_dir": "Направление",
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
    "all_ports": "Барлық порттар",
    "errors_only": "Тек қателер",
    "packets": "пакет",
    "error": "қате",
    "errors": "қате",
    "col_time": "Уақыт",
    "col_dir": "Бағыт",
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
    "all_ports": "Tutte le porte",
    "errors_only": "Solo errori",
    "packets": "pacchetti",
    "error": "errore",
    "errors": "errori",
    "col_time": "Ora",
    "col_dir": "Direzione",
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
    "all_ports": "Alle Ports",
    "errors_only": "Nur Fehler",
    "packets": "Pakete",
    "error": "Fehler",
    "errors": "Fehler",
    "col_time": "Zeit",
    "col_dir": "Richtung",
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
