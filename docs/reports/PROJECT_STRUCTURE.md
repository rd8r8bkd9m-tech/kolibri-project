# Структура проекта Kolibri OS

## Корневая директория

```
kolibri-project/
│
├── README.md                   # Главная документация проекта
├── LICENSE                     # MIT лицензия
├── CONTRIBUTING.md             # Гайд для контрибьюторов
├── CHANGELOG.md                # История изменений
├── CMakeLists.txt              # Главный CMake (C23, 17+ целей сборки)
├── Makefile                    # Make-обёртка (build, test, wasm, frontend, ci)
├── requirements.txt            # Python-зависимости
├── .gitignore                  # Исключения Git
│
├── backend/                    # === BACKEND ПЛАТФОРМА ===
├── frontend/                   # === FRONTEND (React) ===
├── apps/                       # === CLI-ПРИЛОЖЕНИЯ (C) ===
├── kernel/                     # === МИКРОЯДРО ОС ===
├── engine/                     # === GPU-КОДЕР ===
├── kolibri-archiver/           # === STANDALONE АРХИВАТОР ===
├── scripts/                    # === СКРИПТЫ (65+) ===
├── tests/                      # === ТЕСТЫ (80+) ===
├── docs/                       # === ДОКУМЕНТАЦИЯ (140+) ===
├── deploy/                     # === ДЕПЛОЙ ===
├── benchmarks/                 # === БЕНЧМАРКИ ===
├── tools/                      # === УТИЛИТЫ (100+) ===
├── content_factory_mvp/        # === КОНТЕНТ-ФАБРИКА ===
├── training/                   # === ДАННЫЕ ОБУЧЕНИЯ ===
├── data/                       # === ДАННЫЕ (модели, корпуса) ===
├── seeds/                      # === SEED-ФАЙЛЫ ===
├── swarm/                      # === КОНФИГ РОЕВ ===
├── modules/                    # === МОДУЛИ РАСШИРЕНИЙ ===
├── sdk/                        # === SDK ===
├── mobile/                     # === МОБИЛЬНОЕ ПРИЛОЖЕНИЕ ===
├── web-app/                    # === ВЕБ-ПРИЛОЖЕНИЕ ===
├── cloud-storage/              # === ОБЛАЧНОЕ ХРАНИЛИЩЕ ===
├── release/                    # === РЕЛИЗНЫЕ АРТЕФАКТЫ ===
├── demos/                      # === ДЕМОНСТРАЦИИ ===
├── logs/                       # === ЛОГИ ===
├── build/                      # === СБОРКА (cmake) ===
├── boot/                       # === ЗАГРУЗЧИК ОС ===
└── core/                       # === CORE УТИЛИТЫ ===
```

---

## Backend (`backend/`)

```
backend/
├── __init__.py
├── Dockerfile
│
├── include/kolibri/            # 27 публичных C-заголовков
│   ├── genome.h                # ReasonBlock, WAL, блокчейн-цепочка
│   ├── formula.h               # KolibriGene (4000 цифр), FormulaPool, мутации
│   ├── knowledge.h             # Граф знаний
│   ├── knowledge_index.h       # Индексация знаний
│   ├── knowledge_queue.h       # Очередь обучения
│   ├── net.h                   # P2P-протокол (TCP, HMAC, 4200 payload)
│   ├── script.h                # KolibriScript интерпретатор
│   ├── symbol_table.h          # Таблица символов
│   ├── decimal.h               # Десятичная арифметика
│   ├── digits.h                # Цифровые операции
│   ├── digit_text.h            # Текст ↔ цифры
│   ├── compress.h              # Компрессия (RLE/dict/hybrid)
│   ├── random.h                # ГСЧ (LCG)
│   ├── semantic.h              # Семантические паттерны
│   ├── phoneme.h               # Фонетические паттерны
│   ├── context.h               # Контекстное окно
│   ├── corpus.h                # Корпусное обучение
│   ├── corpus_trainer.h        # Массовый тренер
│   ├── generation.h            # Генерация текста
│   ├── inference.h             # Инференс
│   ├── formula_logic.h         # Мета-формулы
│   ├── logical_memory.h        # Логическая память
│   ├── async_executor.h        # Асинхронный исполнитель
│   ├── trace.h                 # Трассировка
│   ├── sim.h                   # Симулятор
│   ├── roy.h                   # Рой
│   └── web_crawler.h           # Веб-краулер
│
├── src/                        # 35 C-модулей ядра
│   ├── genome.c                # ReasonBlock цепочка, HMAC-SHA256, WAL
│   ├── formula.c               # ResNet 500 слоёв, 12 операций, эволюция
│   ├── knowledge.c             # Граф знаний (паттерны + рёбра)
│   ├── knowledge_index.c       # Индексирование
│   ├── knowledge_queue.c       # Очередь обучения
│   ├── net.c                   # P2P (TCP, HMAC, payload 4200B)
│   ├── script.c                # KolibriScript интерпретатор
│   ├── symbol_table.c          # Таблица символов
│   ├── decimal.c               # Десятичная арифметика
│   ├── digits.c                # Цифровые операции
│   ├── digit_text.c            # Текст ↔ цифры
│   ├── compress.c              # RLE + Dictionary + Hybrid
│   ├── random.c                # ГСЧ
│   ├── semantic_digits.c       # Семантические паттерны
│   ├── phoneme.c               # Фонетика
│   ├── context_window.c        # Контекстное окно
│   ├── corpus_learning.c       # Обучение на корпусах
│   ├── corpus_trainer.c        # Массовый тренер (файлы/директории)
│   ├── text_generation.c       # Генерация текста
│   ├── inference.c             # Инференс
│   ├── formula_logic.c         # Мета-формулы
│   ├── logical_memory.c        # Логическая память
│   ├── async_executor.c        # Асинхронный исполнитель
│   ├── trace.c                 # «Стеклянный Разум» трассировка
│   ├── sim.c                   # Симулятор
│   ├── roy.c                   # Рой
│   ├── web_crawler.c           # Краулер на C
│   └── wasm_bridge.c           # WASM-мост
│
├── service/                    # FastAPI backend (14 модулей)
│   ├── main.py                 # Точка входа, подключение роутеров
│   ├── common.py               # Settings, InferenceRequest, LLM-прокси
│   ├── number_mind.py          # Ядро числового мышления (1335 строк)
│   ├── ai_chat.py              # AI Chat роутер (/api/v1/ai/*)
│   ├── ai_engine.py            # Singleton AI-движок
│   ├── agent.py                # Автономный агент (/api/v1/agent/*)
│   ├── crawler.py              # Веб-краулер (/api/v1/crawl)
│   ├── search_engine.py        # DDG/Bing/Wiki поиск
│   ├── swarm_sync.py           # P2P рой (/api/v1/swarm/*)
│   ├── gpu_store.py            # GPU-хранилище (/api/gpu/*)
│   ├── content_factory.py      # Контент-фабрика (/api/factory/*)
│   ├── os_bridge.py            # ОС-мост (/api/dev/*)
│   ├── knowledge_base.py       # Построение базы знаний
│   ├── README.md               # Документация backend
│   └── __init__.py
│
├── feedback_service/           # RLHF Feedback Pipeline
│   ├── main.py                 # FastAPI сервис
│   ├── database.py             # SQLite/PostgreSQL
│   ├── repository.py           # Репозиторий
│   ├── schemas.py              # Pydantic-модели
│   └── rlhf_dataset.py         # Экспорт в RLHF-формат
│
└── python/                     # Python-обёртки
    ├── kolibri_compress.py     # Обёртка C-компрессии
    └── universal_parser.py     # Универсальный парсер
```

---

## Frontend (`frontend/`)

```
frontend/
├── package.json                # React 18 + Vite 5 + TailwindCSS
├── tsconfig.json               # TypeScript конфигурация
├── vite.config.ts              # Vite конфигурация
├── tailwind.config.js          # TailwindCSS
├── postcss.config.js           # PostCSS
├── README.md                   # Документация frontend
│
├── public/
│   └── kolibri.wasm            # WASM-бинарник (< 60MB)
│
└── src/
    ├── App.tsx                 # Корень → <ManusAppUnified />
    ├── main.tsx                # ReactDOM.createRoot
    ├── DesktopOS.tsx           # Десктопный UI
    ├── DevDashboard.tsx        # Панель разработчика
    │
    ├── manus/                  # Unified Manus UI (основной)
    │   ├── ManusAppUnified.tsx
    │   ├── ManusChat.tsx
    │   ├── ManusHeader.tsx
    │   ├── ManusInputBar.tsx
    │   ├── ManusLayout.tsx
    │   ├── ManusTaskPanel.tsx
    │   ├── ManusWelcome.tsx
    │   ├── ThemeContext.tsx     # Система тем
    │   ├── useManusAgent.ts    # Хук агента
    │   └── tabs/
    │       ├── ChatTab.tsx
    │       ├── CrawlerTab.tsx
    │       ├── KnowledgeTab.tsx
    │       ├── SettingsTab.tsx
    │       ├── TasksTab.tsx
    │       └── TerminalTab.tsx
    │
    ├── components/             # 14 базовых компонентов
    │   ├── AppShell.tsx
    │   ├── Layout.tsx
    │   ├── TopBar.tsx
    │   ├── Sidebar.tsx
    │   ├── NavigationRail.tsx
    │   ├── NavItem.tsx
    │   ├── ChatView.tsx
    │   ├── ChatInput.tsx
    │   ├── ChatMessage.tsx
    │   ├── SuggestionCard.tsx
    │   ├── InspectorPanel.tsx
    │   ├── StatusBar.tsx
    │   ├── FeedbackForm.tsx
    │   └── WelcomeScreen.tsx
    │
    ├── core/                   # Бизнес-логика
    │   ├── kolibri-bridge.ts   # WASM-мост (508 строк)
    │   ├── api.ts              # HTTP API клиент
    │   ├── knowledge.ts        # Граф знаний
    │   ├── modes.ts            # Режимы работы
    │   └── useKolibriChat.ts   # React-хук чата
    │
    ├── types/                  # TypeScript-типы
    │   ├── chat.ts
    │   ├── feedback.ts
    │   └── knowledge.ts
    │
    ├── styles/                 # CSS стили
    └── test/                   # Тесты (Vitest)
```

---

## CLI-приложения (`apps/`)

```
apps/
├── kolibri_node.c              # Главный AI-узел (1197 строк)
├── kolibri_archiver.c          # CLI-архиватор (515 строк)
├── kolibri_coordinator.c       # Координатор роя
├── kolibri_bulk_teach.c        # Массовое обучение
├── kolibri_fast_parser.c       # Быстрый парсер
├── kolibri_gen.c               # Генератор
├── kolibri_indexer.c           # Индексатор
├── kolibri_ingest.c            # Загрузка данных
├── kolibri_inspect.c           # Инспектор
├── kolibri_knowledge_relay.c   # Реле знаний
├── kolibri_learn.c             # Обучение
├── kolibri_queue.c             # Очередь
├── kolibri_sim_cli.c           # Симулятор CLI
├── ks_compiler.c               # Компилятор KolibriScript
├── proxy_server.py             # Python-прокси
├── kolibri_chat_app.html       # HTML чат
├── README_ARCHIVER.md          # Документация архиватора (EN)
├── README_ARCHIVER_RU.md       # Документация архиватора (RU)
└── Dockerfile.indexer          # Docker для индексатора
```

---

## Микроядро ОС (`kernel/`)

```
kernel/
├── main.c                      # Инициализация ядра
├── entry.asm                   # Точка входа (GRUB multiboot, x86)
├── interrupts.asm              # Обработчики прерываний
├── link.ld                     # Linker script
├── support.c / support.h       # Поддержка ОС (VGA, память)
├── serial.c / serial.h         # Serial I/O (COM1)
├── ramdisk.c / ramdisk.h       # RAM-диск
├── formula.c                   # Формульный ResNet-движок
├── genome.c                    # Геномная логика
├── net.c                       # Сетевой слой
├── random.c                    # ГСЧ
├── ai_encoder.c / .h           # AI-кодер
├── ai_evolution.c / .h         # AI-эволюция
├── ai_resonance.c / .h         # AI-резонанс
└── kolibri/                    # Заголовки ядра
    ├── formula.h               # digits[4000]
    ├── genome.h
    ├── net.h
    ├── random.h
    ├── ai_encoder.h
    ├── ai_evolution.h
    └── ai_resonance.h
```

---

## Скрипты (`scripts/`)

```
scripts/                        # 65+ скриптов
├── build_wasm.sh               # Сборка WASM
├── build_iso.sh                # Сборка ISO
├── auto_train.sh               # Автообучение
├── run_kolibri_stack.sh        # Запуск стека
├── run_swarm_*.sh              # Запуск роёв (10/50/1000/100k)
├── llm_teacher.py              # LLM-дистилляция
├── kolibri_crawler.py          # Веб-краулер
├── spectral_fingerprint.py     # Спектральный анализ
├── policy_validate.py          # CI-валидация
├── deploy_*.sh                 # Деплой (linux/macos)
└── ...
```

---

## Тесты (`tests/`)

```
tests/                          # 80+ тестов
├── test_runner.c               # C-тест раннер
├── test_decimal.c              # Десятичная арифметика
├── test_genome.c               # Геномная цепочка
├── test_formula.c              # Формульный движок
├── test_knowledge.c            # Граф знаний
├── test_script.c               # KolibriScript
├── test_net.c                  # P2P-протокол
├── test_sim.c                  # Симулятор
├── test_semantic.c             # Семантика
├── test_phoneme.c              # Фонетика
├── test_context.c              # Контекст
├── test_corpus.c               # Корпусное обучение
├── test_generation.c           # Генерация текста
├── test_compress.c             # Компрессия
├── test_mega_compression.c     # Мега-компрессия
├── test_extreme_compression.c  # Экстремальная компрессия
├── test_backend_service.py     # Python тесты backend
├── test_kolibri_sim.py         # Python тесты симулятора
├── wasi_smoke.ts               # WASI smoke-тест
├── test_data/                  # Тестовые данные
└── benchmarks/                 # Бенчмарк-тесты
```

---

## Документация (`docs/`)

```
docs/                           # 140+ документов
├── ARCHITECTURE.md             # Полная архитектура системы
├── API_REFERENCE.md            # Справочник REST API
├── DEPLOYMENT.md               # Деплой и скрипты
├── developer_guide.md          # Гайд разработчика
├── user_guide.md               # Руководство пользователя
├── admin_guide.md              # Руководство администратора
├── kolibri_script.md           # Язык KolibriScript
├── swarm_protocol.md           # P2P-протокол
├── formula_evolution.md        # Эволюция формул
├── genome_chain.md             # Геномная цепочка
├── security_policy.md          # Безопасность
├── adr/                        # Architecture Decision Records
├── analysis/                   # Технические анализы (15+)
├── guides/                     # Руководства (20+)
├── plans/                      # Планы развития (10+)
├── reports/                    # Отчёты тестирования (94+)
├── legal/                      # Лицензии (5)
└── marketing/                  # Маркетинг (3)
```

---

## Деплой (`deploy/`)

```
deploy/
├── README.md
├── docker-compose.yml          # Docker Compose
├── k8s/                        # Kubernetes
│   ├── namespace.yaml
│   ├── backend.yaml
│   ├── frontend.yaml
│   └── training-cronjob.yaml
├── monitoring/
│   ├── prometheus.yml
│   └── grafana_dashboard.json
└── release-manifests/
    └── v0.1.0/
```

---

## Прочие директории

| Директория | Описание |
|-----------|----------|
| `engine/` | GPU-кодер: `kolibri_gpu_encoder.c/h`, CUDA-заготовка, Metal (macOS), CPU-stub |
| `kolibri-archiver/` | Standalone архиватор с Makefile, тестами, документацией |
| `tools/` | 100+ утилит: архиваторы v3–v40, GPU-инструменты, neuroscope |
| `benchmarks/` | Бенчмарки сжатия vs ZIP/Bzip2/XZ/Zstd |
| `content_factory_mvp/` | Контент-фабрика: backend, worker, UI, Docker Compose |
| `training/` | Данные обучения, геномы, bootstrap-файлы |
| `data/` | Модели (.klm), корпуса, seed-данные |
| `swarm/` | Конфигурации роёв |
| `boot/` | Загрузчик ОС |
| `core/` | Core-утилиты |

---

## Статистика

| Метрика | Значение |
|---------|----------|
| C-модулей (backend/src) | 35 |
| Публичных заголовков | 27 |
| Python-модулей (service) | 14 |
| React-компонентов | 30+ |
| CLI-приложений | 17+ |
| Скриптов | 65+ |
| Тестов | 80+ |
| Документов | 140+ |
| Утилит | 100+ |
| Целей CMake | 17+ |
| Целей тестов CTest | 30+ |
