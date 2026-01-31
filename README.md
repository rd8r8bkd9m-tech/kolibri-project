````markdown
# Kolibri OS

![Version](https://img.shields.io/badge/version-1.0.0-blue) ![License](https://img.shields.io/badge/license-MIT-green)

Колибри OS — легковесная экспериментальная платформа, объединяющая KolibriScript, симулятор и набор утилит для отладки цифровых сценариев. Проект включает инновационные архиваторы данных с рекордными показателями сжатия.

## 🎯 Архиваторы Kolibri

### 🥇 Multi-level Archiver - АБСОЛЮТНЫЙ РЕКОРДСМЕН
- **377x** сжатие исходного кода (482 KB → 1.3 KB)
- Многоуровневое иерархическое сжатие (5 уровней)
- Формульная генерация данных
- Восстановление: < 1 миллисекунды
- **См. [kolibri-archiver/](kolibri-archiver/)** - полная документация

### v3.0 RLE Meta - Рекордсмен гомогенных данных
- **57,614x** сжатие гомогенных данных (1MB → 182 байта)
- Лучший для повторяющихся паттернов, логов, баз данных
- Скорость: 306 MB/sec

### v4.0 Adaptive - Умный выбор
- Автоматический выбор режима с энтропийным анализом
- 50,902x на гомогенных данных
- Скорость: 321 MB/sec

### v10.0 Smart - Универсальный
- 4 режима сжатия (RLE/Dictionary/Hybrid/Fallback)
- 2.25x на реальных файлах (vs Bzip2: 5.23x)
- Лучший для повседневного использования

**Быстрый старт с архиваторами:**

```bash
# Multi-level Archiver (377x сжатие!)
cd kolibri-archiver
make demo

# Классические архиваторы
gcc tools/kolibri_archiver_v10.c -o kolibri -O3
./kolibri input.txt output.kolibri compress
./kolibri output.kolibri restored.txt decompress
```

**См. также:**
- [kolibri-archiver/](kolibri-archiver/) - **Самый мощный архиватор! 377x сжатие** 🥇
- [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md) - Структура проекта
- [docs/reports/](docs/reports/) - Результаты тестирования всех архиваторов
- [benchmarks/](benchmarks/) - Сравнение с ZIP, Bzip2, XZ, Zstd
- [docs/analysis/kolibri_ai_masterplan.md](docs/analysis/kolibri_ai_masterplan.md) — план запуска локального Kolibri AI с GPU-ускорением

## 🤖 Kolibri AI (Фаза 1: Завершено)

Колибри теперь обладает способностью к текстовому общению!

### Основные возможности Фазы 1:
- **Численное мышление**: Переход от NLP к цифровым геномам.
- **Интеграция знаний**: Индексация документации, Википедии и словаря Даля.
- **Интерфейс общения**: Усовершенствованный CLI (узел `kolibri_node`) с поддержкой текстовых ответов и генетического синтеза.
- **Масштаб знаний**: Поддержка до 1000 документов одновременно в памяти узла.

**Попробовать сейчас:**
```bash
# Автоматическое обучение на всей базе (Docs, Wikipedia, словарь Даля)
./scripts/auto_train.sh --ticks 500

# Запуск чата (режим :ask)
./build/kolibri_node --genome build/training/auto_genome.dat --bootstrap build/training/bootstrap.ks
```
Внутри узла введите: `:ask АВОСЬ` или `:ask Искусственный интеллект`.
- [engine/gpu_encoder/](engine/gpu_encoder/) — кодер/декодер ReasonBlock на CUDA/Metal
- [scripts/spectral_fingerprint.py](scripts/spectral_fingerprint.py) — спектральный анализ входных знаний

### 📌 Что реально реализовано как AI
- Kolibri OS — экспериментальная платформа с числовыми семантическими паттернами, формулами и индексом знаний.
- Локальные ответы строятся детерминированными алгоритмами (без нейросетевой модели).
- Режим LLM — это прокси к внешнему LLM-сервису; в репозитории нет собственной обученной LLM.

### 🔌 Интеграция больших LLM (внешние модели, включая 450B)
Требование «fork всех доступных моделей и максимум 450B» невозможно выполнить внутри репозитория: веса LLM не включаются в комплект и не форкаются локально. Kolibri поддерживает подключение внешних моделей через прокси-сервис и хранит только результаты в геноме/базе знаний.

**Как подключить внешний 450B-провайдер:**
- Поднимите модель у провайдера или в своем кластере (Inference API).
- Настройте переменные окружения для `backend/service/main.py`:
  ```bash
  export KOLIBRI_RESPONSE_MODE=llm
  export KOLIBRI_LLM_ENDPOINT="https://your-llm-endpoint/v1/chat"
  export KOLIBRI_LLM_MODEL="vendor/model-450b"
  export KOLIBRI_LLM_API_KEY="token"
  ```
- Для «форка» разных моделей запускайте скрипт `scripts/llm_teacher.py` (он запрашивает внешний LLM и обучает Kolibri через teach/feedback), затем фиксируйте ответы в базе знаний. Пример:
  ```bash
  ./scripts/llm_teacher.py "Колибри принципы" --backend http --llm-model vendor/model-450b
  ./scripts/llm_teacher.py "Колибри принципы" --backend http --llm-model vendor/model-70b
  ```

⚠️ Репозиторий не содержит и не распространяет веса LLM; масштабирование до 450B — это внешняя инфраструктурная задача.

## 📦 Kolibri Archiver - Advanced Compression System

Kolibri OS теперь включает мощную систему архивирования с многоуровневым сжатием:

- **Высокие коэффициенты сжатия**: 5-40x в зависимости от типа данных
- **Многослойное сжатие**: Математический анализ + LZ77 + RLE
- **Целостность данных**: Проверка CRC32
- **Поддержка архивов**: Несколько файлов в одном архиве

### Быстрый старт с архиватором

```bash
# Сжать файл
./build/kolibri_archiver compress myfile.txt myfile.klb

# Распаковать файл
./build/kolibri_archiver decompress myfile.klb restored.txt

# Протестировать коэффициент сжатия
./build/kolibri_archiver test myfile.txt
```

Подробная документация: [docs/archiver_ru.md](docs/archiver_ru.md) | [apps/README_ARCHIVER_RU.md](apps/README_ARCHIVER_RU.md) | [ARCHIVER_SUMMARY_RU.md](ARCHIVER_SUMMARY_RU.md)

Документация на английском: [docs/archiver.md](docs/archiver.md) | [apps/README_ARCHIVER.md](apps/README_ARCHIVER.md) | [ARCHIVER_SUMMARY.md](ARCHIVER_SUMMARY.md)

## 🚀 AGI v2.0 Development (In Progress)

Kolibri OS расширяется в направлении полноценной системы искусственного интеллекта, сохраняя уникальный подход "мышления числами". См. [ROADMAP_AGI.md](ROADMAP_AGI.md) для полного плана развития.

### Phase 1: Semantic Foundation (Q1 2026) - ✅ COMPLETE

- ✅ **Semantic Module** - эволюционное обучение семантических паттернов
  - 64-значные числовые представления смыслов слов
  - Популяционная оптимизация (50 индивидов, 1000 поколений)
  - Контекстное окно (до 32 слов)
  - Интеграция с роевым интеллектом
  - **Статус:** Реализован, все тесты проходят

- ✅ **Context Window Module** - контекстное окно с механизмом attention
  - 2048-токенное окно для обработки последовательностей
  - Attention mechanism на основе числового сходства
  - Softmax нормализация весов внимания
  - Top-K извлечение релевантных токенов
  - Sliding window для эффективности памяти
  - Сериализация для роевой сети
  - **Статус:** Реализован, все тесты проходят

- ✅ **Corpus Learning Module** - обучение на текстовых корпусах
  - Токенизация текста (слова, знаки препинания)
  - Динамическое хранилище паттернов
  - Инкрементальное слияние для непрерывного обучения
  - Обработка файлов и директорий
  - Персистентность паттернов (бинарный формат)
  - Статистика обучения (документы, токены, fitness, скорость)
  - **Статус:** Реализован, все тесты проходят

### Запуск AGI модулей
```bash
# Сборка с тестами
cmake -S . -B build -DKOLIBRI_ENABLE_TESTS=ON
cmake --build build

# Запуск тестов семантического кодирования
./build/test_semantic

# Запуск тестов контекстного окна
./build/test_context

# Запуск тестов corpus learning
./build/test_corpus
```

**Следующие шаги:** Text Generation → Reasoning → Multimodal (Q2-Q4 2026)

### Kolibri AI Production Roadmap (2025)
- **Локальная база знаний:** SQLite + FAISS/Metal, управляется сервисом `backend/service/gpu_store.py`.
- **GPU Encoder:** `engine/gpu_encoder` предоставляет CUDA/Metal ядра с CPU-fallback.
- **Spectral Pipeline:** `scripts/knowledge_pipeline.sh` вызывает `spectral_fingerprint.py`, формируя профили для KRHA и GPU-памяти.
- **Документация:** Все детали в `docs/analysis/kolibri_ai_masterplan.md`.

**Проверить GPU-бэкенд:**
```
cmake -S . -B build-gpu -DKOLIBRI_ENABLE_GPU=ON
cmake --build build-gpu --target kolibri_gpu_demo
./build-gpu/kgpu_demo README.md
```

## Требования
- Python 3.10+
- `pip` и `virtualenv`
- Компилятор C/C++ с поддержкой CMake 3.20+
- Ninja либо Make (по желанию)

## Быстрый старт
1. Клонируйте репозиторий и перейдите в директорию проекта.
2. Подготовьте виртуальное окружение Python (поддерживаются версии 3.10+):
   ```bash
   python -m venv .venv
   source .venv/bin/activate  # Windows: .venv\\Scripts\\activate
   python -m pip install --upgrade pip
   ```
3. Установите инструменты, перечисленные в [`requirements.txt`](requirements.txt). Файл включает точные версии `pytest`, `coverage`, `ruff` и `pyright`, которые используются в CI.
   ```bash
   pip install -r requirements.txt
   ```
4. Соберите C-компоненты Kolibri:
   ```bash
   cmake -S . -B build -G "Ninja"  # или опустите -G, чтобы использовать Makefiles
   cmake --build build
   ```

5. Для веб-интерфейса соберите wasm-ядро перед фронтендом:
   ```bash
   ./scripts/build_wasm.sh
   ```
6. Соберите фронтенд (после установки npm-зависимостей в `frontend/`):
   ```bash
   cd frontend
   npm install
   npm run build
   ```
7. Запустите тесты:

   ```bash
   pytest -q
   ruff check .
   pyright
   ctest --test-dir build
   ```

## Проверки качества
- Линтеры Python: `ruff check`, `pyright`
- Политики проекта: `python scripts/policy_validate.py`
- Форматирование C-кода выполняется стандартными средствами компилятора; следуйте существующему стилю файлов в `apps/` и `tests/`.

## Дополнительные ресурсы
- [План релиза](docs/project_plan.md) описывает долгосрочные вехи и критерии готовности.
- Скрипты и утилиты размещены в `scripts/`; каждый скрипт содержит встроенные подсказки по использованию.

## Режимы ответа Kolibri
Kolibri OS поддерживает два режима генерации ответов:

1. **Deterministic KolibriScript** — ответы собираются локально внутри браузера через WebAssembly-мост. Это режим по умолчанию.
2. **LLM Proxy** — запросы проксируются в внешний LLM через FastAPI-сервис.

Чтобы активировать режим LLM, задайте переменные окружения и запустите сервис:

```bash
export KOLIBRI_RESPONSE_MODE=llm
export KOLIBRI_LLM_ENDPOINT="https://llm.example.com/v1/infer"
export KOLIBRI_LLM_API_KEY="<token>"  # опционально
export KOLIBRI_LLM_MODEL="kolibri-pro"  # опционально
./scripts/run_backend.sh --port 8080
```

Фронтенд ожидает те же настройки через Vite:

```bash
export VITE_KOLIBRI_RESPONSE_MODE=llm
export VITE_KOLIBRI_API_BASE="http://localhost:8080/api"
npm --prefix frontend run dev
```

Если `VITE_KOLIBRI_RESPONSE_MODE` не равен `llm`, интерфейс автоматически вернётся к KolibriScript. При ошибке LLM фронтенд повторит запрос через KolibriScript и дополнит ответ примечанием о деградации.

### Локальный форк модели (KolibriScript)
Чтобы "форкнуть" лучшую локальную модель и применить принципы Kolibri, используйте готовый скрипт. Он копирует самый крупный локальный genome (или путь из `KOLIBRI_LOCAL_MODEL_PATH`), добавляет bootstrap с принципами Колибри и запускает `kolibri_node`.

```bash
export KOLIBRI_LOCAL_MODEL_PATH="build/training/auto_genome.dat"  # опционально
./scripts/run_local_model_fork.sh --question "Kolibri принципы"
```

Скрипт сохранит форк в `build/training/local_fork_genome.dat` и bootstrap в `build/training/local_fork_bootstrap.ks`. Если `KOLIBRI_LOCAL_MODEL_PATH` не задан, будет выбран наиболее крупный из `build/training/auto_genome.dat`, `build/knowledge/knowledge_genome.dat`, `.kolibri/knowledge_genome.dat`.
