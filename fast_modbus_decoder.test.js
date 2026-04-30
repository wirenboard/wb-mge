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

  test('unknown fc in PDU', () => {
    // fc=0x07 is not a standard Modbus function
    const r = decodePacket('FD 60 08 00 06 24 66 07 00 01 00 01 00 00');
    const pdu = r.payload.payload.payload;
    eq(pdu.type, 'parse_error');
    eq(pdu.reason, 'unknown_fc');
    eq(pdu.fc, '07');
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
  test('too short (4 bytes < 5 min)', () => {
    const r = decodePacket('FD 60 01 09');
    eq(r.type, 'parse_error');
    eq(r.reason, 'too_short');
  });
  test('wrong first byte', () => {
    const r = decodePacket('01 60 01 09 F0');
    eq(r.type, 'parse_error');
    eq(r.reason, 'wrong_address');
    eq(r.address, '01');
  });
  test('wrong ext byte (not 0x60 or 0x46)', () => {
    const r = decodePacket('FD 03 01 09 F0');
    eq(r.type, 'parse_error');
    eq(r.reason, 'wrong_ext_byte');
    eq(r.ext_byte, '03');
  });
  test('unknown subcommand 0x07', () => {
    const r = decodePacket('FD 60 07 00 00');
    eq(r.payload.payload.type, 'parse_error');
    eq(r.payload.payload.reason, 'unknown_subcommand');
    eq(r.payload.payload.subcommand, '07');
  });
  test('unknown subcommand 0xFF', () => {
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
