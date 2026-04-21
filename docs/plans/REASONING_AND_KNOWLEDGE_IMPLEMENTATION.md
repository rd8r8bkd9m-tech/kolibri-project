# Детальный план имплементации: Reasoning & Knowledge Base

Этот документ описывает шаги по полноценной реализации архитектурных фасадов `backend/src/reasoning.c` и `backend/src/knowledge_base.c`, которые связывают мощное C-ядро (`reasoning_engine.c`, `knowledge.c`) с внешними интерфейсами (WASM, Python API, HTTP).

## 1. Развитие `backend/src/reasoning.c` (Фасад движка рассуждений)

Текущее состояние: Модуль предоставляет базовый вызов `kolibri_re_reason`, возвращая только финальный текстовый ответ.

### Задачи по реализации:
1. **Глобальное состояние конфигурации:**
   - Вынести `KolibriREConfig` из локальной области видимости `kolibri_reasoning_query` в статическое состояние модуля (или передавать контекст явно), чтобы настройки (например, `min_confidence_threshold`) можно было изменять извне через API.
2. **Сериализация цепочки рассуждений (Chain of Thought):**
   - Добавить функцию `kolibri_reasoning_query_json`, которая будет возвращать не только строку ответа, но и весь массив `result.chain.steps` в формате JSON.
   - Это критически важно для UI (Mantine UX), чтобы показывать пользователю промежуточные шаги логического вывода (Deductive, Inductive, Abductive).
3. **Поддержка специализированных выводов:**
   - Предоставить обертки для конкретных видов рассуждений: `kolibri_reasoning_counterfactual` (Что если?), `kolibri_reasoning_abductive` (Поиск причин).
4. **Управление лимитами времени (Timeouts):**
   - Интегрировать `reasoning_time_ms` для прерывания слишком долгих рассуждений, особенно при вызове из WASM, чтобы не блокировать Event Loop браузера.

## 2. Развитие `backend/src/knowledge_base.c` (Фасад базы знаний)

Текущее состояние: Инициализация индекса и `kolibri_knowledge_search_legacy` возвращает топ-1 результат.

### Задачи по реализации:
1. **Уход от Legacy Search:**
   - Заменить `kolibri_knowledge_search_legacy` на семантический поиск, интегрированный с AI Encoder (`ai_encoder.c`).
   - Возвращать массив релевантных документов (Top-K) с их `confidence` и метаданными, упакованными в JSON.
2. **Динамическое управление знаниями (CRUD):**
   - Реализовать функции для инъекции фактов во время выполнения: `kolibri_kb_add_fact()`, `kolibri_kb_add_rule()`.
   - Интегрировать их напрямую с `kolibri_re_add_fact` из `reasoning_engine.c`.
3. **Синхронизация с MCP SQLite:**
   - Разработать механизм загрузки графа знаний из внешнего SQLite через Python Gateway, который будет передавать данные в C-ядро через FFI или разделяемую память.

## 3. Интеграция с WASM-мостом (`wasm_bridge.c`)

- В `wasm_bridge.c` добавить обработчики команд: `REASONING_QUERY`, `KB_SEARCH`, `KB_ADD_FACT`.
- Реализовать асинхронные ответы (через `kolibri_sim_wasm_get_logs` или callback'и), чтобы долгие логические выводы отправляли прогресс-сообщения (например, «Применяю Modus Ponens...») в интерфейс.

## 4. Следующие шаги
1. Починить 3 упавших теста ядра (`test_formula_logic`, `test_inference_demo`, `test_kolibri_http_phase1_benchmark`), так как они блокируют стабильность `reasoning_engine.c`.
2. Реализовать `kolibri_reasoning_query_json` в `reasoning.c`.
3. Добавить C-тесты для фасада `reasoning.c`.
