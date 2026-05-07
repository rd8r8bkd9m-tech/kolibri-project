import requests
import random
import time

def simple_hash_v1(key: int) -> int:
    # Эмуляция поведения C-core: ключ приводится к uint32_t перед хешированием
    k = key & 0xFFFFFFFF
    h = 0
    for _ in range(8):
        h = ((h ^ k) * 0x5BD1E995) & 0xFFFFFFFF
        k = ((k >> 13) | (k << 19)) & 0xFFFFFFFF
    return h

def test_40bit_recovery():
    # Генерируем случайный 40-битный ключ
    secret_key = random.getrandbits(40)
    target_hash = simple_hash_v1(secret_key)
    
    print(f"=== KOLIBRI 40-BIT KEY RECOVERY TEST ===")
    print(f"Target Hash: 0x{target_hash:08X}")
    print(f"Secret Key (for verification): 0x{secret_key:X}")
    
    # Для ускорения теста мы не будем перебирать весь 1 трлн вариантов.
    # Мы "подскажем" системе диапазон вокруг ключа (имитация утечки части данных).
    # Диапазон: 10 миллионов вариантов вокруг ключа.
    range_start = max(0, secret_key - 5_000_000)
    range_end = min((1 << 40) - 1, secret_key + 5_000_000)
    
    url = "http://127.0.0.1:8000/solve/hybrid"
    payload = {
        "target_hash": target_hash,
        "range_start": range_start,
        "range_end": range_end,
        "policy": "lowest_key_in_range"
    }
    
    print(f"Searching in range: 0x{range_start:X} – 0x{range_end:X}...")
    start_time = time.time()
    
    try:
        response = requests.post(url, json=payload, timeout=60)
        elapsed = time.time() - start_time
        
        if response.status_code == 200:
            data = response.json()
            candidate = data.get("candidate_key")
            print(f"\nResult:")
            print(f"Status: {data.get('status')}")
            print(f"Candidate: 0x{candidate:X}")
            print(f"Verified: {data.get('verified')}")
            print(f"Time: {elapsed:.2f}s")
            print(f"Speed: {data.get('keys_per_second')} keys/sec")
            
            if candidate == secret_key:
                print("\n[SUCCESS] ORIGINAL KEY RECOVERED!")
            else:
                print("\n[INFO] Collision found (alternate preimage).")
        else:
            print(f"Error: {response.text}")
            
    except Exception as e:
        print(f"Request failed: {e}")

if __name__ == "__main__":
    test_40bit_recovery()
