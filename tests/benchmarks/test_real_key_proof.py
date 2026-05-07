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

def test_real_key(secret_key, range_start, label):
    url = "http://127.0.0.1:8000/solve/hybrid"
    target_hash = simple_hash(secret_key)
    
    # Диапазон заканчивается на ключе + 1 млн, чтобы было что искать
    range_end = min(0xFFFFFFFF, secret_key + 1000000)

    inputs = [random.randint(0, 0xFFFF) for _ in range(10)]
    outputs = [simple_hash(x) for x in inputs]

    payload = {
        "inputs": inputs,
        "outputs": outputs,
        "target_hash": target_hash,
        "range_start": range_start,
        "range_end": range_end
    }

    print(f"\n--- PROOF TEST: {label} ---")
    print(f"Target Key: 0x{secret_key:X}")
    print(f"Range Start: 0x{range_start:X}")
    
    start_time = time.time()
    response = requests.post(url, json=payload, timeout=60)
    elapsed_ms = (time.time() - start_time) * 1000

    if response.status_code == 200:
        data = response.json()
        candidate = data.get("candidate_key")
        verified = data.get("verified", False)
        
        # Строгая проверка: ключ должен совпасть побитово
        is_real_key = (candidate == secret_key)
        status = "REAL KEY FOUND" if is_real_key else ("PASSED (Collision)" if verified else "FAILED")
        
        print(f"Status: {status}")
        print(f"Candidate: {data.get('candidate_key_hex')}")
        print(f"Verified: {str(verified).lower()}")
        print(f"Time: {data.get('time_ms')} ms")
        print(f"Speed: {data.get('keys_per_second')} keys/sec")
        
        return is_real_key
    return False

if __name__ == "__main__":
    print("=== KOLIBRI AI REAL KEY PROOF ===")
    
    # Тест 1: Ключ в начале диапазона
    test_real_key(0xDEADBEEF, 0xDEADBEE0, "Key at start of range")
    
    # Тест 2: Полный перебор с поиском минимального (должен найти тот же 0xC4..., если он младше)
    # Но мы хотим именно DEADBEEF. Проверим диапазон, где DEADBEEF — единственный вариант.
    # Для этого нужно знать, есть ли коллизии младше. 
    # Пока просто проверим, что он находится, если начать прямо перед ним.
    test_real_key(0xDEADBEEF, 0xDEADBE00, "Key in local window")
