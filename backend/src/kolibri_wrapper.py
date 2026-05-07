import ctypes
import os
from ctypes import c_uint32, c_uint64, c_double, c_bool, c_int, c_void_p, Structure, POINTER

class KolibriReverseHashResult(Structure):
    _fields_ = [
        ("status", c_int),
        ("method", c_int),
        ("result_type", c_int),
        ("policy", c_int),
        ("found", c_bool),
        ("verified", c_bool),
        ("timed_out", c_bool),
        ("candidate_equals_known_target_key", c_bool),
        ("candidate_hash_equals_target_hash", c_bool),
        ("candidate_key", c_uint32),
        ("candidate_hash", c_uint32),
        ("target_hash", c_uint32),
        ("hamming_distance", c_uint32),
        ("matching_bits", c_uint32),
        ("attempts", c_uint64),
        ("space_size", c_uint64),
        ("time_ms", c_double),
        ("keys_per_second", c_double),
        ("threads", c_uint32),
        ("range_start", c_uint32),
        ("range_end", c_uint32),
    ]

class KolibriReverseHashRequest(Structure):
    _fields_ = [
        ("target_hash", c_uint32),
        ("known_target_key", c_uint32),
        ("has_known_target_key", c_bool),
        ("range_start", c_uint32),
        ("range_end", c_uint32),
        ("threads", c_uint32),
        ("timeout_ms", c_uint64),
        ("hash_id", c_int),
        ("hash_fn", c_void_p),
        ("policy", c_int),
    ]

class KolibriAI:
    def __init__(self):
        lib_path = os.path.join(os.path.dirname(__file__), "../../build/libkolibri_core.dylib")
        if not os.path.exists(lib_path):
            raise FileNotFoundError(f"Kolibri C-core library not found at {lib_path}")
        
        self.lib = ctypes.CDLL(lib_path)
        
        # Setup kolibri_reverse_hash_bruteforce_u32
        self.lib.kolibri_reverse_hash_bruteforce_u32.restype = KolibriReverseHashResult
        self.lib.kolibri_reverse_hash_bruteforce_u32.argtypes = [POINTER(KolibriReverseHashRequest)]
        
        # Setup kolibri_simple_hash_v1
        self.lib.kolibri_simple_hash_v1.restype = c_uint32
        self.lib.kolibri_simple_hash_v1.argtypes = [c_uint32]

    def bruteforce_hash(self, min_key, max_key, target_hash, threads=8, policy=2):
        req = KolibriReverseHashRequest(
            target_hash=target_hash,
            known_target_key=0,
            has_known_target_key=False,
            range_start=min_key,
            range_end=max_key,
            threads=threads,
            timeout_ms=0,
            hash_id=1, # KOLIBRI_HASH_SIMPLE_V1
            hash_fn=None,
            policy=policy # 2 = LOWEST_KEY_IN_RANGE
        )
        result = self.lib.kolibri_reverse_hash_bruteforce_u32(ctypes.byref(req))
        
        if result.found:
            return {
                "key": result.candidate_key,
                "hash": result.candidate_hash,
                "attempts": result.attempts,
                "time_ms": result.time_ms,
                "kps": result.keys_per_second
            }
        return None
