from fastapi import FastAPI, HTTPException
from pydantic import BaseModel
from typing import List, Optional
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

class HybridTask(BaseModel):
    inputs: List[int] = []
    outputs: List[int] = []
    generations: int = 500
    target_hash: Optional[int] = 0
    search_space_bits: Optional[int] = 32
    range_start: Optional[int] = None
    range_end: Optional[int] = None
    known_target_key: Optional[int] = None
    policy: Optional[str] = "lowest_key_in_range" # or first_found_fast

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

    candidate_key = result["key"]
    verified = True # C-core already verified it
    
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
        "candidate_hash": result["hash"],
        "candidate_hash_hex": f"0x{result['hash']:08X}",
        "target_hash": task.target_hash,
        "target_hash_hex": f"0x{task.target_hash:08X}",
        "verified": verified,
        "hamming_distance": 0,
        "matching_bits": 32,
        "attempts": result["attempts"],
        "space_size": max_key - min_key + 1,
        "time_ms": round(result["time_ms"], 2),
        "keys_per_second": round(result["kps"]),
        "threads": 8,
        "range_start": min_key,
        "range_start_hex": f"0x{min_key:X}",
        "range_end": max_key,
        "range_end_hex": f"0x{max_key:X}",
        "search_policy": task.policy
    }
    
    return response

@app.get("/health")
def health_check():
    return {"status": "ok", "core_loaded": ai_core is not None}
