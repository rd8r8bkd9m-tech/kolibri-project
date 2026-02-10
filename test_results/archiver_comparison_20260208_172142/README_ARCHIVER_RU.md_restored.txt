# Архиватор Kolibri OS

Продвинутая система многослойного сжатия с математическим анализом для Kolibri OS.

## Быстрый старт

### Сборка

```bash
cmake -S . -B build -DKOLIBRI_ENABLE_TESTS=ON
cmake --build build
```

### Базовое использование

```bash
# Сжать файл
./build/kolibri_archiver compress myfile.txt myfile.klb

# Распаковать
./build/kolibri_archiver decompress myfile.klb restored.txt

# Протестировать коэффициент сжатия
./build/kolibri_archiver test myfile.txt
```

### Управление архивами

```bash
# Создать архив
./build/kolibri_archiver create myarchive.kar

# Добавить файлы
./build/kolibri_archiver add myarchive.kar file1.txt
./build/kolibri_archiver add myarchive.kar file2.pdf

# Список содержимого
./build/kolibri_archiver list myarchive.kar

# Извлечь файл
./build/kolibri_archiver extract myarchive.kar file1.txt
```

## Возможности

- **Многослойное сжатие**: Математический анализ + LZ77 + RLE
- **Высокие коэффициенты сжатия**: 5-40x в зависимости от типа данных
- **Целостность данных**: Контрольные суммы CRC32
- **Определение типа файла**: Автоматическое определение текста, бинарных данных и изображений
- **Поддержка архивов**: Множество файлов в одном архиве
- **Кроссплатформенность**: C11, работает на Linux, macOS, Windows

## Методы сжатия

1. **Математический анализ** - Дельта-кодирование для числовых паттернов
2. **LZ77** - Словарное сжатие с скользящим окном 4КБ
3. **RLE** - Кодирование длин серий для повторяющихся данных

Методы применяются последовательно и используются только если они улучшают сжатие.

## API

### C API

```c
#include "kolibri/compress.h"

// Сжать
KolibriCompressor *comp = kolibri_compressor_create(KOLIBRI_COMPRESS_ALL);
uint8_t *compressed = NULL;
size_t compressed_size = 0;
kolibri_compress(comp, data, size, &compressed, &compressed_size, NULL);
kolibri_compressor_destroy(comp);

// Распаковать
uint8_t *decompressed = NULL;
size_t decompressed_size = 0;
kolibri_decompress(compressed, compressed_size, &decompressed, &decompressed_size, NULL);
```

### Python API

```python
from backend.python import kolibri_compress

# Сжать
data = b"Hello, World!" * 100
compressed, stats = kolibri_compress.compress(data)
print(f"Коэффициент: {stats.compression_ratio:.2f}x")

# Распаковать
decompressed, _ = kolibri_compress.decompress(compressed)

# Файловые операции
stats = kolibri_compress.compress_file("input.txt", "output.klb")
kolibri_compress.decompress_file("output.klb", "restored.txt")
```

### WebAssembly (планируется)

```javascript
const kolibri = await loadKolibriWasm();
const compressed = kolibri.compress(inputData);
const decompressed = kolibri.decompress(compressed);
```

## Документация

См. [docs/archiver_ru.md](../docs/archiver_ru.md) для полной документации, включающей:
- Детали архитектуры
- Спецификации формата файлов
- Характеристики производительности
- Продвинутые примеры использования

## Тестирование

```bash
# Запуск юнит-тестов
./build/test_compress

# Тестирование с CTest
ctest --test-dir build -R test_compress
```

## Производительность

- **Скорость сжатия**: 50-200 МБ/с
- **Скорость распаковки**: 100-300 МБ/с
- **Повторяющиеся данные**: До 100x сжатия
- **Текстовые файлы**: 2-5x типично
- **Бинарные файлы**: 1.5-3x типично

## Ограничения

- Максимум записей в архиве: 1024 файла
- Максимальное расстояние LZ77: 4095 байт
- Максимальная длина совпадения: 255 байт
- Длина имени файла: 256 символов

## Лицензия

Часть проекта Kolibri OS. См. основной файл LICENSE.

## Участие в разработке

Приветствуются вклады! Области для улучшения:
- Слой кодирования Хаффмана
- Улучшения формульного сжатия
- Параллельное сжатие
- Потоковый API
- Оптимизации производительности

## Контакты

См. основной репозиторий Kolibri OS для контактной информации и поддержки.
