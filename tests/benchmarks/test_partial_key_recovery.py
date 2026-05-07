import ctypes
import os
import random
import sys
from ctypes import c_uint64, c_uint32, c_double, c_bool, Structure, byref

lib_path = os.path.join(os.path.dirname(__file__), "../../build/libkolibri_core.dylib")
lib = ctypes.CDLL(lib_path)

class KolibriKey128(Structure):
    _fields_ = [("low", c_uint64), ("high", c_uint64)]

class KolibriHash128(Structure):
    _fields_ = [("low", c_uint64), ("high", c_uint64)]

class KolibriPartial128Result(Structure):
    _fields_ = [
        ("status", c_uint32),       # KolibriSearchStatus
        ("method", c_uint32),       # KolibriSearchMethod
        ("policy", c_uint32),       # KolibriSearchPolicy
        ("found", c_bool),
        ("verified", c_bool),
        ("timed_out", c_bool),
        ("known_high", c_uint64),
        ("recovered_low", c_uint64),
        ("recovered_key", KolibriKey128),
        ("candidate_hash", KolibriHash128),
        ("target_hash", KolibriHash128),
        ("low_start", c_uint64),
        ("low_end", c_uint64),
        ("attempts", c_uint64),
        ("space_size", c_uint64),
        ("time_ms", c_double),
        ("keys_per_second", c_double),
        ("threads", c_uint32),
    ]

# Setup function signatures
lib.kolibri_hash_128.restype = KolibriHash128
lib.kolibri_hash_128.argtypes = [KolibriKey128]

lib.kolibri_recover_low64_with_known_high.restype = KolibriPartial128Result
lib.kolibri_recover_low64_with_known_high.argtypes = [
    c_uint64,          # known_high
    KolibriHash128,    # target_hash
    c_uint64,          # low_start
    c_uint64,          # low_end
    c_uint32,          # threads
    c_uint32           # policy
]

POLICY_FIRST_FOUND_FAST = 1
POLICY_LOWEST_KEY_IN_RANGE = 2
STATUS_OK = 0
STATUS_NOT_FOUND = 1
STATUS_INVALID_ARGUMENT = 2

def run_test(mode="small-window"):
    print(f"=== KOLIBRI PARTIAL KEY RECOVERY TEST ({mode}) ===\n")

    if mode == "small-window":
        secret_high = random.getrandbits(64)
        secret_low = random.getrandbits(32)
        window_half = 100000
        low_start = max(0, secret_low - window_half)
        low_end = min(0xFFFFFFFFFFFFFFFF, secret_low + window_half)
        threads = 8
        policy = POLICY_LOWEST_KEY_IN_RANGE

    elif mode == "not-found":
        secret_high = random.getrandbits(64)
        secret_low = random.getrandbits(32)
        # Окно, которое точно не содержит secret_low
        if secret_low > 0x100000:
            low_start = 0
            low_end = 0xFFFFF
        else:
            low_start = 0x100000
            low_end = 0x1FFFFF
        threads = 8
        policy = POLICY_LOWEST_KEY_IN_RANGE

    elif mode == "32bit-stress":
        secret_high = random.getrandbits(64)
        secret_low = random.getrandbits(32)
        low_start = 0
        low_end = 0xFFFFFFFF
        threads = 8
        policy = POLICY_LOWEST_KEY_IN_RANGE
        print("WARNING: This is a stress test for 32-bit low space. It may take some time.")

    else:
        print(f"Unknown mode: {mode}")
        sys.exit(1)

    secret_key = KolibriKey128(low=secret_low, high=secret_high)
    target_hash = lib.kolibri_hash_128(secret_key)

    print(f"Known High Part:   0x{secret_high:016X}")
    print(f"Target Hash Low:   0x{target_hash.low:016X}")
    print(f"Target Hash High:  0x{target_hash.high:016X}")
    print(f"Search Range:      0x{low_start:X} - 0x{low_end:X}")
    print(f"Threads:           {threads}")
    print(f"Policy:            {'lowest_key_in_range' if policy == POLICY_LOWEST_KEY_IN_RANGE else 'first_found_fast'}")
    print()

    result = lib.kolibri_recover_low64_with_known_high(
        secret_high,
        target_hash,
        low_start,
        low_end,
        threads,
        policy
    )

    print(f"--- Result ---")
    print(f"Status:            {result.status} ({'OK' if result.status == STATUS_OK else 'NOT_FOUND' if result.status == STATUS_NOT_FOUND else 'INVALID_ARG'})")
    print(f"Found:             {result.found}")
    print(f"Verified:          {result.verified}")

    if result.found:
        print(f"Recovered Low:     0x{result.recovered_low:016X}")
        print(f"Expected Low:      0x{secret_low:016X}")
        print(f"Match:             {result.recovered_low == secret_low}")
        print(f"Candidate Hash L:  0x{result.candidate_hash.low:016X}")
        print(f"Candidate Hash H:  0x{result.candidate_hash.high:016X}")
    else:
        print(f"Key not found in range.")

    print(f"Attempts:          {result.attempts}")
    print(f"Space Size:        {result.space_size}")
    print(f"Time (ms):         {result.time_ms:.2f}")
    print(f"Keys/sec:          {result.keys_per_second:.2e}")

    # Verification
    passed = False
    if mode == "not-found":
        passed = (result.status == STATUS_NOT_FOUND and not result.found)
    else:
        passed = (result.status == STATUS_OK and result.found and result.verified and result.recovered_low == secret_low)

    print(f"\nTest Status:       {'PASS' if passed else 'FAIL'}")
    return passed

if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else "small-window"
    success = run_test(mode)
    sys.exit(0 if success else 1)
