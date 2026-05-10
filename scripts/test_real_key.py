import requests
import random
import sys

def simple_hash(key: int) -> int:
    """Простая нелинейная хеш-функция для теста"""
    h = 0
    key &= 0xFFFFFFFF
    for _ in range(8):
        h = ((h ^ key) * 0x5BD1E995) & 0xFFFFFFFF
        key = ((key >> 13) | (key << 19)) & 0xFFFFFFFF
    return h

def test_key_crack(secret_key=0xBEEF, bits=16):
    url = "http://127.0.0.1:8000/solve/hybrid"

    target_hash = simple_hash(secret_key)
    max_val = (1 << bits) - 1

    print(f"[TARGET] secret_key = 0x{secret_key:X}")
    print(f"[TARGET] target_hash = 0x{target_hash:08X} ({target_hash})")
    print(f"[CONFIG] Search space: {bits}-bit (0 to {max_val})")

    # Генерируем обучающую выборку
    inputs = [random.randint(0, max_val) for _ in range(100)]
    outputs = [simple_hash(x) for x in inputs]

    payload = {
        "inputs": inputs,
        "outputs": outputs,
        "target_hash": target_hash,
        "search_space_bits": bits,
        "generations": 500
    }

    print("[CLIENT] Отправка задачи на поиск ключа...")
    response = requests.post(url, json=payload, timeout=120)

    if response.status_code == 200:
        data = response.json()
        print("[SUCCESS] Ответ от Kolibri AI:", data)

        candidate = data.get("candidate_key")
        if candidate is not None:
            candidate = int(candidate)
            candidate_hash = simple_hash(candidate)
            print(f"[CHECK] candidate = 0x{candidate:X}")
            print(f"[CHECK] hash = 0x{candidate_hash:08X}")

            if candidate_hash == target_hash:
                print("[OK] Ключ успешно восстановлен!")
            else:
                print(f"[INFO] Найдена коллизия или близкое решение. Hamming: {data.get('hamming_distance')}")
    else:
        print(f"[ERROR] {response.status_code}: {response.text}")

if __name__ == "__main__":
    # По умолчанию тестируем 16 бит, но можно передать аргументы: python test_real_key.py 0x0BEEF 20
    key = 0xBEEF
    bits = 16
    
    if len(sys.argv) > 1:
        key = int(sys.argv[1], 16)
    if len(sys.argv) > 2:
        bits = int(sys.argv[2])
        
    test_key_crack(key, bits)
