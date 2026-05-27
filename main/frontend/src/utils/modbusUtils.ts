// Modbus RTU utility functions for the packet sender.

/** Modbus CRC16 calculation. Returns the 16-bit CRC (lo-byte first in wire order). */
export function modbusCrc16(data: Uint8Array): number {
  let crc = 0xffff
  for (const byte of data) {
    crc ^= byte
    for (let i = 0; i < 8; i++) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ 0xa001
      } else {
        crc >>= 1
      }
    }
  }
  return crc // low byte first in Modbus RTU wire order
}

/** Append Modbus CRC to a byte array and return the full frame. */
export function appendCrc(frame: Uint8Array): Uint8Array {
  const crc = modbusCrc16(frame)
  const out = new Uint8Array(frame.length + 2)
  out.set(frame)
  out[frame.length] = crc & 0xff       // CRC low byte
  out[frame.length + 1] = (crc >> 8) & 0xff // CRC high byte
  return out
}

/** Convert Uint8Array to uppercase hex string with spaces (e.g. "01 03 00 00 00 0A"). */
export function bytesToHexSpaced(data: Uint8Array): string {
  return Array.from(data)
    .map(b => b.toString(16).padStart(2, '0').toUpperCase())
    .join(' ')
}

/** Build a Modbus RTU read request (FC01/02/03/04). */
export function buildReadFrame(slaveId: number, fc: number, address: number, count: number): Uint8Array {
  const frame = new Uint8Array([
    slaveId & 0xff,
    fc & 0xff,
    (address >> 8) & 0xff, address & 0xff,
    (count >> 8) & 0xff, count & 0xff,
  ])
  return appendCrc(frame)
}

/** Build a Modbus RTU FC06 write single register request. */
export function buildWriteSingleRegister(slaveId: number, address: number, value: number): Uint8Array {
  const frame = new Uint8Array([
    slaveId & 0xff,
    0x06,
    (address >> 8) & 0xff, address & 0xff,
    (value >> 8) & 0xff, value & 0xff,
  ])
  return appendCrc(frame)
}

/** Build a Modbus RTU FC05 write single coil request. Value: 0 = OFF, any non-zero = ON. */
export function buildWriteSingleCoil(slaveId: number, address: number, value: number): Uint8Array {
  const coilVal = value !== 0 ? 0xff00 : 0x0000
  const frame = new Uint8Array([
    slaveId & 0xff,
    0x05,
    (address >> 8) & 0xff, address & 0xff,
    (coilVal >> 8) & 0xff, coilVal & 0xff,
  ])
  return appendCrc(frame)
}

/** Build a Modbus RTU FC16 write multiple registers (with a single value). */
export function buildWriteMultipleRegisters(slaveId: number, address: number, value: number): Uint8Array {
  const frame = new Uint8Array([
    slaveId & 0xff,
    0x10,
    (address >> 8) & 0xff, address & 0xff,
    0x00, 0x01,  // quantity = 1 register
    0x02,        // byte count = 2
    (value >> 8) & 0xff, value & 0xff,
  ])
  return appendCrc(frame)
}

/** Build a Modbus RTU FC15 write multiple coils (with a single coil value). */
export function buildWriteMultipleCoils(slaveId: number, address: number, value: number): Uint8Array {
  const coilByte = value !== 0 ? 0x01 : 0x00
  const frame = new Uint8Array([
    slaveId & 0xff,
    0x0f,
    (address >> 8) & 0xff, address & 0xff,
    0x00, 0x01,  // quantity = 1 coil
    0x01,        // byte count = 1
    coilByte,
  ])
  return appendCrc(frame)
}

/** Parse an address string: "0x..." → hex, otherwise decimal. Returns NaN on invalid input. */
export function parseModbusAddress(raw: string): number {
  const trimmed = raw.trim()
  if (trimmed.toLowerCase().startsWith('0x')) {
    return parseInt(trimmed, 16)
  }
  return parseInt(trimmed, 10)
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
  // Parse slave ID: "0x" prefix → hex, otherwise decimal
  const slaveRaw = slaveStr.trim()
  const slave = slaveRaw.toLowerCase().startsWith('0x')
    ? parseInt(slaveRaw, 16)
    : parseInt(slaveRaw, 10)
  if (isNaN(slave) || slave < 1 || slave > 247) return null

  const fcNum = parseInt(fcStr, 16)
  const addr = parseModbusAddress(addrStr)
  if (isNaN(addr) || addr < 0 || addr > 0xffff) return null

  if (mode === 'read') {
    const cnt = parseInt(valueStr, 10)
    if (isNaN(cnt) || cnt < 1 || cnt > 2000) return null
    return buildReadFrame(slave, fcNum, addr, cnt)
  }

  // Write modes
  const val = parseInt(valueStr, 10)
  if (isNaN(val)) return null

  switch (fcStr) {
    case '06': return buildWriteSingleRegister(slave, addr, val)
    case '05': return buildWriteSingleCoil(slave, addr, val)
    case '10': return buildWriteMultipleRegisters(slave, addr, val)
    case '0f': return buildWriteMultipleCoils(slave, addr, val)
    default: return null
  }
}

/** Convert a full RTU frame (with CRC) to preview parts with CRC annotation. */
export function frameToPreviewParts(bytes: Uint8Array): { hex: string; isCrc: boolean }[] {
  if (bytes.length < 2) return []
  return Array.from(bytes).map((b, i) => ({
    hex: b.toString(16).padStart(2, '0').toUpperCase(),
    isCrc: i >= bytes.length - 2,
  }))
}

/** Send a raw hex-encoded RTU frame to a port via the HTTP API. */
export async function sendPacketToPort(portNum: string, hex: string): Promise<{ sent: number }> {
  const resp = await fetch(`/ports/${portNum}/send`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ hex }),
  })
  if (!resp.ok) {
    const err = await resp.json().catch(() => ({ error: resp.statusText }))
    throw new Error(err.error ?? resp.statusText)
  }
  return resp.json()
}
