<script setup lang="ts">
import { computed, ref } from 'vue'
import { decodePacket, parseHex, type DecodedPacket, type Direction } from '@/common/modbusDecoder'

// ============================================================
// Props
// ============================================================

interface SniffRowLike {
  id: number
  sender: string
  slave: string
  fc: string
  pl: string
  bytes: number
  crc: 'OK' | 'ERR' | 'N/A'
  isArbitration: boolean
  t: string
  dt: string
}

const props = defineProps<{ packet: SniffRowLike }>()

// ============================================================
// State
// ============================================================

const hoveredRange = ref<{ start: number; end: number } | null>(null)
type EndiannessKey = 'abcd' | 'cdab' | 'badc' | 'dcba'
const activeEndianness = ref<EndiannessKey>('abcd')
type BitMode = '16' | '32'
const activeBitMode = ref<BitMode>('16')
const showDataPopup = ref(false)

function setEndianness(key: string) { activeEndianness.value = key as EndiannessKey }

const direction = computed<Direction>(() =>
  props.packet.sender === 'MASTER' ? 'request' : 'response'
)

const rawBytes = computed<number[]>(() => parseHex(props.packet.pl) ?? [])

const decoded = computed<DecodedPacket>(() =>
  decodePacket(props.packet.pl, direction.value)
)

const fullHex = computed<string>(() =>
  rawBytes.value.map(b => b.toString(16).toUpperCase().padStart(2, '0')).join('')
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

// Mapping from FC hex value to human-readable name (for function_code field display)
const FC_DISPLAY_NAMES: Record<string, string> = {
  '01': 'Read Coils', '02': 'Read Discrete Inputs',
  '03': 'Read Holding Registers', '04': 'Read Input Registers',
  '05': 'Write Single Coil', '06': 'Write Single Register',
  '07': 'Read Exception Status', '08': 'Diagnostics',
  '0B': 'Get Comm Event Counter', '0C': 'Get Comm Event Log',
  '0F': 'Write Multiple Coils', '10': 'Write Multiple Registers',
  '11': 'Report Server ID', '14': 'Read File Record',
  '15': 'Write File Record', '16': 'Mask Write Register',
  '17': 'Read/Write Multiple Registers', '18': 'Read FIFO Queue',
  '2B': 'MEI Transport',
}

// Fields whose values are plain hex strings — display with 0x prefix
const HEX_FIELDS = new Set([
  'address', 'crc', 'ext_byte', 'serial_number', 'modbus_address',
  'fc', 'register', 'value', 'data', 'sub_function', 'events',
  'read_register', 'write_register', 'write_data', 'additional_data',
  'fifo_pointer', 'mei_type', 'read_device_id_code', 'object_id',
  'conformity_level', 'more_follows', 'next_object_id',
  'original_fc', 'output_data', 'server_id', 'run_indicator',
  'and_mask', 'or_mask', 'subcommand', 'reference_type',
])

// Fields that also need decimal shown in parentheses
const DEC_ALSO = new Set(['serial_number', 'modbus_address', 'address', 'register', 'read_register', 'write_register', 'value', 'fifo_pointer'])

// Well-known special addresses with human-readable names
const ADDR_NOTES: Record<number, string> = {
  0x00: 'broadcast',
  0xFD: 'Fast Modbus broadcast',
  0xF8: 'reserved',
  0xF9: 'reserved',
  0xFA: 'reserved',
  0xFB: 'reserved',
  0xFC: 'reserved',
  0xFE: 'reserved',
  0xFF: 'FM Bus Arbitration',
}

// All lookup tables use UPPERCASE hex keys; all lookups use .toUpperCase()
const EXT_BYTE_NAMES: Record<string, string> = {
  '60': 'Extended function command (legacy)',
  '46': 'Extended function command',
}

const FM_SUBCOMMAND_NAMES: Record<string, string> = {
  '01': 'Scan Start',
  '02': 'Scan Continue',
  '03': 'Scan Response',
  '04': 'Scan End',
  '08': 'Command by Serial',
  '09': 'Response by Serial',
  '10': 'Event Request',
  '11': 'Event Transfer',
  '12': 'Event Confirm',
  '18': 'Event Config',
}
// Note: single-digit hex values like '60' are 2-char and match toUpperCase() output directly

function fmtVal(key: string, raw: string): string {
  if (key === 'function_code' && /^[0-9A-Fa-f]+$/.test(raw)) {
    const upper = raw.toUpperCase()
    const name = FC_DISPLAY_NAMES[upper] ?? 'Unknown'
    return `0x${upper} (${name})`
  }
  if (key === 'ext_byte' && /^[0-9A-Fa-f]+$/.test(raw)) {
    const upper = raw.toUpperCase()
    const name = EXT_BYTE_NAMES[upper] ?? 'Unknown'
    return `0x${upper} (${name})`
  }
  if (key === 'subcommand' && /^[0-9A-Fa-f]+$/.test(raw)) {
    const upper = raw.toUpperCase()
    const name = FM_SUBCOMMAND_NAMES[upper] ?? 'Unknown'
    return `0x${upper} (${name})`
  }
  if (HEX_FIELDS.has(key) && /^[0-9A-Fa-f]+$/.test(raw)) {
    const hex = `0x${raw.toUpperCase()}`
    if (DEC_ALSO.has(key)) {
      const dec = parseInt(raw, 16)
      const note = (key === 'address' || key === 'modbus_address') ? ADDR_NOTES[dec] : undefined
      const suffix = note ? ` ${note}` : ''
      return `${hex} (${dec}${suffix})`
    }
    return hex
  }
  return raw
}

const FIELD_LABELS: Record<string, string> = {
  address: 'Slave address', crc: 'CRC',  ext_byte: 'FM Command',
  subcommand: 'FM Subcommand',
  serial_number: 'Serial number', modbus_address: 'Modbus address',
  fc: 'Function code', function_code: 'Function', register: 'Register', count: 'Count', value: 'Value',
  byte_count: 'Byte count', data: 'Data', sub_function: 'Sub-function',
  status: 'Status', event_count: 'Event count', message_count: 'Message count',
  events: 'Events', read_register: 'Read register', read_count: 'Read count',
  write_register: 'Write register', write_count: 'Write count',
  write_byte_count: 'Write byte count', write_data: 'Write data',
  fifo_pointer: 'FIFO pointer', fifo_count: 'FIFO count', mei_type: 'MEI type',
  read_device_id_code: 'Read Device ID code', object_id: 'Object ID',
  conformity_level: 'Conformity level', more_follows: 'More follows',
  next_object_id: 'Next object ID', number_of_objects: 'Num objects',
  original_fc: 'Original FC', error_code: 'Exception code', reason: 'Reason',
  output_data: 'Output data', server_id: 'Server ID', run_indicator: 'Run indicator',
  additional_data: 'Additional data', and_mask: 'AND mask', or_mask: 'OR mask',
  request_data_length: 'Request data len', resp_data_length: 'Response data len',
  reference_type: 'Reference type',
  file_number: 'File number', record_number: 'Record number',
  record_length: 'Record length', file_resp_length: 'File resp len',
}

// ============================================================
// Tree — flatten decoded object into rows
// ============================================================

interface TreeRow {
  depth: number;
  label: string;
  key?: string;
  value?: string;
  byteStart: number;
  byteEnd: number;
  isSection: boolean;
  isField: boolean;
  isError: boolean;
  isDataField?: boolean;  // the "data" field — show magnifier icon
  isArray?: boolean;
  arrayItems?: { label: string; value: string; }[];
}

/**
 * Find byte range of nodeRaw within the full packet hex,
 * starting search at parentStart to avoid false matches.
 */
function rawToRange(nodeRaw: string, fhex: string, parentStart: number): { start: number; end: number } {
  if (!nodeRaw) return { start: parentStart, end: parentStart }
  const upper = nodeRaw.toUpperCase()
  const idx = fhex.indexOf(upper, parentStart * 2)
  if (idx === -1 || idx % 2 !== 0) return { start: parentStart, end: parentStart }
  return { start: idx / 2, end: idx / 2 + upper.length / 2 }
}

/**
 * Compute per-field byte ranges within a PDU node.
 * Returns map of fieldName → {start, end} relative to fullHex.
 * We know the internal structure from the node type.
 */
function fieldRanges(obj: Record<string, unknown>, nodeByteStart: number): Record<string, { start: number; end: number }> {
  const r: Record<string, { start: number; end: number }> = {}
  const s = nodeByteStart
  const t = obj.type as string
  // For each known PDU structure, compute field offsets
  // Offset order corresponds to on-wire byte order
  if (['read_coils','read_discrete_inputs','read_holding_registers','read_input_registers'].includes(t)) {
    // request: FC(1) reg(2) count(2)
    r.fc = { start: s, end: s + 1 }
    r.register = { start: s + 1, end: s + 3 }
    r.count = { start: s + 3, end: s + 5 }
  } else if (['read_coils_response','read_discrete_inputs_response','read_holding_registers_response','read_input_registers_response'].includes(t)) {
    // response: FC(1) byteCount(1) data(N)
    r.fc = { start: s, end: s + 1 }
    r.byte_count = { start: s + 1, end: s + 2 }
    const bc = obj.byte_count as number ?? 0
    r.data = { start: s + 2, end: s + 2 + bc }
  } else if (['write_single_coil','write_single_register','write_single_coil_response','write_single_register_response'].includes(t)) {
    r.fc = { start: s, end: s + 1 }
    r.register = { start: s + 1, end: s + 3 }
    r.value = { start: s + 3, end: s + 5 }
  } else if (['write_multiple_coils','write_multiple_registers'].includes(t)) {
    r.fc = { start: s, end: s + 1 }
    r.register = { start: s + 1, end: s + 3 }
    r.count = { start: s + 3, end: s + 5 }
    r.byte_count = { start: s + 5, end: s + 6 }
    const bc = obj.byte_count as number ?? 0
    r.data = { start: s + 6, end: s + 6 + bc }
  } else if (['write_multiple_coils_response','write_multiple_registers_response'].includes(t)) {
    r.fc = { start: s, end: s + 1 }
    r.register = { start: s + 1, end: s + 3 }
    r.count = { start: s + 3, end: s + 5 }
  } else if (t === 'modbus_error') {
    r.fc = { start: s, end: s + 1 }
    r.original_fc = { start: s, end: s + 1 }
    r.error_code = { start: s + 1, end: s + 2 }
  } else if (t === 'scan_response') {
    // sub(1) serial(4) addr(1)
    r.serial_number = { start: s + 1, end: s + 5 }
    r.modbus_address = { start: s + 5, end: s + 6 }
  } else if (['command_by_serial','response_by_serial'].includes(t)) {
    r.serial_number = { start: s + 1, end: s + 5 }
  } else if (t === 'rtu_frame') {
    r.address = { start: s, end: s + 1 }
    const rawLen = (obj.raw as string ?? '').length / 2
    r.crc = { start: s + rawLen - 2, end: s + rawLen }
  }
  return r
}

// 'fc' is always shown as part of the section type label (e.g. "FC03 Read Holding Registers")
// so we skip it as a field to avoid duplication.
// 'reserved_address' is rendered separately with a warning message.
const SKIP_FIELDS = new Set(['type', 'raw', 'payload', 'objects', 'sub_requests', 'sub_responses', 'fc', 'reserved_address'])

function flattenNode(obj: Record<string, unknown>, depth: number, fhex: string, parentStart: number): TreeRow[] {
  const rows: TreeRow[] = []
  const t = (obj.type as string) ?? '?'
  const nodeRaw = (obj.raw as string) ?? ''
  const range = rawToRange(nodeRaw, fhex, parentStart)
  const fRanges = fieldRanges(obj, range.start)

  // Check if this node has any meaningful content beyond the type label itself
  const hasScalars = Object.entries(obj).some(([k, v]) => !SKIP_FIELDS.has(k) && typeof v !== 'object')
  const hasArrays = ['sub_requests', 'sub_responses', 'objects'].some(ak => Array.isArray(obj[ak]))
  const hasChild = obj.payload && typeof obj.payload === 'object'
  // Only add section header if node has content OR is top-level (depth 0)
  if (depth === 0 || hasScalars || hasArrays || hasChild) {
    rows.push({ depth, label: TYPE_LABELS[t] ?? t, byteStart: range.start, byteEnd: range.end, isSection: true, isField: false, isError: t === 'parse_error' })
  }

  for (const [k, v] of Object.entries(obj)) {
    if (SKIP_FIELDS.has(k)) continue
    if (typeof v === 'object') continue
    const fr = fRanges[k]
    const bStart = fr ? fr.start : range.start
    const bEnd = fr ? fr.end : range.end
    const isDataField = k === 'data' || k === 'write_data'
    rows.push({ depth: depth + 1, label: FIELD_LABELS[k] ?? k, key: k, value: fmtVal(k, String(v)), byteStart: bStart, byteEnd: bEnd, isSection: false, isField: true, isError: k === 'reason', isDataField })
  }

  if ((obj as Record<string, unknown>).reserved_address) {
    rows.push({ depth: depth + 1, label: 'Warning', value: 'Reserved address (0xF8–0xFF)', byteStart: range.start, byteEnd: range.end, isSection: false, isField: true, isError: true })
  }

  for (const ak of ['sub_requests', 'sub_responses', 'objects'] as const) {
    const arr = obj[ak]
    if (!Array.isArray(arr)) continue
    rows.push({ depth: depth + 1, label: `${ak.replace(/_/g, ' ')} (${arr.length})`, byteStart: range.start, byteEnd: range.end, isSection: false, isField: false, isError: false, isArray: true, arrayItems: arr.map((item: Record<string, unknown>, i: number) => ({ label: `[${i}]`, value: Object.entries(item).filter(([, v]) => typeof v !== 'object').map(([k, v]) => `${FIELD_LABELS[k] ?? k}: ${v}`).join(' · ') })) })
  }

  if (obj.payload && typeof obj.payload === 'object') {
    rows.push(...flattenNode(obj.payload as Record<string, unknown>, depth + 1, fhex, range.start))
  }

  return rows
}

const treeRows = computed<TreeRow[]>(() =>
  flattenNode(decoded.value as unknown as Record<string, unknown>, 0, fullHex.value, 0)
)

// ============================================================
// Hover
// ============================================================

function onRowHover(start: number, end: number) {
  hoveredRange.value = end > start ? { start, end } : null
}
function onRowLeave() { hoveredRange.value = null }

// ============================================================
// Hex editor — 8 bytes per row
// ============================================================

function isByteHighlighted(i: number): boolean {
  return hoveredRange.value !== null && i >= hoveredRange.value.start && i < hoveredRange.value.end
}

function isPrintable(b: number): boolean { return b >= 0x20 && b < 0x7f }

const HEX_ROW = 8

interface HexEditorRow {
  offset: string;
  bytes: { hex: string; ascii: string; printable: boolean; index: number; }[];
  padCount: number;
}

const hexEditorRows = computed<HexEditorRow[]>(() => {
  const b = rawBytes.value
  const out: HexEditorRow[] = []
  for (let row = 0; row < b.length; row += HEX_ROW) {
    const chunk = b.slice(row, row + HEX_ROW)
    out.push({
      offset: row.toString(16).toUpperCase().padStart(4, '0'),
      bytes: chunk.map((byte, ci) => ({
        hex: byte.toString(16).toUpperCase().padStart(2, '0'),
        ascii: isPrintable(byte) ? String.fromCharCode(byte) : '.',
        printable: isPrintable(byte),
        index: row + ci,
      })),
      padCount: HEX_ROW - chunk.length,
    })
  }
  return out
})

// ============================================================
// Data interpretation popup
// ============================================================

const leafData = computed<string | null>(() => {
  function findDeepest(obj: Record<string, unknown>): string | null {
    if (obj.payload && typeof obj.payload === 'object') {
      const deeper = findDeepest(obj.payload as Record<string, unknown>)
      if (deeper !== null) return deeper
    }
    for (const k of ['data', 'write_data', 'events', 'additional_data']) {
      const v = obj[k]
      if (typeof v === 'string' && v.length >= 4) return v
    }
    return null
  }
  return findDeepest(decoded.value as unknown as Record<string, unknown>)
})

const leafBytes = computed<number[]>(() => {
  const hex = leafData.value
  if (!hex) return []
  const out: number[] = []
  for (let i = 0; i < hex.length; i += 2) out.push(parseInt(hex.slice(i, i + 2), 16))
  return out
})

const regs16 = computed<{ index: number; dec: number; hex: string; }[]>(() => {
  const b = leafBytes.value
  const out = []
  for (let i = 0; i + 1 < b.length; i += 2) {
    const v = ((b[i] << 8) | b[i + 1]) >>> 0
    out.push({ index: i / 2, dec: v, hex: v.toString(16).toUpperCase().padStart(4, '0') })
  }
  return out
})

interface Chunk32 { offset: string; hex: string; uint32: number; int32: number; float32: string; }

const ENDIAN_CONFIGS: Record<EndiannessKey, { label: string; desc: string; fn: (b: number[], i: number) => number; }> = {
  abcd: { label: 'AB CD', desc: 'Big Endian',    fn: (b, i) => ((b[i] << 24) | (b[i+1] << 16) | (b[i+2] << 8) | b[i+3]) >>> 0 },
  cdab: { label: 'CD AB', desc: 'Mid-Little',    fn: (b, i) => ((b[i+2] << 24) | (b[i+3] << 16) | (b[i] << 8) | b[i+1]) >>> 0 },
  badc: { label: 'BA DC', desc: 'Byte-swap',     fn: (b, i) => ((b[i+1] << 24) | (b[i] << 16) | (b[i+3] << 8) | b[i+2]) >>> 0 },
  dcba: { label: 'DC BA', desc: 'Little Endian', fn: (b, i) => ((b[i+3] << 24) | (b[i+2] << 16) | (b[i+1] << 8) | b[i]) >>> 0 },
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
  const out: Chunk32[] = []
  for (let i = 0; i + 3 < b.length; i += 4) {
    const u = cfg.fn(b, i)
    const s = u >= 0x80000000 ? -(0x100000000 - u) : u
    out.push({ offset: `${i}..${i+3}`, hex: '0x' + u.toString(16).toUpperCase().padStart(8, '0'), uint32: u, int32: s, float32: f32str(u) })
  }
  return out
})
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
            <template v-if="row.isSection">
              <span :class="['tree-type', { 'tree-type-error': row.isError }]">{{ row.label }}</span>
            </template>

            <template v-else-if="row.isField">
              <span class="tree-key">{{ row.label }}</span>
              <span :class="['tree-val', { 'tree-val-error': row.isError }]">{{ row.value }}</span>
              <!-- Magnifier icon for data fields — opens interpretation popup -->
              <button v-if="row.isDataField && leafBytes.length >= 2" class="data-icon-btn" title="Interpret data" @click.stop="showDataPopup = !showDataPopup">
                <svg width="13" height="13" viewBox="0 0 16 16" fill="none">
                  <circle cx="6.5" cy="6.5" r="4.5" stroke="currentColor" stroke-width="1.5"/>
                  <line x1="10.5" y1="10.5" x2="14" y2="14" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>
                </svg>
              </button>
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
        <div class="pkt-col-label">RAW BYTES · {{ packet.bytes }} B</div>
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
        <span class="pkt-col-label" style="padding:0">DATA INTERPRETATION</span>
        <div class="bit-mode-tabs">
          <button :class="['bit-tab', { active: activeBitMode === '16' }]" @click="activeBitMode = '16'">16-bit</button>
          <button :class="['bit-tab', { active: activeBitMode === '32' }]" @click="activeBitMode = '32'">32-bit</button>
        </div>
        <button class="popup-close" @click="showDataPopup = false">✕</button>
      </div>

      <!-- 16-bit registers view -->
      <div v-if="activeBitMode === '16'" class="regs16-view">
        <div class="regs16-grid">
          <div v-for="r in regs16" :key="r.index" class="reg16-chip">
            <span class="muted reg-idx">R{{ r.index }}</span>
            <b class="reg-dec">{{ r.dec }}</b>
            <span class="muted">0x{{ r.hex }}</span>
          </div>
        </div>
      </div>

      <!-- 32-bit endianness view -->
      <div v-if="activeBitMode === '32'" class="view32">
        <div v-if="leafBytes.length < 4" class="muted" style="font-size:12px;padding:8px 0">Need ≥ 4 bytes for 32-bit interpretation</div>
        <template v-else>
          <div class="endian-tabs">
            <button
              v-for="(cfg, key) in ENDIAN_CONFIGS"
              :key="key"
              :class="['endian-tab', { active: activeEndianness === key }]"
              @click="setEndianness(key)"
            >{{ cfg.label }} <span class="muted" style="font-size:10px">{{ cfg.desc }}</span></button>
          </div>
          <table class="chunks32-table">
            <thead><tr><th>Bytes</th><th>Hex</th><th>UInt32</th><th>Int32</th><th>Float32</th></tr></thead>
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

/* Magnifier button */
.data-icon-btn {
  flex-shrink: 0;
  background: transparent;
  border: 1px solid transparent;
  color: var(--text-muted);
  cursor: pointer;
  padding: 2px 4px;
  border-radius: 4px;
  line-height: 0;
  transition: color 0.12s, border-color 0.12s;
}

.data-icon-btn:hover {
  color: var(--primary-color);
  border-color: var(--border-color);
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
  width: 480px;
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
.regs16-grid { display: flex; flex-wrap: wrap; gap: 6px; }
.reg16-chip {
  background: var(--bg-surface-subtle);
  border: 1px solid var(--border-color);
  border-radius: 4px;
  padding: 3px 8px;
  font-family: var(--font-mono);
  font-size: 12px;
  display: flex;
  gap: 5px;
  align-items: baseline;
}
.reg-idx { font-size: 10px; }
.reg-dec { color: var(--mb-ok); }

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
