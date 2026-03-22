# Kolibri Official Demo Path

> Один официальный demo-path Kolibri, который должен быть понятен извне и воспроизводим внутри проекта.

## 1. Что демонстрируем

Демо должно показывать не “абстрактный чат”, а одну систему:

1. новое знание попадает в Kolibri;
2. рой из 10 узлов усваивает его лучше одного узла;
3. чат отвечает на основе C-core formula memory;
4. provenance и swarm-метрики видны как часть того же контура.

## 2. Официальный сценарий

### Шаг 1. Подать новое знание

Используем endpoint:

- `POST /api/v1/ai/demo/learn/text`

Тело:

```json
{
  "text": "Право определяет допустимое поведение и защищает права участников общества.",
  "question": "что такое право",
  "title": "Право — короткое определение",
  "source": "manual",
  "category": "law"
}
```

### Шаг 2. Принудительно пересчитать рой

Этот шаг уже входит в `demo/learn/text`, но может быть вызван отдельно:

- `POST /api/v1/swarm/runtime/refresh`

### Шаг 3. Посмотреть метрики `1 vs 10`

Берём:

- `GET /api/v1/swarm/runtime/status`

Смотрим:

- `latest_knowledge`
- `last_knowledge_refresh_delta`
- `latest_demo`

### Шаг 4. Задать вопрос в чат

Используем:

- `POST /api/v1/ai/chat`

Пример:

```json
{
  "message": "что такое право",
  "profile": "balanced",
  "persona": "assistant",
  "memory_enabled": true
}
```

Ожидаемый маршрут:

- `method = "c-core-formula"`

## 3. Воспроизводимый скрипт

Для этого demo-path добавлен скрипт:

- `scripts/demo_public_path.sh`

Пример:

```bash
KOLIBRI_API_BASE=http://127.0.0.1:8001 \
./scripts/demo_public_path.sh
```

## 4. Что считается успешным demo

Демо считается успешным, если одновременно выполняются все условия:

- ingest прошёл без ошибки;
- `swarm_vs_single_delta_change` после refresh не отрицательный;
- новый вопрос по домену отвечает не через мусорный retrieval, а через `c-core-formula`;
- пользователь видит один связный отчёт, а не набор несвязанных экранов.

## 5. Product surfaces того же demo

Один и тот же demo-path должен быть доступен через:

- backend API;
- chat UI;
- CLI/локальный воспроизводимый сценарий;
- swarm status screen.

## 6. Почему это официальный demo-path

Потому что он показывает сразу все ключевые части Kolibri как одну систему:

- `C-core`
- `formula memory`
- `swarm`
- `provenance`
- `chat product surface`

Если demo не показывает связку этих частей, это не официальный demo-path Kolibri.
