# Документация Архиватора Kolibri OS

## Обзор

Архиватор Kolibri OS — это продвинутая система сжатия, реализующая многослойное сжатие с математическим анализом для достижения высоких коэффициентов сжатия. Система поддерживает все типы файлов и обеспечивает проверку целостности через контрольные суммы CRC32.

## Архитектура

### Методы сжатия

Архиватор реализует три взаимодополняющих метода сжатия:

1. **Математический анализ** - Дельта-кодирование для числовых паттернов
   - Анализирует данные на математические последовательности
   - Сохраняет разности вместо абсолютных значений
   - Особенно эффективен для последовательных или числовых данных

2. **Сжатие LZ77** - Словарное сжатие
   - Размер скользящего окна: 4096 байт
   - Максимальная длина совпадения: 255 байт
   - Заменяет повторяющиеся последовательности парами (расстояние, длина)
   - Эффективен для данных с повторяющимися паттернами

3. **Кодирование длин серий (RLE)** - Сжатие повторений
   - Кодирует серии из 4 или более одинаковых байт
   - Формат: маркер (0xFF) + счётчик + значение
   - Экранирует литеральные байты-маркеры
   - Эффективен для данных с длинными сериями одинаковых байт

### Конвейер сжатия

Данные проходят через конвейер сжатия в следующем порядке:

```
Входные данные → Математический → LZ77 → RLE → Сжатый вывод
```

Распаковка выполняется в обратном порядке:

```
Сжатый ввод → RLE → LZ77 → Математический → Выходные данные
```

Применяются и записываются в заголовок только те методы, которые улучшают сжатие.

### Формат файла

#### Заголовок сжатого файла (64 байта)
```c
struct KolibriCompressHeader {
    uint32_t magic;           // 0x4B4C4252 ("KLBR")
    uint32_t version;         // Версия формата (1)
    uint32_t methods;         // Битовое поле применённых методов
    uint32_t original_size;   // Размер исходных данных
    uint32_t compressed_size; // Размер сжатых данных
    uint32_t checksum;        // Контрольная сумма CRC32 исходных данных
    uint32_t file_type;       // Определённый тип файла
    uint8_t  reserved[12];    // Зарезервировано для будущего использования
};
```

#### Формат архива
```c
struct KolibriArchiveHeader {
    uint32_t magic;          // 0x4B415243 ("KARC")
    uint32_t version;        // Версия формата (1)
    uint32_t entry_count;    // Количество файлов в архиве
    uint8_t  reserved[52];   // Зарезервировано для будущего использования
};
```

Каждая запись содержит:
- Имя файла (256 байт)
- Исходный и сжатый размеры
- Контрольную сумму CRC32
- Временную метку
- Тип файла
- Смещение данных в архиве

## Использование

### Интерфейс командной строки

#### Сжать файл
```bash
kolibri_archiver compress input.txt output.klb
```

Вывод:
```
Сжатие 'input.txt' в 'output.klb'...

Сжатие завершено!
Исходный размер:    10000 байт
Сжатый размер:      1500 байт
Коэффициент сжатия: 6.67x
Тип файла:          Текст
Использованные методы: Mathematical+LZ77+RLE
Время сжатия: 2.50 мс
Контрольная сумма: 0x12345678
```

#### Распаковать файл
```bash
kolibri_archiver decompress output.klb restored.txt
```

#### Протестировать коэффициент сжатия
```bash
kolibri_archiver test input.txt
```

Тестирует сжатие и проверяет целостность данных без создания выходных файлов.

#### Создать архив
```bash
kolibri_archiver create myarchive.kar
```

#### Добавить файлы в архив
```bash
kolibri_archiver add myarchive.kar document.pdf
kolibri_archiver add myarchive.kar image.png
kolibri_archiver add myarchive.kar data.json
```

#### Список содержимого архива
```bash
kolibri_archiver list myarchive.kar
```

Вывод:
```
Список содержимого архива 'myarchive.kar':

Имя                                      Исходный  Сжатый      Коэфф  Тип
--------------------------------------------------------------------------------
document.pdf                               245678    98234    2.50x Бинарный
image.png                                  156789   145234    1.08x Изображение
data.json                                   12345     3456    3.57x Текст
--------------------------------------------------------------------------------
Всего файлов: 3
```

#### Извлечь из архива
```bash
kolibri_archiver extract myarchive.kar document.pdf
```

### C API

#### Базовое сжатие
```c
#include "kolibri/compress.h"

// Создать компрессор
KolibriCompressor *comp = kolibri_compressor_create(KOLIBRI_COMPRESS_ALL);

// Сжать данные
uint8_t *compressed = NULL;
size_t compressed_size = 0;
KolibriCompressStats stats;

int ret = kolibri_compress(comp, input_data, input_size,
                            &compressed, &compressed_size, &stats);

if (ret == 0) {
    printf("Сжато %zu -> %zu байт (%.2fx)\n",
           stats.original_size, stats.compressed_size,
           stats.compression_ratio);
}

// Очистка
kolibri_compressor_destroy(comp);
free(compressed);
```

#### Распаковка
```c
// Распаковать данные
uint8_t *decompressed = NULL;
size_t decompressed_size = 0;

int ret = kolibri_decompress(compressed, compressed_size,
                              &decompressed, &decompressed_size, NULL);

if (ret == 0) {
    // Использовать распакованные данные
    // ...
}

free(decompressed);
```

#### Операции с архивами
```c
// Создать архив
KolibriArchive *archive = kolibri_archive_create("myarchive.kar");

// Добавить файлы
kolibri_archive_add_file(archive, "file1.txt", file_data, file_size);
kolibri_archive_add_file(archive, "file2.dat", data2, size2);

// Закрыть и сохранить
kolibri_archive_close(archive);

// Открыть существующий архив
archive = kolibri_archive_open("myarchive.kar");

// Список содержимого
KolibriArchiveEntry *entries = NULL;
size_t count = 0;
kolibri_archive_list(archive, &entries, &count);

for (size_t i = 0; i < count; i++) {
    printf("%s: %zu байт\n", entries[i].name, entries[i].original_size);
}
free(entries);

// Извлечь файл
uint8_t *file_data = NULL;
size_t file_size = 0;
kolibri_archive_extract_file(archive, "file1.txt", &file_data, &file_size);
free(file_data);

kolibri_archive_close(archive);
```

### Python API (Планируется)

```python
from kolibri import compress

# Сжать файл
with open('input.txt', 'rb') as f:
    data = f.read()

compressed, stats = compress.compress(data)
print(f"Коэффициент сжатия: {stats.ratio}x")

# Распаковать
decompressed = compress.decompress(compressed)

# Операции с архивами
archive = compress.Archive('myarchive.kar', mode='w')
archive.add_file('document.pdf', pdf_data)
archive.add_file('image.png', image_data)
archive.close()
```

### WebAssembly API (Планируется)

```javascript
// Загрузить WASM модуль
const kolibri = await loadKolibriWasm();

// Сжать данные
const inputData = new Uint8Array([...]); 
const result = kolibri.compress(inputData);
console.log(`Сжато: ${result.originalSize} -> ${result.compressedSize} байт`);

// Распаковать
const decompressed = kolibri.decompress(result.data);
```

## Характеристики производительности

### Коэффициенты сжатия по типу данных

- **Высокоповторяющиеся данные**: 10-100x
- **Текстовые файлы**: 2-5x
- **Исходный код**: 3-6x
- **Бинарные исполняемые файлы**: 1.5-3x
- **Уже сжатые данные**: ~1x (без улучшения)
- **Случайные данные**: ~1x (без улучшения)

### Скорость

- **Сжатие**: ~50-200 МБ/с (зависит от сложности данных)
- **Распаковка**: ~100-300 МБ/с (быстрее сжатия)

### Использование памяти

- Сжатие требует ~4x размера входных данных для временных буферов
- Распаковка требует ~3x размера сжатых данных для временных буферов
- Операции с архивами используют минимум дополнительной памяти

## Технические характеристики

### Ограничения

- Максимальный размер файла в архиве: Ограничен доступной памятью
- Максимум записей в архиве: 1024 файла
- Максимальное расстояние LZ77: 4095 байт
- Максимальное совпадение LZ77: 255 байт
- Максимальная серия RLE: 255 байт
- Длина имени файла в архивах: 256 символов

### Поддерживаемые типы файлов

- **Текст**: UTF-8 и ASCII текстовые файлы
- **Бинарные**: Все бинарные форматы
- **Изображения**: PNG, JPEG, GIF (определяются, но не оптимизированы специально)

Определение типа файла происходит автоматически на основе сигнатур файлов и анализа содержимого.

### Целостность данных

- Контрольные суммы CRC32 проверяют целостность данных после распаковки
- Распаковка завершается неудачей, если контрольная сумма не совпадает
- Записи архива включают индивидуальные контрольные суммы

## Сборка

### Требования

- Компилятор C с поддержкой C11
- CMake 3.16+
- OpenSSL (для CRC32 и других криптографических функций)
- SQLite3

### Команды сборки

```bash
# Конфигурация
cmake -S . -B build -DKOLIBRI_ENABLE_TESTS=ON

# Сборка
cmake --build build

# Установка
sudo cmake --install build

# Запуск тестов
ctest --test-dir build
```

Исполняемый файл архиватора будет в `build/kolibri_archiver`.

## Примеры

### Сжать несколько файлов

```bash
# Создать архив
kolibri_archiver create backup.kar

# Добавить файлы
for file in *.txt; do
    kolibri_archiver add backup.kar "$file"
done

# Список содержимого
kolibri_archiver list backup.kar
```

### Бенчмарк сжатия

```bash
# Сгенерировать тестовый файл
dd if=/dev/zero of=test.dat bs=1M count=10

# Протестировать сжатие
kolibri_archiver test test.dat
```

### Проверить целостность файла

```bash
# Сжать
kolibri_archiver compress original.txt compressed.klb

# Распаковать
kolibri_archiver decompress compressed.klb restored.txt

# Сравнить
diff original.txt restored.txt
# Не должно быть вывода, если файлы идентичны
```

## Будущие улучшения

### Запланированные функции

1. **Улучшенное формульное сжатие**
   - Определение и кодирование математических формул
   - Поддержка геометрических и алгебраических паттернов
   - Автоматическое обнаружение формул

2. **Кодирование Хаффмана**
   - Слой сжатия на основе энтропии
   - Адаптивные деревья Хаффмана
   - Интеграция с существующим конвейером

3. **Параллельное сжатие**
   - Многопоточное сжатие для больших файлов
   - Параллельная обработка на основе блоков

4. **Потоковый API**
   - Сжатие/распаковка потоков данных
   - Поддержка каналов и сетевых потоков

5. **Продвинутая оптимизация под типы файлов**
   - Специализированное сжатие для изображений
   - Оптимизации для аудио/видео
   - Сжатие с учётом структурированных данных (JSON, XML)

6. **Настройка уровня сжатия**
   - Быстрое сжатие (уровень 1)
   - Сбалансированное (уровень 5, по умолчанию)
   - Максимальное сжатие (уровень 9)

## Лицензия

Copyright (c) 2025 Проект Kolibri OS

См. файл LICENSE для подробностей.

## Участие в разработке

Приветствуются вклады! См. CONTRIBUTING.md для руководства.

## Поддержка

Для сообщений об ошибках и запросов функций, пожалуйста, откройте issue на GitHub.
