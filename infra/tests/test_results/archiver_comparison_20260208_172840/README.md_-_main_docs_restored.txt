# 🐦 Kolibri OS

![Version](https://img.shields.io/badge/version-2.0.0-blue)
![License](https://img.shields.io/badge/license-MIT-green)
![C Standard](https://img.shields.io/badge/C-C23-orange)
![Python](https://img.shields.io/badge/Python-3.10+-brightgreen)
![React](https://img.shields.io/badge/React-18-61dafb)

> **Гибридная C23 / Python / WASM платформа с числовым формульным мышлением, эволюционным сжатием и децентрализованным обменом знаниями.**

---

## 📋 Содержание

- [Обзор](#-обзор)
- [Архитектура](#-архитектура)
- [Быстрый старт](#-быстрый-старт)
- [Числовое Формульное Мышление](#-числовое-формульное-мышление)
- [ResNet-архитектура (500 слоёв)](#-resnet-архитектура-500-слоёв)
- [API Reference](#-api-reference)
- [Архиваторы](#-архиваторы)
- [CLI-приложения](#-cli-приложения)
- [Frontend](#-frontend)
- [Сборка и тестирование](#-сборка-и-тестирование)
- [Деплой](#-деплой)
- [Структура проекта](#-структура-проекта)
- [Лицензия](#-лицензия)

---

## 🔍 Обзор

**Kolibri OS** — экспериментальная платформа, объединяющая:

| Компонент | Технология | Описание |
|-----------|-----------|----------|
| **C-ядро** | C23 (35 модулей) | Геном, формулы, граф знаний, сеть, компрессия |
| **Backend** | Python 3.10+ / FastAPI | REST API, AI-чат, автономный агент, P2P-рой |
| **Frontend** | React 18 / TypeScript / Vite | WASM-мост, чат-интерфейс, DevDashboard |
| **Kernel** | C + ASM (x86) | Микроядро ОС с AI-компонентами |
| **Архиватор** | C23 | Рекордное сжатие до 377x (исходный код) |
| **Рой** | P2P протокол | Децентрализованная синхронизация знаний |

### Ключевая идея

Все знания представлены в **числовой форме**:
- Каждое слово → **64-цифровой паттерн** (DJB2 + LCG каскад)
- Связи между словами → **взвешенный граф** (co-occurrence)
- «Мозг» системы → **4000-цифровой геном** (500-слойная формульная сеть)
- Обучение → **генетический алгоритм** (мутации, кроссовер, отбор)

---

## 🏗 Архитектура

```
┌──────────────────────────────────────────────────┐
│                   Frontend                        │
│        React 18 + TypeScript + Vite               │
│   ┌──────────┐  ┌───────────┐  ┌──────────────┐  │
│   │ ManusApp │  │ ChatView  │  │ DevDashboard │  │
│   └────┬─────┘  └─────┬─────┘  └──────┬───────┘  │
│        └──────┬───────┘               │           │
│         WASM Bridge / HTTP API        │           │
└────────────────┬──────────────────────┘           │
                 │                                   │
┌────────────────▼──────────────────────────────────┐
│                  Backend (FastAPI)                  │
│  Port 8001 ── uvicorn ── Python 3.10+              │
│  ┌─────────┐ ┌──────────┐ ┌───────────┐           │
│  │ AI Chat │ │  Agent   │ │   Swarm   │           │
│  │/api/v1/ │ │/api/v1/  │ │/api/v1/   │           │
│  │  ai/*   │ │ agent/*  │ │ swarm/*   │           │
│  └────┬────┘ └────┬─────┘ └─────┬─────┘           │
│       └─────┬─────┘             │                  │
│      NumberMind Engine          │                  │
│  ┌─────────────────────────┐    │                  │
│  │ FormulaPool (4000 цифр) │    │                  │
│  │ KnowledgeGraph (паттерн)│    │                  │
│  │ SentenceStore (TF-IDF)  │    │                  │
│  │ ResNet 500 слоёв        │    │                  │
│  └──────────┬──────────────┘    │                  │
│             │ subprocess        │                  │
└─────────────┼───────────────────┘                  │
              ▼                                      │
┌──────────────────────────────────────┐             │
│          C-ядро (libkolibri_core)     │             │
│  35 модулей · C23 · CMake + Ninja     │             │
│  genome.c  formula.c  knowledge.c     │             │
│  compress.c  net.c  script.c          │             │
│  corpus_trainer.c  inference.c        │             │
└──────────────────────────────────────┘             │
```

### Потоки данных

1. **Обучение**: Текст → токенизация → числовые паттерны (DJB2) → граф знаний + SentenceStore → эволюция формул
2. **Ответ на вопрос**: Запрос → паттерн → поиск в графе → формульный predict → сборка ответа
3. **Автономный агент**: Тема → DDG/Bing/Wiki → краулинг → текст → C Mass Trainer + Python train
4. **P2P-рой**: Узел → регистрация → обмен паттернами и рёбрами → слияние → SHA-256 контроль

---

## 🚀 Быстрый старт

### Требования

- **ОС**: Linux (Ubuntu 22.04+), macOS, Windows (WSL2)
- **Компилятор**: GCC 13+ или Clang 16+ (C23)
- **CMake** ≥ 3.16, **Ninja**
- **Python** ≥ 3.10
- **Node.js** ≥ 18
- **OpenSSL** (для HMAC/SHA)

### Установка и запуск

```bash
# 1. Клонировать
git clone https://github.com/rd8r8bkd9m-tech/kolibri-project.git
cd kolibri-project

# 2. Собрать C-ядро
cmake -S . -B build -G Ninja
cmake --build build

# 3. Установить Python-зависимости
pip install -r requirements.txt

# 4. Запустить backend (порт 8001)
python -m uvicorn backend.service.main:app --host 0.0.0.0 --port 8001

# 5. Запустить frontend (порт 3000)
cd frontend && npm install && npx vite --host 0.0.0.0 --port 3000
```

### Однострочный запуск

```bash
# Полная сборка и тест
make build && make test

# CI-конвейер (build + test + frontend + iso + policy)
make ci
```

---

## 🧠 Числовое Формульное Мышление

### Как работает

**Шаг 1 — Кодирование слов в числа:**
```
"колибри" → DJB2 hash → LCG каскад → [3,8,1,5,7,2,9,0,4,6,...] (64 цифры)
```

**Шаг 2 — Граф знаний:**
```
"колибри" ←→ "птица"    (вес: 3.5)
"колибри" ←→ "маленькая" (вес: 2.1)
"колибри" ←→ "летает"   (вес: 1.8)
```

**Шаг 3 — Формульная сеть (500 слоёв × 8 цифр = 4000-цифровой геном):**
```
Слой i: operation = genome[i*8] % 12
         slope    = genome[i*8+1..3] → [±0.5, ±2.0]
         bias     = genome[i*8+4..6] → [-0.5, +0.5]
         alpha    = genome[i*8+7]    → [0.1, 0.54] (вес residual)

value = α · op(slope·value + bias) + (1-α) · input  // ResNet
```

**Шаг 4 — Эволюция:**
```
Популяция: 16 формул
Мутации:   point, swap, invert, scramble, shift
Кроссовер: single-point, two-point, uniform
Отбор:     fitness = cosine_similarity(predict(input), expected_output)
```

### Характеристики

| Свойство | Значение |
|----------|----------|
| Размер паттерна | 64 цифры на слово |
| Размер генома | 4000 цифр (500 слоёв) |
| Формул в популяции | 16 |
| Уникальность выходов | 100/100 при всех глубинах |
| Скорость predict | ~642мс / 1000 вызовов (500 слоёв) |
| Fitness после эволюции | ~0.36 |

---

## 🔬 ResNet-архитектура (500 слоёв)

Для преодоления **проблемы затухания** (vanishing problem) при глубоких сетях реализована ResNet-архитектура:

```
Вход (x) → Нормализация [-1, +1]
   ↓
┌─ Блок 1 (слои 0-9) ──────────────┐
│  residual = x                      │
│  for layer in block:               │
│    x = op(slope·x + bias)          │
│    x = tanh_clip(x)  // мягкое     │
│  x = α·x + (1-α)·residual         │  ← Weighted Skip-Connection
└────────────────────────────────────┘
   ↓
┌─ Блок 2 (слои 10-19) ────────────┐
│  ... (аналогично)                  │
└────────────────────────────────────┘
   ↓
   ... (50 блоков × 10 слоёв = 500)
   ↓
Выход → Восстановление масштаба
```

### 12 операций на слой

| # | Операция | Формула |
|---|----------|---------|
| 0 | Линейная | `slope·x + bias` |
| 1 | Обратная | `slope / (1 + \|x\|) + bias` |
| 2 | Модулярная | `(slope·x + bias) mod 1` |
| 3 | Мягко-квадратичная | `slope·x·\|x\|/(1+\|x\|) + bias` |
| 4 | Периодическая | `slope·sin(x) + bias` |
| 5 | Квантизация | `round(slope·x·4)/4 + bias` |
| 6 | Sin | `sin(slope·x + bias)` |
| 7 | Softsign | `slope·x/(1+\|x\|) + bias` |
| 8 | Масштаб | `slope·tanh(x) + bias` |
| 9 | Гауссиана | `slope·exp(-x²) + bias` |
| 10 | Tanh | `tanh(slope·x + bias)` |
| 11 | Leaky ReLU | `x ≥ 0 ? slope·x : 0.1·slope·x + bias` |

---

## 📡 API Reference

### Базовый URL: `http://localhost:8001`

### Health & System

| Метод | Путь | Описание |
|-------|------|----------|
| `GET` | `/api/health` | Статус сервиса и режим ответа |
| `GET` | `/api/knowledge/healthz` | Health-check базы знаний |

### AI Chat — `/api/v1/ai`

| Метод | Путь | Описание |
|-------|------|----------|
| `POST` | `/api/v1/ai/chat` | Главный AI-чат (числовое мышление) |
| `POST` | `/api/v1/ai/train` | Обучить AI на тексте с верификацией |
| `POST` | `/api/v1/ai/pattern` | Получить числовой паттерн слова |
| `POST` | `/api/v1/ai/embedding` | Числовой embedding текста |
| `GET` | `/api/v1/ai/stats` | Статистика движка (граф + формулы + C-модель) |
| `POST` | `/api/v1/ai/reload` | Перезагрузить корпус |
| `DELETE` | `/api/v1/ai/conversations/{id}` | Удалить разговор |

#### Пример: AI Chat

```bash
curl -X POST http://localhost:8001/api/v1/ai/chat \
  -H "Content-Type: application/json" \
  -d '{"message": "Что такое колибри?", "temperature": 0.7}'
```

**Ответ:**
```json
{
  "response": "Колибри — это маленькая птица...",
  "confidence": 0.82,
  "conversation_id": "abc123",
  "sources": ["knowledge_graph", "sentence_store"],
  "knowledge_hits": 5,
  "method": "graph+formula",
  "duration_ms": 45.2,
  "formula_data": {"predict": 0.734, "generation": 42, "fitness": 0.362},
  "graph_stats": {"patterns": 1250, "edges": 3400}
}
```

#### Пример: Обучение

```bash
curl -X POST http://localhost:8001/api/v1/ai/train \
  -H "Content-Type: application/json" \
  -d '{"text": "Колибри — самые маленькие птицы. Они умеют летать назад.", "verify": true}'
```

### Автономный агент — `/api/v1/agent`

| Метод | Путь | Описание |
|-------|------|----------|
| `POST` | `/api/v1/agent/start` | Запустить автономное обучение по теме |
| `GET` | `/api/v1/agent/status/{task_id}` | Статус задачи обучения |
| `POST` | `/api/v1/agent/stop/{task_id}` | Остановить задачу |

```bash
curl -X POST http://localhost:8001/api/v1/agent/start \
  -H "Content-Type: application/json" \
  -d '{"topic": "Квантовая физика", "max_urls": 30, "engines": ["ddg", "wiki"]}'
```

### P2P Рой — `/api/v1/swarm`

| Метод | Путь | Описание |
|-------|------|----------|
| `POST` | `/api/v1/swarm/register` | Зарегистрировать узел в рое |
| `POST` | `/api/v1/swarm/sync` | Синхронизация знаний между узлами |
| `GET` | `/api/v1/swarm/peers` | Список активных пиров |
| `GET` | `/api/v1/swarm/status` | Статус роя |

### Веб-краулер — `/api/v1`

| Метод | Путь | Описание |
|-------|------|----------|
| `POST` | `/api/v1/crawl` | Краулинг URL и обучение C-модели |

### GPU-хранилище — `/api/gpu`

| Метод | Путь | Описание |
|-------|------|----------|
| `POST` | `/api/gpu/store` | Сохранить вектор в базу |
| `POST` | `/api/gpu/search` | Cosine similarity поиск |
| `GET` | `/api/gpu/stats` | Статистика хранилища |

### Контент-фабрика — `/api/factory`

| Метод | Путь | Описание |
|-------|------|----------|
| `POST` | `/api/factory/content` | Создать контент |
| `GET` | `/api/factory/trends` | Получить тренды |
| `GET` | `/api/factory/analytics` | Аналитика контента |

### ОС-мост — `/api/dev`

| Метод | Путь | Описание |
|-------|------|----------|
| `GET` | `/api/dev/ls?path=.` | Листинг директории |
| `POST` | `/api/dev/read` | Чтение файла |
| `POST` | `/api/dev/save` | Сохранение файла |
| `GET` | `/api/dev/system` | Системная информация |

### LLM-прокси

| Метод | Путь | Описание |
|-------|------|----------|
| `POST` | `/api/v1/infer` | Прокси к внешнему LLM (`KOLIBRI_RESPONSE_MODE=llm`) |

---

## 🗜 Архиваторы

### Multi-level Archiver (лучший результат)

| Метрика | Значение |
|---------|----------|
| Сжатие исходного кода | **377x** (482 KB → 1.3 KB) |
| Уровни | 5 (иерархическое) |
| Восстановление | < 1 мс |

### Полный набор архиваторов

| Версия | Сжатие | Специализация |
|--------|--------|--------------|
| Multi-level | 377x | Исходный код |
| v3.0 RLE Meta | 57,614x | Гомогенные данные |
| v4.0 Adaptive | 50,902x | Автоматический выбор |
| v10.0 Smart | 2.25x | Реальные файлы (универсальный) |

```bash
# Multi-level
cd kolibri-archiver && make demo

# CLI
./build/kolibri_archiver compress input.txt output.klb
./build/kolibri_archiver decompress output.klb restored.txt
```

---

## 🖥 CLI-приложения

Все CLI-приложения находятся в `apps/` и собираются через CMake.

| Приложение | Описание |
|-----------|----------|
| `kolibri_node` | **Главный AI-узел** — CLI с HMAC-аутентификацией, P2P, auto-learn, auto-evolve, auto-sync |
| `kolibri_archiver` | CLI-архиватор — compress/decompress/create/add/extract/list/test |
| `kolibri_coordinator` | Координатор роя (управление кластером узлов) |
| `kolibri_mass_trainer` | Масштабный тренер (из файлов/каталогов) |
| `kolibri_bulk_teach` | Массовое обучение (пакетная загрузка знаний) |
| `kolibri_indexer` | Индексатор документов и знаний |
| `kolibri_queue` | Очередь обучения (асинхронная обработка) |
| `kolibri_sim` | CLI-симулятор (интерактивная среда) |
| `kolibri_gen` | Генератор текста из числовых паттернов |
| `kolibri_ingest` | Загрузка и обработка данных |
| `kolibri_learn` | Обучение модели на одном документе |
| `kolibri_fast_parser` | Быстрый парсер текста |
| `kolibri_inspect` | Инспектор геномов и графов знаний |
| `kolibri_knowledge_relay` | Реле знаний между узлами |
| `ks_compiler` | Компилятор KolibriScript |

### Запуск AI-узла

```bash
# С автоматическим обучением
./build/kolibri_node --genome build/training/auto_genome.dat \
  --bootstrap build/training/bootstrap.ks

# Команды внутри узла:
#   :ask <вопрос>    — задать вопрос
#   :teach <текст>   — обучить
#   :evolve          — запустить эволюцию
#   :stats           — статистика
#   :sync            — синхронизация с роем
```

---

## 🎨 Frontend

### Стек

- **React 18** + **TypeScript** + **Vite 5**
- **TailwindCSS** для стилей
- **Lucide React** для иконок
- **WASM Bridge** для интеграции с C-ядром

### Компоненты

```
frontend/src/
├── manus/                      # Unified Manus UI
│   ├── ManusAppUnified.tsx     # Главный контейнер
│   ├── ManusChat.tsx           # Чат-компонент
│   ├── ManusHeader.tsx         # Шапка с навигацией
│   ├── ManusInputBar.tsx       # Поле ввода
│   ├── ManusWelcome.tsx        # Экран приветствия
│   ├── ThemeContext.tsx         # Система тем (светлая/тёмная)
│   ├── useManusAgent.ts        # Хук агента
│   └── tabs/                   # Вкладки
│       ├── ChatTab.tsx         # AI-чат
│       ├── CrawlerTab.tsx      # Веб-краулер
│       ├── KnowledgeTab.tsx    # База знаний
│       ├── SettingsTab.tsx     # Настройки
│       ├── TasksTab.tsx        # Задачи агента
│       └── TerminalTab.tsx     # Терминал
├── components/                 # Базовые компоненты
│   ├── ChatView.tsx, ChatInput.tsx, ChatMessage.tsx
│   ├── AppShell.tsx, Layout.tsx, TopBar.tsx, Sidebar.tsx
│   └── InspectorPanel.tsx, StatusBar.tsx, WelcomeScreen.tsx
└── core/                       # Бизнес-логика
    ├── kolibri-bridge.ts       # WASM-мост (kolibri.wasm → JS)
    ├── api.ts                  # HTTP API клиент
    ├── knowledge.ts            # Граф знаний фронтенда
    ├── modes.ts                # Режимы работы
    └── useKolibriChat.ts       # React-хук чата
```

### WASM Bridge

Frontend может работать в двух режимах:
1. **WASM** — загружает `kolibri.wasm` и выполняет KolibriScript локально
2. **LLM-fallback** — при ошибке WASM деградирует до HTTP-запросов к backend

```bash
# Запуск frontend
cd frontend
npm install
npx vite --host 0.0.0.0 --port 3000
```

---

## 🔧 Сборка и тестирование

### Сборка

```bash
# C-ядро (Native)
cmake -S . -B build -G Ninja
cmake --build build

# WASM (для frontend, < 60MB)
./scripts/build_wasm.sh

# Frontend (WASM → npm install → npm build)
make frontend

# Archiver (standalone)
cd kolibri-archiver && make

# ISO-образ ОС
make iso
```

### Тестирование

```bash
# Полный набор (C + Python + Lint + Frontend)
make test

# Только C-тесты (30+ тестов)
ctest --test-dir build --output-on-failure

# Python
pytest -q
ruff check .
pyright

# Frontend (Vitest)
npm run test --prefix frontend -- --runInBand

# Бенчмарки
make benchmark          # Стандартный
make benchmark-quick    # Быстрый
make benchmark-full     # Полный
```

### Категории C-тестов

| Категория | Тесты |
|-----------|-------|
| **Базовые** | `test_decimal`, `test_genome`, `test_random`, `test_symbol`, `test_formula` |
| **Интеграция** | `test_knowledge`, `test_script`, `test_net`, `test_sim` |
| **AGI-фазы** | `test_semantic`, `test_phoneme`, `test_context`, `test_corpus`, `test_generation` |
| **Компрессия** | `test_compress`, `test_mega_compression`, `test_extreme_compression` |
| **Доказательства** | `test_proof_300000x` |

### CI Pipeline

```bash
make ci  # = build + test + frontend + iso + policy_validate.py
```

---

## 🌐 Деплой

### Docker Compose

```bash
cd deploy && docker-compose up -d
```

### Kubernetes

```bash
kubectl apply -f deploy/k8s/namespace.yaml
kubectl apply -f deploy/k8s/backend.yaml
kubectl apply -f deploy/k8s/frontend.yaml
kubectl apply -f deploy/k8s/training-cronjob.yaml
```

### Мониторинг

- **Prometheus**: `deploy/monitoring/prometheus.yml`
- **Grafana**: `deploy/monitoring/grafana_dashboard.json`

### Переменные окружения

| Переменная | Значение | Описание |
|------------|----------|----------|
| `KOLIBRI_RESPONSE_MODE` | `script` / `llm` | Режим ответа (локальный / LLM-прокси) |
| `KOLIBRI_LLM_ENDPOINT` | URL | Эндпоинт внешнего LLM |
| `KOLIBRI_LLM_MODEL` | string | Имя модели LLM |
| `KOLIBRI_LLM_API_KEY` | string | API-ключ (не коммитить!) |

---

## 📁 Структура проекта

```
kolibri-project/
├── backend/                    # Backend-платформа
│   ├── include/kolibri/        #   27 публичных C-заголовков
│   ├── src/                    #   35 C-модулей ядра
│   ├── service/                #   14 Python-модулей FastAPI
│   ├── feedback_service/       #   RLHF feedback pipeline
│   └── python/                 #   Python-обёртки
├── frontend/                   # React 18 + TypeScript + Vite
│   └── src/
│       ├── components/         #   14 UI-компонентов
│       ├── core/               #   WASM-мост, API, хуки
│       ├── manus/              #   Unified Manus UI
│       └── types/              #   TypeScript-типы
├── apps/                       # 17+ CLI-приложений (C)
├── kernel/                     # Микроядро ОС (C + ASM)
├── engine/                     # GPU-кодер (CUDA/Metal)
├── kolibri-archiver/           # Standalone архиватор
├── scripts/                    # 65+ скриптов (Bash/Python)
├── tests/                      # 80+ тестов (C/Python/TS)
├── docs/                       # 140+ документов
├── deploy/                     # Docker, K8s, мониторинг
├── benchmarks/                 # Бенчмарки сжатия
├── tools/                      # 100+ утилит и архиваторов
├── content_factory_mvp/        # Контент-фабрика (микросервисы)
├── CMakeLists.txt              # Главный CMake (17+ целей)
├── Makefile                    # Make-обёртка
└── requirements.txt            # Python-зависимости
```

Подробная карта: [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md)

---

## 📚 Дополнительная документация

| Документ | Описание |
|----------|----------|
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Полная архитектура системы |
| [docs/API_REFERENCE.md](docs/API_REFERENCE.md) | Справочник REST API |
| [backend/service/README.md](backend/service/README.md) | Backend-модули |
| [frontend/README.md](frontend/README.md) | Frontend-документация |
| [apps/README_ARCHIVER.md](apps/README_ARCHIVER.md) | Документация архиватора |
| [docs/kolibri_script.md](docs/kolibri_script.md) | Язык KolibriScript |
| [docs/swarm_protocol.md](docs/swarm_protocol.md) | P2P-протокол роя |
| [docs/formula_evolution.md](docs/formula_evolution.md) | Эволюция формул |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Гайд для контрибьюторов |
| [CHANGELOG.md](CHANGELOG.md) | История изменений |

---

## 🔒 Безопасность

- **HMAC-SHA256** аутентификация P2P-протокола
- **OpenSSL** для криптографических операций
- Sandboxed ОС-мост (ограничен директорией проекта)
- API-ключи и секреты **не хранятся** в репозитории

---

## 📜 Лицензия

MIT License — см. [LICENSE](LICENSE)
