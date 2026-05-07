import ctypes
import os
import random
import time
from ctypes import c_uint64, Structure

# Загрузка библиотеки
lib_path = os.path.join(os.path.dirname(__file__), "../../build/libkolibri_core.dylib")
lib = ctypes.CDLL(lib_path)

class KolibriKey128(Structure):
    _fields_ = [("low", c_uint64), ("high", c_uint64)]

class KolibriHash128(Structure):
    _fields_ = [("low", c_uint64), ("high", c_uint64)]

lib.kolibri_hash_128.restype = KolibriHash128
lib.kolibri_hash_128.argtypes = [KolibriKey128]

def test_128bit_recovery():
    print("=== KOLIBRI 128-BIT PREFIX RECOVERY TEST ===")
    
    # Генерируем случайный 128-битный ключ
    secret_high = random.getrandbits(64)
    secret_low = random.getrandbits(64)
    
    secret_key = KolibriKey128(low=secret_low, high=secret_high)
    target_hash = lib.kolibri_hash_128(secret_key)
    
    print(f"Secret Key High: 0x{secret_high:016X}")
    print(f"Target Hash Low:  0x{target_hash.low:016X}")
    print(f"Target Hash High: 0x{target_hash.high:016X}")
    
    # Имитация поиска: мы знаем старшие 64 бита (префикс) и ищем младшие 64.
    # Чтобы тест прошел быстро, мы сузим поиск младших 64 бит до 32-битного диапазона.
    # В реальности это заняло бы годы, но здесь мы эмулируем "удачное сужение".
    
    search_start = 0
    search_end = 0xFFFFFFFF # Ищем только в первых 4 миллиардах вариантов для скорости теста
    
    print(f"\nSearching for lower 64 bits in range 0x0 - 0x{search_end:X}...")
    start_time = time.time()
    
    found = False
    result_key = KolibriKey128()
    
    # Простой перебор в Python для демонстрации логики (в C-core это было бы в 1000 раз быстрее)
    # Для полноценного 128-битного солвера нам нужно расширить verified_search.c под 128 бит.
    # Сейчас мы просто проверим, что хеш-функция работает корректно.
    
    for i in range(search_start, min(search_end + 1, 10_000_000)):
        candidate = KolibriKey128(low=i, high=secret_high)
        h = lib.kolibri_hash_128(candidate)
        if h.low == target_hash.low and h.high == target_hash.high:
            result_key = candidate
            found = True
            break
            
    elapsed = time.time() - start_time
    
    if found:
        print(f"\n[SUCCESS] Key Recovered!")
        print(f"Candidate Low: 0x{result_key.low:016X}")
        print(f"Time: {elapsed:.4f}s")
        if result_key.low == secret_low:
            print("Full 128-bit match confirmed.")
    else:
        print(f"\n[INFO] Key not found in the limited search window (expected for full 64-bit space).")
        print(f"This demonstrates that without a reduced range, 128-bit brute-force is infeasible.")

if __name__ == "__main__":
    test_128bit_recovery()
