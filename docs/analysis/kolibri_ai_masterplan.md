# Kolibri Artificial Intelligence Masterplan

**Автор:** Кочуров Владислав Евгеньевич  
**Дата:** 14 ноября 2025 г.

---

## 1. Контекст и уже имеющиеся активы

| Субсистема | Что есть сейчас | Где находится |
|------------|-----------------|---------------|
| **KolibriScript** | Русскоязычный DSL для эволюционного ядра, команды обучения/оценки формул | `docs/kolibri_script.md`, `core/kolibri_script/`, `training/*.ks` |
| **KolibriSim** | C- и Python-симулятор с журналом ReasonBlock и soak-тестами | `core/kolibri_sim.py`, `build/kolibri_sim`, `tests/test_sim.c` |
| **Knowledge Pipeline** | Сбор документов, нормализация, формирование `knowledge_genome.dat` | `scripts/knowledge_pipeline.sh`, `docs/ingested/`, `build/knowledge/` |
| **Auto-train** | Скрипт запуска полного цикла `[ingest → train → soak → summary]` | `scripts/auto_train.sh`, `training/math_dialog.ks` |
| **Digital Genome** | Неизменяемый журнал событий с HMAC | `kernel/`, `docs/genome_chain.md`, `build/training/auto_genome.dat` |
| **Swarm Protocol** | TCP-обмен формулами между узлами | `docs/swarm_protocol.md`, `apps/kolibri_node.c` |
| **KRHA** | Концепция гибридного архиватора с анализом спектров | `KOLIBRI_ARCHIVERS_COMPARISON.md` |

Эти блоки дают всё необходимое для полного цикла ИИ: сбор данных → обучение → исполнение → распространение знаний.

---

## 2. Требования к новому Kolibri AI

1. **Функциональные цели**
   - Автоматически накапливать знания из `docs/`, `data/`, внешних источников.
   - Генерировать и тестировать формулы рассуждения, связывая текстовые запросы с действиями.
   - Отвечать в реальном времени через KolibriScript, CLI и веб-клиенты.
   - Поддерживать гибридную память: генеративные шаблоны + lossless остатки (по идеям KRHA).
   - Держать **локальную оффлайн-базу знаний** (без внешних SaaS) и обеспечивать GPU-ускоренное кодирование/декодирование для всех ReasonBlock и embedding операций.

2. **Нефункциональные критерии**
   - **Детерминизм:** все этапы логируются в genome и воспроизводимы.
   - **Масштабируемость:** разделение на автономные узлы (swarm) и лёгкие воркеры (cloud-storage, backend).
   - **Безопасность:** строгая проверка HMAC, контроль источников данных, sandbox для скриптов.
   - **Валидация:** автоматические soak-тесты (JSONL метрики), ручные acceptance сценарии.
   - **Полная локальность:** продукт собирается и работает в одной машине/кластере без симуляторов и без облачных зависимостей.
   - **GPU-ускорение:** обязательная поддержка CUDA (Linux/Windows) и Metal (macOS) для модулей кодирования, поиска и реконструкции знаний.

3. **Входы/выходы**
   - Вход: Markdown/HTML знания, команды пользователя (CLI/web/mobile), сенсоры симулятора.
   - Выход: ReasonBlocks, ответы KolibriScript, архивы `*.kolibri`, API-ответы.

---

## 3. Архитектурный обзор

```
[Sources] → [Spectral Fingerprint & Sanitizer] → [Knowledge Graph Builder] →
[Resonance Reasoning Core] ↔ [Residual Memory Store] ↔ [Digital Genome]
             ↓                                  ↑
        [KolibriScript Runtime] ←→ [Swarm Fabric] ←→ [Clients (CLI/Web/Mobile)]
```

### 3.1 Spectral Fingerprint & Sanitizer
- Расширяем `scripts/knowledge_pipeline.sh`: добавляем модуль анализа спектров (энтропия, AST, токены) по аналогии с KRHA.
- Реализация: `tools/spectral_fingerprint.py` (Python) + C-хелпер `kernel/spectral.c`.
- Выход: JSON с метаданными блоков (`type`, `entropy`, `checksum`, `recommended_path`).

### 3.2 Knowledge Graph Builder
- Новая служба `backend/service/knowledge_builder.py` собирает ReasonBlocks `KNOWLEDGE_DOC` и строит граф из тем/ссылок.
- Хранение в `build/knowledge/index.json` + лёгкая SQLite (`build/knowledge/knowledge.db`).
- API: gRPC/REST ручки `POST /knowledge/doc`, `GET /knowledge/context`. Используем существующий FastAPI backend.

### 3.3 Resonance Reasoning Core
- Расширяем `kernel/ai_*`:
  - `ai_encoder.c` уже оптимизирует десятичный код → добавляем `ai_resonance.c`, который совмещает генеративные формулы и residual-дельты.
  - Вводим два пула формул: `generative_pool` и `residual_pool`; `kolibri_node` объединяет результаты (операция Δ-орchestration).
- Fitness учитывает согласованность ответов с цифровым архивом + пользовательскими feedback блоками (`feedback_service`).

### 3.4 Residual Memory Store
- Используем KRHA-принципы: создаём сервис `apps/kolibri_memory.c`, сохраняющий lossless-остатки по блокам 4 KB с zstd.
- Для каждого ответа Kolibri хранит «формула → residual map»; при несовпадении генерации восстанавливаем точный ответ.

### 3.5 Interfaces & Orchestration
- **KolibriScript Runtime:** добавляем системные команды `подключить знание <module>` и `сохранить геном snapshot` (изменяем `core/kolibri_script/runtime.c`).
- **Swarm Fabric:** новые типы сообщений `TYPE=4 (DELTA_PATCH)` и `TYPE=5 (KNOWLEDGE_ACK)` в `docs/swarm_protocol.md` + `apps/kolibri_node.c`.
- **Clients:**
  - CLI (`kolibri.sh`, `apps/kolibri_sim_cli.c`) получает команды `ai ask` / `ai teach`.
  - Web (`frontend/kolibri-web`) добавляет панель «Kolibri Intelligence» с live-метриками.
  - Mobile (`mobile/kolibri-app`) — минимальный чат-интерфейс.

### 3.6 On-device GPU Encoding Engine
- Компонент `engine/gpu_encoder` реализуется в двух вариантах: `gpu_encoder.cu` (CUDA) и `gpu_encoder_metal.mm` (Metal), собирается через CMake preset `gpu`.
- API (`kolibri_gpu_encoder.h`): `kgpu_encode_reason_blocks`, `kgpu_decode_responses`, `kgpu_embed_tokens`. Все функции принимают батчи ReasonBlock и используют warp-параллелизм.
- Интеграция: backend вызывает GPU-ядра через `pybind11`-модуль `kolibri_gpu`, KolibriScript runtime использует C-интерфейс напрямую.
- Прямое подключение к локальной базе знаний: каждая запись содержит ссылку на GPU-набор embedding, что позволяет мгновенно возвращать результаты без эмуляторов.

---

## 4. Потоки данных

1. **Ingest**
   - `scripts/kolibri_fetch_docs.py` → `docs/ingested/…`
   - `spectral_fingerprint.py` присваивает каждому документу профиль (тип, энтропия, сигнатура).
   - `knowledge_builder` превращает документы в ReasonBlocks + граф.

2. **Training Loop**
   - `scripts/auto_train.sh --module <m>` вызывает `kolibri_node` с bootstrap.
   - `resonance_core` синтезирует формулы, residual store фиксирует отклонения.
   - Genome обновляется событиями `FORMULA_EVOLVED`, `GEN_RESIDUAL`, `USER_FEEDBACK`.

3. **Serving**
   - Пользователь → CLI/Web/mobile → REST (`backend/service/src`) → KolibriScript runtime.
   - Runtime запрашивает `knowledge_builder` контекст, `resonance_core` вычисляет ответ.
   - Ответ + контрольные суммы публикуются в genome и (опционально) KRHA-архив.

4. **Swarm Sharing**
   - Периодически `kolibri_node` передаёт лучшие формулы + residual пачки через новые типы сообщений.
   - Узлы проверяют HMAC, помещают формулы в свои пулы и синхронизируют knowledge graph.

---

## 5. Реализация по фазам

| Фаза | Период | Ключевые задачи | Директории |
|------|--------|-----------------|------------|
| **F1. Основа (2 недели)** | Настроить окружение, обновить `README`, убедиться в прохождении `pytest/ctest`. Добавить в `PROJECT_STRUCTURE.md` раздел об ИИ. | корень, `docs/` |
| **F2. Spectral + Knowledge (3 недели)** | Реализовать `spectral_fingerprint.py`, интегрировать в pipeline, добавить SQLite-граф. | `scripts/`, `backend/service/` |
| **F3. Resonance Core (4 недели)** | Написать `kernel/ai_resonance.c`, расширить `apps/kolibri_node.c`, добавить residual-хранилище (`apps/kolibri_memory.c`). | `kernel/`, `apps/`, `kolibri-archiver/` |
| **F4. GPU Knowledge Base (3 недели)** | Реализовать `engine/gpu_encoder` (CUDA/Metal), создать локальную базу `build/knowledge/kolibri.db` с GPU-векторным индексом (FAISS/cuVS/Metal Performance Shaders), связать с backend API. | `engine/`, `backend/service/`, `build/knowledge/` |
| **F5. Interfaces & Swarm (3 недели)** | Обновить KolibriScript команды, протокол роя, CLI/Web/mobile UI. | `core/`, `apps/`, `frontend/`, `mobile/` |
| **F6. Autonomy & QA (2 недели)** | Автономный запуск `run_autonomy.sh`, soak-метрики, KRHA-бэкапы знаний. | `scripts/`, `test_results/`, `docs/reports/` |

---

## 6. Проверки и метрики

- **Unit/Integration**: `pytest`, `ctest`, новые тесты `tests/test_ai_resonance.c`, `tests/test_knowledge_builder.py`.
- **Soak**: `build/kolibri_sim soak --minutes 5` с анализом JSONL (доля fallback < 5%, успешные математические ответы > 95%).
- **Knowledge Integrity**: `./build/kolibri_node --verify-genome`, `sqlite3 build/knowledge/knowledge.db "PRAGMA integrity_check;"`.
- **Swarm Consistency**: сравнение хэшей формул между узлами.

---

## 7. План развития после MVP

1. **Multimodal**: добавить ingest изображений/аудио → преобразование в описательные ReasonBlocks.
2. **KRHA-сжатие знаний**: хранить граф и геном в KRHAv1-архивах для сверхбыстрых снапшотов.
3. **Visual Cortex**: Web-интерфейс показывает спектр знаний и активные формулы.
4. **Defense Mode**: автоматическое обнаружение токсичных источников и карантин.

---

## 8. Следующие конкретные шаги

1. Обновить `PROJECT_STRUCTURE.md`, чтобы зафиксировать наличие нового AI-пакета.
2. Создать скелет `scripts/spectral_fingerprint.py` и прописать вызов в `knowledge_pipeline.sh`.
3. Определить схему SQLite-графа (`docs/analysis/knowledge_schema.sql`).
4. Подготовить `tests/test_ai_resonance.c` с мок-пулами и заглушками residual storage.
5. Спроектировать локальную GPU-базу: создать `engine/gpu_encoder/` (CUDA/Metal), `backend/service/gpu_store.py`, `build/knowledge/kolibri.db`.
6. Запланировать KRHA-бэкап для первого обученного генома (`test_results/knowledge_snapshot.krha`).

---

## 9. Локальная база знаний с GPU-ускорением

1. **Хранилище**
   - Используем SQLite + FTS5 для текстовых метаданных (`build/knowledge/kolibri.db`).
   - Векторный слой: `faiss-gpu` (CUDA) или `MetalPerformanceShadersGraph` (macOS) с хранением embedding в `knowledge_vectors.bin`.
   - Каждая запись содержит: `doc_id`, `reason_block_hash`, `spectral_profile`, `embedding_ptr`, `residual_ref`.

2. **GPU-кодер/декодер**
   - CUDA ядра (`engine/gpu_encoder/gpu_encoder.cu`) реализуют батчевое преобразование ReasonBlock → embedding, обратное декодирование и KRHA-остатки.
   - Metal-ядра (`engine/gpu_encoder/gpu_encoder.metal`) позволяют запускать те же алгоритмы на Apple Silicon.
   - Общий API экспортируется через `kolibri_gpu_encoder.h` и Python-биндинг `kolibri_gpu` (pybind11), чтобы backend и скрипты могли обращаться к тем же функциям.

3. **Интеграция в pipeline**
   - `scripts/knowledge_pipeline.sh` после нормализации вызывает `kolibri_gpu_embed --input build/knowledge/index.json --db build/knowledge/kolibri.db`.
   - Backend (`backend/service/gpu_store.py`) предоставляет REST/GRPC-ручки `POST /gpu/store`, `POST /gpu/search`, `POST /gpu/decode`.
   - KolibriScript команда `подключить знание` автоматически создаёт GPU-embedding, если запись отсутствует.

4. **Эксплуатация (реальный продукт)**
   - Пресеты сборки: `cmake -S . -B build-gpu -DKOLIBRI_ENABLE_GPU=ON -DKOLIBRI_GPU_BACKEND=cuda|metal`.
   - Пакет `kolibri-node.service` запускает `kolibri_node`, `gpu_store`, `knowledge_builder`, `frontend` на одном узле; никакие облачные сервисы не требуются.
   - Проверки: `./build-gpu/tests/test_gpu_encoder`, `pytest backend/service/tests/test_gpu_store.py`, `./build-gpu/kolibri_sim soak --minutes 10`.

5. **Аппаратные требования**
   - Минимум: 16 ГБ RAM, GPU с 8 ГБ VRAM (RTX 3060 / Apple M3 Max / AMD RX 6800).
   - Все зависимости локально поставляются через `install.sh` (добавляем установку CUDA toolkit либо Metal headers).

> Таким образом Kolibri AI превращается в полностью офлайн-продукт с локальной базой и аппаратным ускорением, без симуляторов и без внешних API.

---

## 10. Статус реализации Metal backend (14 ноября 2025)
- **Собрано:** `kolibri_gpu_encoder` (диспетчер бэкендов), `gpu_encoder_metal.mm` (Metal-шейдеры для ReasonBlock → embedding), `tools/kgpu_demo` (CLI-проверка), `knowledge_pipeline` (spectral fingerprint), `gpu_store` (локальный REST-слой).
- **Поддерживаемые вычисления:** среднее, дисперсия, динамика переходов и диапазон значений для каждого ReasonBlock — рассчитываются напрямую в GPU-шейдере и совпадают с CPU fallback.
- **Платформы:** macOS (Metal) с автоматическим откатом к CPU на других системах; CUDA-заготовка готова к наполнению.
- **Дальнейшие шаги:** подключить `gpu_store` к Metal API через pybind11, добавить KRHA residual decode и тесты `tests/test_gpu_encoder.c`.

> Этот мастер-план описывает полный цикл создания искусственного интеллекта Kolibri: от ingestion и эволюционной логики до интерфейсов и проверок. Реализация распределена по существующим каталогам, чтобы сразу встроиться в инфраструктуру Kolibri OS.
