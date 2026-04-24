# Kolibri AI - C Core

Высокопроизводительное ядро сжатия данных на языке C для проекта Kolibri AI.

## Возможности

- ✅ Быстрое сжатие/распаковка данных
- ✅ Статистика операций в реальном времени
- ✅ Многопоточная обработка (опционально)
- ✅ Уровни оптимизации (0-3)
- ✅ Минимальное использование памяти
- ✅ Кроссплатформенность (Linux, macOS, Windows)
- ✅ Готовность к интеграции с AGI модулями

## Структура проекта

```
kolibri-c/
├── include/
│   └── kolibri_core.h      # Заголовочный файл API
├── src/
│   ├── kolibri_core.c      # Реализация ядра
│   └── main.c              # CLI приложение
├── build/                  # Каталог сборки
├── CMakeLists.txt          # Конфигурация CMake
└── README.md               # Документация
```

## Сборка

### Требования

- CMake 3.10+
- Компилятор C (GCC, Clang, MSVC)
- Опционально: pthreads для многопоточности

### Инструкция

```bash
# Создание каталога сборки
mkdir build && cd build

# Конфигурация
cmake .. -DENABLE_OPTIMIZATIONS=ON -DBUILD_CLI=ON

# Сборка
make -j$(nproc)

# Установка (опционально)
sudo make install
```

### Опции сборки

| Опция | Описание | По умолчанию |
|-------|----------|--------------|
| `BUILD_CLI` | Сборка CLI приложения | ON |
| `BUILD_TESTS` | Сборка тестов | OFF |
| `ENABLE_OPTIMIZATIONS` | Оптимизация компилятора (-O3) | ON |
| `USE_MULTITHREADING` | Поддержка потоков | OFF |

## Использование

### CLI интерфейс

```bash
# Сжатие файла
./kolibri -c input.txt output.kgen

# Распаковка
./kolibri -d archive.kgen restored.txt

# С оптимизацией и статистикой
./kolibri -c -o 3 -t 4 large.bin compressed.kgen -s

# Помощь
./kolibri --help
```

### API (программное использование)

```c
#include "kolibri_core.h"

int main() {
    KolibriEngine engine;
    
    // Инициализация
    kolibri_init(&engine, 2); // уровень оптимизации 2
    
    // Данные для сжатия
    uint8_t data[] = "Hello Kolibri!";
    size_t size = sizeof(data);
    
    // Сжатие
    KolibriResult result = kolibri_process(
        &engine, 
        data, 
        size, 
        KOLIBRI_OP_COMPRESS
    );
    
    if (result.status == KOLIBRI_STATUS_OK) {
        printf("Compressed: %zu -> %zu bytes\n", 
               result.original_size, result.size);
        printf("Ratio: %.2f\n", result.compression_ratio);
    }
    
    // Очистка
    kolibri_free_result(&result);
    kolibri_cleanup(&engine);
    
    return 0;
}
```

## Дорожная карта до AGI

### v0.1 (Текущая)
- [x] Базовое ядро на C
- [x] CLI интерфейс
- [x] Статистика операций
- [ ] Продвинутый алгоритм сжатия

### v0.2 (Q1 2025)
- [ ] Интеграция с нейросетями
- [ ] Семантический анализ данных
- [ ] Адаптивная компрессия

### v0.5 (Q3 2025)
- [ ] Обучение на лету
- [ ] Предсказание паттернов
- [ ] Автономная оптимизация

### v1.0 (2026)
- [ ] Полноценный AGI модуль
- [ ] Самосознание системы
- [ ] Автономное развитие

## Производительность

Тесты на файле 1MB:
- Сжатие: ~50ms (O3 оптимизация)
- Распаковка: ~30ms
- Использование памяти: <10MB

## Лицензия

MIT License

## Контакты

Website: kolibriai.ru
GitHub: github.com/kolibri-ai
