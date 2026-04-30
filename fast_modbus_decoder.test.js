'use strict';

const { decodePacket, parseHex } = require('./fast_modbus_decoder.js');

// ============================================================
// Mini test runner
// ============================================================
let passed = 0, failed = 0;

function test(name, fn) {
  try {
    fn();
    console.log(`  ✓ ${name}`);
    passed++;
  } catch (e) {
    console.error(`  ✗ ${name}`);
    console.error(`    ${e.message}`);
    failed++;
  }
}

function group(name, fn) {
  console.log(`\n${name}`);
  fn();
}

function assert(cond, msg) {
  if (!cond) throw new Error(msg || 'assertion failed');
}

function eq(a, b, msg) {
  if (a !== b) throw new Error(`${msg || ''}: got ${JSON.stringify(a)}, want ${JSON.stringify(b)}`);
}

function deepEq(a, b, msg) {
  const as = JSON.stringify(a), bs = JSON.stringify(b);
  if (as !== bs) throw new Error(`${msg || 'deep eq failed'}:\n  got  ${as}\n  want ${bs}`);
}

// ============================================================
// parseHex
// ============================================================
group('parseHex', () => {
  test('spaces', () => deepEq(parseHex('FD 60 01'), [0xFD, 0x60, 0x01]));
  test('no spaces', () => deepEq(parseHex('FD6001'), [0xFD, 0x60, 0x01]));
  test('lowercase', () => deepEq(parseHex('fd 60 01'), [0xFD, 0x60, 0x01]));
  test('mixed case', () => deepEq(parseHex('Fd 60 01'), [0xFD, 0x60, 0x01]));
  test('empty → null', () => eq(parseHex(''), null));
  test('spaces only → null', () => eq(parseHex('   '), null));
  test('odd length → null', () => eq(parseHex('FD6'), null));
  test('invalid chars → null', () => eq(parseHex('FD ZZ'), null));
  test('single byte', () => deepEq(parseHex('FF'), [0xFF]));
  test('leading/trailing spaces', () => deepEq(parseHex('  FD 60  '), [0xFD, 0x60]));
});

// ============================================================
// Arbitration (packets 32, 47)
// ============================================================
group('arbitration', () => {
  test('10 FF bytes (packet 32)', () => {
    const r = decodePacket('FF FF FF FF FF FF FF FF FF FF');
    eq(r.type, 'arbitration');
    eq(r.raw, 'FFFFFFFFFFFFFFFFFFFF');
  });
  test('9 FF bytes (packet 47)', () => {
    const r = decodePacket('FF FF FF FF FF FF FF FF FF');
    eq(r.type, 'arbitration');
    eq(r.raw, 'FFFFFFFFFFFFFFFFFF');
  });
  test('single FF', () => {
    eq(decodePacket('FF').type, 'arbitration');
  });
});

// ============================================================
// Scan Start — packet 31: FD 60 01 09 F0
// ============================================================
group('scan_start (packet 31)', () => {
  const RAW = 'FD600109F0';

  test('type and structure', () => {
    const r = decodePacket('FD 60 01 09 F0');
    eq(r.type, 'rtu_frame');
    eq(r.raw, RAW);
    eq(r.address, 'FD');
    eq(r.crc, '09F0');
    eq(r.payload.type, 'fast_modbus');
    eq(r.payload.ext_byte, '60');
    eq(r.payload.payload.type, 'scan_start');
    eq(r.payload.payload.raw, '01');
  });

  test('no spaces in input gives same result', () => {
    deepEq(decodePacket(RAW), decodePacket('FD 60 01 09 F0'));
  });

  test('with 0x46 ext_byte', () => {
    const r = decodePacket('FD 46 01 00 00');
    eq(r.payload.ext_byte, '46');
    eq(r.payload.payload.type, 'scan_start');
  });
});

// ============================================================
// Scan Continue — packet 46: FD 60 02 49 F1
// ============================================================
group('scan_continue (packet 46)', () => {
  test('structure', () => {
    const r = decodePacket('FD 60 02 49 F1');
    eq(r.type, 'rtu_frame');
    eq(r.address, 'FD');
    eq(r.crc, '49F1');
    eq(r.payload.payload.type, 'scan_continue');
    eq(r.payload.payload.raw, '02');
  });
});

// ============================================================
// Scan Response — packet 33: FD 60 03 00 06 24 66 83 C4 61
// ============================================================
group('scan_response (packet 33)', () => {
  test('full structure', () => {
    const r = decodePacket('FD 60 03 00 06 24 66 83 C4 61');
    eq(r.type, 'rtu_frame');
    eq(r.crc, 'C461');
    const p = r.payload.payload;
    eq(p.type, 'scan_response');
    eq(p.serial_number, '00062466');
    eq(p.modbus_address, '83');
  });

  test('raw of subcommand layer includes subcommand byte + data', () => {
    const r = decodePacket('FD 60 03 00 06 24 66 83 C4 61');
    // fm payload raw = ext_byte through (excluding) CRC
    eq(r.payload.raw, '60030006246683');
    // subcommand payload raw
    eq(r.payload.payload.raw, '030006246683');
  });

  test('truncated to 8 bytes → parse_error', () => {
    const r = decodePacket('FD 60 03 00 06 24 66 83');
    // 8 bytes: addr + ext + sub + serial(4) + addr = payload is 4 bytes minus CRC
    // bytes.slice(2, len-2) = bytes[2..5] = 4 bytes which is sub(1)+serial(3) < 6
    eq(r.payload.payload.type, 'parse_error');
    eq(r.payload.payload.reason, 'scan_response_too_short');
  });
});

// ============================================================
// Scan End — packet 48: FD 60 04 C9 F3
// ============================================================
group('scan_end (packet 48)', () => {
  test('structure', () => {
    const r = decodePacket('FD 60 04 C9 F3');
    eq(r.type, 'rtu_frame');
    eq(r.crc, 'C9F3');
    eq(r.payload.payload.type, 'scan_end');
    eq(r.payload.payload.raw, '04');
  });
});

// ============================================================
// Cmd Send (0x08) — packets 34,36,38,40,42,44
// ============================================================
group('command_by_serial (0x08)', () => {
  test('packet 34: read 20 regs from 200 (0x00C8)', () => {
    const r = decodePacket('FD 60 08 00 06 24 66 03 00 C8 00 14 9D 24');
    eq(r.type, 'rtu_frame');
    eq(r.crc, '9D24');
    const fm = r.payload.payload;
    eq(fm.type, 'command_by_serial');
    eq(fm.serial_number, '00062466');
    const pdu = fm.payload;
    eq(pdu.type, 'read_holding_registers');
    eq(pdu.fc, '03');
    eq(pdu.register, '00C8');
    eq(pdu.count, 20);
  });

  test('packet 36: read 12 regs from 290 (0x0122)', () => {
    const r = decodePacket('FD 60 08 00 06 24 66 03 01 22 00 0C BD 26');
    const pdu = r.payload.payload.payload;
    eq(pdu.register, '0122');
    eq(pdu.count, 12);
  });

  test('packet 38: read 1 reg from 110 (0x006E)', () => {
    const r = decodePacket('FD 60 08 00 06 24 66 03 00 6E 00 01 BC C8');
    const pdu = r.payload.payload.payload;
    eq(pdu.register, '006E');
    eq(pdu.count, 1);
  });

  test('packet 40: read 1 reg from 112 (0x0070)', () => {
    const r = decodePacket('FD 60 08 00 06 24 66 03 00 70 00 01 DC CE');
    const pdu = r.payload.payload.payload;
    eq(pdu.register, '0070');
    eq(pdu.count, 1);
  });

  test('packet 42: read 1 reg from 111 (0x006F)', () => {
    const r = decodePacket('FD 60 08 00 06 24 66 03 00 6F 00 01 ED 08');
    const pdu = r.payload.payload.payload;
    eq(pdu.register, '006F');
    eq(pdu.count, 1);
  });

  test('packet 44: read 16 regs from 250 (0x00FA)', () => {
    const r = decodePacket('FD 60 08 00 06 24 66 03 00 FA 00 10 3D 28');
    const pdu = r.payload.payload.payload;
    eq(pdu.register, '00FA');
    eq(pdu.count, 16);
  });

  test('write single register fc=6', () => {
    // FD 60 08 <SN:00010001> 06 <reg:0001> <val:0005> <CRC:0000>
    const r = decodePacket('FD 60 08 00 01 00 01 06 00 01 00 05 00 00');
    const pdu = r.payload.payload.payload;
    eq(pdu.type, 'write_single_register');
    eq(pdu.fc, '06');
    eq(pdu.register, '0001');
    eq(pdu.value, 5);
  });

  test('write multiple registers fc=16', () => {
    // FD 60 08 <SN:00010001> 10 <reg:0001> <cnt:0001> <bytecount:02> <data:0005> <CRC:0000>
    const r = decodePacket('FD 60 08 00 01 00 01 10 00 01 00 01 02 00 05 00 00');
    const pdu = r.payload.payload.payload;
    eq(pdu.type, 'write_multiple_registers');
    eq(pdu.fc, '10');
    eq(pdu.register, '0001');
    eq(pdu.count, 1);
    eq(pdu.byte_count, 2);
    eq(pdu.data, '0005');
  });

  test('truncated command_by_serial (6 bytes total) → parse_error in subcommand', () => {
    // 6 bytes: FD 60 08 00 06 24 → after CRC strip, fmBytes = bytes[2..3] = just "08 00" = 2 bytes < 6
    const r = decodePacket('FD 60 08 00 06 24');
    eq(r.payload.payload.type, 'parse_error');
    eq(r.payload.payload.reason, 'command_by_serial_too_short');
  });

  test('PDU too short (only fc, no register bytes)', () => {
    // FD 60 08 <SN:4 bytes> <fc=03 only> <CRC:2 bytes> — pdu is just [03]
    const r = decodePacket('FD 60 08 00 06 24 66 03 00 00');
    const pdu = r.payload.payload.payload;
    eq(pdu.type, 'parse_error');
    eq(pdu.reason, 'pdu_too_short');
  });

  test('unknown fc in PDU (fc=0x00)', () => {
    // fc=0x00 is invalid
    const r = decodePacket('FD 60 08 00 06 24 66 00 00 01 00 01 00 00');
    const pdu = r.payload.payload.payload;
    eq(pdu.type, 'parse_error');
    eq(pdu.reason, 'invalid_fc');
    eq(pdu.fc, '00');
  });
});

// ============================================================
// Cmd Response (0x09) — packets 35,37,39,41,43,45
// ============================================================
group('response_by_serial (0x09)', () => {
  test('packet 35: WB-MSW4 name (40 bytes = 20 regs)', () => {
    const r = decodePacket('FD 60 09 00 06 24 66 03 28 00 57 00 42 00 4D 00 53 00 57 00 34 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 A9 78');
    eq(r.type, 'rtu_frame');
    eq(r.crc, 'A978');
    const fm = r.payload.payload;
    eq(fm.type, 'response_by_serial');
    eq(fm.serial_number, '00062466');
    const pdu = fm.payload;
    eq(pdu.type, 'read_holding_registers_response');
    eq(pdu.fc, '03');
    eq(pdu.byte_count, 40);
    // data starts with 00 57 00 42 = 'W' 'B' in UTF-16BE
    assert(pdu.data.startsWith('00570042004D00530057003400'), 'data should start with WBMSW4 codepoints');
  });

  test('packet 37: msw5Ge string (12 regs = 24 bytes)', () => {
    const r = decodePacket('FD 60 09 00 06 24 66 03 18 00 6D 00 73 00 77 00 35 00 47 00 65 00 00 00 00 00 00 00 00 00 00 00 00 B2 04');
    const pdu = r.payload.payload.payload;
    eq(pdu.byte_count, 24);
    assert(pdu.data.startsWith('006D00730077003500470065'), 'data should start with msw5Ge codepoints');
  });

  test('packet 39: 1 reg = value 0x0060 = 96', () => {
    const r = decodePacket('FD 60 09 00 06 24 66 03 02 00 60 9E E5');
    const pdu = r.payload.payload.payload;
    eq(pdu.byte_count, 2);
    eq(pdu.data, '0060');
  });

  test('packet 41: 1 reg = value 0x0002 = 2', () => {
    const r = decodePacket('FD 60 09 00 06 24 66 03 02 00 02 1F 0C');
    const pdu = r.payload.payload.payload;
    eq(pdu.data, '0002');
  });

  test('packet 43: 1 reg = value 0x0000', () => {
    const r = decodePacket('FD 60 09 00 06 24 66 03 02 00 00 9E CD');
    const pdu = r.payload.payload.payload;
    eq(pdu.data, '0000');
  });

  test('packet 45: firmware 4.35.5 (32 bytes = 16 regs)', () => {
    const r = decodePacket('FD 60 09 00 06 24 66 03 20 00 34 00 2E 00 33 00 35 00 2E 00 35 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 66 19');
    const pdu = r.payload.payload.payload;
    eq(pdu.byte_count, 32);
    // 4.35.5 in UTF-16BE: 0034 002E 0033 0035 002E 0035
    assert(pdu.data.startsWith('0034002E00330035002E0035'), 'data should start with 4.35.5 codepoints');
  });

  test('Modbus error response (fc=0x83, code=2)', () => {
    // FD 60 09 <SN:00062466> 83 02 <CRC:0000>
    const r = decodePacket('FD 60 09 00 06 24 66 83 02 00 00');
    const pdu = r.payload.payload.payload;
    eq(pdu.type, 'modbus_error');
    eq(pdu.fc, '83');
    eq(pdu.original_fc, '03');
    eq(pdu.error_code, 2);
  });

  test('Modbus error fc=0x90 (for fc=16), code=1', () => {
    const r = decodePacket('FD 60 09 00 06 24 66 90 01 00 00');
    const pdu = r.payload.payload.payload;
    eq(pdu.type, 'modbus_error');
    eq(pdu.original_fc, '10');
    eq(pdu.error_code, 1);
  });

  test('truncated response_by_serial → parse_error', () => {
    const r = decodePacket('FD 60 09 00 06 24');
    eq(r.payload.payload.type, 'parse_error');
    eq(r.payload.payload.reason, 'response_by_serial_too_short');
  });

  test('PDU data truncated (byte_count claims more than available)', () => {
    // byte_count=10 but only 2 data bytes follow before CRC
    const r = decodePacket('FD 60 09 00 06 24 66 03 0A 00 01 00 00');
    const pdu = r.payload.payload.payload;
    eq(pdu.type, 'parse_error');
    eq(pdu.reason, 'pdu_data_truncated');
  });
});

// ============================================================
// Parse error cases
// ============================================================
group('parse_error cases', () => {
  test('empty string', () => {
    eq(decodePacket('').type, 'parse_error');
  });
  test('invalid hex chars', () => {
    const r = decodePacket('FD ZZ 01');
    eq(r.type, 'parse_error');
    eq(r.reason, 'invalid_hex');
  });
  test('too short (3 bytes < 4 min)', () => {
    const r = decodePacket('FD 60 01');
    eq(r.type, 'parse_error');
    eq(r.reason, 'too_short');
  });
  test('non-0xFD address parses as standard RTU, not parse_error', () => {
    // 01 60 01 09 F0 — slave 1, fc=0x60 (vendor specific range), not a Fast Modbus packet
    const r = decodePacket('01 60 01 09 F0');
    eq(r.type, 'rtu_frame');
    eq(r.address, '01');
    eq(r.payload.type, 'modbus_rtu');
  });
  test('FD with non-ext ext_byte parses as standard RTU', () => {
    // FD 03 ... — address=0xFD but fc=0x03 (Read Holding Registers), not Fast Modbus
    const r = decodePacket('FD 03 00 01 00 01 94 F8', 'request');
    eq(r.type, 'rtu_frame');
    eq(r.payload.type, 'modbus_rtu');
    eq(r.payload.payload.type, 'read_holding_registers');
  });
  test('unknown subcommand 0x07 in Fast Modbus', () => {
    const r = decodePacket('FD 60 07 00 00');
    eq(r.payload.payload.type, 'parse_error');
    eq(r.payload.payload.reason, 'unknown_subcommand');
    eq(r.payload.payload.subcommand, '07');
  });
  test('unknown subcommand 0xFF in Fast Modbus', () => {
    const r = decodePacket('FD 46 FF 00 00');
    eq(r.payload.payload.type, 'parse_error');
    eq(r.payload.payload.reason, 'unknown_subcommand');
  });
  test('mixed FF and non-FF is not arbitration', () => {
    const r = decodePacket('FF FF FD 60 01');
    assert(r.type !== 'arbitration');
  });
  test('null input', () => {
    const r = decodePacket(null);
    eq(r.type, 'parse_error');
  });
  test('array input works', () => {
    const r = decodePacket([0xFD, 0x60, 0x01, 0x09, 0xF0]);
    eq(r.type, 'rtu_frame');
    eq(r.payload.payload.type, 'scan_start');
  });
});

// ============================================================
// raw field integrity — every node must have raw with correct hex
// ============================================================
group('raw field integrity', () => {
  test('arbitration raw = all bytes', () => {
    const r = decodePacket('FF FF FF FF');
    eq(r.raw, 'FFFFFFFF');
  });

  test('rtu_frame raw = full packet', () => {
    const r = decodePacket('FD 60 01 09 F0');
    eq(r.raw, 'FD600109F0');
  });

  test('fast_modbus raw excludes address byte', () => {
    const r = decodePacket('FD 60 01 09 F0');
    // payload raw = from ext_byte(60) to before CRC
    eq(r.payload.raw, '6001');
  });

  test('scan_start payload raw = just subcommand byte', () => {
    const r = decodePacket('FD 60 01 09 F0');
    eq(r.payload.payload.raw, '01');
  });

  test('scan_response payload raw includes all subcommand data', () => {
    const r = decodePacket('FD 60 03 00 06 24 66 83 C4 61');
    // sub(03) + serial(00062466) + addr(83)
    eq(r.payload.payload.raw, '030006246683');
  });

  test('command_by_serial raw includes sub + serial + pdu', () => {
    const r = decodePacket('FD 60 08 00 06 24 66 03 00 C8 00 14 9D 24');
    // from sub(08) through end-of-pdu (before CRC)
    eq(r.payload.payload.raw, '0800062466030 0C80014'.replace(/\s/g, ''));
    // Also verify pdu raw
    const pduRaw = r.payload.payload.payload.raw;
    eq(pduRaw, '0300C80014');
  });

  test('parse_error includes raw bytes', () => {
    const r = decodePacket('FD 60 07 00 00');
    assert(r.payload.payload.raw.length > 0, 'parse_error should have non-empty raw');
  });
});

// ============================================================
// Full trace end-to-end (packets 31-48)
// ============================================================
group('full trace packets 31-48', () => {
  const trace = [
    { hex: 'FD 60 01 09 F0',       outerType: 'rtu_frame', innerType: 'scan_start' },
    { hex: 'FF FF FF FF FF FF FF FF FF FF', outerType: 'arbitration', innerType: null },
    { hex: 'FD 60 03 00 06 24 66 83 C4 61', outerType: 'rtu_frame', innerType: 'scan_response' },
    { hex: 'FD 60 08 00 06 24 66 03 00 C8 00 14 9D 24', outerType: 'rtu_frame', innerType: 'command_by_serial' },
    { hex: 'FD 60 09 00 06 24 66 03 28 00 57 00 42 00 4D 00 53 00 57 00 34 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 A9 78', outerType: 'rtu_frame', innerType: 'response_by_serial' },
    { hex: 'FD 60 08 00 06 24 66 03 01 22 00 0C BD 26', outerType: 'rtu_frame', innerType: 'command_by_serial' },
    { hex: 'FD 60 09 00 06 24 66 03 18 00 6D 00 73 00 77 00 35 00 47 00 65 00 00 00 00 00 00 00 00 00 00 00 00 B2 04', outerType: 'rtu_frame', innerType: 'response_by_serial' },
    { hex: 'FD 60 08 00 06 24 66 03 00 6E 00 01 BC C8', outerType: 'rtu_frame', innerType: 'command_by_serial' },
    { hex: 'FD 60 09 00 06 24 66 03 02 00 60 9E E5',   outerType: 'rtu_frame', innerType: 'response_by_serial' },
    { hex: 'FD 60 08 00 06 24 66 03 00 70 00 01 DC CE', outerType: 'rtu_frame', innerType: 'command_by_serial' },
    { hex: 'FD 60 09 00 06 24 66 03 02 00 02 1F 0C',   outerType: 'rtu_frame', innerType: 'response_by_serial' },
    { hex: 'FD 60 08 00 06 24 66 03 00 6F 00 01 ED 08', outerType: 'rtu_frame', innerType: 'command_by_serial' },
    { hex: 'FD 60 09 00 06 24 66 03 02 00 00 9E CD',   outerType: 'rtu_frame', innerType: 'response_by_serial' },
    { hex: 'FD 60 08 00 06 24 66 03 00 FA 00 10 3D 28', outerType: 'rtu_frame', innerType: 'command_by_serial' },
    { hex: 'FD 60 09 00 06 24 66 03 20 00 34 00 2E 00 33 00 35 00 2E 00 35 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 66 19', outerType: 'rtu_frame', innerType: 'response_by_serial' },
    { hex: 'FD 60 02 49 F1', outerType: 'rtu_frame', innerType: 'scan_continue' },
    { hex: 'FF FF FF FF FF FF FF FF FF', outerType: 'arbitration', innerType: null },
    { hex: 'FD 60 04 C9 F3', outerType: 'rtu_frame', innerType: 'scan_end' },
  ];

  trace.forEach(({ hex, outerType, innerType }, i) => {
    test(`packet ${31 + i}: ${outerType}${innerType ? '/' + innerType : ''}`, () => {
      const r = decodePacket(hex);
      eq(r.type, outerType);
      if (innerType) {
        eq(r.payload.payload.type, innerType, `inner type for packet ${31 + i}`);
      }
      // raw must equal cleaned input hex
      const expectedRaw = hex.replace(/\s+/g, '').toUpperCase();
      eq(r.raw, expectedRaw, `raw bytes for packet ${31 + i}`);
    });
  });
});

// ============================================================
// Standard Modbus RTU tests (from modbus_codes.md)
// ============================================================

// Helper: get standard modbus payload
function pdu(r) { return r.payload.payload; }

group('Standard RTU — FC 01 Read Coils', () => {
  test('request: slave 0x11, 19 coils from 0x0013', () => {
    const r = decodePacket('11 01 00 13 00 13 8E 92', 'request');
    eq(r.type, 'rtu_frame');
    eq(r.address, '11');
    eq(r.crc, '8E92');
    eq(r.payload.type, 'modbus_rtu');
    const p = pdu(r);
    eq(p.type, 'read_coils');
    eq(p.fc, '01');
    eq(p.register, '0013');
    eq(p.count, 19);
  });

  test('response: slave 0x11, ByteCount=3, data=CD6B05', () => {
    const r = decodePacket('11 01 03 CD 6B 05 40 12', 'response');
    eq(r.address, '11');
    const p = pdu(r);
    eq(p.type, 'read_coils_response');
    eq(p.byte_count, 3);
    eq(p.data, 'CD6B05');
  });
});

group('Standard RTU — FC 02 Read Discrete Inputs', () => {
  test('request: slave 0x11, 22 inputs from 0x00C4', () => {
    const r = decodePacket('11 02 00 C4 00 16 BA A9', 'request');
    const p = pdu(r);
    eq(p.type, 'read_discrete_inputs');
    eq(p.register, '00C4');
    eq(p.count, 22);
  });

  test('response: ByteCount=3', () => {
    const r = decodePacket('11 02 03 AC DB 35 20 18', 'response');
    const p = pdu(r);
    eq(p.type, 'read_discrete_inputs_response');
    eq(p.byte_count, 3);
    eq(p.data, 'ACDB35');
  });
});

group('Standard RTU — FC 03 Read Holding Registers', () => {
  test('request: slave 1, 2 regs from 0x0000', () => {
    const r = decodePacket('01 03 00 00 00 02 C4 0B', 'request');
    eq(r.address, '01');
    const p = pdu(r);
    eq(p.type, 'read_holding_registers');
    eq(p.fc, '03');
    eq(p.register, '0000');
    eq(p.count, 2);
  });

  test('response: ByteCount=4, regs 500,100', () => {
    const r = decodePacket('01 03 04 01 F4 00 64 BB D6', 'response');
    const p = pdu(r);
    eq(p.type, 'read_holding_registers_response');
    eq(p.byte_count, 4);
    eq(p.data, '01F40064');
  });
});

group('Standard RTU — FC 04 Read Input Registers', () => {
  test('request: slave 3, 1 reg from 0x0005', () => {
    const r = decodePacket('03 04 00 05 00 01 20 29', 'request');
    const p = pdu(r);
    eq(p.type, 'read_input_registers');
    eq(p.register, '0005');
    eq(p.count, 1);
  });

  test('response: value 0x01A4=420', () => {
    const r = decodePacket('03 04 02 01 A4 C0 DB', 'response');
    const p = pdu(r);
    eq(p.type, 'read_input_registers_response');
    eq(p.data, '01A4');
  });
});

group('Standard RTU — FC 05 Write Single Coil', () => {
  test('ON: coil 0x00AC = 0xFF00', () => {
    const r = decodePacket('01 05 00 AC FF 00 4C 1B', 'request');
    const p = pdu(r);
    eq(p.type, 'write_single_coil');
    eq(p.register, '00AC');
    eq(p.value, 0xFF00);
  });

  test('OFF: coil 0x00AC = 0x0000', () => {
    const r = decodePacket('01 05 00 AC 00 00 0D EB', 'request');
    const p = pdu(r);
    eq(p.type, 'write_single_coil');
    eq(p.value, 0x0000);
  });
});

group('Standard RTU — FC 06 Write Single Register', () => {
  test('write 0x2580 to reg 0x0003', () => {
    const r = decodePacket('01 06 00 03 25 80 62 FA', 'request');
    const p = pdu(r);
    eq(p.type, 'write_single_register');
    eq(p.register, '0003');
    eq(p.value, 0x2580);
  });
});

group('Standard RTU — FC 07 Read Exception Status', () => {
  test('request: slave 1, PDU=1 byte', () => {
    const r = decodePacket('01 07 41 E2', 'request');
    const p = pdu(r);
    eq(p.type, 'read_exception_status');
    eq(p.fc, '07');
  });

  test('response: output_data=0x6D', () => {
    const r = decodePacket('01 07 6D E3 DD', 'response');
    const p = pdu(r);
    eq(p.type, 'read_exception_status_response');
    eq(p.output_data, '6D');
  });
});

group('Standard RTU — FC 08 Diagnostics', () => {
  test('request sub=0x0000 Return Query Data', () => {
    const r = decodePacket('01 08 00 00 A5 37 DA 8D', 'request');
    const p = pdu(r);
    eq(p.type, 'diagnostics');
    eq(p.sub_function, '0000');
    eq(p.data, 'A537');
  });

  test('request sub=0x0001 Restart Communications, data=0x0000', () => {
    const r = decodePacket('01 08 00 01 00 00 B1 CB', 'request');
    const p = pdu(r);
    eq(p.sub_function, '0001');
    eq(p.data, '0000');
  });

  test('request sub=0x0004 Force Listen Only Mode', () => {
    const r = decodePacket('01 08 00 04 00 00 A1 CA', 'request');
    const p = pdu(r);
    eq(p.sub_function, '0004');
  });

  test('request sub=0x000A Clear Counters', () => {
    const r = decodePacket('01 08 00 0A 00 00 C0 09', 'request');
    const p = pdu(r);
    eq(p.sub_function, '000A');
  });

  test('response sub=0x000B, value=0x0088', () => {
    const r = decodePacket('01 08 00 0B 00 88 91 AF', 'response');
    const p = pdu(r);
    eq(p.sub_function, '000B');
    eq(p.data, '0088');
  });
});

group('Standard RTU — FC 0B Get Comm Event Counter', () => {
  test('request: slave 1, PDU=1 byte', () => {
    const r = decodePacket('01 0B 41 E7', 'request');
    const p = pdu(r);
    eq(p.type, 'get_comm_event_counter');
    eq(p.fc, '0B');
  });

  test('response: status=0x0000, count=264', () => {
    const r = decodePacket('01 0B 00 00 01 08 A4 5D', 'response');
    const p = pdu(r);
    eq(p.type, 'get_comm_event_counter_response');
    eq(p.status, '0000');
    eq(p.event_count, 0x0108);
  });
});

group('Standard RTU — FC 0C Get Comm Event Log', () => {
  test('request: slave 1, PDU=1 byte', () => {
    const r = decodePacket('01 0C 00 25', 'request');
    const p = pdu(r);
    eq(p.type, 'get_comm_event_log');
  });

  test('response: ByteCount=8, status/count/message/events', () => {
    const r = decodePacket('01 0C 08 00 00 01 08 01 21 20 00 0D C1', 'response');
    const p = pdu(r);
    eq(p.type, 'get_comm_event_log_response');
    eq(p.byte_count, 8);
    eq(p.status, '0000');
    eq(p.event_count, 0x0108);
    eq(p.message_count, 0x0121);
    eq(p.events, '2000');
  });
});

group('Standard RTU — FC 0F Write Multiple Coils', () => {
  test('request: 10 coils from 0x0013', () => {
    const r = decodePacket('11 0F 00 13 00 0A 02 CD 01 BF 0B', 'request');
    const p = pdu(r);
    eq(p.type, 'write_multiple_coils');
    eq(p.register, '0013');
    eq(p.count, 10);
    eq(p.byte_count, 2);
    eq(p.data, 'CD01');
  });

  test('response: addr+count echo', () => {
    const r = decodePacket('11 0F 00 13 00 0A 26 99', 'response');
    const p = pdu(r);
    eq(p.type, 'write_multiple_coils_response');
    eq(p.register, '0013');
    eq(p.count, 10);
  });
});

group('Standard RTU — FC 10 Write Multiple Registers', () => {
  test('request: 2 regs from 0x000A', () => {
    const r = decodePacket('01 10 00 0A 00 02 04 42 C8 00 00 E6 56', 'request');
    const p = pdu(r);
    eq(p.type, 'write_multiple_registers');
    eq(p.register, '000A');
    eq(p.count, 2);
    eq(p.byte_count, 4);
    eq(p.data, '42C80000');
  });

  test('response: addr+count echo', () => {
    const r = decodePacket('01 10 00 0A 00 02 61 CA', 'response');
    const p = pdu(r);
    eq(p.type, 'write_multiple_registers_response');
    eq(p.register, '000A');
    eq(p.count, 2);
  });
});

group('Standard RTU — FC 11 Report Server ID', () => {
  test('request: slave 1', () => {
    const r = decodePacket('01 11 C0 2C', 'request');
    const p = pdu(r);
    eq(p.type, 'report_server_id');
    eq(p.fc, '11');
  });

  test('response: ByteCount=4, server_id=0x05, run=ON', () => {
    const r = decodePacket('01 11 04 05 FF 41 42 79 DC', 'response');
    const p = pdu(r);
    eq(p.type, 'report_server_id_response');
    eq(p.byte_count, 4);
    eq(p.server_id, '05');
    eq(p.run_indicator, 'FF');
    eq(p.additional_data, '4142');
  });
});

group('Standard RTU — FC 14 Read File Record', () => {
  test('request: 1 sub-req, file 4, rec 1, len 2', () => {
    const r = decodePacket('01 14 07 06 00 04 00 01 00 02 D8 E5', 'request');
    const p = pdu(r);
    eq(p.type, 'read_file_record');
    eq(p.byte_count, 7);
    assert(Array.isArray(p.sub_requests));
    eq(p.sub_requests.length, 1);
    eq(p.sub_requests[0].reference_type, '06');
    eq(p.sub_requests[0].file_number, 4);
    eq(p.sub_requests[0].record_number, 1);
    eq(p.sub_requests[0].record_length, 2);
  });

  test('response: 2 regs worth of data', () => {
    const r = decodePacket('01 14 06 05 06 0D FE 00 20 8B 4E', 'response');
    const p = pdu(r);
    eq(p.type, 'read_file_record_response');
    eq(p.resp_data_length, 6);
    assert(Array.isArray(p.sub_responses));
    eq(p.sub_responses[0].data, '0DFE0020');
  });
});

group('Standard RTU — FC 15 Write File Record', () => {
  test('request: file 4, rec 1, 2 regs', () => {
    const r = decodePacket('01 15 0B 06 00 04 00 01 00 02 0A AB CD EF 5E E2', 'request');
    const p = pdu(r);
    eq(p.type, 'write_file_record');
    eq(p.request_data_length, 11);
    assert(Array.isArray(p.sub_requests));
    eq(p.sub_requests[0].record_length, 2);
    eq(p.sub_requests[0].data, '0AABCDEF');
  });
});

group('Standard RTU — FC 16 Mask Write Register', () => {
  test('request: reg 0x0002, AND=0x00F2, OR=0x0025', () => {
    const r = decodePacket('01 16 00 02 00 F2 00 25 EF EE', 'request');
    const p = pdu(r);
    eq(p.type, 'mask_write_register');
    eq(p.register, '0002');
    eq(p.and_mask, '00F2');
    eq(p.or_mask, '0025');
  });
});

group('Standard RTU — FC 17 Read/Write Multiple Registers', () => {
  test('request: read 6 from 0x0003, write 3 to 0x000E', () => {
    const r = decodePacket('01 17 00 03 00 06 00 0E 00 03 06 00 FF 00 FF 00 FF 46 91', 'request');
    const p = pdu(r);
    eq(p.type, 'read_write_multiple_registers');
    eq(p.read_register, '0003');
    eq(p.read_count, 6);
    eq(p.write_register, '000E');
    eq(p.write_count, 3);
    eq(p.write_byte_count, 6);
    eq(p.write_data, '00FF00FF00FF');
  });

  test('response: 6 regs read', () => {
    const r = decodePacket('01 17 0C 00 FE 0A CD 00 01 00 03 00 0D 00 FF 1D 79', 'response');
    const p = pdu(r);
    eq(p.type, 'read_write_multiple_registers_response');
    eq(p.byte_count, 12);
    eq(p.data, '00FE0ACD0001000300 0D00FF'.replace(/\s/g, ''));
  });
});

group('Standard RTU — FC 18 Read FIFO Queue', () => {
  test('request: FIFO pointer 0x04DE', () => {
    const r = decodePacket('01 18 04 DE 03 47', 'request');
    const p = pdu(r);
    eq(p.type, 'read_fifo_queue');
    eq(p.fifo_pointer, '04DE');
  });

  test('response: 2-byte ByteCount=6, FIFOCount=2, values', () => {
    const r = decodePacket('01 18 00 06 00 02 01 B8 12 84 19 18', 'response');
    const p = pdu(r);
    eq(p.type, 'read_fifo_queue_response');
    eq(p.byte_count, 6);  // 2-byte field BE
    eq(p.fifo_count, 2);
    eq(p.data, '01B81284');
  });
});

group('Standard RTU — FC 2B MEI', () => {
  test('Read Device ID request: slave 1, Basic, VendorName', () => {
    const r = decodePacket('01 2B 0E 01 00 70 77', 'request');
    const p = pdu(r);
    eq(p.type, 'mei_read_device_identification');
    eq(p.fc, '2B');
    eq(p.mei_type, '0E');
    eq(p.read_device_id_code, '01');
    eq(p.object_id, '00');
  });

  test('Read Device ID response: 3 objects', () => {
    const r = decodePacket('01 2B 0E 01 01 00 00 03 00 06 43 6F 6D 70 61 6E 01 04 50 72 6F 64 02 03 31 2E 30 14 9C', 'response');
    const p = pdu(r);
    eq(p.type, 'mei_read_device_identification_response');
    eq(p.conformity_level, '01');
    eq(p.more_follows, '00');
    eq(p.number_of_objects, 3);
    assert(Array.isArray(p.objects));
    eq(p.objects[0].id, '00');
    eq(p.objects[0].value, '436F6D70616E');  // "Compan"
    eq(p.objects[1].id, '01');
    eq(p.objects[1].value, '50726F64');      // "Prod"
    eq(p.objects[2].id, '02');
    eq(p.objects[2].value, '312E30');        // "1.0"
  });

  test('CANopen MEI 0x0D: opaque payload', () => {
    const r = decodePacket('01 2B 0D 01 00 02 00 03 C7 D9', 'response');
    const p = pdu(r);
    eq(p.type, 'mei_canopen');
    eq(p.mei_type, '0D');
    assert(p.data !== undefined);
  });
});

group('Standard RTU — Exception responses', () => {
  test('FC 83 (FC03 error), code=0x02 Illegal Data Address', () => {
    const r = decodePacket('01 83 02 C0 F1', 'response');
    eq(r.type, 'rtu_frame');
    const p = pdu(r);
    eq(p.type, 'modbus_error');
    eq(p.fc, '83');
    eq(p.original_fc, '03');
    eq(p.error_code, 2);
  });

  test('FC 90 (FC16 error), code=0x03 Illegal Data Value', () => {
    const r = decodePacket('01 90 03 0C 01', 'response');
    const p = pdu(r);
    eq(p.original_fc, '10');
    eq(p.error_code, 3);
  });

  test('FC 81 (FC01 error), code=0x01 Illegal Function', () => {
    const r = decodePacket('01 81 01 81 90', 'response');
    const p = pdu(r);
    eq(p.original_fc, '01');
    eq(p.error_code, 1);
  });

  test('FC 86 (FC06 error), code=0x04 Server Device Failure', () => {
    const r = decodePacket('01 86 04 43 A3', 'response');
    const p = pdu(r);
    eq(p.original_fc, '06');
    eq(p.error_code, 4);
  });

  test('code=0x05 Acknowledge', () => {
    const r = decodePacket('01 83 05 81 33', 'response');
    const p = pdu(r);
    eq(p.error_code, 5);
  });

  test('code=0x06 Server Device Busy', () => {
    const r = decodePacket('01 90 06 CC 02', 'response');
    const p = pdu(r);
    eq(p.error_code, 6);
  });

  test('code=0x0A Gateway Path Unavailable', () => {
    const r = decodePacket('01 83 0A C1 37', 'response');
    const p = pdu(r);
    eq(p.error_code, 0x0A);
  });

  test('code=0x0B Gateway Target Device Failed', () => {
    const r = decodePacket('01 83 0B 00 F7', 'response');
    const p = pdu(r);
    eq(p.error_code, 0x0B);
  });
});

group('Standard RTU — Broadcast (slave=0)', () => {
  test('FC 06 broadcast: write 0x1234 to reg 0x0003', () => {
    const r = decodePacket('00 06 00 03 12 34 75 6C', 'request');
    eq(r.type, 'rtu_frame');
    eq(r.address, '00');
    const p = pdu(r);
    eq(p.type, 'write_single_register');
    eq(p.register, '0003');
    eq(p.value, 0x1234);
  });

  test('FC 10 broadcast: write 1 reg to 0x0000', () => {
    const r = decodePacket('00 10 00 00 00 01 02 12 34 A6 B7', 'request');
    eq(r.address, '00');
    const p = pdu(r);
    eq(p.type, 'write_multiple_registers');
  });
});

group('Standard RTU — Vendor-specific and user-defined', () => {
  test('FC 0x5A (UMAS/Schneider): opaque payload', () => {
    const r = decodePacket('01 5A 00 10 00 00 00 03 FA', 'request');
    eq(r.type, 'rtu_frame');
    const p = pdu(r);
    eq(p.type, 'vendor_specific');
    eq(p.fc, '5A');
    assert(p.data !== undefined);
  });

  test('FC 0x64 (user-defined 100): opaque payload', () => {
    const r = decodePacket('01 64 AA BB CC DD B4 A6', 'request');
    const p = pdu(r);
    eq(p.type, 'user_defined');
    eq(p.fc, '64');
    assert(p.data !== undefined);
  });

  test('FC 0x41 (user-defined 65): opaque payload', () => {
    const r = decodePacket('01 41 00 0A 00 02 9C 06', 'request');
    const p = pdu(r);
    eq(p.type, 'user_defined');
    eq(p.fc, '41');
  });
});

group('Standard RTU — reserved slave address', () => {
  test('slave 0xF8 (248) is reserved but parseable', () => {
    const r = decodePacket('F8 03 00 00 00 01 90 63', 'request');
    eq(r.type, 'rtu_frame');
    eq(r.address, 'F8');
    assert(r.reserved_address === true);
  });
});

group('Standard RTU — invalid FC=0', () => {
  test('FC=0x00 returns parse_error', () => {
    const r = decodePacket('01 00 00 00 01 D8', 'request');
    eq(r.type, 'rtu_frame');
    const p = pdu(r);
    eq(p.type, 'parse_error');
    eq(p.reason, 'invalid_fc');
  });
});

group('Standard RTU — ADU too short', () => {
  test('3 bytes → parse_error', () => {
    const r = decodePacket('01 03 04');
    eq(r.type, 'parse_error');
    eq(r.reason, 'too_short');
  });
});

// ============================================================
// Summary
// ============================================================
console.log(`\n${'='.repeat(50)}`);
console.log(`Total: ${passed + failed}  |  Passed: ${passed}  |  Failed: ${failed}`);
if (failed > 0) {
  console.error(`\n${failed} test(s) FAILED`);
  process.exit(1);
} else {
  console.log('\nAll tests passed!');
}
