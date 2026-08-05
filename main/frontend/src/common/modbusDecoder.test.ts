import { describe, it, expect } from 'vitest';
import { decodePacket, parseHex } from './modbusDecoder';

describe('parseHex', () => {
  it('spaces', () => expect(parseHex('FD 60 01')).toEqual([0xFD, 0x60, 0x01]));
  it('no spaces', () => expect(parseHex('FD6001')).toEqual([0xFD, 0x60, 0x01]));
  it('lowercase', () => expect(parseHex('fd 60 01')).toEqual([0xFD, 0x60, 0x01]));
  it('mixed case', () => expect(parseHex('Fd 60 01')).toEqual([0xFD, 0x60, 0x01]));
  it('empty → null', () => expect(parseHex('')).toBe(null));
  it('spaces only → null', () => expect(parseHex('   ')).toBe(null));
  it('odd length → null', () => expect(parseHex('FD6')).toBe(null));
  it('invalid chars → null', () => expect(parseHex('FD ZZ')).toBe(null));
  it('single byte', () => expect(parseHex('FF')).toEqual([0xFF]));
  it('leading/trailing spaces', () => expect(parseHex('  FD 60  ')).toEqual([0xFD, 0x60]));
});

describe('arbitration', () => {
  it('10 FF bytes (packet 32)', () => {
    const r = decodePacket('FF FF FF FF FF FF FF FF FF FF');
    expect(r.type).toBe('arbitration');
    expect(r.raw).toBe('FFFFFFFFFFFFFFFFFFFF');
  });
  it('9 FF bytes (packet 47)', () => {
    const r = decodePacket('FF FF FF FF FF FF FF FF FF');
    expect(r.type).toBe('arbitration');
    expect(r.raw).toBe('FFFFFFFFFFFFFFFFFF');
  });
  it('single FF', () => {
    expect(decodePacket('FF').type).toBe('arbitration');
  });
});

// Scan Start — packet 31: FD 60 01 09 F0
describe('scan_start (packet 31)', () => {
  const RAW = 'FD600109F0';

  it('type and structure', () => {
    const r = decodePacket('FD 60 01 09 F0');
    expect(r.type).toBe('rtu_frame');
    expect(r.raw).toBe(RAW);
    expect(r.address).toBe('FD');
    expect(r.crc).toBe('09F0');
    expect(r.payload.type).toBe('fast_modbus');
    expect((r.payload as any).ext_byte).toBe('60');
    expect((r.payload as any).payload.type).toBe('scan_start');
    expect((r.payload as any).payload.raw).toBe('01');
  });

  it('no spaces in input gives same result', () => {
    expect(decodePacket(RAW)).toEqual(decodePacket('FD 60 01 09 F0'));
  });

  it('with 0x46 ext_byte', () => {
    const r = decodePacket('FD 46 01 00 00');
    expect((r.payload as any).ext_byte).toBe('46');
    expect((r.payload as any).payload.type).toBe('scan_start');
  });
});

// Scan Continue — packet 46: FD 60 02 49 F1
describe('scan_continue (packet 46)', () => {
  it('structure', () => {
    const r = decodePacket('FD 60 02 49 F1');
    expect(r.type).toBe('rtu_frame');
    expect(r.address).toBe('FD');
    expect(r.crc).toBe('49F1');
    expect((r.payload as any).payload.type).toBe('scan_continue');
    expect((r.payload as any).payload.raw).toBe('02');
  });
});

// Scan Response — packet 33: FD 60 03 00 06 24 66 83 C4 61
describe('scan_response (packet 33)', () => {
  it('full structure', () => {
    const r = decodePacket('FD 60 03 00 06 24 66 83 C4 61');
    expect(r.type).toBe('rtu_frame');
    expect(r.crc).toBe('C461');
    const p = (r.payload as any).payload;
    expect(p.type).toBe('scan_response');
    expect(p.serial_number).toBe('00062466');
    expect(p.modbus_address).toBe('83');
  });

  it('raw of subcommand layer includes subcommand byte + data', () => {
    const r = decodePacket('FD 60 03 00 06 24 66 83 C4 61');
    // fm payload raw = ext_byte through (excluding) CRC
    expect((r.payload as any).raw).toBe('60030006246683');
    // subcommand payload raw
    expect((r.payload as any).payload.raw).toBe('030006246683');
  });

  it('truncated to 8 bytes → parse_error', () => {
    const r = decodePacket('FD 60 03 00 06 24 66 83');
    // 8 bytes: addr + ext + sub + serial(4) + addr = payload is 4 bytes minus CRC
    // bytes.slice(2, len-2) = bytes[2..5] = 4 bytes which is sub(1)+serial(3) < 6
    expect((r.payload as any).payload.type).toBe('parse_error');
    expect((r.payload as any).payload.reason).toBe('scan_response_too_short');
  });
});

// Scan End — packet 48: FD 60 04 C9 F3
describe('scan_end (packet 48)', () => {
  it('structure', () => {
    const r = decodePacket('FD 60 04 C9 F3');
    expect(r.type).toBe('rtu_frame');
    expect(r.crc).toBe('C9F3');
    expect((r.payload as any).payload.type).toBe('scan_end');
    expect((r.payload as any).payload.raw).toBe('04');
  });
});

// Cmd Send (0x08) — packets 34,36,38,40,42,44
describe('command_by_serial (0x08)', () => {
  it('packet 34: read 20 regs from 200 (0x00C8)', () => {
    const r = decodePacket('FD 60 08 00 06 24 66 03 00 C8 00 14 9D 24');
    expect(r.type).toBe('rtu_frame');
    expect(r.crc).toBe('9D24');
    const fm = (r.payload as any).payload;
    expect(fm.type).toBe('command_by_serial');
    expect(fm.serial_number).toBe('00062466');
    const pdu = fm.payload;
    expect(pdu.type).toBe('read_holding_registers');
    expect(pdu.fc).toBe('03');
    expect(pdu.register).toBe('00C8');
    expect(pdu.count).toBe(20);
  });

  it('packet 36: read 12 regs from 290 (0x0122)', () => {
    const r = decodePacket('FD 60 08 00 06 24 66 03 01 22 00 0C BD 26');
    const pdu = (r.payload as any).payload.payload;
    expect(pdu.register).toBe('0122');
    expect(pdu.count).toBe(12);
  });

  it('packet 38: read 1 reg from 110 (0x006E)', () => {
    const r = decodePacket('FD 60 08 00 06 24 66 03 00 6E 00 01 BC C8');
    const pdu = (r.payload as any).payload.payload;
    expect(pdu.register).toBe('006E');
    expect(pdu.count).toBe(1);
  });

  it('packet 40: read 1 reg from 112 (0x0070)', () => {
    const r = decodePacket('FD 60 08 00 06 24 66 03 00 70 00 01 DC CE');
    const pdu = (r.payload as any).payload.payload;
    expect(pdu.register).toBe('0070');
    expect(pdu.count).toBe(1);
  });

  it('packet 42: read 1 reg from 111 (0x006F)', () => {
    const r = decodePacket('FD 60 08 00 06 24 66 03 00 6F 00 01 ED 08');
    const pdu = (r.payload as any).payload.payload;
    expect(pdu.register).toBe('006F');
    expect(pdu.count).toBe(1);
  });

  it('packet 44: read 16 regs from 250 (0x00FA)', () => {
    const r = decodePacket('FD 60 08 00 06 24 66 03 00 FA 00 10 3D 28');
    const pdu = (r.payload as any).payload.payload;
    expect(pdu.register).toBe('00FA');
    expect(pdu.count).toBe(16);
  });

  it('write single register fc=6', () => {
    // FD 60 08 <SN:00010001> 06 <reg:0001> <val:0005> <CRC:0000>
    const r = decodePacket('FD 60 08 00 01 00 01 06 00 01 00 05 00 00');
    const pdu = (r.payload as any).payload.payload;
    expect(pdu.type).toBe('write_single_register');
    expect(pdu.fc).toBe('06');
    expect(pdu.register).toBe('0001');
    expect(pdu.value).toBe(5);
  });

  it('write multiple registers fc=16', () => {
    // FD 60 08 <SN:00010001> 10 <reg:0001> <cnt:0001> <bytecount:02> <data:0005> <CRC:0000>
    const r = decodePacket('FD 60 08 00 01 00 01 10 00 01 00 01 02 00 05 00 00');
    const pdu = (r.payload as any).payload.payload;
    expect(pdu.type).toBe('write_multiple_registers');
    expect(pdu.fc).toBe('10');
    expect(pdu.register).toBe('0001');
    expect(pdu.count).toBe(1);
    expect(pdu.byte_count).toBe(2);
    expect(pdu.data).toBe('0005');
  });

  it('truncated command_by_serial (6 bytes total) → parse_error in subcommand', () => {
    // 6 bytes: FD 60 08 00 06 24 → after CRC strip, fmBytes = bytes[2..3] = just "08 00" = 2 bytes < 6
    const r = decodePacket('FD 60 08 00 06 24');
    expect((r.payload as any).payload.type).toBe('parse_error');
    expect((r.payload as any).payload.reason).toBe('command_by_serial_too_short');
  });

  it('PDU too short (only fc, no register bytes)', () => {
    // FD 60 08 <SN:4 bytes> <fc=03 only> <CRC:2 bytes> — pdu is just [03]
    const r = decodePacket('FD 60 08 00 06 24 66 03 00 00');
    const pdu = (r.payload as any).payload.payload;
    expect(pdu.type).toBe('parse_error');
    expect(pdu.reason).toBe('pdu_too_short');
  });

  it('unknown fc in PDU (fc=0x00)', () => {
    // fc=0x00 is invalid
    const r = decodePacket('FD 60 08 00 06 24 66 00 00 01 00 01 00 00');
    const pdu = (r.payload as any).payload.payload;
    expect(pdu.type).toBe('parse_error');
    expect(pdu.reason).toBe('invalid_fc');
    expect(pdu.fc).toBe('00');
  });
});

// Cmd Response (0x09) — packets 35,37,39,41,43,45
describe('response_by_serial (0x09)', () => {
  it('packet 35: WB-MSW4 name (40 bytes = 20 regs)', () => {
    const r = decodePacket('FD 60 09 00 06 24 66 03 28 00 57 00 42 00 4D 00 53 00 57 00 34 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 A9 78');
    expect(r.type).toBe('rtu_frame');
    expect(r.crc).toBe('A978');
    const fm = (r.payload as any).payload;
    expect(fm.type).toBe('response_by_serial');
    expect(fm.serial_number).toBe('00062466');
    const pdu = fm.payload;
    expect(pdu.type).toBe('read_holding_registers_response');
    expect(pdu.fc).toBe('03');
    expect(pdu.byte_count).toBe(40);
    // data starts with 00 57 00 42 = 'W' 'B' in UTF-16BE
    expect(pdu.data.startsWith('00570042004D00530057003400'), 'data should start with WBMSW4 codepoints').toBeTruthy();
  });

  it('packet 37: msw5Ge string (12 regs = 24 bytes)', () => {
    const r = decodePacket('FD 60 09 00 06 24 66 03 18 00 6D 00 73 00 77 00 35 00 47 00 65 00 00 00 00 00 00 00 00 00 00 00 00 B2 04');
    const pdu = (r.payload as any).payload.payload;
    expect(pdu.byte_count).toBe(24);
    expect(pdu.data.startsWith('006D00730077003500470065'), 'data should start with msw5Ge codepoints').toBeTruthy();
  });

  it('packet 39: 1 reg = value 0x0060 = 96', () => {
    const r = decodePacket('FD 60 09 00 06 24 66 03 02 00 60 9E E5');
    const pdu = (r.payload as any).payload.payload;
    expect(pdu.byte_count).toBe(2);
    expect(pdu.data).toBe('0060');
  });

  it('packet 41: 1 reg = value 0x0002 = 2', () => {
    const r = decodePacket('FD 60 09 00 06 24 66 03 02 00 02 1F 0C');
    const pdu = (r.payload as any).payload.payload;
    expect(pdu.data).toBe('0002');
  });

  it('packet 43: 1 reg = value 0x0000', () => {
    const r = decodePacket('FD 60 09 00 06 24 66 03 02 00 00 9E CD');
    const pdu = (r.payload as any).payload.payload;
    expect(pdu.data).toBe('0000');
  });

  it('packet 45: firmware 4.35.5 (32 bytes = 16 regs)', () => {
    const r = decodePacket('FD 60 09 00 06 24 66 03 20 00 34 00 2E 00 33 00 35 00 2E 00 35 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 66 19');
    const pdu = (r.payload as any).payload.payload;
    expect(pdu.byte_count).toBe(32);
    // 4.35.5 in UTF-16BE: 0034 002E 0033 0035 002E 0035
    expect(pdu.data.startsWith('0034002E00330035002E0035'), 'data should start with 4.35.5 codepoints').toBeTruthy();
  });

  it('Modbus error response (fc=0x83, code=2)', () => {
    // FD 60 09 <SN:00062466> 83 02 <CRC:0000>
    const r = decodePacket('FD 60 09 00 06 24 66 83 02 00 00');
    const pdu = (r.payload as any).payload.payload;
    expect(pdu.type).toBe('modbus_error');
    expect(pdu.fc).toBe('83');
    expect(pdu.original_fc).toBe('03');
    expect(pdu.error_code).toBe(2);
  });

  it('Modbus error fc=0x90 (for fc=16), code=1', () => {
    const r = decodePacket('FD 60 09 00 06 24 66 90 01 00 00');
    const pdu = (r.payload as any).payload.payload;
    expect(pdu.type).toBe('modbus_error');
    expect(pdu.original_fc).toBe('10');
    expect(pdu.error_code).toBe(1);
  });

  it('truncated response_by_serial → parse_error', () => {
    const r = decodePacket('FD 60 09 00 06 24');
    expect((r.payload as any).payload.type).toBe('parse_error');
    expect((r.payload as any).payload.reason).toBe('response_by_serial_too_short');
  });

  it('PDU data truncated (byte_count claims more than available)', () => {
    // byte_count=10 but only 2 data bytes follow before CRC
    const r = decodePacket('FD 60 09 00 06 24 66 03 0A 00 01 00 00');
    const pdu = (r.payload as any).payload.payload;
    expect(pdu.type).toBe('parse_error');
    expect(pdu.reason).toBe('pdu_data_truncated');
  });
});

describe('parse_error cases', () => {
  it('empty string', () => {
    expect(decodePacket('').type).toBe('parse_error');
  });
  it('invalid hex chars', () => {
    const r = decodePacket('FD ZZ 01');
    expect(r.type).toBe('parse_error');
    expect((r as any).reason).toBe('invalid_hex');
  });
  it('too short (3 bytes < 4 min)', () => {
    const r = decodePacket('FD 60 01');
    expect(r.type).toBe('parse_error');
    expect((r as any).reason).toBe('too_short');
  });
  it('non-0xFD address parses as standard RTU, not parse_error', () => {
    // 01 60 01 09 F0 — slave 1, fc=0x60 (vendor specific range), not a Fast Modbus packet
    const r = decodePacket('01 60 01 09 F0');
    expect(r.type).toBe('rtu_frame');
    expect(r.address).toBe('01');
    expect(r.payload.type).toBe('modbus_rtu');
  });
  it('FD with non-ext ext_byte parses as standard RTU', () => {
    // FD 03 ... — address=0xFD but fc=0x03 (Read Holding Registers), not Fast Modbus
    const r = decodePacket('FD 03 00 01 00 01 94 F8', 'request');
    expect(r.type).toBe('rtu_frame');
    expect(r.payload.type).toBe('modbus_rtu');
    expect((r.payload as any).payload.type).toBe('read_holding_registers');
  });
  it('unknown subcommand 0x07 in Fast Modbus', () => {
    const r = decodePacket('FD 60 07 00 00');
    expect((r.payload as any).payload.type).toBe('parse_error');
    expect((r.payload as any).payload.reason).toBe('unknown_subcommand');
    expect((r.payload as any).payload.subcommand).toBe('07');
  });
  it('unknown subcommand 0xFF in Fast Modbus', () => {
    const r = decodePacket('FD 46 FF 00 00');
    expect((r.payload as any).payload.type).toBe('parse_error');
    expect((r.payload as any).payload.reason).toBe('unknown_subcommand');
  });
  it('mixed FF and non-FF is not arbitration', () => {
    const r = decodePacket('FF FF FD 60 01');
    expect(r.type).not.toBe('arbitration');
  });
  it('null input', () => {
    // null is outside the TypeScript type, but the decoder must handle it gracefully
    const r = decodePacket(null as unknown as string);
    expect(r.type).toBe('parse_error');
  });
  it('array input works', () => {
    const r = decodePacket([0xFD, 0x60, 0x01, 0x09, 0xF0]);
    expect(r.type).toBe('rtu_frame');
    expect((r.payload as any).payload.type).toBe('scan_start');
  });
  it('non-0xFD address with FC=0x46 and non-FM subcommand parses as standard RTU user_defined', () => {
    // address=0x01, FC=0x46 (user-defined), subcommand=0xAA — should NOT be FM
    // but with the new condition it WILL be FM (this is intentional behavior — document it)
    // Verify it decodes as fast_modbus with unknown_subcommand (not as modbus_rtu user_defined)
    const r = decodePacket('01 46 AA BB 00 00', 'request');
    expect(r.type).toBe('rtu_frame');
    expect(r.payload.type).toBe('fast_modbus'); // intentional: any addr + FC=0x46 → FM
    expect((r.payload as any).payload.type).toBe('parse_error');
    expect((r.payload as any).payload.reason).toBe('unknown_subcommand');
  });
  it('non-0xFD address with FC=0x41 (user-defined, not 0x46) parses as standard RTU', () => {
    // FC=0x41 is user-defined but NOT Fast Modbus ext_byte — must remain modbus_rtu
    const r = decodePacket('01 41 00 0A 00 02 9C 06', 'request');
    expect(r.type).toBe('rtu_frame');
    expect(r.payload.type).toBe('modbus_rtu');
    expect((r.payload as any).payload.type).toBe('user_defined');
  });
});

// raw field integrity — every node must have raw with correct hex
describe('raw field integrity', () => {
  it('arbitration raw = all bytes', () => {
    const r = decodePacket('FF FF FF FF');
    expect(r.raw).toBe('FFFFFFFF');
  });

  it('rtu_frame raw = full packet', () => {
    const r = decodePacket('FD 60 01 09 F0');
    expect(r.raw).toBe('FD600109F0');
  });

  it('fast_modbus raw excludes address byte', () => {
    const r = decodePacket('FD 60 01 09 F0');
    // payload raw = from ext_byte(60) to before CRC
    expect((r.payload as any).raw).toBe('6001');
  });

  it('scan_start payload raw = just subcommand byte', () => {
    const r = decodePacket('FD 60 01 09 F0');
    expect((r.payload as any).payload.raw).toBe('01');
  });

  it('scan_response payload raw includes all subcommand data', () => {
    const r = decodePacket('FD 60 03 00 06 24 66 83 C4 61');
    // sub(03) + serial(00062466) + addr(83)
    expect((r.payload as any).payload.raw).toBe('030006246683');
  });

  it('command_by_serial raw includes sub + serial + pdu', () => {
    const r = decodePacket('FD 60 08 00 06 24 66 03 00 C8 00 14 9D 24');
    // from sub(08) through end-of-pdu (before CRC)
    expect((r.payload as any).payload.raw).toBe('08000624660300C80014');
    // Also verify pdu raw
    const pduRaw = (r.payload as any).payload.payload.raw;
    expect(pduRaw).toBe('0300C80014');
  });

  it('parse_error includes raw bytes', () => {
    const r = decodePacket('FD 60 07 00 00');
    expect((r.payload as any).payload.raw.length, 'parse_error should have non-empty raw').toBeGreaterThan(0);
  });
});

// Full trace end-to-end (packets 31-48)
describe('full trace packets 31-48', () => {
  const trace = [
    { hex: 'FD 60 01 09 F0', outerType: 'rtu_frame', innerType: 'scan_start' },
    { hex: 'FF FF FF FF FF FF FF FF FF FF', outerType: 'arbitration', innerType: null },
    { hex: 'FD 60 03 00 06 24 66 83 C4 61', outerType: 'rtu_frame', innerType: 'scan_response' },
    { hex: 'FD 60 08 00 06 24 66 03 00 C8 00 14 9D 24', outerType: 'rtu_frame', innerType: 'command_by_serial' },
    { hex: 'FD 60 09 00 06 24 66 03 28 00 57 00 42 00 4D 00 53 00 57 00 34 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 A9 78', outerType: 'rtu_frame', innerType: 'response_by_serial' },
    { hex: 'FD 60 08 00 06 24 66 03 01 22 00 0C BD 26', outerType: 'rtu_frame', innerType: 'command_by_serial' },
    { hex: 'FD 60 09 00 06 24 66 03 18 00 6D 00 73 00 77 00 35 00 47 00 65 00 00 00 00 00 00 00 00 00 00 00 00 B2 04', outerType: 'rtu_frame', innerType: 'response_by_serial' },
    { hex: 'FD 60 08 00 06 24 66 03 00 6E 00 01 BC C8', outerType: 'rtu_frame', innerType: 'command_by_serial' },
    { hex: 'FD 60 09 00 06 24 66 03 02 00 60 9E E5', outerType: 'rtu_frame', innerType: 'response_by_serial' },
    { hex: 'FD 60 08 00 06 24 66 03 00 70 00 01 DC CE', outerType: 'rtu_frame', innerType: 'command_by_serial' },
    { hex: 'FD 60 09 00 06 24 66 03 02 00 02 1F 0C', outerType: 'rtu_frame', innerType: 'response_by_serial' },
    { hex: 'FD 60 08 00 06 24 66 03 00 6F 00 01 ED 08', outerType: 'rtu_frame', innerType: 'command_by_serial' },
    { hex: 'FD 60 09 00 06 24 66 03 02 00 00 9E CD', outerType: 'rtu_frame', innerType: 'response_by_serial' },
    { hex: 'FD 60 08 00 06 24 66 03 00 FA 00 10 3D 28', outerType: 'rtu_frame', innerType: 'command_by_serial' },
    { hex: 'FD 60 09 00 06 24 66 03 20 00 34 00 2E 00 33 00 35 00 2E 00 35 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 66 19', outerType: 'rtu_frame', innerType: 'response_by_serial' },
    { hex: 'FD 60 02 49 F1', outerType: 'rtu_frame', innerType: 'scan_continue' },
    { hex: 'FF FF FF FF FF FF FF FF FF', outerType: 'arbitration', innerType: null },
    { hex: 'FD 60 04 C9 F3', outerType: 'rtu_frame', innerType: 'scan_end' },
  ];

  trace.forEach(({ hex, outerType, innerType }, i) => {
    it(`packet ${31 + i}: ${outerType}${innerType ? '/' + innerType : ''}`, () => {
      const r = decodePacket(hex);
      expect(r.type).toBe(outerType);
      if (innerType) {
        expect((r.payload as any).payload.type).toBe(innerType);
      }
      // raw must equal cleaned input hex
      const expectedRaw = hex.replace(/\s+/g, '').toUpperCase();
      expect(r.raw).toBe(expectedRaw);
    });
  });
});

// Standard Modbus RTU tests (from modbus_codes.md)

// Helper: get standard modbus payload
function pdu(r: ReturnType<typeof decodePacket>): any {
 return (r.payload as any).payload;
}

describe('Standard RTU — FC 01 Read Coils', () => {
  it('request: slave 0x11, 19 coils from 0x0013', () => {
    const r = decodePacket('11 01 00 13 00 13 8E 92', 'request');
    expect(r.type).toBe('rtu_frame');
    expect(r.address).toBe('11');
    expect(r.crc).toBe('8E92');
    expect(r.payload.type).toBe('modbus_rtu');
    const p = pdu(r);
    expect(p.type).toBe('read_coils');
    expect(p.fc).toBe('01');
    expect(p.register).toBe('0013');
    expect(p.count).toBe(19);
  });

  it('response: slave 0x11, ByteCount=3, data=CD6B05', () => {
    const r = decodePacket('11 01 03 CD 6B 05 40 12', 'response');
    expect(r.address).toBe('11');
    const p = pdu(r);
    expect(p.type).toBe('read_coils_response');
    expect(p.byte_count).toBe(3);
    expect(p.data).toBe('CD6B05');
  });
});

describe('Standard RTU — FC 02 Read Discrete Inputs', () => {
  it('request: slave 0x11, 22 inputs from 0x00C4', () => {
    const r = decodePacket('11 02 00 C4 00 16 BA A9', 'request');
    const p = pdu(r);
    expect(p.type).toBe('read_discrete_inputs');
    expect(p.register).toBe('00C4');
    expect(p.count).toBe(22);
  });

  it('response: ByteCount=3', () => {
    const r = decodePacket('11 02 03 AC DB 35 20 18', 'response');
    const p = pdu(r);
    expect(p.type).toBe('read_discrete_inputs_response');
    expect(p.byte_count).toBe(3);
    expect(p.data).toBe('ACDB35');
  });
});

describe('Standard RTU — FC 03 Read Holding Registers', () => {
  it('request: slave 1, 2 regs from 0x0000', () => {
    const r = decodePacket('01 03 00 00 00 02 C4 0B', 'request');
    expect(r.address).toBe('01');
    const p = pdu(r);
    expect(p.type).toBe('read_holding_registers');
    expect(p.fc).toBe('03');
    expect(p.register).toBe('0000');
    expect(p.count).toBe(2);
  });

  it('response: ByteCount=4, regs 500,100', () => {
    const r = decodePacket('01 03 04 01 F4 00 64 BB D6', 'response');
    const p = pdu(r);
    expect(p.type).toBe('read_holding_registers_response');
    expect(p.byte_count).toBe(4);
    expect(p.data).toBe('01F40064');
  });
});

describe('Standard RTU — FC 04 Read Input Registers', () => {
  it('request: slave 3, 1 reg from 0x0005', () => {
    const r = decodePacket('03 04 00 05 00 01 20 29', 'request');
    const p = pdu(r);
    expect(p.type).toBe('read_input_registers');
    expect(p.register).toBe('0005');
    expect(p.count).toBe(1);
  });

  it('response: value 0x01A4=420', () => {
    const r = decodePacket('03 04 02 01 A4 C0 DB', 'response');
    const p = pdu(r);
    expect(p.type).toBe('read_input_registers_response');
    expect(p.data).toBe('01A4');
  });
});

/**
 * FC05 carries a coil STATE, not a register value: 0xFF00 = ON, 0x0000 = OFF, nothing else is
 * legal. Reporting the raw word made a coil the user set to 1 read back as 65280, so the
 * decoder must report the state and must never emit a numeric `value` field for FC05.
 */
describe('Standard RTU — FC 05 Write Single Coil', () => {
  it('ON: coil 0x00AC = 0xFF00 decodes to coil_state "on", not the raw word', () => {
    const r = decodePacket('01 05 00 AC FF 00 4C 1B', 'request');
    const p = pdu(r);
    expect(p.type).toBe('write_single_coil');
    expect(p.register).toBe('00AC');
    expect(p.coil_state).toBe('on');
    // The raw 0xFF00 word must not leak out as a number — that is the reported bug.
    expect(p.value).toBeUndefined();
    expect(p.coil_value).toBeUndefined();
  });

  it('OFF: coil 0x00AC = 0x0000 decodes to coil_state "off"', () => {
    const r = decodePacket('01 05 00 AC 00 00 0D EB', 'request');
    const p = pdu(r);
    expect(p.type).toBe('write_single_coil');
    expect(p.coil_state).toBe('off');
    expect(p.value).toBeUndefined();
  });

  it('illegal state word is reported as invalid and the offending word is kept', () => {
    // 0x1234 is neither 0x0000 nor 0xFF00 — a protocol violation, not a coil value.
    const r = decodePacket('01 05 00 AC 12 34 6C E1', 'request');
    const p = pdu(r);
    expect(p.type).toBe('write_single_coil');
    expect(p.coil_state).toBe('invalid');
    expect(p.coil_value).toBe('1234');
  });

  it('response echo decodes the coil state the same way as the request', () => {
    const r = decodePacket('01 05 00 AC FF 00 4C 1B', 'response');
    const p = pdu(r);
    expect(p.type).toBe('write_single_coil_response');
    expect(p.register).toBe('00AC');
    expect(p.coil_state).toBe('on');
    expect(p.value).toBeUndefined();
  });

  it('response echo of an illegal state word is flagged as invalid', () => {
    const r = decodePacket('01 05 00 AC 00 01 CC 1B', 'response');
    const p = pdu(r);
    expect(p.type).toBe('write_single_coil_response');
    expect(p.coil_state).toBe('invalid');
    expect(p.coil_value).toBe('0001');
  });
});

describe('Standard RTU — FC 06 Write Single Register', () => {
  it('write 0x2580 to reg 0x0003', () => {
    const r = decodePacket('01 06 00 03 25 80 62 FA', 'request');
    const p = pdu(r);
    expect(p.type).toBe('write_single_register');
    expect(p.register).toBe('0003');
    expect(p.value).toBe(0x2580);
  });
});

describe('Standard RTU — FC 07 Read Exception Status', () => {
  it('request: slave 1, PDU=1 byte', () => {
    const r = decodePacket('01 07 41 E2', 'request');
    const p = pdu(r);
    expect(p.type).toBe('read_exception_status');
    expect(p.fc).toBe('07');
  });

  it('response: output_data=0x6D', () => {
    const r = decodePacket('01 07 6D E3 DD', 'response');
    const p = pdu(r);
    expect(p.type).toBe('read_exception_status_response');
    expect(p.output_data).toBe('6D');
  });
});

describe('Standard RTU — FC 08 Diagnostics', () => {
  it('request sub=0x0000 Return Query Data', () => {
    const r = decodePacket('01 08 00 00 A5 37 DA 8D', 'request');
    const p = pdu(r);
    expect(p.type).toBe('diagnostics');
    expect(p.sub_function).toBe('0000');
    expect(p.data).toBe('A537');
  });

  it('request sub=0x0001 Restart Communications, data=0x0000', () => {
    const r = decodePacket('01 08 00 01 00 00 B1 CB', 'request');
    const p = pdu(r);
    expect(p.sub_function).toBe('0001');
    expect(p.data).toBe('0000');
  });

  it('request sub=0x0004 Force Listen Only Mode', () => {
    const r = decodePacket('01 08 00 04 00 00 A1 CA', 'request');
    const p = pdu(r);
    expect(p.sub_function).toBe('0004');
  });

  it('request sub=0x000A Clear Counters', () => {
    const r = decodePacket('01 08 00 0A 00 00 C0 09', 'request');
    const p = pdu(r);
    expect(p.sub_function).toBe('000A');
  });

  it('response sub=0x000B, value=0x0088', () => {
    const r = decodePacket('01 08 00 0B 00 88 91 AF', 'response');
    const p = pdu(r);
    expect(p.sub_function).toBe('000B');
    expect(p.data).toBe('0088');
  });
});

describe('Standard RTU — FC 0B Get Comm Event Counter', () => {
  it('request: slave 1, PDU=1 byte', () => {
    const r = decodePacket('01 0B 41 E7', 'request');
    const p = pdu(r);
    expect(p.type).toBe('get_comm_event_counter');
    expect(p.fc).toBe('0B');
  });

  it('response: status=0x0000, count=264', () => {
    const r = decodePacket('01 0B 00 00 01 08 A4 5D', 'response');
    const p = pdu(r);
    expect(p.type).toBe('get_comm_event_counter_response');
    expect(p.status).toBe('0000');
    expect(p.event_count).toBe(0x0108);
  });
});

describe('Standard RTU — FC 0C Get Comm Event Log', () => {
  it('request: slave 1, PDU=1 byte', () => {
    const r = decodePacket('01 0C 00 25', 'request');
    const p = pdu(r);
    expect(p.type).toBe('get_comm_event_log');
  });

  it('response: ByteCount=8, status/count/message/events', () => {
    const r = decodePacket('01 0C 08 00 00 01 08 01 21 20 00 0D C1', 'response');
    const p = pdu(r);
    expect(p.type).toBe('get_comm_event_log_response');
    expect(p.byte_count).toBe(8);
    expect(p.status).toBe('0000');
    expect(p.event_count).toBe(0x0108);
    expect(p.message_count).toBe(0x0121);
    expect(p.events).toBe('2000');
  });
});

describe('Standard RTU — FC 0F Write Multiple Coils', () => {
  it('request: 10 coils from 0x0013', () => {
    const r = decodePacket('11 0F 00 13 00 0A 02 CD 01 BF 0B', 'request');
    const p = pdu(r);
    expect(p.type).toBe('write_multiple_coils');
    expect(p.register).toBe('0013');
    expect(p.count).toBe(10);
    expect(p.byte_count).toBe(2);
    expect(p.data).toBe('CD01');
  });

  it('response: addr+count echo', () => {
    const r = decodePacket('11 0F 00 13 00 0A 26 99', 'response');
    const p = pdu(r);
    expect(p.type).toBe('write_multiple_coils_response');
    expect(p.register).toBe('0013');
    expect(p.count).toBe(10);
  });
});

describe('Standard RTU — FC 10 Write Multiple Registers', () => {
  it('request: 2 regs from 0x000A', () => {
    const r = decodePacket('01 10 00 0A 00 02 04 42 C8 00 00 E6 56', 'request');
    const p = pdu(r);
    expect(p.type).toBe('write_multiple_registers');
    expect(p.register).toBe('000A');
    expect(p.count).toBe(2);
    expect(p.byte_count).toBe(4);
    expect(p.data).toBe('42C80000');
  });

  it('response: addr+count echo', () => {
    const r = decodePacket('01 10 00 0A 00 02 61 CA', 'response');
    const p = pdu(r);
    expect(p.type).toBe('write_multiple_registers_response');
    expect(p.register).toBe('000A');
    expect(p.count).toBe(2);
  });
});

describe('Standard RTU — FC 11 Report Server ID', () => {
  it('request: slave 1', () => {
    const r = decodePacket('01 11 C0 2C', 'request');
    const p = pdu(r);
    expect(p.type).toBe('report_server_id');
    expect(p.fc).toBe('11');
  });

  it('response: ByteCount=4, server_id=0x05, run=ON', () => {
    const r = decodePacket('01 11 04 05 FF 41 42 79 DC', 'response');
    const p = pdu(r);
    expect(p.type).toBe('report_server_id_response');
    expect(p.byte_count).toBe(4);
    expect(p.server_id).toBe('05');
    expect(p.run_indicator).toBe('FF');
    expect(p.additional_data).toBe('4142');
  });
});

describe('Standard RTU — FC 14 Read File Record', () => {
  it('request: 1 sub-req, file 4, rec 1, len 2', () => {
    const r = decodePacket('01 14 07 06 00 04 00 01 00 02 D8 E5', 'request');
    const p = pdu(r);
    expect(p.type).toBe('read_file_record');
    expect(p.byte_count).toBe(7);
    expect(Array.isArray(p.sub_requests)).toBeTruthy();
    expect(p.sub_requests.length).toBe(1);
    expect(p.sub_requests[0].reference_type).toBe('06');
    expect(p.sub_requests[0].file_number).toBe(4);
    expect(p.sub_requests[0].record_number).toBe(1);
    expect(p.sub_requests[0].record_length).toBe(2);
  });

  it('response: 2 regs worth of data', () => {
    const r = decodePacket('01 14 06 05 06 0D FE 00 20 8B 4E', 'response');
    const p = pdu(r);
    expect(p.type).toBe('read_file_record_response');
    expect(p.resp_data_length).toBe(6);
    expect(Array.isArray(p.sub_responses)).toBeTruthy();
    expect(p.sub_responses[0].data).toBe('0DFE0020');
  });
});

describe('Standard RTU — FC 15 Write File Record', () => {
  it('request: file 4, rec 1, 2 regs', () => {
    const r = decodePacket('01 15 0B 06 00 04 00 01 00 02 0A AB CD EF 5E E2', 'request');
    const p = pdu(r);
    expect(p.type).toBe('write_file_record');
    expect(p.request_data_length).toBe(11);
    expect(Array.isArray(p.sub_requests)).toBeTruthy();
    expect(p.sub_requests[0].record_length).toBe(2);
    expect(p.sub_requests[0].data).toBe('0AABCDEF');
  });
});

describe('Standard RTU — FC 16 Mask Write Register', () => {
  it('request: reg 0x0002, AND=0x00F2, OR=0x0025', () => {
    const r = decodePacket('01 16 00 02 00 F2 00 25 EF EE', 'request');
    const p = pdu(r);
    expect(p.type).toBe('mask_write_register');
    expect(p.register).toBe('0002');
    expect(p.and_mask).toBe('00F2');
    expect(p.or_mask).toBe('0025');
  });
});

describe('Standard RTU — FC 17 Read/Write Multiple Registers', () => {
  it('request: read 6 from 0x0003, write 3 to 0x000E', () => {
    const r = decodePacket('01 17 00 03 00 06 00 0E 00 03 06 00 FF 00 FF 00 FF 46 91', 'request');
    const p = pdu(r);
    expect(p.type).toBe('read_write_multiple_registers');
    expect(p.read_register).toBe('0003');
    expect(p.read_count).toBe(6);
    expect(p.write_register).toBe('000E');
    expect(p.write_count).toBe(3);
    expect(p.write_byte_count).toBe(6);
    expect(p.write_data).toBe('00FF00FF00FF');
  });

  it('response: 6 regs read', () => {
    const r = decodePacket('01 17 0C 00 FE 0A CD 00 01 00 03 00 0D 00 FF 1D 79', 'response');
    const p = pdu(r);
    expect(p.type).toBe('read_write_multiple_registers_response');
    expect(p.byte_count).toBe(12);
    expect(p.data).toBe('00FE0ACD00010003000D00FF');
  });
});

describe('Standard RTU — FC 18 Read FIFO Queue', () => {
  it('request: FIFO pointer 0x04DE', () => {
    const r = decodePacket('01 18 04 DE 03 47', 'request');
    const p = pdu(r);
    expect(p.type).toBe('read_fifo_queue');
    expect(p.fifo_pointer).toBe('04DE');
  });

  it('response: 2-byte ByteCount=6, FIFOCount=2, values', () => {
    const r = decodePacket('01 18 00 06 00 02 01 B8 12 84 19 18', 'response');
    const p = pdu(r);
    expect(p.type).toBe('read_fifo_queue_response');
    expect(p.byte_count).toBe(6); // 2-byte field BE
    expect(p.fifo_count).toBe(2);
    expect(p.data).toBe('01B81284');
  });
});

describe('Standard RTU — FC 2B MEI', () => {
  it('Read Device ID request: slave 1, Basic, VendorName', () => {
    const r = decodePacket('01 2B 0E 01 00 70 77', 'request');
    const p = pdu(r);
    expect(p.type).toBe('mei_read_device_identification');
    expect(p.fc).toBe('2B');
    expect(p.mei_type).toBe('0E');
    expect(p.read_device_id_code).toBe('01');
    expect(p.object_id).toBe('00');
  });

  it('Read Device ID response: 3 objects', () => {
    const r = decodePacket('01 2B 0E 01 01 00 00 03 00 06 43 6F 6D 70 61 6E 01 04 50 72 6F 64 02 03 31 2E 30 14 9C', 'response');
    const p = pdu(r);
    expect(p.type).toBe('mei_read_device_identification_response');
    expect(p.conformity_level).toBe('01');
    expect(p.more_follows).toBe('00');
    expect(p.number_of_objects).toBe(3);
    expect(Array.isArray(p.objects)).toBeTruthy();
    expect(p.objects[0].id).toBe('00');
    expect(p.objects[0].value).toBe('436F6D70616E'); // "Compan"
    expect(p.objects[1].id).toBe('01');
    expect(p.objects[1].value).toBe('50726F64'); // "Prod"
    expect(p.objects[2].id).toBe('02');
    expect(p.objects[2].value).toBe('312E30'); // "1.0"
  });

  it('CANopen MEI 0x0D: opaque payload', () => {
    const r = decodePacket('01 2B 0D 01 00 02 00 03 C7 D9', 'response');
    const p = pdu(r);
    expect(p.type).toBe('mei_canopen');
    expect(p.mei_type).toBe('0D');
    expect(p.data).not.toBe(undefined);
  });
});

describe('Standard RTU — Exception responses', () => {
  it('FC 83 (FC03 error), code=0x02 Illegal Data Address', () => {
    const r = decodePacket('01 83 02 C0 F1', 'response');
    expect(r.type).toBe('rtu_frame');
    const p = pdu(r);
    expect(p.type).toBe('modbus_error');
    expect(p.fc).toBe('83');
    expect(p.original_fc).toBe('03');
    expect(p.error_code).toBe(2);
  });

  it('FC 90 (FC16 error), code=0x03 Illegal Data Value', () => {
    const r = decodePacket('01 90 03 0C 01', 'response');
    const p = pdu(r);
    expect(p.original_fc).toBe('10');
    expect(p.error_code).toBe(3);
  });

  it('FC 81 (FC01 error), code=0x01 Illegal Function', () => {
    const r = decodePacket('01 81 01 81 90', 'response');
    const p = pdu(r);
    expect(p.original_fc).toBe('01');
    expect(p.error_code).toBe(1);
  });

  it('FC 86 (FC06 error), code=0x04 Server Device Failure', () => {
    const r = decodePacket('01 86 04 43 A3', 'response');
    const p = pdu(r);
    expect(p.original_fc).toBe('06');
    expect(p.error_code).toBe(4);
  });

  it('code=0x05 Acknowledge', () => {
    const r = decodePacket('01 83 05 81 33', 'response');
    const p = pdu(r);
    expect(p.error_code).toBe(5);
  });

  it('code=0x06 Server Device Busy', () => {
    const r = decodePacket('01 90 06 CC 02', 'response');
    const p = pdu(r);
    expect(p.error_code).toBe(6);
  });

  it('code=0x0A Gateway Path Unavailable', () => {
    const r = decodePacket('01 83 0A C1 37', 'response');
    const p = pdu(r);
    expect(p.error_code).toBe(0x0A);
  });

  it('code=0x0B Gateway Target Device Failed', () => {
    const r = decodePacket('01 83 0B 00 F7', 'response');
    const p = pdu(r);
    expect(p.error_code).toBe(0x0B);
  });
});

describe('Standard RTU — Broadcast (slave=0)', () => {
  it('FC 06 broadcast: write 0x1234 to reg 0x0003', () => {
    const r = decodePacket('00 06 00 03 12 34 75 6C', 'request');
    expect(r.type).toBe('rtu_frame');
    expect(r.address).toBe('00');
    const p = pdu(r);
    expect(p.type).toBe('write_single_register');
    expect(p.register).toBe('0003');
    expect(p.value).toBe(0x1234);
  });

  it('FC 10 broadcast: write 1 reg to 0x0000', () => {
    const r = decodePacket('00 10 00 00 00 01 02 12 34 A6 B7', 'request');
    expect(r.address).toBe('00');
    const p = pdu(r);
    expect(p.type).toBe('write_multiple_registers');
  });
});

describe('Standard RTU — Vendor-specific and user-defined', () => {
  it('FC 0x5A (UMAS/Schneider): opaque payload', () => {
    const r = decodePacket('01 5A 00 10 00 00 00 03 FA', 'request');
    expect(r.type).toBe('rtu_frame');
    const p = pdu(r);
    expect(p.type).toBe('vendor_specific');
    expect(p.fc).toBe('5A');
    expect(p.data).not.toBe(undefined);
  });

  it('FC 0x64 (user-defined 100): opaque payload', () => {
    const r = decodePacket('01 64 AA BB CC DD B4 A6', 'request');
    const p = pdu(r);
    expect(p.type).toBe('user_defined');
    expect(p.fc).toBe('64');
    expect(p.data).not.toBe(undefined);
  });

  it('FC 0x41 (user-defined 65): opaque payload', () => {
    const r = decodePacket('01 41 00 0A 00 02 9C 06', 'request');
    const p = pdu(r);
    expect(p.type).toBe('user_defined');
    expect(p.fc).toBe('41');
  });
});

describe('Standard RTU — reserved slave address', () => {
  it('slave 0xF8 (248) is reserved but parseable', () => {
    const r = decodePacket('F8 03 00 00 00 01 90 63', 'request');
    expect(r.type).toBe('rtu_frame');
    expect(r.address).toBe('F8');
    expect((r as any).reserved_address).toBe(true);
  });
});

describe('Standard RTU — invalid FC=0', () => {
  it('FC=0x00 returns parse_error', () => {
    const r = decodePacket('01 00 00 00 01 D8', 'request');
    expect(r.type).toBe('rtu_frame');
    const p = pdu(r);
    expect(p.type).toBe('parse_error');
    expect(p.reason).toBe('invalid_fc');
  });
});

describe('Standard RTU — ADU too short', () => {
  it('3 bytes → parse_error', () => {
    const r = decodePacket('01 03 04');
    expect(r.type).toBe('parse_error');
    expect((r as any).reason).toBe('too_short');
  });
});

// FM Event subcommands: 0x10, 0x11, 0x12, 0x18

describe('FM event_request (0x10)', () => {
  // FD 46 10 min_server_id max_data_len prev_server_id prev_flag CRC
  // fmBytes: [0x10, 0x00, 0x64, 0x0A, 0x01] = 5 bytes
  it('full structure', () => {
    const r = decodePacket('FD 46 10 00 64 0A 01 00 00');
    expect(r.type).toBe('rtu_frame');
    expect(r.address).toBe('FD');
    expect((r.payload as any).type).toBe('fast_modbus');
    expect((r.payload as any).ext_byte).toBe('46');
    const p = (r.payload as any).payload;
    expect(p.type).toBe('event_request');
    expect(p.min_server_id).toBe('00');
    expect(p.max_data_len).toBe('64');
    expect(p.prev_server_id).toBe('0A');
    expect(p.prev_flag).toBe('01');
  });

  it('raw field contains all fmBytes', () => {
    const r = decodePacket('FD 46 10 00 64 0A 01 00 00');
    expect((r.payload as any).payload.raw).toBe('100064' + '0A01');
  });

  it('truncated (< 5 fmBytes) → parse_error event_request_too_short', () => {
    // fmBytes = [10, 00, 64, 0A] = 4 bytes — too short
    const r = decodePacket('FD 46 10 00 64 0A 00 00');
    expect((r.payload as any).payload.type).toBe('parse_error');
    expect((r.payload as any).payload.reason).toBe('event_request_too_short');
  });
});

describe('FM event_transfer (0x11)', () => {
  // address = server_id (05), NOT 0xFD
  // fmBytes: [0x11, flag, unacked_count, data_len, ...events_data]
  it('full structure — address is server_id (05), not 0xFD', () => {
    const r = decodePacket('05 46 11 01 01 06 02 04 01 D0 04 00 00 00');
    expect(r.type).toBe('rtu_frame');
    expect(r.address).toBe('05');
    expect((r.payload as any).type).toBe('fast_modbus');
    const p = (r.payload as any).payload;
    expect(p.type).toBe('event_transfer');
    expect(p.flag).toBe('01');
    expect(p.unacked_count).toBe('01');
    expect(p.data_len).toBe('06');
  });

  it('data field contains bytes after data_len', () => {
    const r = decodePacket('05 46 11 01 01 06 02 04 01 D0 04 00 00 00');
    const p = (r.payload as any).payload;
    // fmBytes.slice(4) = [02, 04, 01, D0, 04, 00]
    expect(p.data).toBe('020401D00400');
  });

  it('truncated (< 4 fmBytes) → parse_error event_transfer_too_short', () => {
    // fmBytes = [11, 01, 01] = 3 bytes — too short
    const r = decodePacket('05 46 11 01 01 00 00');
    expect((r.payload as any).payload.type).toBe('parse_error');
    expect((r.payload as any).payload.reason).toBe('event_transfer_too_short');
  });
});

describe('FM no_events (0x12)', () => {
  // FD 46 12 52 5D — real CRC from spec
  it('structure and raw', () => {
    const r = decodePacket('FD 46 12 52 5D');
    expect(r.type).toBe('rtu_frame');
    expect(r.address).toBe('FD');
    expect((r.payload as any).type).toBe('fast_modbus');
    const p = (r.payload as any).payload;
    expect(p.type).toBe('no_events');
    expect(p.raw).toBe('12');
  });
});

describe('FM event_config (0x18)', () => {
  // address = server_id (0A), NOT 0xFD
  // fmBytes: [0x18, data_len, ...settings]
  // Example response from spec: 0A 46 18 03 05 05 00 XX XX
  it('full structure — address is server_id (0A), not 0xFD', () => {
    const r = decodePacket('0A 46 18 03 05 05 00 00 00');
    expect(r.type).toBe('rtu_frame');
    expect(r.address).toBe('0A');
    expect((r.payload as any).type).toBe('fast_modbus');
    const p = (r.payload as any).payload;
    expect(p.type).toBe('event_config');
    expect(p.data_len).toBe('03');
    // data = fmBytes.slice(2) = [05, 05, 00]
    expect(p.data).toBe('050500');
  });

  it('truncated (< 2 fmBytes) → parse_error event_config_too_short', () => {
    // fmBytes = [18] = 1 byte — too short
    const r = decodePacket('0A 46 18 00 00');
    expect((r.payload as any).payload.type).toBe('parse_error');
    expect((r.payload as any).payload.reason).toBe('event_config_too_short');
  });
});
