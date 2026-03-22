# Kolibri Formula API / DSL

> Официальная публичная спецификация формульного слоя Kolibri.
>
> Этот документ фиксирует три поверхности:
>
> 1. C Formula API
> 2. KolibriScript runtime API
> 3. Поддерживаемый DSL-синтаксис, который реально исполняется в [script.c](/Users/kolibri/kolibri-project/backend/src/script.c)

## 1. Что такое формульный слой Kolibri

Формульный слой Kolibri — это не “LLM prompt layer”, а исполняемая память поверх C-ядра:

- формулы и ассоциации живут в `backend/include/kolibri/formula.h`;
- сценарии управления живут в `backend/include/kolibri/script.h`;
- runtime исполняется в `backend/src/formula.c` и `backend/src/script.c`.

## 2. Публичный C Formula API

### 2.1 Основные структуры

- `KolibriGene`
- `KolibriAssociation`
- `KolibriFormula`
- `KolibriFormulaPool`
- `KolibriDomainType`
- `KolibriEvolutionConfig`
- `KolibriEvolutionMetrics`

Источник: `backend/include/kolibri/formula.h`

### 2.2 Основные функции пула

```c
void kf_pool_init(KolibriFormulaPool *pool, uint64_t seed);
void kf_pool_free(KolibriFormulaPool *pool);
void kf_pool_clear_examples(KolibriFormulaPool *pool);
int  kf_pool_add_example(KolibriFormulaPool *pool, int input, int target);
int  kf_pool_add_association(KolibriFormulaPool *pool,
                             KolibriSymbolTable *symbols,
                             const char *question,
                             const char *answer,
                             const char *source,
                             uint64_t timestamp);
int  kf_pool_import_association(KolibriFormulaPool *pool,
                                KolibriSymbolTable *symbols,
                                const KolibriAssociation *association);
void kf_pool_tick(KolibriFormulaPool *pool, size_t generations);
const KolibriFormula *kf_pool_best(const KolibriFormulaPool *pool);
const KolibriFormula *kf_pool_best_for_domain(const KolibriFormulaPool *pool,
                                              KolibriDomainType domain);
```

### 2.3 Выполнение и обратная связь

```c
int    kf_formula_apply(const KolibriFormula *formula, int input, int *output);
size_t kf_formula_digits(const KolibriFormula *formula, uint8_t *out, size_t out_len);
int    kf_formula_describe(const KolibriFormula *formula, char *buffer, size_t buffer_len);
int    kf_formula_lookup_answer(const KolibriFormula *formula, int input,
                                char *buffer, size_t buffer_len);
int    kf_pool_feedback(KolibriFormulaPool *pool, const KolibriGene *gene, double delta);
```

### 2.4 Эволюционный реактор

```c
void kf_config_default(KolibriEvolutionConfig *config);
int  kf_pool_set_config(KolibriFormulaPool *pool, const KolibriEvolutionConfig *config);
int  kf_pool_get_metrics(const KolibriFormulaPool *pool, KolibriEvolutionMetrics *metrics);
int  kf_reactor_run(KolibriFormulaPool *pool, size_t max_generations, double target_fitness);
void kf_config_adapt(KolibriFormulaPool *pool);
```

## 3. KolibriScript runtime API

Источник: `backend/include/kolibri/script.h`

```c
int  ks_init(KolibriScript *skript, KolibriFormulaPool *pool, KolibriGenome *genome);
void ks_free(KolibriScript *skript);
void ks_set_output(KolibriScript *skript, FILE *vyvod);
int  ks_load_text(KolibriScript *skript, const char *text);
int  ks_load_file(KolibriScript *skript, const char *path);
int  ks_execute(KolibriScript *skript);
```

`KolibriScript` работает как управляющий слой поверх:

- formula pool;
- genome / provenance;
- symbol table;
- локальной памяти сценария;
- фрактальной памяти для `запомнить/вспомнить`.

## 4. Поддерживаемый DSL-синтаксис

Ниже приведён честный DSL, который реально парсится в `backend/src/script.c`.

### 4.1 Каркас программы

```text
начало:
  ...
конец.
```

`:` после `начало` обязателен. Точка после `конец` допускается.

### 4.2 Поддерживаемые операторы

#### Показать выражение

```text
показать <выражение>
```

#### Переменная

```text
переменная <имя> = <выражение>
```

#### Режим ответа

```text
режим <значение>
```

Реально поддерживаемые режимы в runtime:

- `neutral`
- `journal` / `журнал`
- `emoji` / `эмодзи`
- `analytics` / `аналитика`

#### Обучить связь

```text
обучить связь <стимул> -> <ответ>
```

Это создаёт/обновляет ассоциацию в formula pool.

#### Создать формулу

```text
создать формулу <имя> из <выражение>
```

или

```text
создать формула <имя> из <выражение>
```

#### Оценить формулу

```text
оценить <имя> на задаче <выражение>
```

#### Сохранить формулу в геном

```text
сохранить <имя> в геном
```

#### Отбросить формулу

```text
отбросить <имя>
```

#### Вызвать эволюцию

```text
вызвать эволюцию
```

#### Распечатать канву

```text
распечатать канву
```

#### Отправить формулу в рой

```text
рой отправить <имя>
```

#### Запомнить в фрактальную память

```text
запомнить <ключ> как <значение>
```

Ключевое слово `как` необязательно:

```text
запомнить <ключ> <значение>
```

#### Вспомнить из фрактальной памяти

```text
вспомнить <ключ>
```

#### Условие

```text
если <условие> тогда
  ...
иначе
  ...
конец
```

`иначе` необязателен.

#### Цикл

```text
пока <условие> делать
  ...
конец
```

#### Верификация

```text
верифицировать <выражение>
```

## 5. Минимальный рабочий пример

```text
начало:
режим neutral
обучить связь "что такое право" -> "Право — система общеобязательных норм."
запомнить "право" как "общеобязательные нормы"
показать "что такое право"
конец.
```

## 6. Семантика выполнения

- `обучить связь` обновляет ассоциации формульной памяти;
- `создать формулу` и `оценить` работают с именованными binding'ами внутри runtime;
- `сохранить ... в геном` уводит результат в provenance-слой;
- `рой отправить` экспортирует формулу в swarm-контур;
- `запомнить/вспомнить` используют фрактальную память как десятичное дерево знаний;
- `режим` влияет только на presentation layer runtime, а не на сами формулы.

## 7. Ограничения текущей версии

Этот DSL уже рабочий, но важно честно понимать его границы:

- это управляющий DSL для Kolibri runtime, а не полноценный общий язык программирования;
- он умеет работать с формулами, памятью, provenance и роем, но ещё не является полным логическим доказателем;
- часть операторов сейчас сильнее как runtime control, чем как конечный пользовательский язык.

## 8. Связанные документы

- [PUBLIC_ARCHITECTURE.md](PUBLIC_ARCHITECTURE.md)
- [public_interfaces.md](public_interfaces.md)
- [C_CORE_REFERENCE.md](C_CORE_REFERENCE.md)
- [swarm_protocol.md](swarm_protocol.md)
