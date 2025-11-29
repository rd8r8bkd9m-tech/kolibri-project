#!/usr/bin/env python3
"""
Kolibri SDK Correctness Tests

Tests to verify that:
1. Pure Python implementation works correctly
2. C extension (if available) produces identical results
3. Round-trip encoding/decoding preserves data integrity

Run with: pytest test_correctness.py -v
"""

import sys
import os

# Ensure we import from the local package
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from kolibri import encode, decode, is_c_extension_available
from kolibri.pure_python import encode_pure, decode_pure


class TestPurePython:
    """Tests for pure Python implementation."""

    def test_encode_empty(self):
        """Empty bytes should encode to empty string."""
        assert encode_pure(b"") == ""

    def test_decode_empty(self):
        """Empty string should decode to empty bytes."""
        assert decode_pure("") == b""

    def test_encode_single_byte_zero(self):
        """Byte 0 should encode to '000'."""
        assert encode_pure(b"\x00") == "000"

    def test_encode_single_byte_max(self):
        """Byte 255 should encode to '255'."""
        assert encode_pure(b"\xff") == "255"

    def test_encode_hello(self):
        """Test encoding 'Hello' string."""
        data = b"Hello"
        expected = "072101108108111"  # H=72, e=101, l=108, l=108, o=111
        assert encode_pure(data) == expected

    def test_decode_hello(self):
        """Test decoding back to 'Hello'."""
        encoded = "072101108108111"
        assert decode_pure(encoded) == b"Hello"

    def test_all_bytes(self):
        """All 256 byte values should encode and decode correctly."""
        data = bytes(range(256))
        encoded = encode_pure(data)

        # Check length: each byte becomes 3 digits
        assert len(encoded) == 256 * 3

        # Check round-trip
        decoded = decode_pure(encoded)
        assert decoded == data

    def test_roundtrip_various(self):
        """Various test cases for round-trip correctness."""
        test_cases = [
            b"Hello, World!",
            b"\x00\xff",
            b"\x00\x00\x00",
            b"\xff\xff\xff",
            bytes(range(256)),
            bytes(range(256)) * 10,
            b"Test data 123",
            b"\n\r\t",
        ]
        for data in test_cases:
            encoded = encode_pure(data)
            decoded = decode_pure(encoded)
            assert decoded == data, f"Round-trip failed for {data!r}"

    def test_decode_invalid_length(self):
        """Decoding non-multiple-of-3 length should raise ValueError."""
        try:
            decode_pure("12")  # length 2, not multiple of 3
            assert False, "Should have raised ValueError"
        except ValueError:
            pass

        try:
            decode_pure("1234")  # length 4, not multiple of 3
            assert False, "Should have raised ValueError"
        except ValueError:
            pass


class TestMainAPI:
    """Tests for main API (uses C extension if available, else pure Python)."""

    def test_encode_decode_roundtrip(self):
        """Main API should correctly round-trip data."""
        test_cases = [
            b"",
            b"\x00",
            b"\xff",
            b"Hello, World!",
            bytes(range(256)),
        ]
        for data in test_cases:
            assert decode(encode(data)) == data

    def test_all_bytes(self):
        """All 256 byte values should work correctly."""
        data = bytes(range(256))
        assert decode(encode(data)) == data


class TestCExtension:
    """Tests specifically for C extension (skipped if not available)."""

    def test_c_extension_matches_pure_python(self):
        """C extension should produce identical results to pure Python."""
        if not is_c_extension_available():
            print("SKIP: C extension not available")
            return

        test_cases = [
            b"",
            b"Hello, World!",
            b"\x00\xff",
            bytes(range(256)),
            bytes(range(256)) * 100,
            b"Test data 123 with various bytes: \x00\x01\x02\xfe\xff",
        ]

        for data in test_cases:
            c_encoded = encode(data)
            pure_encoded = encode_pure(data)
            assert c_encoded == pure_encoded, \
                f"Encoding mismatch for {data!r}: C={c_encoded!r}, Pure={pure_encoded!r}"

            c_decoded = decode(c_encoded)
            pure_decoded = decode_pure(pure_encoded)
            assert c_decoded == pure_decoded, \
                f"Decoding mismatch for {c_encoded!r}"

    def test_c_extension_large_data(self):
        """C extension should handle large data correctly."""
        if not is_c_extension_available():
            print("SKIP: C extension not available")
            return

        # 1 MB of data
        data = bytes(range(256)) * (1024 * 1024 // 256)
        assert decode(encode(data)) == data


def run_tests():
    """Run all tests without pytest."""
    import traceback

    test_classes = [TestPurePython, TestMainAPI, TestCExtension]
    passed = 0
    failed = 0
    skipped = 0

    for test_class in test_classes:
        print(f"\n{test_class.__name__}:")
        instance = test_class()

        for method_name in dir(instance):
            if not method_name.startswith('test_'):
                continue

            method = getattr(instance, method_name)
            try:
                method()
                print(f"  ✓ {method_name}")
                passed += 1
            except Exception as e:
                if "SKIP" in str(e):
                    print(f"  - {method_name} (skipped)")
                    skipped += 1
                else:
                    print(f"  ✗ {method_name}")
                    traceback.print_exc()
                    failed += 1

    print(f"\n{'='*40}")
    print(f"Results: {passed} passed, {failed} failed, {skipped} skipped")
    print(f"{'='*40}")

    return failed == 0


if __name__ == "__main__":
    # Check if pytest is available
    try:
        import pytest
        sys.exit(pytest.main([__file__, "-v"]))
    except ImportError:
        # Fall back to simple test runner
        print("pytest not available, using simple test runner\n")
        success = run_tests()
        sys.exit(0 if success else 1)
