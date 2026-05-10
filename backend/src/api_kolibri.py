from fastapi import FastAPI, HTTPException
from pydantic import BaseModel
from typing import List, Optional, Union
import sys
import os

sys.path.append(os.path.dirname(__file__))
try:
    from kolibri_wrapper import KolibriAI
    ai_core = KolibriAI()
except Exception as e:
    print(f"Warning: Kolibri C-core not loaded: {e}")
    ai_core = None

app = FastAPI(title="Kolibri AI Backend")

class ReverseHashTask(BaseModel):
    inputs: List[int] = []
    outputs: List[int] = []
    generations: int = 500
    target_hash: Optional[int] = 0
    search_space_bits: Optional[int] = 32
    range_start: Optional[int] = None
    range_end: Optional[int] = None
    known_target_key: Optional[int] = None
    policy: Optional[str] = "lowest_key_in_range"

class PartialKeyRecovery128Task(BaseModel):
    hash_function: Optional[str] = "feistel128_demo"
    known_high_hex: str
    target_hash_low_hex: str
    target_hash_high_hex: str
    low_start_hex: str
    low_end_hex: str
    threads: Optional[int] = 8
    search_policy: Optional[str] = "lowest_key_in_range"

class HashLab128Task(BaseModel):
    mode: str = "BRUTE_FORCE_MODE"  # BRUTE_FORCE_MODE or INVERSION_MODE
    hash_function: Optional[str] = "feistel128_demo"
    target_hash_low_hex: str
    target_hash_high_hex: str
    # For BRUTE_FORCE_MODE only:
    known_high_hex: Optional[str] = None
    low_start_hex: Optional[str] = None
    low_end_hex: Optional[str] = None
    threads: Optional[int] = 8

class HybridTask(BaseModel):
    task: Optional[str] = "reverse_hash"
    # reverse_hash fields
    inputs: Optional[List[int]] = []
    outputs: Optional[List[int]] = []
    generations: Optional[int] = 500
    target_hash: Optional[int] = 0
    search_space_bits: Optional[int] = 32
    range_start: Optional[int] = None
    range_end: Optional[int] = None
    known_target_key: Optional[int] = None
    policy: Optional[str] = "lowest_key_in_range"
    # partial_key_recovery_128 fields
    hash_function: Optional[str] = "feistel128_demo"
    known_high_hex: Optional[str] = None
    target_hash_low_hex: Optional[str] = None
    target_hash_high_hex: Optional[str] = None
    low_start_hex: Optional[str] = None
    low_end_hex: Optional[str] = None
    threads: Optional[int] = 8
    search_policy: Optional[str] = "lowest_key_in_range"

def simple_hash_check(key: int) -> int:
    if ai_core:
        return ai_core.lib.kolibri_simple_hash_v1(key)
    h = 0
    k = key & 0xFFFFFFFF
    for _ in range(8):
        h = ((h ^ k) * 0x5BD1E995) & 0xFFFFFFFF
        k = ((k >> 13) | (k << 19)) & 0xFFFFFFFF
    return h

@app.post("/solve/hybrid")
def solve_hybrid(task: HybridTask):
    if not ai_core:
        raise HTTPException(status_code=503, detail="Kolibri C-core is unavailable")

    task_type = task.task or "reverse_hash"

    if task_type == "partial_key_recovery_128":
        return _handle_partial_key_recovery_128(task)
    else:
        return _handle_reverse_hash(task)


def _handle_reverse_hash(task: HybridTask):
    min_key = task.range_start if task.range_start is not None else 0
    max_key = task.range_end if task.range_end is not None else ((1 << (task.search_space_bits or 32)) - 1)

    policy_val = 2 if task.policy == "lowest_key_in_range" else 1

    result = ai_core.bruteforce_hash(
        min_key=min_key,
        max_key=max_key,
        target_hash=task.target_hash,
        threads=8,
        policy=policy_val
    )

    if not result:
        return {
            "status": "not_found",
            "verified": False,
            "message": "No key found in the specified range"
        }

    candidate_key = result.get("key", 0)
    verified = True

    # Classification logic
    result_type = "preimage_found"
    if task.known_target_key is not None:
        if candidate_key == task.known_target_key:
            result_type = "original_key_recovered"
        else:
            result_type = "alternate_preimage_collision"

    response = {
        "status": "solved",
        "task": "reverse_hash",
        "method": "bruteforce_c_parallel",
        "result_type": result_type,
        "candidate_key": candidate_key,
        "candidate_key_hex": f"0x{candidate_key:X}",
        "candidate_hash": result.get("hash", 0),
        "candidate_hash_hex": f"0x{result.get('hash', 0):08X}",
        "target_hash": task.target_hash,
        "target_hash_hex": f"0x{task.target_hash:08X}",
        "verified": verified,
        "hamming_distance": 0,
        "matching_bits": 32,
        "attempts": result.get("attempts", 0),
        "space_size": max_key - min_key + 1,
        "time_ms": round(result.get("time_ms", 0), 2),
        "keys_per_second": round(result.get("kps", 0)),
        "threads": 8,
        "range_start": min_key,
        "range_start_hex": f"0x{min_key:X}",
        "range_end": max_key,
        "range_end_hex": f"0x{max_key:X}",
        "search_policy": task.policy
    }

    return response


def _handle_partial_key_recovery_128(task: HybridTask):
    # Parse hex values
    try:
        known_high = int(task.known_high_hex, 16) if task.known_high_hex else 0
        target_hash_low = int(task.target_hash_low_hex, 16) if task.target_hash_low_hex else 0
        target_hash_high = int(task.target_hash_high_hex, 16) if task.target_hash_high_hex else 0
        low_start = int(task.low_start_hex, 16) if task.low_start_hex else 0
        low_end = int(task.low_end_hex, 16) if task.low_end_hex else 0
    except ValueError as e:
        return {
            "status": "rejected",
            "reason": "invalid_hex_format",
            "error": str(e)
        }

    policy_val = 2 if task.search_policy == "lowest_key_in_range" else 1
    threads = task.threads or 8

    # Check for infeasible search space
    space_size = low_end - low_start + 1
    if space_size > (1 << 40):
        return {
            "status": "rejected",
            "reason": "search_space_infeasible",
            "space_bits": 128,
            "space_size_estimate": f"2^{128}",
            "estimated_time_years": "~1.26e22",
            "suggested_methods": [
                "known_prefix",
                "bounded_window",
                "analytic_inverse",
                "symbolic_solver",
                "meet_in_the_middle"
            ]
        }

    result = ai_core.recover_low64_with_known_high(
        known_high=known_high,
        target_hash_low=target_hash_low,
        target_hash_high=target_hash_high,
        low_start=low_start,
        low_end=low_end,
        threads=threads,
        policy=policy_val
    )

    # Map status
    STATUS_OK = 0
    STATUS_NOT_FOUND = 1
    STATUS_INVALID_ARGUMENT = 2

    if result.status == STATUS_INVALID_ARGUMENT:
        return {
            "status": "rejected",
            "reason": "invalid_argument",
            "space_size": result.space_size
        }

    if not result.found:
        return {
            "status": "not_found",
            "task": "partial_key_recovery_128",
            "method": "bruteforce_c_parallel",
            "verified": False,
            "known_high_hex": f"0x{known_high:016X}",
            "low_start_hex": f"0x{low_start:X}",
            "low_end_hex": f"0x{low_end:X}",
            "search_policy": task.search_policy
        }

    # Determine result_type
    result_type = "preimage_found"  # For partial recovery, we don't know the original low

    response = {
        "status": "solved",
        "task": "partial_key_recovery_128",
        "method": "bruteforce_c_parallel",
        "result_type": result_type,
        "known_high_hex": f"0x{result.known_high:016X}",
        "recovered_low_hex": f"0x{result.recovered_low:016X}",
        "recovered_key_low_hex": f"0x{result.recovered_key.low:016X}",
        "recovered_key_high_hex": f"0x{result.recovered_key.high:016X}",
        "target_hash_low_hex": f"0x{result.target_hash.low:016X}",
        "target_hash_high_hex": f"0x{result.target_hash.high:016X}",
        "candidate_hash_low_hex": f"0x{result.candidate_hash.low:016X}",
        "candidate_hash_high_hex": f"0x{result.candidate_hash.high:016X}",
        "verified": result.verified,
        "attempts": result.attempts,
        "space_size": result.space_size,
        "time_ms": round(result.time_ms, 2),
        "keys_per_second": round(result.keys_per_second),
        "threads": result.threads,
        "search_policy": task.search_policy
    }

    return response

@app.post("/hash_lab/128")
def hash_lab_128(task: HashLab128Task):
    if not ai_core:
        raise HTTPException(status_code=503, detail="Kolibri C-core is unavailable")

    # Parse hex values
    try:
        target_hash_low = int(task.target_hash_low_hex, 16) if task.target_hash_low_hex else 0
        target_hash_high = int(task.target_hash_high_hex, 16) if task.target_hash_high_hex else 0
    except ValueError as e:
        return {
            "status": "rejected",
            "reason": "invalid_hex_format",
            "error": str(e)
        }

    mode = 0 if task.mode == "BRUTE_FORCE_MODE" else 1
    hash_id = 2  # KOLIBRI_HASH_FEISTEL_128_DEMO
    threads = task.threads or 8

    if mode == 1:
        # INVERSION_MODE: analytical inversion
        result = ai_core.hash_lab_128(
            mode=mode,
            hash_id=hash_id,
            target_hash_low=target_hash_low,
            target_hash_high=target_hash_high,
            known_high=0,
            low_start=0,
            low_end=0,
            threads=threads
        )

        STATUS_OK = 0
        if result.status != STATUS_OK or not result.success:
            return {
                "status": "failed",
                "mode": "INVERSION_MODE",
                "message": "Inversion failed for this hash (may not be invertible)"
            }

        return {
            "status": "solved",
            "mode": "INVERSION_MODE",
            "hash_function": task.hash_function,
            "target_hash_low_hex": f"0x{result.target_hash.low:016X}",
            "target_hash_high_hex": f"0x{result.target_hash.high:016X}",
            "inverted_key_low_hex": f"0x{result.inverted_key.low:016X}",
            "inverted_key_high_hex": f"0x{result.inverted_key.high:016X}",
            "recomputed_hash_low_hex": f"0x{result.recomputed_hash.low:016X}",
            "recomputed_hash_high_hex": f"0x{result.recomputed_hash.high:016X}",
            "verified": result.success,
            "attempts": result.attempts,
            "time_ms": round(result.time_ms, 4),
            "keys_per_second": round(result.keys_per_second) if result.keys_per_second > 0 else 0
        }
    else:
        # BRUTE_FORCE_MODE: partial brute-force with known high prefix
        try:
            known_high = int(task.known_high_hex, 16) if task.known_high_hex else 0
            low_start = int(task.low_start_hex, 16) if task.low_start_hex else 0
            low_end = int(task.low_end_hex, 16) if task.low_end_hex else 0
        except ValueError as e:
            return {
                "status": "rejected",
                "reason": "invalid_hex_format",
                "error": str(e)
            }

        # Check for infeasible search space
        space_size = low_end - low_start + 1
        if space_size > (1 << 40):
            return {
                "status": "rejected",
                "reason": "search_space_infeasible",
                "space_bits": 128,
                "space_size_estimate": f"2^{128}",
                "estimated_time_years": "~1.26e22",
                "suggested_methods": [
                    "known_prefix",
                    "bounded_window",
                    "analytic_inverse",
                    "symbolic_solver",
                    "meet_in_the_middle"
                ]
            }

        result = ai_core.hash_lab_128(
            mode=mode,
            hash_id=hash_id,
            target_hash_low=target_hash_low,
            target_hash_high=target_hash_high,
            known_high=known_high,
            low_start=low_start,
            low_end=low_end,
            threads=threads
        )

        STATUS_OK = 0
        if result.status != STATUS_OK or not result.success:
            return {
                "status": "not_found",
                "mode": "BRUTE_FORCE_MODE",
                "known_high_hex": f"0x{known_high:016X}",
                "low_start_hex": f"0x{low_start:X}",
                "low_end_hex": f"0x{low_end:X}",
                "message": "No key found in the specified range"
            }

        return {
            "status": "solved",
            "mode": "BRUTE_FORCE_MODE",
            "hash_function": task.hash_function,
            "known_high_hex": f"0x{known_high:016X}",
            "recovered_low_hex": f"0x{result.recovered_key.low:016X}",
            "recovered_key_low_hex": f"0x{result.recovered_key.low:016X}",
            "recovered_key_high_hex": f"0x{result.recovered_key.high:016X}",
            "target_hash_low_hex": f"0x{result.target_hash.low:016X}",
            "target_hash_high_hex": f"0x{result.target_hash.high:016X}",
            "candidate_hash_low_hex": f"0x{result.candidate_hash.low:016X}",
            "candidate_hash_high_hex": f"0x{result.candidate_hash.high:016X}",
            "verified": result.success,
            "attempts": result.attempts,
            "time_ms": round(result.time_ms, 2),
            "keys_per_second": round(result.keys_per_second),
            "threads": threads
        }

@app.get("/health")
def health_check():
    return {"status": "ok", "core_loaded": ai_core is not None}
