import ctypes
import os
import random
import time
from ctypes import c_uint64, Structure, c_int, byref

lib_path = os.path.join(os.path.dirname(__file__), "../../build/libkolibri_core.dylib")
lib = ctypes.CDLL(lib_path)

class KolibriKey128(Structure):
    _fields_ = [("low", c_uint64), ("high", c_uint64)]

class KolibriHash128(Structure):
    _fields_ = [("low", c_uint64), ("high", c_uint64)]

lib.kolibri_hash_128.restype = KolibriHash128
lib.kolibri_hash_128.argtypes = [KolibriKey128]

lib.kolibri_unhash_128_demo.restype = c_int
lib.kolibri_unhash_128_demo.argtypes = [KolibriHash128, ctypes.POINTER(KolibriKey128)]

def test_roundtrip_inversion(count=100000):
    print(f"=== TOY/DEMO REVERSIBLE HASH INVERSION TEST ===")
    print(f"Testing {count} random 128-bit keys...")
    
    errors = 0
    start_time = time.time()
    
    for i in range(count):
        # Generate random 128-bit key
        original_key = KolibriKey128(
            low=random.getrandbits(64),
            high=random.getrandbits(64)
        )
        
        # Forward: Key -> Hash
        h = lib.kolibri_hash_128(original_key)
        
        # Inverse: Hash -> Key
        recovered_key = KolibriKey128()
        status = lib.kolibri_unhash_128_demo(h, byref(recovered_key))
        
        if status != 0:
            print(f"Error: unhash returned non-zero status at iteration {i}")
            errors += 1
            continue
            
        # Verify roundtrip
        if recovered_key.low != original_key.low or recovered_key.high != original_key.high:
            print(f"Mismatch at iteration {i}:")
            print(f"  Original:  L=0x{original_key.low:016X} H=0x{original_key.high:016X}")
            print(f"  Recovered: L=0x{recovered_key.low:016X} H=0x{recovered_key.high:016X}")
            errors += 1
            
    elapsed = time.time() - start_time
    kps = count / elapsed if elapsed > 0 else 0
    
    print(f"\nResults:")
    print(f"Total tests: {count}")
    print(f"Errors: {errors}")
    print(f"Time: {elapsed:.2f}s")
    print(f"Speed: {kps:.0f} keys/sec")
    
    if errors == 0:
        print("Status: PASSED (Bijective roundtrip confirmed)")
    else:
        print("Status: FAILED")

if __name__ == "__main__":
    test_roundtrip_inversion()
