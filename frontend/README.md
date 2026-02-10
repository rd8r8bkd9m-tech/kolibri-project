# Frontend — Kolibri OS

## Обзор

Веб-интерфейс платформы Kolibri OS. Unified Manus UI с AI-чатом, краулером, базой знаний и DevDashboard.

---

## Стек технологий

| Технология | Версия | Назначение |
|-----------|--------|-----------|
| **React** | 18.3 | UI-фреймворк |
| **TypeScript** | 5.x | Типизация |
| **Vite** | 5.4 | Сборщик и dev-сервер |
| **TailwindCSS** | 3.x | Стили |
| **Lucide React** | 0.469 | Иконки |
| **Vitest** | — | Тестирование |

---

## Структура

```
frontend/
├── src/
│   ├── App.tsx                     # Корневой компонент → <ManusAppUnified />
│   ├── main.tsx                    # Точка входа React
│   ├── DesktopOS.tsx               # Десктопный UI (альтернативный)
│   ├── DevDashboard.tsx            # Панель разработчика
│   │
│   ├── manus/                      # 🎯 Unified Manus UI (основной)
│   │   ├── ManusAppUnified.tsx     # Главный контейнер приложения
│   │   ├── ManusChat.tsx           # Чат-компонент (AI-ответы с формулами)
│   │   ├── ManusHeader.tsx         # Шапка с навигацией и темой
│   │   ├── ManusInputBar.tsx       # Поле ввода сообщений
│   │   ├── ManusLayout.tsx         # Общий layout
│   │   ├── ManusTaskPanel.tsx      # Панель задач агента
│   │   ├── ManusWelcome.tsx        # Экран приветствия
│   │   ├── ThemeContext.tsx         # Система тем (светлая/тёмная)
│   │   ├── useManusAgent.ts        # React-хук агента
│   │   └── tabs/                   # Вкладки
│   │       ├── ChatTab.tsx         # AI-чат
│   │       ├── CrawlerTab.tsx      # Веб-краулер (управление обучением)
│   │       ├── KnowledgeTab.tsx    # Граф знаний (визуализация)
│   │       ├── SettingsTab.tsx     # Настройки (тема, режим, API)
│   │       ├── TasksTab.tsx        # Задачи автономного агента
│   │       └── TerminalTab.tsx     # Встроенный терминал
│   │
│   ├── components/                 # Базовые UI-компоненты
│   │   ├── AppShell.tsx            # Общая оболочка приложения
│   │   ├── Layout.tsx              # Адаптивный layout
│   │   ├── TopBar.tsx              # Верхняя панель
│   │   ├── Sidebar.tsx             # Боковая панель
│   │   ├── NavigationRail.tsx      # Навигация (Material Design)
│   │   ├── NavItem.tsx             # Элемент навигации
│   │   ├── ChatView.tsx            # Общий вид чата
│   │   ├── ChatInput.tsx           # Компонент ввода
│   │   ├── ChatMessage.tsx         # Сообщение в чате
│   │   ├── SuggestionCard.tsx      # Карточка подсказки
│   │   ├── InspectorPanel.tsx      # Инспектор (отладка)
│   │   ├── StatusBar.tsx           # Статус-бар
│   │   ├── FeedbackForm.tsx        # Форма обратной связи
│   │   └── WelcomeScreen.tsx       # Экран приветствия
│   │
│   ├── core/                       # Бизнес-логика
│   │   ├── kolibri-bridge.ts       # WASM-мост (508 строк)
│   │   ├── api.ts                  # HTTP API клиент
│   │   ├── knowledge.ts            # Граф знаний (фронтенд)
│   │   ├── modes.ts                # Режимы работы
│   │   └── useKolibriChat.ts       # React-хук чата
│   │
│   ├── types/                      # TypeScript-типы
│   │   ├── chat.ts                 # Типы чата
│   │   ├── feedback.ts             # Типы обратной связи
│   │   └── knowledge.ts            # Типы графа знаний
│   │
│   ├── styles/                     # CSS/TailwindCSS стили
│   └── test/                       # Тесты (Vitest)
│
├── public/                         # Статические файлы
│   └── kolibri.wasm               # WASM-бинарник (< 60MB)
├── package.json
├── tsconfig.json
├── vite.config.ts
├── tailwind.config.js
└── postcss.config.js
```

---

## WASM Bridge (`core/kolibri-bridge.ts`)

Ключевой модуль — мост между JavaScript и C-ядром через WebAssembly.

### Режимы работы

| Режим | Переменная | Описание |
|-------|-----------|----------|
| **script** | `VITE_KOLIBRI_RESPONSE_MODE=script` | Загружает `kolibri.wasm`, выполняет KolibriScript локально |
| **llm** | `VITE_KOLIBRI_RESPONSE_MODE=llm` | Прокси к внешнему LLM через `/api/v1/infer` |

### Graceful Degradation

```
1. Попытка загрузить /kolibri.wasm
2. Если wasm загружен → выполнение KolibriScript локально
3. Если wasm не загружен → fallback к LLM-прокси
4. Если LLM недоступен → статическое сообщение
```

### WASI-адаптер

Полная реализация WASI (WebAssembly System Interface):
- `args_get`, `args_sizes_get` — аргументы
- `fd_write`, `fd_read` — ввод/вывод
- `clock_time_get` — время
- `environ_get`, `environ_sizes_get` — переменные окружения

### Интерфейс

```typescript
export interface KolibriBridge {
  readonly ready: Promise<void>;
  ask(prompt: string, mode?: string, context?: KnowledgeSnippet[]): Promise<string>;
  reset(): Promise<void>;
}
```

---

## Вкладки Manus UI

### ChatTab — AI-чат
Отправляет сообщения на `/api/v1/ai/chat`. Отображает:
- Текстовый ответ
- Числовые паттерны
- Confidence и источники
- Формульные данные (generation, fitness)

### CrawlerTab — Веб-краулер
Управление `/api/v1/agent/start`:
- Ввод темы
- Выбор поисковых систем (DDG, Bing, Wiki)
- Мониторинг прогресса обучения
- Статистика собранных страниц

### KnowledgeTab — База знаний
Визуализация графа знаний:
- Список паттернов слов
- Визуализация связей (рёбер)
- Статистика (кол-во паттернов, рёбер, документов)

### SettingsTab — Настройки
- Переключение темы (светлая/тёмная)
- Выбор режима ответа (script/llm)
- Настройка API URL

### TasksTab — Задачи агента
- Список активных задач обучения
- Прогресс (страницы, паттерны, рёбра)
- Управление (старт/стоп)

### TerminalTab — Терминал
- Встроенный терминал для команд
- Вывод логов

---

## Переменные окружения

| Переменная | Описание | По умолчанию |
|------------|----------|-------------|
| `VITE_KOLIBRI_RESPONSE_MODE` | Режим (`script` / `llm`) | `script` |
| `VITE_KOLIBRI_API_BASE` | Базовый URL API | `/api` |
| `VITE_API_BASE_URL` | URL для feedback | `` |

---

## Запуск

```bash
# Development (с hot-reload)
cd frontend
npm install
npx vite --host 0.0.0.0 --port 3000

# Или с переменными для WASM stub (dev без wasm)
KOLIBRI_SKIP_WASM_AUTOBUILD=1 KOLIBRI_ALLOW_WASM_STUB=1 npx vite --port 3000

# Production build
npm run build      # → dist/

# Тестирование
npm run test       # Vitest

# Lint
npm run lint       # ESLint
```

---

## Сборка WASM

```bash
# Из корня проекта
./scripts/build_wasm.sh
# Результат: build/wasm/kolibri.wasm (должен быть < 60MB)

# Копировать в frontend
cp build/wasm/kolibri.wasm frontend/public/
```
