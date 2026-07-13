// Modbus RTU utility functions for the packet sender.

/** Modbus CRC16 calculation. Returns the 16-bit CRC (lo-byte first in wire order). */
export function modbusCrc16(data: Uint8Array): number {
  let crc = 0xffff;
  for (const byte of data) {
    crc ^= byte;
    for (let i = 0; i < 8; i++) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ 0xa001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc; // low byte first in Modbus RTU wire order
}

/** Append Modbus CRC to a byte array and return the full frame. */
export function appendCrc(frame: Uint8Array): Uint8Array {
  const crc = modbusCrc16(frame);
  const out = new Uint8Array(frame.length + 2);
  out.set(frame);
  out[frame.length] = crc & 0xff; // CRC low byte
  out[frame.length + 1] = (crc >> 8) & 0xff; // CRC high byte
  return out;
}

/** Convert Uint8Array to uppercase hex string with spaces (e.g. "01 03 00 00 00 0A"). */
export function bytesToHexSpaced(data: Uint8Array): string {
  return Array.from(data)
    .map(b => b.toString(16).padStart(2, '0').toUpperCase())
    .join(' ');
}

/** Build a Modbus RTU read request (FC01/02/03/04). */
export function buildReadFrame(slaveId: number, fc: number, address: number, count: number): Uint8Array {
  const frame = new Uint8Array([
    slaveId & 0xff,
    fc & 0xff,
    (address >> 8) & 0xff, address & 0xff,
    (count >> 8) & 0xff, count & 0xff,
  ]);
  return appendCrc(frame);
}

/** Build a Modbus RTU FC06 write single register request. */
export function buildWriteSingleRegister(slaveId: number, address: number, value: number): Uint8Array {
  const frame = new Uint8Array([
    slaveId & 0xff,
    0x06,
    (address >> 8) & 0xff, address & 0xff,
    (value >> 8) & 0xff, value & 0xff,
  ]);
  return appendCrc(frame);
}

/** Build a Modbus RTU FC05 write single coil request. Value: 0 = OFF, any non-zero = ON. */
export function buildWriteSingleCoil(slaveId: number, address: number, value: number): Uint8Array {
  const coilVal = value !== 0 ? 0xff00 : 0x0000;
  const frame = new Uint8Array([
    slaveId & 0xff,
    0x05,
    (address >> 8) & 0xff, address & 0xff,
    (coilVal >> 8) & 0xff, coilVal & 0xff,
  ]);
  return appendCrc(frame);
}

/** Build a Modbus RTU FC16 write multiple registers. Writes N = values.length registers. */
export function buildWriteMultipleRegisters(slaveId: number, address: number, values: number[]): Uint8Array {
  const quantity = values.length;
  const byteCount = quantity * 2;
  const frame = new Uint8Array(7 + byteCount);
  frame[0] = slaveId & 0xff;
  frame[1] = 0x10;
  frame[2] = (address >> 8) & 0xff;
  frame[3] = address & 0xff;
  frame[4] = (quantity >> 8) & 0xff;
  frame[5] = quantity & 0xff;
  frame[6] = byteCount & 0xff;
  // Register values, each big-endian (high byte first).
  for (let i = 0; i < quantity; i++) {
    frame[7 + i * 2] = (values[i] >> 8) & 0xff;
    frame[7 + i * 2 + 1] = values[i] & 0xff;
  }
  return appendCrc(frame);
}

/** Build a Modbus RTU FC15 write multiple coils. Writes N = values.length coils. */
export function buildWriteMultipleCoils(slaveId: number, address: number, values: number[]): Uint8Array {
  const quantity = values.length;
  const byteCount = Math.ceil(quantity / 8);
  const frame = new Uint8Array(7 + byteCount);
  frame[0] = slaveId & 0xff;
  frame[1] = 0x0f;
  frame[2] = (address >> 8) & 0xff;
  frame[3] = address & 0xff;
  frame[4] = (quantity >> 8) & 0xff;
  frame[5] = quantity & 0xff;
  frame[6] = byteCount & 0xff;
  // Pack coil bits LSB-first per the Modbus spec: coil i occupies bit (i % 8) of data byte (i / 8).
  for (let i = 0; i < quantity; i++) {
    if (values[i] !== 0) {
      frame[7 + Math.floor(i / 8)] |= 1 << (i % 8);
    }
  }
  return appendCrc(frame);
}

/**
 * Strict decimal integer parse: reject partial/float/garbage tokens ("1.5", "5x", "0x10",
 * "10abc"). Unlike parseInt(s, 10), which stops at the first non-digit and returns the leading
 * number, this returns NaN unless the whole (trimmed) token is a signed integer.
 */
export function parseStrictInt(s: string): number {
  const t = s.trim();
  return /^-?\d+$/.test(t) ? parseInt(t, 10) : NaN;
}

/**
 * Parse a value list from a raw string: split on commas and/or whitespace, drop empties.
 * Each token is parsed as a strict decimal integer; non-integer tokens become NaN so callers
 * can reject them.
 */
export function parseValueList(raw: string): number[] {
  return raw
    .split(/[\s,]+/)
    .filter(s => s.length > 0)
    .map(s => parseStrictInt(s));
}

/** Parse an address string: "0x..." → hex, otherwise decimal. Returns NaN on invalid input. */
export function parseModbusAddress(raw: string): number {
  const trimmed = raw.trim();
  if (trimmed.toLowerCase().startsWith('0x')) {
    return parseInt(trimmed, 16);
  }
  return parseInt(trimmed, 10);
}

/**
 * Build the preview Uint8Array from raw string inputs.
 * Returns null when any input is invalid or out of range.
 */
export function buildPreviewFrame(
  slaveStr: string,
  fcStr: string,
  addrStr: string,
  valueStr: string,
  mode: 'read' | 'write',
): Uint8Array | null {
  // Parse slave ID: "0x" prefix → hex, otherwise decimal (same rule as an address).
  const slave = parseModbusAddress(slaveStr);
  if (isNaN(slave) || slave < 1 || slave > 247) return null;

  const fcNum = parseInt(fcStr, 16);
  const addr = parseModbusAddress(addrStr);
  if (isNaN(addr) || addr < 0 || addr > 0xffff) return null;

  if (mode === 'read') {
    // Only FC01/02/03/04 are valid read function codes; reject anything else.
    if (fcNum !== 0x01 && fcNum !== 0x02 && fcNum !== 0x03 && fcNum !== 0x04) return null;
    const cnt = parseStrictInt(valueStr);
    if (isNaN(cnt) || cnt < 1 || cnt > 2000) return null;
    return buildReadFrame(slave, fcNum, addr, cnt);
  }

  // Write modes. FC06/FC05 take a single value; FC16/FC15 take a comma/space list.
  switch (fcStr) {
    // Single register write: value must fit a 16-bit register (0..0xFFFF), otherwise it
    // would be silently truncated/wrapped on the wire (e.g. 70000 → 4464, -1 → 0xFFFF).
    case '06': {
      const val = parseStrictInt(valueStr);
      if (isNaN(val) || val < 0 || val > 0xffff) return null;
      return buildWriteSingleRegister(slave, addr, val);
    }
    // Single coil write: coils are boolean, so the value must be exactly 0 or 1.
    case '05': {
      const val = parseStrictInt(valueStr);
      if (isNaN(val) || (val !== 0 && val !== 1)) return null;
      return buildWriteSingleCoil(slave, addr, val);
    }
    // Multiple registers: 1..123 values, each a valid 16-bit register.
    case '10': {
      const values = parseValueList(valueStr);
      if (values.length < 1 || values.length > 123) return null;
      if (values.some(v => isNaN(v) || v < 0 || v > 0xffff)) return null;
      return buildWriteMultipleRegisters(slave, addr, values);
    }
    // Multiple coils: 1..1968 values, each 0 or 1.
    case '0f': {
      const values = parseValueList(valueStr);
      if (values.length < 1 || values.length > 1968) return null;
      if (values.some(v => isNaN(v) || (v !== 0 && v !== 1))) return null;
      return buildWriteMultipleCoils(slave, addr, values);
    }
    default: return null;
  }
}

/** Convert a full RTU frame (with CRC) to preview parts with CRC annotation. */
export function frameToPreviewParts(bytes: Uint8Array): { hex: string; isCrc: boolean }[] {
  if (bytes.length < 2) return [];
  return Array.from(bytes).map((b, i) => ({
    hex: b.toString(16).padStart(2, '0').toUpperCase(),
    isCrc: i >= bytes.length - 2,
  }));
}

/** Send a raw hex-encoded RTU frame to a port via the HTTP API. */
export async function sendPacketToPort(portNum: string, hex: string): Promise<{ sent: number }> {
  const resp = await fetch(`/ports/${portNum}/send`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ hex }),
  });
  if (!resp.ok) {
    const err = await resp.json().catch(() => ({ error: resp.statusText }));
    throw new Error(err.error ?? resp.statusText);
  }
  return resp.json();
}
