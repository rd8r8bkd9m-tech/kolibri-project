import ctypes
import os
import random
import time
from ctypes import c_uint64, Structure, c_int

lib_path = os.path.join(os.path.dirname(__file__), "../../build/libkolibri_core.dylib")
lib = ctypes.CDLL(lib_path)

class KolibriKey128(Structure):
    _fields_ = [("low", c_uint64), ("high", c_uint64)]

class KolibriHash128(Structure):
    _fields_ = [("low", c_uint64), ("high", c_uint64)]

lib.kolibri_hash_128.restype = KolibriHash128
lib.kolibri_hash_128.argtypes = [KolibriKey128]

def popcount(x):
    return bin(x).count('1')

def hamming_distance(a, b):
    return popcount(a ^ b)

def run_test(name, test_func):
    print(f"\n=== {name} ===")
    try:
        test_func()
        print("Status: PASSED")
    except AssertionError as e:
        print(f"Status: FAILED - {e}")
    except Exception as e:
        print(f"Status: ERROR - {e}")

def test_positive_recovery_32bit_window():
    """Positive test: Secret is inside a 32-bit search window."""
    secret_high = 0x1234567890ABCDEF
    # Генерируем low так, чтобы он попал в окно 0x50000000 - 0x5FFFFFFF
    secret_low = 0x5D80EB1B 
    
    secret_key = KolibriKey128(low=secret_low, high=secret_high)
    target_hash = lib.kolibri_hash_128(secret_key)
    
    window_start = 0x50000000
    window_end = 0x5FFFFFFF
    
    print(f"key_bits: 128")
    print(f"known_high64: true")
    print(f"unknown_bits: 32")
    print(f"search_window: 0x{window_start:016X} - 0x{window_end:016X}")
    print(f"secret: high=0x{secret_high:016X} low=0x{secret_low:016X}")
    print(f"secret_inside_window: true")
    print(f"expected: FOUND")
    
    found = False
    result_key = KolibriKey128()
    
    for i in range(window_start, window_end + 1):
        candidate = KolibriKey128(low=i, high=secret_high)
        h = lib.kolibri_hash_128(candidate)
        if h.low == target_hash.low and h.high == target_hash.high:
            result_key = candidate
            found = True
            break
            
    print(f"actual: {'FOUND' if found else 'NOT_FOUND'}")
    assert found, "Key should have been found in the window"
    assert result_key.low == secret_low and result_key.high == secret_high, "Exact key mismatch"
    print(f"verified_exact_key: true")

def test_negative_recovery_outside_window():
    """Negative test: Secret is outside the 32-bit search window."""
    secret_high = 0x1234567890ABCDEF
    secret_low = 0x375D80EB1B # За пределами 0xFFFFFFFF
    
    secret_key = KolibriKey128(low=secret_low, high=secret_high)
    target_hash = lib.kolibri_hash_128(secret_key)
    
    window_start = 0x0
    window_end = 0xFFFFFFFF
    
    print(f"key_bits: 128")
    print(f"known_high64: true")
    print(f"unknown_bits: 32")
    print(f"search_window: 0x{window_start:016X} - 0x{window_end:016X}")
    print(f"secret: high=0x{secret_high:016X} low=0x{secret_low:016X}")
    print(f"secret_inside_window: false")
    print(f"expected: NOT_FOUND")
    
    found = False
    for i in range(window_start, window_end + 1):
        candidate = KolibriKey128(low=i, high=secret_high)
        h = lib.kolibri_hash_128(candidate)
        if h.low == target_hash.low and h.high == target_hash.high:
            found = True
            break
            
    print(f"actual: {'FOUND' if found else 'NOT_FOUND'}")
    assert not found, "Key should NOT be found outside the window"
    print(f"verified: true")

def test_high_bit_sensitivity():
    """High bits must affect the hash."""
    key_a = KolibriKey128(low=123, high=0)
    key_b = KolibriKey128(low=123, high=1)
    
    h_a = lib.kolibri_hash_128(key_a)
    h_b = lib.kolibri_hash_128(key_b)
    
    ha_total = (h_a.high << 64) | h_a.low
    hb_total = (h_b.high << 64) | h_b.low
    
    dist = hamming_distance(ha_total, hb_total)
    print(f"Hamming distance (high bit change): {dist}/128")
    assert dist >= 40, f"Avalanche effect too weak for high bits: {dist}"

def test_low_bit_sensitivity():
    """Low bits must affect the hash."""
    key_a = KolibriKey128(low=123, high=0)
    key_b = KolibriKey128(low=124, high=0)
    
    h_a = lib.kolibri_hash_128(key_a)
    h_b = lib.kolibri_hash_128(key_b)
    
    ha_total = (h_a.high << 64) | h_a.low
    hb_total = (h_b.high << 64) | h_b.low
    
    dist = hamming_distance(ha_total, hb_total)
    print(f"Hamming distance (low bit change): {dist}/128")
    assert dist >= 40, f"Avalanche effect too weak for low bits: {dist}"

if __name__ == "__main__":
    print("=== KOLIBRI 128-BIT PARTIAL RECOVERY SUITE ===")
    run_test("128-bit partial recovery positive test (32-bit window)", test_positive_recovery_32bit_window)
    run_test("128-bit partial recovery negative test (outside window)", test_negative_recovery_outside_window)
    run_test("128-bit high-bit sensitivity test", test_high_bit_sensitivity)
    run_test("128-bit low-bit sensitivity test", test_low_bit_sensitivity)
