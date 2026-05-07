# QWEN.md — Контекст проекта Kolibri OS

## Обзор проекта
Kolibri OS — это гибридная ИИ-платформа с C-ядром, моделью знаний KLM и swarm-архитектурой. Проект включает в себя микроядро ОС, backend на FastAPI/C, frontend на React/TypeScript и набор CLI-утилит.

**Ключевые особенности:**
*   **C-ядро:** Реализует геномную цепочку (`genome.c`), формульный ResNet-движок (`formula.c`) и граф знаний.
*   **Swarm-архитектура:** P2P-протокол для синхронизации узлов и распределенного обучения.
*   **Гибридный AI:** Сочетание символьных вычислений (KolibriScript) и нейросетевых подходов.
*   **WASM-интеграция:** Возможность запуска ядра в браузере через WebAssembly.

## Технологический стек
*   **Backend:** Python 3.14, FastAPI, Uvicorn, C (C23).
*   **Frontend:** React 18, TypeScript, Vite, TailwindCSS.
*   **Сборка:** CMake, Ninja, Make.
*   **ОС:** Микроядро (x86, GRUB multiboot).

## Основные команды

### Сборка и запуск
```bash
# Сборка C-ядра и бинариев
cmake -S . -B build -G Ninja && cmake --build build

# Запуск backend (FastAPI)
cd backend && uvicorn service.main:app --reload

# Запуск frontend (React)
cd frontend && npm run dev

# Сборка WASM-модуля
./scripts/build_wasm.sh
```

### Тестирование
```bash
# Полный прогон тестов (C + Python + Frontend)
make test

# Только Python-тесты
pytest -q

# Только C-тесты
ctest --test-dir build
```

## Структура репозитория
*   `backend/` — C-исходники ядра (`src/`), FastAPI сервисы (`service/`) и feedback-пайплайн.
*   `frontend/` — UI приложения "Manus" и WASM-мост.
*   `kernel/` — Исходники микроядра ОС (Assembler + C).
*   `apps/` — CLI-утилиты (узлы роя, архиваторы, симуляторы).
*   `docs/` — Архитектура, API Reference и гайды.

## Соглашения по разработке
*   **C-код:** Следует стандарту C23. Используйте существующие заголовки из `backend/include/kolibri/`.
*   **Python:** Используется type hinting. Стиль проверяется через `ruff` и `pyright`.
*   **Frontend:** Строгая типизация TypeScript. Компоненты оформляются в виде функций с хуками.
*   **Коммиты:** Пишите понятные сообщения, описывающие "почему" было внесено изменение.
