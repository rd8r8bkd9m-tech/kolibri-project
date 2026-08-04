# Вклад в Kolibri OS

Добро пожаловать в проект Kolibri OS! Этот документ описывает, как начать разработку, запустить проект и внести вклад.

## 📋 Быстрый старт для разработчиков

### Предварительные требования

- **ОС**: macOS/Linux/Windows WSL2
- **Python** 3.10+ с pip и virtualenv
- **Компилятор C**: gcc/clang с CMake 3.20+
- **QEMU** для запуска ОС (опционально)
- **Node.js** 16+ для фронтенда (опционально)

### Установка окружения

```bash
# 1. Клонируйте репозиторий
git clone https://github.com/leontov/kolibri-os.git
cd kolibri-os

# 2. Создайте виртуальное окружение Python
python3 -m venv .venv
source .venv/bin/activate  # macOS/Linux
# или для Windows: .venv\Scripts\activate

# 3. Установите зависимости
pip install --upgrade pip
pip install -r requirements.txt

# 4. Соберите C-компоненты
cmake -S . -B build -G "Ninja"  # или используйте -G "Unix Makefiles"
cmake --build build

# 5. Запустите тесты
pytest -q
ctest --test-dir build
```

### Запуск Kolibri OS в QEMU

```bash
# Вариант 1: Использовать скрипт (рекомендуется)
./scripts/run_qemu.sh

# Вариант 2: С опциями
./scripts/run_qemu.sh --no-gui  # текстовый режим
./scripts/run_qemu.sh --mem 256 # 256 МБ памяти
./scripts/run_qemu.sh --rebuild # пересобрать ISO

# Вариант 3: Ручной запуск
qemu-system-i386 -cdrom build/kolibri.iso -serial stdio -display cocoa
```

### Команды в Kolibri OS Shell

После загрузки ОС вы увидите приглашение `Kolibri OS Shell> > `. Доступные команды:

```
help              - список команд
pwd               - текущая директория
ls                - список файлов/директорий
cd <dir>          - перейти в директорию (.. для родительской)
mkdir <dir>       - создать директорию
touch <file>      - создать пустой файл
rm <file>         - удалить файл
cat <file>        - просмотреть содержимое файла
echo <text>       - вывести текст
ai                - показать лучшую AI формулу
ping              - проверить соединение (localhost)
netstat           - сетевые соединения
clear             - очистить экран
```

**Примеры:**
```
> mkdir data
> cd data
> touch test.txt
> ls
> cd ..
> ai
```

---

## 🏗️ Архитектура проекта

```
kolibri-os/
├── backend/                 # C-ядро системы
│   ├── include/            # Публичные заголовки
│   │   └── kolibri/
│   │       ├── decimal.h    # Нормализация ввода
│   │       ├── genome.h     # Цифровой геном
│   │       ├── formula.h    # AI эволюция
│   │       └── net.h        # Сетевой протокол
│   └── src/                 # Реализация
│       ├── decimal.c
│       ├── genome.c
│       ├── formula.c
│       └── net.c
├── kernel/                  # Kernel ОС (x86)
│   ├── main.c              # Главное ядро
│   ├── entry.asm           # x86 точка входа
│   └── interrupts.asm      # Обработчики прерываний
├── frontend/               # React UI (опционально)
│   └── src/
│       └── App.tsx
├── tests/                  # Unit и integration тесты
│   ├── test_decimal.c
│   ├── test_genome.c
│   ├── test_formula.c
│   └── test_net.c
├── scripts/                # Утилиты сборки
│   ├── run_qemu.sh        # Запуск эмулятора
│   ├── build_iso.sh       # Сборка ISO
│   └── run_all.sh         # Полный запуск
├── docs/                   # Документация
│   ├── architecture.md
│   ├── boevoy_roadmap_ru.md
│   └── IMPLEMENTATION_PLAN.md
└── CMakeLists.txt         # Конфиг CMake
```

---

## 🔧 Сборка и тестирование

### Сборка компонентов

```bash
# Пересобрать всё
cmake --build build

# Собрать только C-ядро
cmake --build build --target kolibri_core

# Собрать только ISO для QEMU
cmake --build build --target kolibri_iso

# Собрать WASM (браузер)
./scripts/build_wasm.sh
```

### Запуск тестов

```bash
# Все C-тесты
ctest --test-dir build

# Python тесты
pytest -v

# Линтеры
ruff check .
pyright

# Валидация политик
python scripts/policy_validate.py
```

---

## 📝 Структура кода

### Пример: Добавление новой команды в shell

**Файл:** `kernel/main.c`

```c
// 1. Добавьте обработчик в shell_process_command()
static void shell_process_command(const char *cmd) {
    if (k_strcmp(cmd, "help") == 0) {
        vga_pechat_stroku("Available commands: ...\n");
    }
    // ... existing code ...
    else if (k_strcmp(cmd, "mycommand") == 0) {  // ← НОВАЯ КОМАНДА
        vga_pechat_stroku("Executing my command\n");
        // Ваш код здесь
    }
    else {
        vga_pechat_stroku("Unknown command\n");
    }
}
```

### Пример: Добавление теста

**Файл:** `tests/test_myfeature.c`

```c
#include <stdio.h>
#include <assert.h>
#include "kolibri/decimal.h"

void test_my_feature(void) {
    printf("Testing my feature...\n");

    // Arrange
    int expected = 42;

    // Act
    int result = my_function();

    // Assert
    assert(result == expected);
    printf("✓ Test passed\n");
}
```

Добавьте тест в `tests/test_main.c`:
```c
void test_my_feature(void);  // ← ДЕКЛАРАЦИЯ

int main(void) {
    // ... existing tests ...
    test_my_feature();        // ← ВЫЗОВ
    return 0;
}
```

---

## 🚀 Workflow разработки

### 1. Создание ветки для фичи

```bash
git checkout -b feature/my-feature-name
```

### 2. Разработка

```bash
# Отредактируйте файлы
# Добавьте тесты
# Пересоберите

cmake --build build
ctest --test-dir build
```

### 3. Коммит

```bash
# Следуйте стилю коммитов
git add .
git commit -m "feat: add my new feature

- Описание что добавили
- Что изменилось
- Fixes #123 (если исправляет issue)
"
```

### 4. Пушим и создаём PR

```bash
git push origin feature/my-feature-name
# Создайте Pull Request на GitHub
```

### 5. Code Review

- Минимум 1 review от @core-team
- Все тесты должны проходить
- Статический анализ ruff/pyright должен быть clean

---

## 📊 KPIs и метрики

Отслеживайте прогресс на основе IMPLEMENTATION_PLAN.md:

| Компонент | Метрика | Целевое значение |
|-----------|---------|-----------------|
| Decimal | Покрытие UTF-8 | 100% |
| Genome | Скорость записи | <1мс на блок |
| Formula | Скорость мутаций | >100 формул/сек |
| Network | Распространение формул | <10 мин на 90% узлов |
| Tests | Покрытие кода | >80% |

---

## 🐛 Debugging

### Запуск с отладкой

```bash
# Запустить QEMU с отладочным выводом
./scripts/run_qemu.sh --no-gui --serial debug.log

# Читать логи
tail -f debug.log
```

### Проверка серийного порта

```bash
# Сохранить вывод QEMU в файл
qemu-system-i386 -cdrom build/kolibri.iso -serial file:qemu.log

# Прочитать логи
cat qemu.log
```

### Профилирование

```bash
# Собрать с поддержкой профилирования
cmake -B build-perf -DCMAKE_BUILD_TYPE=Release
cmake --build build-perf

# Запустить тесты с профилем
perf record ./build-perf/kolibri_tests
perf report
```

---

## 📚 Полезные ссылки

- **Architecture:** [docs/architecture.md](docs/architecture.md)
- **Roadmap:** [docs/boevoy_roadmap_ru.md](docs/boevoy_roadmap_ru.md)
- **Implementation Plan:** [docs/IMPLEMENTATION_PLAN.md](docs/IMPLEMENTATION_PLAN.md)
- **GitHub Issues:** https://github.com/leontov/kolibri-os/issues

---

## 💬 Общение и вопросы

- **Issues:** Создавайте GitHub issues для багов и фич
- **Discussions:** Вопросы в Discussions
- **Weekly Sync:** Пятница 10:00 UTC (see IMPLEMENTATION_PLAN.md)

---

## 📄 Лицензия

Kolibri OS распространяется под лицензией указанной в файле LICENSE.

---

**Спасибо за вклад в Kolibri OS!** 🚀

Последнее обновление: 11 ноября 2025 г.
