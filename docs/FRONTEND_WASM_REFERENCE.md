# Kolibri OS — Frontend + WASM: Справочник

> Документация Frontend-приложения и WebAssembly-моста
>
> React + TypeScript + Vite - 53 файла - 9 970 строк

---

## 1. Стек технологий

| Технология | Назначение |
|-----------|-----------|
| React 18+ | UI-библиотека |
| TypeScript 5+ | Типизация |
| Vite 5+ | Сборщик |
| TailwindCSS 3+ | Стилизация |
| Vitest | Тестирование |
| WebAssembly | C -> браузер |

---

## 2. Структура проекта

```
frontend/src/
+-- main.tsx                      # Точка входа React
+-- App.tsx                       # Главный компонент
+-- DesktopOS.tsx                 # Десктопный режим (оконный менеджер)
+-- DevDashboard.tsx              # Панель разработчика
|
+-- core/                         # Ядро (бизнес-логика)
|   +-- kolibri-bridge.ts         # WASM-мост (главный модуль)
|   +-- api.ts                    # HTTP-клиент к бэкенду
|   +-- knowledge.ts              # API знаний
|   +-- modes.ts                  # Режимы работы ИИ
|   +-- numeric-patterns.ts       # Числовые паттерны
|   +-- useKolibriChat.ts         # React-хук чата
|   +-- __tests__/                # Тесты ядра
|
+-- manus/                        # Основной UI
|   +-- ManusAppUnified.tsx       # Главный контейнер
|   +-- ManusHeader.tsx           # Навигация + тема
|   +-- ManusInputBar.tsx         # Поле ввода
|   +-- ManusChat.tsx             # Чат-компонент
|   +-- ManusWelcome.tsx          # Экран приветствия
|   +-- ManusTaskPanel.tsx        # Панель задач
|   +-- ThemeContext.tsx          # Dark/Light тема
|   +-- useManusAgent.ts          # Хук агента
|   +-- tabs/
|   |   +-- ChatTab.tsx           # Чат с ИИ
|   |   +-- CrawlerTab.tsx        # Веб-краулинг
|   |   +-- KnowledgeTab.tsx      # Знания
|   |   +-- SettingsTab.tsx       # Настройки
|   |   +-- TasksTab.tsx          # Задачи
|   |   +-- TerminalTab.tsx       # Терминал
|   |   +-- ArchiverTab.tsx       # Архивация
|   +-- components/
|       +-- MarkdownRenderer.tsx
|
+-- types/                        # TypeScript типы
|   +-- chat.ts
|   +-- knowledge.ts
|   +-- feedback.ts
|
+-- styles/                       # CSS
+-- test/setup.ts                 # Настройка Vitest
```

---

## 3. WASM-мост (kolibri-bridge.ts)

Главный модуль связи фронтенда с C-ядром через WebAssembly.

### Интерфейс KolibriBridge

```typescript
export interface KolibriBridge {
  readonly ready: Promise<void>;
  ask(prompt: string, mode?: string, context?: KnowledgeSnippet[]): Promise<string>;
  reset(): Promise<void>;
}
```

### WASM-экспорты из C

```typescript
interface KolibriWasmExports {
  memory: WebAssembly.Memory;
  _malloc(size: number): number;
  _free(ptr: number): void;
  _kolibri_bridge_init(): number;
  _kolibri_bridge_reset(): number;
  _kolibri_bridge_execute(
    programPtr: number,
    outputPtr: number,
    outputCapacity: number
  ): number;
}
```

### Режимы работы

Управляется переменной `VITE_KOLIBRI_RESPONSE_MODE`:

| Режим | Описание |
|-------|----------|
| `script` (default) | Загрузка kolibri.wasm, выполнение KolibriScript |
| `llm` | HTTP-прокси через /api/v1/infer к LLM |

### Механизм загрузки

```
1. Попытка загрузки /kolibri.wasm
2. Успех -> WASM-режим:
   - _kolibri_bridge_init()
   - ask() -> KolibriScript -> _kolibri_bridge_execute() -> ответ
3. Ошибка -> Graceful degradation:
   - LLM-режим: HTTP POST /api/v1/infer
   - Статический режим: предзаписанные ответы
```

### Константы

```typescript
const OUTPUT_CAPACITY = 8192;            // Буфер ответа (8 KB)
const WASM_RESOURCE_URL = "/kolibri.wasm";
const WASM_INFO_URL = "/kolibri.wasm.txt";
```

---

## 4. Компоненты интерфейса

### 4.1. ManusAppUnified - Главный компонент

Единый контейнер с системой вкладок:

```
+------------------------------------------------+
|  ManusHeader  (лого, навигация, тема)          |
+--------+---------------------------------------+
|        |                                       |
|  Tabs  |  Content Area                         |
|  --    |                                       |
|  Chat  |  [Активная вкладка]                   |
|  Know  |                                       |
|  Crawl |                                       |
|  Tasks |                                       |
|  Arch  |                                       |
|  Term  |                                       |
|  Set   |                                       |
|        |                                       |
+--------+---------------------------------------+
|  ManusInputBar  (ввод сообщения)               |
+------------------------------------------------+
```

### 4.2. Вкладки (7 шт.)

| Вкладка | Файл | Описание |
|---------|------|----------|
| Chat | ChatTab.tsx | Основной чат с ИИ |
| Knowledge | KnowledgeTab.tsx | Просмотр/управление базой знаний |
| Crawler | CrawlerTab.tsx | Управление веб-краулингом |
| Tasks | TasksTab.tsx | Задачи автономного агента |
| Archiver | ArchiverTab.tsx | Сжатие/распаковка данных |
| Terminal | TerminalTab.tsx | Интерактивный терминал |
| Settings | SettingsTab.tsx | Настройки приложения |

### 4.3. Десктопный режим (DesktopOS.tsx)

Оконный менеджер, имитирующий рабочий стол ОС:
- Каждый компонент = "окно"
- Перетаскивание, сворачивание, закрытие
- Панель задач внизу

---

## 5. Ядро (core/)

### 5.1. api.ts - HTTP-клиент

```typescript
const API_BASE = import.meta.env.VITE_KOLIBRI_API_BASE ?? "/api";

export async function apiChat(query: string): Promise<ChatResponse>;
export async function apiTrain(text: string): Promise<TrainResponse>;
export async function apiSearch(query: string): Promise<SearchResponse>;
export async function apiStats(): Promise<StatsResponse>;
```

### 5.2. useKolibriChat.ts - React-хук

```typescript
export function useKolibriChat() {
  const [messages, setMessages] = useState<Message[]>([]);
  const [loading, setLoading] = useState(false);

  const send = async (text: string) => { ... };
  const clear = () => setMessages([]);

  return { messages, loading, send, clear };
}
```

### 5.3. knowledge.ts - API знаний

```typescript
export async function teachKnowledge(text: string): Promise<void>;
export async function searchKnowledge(query: string): Promise<KnowledgeSnippet[]>;
export async function sendKnowledgeFeedback(id: string, positive: boolean): Promise<void>;
```

### 5.4. modes.ts - Режимы

```typescript
export const MODES = [
  { id: "neutral",    label: "Нейтральный" },
  { id: "creative",   label: "Творческий" },
  { id: "analytical", label: "Аналитический" },
  { id: "concise",    label: "Краткий" },
] as const;
```

---

## 6. Сборка и развертывание

### Команды

```bash
# Разработка (hot reload)
cd frontend && npm run dev

# Сборка WASM + Frontend
make frontend

# Только frontend
cd frontend && npm run build

# Тесты
cd frontend && npm run test
```

### WASM-сборка

```bash
# Сборка WASM бинарника
./scripts/build_wasm.sh

# Результат:
# build/wasm/kolibri.wasm       (< 60 MB)
# build/wasm/kolibri.wasm.txt   (информация)
```

### Переменные окружения (Vite)

| Переменная | По умолчанию | Описание |
|-----------|-------------|----------|
| VITE_KOLIBRI_API_BASE | /api | URL API-бэкенда |
| VITE_KOLIBRI_RESPONSE_MODE | script | Режим: script/llm |

### Ограничения WASM

- **Размер**: < 60 MB (61 440 KB)
- **Детерминизм**: Выход WASM = выход нативного C
- **OpenSSL**: Недоступен в WASM -> используется wasm_genome_stub.c

### Развертывание

```
1. ./scripts/build_wasm.sh
2. cp build/wasm/kolibri.wasm frontend/public/
3. cd frontend && npm run build
4. Результат: frontend/dist/  (статика)
5. Любой HTTP-сервер: nginx, caddy, etc.
```

---

## Типы данных

### chat.ts

```typescript
export interface Message {
  id: string;
  role: "user" | "assistant";
  content: string;
  timestamp: number;
}

export interface ChatResponse {
  response: string;
  sources?: string[];
  confidence?: number;
}
```

### knowledge.ts

```typescript
export interface KnowledgeSnippet {
  id: string;
  text: string;
  source: string;
  confidence: number;
  embedding?: number[];
}
```

---

*Документация Frontend + WASM - Kolibri OS*
*Copyright (c) 2025 Кочуров Владислав Евгеньевич*
