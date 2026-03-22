# Kolibri Knowledge Pack Format (`.kpack`)

> Официальный публичный пакетный формат знаний Kolibri.
>
> `.kpack` — это продуктовая единица распространения знаний, формул и provenance.

## 1. Зачем нужен `.kpack`

Kolibri уже умеет работать с:

- domain docs в `data/formula_domains/`
- live formula memory в `data/swarm/live_formula_memory/`
- swarm refresh и provenance/genome

Но как продукту ему нужна переносимая единица знаний. Этой единицей объявляется `.kpack`.

`.kpack` нужен для:

- переноса знаний между инсталляциями;
- оффлайн доставки доменного пакета;
- понятного обмена знаниями в рое и между продуктами;
- воспроизводимого provenance.

## 2. Канонический контейнер

`.kpack` — это ZIP-контейнер с UTF-8 файлами и фиксированной внутренней структурой.

Минимальная структура:

```text
my-pack.kpack
├── manifest.json
├── knowledge/
│   ├── 000001_intro.txt
│   └── 000002_terms.md
├── formulas/
│   └── associations.jsonl
└── provenance/
    └── genesis.json
```

## 3. Обязательные файлы

### 3.1 `manifest.json`

Главный индекс пакета.

Минимальный пример:

```json
{
  "format": "kpack",
  "version": 1,
  "id": "kolibri.public-law.ru.v1",
  "title": "Право: базовый русскоязычный пакет",
  "language": "ru",
  "domains": ["law"],
  "entrypoints": {
    "default_query": "что такое право"
  },
  "artifacts": {
    "knowledge_dir": "knowledge",
    "formula_index": "formulas/associations.jsonl",
    "provenance": "provenance/genesis.json"
  }
}
```

### 3.2 `knowledge/`

Каталог исходных текстов пакета.

Поддерживаемые типы:

- `.txt`
- `.md`

Требования:

- UTF-8
- один файл = один осмысленный knowledge unit
- имя файла должно быть детерминированным и переносимым

## 4. Необязательные, но рекомендуемые файлы

### 4.1 `formulas/associations.jsonl`

Предварительно собранные формульные ассоциации для быстрого импорта.

Один JSONL-объект на строку:

```json
{"question":"что такое право","answer":"Право — система общеобязательных норм.","source":"manual","timestamp":1770000000}
```

### 4.2 `provenance/genesis.json`

Короткая provenance-запись о происхождении пакета:

```json
{
  "created_at": "2026-03-21T10:00:00Z",
  "created_by": "Kolibri",
  "source_kind": "manual+web",
  "genome_ref": null
}
```

## 5. Product contract

`.kpack` должен быть:

- переносимым;
- воспроизводимым;
- пригодным для swarm-обмена;
- пригодным для offline/WASM distribution;
- читаемым без приватных внутренних соглашений проекта.

## 6. Связь с текущим runtime

На текущем этапе рабочий контур уже есть:

- runtime потребляет live formula memory;
- `.kpack` зафиксирован как официальный public format;
- есть CLI и runtime API для export/import.

Практический путь сейчас такой:

1. экспортировать `.kpack` из live memory или домена;
2. импортировать `.kpack` в live memory;
3. прогнать refresh роя;
4. задавать вопрос через чат уже по импортированному знанию.

### CLI

Скрипт:

- `scripts/kpack_tool.py`

Примеры:

```bash
python3 scripts/kpack_tool.py export \
  --id kolibri.law.demo \
  --title "Право demo" \
  --domain law

python3 scripts/kpack_tool.py inspect --pack data/swarm/kpacks/kolibri.law.demo.kpack

python3 scripts/kpack_tool.py import --pack data/swarm/kpacks/kolibri.law.demo.kpack
```

### Runtime API

- `POST /api/v1/swarm/runtime/kpack/export`
- `GET /api/v1/swarm/runtime/kpack/download/{filename}`
- `POST /api/v1/swarm/runtime/kpack/import`

## 7. Рекомендуемые поля `manifest.json`

| Поле | Обязательность | Назначение |
|---|---|---|
| `format` | да | должно быть `kpack` |
| `version` | да | версия формата |
| `id` | да | стабильный идентификатор пакета |
| `title` | да | человекочитаемое имя |
| `language` | да | язык основного содержимого |
| `domains` | да | домены знаний |
| `description` | нет | краткое описание |
| `entrypoints.default_query` | нет | демонстрационный вопрос по умолчанию |
| `artifacts.knowledge_dir` | да | каталог знаний |
| `artifacts.formula_index` | нет | путь к ассоциациям |
| `artifacts.provenance` | нет | путь к provenance |

## 8. Почему `.kpack` важен для всей системы

`.kpack` связывает между собой:

- `рой` — как единицу обмена знаниями;
- `offline` — как доставку без облака;
- `provenance` — как происхождение знаний;
- `WASM` — как переносимый пакет для локальных браузерных инсталляций;
- `C-core` — как источник исполнения формул и ассоциаций.

То есть `.kpack` — это не “архивчик сбоку”, а упаковка одной и той же системы Kolibri.

## 9. Связанные документы

- [PUBLIC_ARCHITECTURE.md](PUBLIC_ARCHITECTURE.md)
- [FORMULA_API_DSL.md](FORMULA_API_DSL.md)
- [FORMAT_SPEC.md](FORMAT_SPEC.md)
- [swarm_protocol.md](swarm_protocol.md)
