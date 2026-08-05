"""Unit tests for io_bus_helpers.py IoBus._parse — run without QEMU.

These call the staticmethod directly with raw wire records (bytes); no socket
and no IoBus instance are involved, so they need no QEMU and no fixtures.
"""

import pytest

from io_bus_helpers import IoBus


# ---------------------------------------------------------------------------
# Tests: IoBus._parse() — one 5-byte wire record -> (pin, value) or None
# ---------------------------------------------------------------------------

class TestParseValid:
    """Records that must parse into an exact (pin, value) tuple."""

    @pytest.mark.parametrize(
        "data, expected",
        [
            (b"E07/1", ("E07", 1)),
            (b"G34/0", ("G34", 0)),
            (b"D04/1", ("D04", 1)),
            (b"V04/2", ("V04", 2)),
            # A single trailing newline (len 6) is tolerated and stripped.
            (b"E07/1\n", ("E07", 1)),
            # 'V' carries a single-digit cause, including multi-value 2.
            (b"V13/2", ("V13", 2)),
            # Level '2' is accepted only for 'V'.
            (b"V07/2", ("V07", 2)),
        ],
        ids=[
            "expander_E07_high",
            "native_G34_low",
            "direction_D04_output",
            "violation_V04_cause2",
            "trailing_newline_tolerated",
            "violation_multi_digit_cause",
            "violation_level2_accepted",
        ],
    )
    def test_valid_records(self, data, expected):
        """A well-formed record parses to the exact (pin, value) tuple."""
        assert IoBus._parse(data) == expected


class TestParseInvalid:
    """Malformed records that must yield None."""

    @pytest.mark.parametrize(
        "data",
        [
            # Level '2' is rejected for E/G/D (only 0/1 allowed there).
            b"E07/2",
            b"G34/2",
            b"D04/2",
            # Separator not at index 3.
            b"E0/71",
            b"E071/",
            # Pin number is not two digits.
            b"EZZ/1",
            # Unknown / wrong-case type byte.
            b"X07/1",
            b"e07/1",
            # 6 bytes where the extra byte is not a newline.
            b"E07/1X",
            # Length edge cases.
            b"",
            b"E07",
            b"E07/12\n",
        ],
        ids=[
            "level2_rejected_E",
            "level2_rejected_G",
            "level2_rejected_D",
            "sep_too_early",
            "sep_too_late",
            "non_numeric_pin",
            "unknown_type",
            "lowercase_type",
            "six_bytes_not_newline",
            "empty",
            "too_short",
            "too_long_seven_bytes",
        ],
    )
    def test_invalid_records(self, data):
        """A malformed record returns None."""
        assert IoBus._parse(data) is None
