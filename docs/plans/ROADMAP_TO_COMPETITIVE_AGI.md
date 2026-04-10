# Kolibri True AI Master Roadmap v2

> **Дата обновления:** 2026-04-09
>
> **Статус документа:** основной стратегический roadmap до конца 2027 года
>
> **Назначение:** заменить старый optimistic roadmap единым master-документом,
> который соединяет `Reasoner -> Chat -> Swarm` и использует только честные
> статусы `implemented / in_progress / planned`.

---

## 1. Что для Kolibri значит "настоящий ИИ"

Kolibri не должен превращаться в ещё одну LLM с большим количеством текстовых
эвристик. Для проекта Kolibri "настоящий ИИ" означает систему, которая:

- рассуждает, а не только статистически продолжает текст;
- считает точно, а не угадывает арифметику;
- объясняет происхождение ответа;
- умеет проверять себя альтернативным методом;
- накапливает память и улучшает качество после взаимодействий;
- работает как единый интеллект в `native C`, `WASM` и `product shell`;
- в перспективе масштабируется до автономного swarm-организма.

### Приоритет целей

Kolibri развивает все три линии, но строго в таком порядке:

1. **Trustworthy Reasoner**
   Надёжный reasoning-first ИИ для математики, науки, кода, права и
   структурированных знаний.
2. **General Conversational Intelligence**
   Сильный chat-first продукт на том же ядре: multi-turn диалог, grounded
   follow-ups, provenance и self-consistency.
3. **Autonomous Swarm Scientist**
   Автономный рой узлов, который учится, валидирует новые знания, синхронизирует
   лучшие формулы и улучшает коллективное качество.

---

## 2. Неизменяемые столпы Kolibri

### 2.1 Decimal Cognition

Внутреннее представление Kolibri строится вокруг цифр `0..9`, а не вокруг
обычных токенов строкового LLM-мира.

- Все входы проходят через decimal transduction.
- Числа, арифметика, токены, сигналы голосования и provenance должны быть
  совместимы с decimal-first контуром.
- `decimal.c`, `digits.c`, `digit_text.c`, `numeric_tokenizer.c` остаются
  фундаментом восприятия.

### 2.2 Formula Evolution

Kolibri учится через эволюцию и отбор исполняемых формул, а не только через
масштабирование параметров.

- Формула считается рабочей единицей знания.
- Fitness, mutation, crossover, population diversity и replayability остаются
  обязательной частью архитектуры.
- Даже при наличии backprop и transformer-компонентов формульный слой остаётся
  первичным differentiator проекта.

### 2.3 Digital Genome

Память Kolibri должна быть проверяемой и прослеживаемой.

- Все значимые обучающие и reasoning-события должны иметь provenance.
- `genome.c` и `ReasonBlock` остаются базой для audit trail.
- Формула без происхождения не считается fully trusted knowledge.

### 2.4 Swarm Intelligence

Swarm не является декоративным экраном или маркетинговым benchmark.

- Рой нужен для реального распространения полезных знаний.
- Узлы должны иметь роли, health, consensus, disagreement и measurable uplift.
- Любой ingest должен в перспективе уметь доходить до swarm refresh и validator
  consensus path.

---

## 3. Anti-Goals

Kolibri сознательно **не** строится как:

- клон GPT/Claude/ChatGPT с текстовой оптимизацией любой ценой;
- продукт, где Python request-serving считается ядром интеллекта;
- набор несвязанных демо-модулей без одного канонического runtime;
- "сверх-AGI" по заявлениям в документах без reproducible benchmarks;
- swarm-визуализация без доказуемого прироста качества;
- WASM-демо, которое живёт отдельно от боевого ядра.

---

## 4. Truth Policy for Status Claims

Этот документ подчиняется жёсткой политике статусов.

### 4.1 Разрешённые статусы

- `implemented` — есть код, тесты, воспроизводимый запуск и acceptance.
- `in_progress` — есть код или design, но нет полного подтверждения.
- `planned` — есть только план, идея, черновик или частичная заготовка.

### 4.2 Правила

- Нельзя отмечать phase outcome как завершённый только потому, что существует
  файл, модуль или локальный прототип.
- Если acceptance gates из [QA_ACCEPTANCE.md](../QA_ACCEPTANCE.md) не зелёные,
  задача не считается fully complete.
- Если CTest зарегистрировал тест, но бинарь не собирается или не запускается,
  такой тест не может подтверждать roadmap-claim.
- `ROADMAP_SINGLE_SOURCE_OF_TRUTH.md` остаётся factual baseline по реализованному
  состоянию; этот документ задаёт стратегию и квартальную программу.

### 4.3 Иерархия правды

Порядок источников правды:

1. [PRODUCT_SPEC_V2.md](../PRODUCT_SPEC_V2.md)
2. [PUBLIC_ARCHITECTURE.md](../PUBLIC_ARCHITECTURE.md)
3. [API_REFERENCE.md](../API_REFERENCE.md)
4. [QA_ACCEPTANCE.md](../QA_ACCEPTANCE.md)
5. [ROADMAP_SINGLE_SOURCE_OF_TRUTH.md](ROADMAP_SINGLE_SOURCE_OF_TRUTH.md)
6. Этот master-roadmap

Если документы конфликтуют, приоритет имеет более высокий элемент списка.

---

## 5. Current Factual Baseline on 2026-04-09

### 5.1 Что уже подтверждено

- product shell остаётся `chat-first`;
- C-core содержит модули для formula/inference, logical memory, world model,
  exact math, reasoning engine, self-verification, explanation generation,
  swarm-learner и related subsystems;
- WASM path существует и уже встроен во frontend;
- C HTTP server существует и уже обслуживает часть runtime/API сценариев;
- numeric voting и morphology/semantics имеют по крайней мере первую
  production-stage реализацию в C inference path;
- live learning и swarm runtime существуют как продуктовые контуры.

### 5.2 Что ещё не подтверждено как завершённое

- C runtime как единственный канонический production gateway;
- full parity между `native C`, `WASM` и chat runtime;
- 50-node swarm как реально завершённый production capability;
- agentic tool use и reasoning-guided action loop;
- единый C-owned learning loop без Python orchestration debt;
- consistency всей документации вокруг одного roadmap/status model.

### 5.3 Главный текущий разрыв

Kolibri уже имеет сильный набор идей и модулей, но пока ещё не собран в один
проверяемый интеллект-контур:

`Perception -> Working Cognition -> Trust -> Memory -> Learning -> Action -> Swarm`

Именно этот разрыв закрывает настоящий roadmap.

---

## 6. Target Architecture Through 2027

### 6.1 North Star

Канонический production path Kolibri:

`C cognitive kernel -> C runtime gateway -> WASM twin -> product shell`

Python допускается только как:

- research tooling;
- dataset preparation;
- CI/CD scripts;
- migration helpers;
- operator tooling вне канонического intelligence path.

### 6.2 Canonical Intelligence Pipeline

#### Perception

- decimal transduction;
- tokenization;
- morphology;
- normalization;
- topic/entity extraction;
- semantic continuity signals.

#### Working Cognition

- numeric voting;
- formula / inference layer;
- symbolic reasoning;
- exact arithmetic;
- world model;
- logical memory;
- memory linking.

#### Trust Layer

- self-verification;
- contradiction detection;
- confidence rationale;
- provenance collection;
- explanation generation.

#### Persistent Learning

- live queue;
- approved knowledge assimilation;
- formula evolution;
- genome updates;
- quality history;
- replayable learning artifacts.

#### Autonomy

- planning;
- tool routing;
- task state;
- post-action verification;
- reflection cycle;
- swarm propagation.

### 6.3 Public Runtime Principle

- Frontend должен видеть один канонический runtime contract.
- WASM должен быть twin того же ядра, а не отдельной деградированной логикой.
- Public API и frontend types должны быть runtime-agnostic, а не привязаны к
  FastAPI semantics.

---

## 7. Quarterly Roadmap

### Phase 0 — Truth Reset and Canonicalization

**Срок:** 2026-04-09 -> 2026-05-31

**Цель:** перестать жить в conflicting-docs режиме и зафиксировать один
engineering reality model.

### Outcomes

- `ROADMAP_TO_COMPETITIVE_AGI.md` полностью заменён на этот master-roadmap.
- Канонический набор документов официально определён.
- Старые optimistic claims переведены в честные статусы либо отправлены в
  `unconfirmed`/archive контур.
- Build/test inventory очищен: test registry соответствует реально собираемым и
  запускаемым артефактам.

### Required work

- Проверить все стратегические claims на соответствие `implemented` policy.
- Убрать future-perfect формулировки вроде "всё завершено", если acceptance не
  подтверждён.
- Починить или понизить статус тестов, которые числятся, но не исполняются.
- Привести `README`, roadmap docs и status docs к одной навигационной модели.

### Exit gate

- docs consistent;
- roadmap conflicts removed;
- test inventory honest;
- no false-completion language in primary docs.

**Статус на 2026-04-09:** `in_progress`

---

### Phase 1 — Cognitive Kernel for Trustworthy Reasoning

**Срок:** 2026-06-01 -> 2026-09-30

**Цель:** довести C-kernel до состояния reasoning-first AI, а не набора
telemetry-модулей.

### Core objectives

- morphology и semantics становятся обязательным runtime layer;
- numeric voting принимает реальные routing/quality decisions;
- reasoning engine, exact arithmetic, explanation generator и self-verification
  входят в единый canonical answer pipeline;
- memory linking и follow-up continuity уходят из route-specific patchwork в ядро.

### Required outcomes

- точный math/solver path для арифметики, алгебры и logic puzzles;
- contradiction-aware answer evaluation;
- explanation + provenance attached by default для reasoning-grade answers;
- `5-turn continuity` как explicit engineering target;
- benchmark-first development вместо feature-claim-first development.

### Hard restrictions

- нельзя объявлять Kolibri competitive AI из-за наличия отдельных модулей;
- нельзя идти в model-scaling, пока reasoning benchmark ladder не зелёный;
- нельзя оставлять morphology/semantics только как export telemetry.

### Exit gate

- exact arithmetic green;
- algebra/solver tasks green;
- logic + contradiction tasks green;
- explanation fidelity validated;
- 5-turn continuity baseline reached;
- targeted C/backend tests green.

**Статус на 2026-04-09:** `in_progress`

**Текущее reproducible evidence**

- `ctest --test-dir build -R '^(test_math_solver|test_self_verification|test_explanation_generator|test_reasoning_engine|test_kolibri_http_server_api|test_kolibri_http_stream_api|test_kolibri_http_phase1_benchmark)$' --output-on-failure`
- `python3 tests/test_kolibri_http_phase1_benchmark.py build/kolibri_http_server --output-json build/benchmarks/kolibri_http_phase1_benchmark.json`
- `python3 scripts/check_ctest_inventory.py --build-dir build`
- `cd frontend && npm run test && npm run lint && npm run build`

---

### Phase 2 — General Conversational Intelligence on the Same Core

**Срок:** 2026-10-01 -> 2026-12-31

**Цель:** построить сильный chat layer поверх reasoning-core, не разрушая trust
и interpretability.

### Core objectives

- устойчивый multi-turn dialogue;
- entity/topic switching без route hacks;
- tool necessity detection через numeric voting;
- grounded answers with provenance;
- retry, revise, resend через тот же canonical runtime.

### Product rules

- Kolibri остаётся chat-first продуктом;
- user-facing `/api/v1/ai/chat` и `/api/v1/ai/chat/stream` остаются каноническими
  поверхностями;
- backend-specific shortcut branches должны исчезать из обычного диалога;
- WASM/native parity становится release requirement для базовых reasoning/chat flows.

### Exit gate

- multi-turn dialogue stability validated;
- entity/topic switching stable;
- answer revision path consistent;
- WASM/native parity smoke green;
- browser smoke green for desktop and mobile.

**Статус на 2026-04-09:** `planned`

---

### Phase 3 — Learning, Memory and Self-Improvement

**Срок:** 2027-01-01 -> 2027-03-31

**Цель:** превратить разрозненные обучающие подсистемы в один C-owned learning
loop уровня настоящего ИИ.

### Core objectives

- свести `live queue -> moderation -> approved knowledge -> assimilation ->
  training -> refresh -> provenance` в единый контролируемый цикл;
- ввести долговременную память уровня ИИ;
- включить reflection cycle после ответа;
- сделать quality history и knowledge growth видимыми для оператора.

### Required memory layers

- semantic memory linking;
- episodic conversation memory;
- learned document memory;
- formula/genome memory with traceability.

### Exit gate

- approved knowledge reliably influences future answers;
- reflection signals persist and are inspectable;
- memory layers are observable through product/runtime interfaces;
- live learning propagation delay measured and reported.

**Статус на 2026-04-09:** `planned`

---

### Phase 4 — Autonomous Tool-Using Agent

**Срок:** 2027-04-01 -> 2027-06-30

**Цель:** сделать следующий шаг от отвечающей системы к действующей системе.

### Core objectives

- планирование задач;
- tool routing;
- stateful execution;
- post-action verification;
- reflection after tool use.

### Product meaning

После этой фазы Kolibri должен уметь не только отвечать, но и выполнять
reasoning-guided action loops в целевых доменах:

- code workflows;
- math/science workflows;
- structured knowledge operations;
- swarm administration tasks.

### Exit gate

- tool use is reasoned, logged and verified;
- every tool step has provenance and expected outcome;
- failed actions are detectable and recoverable;
- canonical runtime supports action loop telemetry.

**Статус на 2026-04-09:** `planned`

---

### Phase 5 — Autonomous Swarm Intelligence

**Срок:** 2027-07-01 -> 2027-12-31

**Цель:** превратить swarm в настоящий distributed intelligence layer.

### Core objectives

- реализовать production смысл `1 vs 10 vs 50`;
- перевести роли `anchor / learner / validator` в реальный runtime contract;
- сделать consensus, disagreement, propagation и uplift product-visible и
  benchmarked;
- довести ingest path до validator-backed swarm learning cycle.

### Required ingest chain

Каждый ingest в зрелом runtime должен вызывать:

1. memory update;
2. provenance write;
3. candidate evaluation;
4. learner propagation;
5. validator check;
6. consensus score update.

### Exit gate

- swarm uplift measurable and reproducible;
- validator quorum meaningful;
- disagreement reporting live;
- propagation delay under control;
- 50-node contract backed by real runtime evidence.

**Статус на 2026-04-09:** `planned`

---

## 8. Public Interfaces and Docs Contract

Kolibri сохраняет текущие user-facing surfaces:

- chat;
- workspace;
- settings;
- account;
- live queue;
- swarm runtime.

### Stable interface targets

Нужно сохранить и стабилизировать следующие классы интерфейсов:

- `/api/v1/ai/*`
- `/api/v1/auth/*`
- `/api/v1/account/*`
- `/api/v1/swarm/runtime/*`
- `/api/v1/live-queue/*`
- `/metrics`
- conversation metadata и turns APIs

### Rules

- Если endpoint зависит от Python-only semantics, он должен либо получить
  C-equivalent, либо быть выведен из stable contract.
- Frontend types не должны зависеть от конкретной реализации gateway.
- Docs описывают contract, а не случайную текущую реализацию конкретного сервера.

---

## 9. Benchmark Ladder

Развитие Kolibri оценивается не одним benchmark score, а лестницей зрелости.

### Level A — Exact Reasoning

- exact arithmetic;
- linear/quadratic solving;
- systems solving;
- symbolic checks;
- contradiction detection.

### Level B — Trustworthy Explanations

- step-by-step explanation fidelity;
- provenance completeness;
- confidence rationale;
- self-verification agreement.

### Level C — Conversational Intelligence

- 5-turn follow-up continuity;
- entity switching;
- topic retention;
- grounded retries and revisions.

### Level D — Learning and Reflection

- approved knowledge assimilation;
- persistent memory effect;
- reflection after weak answers;
- quality history growth.

### Level E — Action and Swarm

- verified tool use;
- native/WASM parity;
- live learning propagation delay;
- swarm uplift `1 vs 10 vs 50`;
- consensus/disagreement telemetry.

---

## 10. Release Gates

Каждый квартальный milestone считается завершённым только если выполняются все
обязательные gates:

1. docs consistent;
2. targeted backend/C tests green;
3. frontend typecheck/build green;
4. browser or product smoke green;
5. reproducible benchmark report attached.

### Additional rules

- Нельзя называть Kolibri "настоящим ИИ" на уровне продукта, пока система не
  проходит reasoning, multi-turn, learning и action gates в одном каноническом
  runtime.
- Нельзя называть swarm production-ready, пока нет реального uplift и validator
  consensus evidence.
- Нельзя объявлять C/WASM-only path завершённым, пока обычный пользовательский
  product flow ещё зависит от Python request-serving в default deployment.

---

## 11. Architecture Defaults

По умолчанию считаются принятыми следующие решения:

- Приоритет развития: `Reasoner -> Chat -> Swarm`.
- "Настоящий ИИ" для Kolibri — это надёжный интеллект с памятью, reasoning,
  verification, learning и agency.
- Производственная архитектура целится в `C/WASM-only intelligence path`.
- Python допускается только вне основного пути инференса и orchestration.
- Масштабирование моделей, GPU-ветки и benchmark-chasing против LLM
  откладываются до доказательства силы Kolibri на собственных differentiators.

---

## 12. Immediate Next Steps

Следующие действия запускаются немедленно после принятия этого roadmap:

1. привести primary docs и index к новой иерархии правды;
2. инвентаризировать test registry и убрать ложные подтверждения прогресса;
3. определить canonical reasoning benchmark pack;
4. определить список Python-owned endpoints и план их C migration или deprecation;
5. свести answer pipeline к одному reasoning-first контуру.

---

## 13. Final Definition

Kolibri 2027 — это не "маленькая LLM" и не набор экспериментальных модулей.

Kolibri 2027 — это:

- **точный** ИИ, потому что числа и проверка являются частью ядра;
- **объяснимый** ИИ, потому что reasoning и provenance доступны наружу;
- **живой** ИИ, потому что он умеет учиться и отражать опыт;
- **действующий** ИИ, потому что способен выполнять verified action loops;
- **распределённый** ИИ, потому что swarm даёт реальный коллективный прирост.

Только такая траектория соответствует оригинальному концепту Kolibri и задаче
превратить его в настоящий ИИ.
