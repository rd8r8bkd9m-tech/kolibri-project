# Kolibri Verified Search Core: Reverse Hash Benchmark

Этот документ описывает архитектуру, производительность и результаты тестирования модуля **Kolibri Verified Search Core**, специализирующегося на решении задач обратного хеширования (reverse-hash) и поиска с верификацией.

## Архитектура

Модуль реализован как часть C-ядра Kolibri AI (`backend/core/src/verified_search.c`) и предоставляет следующие возможности:

1.  **Высокопроизводительный перебор:** Использование OpenMP для параллельного выполнения на всех доступных ядрах (Apple Silicon M-series).
2.  **Детерминированность:** Поддержка политик поиска:
    *   `lowest_key_in_range`: Гарантирует поиск наименьшего ключа в диапазоне (для воспроизводимости результатов).
    *   `first_found_fast`: Возвращает первый найденный ключ любой нитью (для максимальной скорости).
3.  **Классификация результатов:** Система автоматически определяет, является ли найденный ключ оригинальным (`original_key_recovered`) или коллизией (`alternate_preimage_collision`).

## API Endpoint: `/solve/hybrid`

Пример JSON-запроса:
```json
{
  "target_hash": 1897813294,
  "range_start": 0,
  "range_end": 4294967295,
  "policy": "lowest_key_in_range"
}
```

Пример ответа:
```json
{
  "status": "solved",
  "method": "bruteforce_c_parallel",
  "result_type": "original_key_recovered",
  "candidate_key_hex": "0xDEADBEEF",
  "verified": true,
  "keys_per_second": 851439184,
  "time_ms": 3872.23
}
```

## Результаты бенчмарков (Apple Silicon M-Series)

| Тест | Пространство | Время | Скорость (keys/sec) | Статус |
| :--- | :--- | :--- | :--- | :--- |
| **8-bit** | $2^8$ | < 1 ms | ~100M+ | PASSED |
| **16-bit** | $2^{16}$ | ~2 ms | ~100M+ | PASSED |
| **24-bit** | $2^{24}$ | ~60 ms | ~280M | PASSED |
| **28-bit** | $2^{28}$ | ~760 ms | ~630M | PASSED |
| **32-bit (Full)**| $2^{32}$ | ~3.9 s | ~851M | PASSED |

## Partial Key Recovery (128-bit с известным префиксом)

Модуль `kolibri_recover_low64_with_known_high` позволяет восстанавливать неизвестную младшую часть 128-битного ключа, если старшая часть (high 64-bit) известна.

### Ограничения

- Полный перебор 128-bit пространства ($2^{128}$) невозможен и отвергается как infeasible.
- Поддерживается поиск только в ограниченном окне (constrained window).
- Infeasible guard: диапазоны больше $2^{40}$ автоматически отклоняются.

### Производительность

| Тест | Окно поиска | Время | Скорость (keys/sec) | Статус |
| :--- | :--- | :--- | :--- | :--- |
| **small-window** | ~200K | ~2 ms | ~110M | PASSED |
| **not-found** | ~1M | ~5 ms | ~214M | PASSED |
| **32-bit stress** | $2^{32}$ | ~14 s | ~316M | PASSED |

### Запуск тестов

```bash
# Малое окно (fast test)
python3.14 tests/benchmarks/test_partial_key_recovery.py small-window

# Окно без результата (not-found test)
python3.14 tests/benchmarks/test_partial_key_recovery.py not-found

# Полный 32-bit low stress (manual, ~14s)
python3.14 tests/benchmarks/test_partial_key_recovery.py 32bit-stress
```

### API Endpoint: `/solve/hybrid` с task `partial_key_recovery_128`

Пример запроса:
```json
{
  "task": "partial_key_recovery_128",
  "hash_function": "feistel128_demo",
  "known_high_hex": "0x01D1D77FDAB7F553",
  "target_hash_low_hex": "0x63BB684841F6A2C1",
  "target_hash_high_hex": "0xE5CA35CF30A828AB",
  "low_start_hex": "0x000000000C8C24A6",
  "low_end_hex": "0x000000000C8F31E6",
  "threads": 8,
  "search_policy": "lowest_key_in_range"
}
```

Пример ответа:
```json
{
  "status": "solved",
  "task": "partial_key_recovery_128",
  "method": "bruteforce_c_parallel",
  "result_type": "preimage_found",
  "known_high_hex": "0x01D1D77FDAB7F553",
  "recovered_low_hex": "0x000000000C8DAB46",
  "recovered_key_low_hex": "0x000000000C8DAB46",
  "recovered_key_high_hex": "0x01D1D77FDAB7F553",
  "verified": true,
  "attempts": 200001,
  "space_size": 200001,
  "time_ms": 1.82,
  "keys_per_second": 110000000,
  "threads": 8,
  "search_policy": "lowest_key_in_range"
}
```

## Запуск тестов

Для запуска полной лестницы бенчмарков используйте:
```bash
python3.14 tests/benchmarks/test_benchmark_ladder.py
```

Для стресс-теста полного 32-битного пространства используйте новый тест:
```bash
python3.14 tests/benchmarks/test_partial_key_recovery.py 32bit-stress
```

## Внедрение в ядро

Модуль является частью официального API Kolibri Core и доступен через заголовочный файл `kolibri_verified_search.h`. Он служит базовым блоком для более сложных задач ИИ, таких как поиск формул и оптимизация стратегий сжатия.
