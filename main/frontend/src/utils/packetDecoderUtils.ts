import { splitFunctionCode } from '@/common/modbusDecoder';

// Human-readable labels

export const TYPE_LABELS: Record<string, string> = {
  rtu_frame: 'RTU Frame',
  arbitration: 'Arbitration',
  parse_error: 'Parse Error',
  fast_modbus: 'Fast Modbus',
  modbus_rtu: 'Modbus RTU',
  scan_start: 'Scan Start',
  scan_continue: 'Scan Continue',
  scan_response: 'Scan Response',
  scan_end: 'Scan End',
  command_by_serial: 'Command by Serial',
  response_by_serial: 'Response by Serial',
  event_request: 'Event Request',
  event_transfer: 'Event Transfer',
  no_events: 'No Events',
  event_config: 'Event Config',
  read_coils: 'Read Coils',
  read_discrete_inputs: 'Read Discrete Inputs',
  read_holding_registers: 'Read Holding Registers',
  read_input_registers: 'Read Input Registers',
  write_single_coil: 'Write Single Coil',
  write_single_register: 'Write Single Register',
  read_exception_status: 'Read Exception Status',
  diagnostics: 'Diagnostics',
  get_comm_event_counter: 'Comm Event Counter',
  get_comm_event_log: 'Comm Event Log',
  write_multiple_coils: 'Write Multiple Coils',
  write_multiple_registers: 'Write Multiple Registers',
  report_server_id: 'Report Server ID',
  read_file_record: 'Read File Record',
  write_file_record: 'Write File Record',
  mask_write_register: 'Mask Write Register',
  read_write_multiple_registers: 'Read/Write Multiple Registers',
  read_fifo_queue: 'Read FIFO Queue',
  mei_transport: 'MEI Transport',
  mei_read_device_identification: 'Read Device Identification',
  mei_canopen: 'CANopen',
  modbus_error: 'Modbus Exception',
  user_defined: 'User-Defined FC',
  vendor_specific: 'Vendor-Specific FC',
  read_coils_response: 'Read Coils Response',
  read_discrete_inputs_response: 'Read Discrete Inputs Response',
  read_holding_registers_response: 'Read Holding Registers Response',
  read_input_registers_response: 'Read Input Registers Response',
  write_single_coil_response: 'Write Single Coil Echo',
  write_single_register_response: 'Write Single Register Echo',
  read_exception_status_response: 'Exception Status Response',
  diagnostics_response: 'Diagnostics Response',
  get_comm_event_counter_response: 'Comm Event Counter Response',
  get_comm_event_log_response: 'Comm Event Log Response',
  write_multiple_coils_response: 'Write Multiple Coils Response',
  write_multiple_registers_response: 'Write Multiple Registers Response',
  report_server_id_response: 'Report Server ID Response',
  read_file_record_response: 'Read File Record Response',
  mask_write_register_response: 'Mask Write Register Echo',
  read_write_multiple_registers_response: 'Read/Write Multiple Registers Response',
  read_fifo_queue_response: 'Read FIFO Queue Response',
  mei_read_device_identification_response: 'Device ID Response',
};

/** Mapping from FC hex value to human-readable name (for function_code field display) */
export const FC_DISPLAY_NAMES: Record<string, string> = {
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
};

/** Fields whose values are plain hex strings — display with 0x prefix */
// NOTE: 'value' is deliberately absent — the decoder emits it as a NUMBER (FC06 register value),
// and treating "9600" as a hex string would render it as 0x9600 (38400). It is formatted
// explicitly in flattenNode() instead.
export const HEX_FIELDS = new Set([
  'address', 'crc', 'ext_byte', 'serial_number', 'modbus_address',
  'fc', 'register', 'coil_value', 'data', 'sub_function', 'events',
  'read_register', 'write_register', 'write_data', 'additional_data',
  'fifo_pointer', 'mei_type', 'read_device_id_code', 'object_id',
  'conformity_level', 'more_follows', 'next_object_id',
  'original_fc', 'output_data', 'server_id', 'run_indicator',
  'and_mask', 'or_mask', 'subcommand', 'reference_type',
  'min_server_id', 'prev_server_id', 'prev_flag', 'flag',
]);

/** Fields that also need decimal shown in parentheses */
export const DEC_ALSO = new Set(['serial_number', 'modbus_address', 'address', 'register', 'read_register', 'write_register', 'fifo_pointer']);

/** Well-known special addresses with human-readable names */
export const ADDR_NOTES: Record<number, string> = {
  0x00: 'broadcast',
  0xFD: 'Fast Modbus broadcast',
  0xF8: 'reserved',
  0xF9: 'reserved',
  0xFA: 'reserved',
  0xFB: 'reserved',
  0xFC: 'reserved',
  0xFE: 'reserved',
  0xFF: 'FM Bus Arbitration',
};

/** All lookup tables use UPPERCASE hex keys; all lookups use .toUpperCase() */
export const EXT_BYTE_NAMES: Record<string, string> = {
  '60': 'Extended function command (legacy)',
  '46': 'Extended function command',
};

export const FM_SUBCOMMAND_NAMES: Record<string, string> = {
  '01': 'Scan Start',
  '02': 'Scan Continue',
  '03': 'Scan Response',
  '04': 'Scan End',
  '08': 'Command by Serial',
  '09': 'Response by Serial',
  '10': 'Event Request',
  '11': 'Event Transfer',
  '12': 'No Events',
  '18': 'Event Config',
};

export const FM_SUBCOMMAND_TOOLTIPS: Record<string, string> = {
  '01': 'Fast Modbus Scan Start: master resets scan status on all unscanned devices. Devices participate in arbitration; lowest serial number wins.',
  '02': 'Fast Modbus Scan Continue: master continues scanning. Devices not yet scanned participate in arbitration.',
  '03': 'Fast Modbus Scan Response: device responds with its serial number and data during scan arbitration.',
  '04': 'Fast Modbus Scan End: no new devices remain. The device with the lowest serial number that already responded wins arbitration to signal completion.',
  '08': 'Fast Modbus Cmd Send: master sends a standard Modbus command via Fast Modbus addressing (by serial number).',
  '09': 'Fast Modbus Cmd Response: device responds to a Fast Modbus standard command addressed by serial number.',
  '10': 'Fast Modbus Event Request: master polls all devices for pending events. Devices with events participate in arbitration.',
  '11': 'Fast Modbus Event Transfer: winning device sends its pending event data to master.',
  '12': 'Fast Modbus No Events: device that wins arbitration reports that no devices on the bus have pending events. Prevents master from waiting for timeout.',
  '18': 'Fast Modbus Event Config: master configures event reporting settings on a specific device.',
};

export const FIELD_LABELS: Record<string, string> = {
  address: 'Slave address', crc: 'CRC', ext_byte: 'FM Command',
  subcommand: 'FM Subcommand',
  serial_number: 'Serial number', modbus_address: 'Modbus address',
  fc: 'Function code', function_code: 'Function', register: 'Register', count: 'Count', value: 'Value',
  coil_state: 'Coil state', coil_value: 'Coil value',
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
  min_server_id: 'Minimum Server ID', max_data_len: 'Maximum Data Length',
  prev_server_id: 'Previous Server ID', prev_flag: 'Previous Flag',
  flag: 'Packet flag', unacked_count: 'Unacked events', data_len: 'Data length',
};

export const FIELD_TOOLTIPS: Record<string, string> = {
  address: 'Slave address (1 byte). Identifies the target device on the RS-485 bus. 0x00 = broadcast; 0xFD = Fast Modbus broadcast.',
  crc: 'Cyclic Redundancy Check (2 bytes, little-endian). Used to detect transmission errors in the RTU frame.',
  ext_byte: 'Fast Modbus extension marker byte (0x46 or 0x60). Signals that this is a Fast Modbus command rather than standard Modbus RTU.',
  subcommand: 'Fast Modbus subcommand byte. Specifies the exact operation within the Fast Modbus protocol (scan, event, etc.).',
  serial_number: 'Unique 4-byte factory serial number of the device. Used in Fast Modbus to address a specific device before it has a Modbus address assigned.',
  modbus_address: 'Modbus slave address assigned to the device during scan. This is the address the device will use for standard Modbus communication.',
  fc: 'Function code (1 byte). Specifies the Modbus operation to perform (read registers, write coils, etc.).',
  function_code: 'Function code of the nested Modbus PDU carried inside this Fast Modbus command.',
  register: 'Register address in the device address space.',
  count: 'Number of registers or coils to read/write.',
  value: 'Value to write to the register.',
  coil_state: 'State of the single coil written by FC 05. The 2-byte field is not a number: 0xFF00 means ON, 0x0000 means OFF, and no other value is legal.',
  coil_value: 'The illegal 2-byte word found in the coil state field. Shown only when it is neither 0x0000 nor 0xFF00.',
  byte_count: 'Number of data bytes that follow in this PDU.',
  data: 'Raw payload bytes. The meaning depends on the function code and direction.',
  sub_function: 'Sub-function code for the Diagnostics command (FC 08). Specifies the exact diagnostic operation.',
  status: 'Communication status word returned by the device.',
  event_count: 'Number of Modbus events (messages) that the device has processed since last reset.',
  message_count: 'Total number of messages the device has detected on the bus since last reset.',
  events: 'Event log bytes. Each byte encodes one bus event as defined by the Modbus specification.',
  read_register: 'Starting address of the registers to read in a Read/Write Multiple Registers request.',
  read_count: 'Number of registers to read in a Read/Write Multiple Registers request.',
  write_register: 'Starting address of the registers to write in a Read/Write Multiple Registers request.',
  write_count: 'Number of registers to write in a Read/Write Multiple Registers request.',
  write_byte_count: 'Number of bytes of write data that follow.',
  write_data: 'Data to write into the registers.',
  fifo_pointer: 'Pointer to the FIFO queue register to read from.',
  fifo_count: 'Number of data words currently in the FIFO queue.',
  mei_type: 'MEI (Modbus Encapsulated Interface) type byte. 0x0E = Read Device Identification; 0x0D = CANopen.',
  read_device_id_code: 'Specifies which set of device identification objects to read: 0x01 = Basic, 0x02 = Regular, 0x03 = Extended, 0x04 = specific object.',
  object_id: 'Object identifier for the specific device identification object being requested.',
  conformity_level: 'Device identification conformity level. Indicates which identification objects the device supports.',
  more_follows: 'Indicates whether more identification objects follow: 0x00 = no more, 0xFF = more available.',
  next_object_id: 'Object ID to use in the next request when more_follows = 0xFF.',
  number_of_objects: 'Number of identification objects contained in this response.',
  original_fc: 'The original function code that caused the exception (FC with bit 7 cleared).',
  error_code: 'Modbus exception code. 0x01=Illegal Function, 0x02=Illegal Data Address, 0x03=Illegal Data Value, 0x04=Server Failure.',
  reason: 'Internal parse error reason. Indicates why the packet could not be decoded.',
  output_data: 'Output data byte returned in the Read Exception Status response.',
  server_id: 'Device server ID returned in the Report Server ID response.',
  run_indicator: 'Run indicator status: 0xFF = device is running, 0x00 = device is stopped.',
  additional_data: 'Additional device-specific data appended to the Report Server ID response.',
  and_mask: 'AND mask for the Mask Write Register operation. Applied first: result = (current AND and_mask) OR or_mask.',
  or_mask: 'OR mask for the Mask Write Register operation. Applied after AND mask.',
  request_data_length: 'Total byte length of all sub-request records in a Write File Record request.',
  resp_data_length: 'Total byte length of all sub-response records in a Read File Record response.',
  reference_type: 'Reference type field in file record operations. Must be 0x06 per Modbus specification.',
  file_number: 'File number to access in file record operations.',
  record_number: 'Record number within the file to read or write.',
  record_length: 'Length of the record in 16-bit words.',
  file_resp_length: 'Length of this sub-response in bytes, including the reference type byte.',
  min_server_id: 'Minimum slave address the master is interested in for event polling. Devices with lower IDs skip responding.',
  max_data_len: 'Maximum number of event data bytes the master can accept in the Event Transfer response.',
  prev_server_id: 'Slave address of the device that responded in the previous event poll cycle. Used to continue round-robin polling.',
  prev_flag: 'Packet flag from the previous Event Transfer response. Echoed back so the device knows its previous response was received.',
  flag: 'Packet flag byte in the Event Transfer response. Encodes event type and sequence information.',
  unacked_count: 'Number of unacknowledged events still pending in the device event queue.',
  data_len: 'Length of the event data payload in bytes.',
};

/** Context-aware labels for the `register` field depending on PDU type */
export const REGISTER_LABELS: Record<string, string> = {
  // Read commands (FC 01–04) and write-multiple (FC 0F, 10): starting address
  read_coils: 'Starting Address',
  read_discrete_inputs: 'Starting Address',
  read_holding_registers: 'Starting Address',
  read_input_registers: 'Starting Address',
  write_multiple_coils: 'Starting Address',
  write_multiple_registers: 'Starting Address',
  write_multiple_coils_response: 'Starting Address',
  write_multiple_registers_response: 'Starting Address',
  // Write single coil (FC 05): output address
  write_single_coil: 'Output Address',
  write_single_coil_response: 'Output Address',
  // Write single register (FC 06): register address
  write_single_register: 'Register Address',
  write_single_register_response: 'Register Address',
  // Mask write register (FC 16): reference address
  mask_write_register: 'Reference Address',
  mask_write_register_response: 'Reference Address',
};

// 'reserved_address' is rendered separately with a warning message.
export const SKIP_FIELDS = new Set(['type', 'raw', 'payload', 'objects', 'sub_requests', 'sub_responses', 'reserved_address']);

export interface TreeRow {
  depth: number;
  label: string;
  key?: string;
  value?: string;
  tooltip?: string; // tooltip shown on hover over the field label
  valueTooltip?: string; // tooltip shown on hover over the field value
  byteStart: number;
  byteEnd: number;
  isSection: boolean;
  isField: boolean;
  isError: boolean;
  isDataField?: boolean; // the "data" field — show magnifier icon
  isArray?: boolean;
  arrayItems?: { label: string; value: string }[];
}

export type EndiannessKey = 'abcd' | 'cdab' | 'badc' | 'dcba';

/** PDU types whose `data` field carries bit-packed coil/discrete input bytes */
export const COIL_DATA_TYPES = new Set([
  'read_coils_response',
  'read_discrete_inputs_response',
  'write_multiple_coils',
]);

/**
 * PDU types whose `data` / `write_data` field is a sequence of whole 16-bit registers
 * rather than an opaque byte blob. Their payload is displayed one register per group.
 */
export const REGISTER_DATA_TYPES = new Set([
  'read_holding_registers_response', // FC 03
  'read_input_registers_response', // FC 04
  'write_multiple_registers', // FC 10 request
  'read_write_multiple_registers', // FC 17 request (write_data)
  'read_write_multiple_registers_response', // FC 17 response
  'read_fifo_queue_response', // FC 18
]);

/** Display text for the FC05 coil state, keyed by the decoder's `coil_state` field. */
const COIL_STATE_LABELS: Record<string, string> = {
  on: 'ON (0xFF00)',
  off: 'OFF (0x0000)',
  invalid: 'Invalid (expected 0x0000 or 0xFF00)',
};

/**
 * Format the FC05 coil state emitted by the decoder.
 * Unknown states are passed through unchanged so a future decoder value is never swallowed.
 */
export function fmtCoilState(state: string): string {
  return COIL_STATE_LABELS[state] ?? state;
}

/**
 * Format a hex register payload as one `0x`-prefixed group per 16-bit register:
 * '00320037003C' → '0x0032 0x0037 0x003C'. A continuous blob is unreadable once it is more
 * than a couple of registers long, and register boundaries are exactly what the reader needs.
 *
 * A trailing odd byte (a malformed frame that the byte_count let through) is emitted as its
 * own short group rather than being padded or dropped: '003200' → '0x0032 0x00'.
 */
export function fmtRegisterData(hexStr: string): string {
  const upper = hexStr.toUpperCase();
  if (!upper) return '';
  return (upper.match(/.{1,4}/g) ?? []).map(g => `0x${g}`).join(' ');
}

/** Mapping from PDU type to Modicon address-space prefix digit */
const MODICON_PREFIX: Record<string, number> = {
  // Coils (0x): FC 01/05/0F
  read_coils: 0, write_single_coil: 0, write_multiple_coils: 0,
  read_coils_response: 0, write_single_coil_response: 0, write_multiple_coils_response: 0,
  // Discrete inputs (1x): FC 02
  read_discrete_inputs: 1, read_discrete_inputs_response: 1,
  // Input registers (3x): FC 04
  read_input_registers: 3, read_input_registers_response: 3,
  // Holding registers (4x): FC 03/06/10/16/17
  read_holding_registers: 4, read_holding_registers_response: 4,
  write_single_register: 4, write_single_register_response: 4,
  write_multiple_registers: 4, write_multiple_registers_response: 4,
  mask_write_register: 4, mask_write_register_response: 4,
  read_write_multiple_registers: 4, read_write_multiple_registers_response: 4,
};

/**
 * Return Modicon address notation string for a given wire address and prefix digit.
 * Uses 5-digit notation (prefix + 4-digit address) when wireAddr+1 fits in 4 digits (<=9999),
 * otherwise 6-digit notation (prefix + 5-digit address).
 * Example: wire 0, prefix 4 → "Modicon: 40001"; wire 0, prefix 0 → "Modicon: 00001".
 */
export function modiconStr(wireAddr: number, prefix: number): string {
  const addr1 = wireAddr + 1; // 1-based address
  const digits = addr1 <= 9999 ? 4 : 5; // 5-digit or 6-digit notation
  return `Modicon: ${prefix}${String(addr1).padStart(digits, '0')}`;
}

/**
 * Format a hex coil/discrete data string as "0x<HEX>  (<binary bytes>)".
 * Each byte is shown as 8 binary digits (MSB on left), bytes separated by space.
 * Example: fmtCoilData('CD6B05') → '0xCD6B05  (11001101 01101011 00000101)'
 */
export function fmtCoilData(hexStr: string): string {
  const upper = hexStr.toUpperCase();
  const hex = `0x${upper}`;
  if (!upper || upper.length % 2 !== 0) return hex;
  const pairs = upper.match(/.{2}/g) ?? [];
  const binaryStr = pairs.map(b => parseInt(b, 16).toString(2).padStart(8, '0')).join(' ');
  return `${hex}  (${binaryStr})`;
}

/**
 * Format a field value for display.
 * - Hex fields get 0x prefix and optional decimal annotation.
 * - Known enum fields (function_code, ext_byte, subcommand) get human-readable name in parens.
 */
export function fmtVal(key: string, raw: string): string {
  if ((key === 'fc' || key === 'function_code') && /^[0-9A-Fa-f]+$/.test(raw)) {
    const upper = raw.toUpperCase();
    const { isException, baseFc } = splitFunctionCode(parseInt(upper, 16));
    if (isException) {
      // Error FC: bit 7 is set — the original FC is the low 7 bits
      const origHex = baseFc.toString(16).toUpperCase().padStart(2, '0');
      const origName = FC_DISPLAY_NAMES[origHex] ?? 'Unknown';
      return `0x${upper} (Error: ${origName})`;
    }
    const name = FC_DISPLAY_NAMES[upper] ?? 'Unknown';
    return `0x${upper} (${name})`;
  }
  if (key === 'ext_byte' && /^[0-9A-Fa-f]+$/.test(raw)) {
    const upper = raw.toUpperCase();
    const name = EXT_BYTE_NAMES[upper] ?? 'Unknown';
    return `0x${upper} (${name})`;
  }
  if (key === 'subcommand' && /^[0-9A-Fa-f]+$/.test(raw)) {
    const upper = raw.toUpperCase();
    const name = FM_SUBCOMMAND_NAMES[upper] ?? 'Unknown';
    return `0x${upper} (${name})`;
  }
  if (HEX_FIELDS.has(key) && /^[0-9A-Fa-f]+$/.test(raw)) {
    const hex = `0x${raw.toUpperCase()}`;
    if (DEC_ALSO.has(key)) {
      const dec = parseInt(raw, 16);
      const note = (key === 'address' || key === 'modbus_address') ? ADDR_NOTES[dec] : undefined;
      const suffix = note ? ` ${note}` : '';
      return `${hex} (${dec}${suffix})`;
    }
    return hex;
  }
  return raw;
}

/**
 * Find byte range of nodeRaw within the full packet hex,
 * starting search at parentStart to avoid false matches.
 */
export function rawToRange(nodeRaw: string, fhex: string, parentStart: number): { start: number; end: number } {
  if (!nodeRaw) return { start: parentStart, end: parentStart };
  const upper = nodeRaw.toUpperCase();
  const idx = fhex.indexOf(upper, parentStart * 2);
  if (idx === -1 || idx % 2 !== 0) return { start: parentStart, end: parentStart };
  return { start: idx / 2, end: idx / 2 + upper.length / 2 };
}

/**
 * Compute per-field byte ranges within a PDU node.
 * Returns map of fieldName → {start, end} relative to fullHex.
 * We know the internal structure from the node type.
 */
export function fieldRanges(obj: Record<string, unknown>, nodeByteStart: number): Record<string, { start: number; end: number }> {
  const r: Record<string, { start: number; end: number }> = {};
  const s = nodeByteStart;
  const t = obj.type as string;
  // For each known PDU structure, compute field offsets
  // Offset order corresponds to on-wire byte order
  if (['read_coils', 'read_discrete_inputs', 'read_holding_registers', 'read_input_registers'].includes(t)) {
    // request: FC(1) reg(2) count(2)
    r.fc = { start: s, end: s + 1 };
    r.register = { start: s + 1, end: s + 3 };
    r.count = { start: s + 3, end: s + 5 };
  } else if (['read_coils_response', 'read_discrete_inputs_response', 'read_holding_registers_response', 'read_input_registers_response'].includes(t)) {
    // response: FC(1) byteCount(1) data(N)
    r.fc = { start: s, end: s + 1 };
    r.byte_count = { start: s + 1, end: s + 2 };
    const bc = obj.byte_count as number ?? 0;
    r.data = { start: s + 2, end: s + 2 + bc };
  } else if (['write_single_register', 'write_single_register_response'].includes(t)) {
    r.fc = { start: s, end: s + 1 };
    r.register = { start: s + 1, end: s + 3 };
    r.value = { start: s + 3, end: s + 5 };
  } else if (['write_single_coil', 'write_single_coil_response'].includes(t)) {
    // Same wire layout as FC06, but the last 2 bytes carry a coil state, not a value.
    // Both coil fields describe that same word, so both map onto the same byte range.
    r.fc = { start: s, end: s + 1 };
    r.register = { start: s + 1, end: s + 3 };
    r.coil_state = { start: s + 3, end: s + 5 };
    r.coil_value = { start: s + 3, end: s + 5 };
  } else if (['write_multiple_coils', 'write_multiple_registers'].includes(t)) {
    r.fc = { start: s, end: s + 1 };
    r.register = { start: s + 1, end: s + 3 };
    r.count = { start: s + 3, end: s + 5 };
    r.byte_count = { start: s + 5, end: s + 6 };
    const bc = obj.byte_count as number ?? 0;
    r.data = { start: s + 6, end: s + 6 + bc };
  } else if (['write_multiple_coils_response', 'write_multiple_registers_response'].includes(t)) {
    r.fc = { start: s, end: s + 1 };
    r.register = { start: s + 1, end: s + 3 };
    r.count = { start: s + 3, end: s + 5 };
  } else if (t === 'modbus_error') {
    r.fc = { start: s, end: s + 1 };
    r.original_fc = { start: s, end: s + 1 };
    r.error_code = { start: s + 1, end: s + 2 };
  } else if (t === 'scan_response') {
    // sub(1) serial(4) addr(1)
    r.serial_number = { start: s + 1, end: s + 5 };
    r.modbus_address = { start: s + 5, end: s + 6 };
  } else if (['command_by_serial', 'response_by_serial'].includes(t)) {
    r.serial_number = { start: s + 1, end: s + 5 };
  } else if (t === 'event_request') {
    // event_request wire layout: [subcommand(1), min_server_id(1), max_data_len(1), prev_server_id(1), prev_flag(1)]
    r.min_server_id = { start: s + 1, end: s + 2 };
    r.max_data_len = { start: s + 2, end: s + 3 };
    r.prev_server_id = { start: s + 3, end: s + 4 };
    r.prev_flag = { start: s + 4, end: s + 5 };
  } else if (t === 'event_transfer') {
    // event_transfer wire layout: [subcommand(1), flag(1), unacked_count(1), data_len(1), data(N)]
    const dataLen = typeof obj.data_len === 'string' && obj.data_len.length > 0 ? (parseInt(obj.data_len, 16) || 0) : 0;
    r.flag = { start: s + 1, end: s + 2 };
    r.unacked_count = { start: s + 2, end: s + 3 };
    r.data_len = { start: s + 3, end: s + 4 };
    r.data = { start: s + 4, end: s + 4 + dataLen };
  } else if (t === 'event_config') {
    // event_config wire layout: [subcommand(1), data_len(1), data(N)]
    const dataLen = typeof obj.data_len === 'string' && obj.data_len.length > 0 ? (parseInt(obj.data_len, 16) || 0) : 0;
    r.data_len = { start: s + 1, end: s + 2 };
    r.data = { start: s + 2, end: s + 2 + dataLen };
  } else if (t === 'fast_modbus') {
    // fast_modbus wire layout: [ext_byte(1), subcommand(1), ...payload]
    r.ext_byte = { start: s, end: s + 1 };
    r.subcommand = { start: s + 1, end: s + 2 };
  } else if (t === 'modbus_rtu') {
    // modbus_rtu wire layout: [fc(1), ...pdu_data]
    r.fc = { start: s, end: s + 1 };
  } else if (t === 'rtu_frame') {
    r.address = { start: s, end: s + 1 };
    const rawLen = (obj.raw as string ?? '').length / 2;
    r.crc = { start: s + rawLen - 2, end: s + rawLen };
  }
  return r;
}

/** Flatten a decoded packet node into a list of tree rows for display */
export function flattenNode(obj: Record<string, unknown>, depth: number, fhex: string, parentStart: number, parentType?: string, crcStatus?: 'OK' | 'ERR'): TreeRow[] {
  const rows: TreeRow[] = [];
  const t = (obj.type as string) ?? '?';
  const nodeRaw = (obj.raw as string) ?? '';
  const range = rawToRange(nodeRaw, fhex, parentStart);
  const fRanges = fieldRanges(obj, range.start);

  // Check if this node has any meaningful content beyond the type label itself
  const hasScalars = Object.entries(obj).some(([k, v]) => !SKIP_FIELDS.has(k) && typeof v !== 'object');
  const hasArrays = ['sub_requests', 'sub_responses', 'objects'].some(ak => Array.isArray(obj[ak]));
  const hasChild = obj.payload && typeof obj.payload === 'object';
  // Only add section header if node has content OR is top-level (depth 0)
  if (depth === 0 || hasScalars || hasArrays || hasChild) {
    rows.push({ depth, label: TYPE_LABELS[t] ?? t, byteStart: range.start, byteEnd: range.end, isSection: true, isField: false, isError: t === 'parse_error' });
  }

  for (const [k, v] of Object.entries(obj)) {
    if (SKIP_FIELDS.has(k)) continue;
    // Skip fc in PDU nodes when the parent already exposes the function code at its own level:
    // modbus_rtu has fc, command_by_serial/response_by_serial have function_code — no need to repeat.
    // parse_error nodes are also covered: modbus_rtu.fc already shows the bad FC value above.
    if (k === 'fc' && (parentType === 'modbus_rtu' || parentType === 'command_by_serial' || parentType === 'response_by_serial')) continue;
    if (typeof v === 'object') continue;
    const fr = fRanges[k];
    const bStart = fr ? fr.start : range.start;
    const bEnd = fr ? fr.end : range.end;
    const isDataField = k === 'data' || k === 'write_data';
    const label = (k === 'register' && REGISTER_LABELS[t]) ? REGISTER_LABELS[t] : (FIELD_LABELS[k] ?? k);
    let value: string;
    if (k === 'coil_state') {
      value = fmtCoilState(String(v));
    } else if (k === 'value' && typeof v === 'number') {
      // FC06 register value — the decoder emits a number, so format it here instead of
      // running it through the hex-string path (which would read "9600" as hex).
      value = `0x${v.toString(16).toUpperCase().padStart(4, '0')} (${v})`;
    } else if (k === 'data' && COIL_DATA_TYPES.has(t)) {
      value = fmtCoilData(String(v));
    } else if ((k === 'data' || k === 'write_data') && REGISTER_DATA_TYPES.has(t)) {
      value = fmtRegisterData(String(v));
    } else if ((k === 'register' || k === 'read_register' || k === 'write_register') && /^[0-9A-Fa-f]{2,}$/.test(String(v))) {
      // Format register with Modicon notation if PDU type is known
      const wireAddr = parseInt(String(v), 16);
      const prefix = MODICON_PREFIX[t];
      const hexStr = `0x${String(v).toUpperCase()}`;
      if (prefix !== undefined) {
        value = `${hexStr} (${wireAddr}, ${modiconStr(wireAddr, prefix)})`;
      } else {
        value = `${hexStr} (${wireAddr})`;
      }
    } else {
      value = fmtVal(k, String(v));
    }
    // If this is the CRC field and crcStatus is provided, append the status to the value
    if (k === 'crc' && crcStatus) {
      value = `${value} (${crcStatus})`;
    }
    let valueTooltip: string | undefined = undefined;
    if (k === 'subcommand' && typeof v === 'string') {
      valueTooltip = FM_SUBCOMMAND_TOOLTIPS[v.toUpperCase()] ?? undefined;
    }
    if ((k === 'address' || k === 'modbus_address') && typeof v === 'string') {
      const dec = parseInt(v, 16);
      const note = ADDR_NOTES[dec];
      if (note) valueTooltip = `0x${v.toUpperCase()} — ${note}`;
    }
    if (k === 'ext_byte' && typeof v === 'string') {
      const upper = v.toUpperCase();
      // Provide a meaningful description distinguishing the two extension markers
      if (upper === '46') valueTooltip = 'Fast Modbus extension marker (FC 0x46). Current standard variant used in all modern devices.';
      else if (upper === '60') valueTooltip = 'Fast Modbus extension marker (FC 0x60). Legacy variant used in older firmware; deprecated but still accepted.';
    }
    if ((k === 'fc' || k === 'function_code') && typeof v === 'string') {
      const upper = v.toUpperCase();
      const { isException, baseFc } = splitFunctionCode(parseInt(upper, 16));
      // Only set a tooltip for error FC responses — the plain name is already visible inline via fmtVal()
      if (isException) {
        const origHex = baseFc.toString(16).toUpperCase().padStart(2, '0');
        valueTooltip = `Exception response for FC 0x${origHex} (${FC_DISPLAY_NAMES[origHex] ?? 'Unknown'})`;
      }
    }
    // An FC05 state word that is neither 0x0000 nor 0xFF00 violates the spec — flag it like a
    // parse error so it is visually distinct from a normal ON/OFF row.
    const isError = k === 'reason' || (k === 'coil_state' && v === 'invalid');
    rows.push({ depth: depth + 1, label, key: k, value, tooltip: FIELD_TOOLTIPS[k], valueTooltip, byteStart: bStart, byteEnd: bEnd, isSection: false, isField: true, isError, isDataField });
  }

  if ((obj as Record<string, unknown>).reserved_address) {
    rows.push({ depth: depth + 1, label: 'Warning', value: 'Reserved address (0xF8–0xFF)', byteStart: range.start, byteEnd: range.end, isSection: false, isField: true, isError: true });
  }

  for (const ak of ['sub_requests', 'sub_responses', 'objects'] as const) {
    const arr = obj[ak];
    if (!Array.isArray(arr)) continue;
    rows.push({ depth: depth + 1, label: `${ak.replace(/_/g, ' ')} (${arr.length})`, byteStart: range.start, byteEnd: range.end, isSection: false, isField: false, isError: false, isArray: true, arrayItems: arr.map((item: Record<string, unknown>, i: number) => ({ label: `[${i}]`, value: Object.entries(item).filter(([, v]) => typeof v !== 'object').map(([k, v]) => `${FIELD_LABELS[k] ?? k}: ${v}`).join(' · ') })) });
  }

  if (obj.payload && typeof obj.payload === 'object') {
    rows.push(...flattenNode(obj.payload as Record<string, unknown>, depth + 1, fhex, range.start, t, crcStatus));
  }

  return rows;
}

/** Check if a byte value is a printable ASCII character — range [0x20, 0x7F), i.e. space through tilde */
export function isPrintable(b: number): boolean {
 return b >= 0x20 && b < 0x7f;
}

// 32-bit endianness configurations

export const ENDIAN_CONFIGS: Record<EndiannessKey, { label: string; desc: string; fn: (b: number[], i: number) => number }> = {
  abcd: { label: 'AB CD', desc: 'Big Endian', fn: (b, i) => ((b[i] << 24) | (b[i+1] << 16) | (b[i+2] << 8) | b[i+3]) >>> 0 },
  cdab: { label: 'CD AB', desc: 'Mid-Little', fn: (b, i) => ((b[i+2] << 24) | (b[i+3] << 16) | (b[i] << 8) | b[i+1]) >>> 0 },
  badc: { label: 'BA DC', desc: 'Byte-swap', fn: (b, i) => ((b[i+1] << 24) | (b[i] << 16) | (b[i+3] << 8) | b[i+2]) >>> 0 },
  dcba: { label: 'DC BA', desc: 'Little Endian', fn: (b, i) => ((b[i+3] << 24) | (b[i+2] << 16) | (b[i+1] << 8) | b[i]) >>> 0 },
};

/**
 * Format a uint32 value as a float32 string.
 * Uses DataView to interpret the raw bits as a 32-bit IEEE 754 float.
 */
export function f32str(u: number): string {
  const view = new DataView(new ArrayBuffer(4));
  view.setUint32(0, u, false);
  const f = view.getFloat32(0, false);
  if (isNaN(f)) return 'NaN';
  if (Math.abs(f) < 1e-6 && f !== 0) return f.toExponential(4);
  return parseFloat(f.toPrecision(7)).toString();
}
