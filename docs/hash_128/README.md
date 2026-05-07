# Kolibri 128-bit Hash & Partial Recovery

Этот документ описывает реализацию 128-битного хеша и методы частичного восстановления ключей в платформе Kolibri AI.

## 1. Toy/Demo Reversible Hash (Feistel Network)

Для демонстрации возможностей инверсии используется биjective 128-битная функция на основе сети Фейстеля (`kolibri_hash_128`).

- **Гарантия:** Полная обратимость (bijective mapping).
- **Тестирование:** Roundtrip proof на 100,000 случайных ключей подтверждает отсутствие коллизий при инверсии.
- **Использование:** `kolibri_unhash_128_demo()` позволяет мгновенно восстановить ключ по хешу без перебора.

> **Важно:** Данная функция является "toy/demo" и не предназначена для криптографических целей. Она используется исключительно для тестирования архитектуры инверсии и partial recovery.

## 2. Partial Key Recovery (Known High Prefix)

Модуль `kolibri_recover_low64_with_known_high` решает задачу восстановления младшей части ключа (low 64-bit), если старшая часть (high 64-bit) известна.

### Архитектура
- **C-Core + OpenMP:** Параллельный перебор с использованием всех ядер CPU.
- **Infeasible Guard:** Автоматический отказ от поиска в диапазонах $> 2^{40}$.
- **Политики поиска:**
  - `lowest_key_in_range`: Детерминированный поиск наименьшего ключа.
  - `first_found_fast`: Максимально быстрый возврат первого найденного совпадения.

### Производительность (Apple Silicon M-Series)
| Режим | Скорость |
| :--- | :--- |
| **Partial Recovery** | ~316M keys/sec |
| **Full Inversion (Demo)** | ~570K ops/sec |

## 3. API Integration

Функциональность доступна через FastAPI endpoint `/solve/hybrid`:

```json
{
  "task": "partial_key_recovery_128",
  "known_high_hex": "0x...",
  "target_hash_low_hex": "0x...",
  "target_hash_high_hex": "0x...",
  "low_start_hex": "0x...",
  "low_end_hex": "0x..."
}
```

## 4. Тестирование

Для проверки корректности работы используйте набор тестов:

```bash
# Проверка roundtrip (биjectivity)
python3.14 tests/benchmarks/test_128bit_inversion_suite.py

# Проверка partial recovery
python3.14 tests/benchmarks/test_partial_key_recovery.py small-window
```
