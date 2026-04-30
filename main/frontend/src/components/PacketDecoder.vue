<script setup lang="ts">
import { computed, ref } from 'vue'
import { decodePacket, parseHex, type DecodedPacket, type Direction } from '@/common/modbusDecoder'

// ============================================================
// Props
// ============================================================

interface SniffRowLike {
  id: number
  sender: string   // 'MASTER' | 'SLAVE' | 'ERR' | 'TIMEOUT'
  slave: string    // hex string e.g. '01'
  fc: string       // display name
  pl: string       // hex bytes space-separated
  bytes: number
  crc: 'OK' | 'ERR' | 'N/A'
  isArbitration: boolean
  t: string
  dt: string
}

const props = defineProps<{ packet: SniffRowLike }>()

// ============================================================
// Decoder state
// ============================================================

const hoveredRawRange = ref<{ start: number; end: number } | null>(null)
type EndiannessKey = 'abcd' | 'cdab' | 'badc' | 'dcba'
const activeEndianness = ref<EndiannessKey>('abcd')

function setEndianness(key: string) {
  activeEndianness.value = key as EndiannessKey
}

const direction = computed<Direction>(() =>
  props.packet.sender === 'MASTER' ? 'request' : 'response'
)

const rawBytes = computed<number[]>(() => parseHex(props.packet.pl) ?? [])

const decoded = computed<DecodedPacket>(() =>
  decodePacket(props.packet.pl, direction.value)
)

// ============================================================
// Human-readable labels
// ============================================================

const TYPE_LABELS: Record<string, string> = {
  rtu_frame: 'RTU Frame',
  arbitration: 'Arbitration',
  parse_error: 'Parse Error',
  fast_modbus: 'Fast Modbus',
  modbus_rtu: 'Modbus RTU',
  scan_start: 'Scan Start (0x01)',
  scan_continue: 'Scan Continue (0x02)',
  scan_response: 'Scan Response (0x03)',
  scan_end: 'Scan End (0x04)',
  command_by_serial: 'Command by Serial (0x08)',
  response_by_serial: 'Response by Serial (0x09)',
  read_coils: 'FC01 Read Coils',
  read_discrete_inputs: 'FC02 Read Discrete Inputs',
  read_holding_registers: 'FC03 Read Holding Registers',
  read_input_registers: 'FC04 Read Input Registers',
  write_single_coil: 'FC05 Write Single Coil',
  write_single_register: 'FC06 Write Single Register',
  read_exception_status: 'FC07 Read Exception Status',
  diagnostics: 'FC08 Diagnostics',
  get_comm_event_counter: 'FC0B Comm Event Counter',
  get_comm_event_log: 'FC0C Comm Event Log',
  write_multiple_coils: 'FC0F Write Multiple Coils',
  write_multiple_registers: 'FC10 Write Multiple Registers',
  report_server_id: 'FC11 Report Server ID',
  read_file_record: 'FC14 Read File Record',
  write_file_record: 'FC15 Write File Record',
  mask_write_register: 'FC16 Mask Write Register',
  read_write_multiple_registers: 'FC17 Read/Write Multiple Registers',
  read_fifo_queue: 'FC18 Read FIFO Queue',
  mei_transport: 'FC2B MEI',
  mei_read_device_identification: 'FC2B/0E Read Device ID',
  mei_canopen: 'FC2B/0D CANopen',
  modbus_error: 'Modbus Exception',
  user_defined: 'User-Defined FC',
  vendor_specific: 'Vendor-Specific FC',
  read_coils_response: 'FC01 Read Coils Response',
  read_discrete_inputs_response: 'FC02 Read Discrete Inputs Response',
  read_holding_registers_response: 'FC03 Read Holding Registers Response',
  read_input_registers_response: 'FC04 Read Input Registers Response',
  write_single_coil_response: 'FC05 Write Single Coil Echo',
  write_single_register_response: 'FC06 Write Single Register Echo',
  read_exception_status_response: 'FC07 Exception Status Response',
  diagnostics_response: 'FC08 Diagnostics Response',
  get_comm_event_counter_response: 'FC0B Comm Event Counter Response',
  get_comm_event_log_response: 'FC0C Comm Event Log Response',
  write_multiple_coils_response: 'FC0F Write Multiple Coils Response',
  write_multiple_registers_response: 'FC10 Write Multiple Registers Response',
  report_server_id_response: 'FC11 Report Server ID Response',
  read_file_record_response: 'FC14 Read File Record Response',
  mask_write_register_response: 'FC16 Mask Write Register Echo',
  read_write_multiple_registers_response: 'FC17 Read/Write Multiple Registers Response',
  read_fifo_queue_response: 'FC18 Read FIFO Queue Response',
  mei_read_device_identification_response: 'FC2B/0E Device ID Response',
}

const FIELD_LABELS: Record<string, string> = {
  address: 'Slave address',
  crc: 'CRC',
  ext_byte: 'Ext byte',
  serial_number: 'Serial number',
  modbus_address: 'Modbus address',
  fc: 'Function code',
  register: 'Register',
  count: 'Count',
  value: 'Value',
  byte_count: 'Byte count',
  data: 'Data',
  sub_function: 'Sub-function',
  status: 'Status',
  event_count: 'Event count',
  message_count: 'Message count',
  events: 'Events',
  read_register: 'Read register',
  read_count: 'Read count',
  write_register: 'Write register',
  write_count: 'Write count',
  write_byte_count: 'Write byte count',
  write_data: 'Write data',
  fifo_pointer: 'FIFO pointer',
  fifo_count: 'FIFO count',
  mei_type: 'MEI type',
  read_device_id_code: 'Read Device ID code',
  object_id: 'Object ID',
  conformity_level: 'Conformity level',
  more_follows: 'More follows',
  next_object_id: 'Next object ID',
  number_of_objects: 'Num objects',
  original_fc: 'Original FC',
  error_code: 'Exception code',
  reason: 'Reason',
  output_data: 'Output data',
  server_id: 'Server ID',
  run_indicator: 'Run indicator',
  additional_data: 'Additional data',
  and_mask: 'AND mask',
  or_mask: 'OR mask',
  request_data_length: 'Request data len',
  resp_data_length: 'Response data len',
  subcommand: 'Subcommand',
  reference_type: 'Reference type',
  file_number: 'File number',
  record_number: 'Record number',
  record_length: 'Record length',
  file_resp_length: 'File resp len',
}

function typeLabel(type: string): string {
  return TYPE_LABELS[type] ?? type
}

function fieldLabel(key: string): string {
  return FIELD_LABELS[key] ?? key
}

// ============================================================
// Tree flattening — produce rows for the decoded column
// ============================================================

interface TreeRow {
  depth: number;
  label: string;
  key?: string;
  value?: string;
  byteStart: number;   // byte offset in full packet
  byteEnd: number;     // exclusive
  isSection: boolean;
  isField: boolean;
  isError: boolean;
  isArray?: boolean;
  arrayItems?: { label: string; value: string; }[];
}

/** Compute byte range from node's raw hex string within full packet hex. */
function rawToRange(nodeRaw: string, fullHex: string, searchFrom: number): { start: number; end: number } {
  if (!nodeRaw || nodeRaw.length === 0) return { start: searchFrom, end: searchFrom };
  const nodeUpper = nodeRaw.toUpperCase();
  // Search only within the parent's range by using the known offset
  const idx = fullHex.indexOf(nodeUpper, searchFrom * 2);
  if (idx === -1 || idx % 2 !== 0) return { start: searchFrom, end: searchFrom };
  return { start: idx / 2, end: idx / 2 + nodeUpper.length / 2 };
}

const SKIP_FIELDS = new Set(['type', 'raw', 'payload', 'objects', 'sub_requests', 'sub_responses'])

/**
 * Flatten a decoded node into tree rows.
 * fullHex: hex string of the full packet (for computing byte offsets).
 * parentStart: byte offset of parent node start (to avoid finding wrong match).
 */
function flattenNode(obj: Record<string, unknown>, depth: number, fullHex: string, parentStart: number): TreeRow[] {
  const rows: TreeRow[] = []
  const t = (obj.type as string) ?? '?'
  const nodeRaw = (obj.raw as string) ?? ''

  // Compute byte range for this node
  const range = rawToRange(nodeRaw, fullHex, parentStart)

  // Section header
  rows.push({ depth, label: typeLabel(t), byteStart: range.start, byteEnd: range.end, isSection: true, isField: false, isError: t === 'parse_error' })

  // Scalar fields — share the same byte range as parent node
  for (const [k, v] of Object.entries(obj)) {
    if (SKIP_FIELDS.has(k) || k === 'reserved_address') continue
    if (typeof v === 'object') continue
    rows.push({ depth: depth + 1, label: fieldLabel(k), key: k, value: String(v), byteStart: range.start, byteEnd: range.end, isSection: false, isField: true, isError: k === 'reason' })
  }

  if ((obj as Record<string, unknown>).reserved_address) {
    rows.push({ depth: depth + 1, label: 'Warning', value: 'Reserved address (0xF8–0xFF)', byteStart: range.start, byteEnd: range.end, isSection: false, isField: true, isError: true })
  }

  // Arrays
  for (const ak of ['sub_requests', 'sub_responses', 'objects'] as const) {
    const arr = obj[ak]
    if (!Array.isArray(arr)) continue
    rows.push({ depth: depth + 1, label: `${ak.replace(/_/g, ' ')} (${arr.length})`, byteStart: range.start, byteEnd: range.end, isSection: false, isField: false, isError: false, isArray: true, arrayItems: arr.map((item: Record<string, unknown>, i: number) => ({ label: `[${i}]`, value: Object.entries(item).filter(([, v]) => typeof v !== 'object').map(([k, v]) => `${fieldLabel(k)}: ${v}`).join(' · ') })) })
  }

  // Recurse into payload (starting search from this node's start to stay within range)
  if (obj.payload && typeof obj.payload === 'object') {
    rows.push(...flattenNode(obj.payload as Record<string, unknown>, depth + 1, fullHex, range.start))
  }

  return rows
}

const fullHex = computed<string>(() =>
  rawBytes.value.map(b => b.toString(16).toUpperCase().padStart(2, '0')).join('')
)

const treeRows = computed<TreeRow[]>(() => {
  const d = decoded.value
  return flattenNode(d as unknown as Record<string, unknown>, 0, fullHex.value, 0)
})

// ============================================================
// Hover — highlight bytes for hovered tree row
// ============================================================

function onRowHover(byteStart: number, byteEnd: number) {
  if (byteEnd > byteStart) {
    hoveredRawRange.value = { start: byteStart, end: byteEnd }
  } else {
    hoveredRawRange.value = null
  }
}

function onRowLeave() {
  hoveredRawRange.value = null
}

// ============================================================
// Hex editor (right panel)
// ============================================================

function isByteHighlighted(i: number): boolean {
  if (!hoveredRawRange.value) return false
  return i >= hoveredRawRange.value.start && i < hoveredRawRange.value.end
}

function isPrintable(b: number): boolean {
  return b >= 0x20 && b < 0x7f
}

const HEX_ROW = 16

interface HexEditorRow {
  offset: string
  bytes: { hex: string; ascii: string; printable: boolean; index: number }[]
  padCount: number
}

const hexEditorRows = computed<HexEditorRow[]>(() => {
  const rows: HexEditorRow[] = []
  const b = rawBytes.value
  for (let row = 0; row < b.length; row += HEX_ROW) {
    const chunk = b.slice(row, row + HEX_ROW)
    rows.push({
      offset: row.toString(16).toUpperCase().padStart(8, '0'),
      bytes: chunk.map((byte, ci) => ({
        hex: byte.toString(16).toUpperCase().padStart(2, '0'),
        ascii: isPrintable(byte) ? String.fromCharCode(byte) : '.',
        printable: isPrintable(byte),
        index: row + ci,
      })),
      padCount: HEX_ROW - chunk.length,
    })
  }
  return rows
})

// ============================================================
// Data panel: 32-bit endianness for the leaf `data` field
// ============================================================

const leafData = computed<string | null>(() => {
  // Walk the full payload chain to the deepest node, then look for data fields.
  // This ensures we get the PDU-level data (e.g. register values) not
  // the command_by_serial wrapper's raw, which would include the serial number.
  function findDeepestData(obj: Record<string, unknown>): string | null {
    // Recurse into payload first — prefer deeper data
    if (obj.payload && typeof obj.payload === 'object') {
      const deeper = findDeepestData(obj.payload as Record<string, unknown>)
      if (deeper !== null) return deeper
    }
    // Check for data fields at this level (only if no payload / or payload has no data)
    const DATA_KEYS = ['data', 'write_data', 'events', 'additional_data']
    for (const k of DATA_KEYS) {
      const v = obj[k]
      if (typeof v === 'string' && v.length >= 4) return v
    }
    return null
  }
  return findDeepestData(decoded.value as unknown as Record<string, unknown>)
})

const leafBytes = computed<number[]>(() => {
  const hex = leafData.value
  if (!hex) return []
  const bytes: number[] = []
  for (let i = 0; i < hex.length; i += 2) bytes.push(parseInt(hex.slice(i, i + 2), 16))
  return bytes
})

const regs16 = computed<{ index: number; dec: number; hex: string }[]>(() => {
  const b = leafBytes.value
  if (b.length < 2) return []
  const out = []
  for (let i = 0; i + 1 < b.length; i += 2) {
    const v = ((b[i] << 8) | b[i + 1]) >>> 0
    out.push({ index: i / 2, dec: v, hex: v.toString(16).toUpperCase().padStart(4, '0') })
  }
  return out
})

interface Chunk32 {
  offset: string
  hex: string
  uint32: number
  int32: number
  float32: string
}

const ENDIAN_CONFIGS = {
  abcd: { label: 'AB CD', desc: 'Big Endian',    fn: (b: number[], i: number) => ((b[i] << 24) | (b[i+1] << 16) | (b[i+2] << 8) | b[i+3]) >>> 0 },
  cdab: { label: 'CD AB', desc: 'Mid-Little',    fn: (b: number[], i: number) => ((b[i+2] << 24) | (b[i+3] << 16) | (b[i] << 8) | b[i+1]) >>> 0 },
  badc: { label: 'BA DC', desc: 'Byte-swap',     fn: (b: number[], i: number) => ((b[i+1] << 24) | (b[i] << 16) | (b[i+3] << 8) | b[i+2]) >>> 0 },
  dcba: { label: 'DC BA', desc: 'Little Endian', fn: (b: number[], i: number) => ((b[i+3] << 24) | (b[i+2] << 16) | (b[i+1] << 8) | b[i]) >>> 0 },
}

function f32str(u: number): string {
  const view = new DataView(new ArrayBuffer(4))
  view.setUint32(0, u, false)
  const f = view.getFloat32(0, false)
  if (isNaN(f)) return 'NaN'
  if (Math.abs(f) < 1e-6 && f !== 0) return f.toExponential(4)
  return parseFloat(f.toPrecision(7)).toString()
}

const chunks32 = computed<Chunk32[]>(() => {
  const b = leafBytes.value
  const cfg = ENDIAN_CONFIGS[activeEndianness.value]
  if (b.length < 4) return []
  const out: Chunk32[] = []
  for (let i = 0; i + 3 < b.length; i += 4) {
    const u = cfg.fn(b, i)
    const s = u >= 0x80000000 ? -(0x100000000 - u) : u
    out.push({ offset: `${i}..${i + 3}`, hex: '0x' + u.toString(16).toUpperCase().padStart(8, '0'), uint32: u, int32: s, float32: f32str(u) })
  }
  return out
})

const hasDataPanel = computed(() => leafBytes.value.length >= 2)
</script>

<template>
  <div class="pkt-panel">
    <!-- Header -->
    <div class="pkt-header">
      <div class="pkt-header-left">
        <span class="pkt-title">Packet #{{ packet.id }}</span>
        <span :class="['sender-pill', 'sender-' + packet.sender.toLowerCase()]">{{ packet.sender }}</span>
        <span class="pkt-dir muted">
          <template v-if="packet.sender === 'MASTER'">Master → Slave 0x{{ packet.slave }}</template>
          <template v-else-if="packet.sender === 'SLAVE'">Slave 0x{{ packet.slave }} → Master</template>
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
        <div class="pkt-col-label">DECODED</div>
        <div class="tree-rows">
          <div
            v-for="(row, i) in treeRows"
            :key="i"
            class="tree-row"
            :style="{ paddingLeft: `${row.depth * 14 + 8}px` }"
            @mouseenter="onRowHover(row.byteStart, row.byteEnd)"
            @mouseleave="onRowLeave"
          >
            <!-- Section header -->
            <template v-if="row.isSection">
              <span :class="['tree-type', { 'tree-type-error': row.isError }]">{{ row.label }}</span>
            </template>

            <!-- Field row -->
            <template v-else-if="row.isField">
              <span class="tree-key">{{ row.label }}</span>
              <span :class="['tree-val', { 'tree-val-error': row.isError }]">{{ row.value }}</span>
            </template>

            <!-- Array -->
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

        <!-- Data interpretation panel -->
        <div v-if="hasDataPanel" class="data-panel">
          <div class="pkt-col-label" style="margin-top: 12px">DATA INTERPRETATION</div>

          <!-- 16-bit registers -->
          <div v-if="regs16.length > 0" class="regs16">
            <span class="data-label">16-bit BE registers:</span>
            <span v-for="r in regs16" :key="r.index" class="reg16-chip">
              R{{ r.index }} <b>{{ r.dec }}</b> <span class="muted">0x{{ r.hex }}</span>
            </span>
          </div>

          <!-- 32-bit endianness tabs -->
          <div v-if="leafBytes.length >= 4">
            <div class="endian-tabs">
              <button
                v-for="(cfg, key) in ENDIAN_CONFIGS"
                :key="key"
                :class="['endian-tab', { active: activeEndianness === key }]"
                @click="setEndianness(key)"
              >{{ cfg.label }} <span class="muted" style="font-size:11px">{{ cfg.desc }}</span></button>
            </div>
            <table v-if="chunks32.length > 0" class="chunks32-table">
              <thead>
                <tr>
                  <th>Bytes</th>
                  <th>Hex</th>
                  <th>UInt32</th>
                  <th>Int32</th>
                  <th>Float32</th>
                </tr>
              </thead>
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
          </div>
        </div>
      </div>

      <!-- Right: raw bytes hex editor -->
      <div class="pkt-raw">
        <div class="pkt-col-label">RAW BYTES · {{ packet.bytes }} B</div>
        <div class="hexed">
          <div v-for="(row, ri) in hexEditorRows" :key="ri" class="hexed-row">
            <span class="hexed-off">{{ row.offset }}</span>
            <!-- hex bytes, split in two groups of 8 -->
            <span class="hexed-hex">
              <span v-for="(b, bi) in row.bytes" :key="b.index">
                <span v-if="bi === 8" class="hexed-gap"> </span>
                <span :class="['hexed-byte', { 'hexed-byte-hi': isByteHighlighted(b.index) }]">{{ b.hex }}</span>
              </span>
              <!-- padding for incomplete last row -->
              <span v-if="row.padCount > 0">
                <span v-if="row.bytes.length <= 8 && row.bytes.length + row.padCount > 8" class="hexed-gap"> </span>
                <span v-for="pi in row.padCount" :key="pi" class="hexed-pad">   </span>
              </span>
            </span>
            <!-- ascii -->
            <span class="hexed-ascii">
              <span v-for="b in row.bytes" :key="b.index" :class="['hexed-ch', { 'hexed-ch-print': b.printable, 'hexed-ch-dot': !b.printable, 'hexed-byte-hi': isByteHighlighted(b.index) }]">{{ b.ascii }}</span>
            </span>
          </div>
        </div>
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

.pkt-title {
  font-size: 13px;
  font-weight: 600;
  white-space: nowrap;
}

.pkt-dir {
  font-size: 12px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.pkt-meta {
  font-size: 11px;
  white-space: nowrap;
  flex-shrink: 0;
}

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
  width: 380px;
  flex-shrink: 0;
  overflow-y: auto;
  padding: 10px 16px;
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
.tree-rows {
  font-size: 12.5px;
}

.tree-row {
  display: flex;
  align-items: baseline;
  gap: 8px;
  padding-top: 2px;
  padding-bottom: 2px;
  padding-right: 14px;
  border-radius: 3px;
  transition: background 0.08s;
}

.tree-row:hover {
  background: var(--bg-surface);
}

.tree-type {
  font-weight: 600;
  color: var(--text-secondary);
  font-size: 12px;
}

.tree-type-error {
  color: var(--mb-err);
}

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
}

.tree-val-error {
  color: var(--mb-err);
}

.array-items {
  display: flex;
  flex-direction: column;
  gap: 1px;
}

.array-item {
  display: flex;
  gap: 6px;
  font-size: 11.5px;
}

/* Data panel */
.data-panel {
  padding: 8px 14px 0;
  border-top: 1px solid var(--border-color);
  margin-top: 10px;
}

.data-label {
  font-size: 11px;
  color: var(--text-muted);
  margin-right: 8px;
}

.regs16 {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 6px;
  margin: 4px 0 8px;
  font-size: 12px;
}

.reg16-chip {
  background: var(--bg-surface);
  border: 1px solid var(--border-color);
  border-radius: 4px;
  padding: 2px 7px;
  font-family: var(--font-mono);
  font-size: 12px;
}

/* Endianness tabs */
.endian-tabs {
  display: flex;
  gap: 4px;
  flex-wrap: wrap;
  margin-bottom: 6px;
}

.endian-tab {
  padding: 3px 10px;
  font-size: 12px;
  font-family: var(--font-mono);
  background: transparent;
  border: 1px solid var(--border-color);
  border-radius: 4px;
  cursor: pointer;
  color: var(--text-muted);
  transition: background 0.1s, border-color 0.1s, color 0.1s;
}

.endian-tab:hover {
  background: var(--bg-surface);
  color: var(--text-color);
  border-color: var(--border-strong);
}

.endian-tab.active {
  background: color-mix(in oklch, var(--primary-color) 8%, var(--bg-surface));
  border-color: var(--primary-color);
  color: var(--primary-color);
  font-weight: 600;
}

.chunks32-table {
  width: 100%;
  border-collapse: collapse;
  font-size: 12px;
  margin-bottom: 8px;
}

.chunks32-table th {
  font-size: 10px;
  text-transform: uppercase;
  letter-spacing: 0.06em;
  color: var(--text-muted);
  font-weight: 500;
  text-align: left;
  padding: 3px 6px;
  border-bottom: 1px solid var(--border-color);
}

.chunks32-table td {
  padding: 3px 6px;
  border-bottom: 1px solid color-mix(in oklch, var(--border-color) 50%, transparent);
}

.c-hex { color: var(--mb-hex-slot); }
.c-u32 { color: var(--mb-ok); }
.c-i32 { color: var(--mb-slave); }
.c-f32 { color: var(--mb-master); }

/* Hex editor */
.hexed {
  font-family: var(--font-mono);
  font-size: 12px;
  line-height: 1.7;
}

.hexed-row {
  display: flex;
  gap: 10px;
  white-space: pre;
}

.hexed-off {
  color: var(--text-muted);
  flex-shrink: 0;
}

.hexed-hex {
  flex-shrink: 0;
}

.hexed-gap {
  display: inline-block;
  width: 8px;
}

.hexed-byte {
  display: inline-block;
  width: 22px;
  color: var(--text-secondary);
  transition: background 0.08s, color 0.08s;
}

.hexed-byte-hi {
  background: color-mix(in oklch, var(--primary-color) 20%, transparent);
  color: var(--primary-color);
  border-radius: 2px;
}

.hexed-pad {
  display: inline-block;
  width: 22px;
}

.hexed-ascii {
  color: var(--text-muted);
  flex-shrink: 0;
}

.hexed-ch {
  display: inline-block;
  width: 9px;
}

.hexed-ch-print {
  color: var(--text-color);
}

.hexed-ch-dot {
  color: var(--text-muted);
}

/* Sender pills — reuse from Sniffer.vue via global or repeat here */
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

.crc-err { color: var(--mb-err); font-weight: 600; }
.crc-ok  { color: var(--mb-ok); }
.muted   { color: var(--text-muted); }
.mono    { font-family: var(--font-mono); }
</style>
