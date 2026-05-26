"""Unit tests for rtu_slave_helpers.py — run without QEMU."""

import struct
import pytest

from rtu_slave_helpers import ModbusRtuSlaveThread, _crc16, _build_rtu_response


# ---------------------------------------------------------------------------
# Helper: build a raw Modbus RTU request frame
# ---------------------------------------------------------------------------

def _build_rtu_request(slave_id: int, fc: int, addr: int, count: int) -> bytes:
    """Build a complete Modbus RTU request (slave+FC+addr+count+CRC)."""
    body = bytes([slave_id, fc, addr >> 8, addr & 0xFF, count >> 8, count & 0xFF])
    crc = _crc16(body)
    return body + bytes([crc & 0xFF, crc >> 8])


# ---------------------------------------------------------------------------
# Tests: _build_response() — all supported FC codes
# ---------------------------------------------------------------------------

class TestBuildResponse:
    """Test ModbusRtuSlaveThread._build_response() for all FC types."""

    def setup_method(self):
        """Create a slave instance with fake_value=0x1234 for each test."""
        self.slave = ModbusRtuSlaveThread(fake_value=0x1234)

    def test_fc03_read_holding_registers(self):
        """FC03: response contains byte_count and correct register values."""
        resp = self.slave._build_response(slave_id=1, fc=0x03, addr=0, count=2)
        assert resp is not None
        # Format: slave(1) + FC(1) + byte_count(1) + data(count*2) + CRC(2)
        assert resp[0] == 1           # slave_id
        assert resp[1] == 0x03        # FC
        assert resp[2] == 4           # byte_count = count * 2
        # Two registers, each 0x1234
        assert resp[3:5] == b'\x12\x34'
        assert resp[5:7] == b'\x12\x34'
        assert len(resp) == 9         # 3 header + 4 data + 2 CRC

    def test_fc04_read_input_registers(self):
        """FC04: same structure as FC03."""
        resp = self.slave._build_response(slave_id=1, fc=0x04, addr=0, count=1)
        assert resp is not None
        assert resp[1] == 0x04
        assert resp[2] == 2           # byte_count = 1 * 2
        assert resp[3:5] == b'\x12\x34'

    def test_fc01_read_coils_single(self):
        """FC01 with count=1 and fake_value=0x1234 (non-zero): coil bit is 1."""
        resp = self.slave._build_response(slave_id=1, fc=0x01, addr=0, count=1)
        assert resp is not None
        assert resp[1] == 0x01
        assert resp[2] == 1           # byte_count = ceil(1/8) = 1
        assert resp[3] & 0x01 == 1    # LSB of first byte: coil 0 is ON

    def test_fc02_read_discrete_inputs(self):
        """FC02 with count=8: all 8 bits set (fake_value != 0)."""
        resp = self.slave._build_response(slave_id=1, fc=0x02, addr=0, count=8)
        assert resp is not None
        assert resp[1] == 0x02
        assert resp[2] == 1           # byte_count = ceil(8/8) = 1
        assert resp[3] == 0xFF        # all 8 coils ON

    def test_fc06_returns_exception_0x01(self):
        """FC06 is unsupported: must return exception code 0x01."""
        resp = self.slave._build_response(slave_id=1, fc=0x06, addr=0, count=0)
        assert resp is not None
        assert resp[1] == (0x06 | 0x80)   # exception FC
        assert resp[2] == 0x01            # exception code: Illegal Function

    def test_fc16_returns_exception_0x01(self):
        """FC16 is unsupported: must return exception code 0x01."""
        resp = self.slave._build_response(slave_id=1, fc=0x10, addr=0, count=0)
        assert resp is not None
        assert resp[1] == (0x10 | 0x80)
        assert resp[2] == 0x01

    def test_exception_fc_overrides_normal_response(self):
        """exception_fc dict: FC03 -> exception 0x02 overrides normal FC03 response."""
        slave = ModbusRtuSlaveThread(fake_value=0x1234, exception_fc={0x03: 0x02})
        resp = slave._build_response(slave_id=1, fc=0x03, addr=0, count=1)
        assert resp is not None
        assert resp[1] == (0x03 | 0x80)   # exception FC
        assert resp[2] == 0x02            # configured exception code

    def test_fc01_all_coils_off_when_fake_value_zero(self):
        """FC01 with fake_value=0: all coil bits must be 0."""
        slave = ModbusRtuSlaveThread(fake_value=0)
        resp = slave._build_response(slave_id=1, fc=0x01, addr=0, count=8)
        assert resp is not None
        assert resp[3] == 0x00            # all coils OFF


# ---------------------------------------------------------------------------
# Tests: _process_buffer() — frame parsing and re-sync
# ---------------------------------------------------------------------------

class TestProcessBuffer:
    """Test ModbusRtuSlaveThread._process_buffer() logic."""

    def setup_method(self):
        """Create a slave without a socket (we capture sent data separately)."""
        self.slave = ModbusRtuSlaveThread(fake_value=0x5678)
        self.slave._sock = None         # no real socket; responses won't be sent

    def test_single_complete_frame_consumed(self):
        """One complete request: buffer returned empty, request_count incremented."""
        req = _build_rtu_request(1, 0x03, 0, 1)
        remaining = self.slave._process_buffer(req)
        assert remaining == b''
        assert self.slave.request_count == 1

    def test_two_back_to_back_frames_both_consumed(self):
        """Two complete requests in one buffer: both processed, buffer empty."""
        req1 = _build_rtu_request(1, 0x03, 0, 1)
        req2 = _build_rtu_request(1, 0x03, 1, 2)
        remaining = self.slave._process_buffer(req1 + req2)
        assert remaining == b''
        assert self.slave.request_count == 2

    def test_split_frame_partial_left_in_buffer(self):
        """Partial frame (first 4 of 8 bytes): not consumed, returned as remainder."""
        req = _build_rtu_request(1, 0x03, 0, 1)
        partial = req[:4]
        remaining = self.slave._process_buffer(partial)
        assert remaining == partial       # returned unchanged
        assert self.slave.request_count == 0

    def test_crc_mismatch_resyncs_by_discarding_one_byte(self):
        """CRC mismatch: first byte discarded, re-sync attempted on next bytes."""
        req = _build_rtu_request(1, 0x03, 0, 1)
        # Corrupt CRC byte (index 6)
        bad = bytearray(req)
        bad[6] ^= 0xFF
        bad = bytes(bad)
        remaining = self.slave._process_buffer(bad)
        # After discarding one byte at a time, the remaining should be < 8
        # (the valid CRC check would fail for all sub-slices of bad data)
        assert self.slave.request_count == 0
        assert len(remaining) < 8

    def test_drop_count_skips_requests_silently(self):
        """drop_count=1: first request silently dropped, request_count still incremented."""
        slave = ModbusRtuSlaveThread(fake_value=0x1234, drop_count=1)
        slave._sock = None
        req = _build_rtu_request(1, 0x03, 0, 1)
        remaining = slave._process_buffer(req)
        assert remaining == b''
        assert slave.request_count == 1   # counted but not responded to
        assert slave.drop_count == 0      # decremented


# ---------------------------------------------------------------------------
# Tests: _process_buffer() — buf[8:] fixed-size advancement with write FC
# ---------------------------------------------------------------------------

class TestProcessBufferWriteFC:
    """Test that fixed buf = buf[8:] advancement is a known limitation for write FCs.

    FC06 (Write Single Register) has a PDU of 4 bytes: addr(2) + value(2), so
    total RTU frame = slave(1)+FC(1)+addr(2)+value(2)+CRC(2) = 8 bytes.
    FC16 (Write Multiple Registers) has variable length > 8 bytes.
    This test documents the current behaviour for FC06 (fixed 8 bytes = correct for FC06).
    """

    def test_fc06_request_is_8_bytes(self):
        """FC06 RTU request is exactly 8 bytes; buf[8:] advancement is correct for it."""
        # FC06: slave(1) + FC(1) + addr(2) + value(2) + CRC(2) = 8 bytes
        addr = 0x0010
        value = 0x00FF
        body = bytes([1, 0x06, addr >> 8, addr & 0xFF, value >> 8, value & 0xFF])
        crc = _crc16(body)
        req = body + bytes([crc & 0xFF, crc >> 8])
        assert len(req) == 8
        slave = ModbusRtuSlaveThread(fake_value=0)
        slave._sock = None
        remaining = slave._process_buffer(req)
        assert remaining == b''
        assert slave.request_count == 1
