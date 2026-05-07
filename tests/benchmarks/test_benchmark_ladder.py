import requests
import random
import time

def simple_hash(key: int) -> int:
    h = 0
    k = key & 0xFFFFFFFF
    for _ in range(8):
        h = ((h ^ k) * 0x5BD1E995) & 0xFFFFFFFF
        k = ((k >> 13) | (k << 19)) & 0xFFFFFFFF
    return h

def run_benchmark(secret_key, bits):
    url = "http://127.0.0.1:8000/solve/hybrid"
    target_hash = simple_hash(secret_key)
    max_val = (1 << bits) - 1

    # Генерируем "шум" для обучающей выборки (для эволюции, если понадобится)
    inputs = [random.randint(0, max_val) for _ in range(50)]
    outputs = [simple_hash(x) for x in inputs]

    payload = {
        "inputs": inputs,
        "outputs": outputs,
        "target_hash": target_hash,
        "search_space_bits": bits,
        "generations": 100 
    }

    start_time = time.time()
    response = requests.post(url, json=payload, timeout=300)
    elapsed_ms = (time.time() - start_time) * 1000

    if response.status_code == 200:
        data = response.json()
        verified = data.get("verified", False)
        status = "PASSED" if verified else "FAILED"
        
        print(f"\n--- BENCHMARK: reverse_hash_{bits}bit_0x{secret_key:X} ---")
        print(f"status: {status}")
        print(f"method: {data.get('method')}")
        print(f"key: 0x{secret_key:X}")
        print(f"candidate_key_hex: {data.get('candidate_key_hex')}")
        print(f"target_hash: 0x{target_hash:08X}")
        print(f"verified: {str(verified).lower()}")
        print(f"matching_bits: {data.get('matching_bits')}/32")
        print(f"hamming_distance: {data.get('hamming_distance')}")
        print(f"time_ms: {data.get('time_ms'):.2f} (API: {elapsed_ms:.2f})")
        
        return verified
    else:
        print(f"[ERROR] Request failed: {response.text}")
        return False

if __name__ == "__main__":
    print("=== KOLIBRI AI REVERSE HASH BENCHMARK LADDER ===")
    
    tests = [
        (0xEF, 8),
        (0xBEEF, 16),
        (0xDBEEF, 20),
        (0xADBEEF, 24),
        # (0xDEADBEEF, 32) # Пока закомментируем, чтобы не ждать долго
    ]

    passed = 0
    for key, bits in tests:
        if run_benchmark(key, bits):
            passed += 1

    print(f"\n=== SUMMARY: {passed}/{len(tests)} TESTS PASSED ===")
