<script setup lang="ts">
import { computed, ref } from 'vue';
import { useI18n } from 'vue-i18n';
import { decodePacket, parseHex, type DecodedPacket, type Direction } from '@/common/modbusDecoder';
import {
  type TreeRow,
  type EndiannessKey,
  ENDIAN_CONFIGS,
  flattenNode,
  isPrintable,
  f32str,
} from '@/utils/packetDecoderUtils';
import { type SniffRow } from '@/utils/snifferUtils';
import MagnifierIcon from '@/assets/magnifierIcon.svg?component';

// Props
const props = defineProps<{ packet: SniffRow }>();

const { t } = useI18n();

// State
const hoveredRange = ref<{ start: number; end: number } | null>(null);
const activeEndianness = ref<EndiannessKey>('abcd');
type BitMode = '16' | '32';
const activeBitMode = ref<BitMode>('16');
const showDataPopup = ref(false);

function setEndianness(key: string) {
 activeEndianness.value = key as EndiannessKey;
}

const direction = computed<Direction>(() =>
  props.packet.sender === 'MASTER' ? 'request' : 'response'
);

const rawBytes = computed<number[]>(() => parseHex(props.packet.pl) ?? []);

const decoded = computed<DecodedPacket>(() =>
  decodePacket(props.packet.pl, direction.value)
);

const fullHex = computed<string>(() =>
  rawBytes.value.map(b => b.toString(16).toUpperCase().padStart(2, '0')).join('')
);

// Tree — flatten decoded object into rows
const treeRows = computed<TreeRow[]>(() =>
  flattenNode(
    decoded.value as unknown as Record<string, unknown>,
    0, fullHex.value, 0, undefined,
    // Arbitration packets carry crc === 'N/A' (a truthy string that is not a valid status);
    // pass undefined so flattenNode does not append a spurious "(N/A)" annotation.
    props.packet.crc !== 'N/A' ? props.packet.crc as 'OK' | 'ERR' : undefined
  )
);

// Hover
function onRowHover(start: number, end: number) {
  hoveredRange.value = end > start ? { start, end } : null;
}
function onRowLeave() {
 hoveredRange.value = null;
}

// Hex editor — 8 bytes per row
function isByteHighlighted(i: number): boolean {
  return hoveredRange.value !== null && i >= hoveredRange.value.start && i < hoveredRange.value.end;
}

const HEX_ROW = 8;

interface HexEditorRow {
  offset: string;
  bytes: { hex: string; ascii: string; printable: boolean; index: number }[];
  padCount: number;
}

const hexEditorRows = computed<HexEditorRow[]>(() => {
  const b = rawBytes.value;
  const out: HexEditorRow[] = [];
  for (let row = 0; row < b.length; row += HEX_ROW) {
    const chunk = b.slice(row, row + HEX_ROW);
    out.push({
      offset: row.toString(16).toUpperCase().padStart(4, '0'),
      bytes: chunk.map((byte, ci) => ({
        hex: byte.toString(16).toUpperCase().padStart(2, '0'),
        ascii: isPrintable(byte) ? String.fromCharCode(byte) : '.',
        printable: isPrintable(byte),
        index: row + ci,
      })),
      padCount: HEX_ROW - chunk.length,
    });
  }
  return out;
});

// Data interpretation popup
const leafData = computed<string | null>(() => {
  function findDeepest(obj: Record<string, unknown>): string | null {
    if (obj.payload && typeof obj.payload === 'object') {
      const deeper = findDeepest(obj.payload as Record<string, unknown>);
      if (deeper !== null) return deeper;
    }
    for (const k of ['data', 'write_data', 'events', 'additional_data']) {
      const v = obj[k];
      if (typeof v === 'string' && v.length >= 4) return v;
    }
    return null;
  }
  return findDeepest(decoded.value as unknown as Record<string, unknown>);
});

const leafBytes = computed<number[]>(() => {
  const hex = leafData.value;
  if (!hex) return [];
  const out: number[] = [];
  for (let i = 0; i < hex.length; i += 2) out.push(parseInt(hex.slice(i, i + 2), 16));
  return out;
});

const regs16 = computed<{ index: number; dec: number; int16: number; hex: string }[]>(() => {
  const b = leafBytes.value;
  const out = [];
  for (let i = 0; i + 1 < b.length; i += 2) {
    const v = ((b[i] << 8) | b[i + 1]) >>> 0;
    // Interpret as signed Int16: if MSB is set, subtract 2^16
    const s = v >= 0x8000 ? v - 0x10000 : v;
    out.push({ index: i / 2, dec: v, int16: s, hex: v.toString(16).toUpperCase().padStart(4, '0') });
  }
  return out;
});

interface Chunk32 {
 offset: string; hex: string; uint32: number; int32: number; float32: string;
}

const chunks32 = computed<Chunk32[]>(() => {
  const b = leafBytes.value;
  const cfg = ENDIAN_CONFIGS[activeEndianness.value];
  const out: Chunk32[] = [];
  for (let i = 0; i + 3 < b.length; i += 4) {
    const u = cfg.fn(b, i);
    const s = u >= 0x80000000 ? -(0x100000000 - u) : u;
    out.push({ offset: `${i}..${i+3}`, hex: '0x' + u.toString(16).toUpperCase().padStart(8, '0'), uint32: u, int32: s, float32: f32str(u) });
  }
  return out;
});
</script>

<template>
  <div class="pkt-panel">
    <!-- Header -->
    <div class="pkt-header">
      <div class="pkt-header-left">
        <span class="pkt-title">{{ t('packet') }} #{{ packet.id }}</span>
        <span :class="['sender-pill', 'sender-' + packet.sender.toLowerCase()]">{{ packet.sender }}</span>
        <span class="pkt-dir muted">
          <template v-if="packet.sender === 'MASTER'">{{ t('dir_master_to_slave', { slave: packet.slave }) }}</template>
          <template v-else-if="packet.sender === 'SLAVE'">{{ t('dir_slave_to_master', { slave: packet.slave }) }}</template>
          <template v-else>{{ packet.sender }}</template>
          · {{ packet.fc }}
        </span>
      </div>
      <span class="mono muted pkt-meta">{{ packet.t }} · Δt {{ packet.dt }} · {{ packet.bytes }} B · CRC
        <span :class="packet.crc === 'ERR' ? 'crc-err' : packet.crc === 'N/A' ? 'muted' : 'crc-ok'">{{ packet.crc }}</span>
      </span>
    </div>

    <!-- Body: two columns -->
    <div class="pkt-body">
      <!-- Left: decoded tree -->
      <div class="pkt-decoded">
        <div class="pkt-col-label">{{ t('decoded') }}</div>
        <div class="tree-rows">
          <!-- Intentional dynamic geometry: per-row indentation depends on the runtime tree depth, so it cannot be a static CSS class. -->
          <div
            v-for="(row, i) in treeRows"
            :key="i"
            class="tree-row"
            :style="{ paddingLeft: `${row.depth * 14 + 8}px` }"
            @mouseenter="onRowHover(row.byteStart, row.byteEnd)"
            @mouseleave="onRowLeave"
          >
            <template v-if="row.isSection">
              <span :class="['tree-type', { 'tree-type-error': row.isError }]">{{ row.label }}</span>
            </template>

            <template v-else-if="row.isField">
              <span class="tree-key" :title="row.tooltip">{{ row.label }}</span>
              <span :class="['tree-val', { 'tree-val-error': row.isError }]" :title="row.valueTooltip || undefined">{{ row.value }}</span>
              <!-- Magnifier icon for data fields — opens interpretation popup -->
              <span v-if="row.isDataField && leafBytes.length >= 2" class="data-icon-btn" role="button" tabindex="0" :title="t('interpret_data')" :aria-label="t('interpret_data')" @click.stop="showDataPopup = !showDataPopup" @keydown.enter.stop="showDataPopup = !showDataPopup" @keydown.space.prevent.stop="showDataPopup = !showDataPopup">
                <MagnifierIcon class="data-icon-svg" />
              </span>
            </template>

            <template v-else-if="row.isArray">
              <span class="tree-key muted">{{ row.label }}</span>
              <div v-if="row.arrayItems" class="array-items">
                <div v-for="(item, ai) in row.arrayItems" :key="ai" class="array-item">
                  <span class="muted">{{ item.label }}</span>
                  <span class="tree-val">{{ item.value }}</span>
                </div>
              </div>
            </template>
          </div>
        </div>
      </div>

      <!-- Right: raw bytes hex editor -->
      <div class="pkt-raw">
        <div class="pkt-col-label">{{ t('raw_bytes') }} · {{ packet.bytes }} B</div>
        <div class="hexed">
          <div v-for="(row, ri) in hexEditorRows" :key="ri" class="hexed-row">
            <span class="hexed-off">{{ row.offset }}</span>
            <span class="hexed-hex">
              <span v-for="b in row.bytes" :key="b.index" :class="['hexed-byte', { 'hexed-byte-hi': isByteHighlighted(b.index) }]">{{ b.hex }}</span>
              <span v-for="pi in row.padCount" :key="pi" class="hexed-pad"></span>
            </span>
            <span class="hexed-ascii">
              <span v-for="b in row.bytes" :key="b.index" :class="['hexed-ch', { 'hexed-ch-print': b.printable, 'hexed-byte-hi': isByteHighlighted(b.index) }]">{{ b.ascii }}</span>
            </span>
          </div>
        </div>
      </div>
    </div>

    <!-- Data interpretation popup (absolute positioned) -->
    <div v-if="showDataPopup && leafBytes.length >= 2" class="data-popup">
      <div class="data-popup-header">
        <span class="pkt-col-label pkt-col-labelNoPad">{{ t('data_interpretation') }}</span>
        <div class="bit-mode-tabs">
          <button :class="['bit-tab', { active: activeBitMode === '16' }]" @click="activeBitMode = '16'">{{ t('bit_16') }}</button>
          <button :class="['bit-tab', { active: activeBitMode === '32' }]" @click="activeBitMode = '32'">{{ t('bit_32') }}</button>
        </div>
        <button class="popup-close" :aria-label="t('close')" @click="showDataPopup = false">✕</button>
      </div>

      <!-- 16-bit registers view — table layout matching the 32-bit view -->
      <div v-if="activeBitMode === '16'" class="regs16-view">
        <table class="chunks32-table">
          <thead><tr><th>{{ t('th_reg') }}</th><th>{{ t('th_hex') }}</th><th>{{ t('th_uint16') }}</th><th>{{ t('th_int16') }}</th></tr></thead>
          <tbody>
            <tr v-for="r in regs16" :key="r.index">
              <td class="muted">R{{ r.index }}</td>
              <td class="mono c-hex">0x{{ r.hex }}</td>
              <td class="mono c-u32">{{ r.dec }}</td>
              <td class="mono c-i32">{{ r.int16 }}</td>
            </tr>
          </tbody>
        </table>
      </div>

      <!-- 32-bit endianness view -->
      <div v-if="activeBitMode === '32'" class="view32">
        <div v-if="leafBytes.length < 4" class="muted view32-empty">{{ t('need_4_bytes') }}</div>
        <template v-else>
          <div class="endian-tabs">
            <button
              v-for="(cfg, key) in ENDIAN_CONFIGS"
              :key="key"
              :class="['endian-tab', { active: activeEndianness === key }]"
              @click="setEndianness(key)"
            >
{{ cfg.label }} <span class="muted endian-tab-desc">{{ cfg.desc }}</span>
</button>
          </div>
          <table class="chunks32-table">
            <thead><tr><th>{{ t('th_bytes') }}</th><th>{{ t('th_hex') }}</th><th>{{ t('th_uint32') }}</th><th>{{ t('th_int32') }}</th><th>{{ t('th_float32') }}</th></tr></thead>
            <tbody>
              <tr v-for="c in chunks32" :key="c.offset">
                <td class="muted">{{ c.offset }}</td>
                <td class="mono c-hex">{{ c.hex }}</td>
                <td class="mono c-u32">{{ c.uint32 }}</td>
                <td class="mono c-i32">{{ c.int32 }}</td>
                <td class="mono c-f32">{{ c.float32 }}</td>
              </tr>
            </tbody>
          </table>
        </template>
      </div>
    </div>
  </div>
</template>

<style scoped>
.pkt-panel {
  border-top: 1px solid var(--border-color);
  background: var(--bg-surface-subtle);
  flex-shrink: 0;
  display: flex;
  flex-direction: column;
  position: relative;
}

/* Header */
.pkt-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 10px 20px 8px;
  border-bottom: 1px solid var(--border-color);
  gap: 12px;
}

.pkt-header-left {
  display: flex;
  align-items: center;
  gap: 8px;
  min-width: 0;
  flex: 1;
}

.pkt-title { font-size: 13px; font-weight: 600; white-space: nowrap; }
.pkt-dir   { font-size: 12px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.pkt-meta  { font-size: 11px; white-space: nowrap; flex-shrink: 0; }

/* Body */
.pkt-body {
  display: flex;
  min-height: 0;
  flex: 1;
  overflow: hidden;
}

.pkt-decoded {
  flex: 1;
  overflow-y: auto;
  padding: 10px 0;
  border-right: 1px solid var(--border-color);
}

.pkt-raw {
  width: 340px;
  flex-shrink: 0;
  overflow-y: auto;
  overflow-x: hidden;
  padding: 10px 14px;
}

@media (max-width: 680px) {
  .pkt-body { flex-direction: column; }
  .pkt-decoded { border-right: 0; border-bottom: 1px solid var(--border-color); }
  .pkt-raw { width: 100%; }
  .tree-key { min-width: 110px; }
}

.pkt-col-label {
  font-size: 10px;
  text-transform: uppercase;
  letter-spacing: 0.1em;
  color: var(--text-muted);
  font-weight: 600;
  padding: 0 14px 6px;
}

/* Tree rows */
.tree-rows { font-size: 12.5px; }

.tree-row {
  display: flex;
  align-items: center;
  gap: 6px;
  padding-top: 2px;
  padding-bottom: 2px;
  padding-right: 14px;
  border-radius: 3px;
  transition: background 0.08s;
}

.tree-row:hover { background: var(--bg-surface); }

.tree-type { font-weight: 600; color: var(--text-secondary); font-size: 12px; }
.tree-type-error { color: var(--mb-err); }

.tree-key {
  color: var(--text-muted);
  min-width: 140px;
  flex-shrink: 0;
  font-size: 12px;
}

.tree-val {
  font-family: var(--font-mono);
  font-size: 12px;
  color: var(--text-color);
  word-break: break-all;
  flex: 1;
}

.tree-val-error { color: var(--mb-err); }

.array-items { display: flex; flex-direction: column; gap: 1px; }
.array-item  { display: flex; gap: 6px; font-size: 11.5px; }

/* Magnifier icon */
.data-icon-btn {
  flex-shrink: 0;
  display: inline-flex;
  align-items: center;
  color: var(--text-muted);
  cursor: pointer;
  line-height: 1;
  transition: color 0.12s;
}

.data-icon-btn:hover {
  color: var(--primary-color);
}

.data-icon-svg {
  display: block;
}

.pkt-col-labelNoPad {
  padding: 0;
}

.view32-empty {
  font-size: 12px;
  padding: 8px 0;
}

.endian-tab-desc {
  font-size: 10px;
}

/* Hex editor */
.hexed {
  font-family: var(--font-mono);
  font-size: 12px;
  line-height: 1.8;
}

.hexed-row {
  display: flex;
  align-items: baseline;
  gap: 8px;
}

.hexed-off {
  color: var(--text-muted);
  flex-shrink: 0;
  user-select: none;
}

.hexed-hex { display: flex; gap: 3px; flex-shrink: 0; }
.hexed-ascii { display: flex; flex-shrink: 0; }

.hexed-byte {
  display: inline-block;
  width: 20px;
  text-align: center;
  color: var(--text-secondary);
  border-radius: 2px;
  transition: background 0.07s, color 0.07s;
}

.hexed-pad {
  display: inline-block;
  width: 20px;
}

.hexed-byte-hi {
  background: color-mix(in oklch, var(--primary-color) 18%, transparent);
  color: var(--primary-color);
}

.hexed-ch {
  display: inline-block;
  width: 8px;
  text-align: center;
  color: var(--text-muted);
  border-radius: 2px;
}

.hexed-ch-print { color: var(--text-color); }

/* Data popup */
.data-popup {
  position: absolute;
  bottom: 100%;
  right: 0;
  width: 680px;
  max-width: 90vw;
  background: var(--bg-surface);
  border: 1px solid var(--border-color);
  border-radius: 8px 8px 0 0;
  box-shadow: 0 -4px 16px rgba(0,0,0,0.12);
  z-index: 10;
  padding: 12px 16px 14px;
}

.data-popup-header {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-bottom: 10px;
}

.popup-close {
  margin-left: auto;
  background: transparent;
  border: none;
  cursor: pointer;
  color: var(--text-muted);
  font-size: 13px;
  padding: 2px 5px;
  border-radius: 4px;
}
.popup-close:hover { color: var(--text-color); background: var(--bg-surface-subtle); }

/* Bit mode tabs */
.bit-mode-tabs { display: flex; gap: 3px; }
.bit-tab {
  padding: 2px 10px;
  font-size: 11px;
  border: 1px solid var(--border-color);
  border-radius: 4px;
  background: transparent;
  color: var(--text-muted);
  cursor: pointer;
}
.bit-tab.active {
  background: color-mix(in oklch, var(--primary-color) 8%, var(--bg-surface));
  border-color: var(--primary-color);
  color: var(--primary-color);
  font-weight: 600;
}

/* 16-bit view */
.regs16-view { padding: 2px 0; }

/* Endianness tabs */
.endian-tabs { display: flex; gap: 4px; flex-wrap: wrap; margin-bottom: 6px; }
.endian-tab {
  padding: 3px 10px;
  font-size: 11px;
  font-family: var(--font-mono);
  background: transparent;
  border: 1px solid var(--border-color);
  border-radius: 4px;
  cursor: pointer;
  color: var(--text-muted);
  transition: background 0.1s, border-color 0.1s;
}
.endian-tab:hover { background: var(--bg-surface-subtle); color: var(--text-color); border-color: var(--border-strong); }
.endian-tab.active {
  background: color-mix(in oklch, var(--primary-color) 8%, var(--bg-surface));
  border-color: var(--primary-color);
  color: var(--primary-color);
  font-weight: 600;
}

.chunks32-table { width: 100%; border-collapse: collapse; font-size: 12px; }
.chunks32-table th {
  font-size: 10px; text-transform: uppercase; letter-spacing: 0.06em;
  color: var(--text-muted); font-weight: 500; text-align: left;
  padding: 3px 6px; border-bottom: 1px solid var(--border-color);
}
.chunks32-table td { padding: 3px 6px; border-bottom: 1px solid color-mix(in oklch, var(--border-color) 40%, transparent); }

.c-hex { color: var(--mb-hex-slot); }
.c-u32 { color: var(--mb-ok); }
.c-i32 { color: var(--mb-slave); }
.c-f32 { color: var(--mb-master); }

/* Sender pills */
.sender-pill {
  display: inline-block;
  font-family: var(--font-mono);
  font-size: 11px;
  font-weight: 600;
  padding: 1px 7px;
  border-radius: 4px;
  border: 1px solid;
  letter-spacing: 0.04em;
  white-space: nowrap;
}
.sender-master { color: var(--mb-master); background: color-mix(in oklch, var(--mb-master) 8%, white); border-color: color-mix(in oklch, var(--mb-master) 25%, white); }
.sender-slave  { color: var(--mb-slave);  background: color-mix(in oklch, var(--mb-slave) 6%, white);  border-color: color-mix(in oklch, var(--mb-slave) 22%, white); }
.sender-err    { color: var(--mb-err);    background: color-mix(in oklch, var(--mb-err) 8%, white);    border-color: color-mix(in oklch, var(--mb-err) 25%, white); }
.sender-timeout { color: var(--text-muted); background: color-mix(in oklch, var(--text-muted) 6%, var(--bg-surface)); border-color: color-mix(in oklch, var(--text-muted) 20%, var(--bg-surface)); }

.crc-err { color: var(--mb-err); font-weight: 600; }
.crc-ok  { color: var(--mb-ok); }
.muted   { color: var(--text-muted); }
.mono    { font-family: var(--font-mono); }
</style>

<i18n>
{
  "en": {
    "packet": "Packet",
    "dir_master_to_slave": "Master → Slave 0x{slave}",
    "dir_slave_to_master": "Slave 0x{slave} → Master",
    "decoded": "DECODED",
    "close": "Close",
    "raw_bytes": "RAW BYTES",
    "data_interpretation": "DATA INTERPRETATION",
    "interpret_data": "Interpret data",
    "bit_16": "16-bit",
    "bit_32": "32-bit",
    "need_4_bytes": "Need ≥ 4 bytes for 32-bit interpretation",
    "th_reg": "Reg",
    "th_hex": "Hex",
    "th_uint16": "UInt16",
    "th_int16": "Int16",
    "th_bytes": "Bytes",
    "th_uint32": "UInt32",
    "th_int32": "Int32",
    "th_float32": "Float32"
  },
  "ru": {
    "packet": "Пакет",
    "dir_master_to_slave": "Master → Slave 0x{slave}",
    "dir_slave_to_master": "Slave 0x{slave} → Master",
    "decoded": "РАЗОБРАНО",
    "close": "Закрыть",
    "raw_bytes": "СЫРЫЕ БАЙТЫ",
    "data_interpretation": "ИНТЕРПРЕТАЦИЯ ДАННЫХ",
    "interpret_data": "Интерпретировать данные",
    "bit_16": "16 бит",
    "bit_32": "32 бита",
    "need_4_bytes": "Нужно ≥ 4 байт для 32-битной интерпретации",
    "th_reg": "Рег",
    "th_hex": "Hex",
    "th_uint16": "UInt16",
    "th_int16": "Int16",
    "th_bytes": "Байты",
    "th_uint32": "UInt32",
    "th_int32": "Int32",
    "th_float32": "Float32"
  },
  "kk": {
    "packet": "Пакет",
    "dir_master_to_slave": "Master → Slave 0x{slave}",
    "dir_slave_to_master": "Slave 0x{slave} → Master",
    "decoded": "ТАЛДАНҒАН",
    "close": "Жабу",
    "raw_bytes": "ШИКІ БАЙТТАР",
    "data_interpretation": "ДЕРЕКТЕРДІ ТАЛДАУ",
    "interpret_data": "Деректерді талдау",
    "bit_16": "16 бит",
    "bit_32": "32 бит",
    "need_4_bytes": "32-биттік талдау үшін ≥ 4 байт қажет",
    "th_reg": "Рег",
    "th_hex": "Hex",
    "th_uint16": "UInt16",
    "th_int16": "Int16",
    "th_bytes": "Байттар",
    "th_uint32": "UInt32",
    "th_int32": "Int32",
    "th_float32": "Float32"
  },
  "it": {
    "packet": "Pacchetto",
    "dir_master_to_slave": "Master → Slave 0x{slave}",
    "dir_slave_to_master": "Slave 0x{slave} → Master",
    "decoded": "DECODIFICATO",
    "close": "Chiudi",
    "raw_bytes": "BYTE GREZZI",
    "data_interpretation": "INTERPRETAZIONE DATI",
    "interpret_data": "Interpreta dati",
    "bit_16": "16 bit",
    "bit_32": "32 bit",
    "need_4_bytes": "Servono ≥ 4 byte per l'interpretazione a 32 bit",
    "th_reg": "Reg",
    "th_hex": "Hex",
    "th_uint16": "UInt16",
    "th_int16": "Int16",
    "th_bytes": "Byte",
    "th_uint32": "UInt32",
    "th_int32": "Int32",
    "th_float32": "Float32"
  },
  "de": {
    "packet": "Paket",
    "dir_master_to_slave": "Master → Slave 0x{slave}",
    "dir_slave_to_master": "Slave 0x{slave} → Master",
    "decoded": "DEKODIERT",
    "close": "Schließen",
    "raw_bytes": "ROHBYTES",
    "data_interpretation": "DATENINTERPRETATION",
    "interpret_data": "Daten interpretieren",
    "bit_16": "16-Bit",
    "bit_32": "32-Bit",
    "need_4_bytes": "≥ 4 Bytes für 32-Bit-Interpretation erforderlich",
    "th_reg": "Reg",
    "th_hex": "Hex",
    "th_uint16": "UInt16",
    "th_int16": "Int16",
    "th_bytes": "Bytes",
    "th_uint32": "UInt32",
    "th_int32": "Int32",
    "th_float32": "Float32"
  }
}
</i18n>
