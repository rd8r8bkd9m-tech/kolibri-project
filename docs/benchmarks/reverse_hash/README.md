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

## Запуск тестов

Для запуска полной лестницы бенчмарков используйте:
```bash
python3.14 tests/benchmarks/test_benchmark_ladder.py
```

Для стресс-теста полного 32-битного пространства:
```bash
python3.14 tests/benchmarks/test_32bit_stress.py
```

## Внедрение в ядро

Модуль является частью официального API Kolibri Core и доступен через заголовочный файл `kolibri_verified_search.h`. Он служит базовым блоком для более сложных задач ИИ, таких как поиск формул и оптимизация стратегий сжатия.
