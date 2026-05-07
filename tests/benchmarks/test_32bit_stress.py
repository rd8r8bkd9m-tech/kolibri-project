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

def run_stress_test(secret_key, range_start, range_end, label):
    url = "http://127.0.0.1:8000/solve/hybrid"
    target_hash = simple_hash(secret_key)

    # Минимальные данные для payload
    inputs = [random.randint(0, 0xFFFF) for _ in range(10)]
    outputs = [simple_hash(x) for x in inputs]

    payload = {
        "inputs": inputs,
        "outputs": outputs,
        "target_hash": target_hash,
        "range_start": range_start,
        "range_end": range_end
    }

    print(f"\n--- STRESS TEST: {label} ---")
    print(f"Range: 0x{range_start:X} – 0x{range_end:X}")
    print(f"Target Key: 0x{secret_key:X}")
    
    start_time = time.time()
    response = requests.post(url, json=payload, timeout=600) # 10 min timeout
    elapsed_ms = (time.time() - start_time) * 1000

    if response.status_code == 200:
        data = response.json()
        verified = data.get("verified", False)
        status = "PASSED" if verified else "FAILED"
        
        print(f"Status: {status}")
        print(f"Method: {data.get('method')}")
        print(f"Candidate: {data.get('candidate_key_hex')}")
        print(f"Verified: {str(verified).lower()}")
        print(f"Hamming Distance: {data.get('hamming_distance')}")
        print(f"Time: {data.get('time_ms')} ms")
        print(f"Attempts: {data.get('attempts')}")
        print(f"Speed: {data.get('keys_per_second')} keys/sec")
        print(f"Threads: {data.get('threads')}")
        
        return verified
    else:
        print(f"[ERROR] Request failed: {response.text}")
        return False

if __name__ == "__main__":
    print("=== KOLIBRI AI 32-BIT STRESS LADDER ===")
    
    secret_key = 0xDEADBEEF
    
    tests = [
        (secret_key, 0xDE000000, 0xDEFFFFFF, "32-bit Window (24-bit space)"),
        (secret_key, 0xD0000000, 0xDFFFFFFF, "32-bit Window (28-bit space)"),
        (secret_key, 0x00000000, 0xFFFFFFFF, "Full 32-bit Space")
    ]

    passed = 0
    for key, start, end, label in tests:
        if run_stress_test(key, start, end, label):
            passed += 1

    print(f"\n=== SUMMARY: {passed}/{len(tests)} TESTS PASSED ===")
