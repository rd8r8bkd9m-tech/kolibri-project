# Kolibri Training Recipe

## Purpose
Зафиксировать практический процесс пополнения знаний и обучения формул KolibriSim. Сценарий ориентирован на автоматизацию `scripts/auto_train.sh` и учебный файл `training/math_dialog.ks`.

## Steps
1. **Подготовьте источники**  
   Пополните каталоги `docs/` и `data/` новыми материалами (например, математические конспекты, примеры диалогов). Они будут обработаны конвейером знаний.

2. **При необходимости подкачайте внешние сайты**  
   Составьте файл `urls.txt` с нужными ссылками (по одной в строке) и выполните:
   ```
   ./scripts/kolibri_fetch_docs.py --url-file urls.txt --output docs/ingested
   ```
   Скрипт скачает страницы, вырежет текст и сохранит Markdown-файлы в `docs/ingested/`, после чего их увидит конвейер знаний.
   Для автоматического пополнения списков используйте:
   ```
   ./scripts/collect_seeds.py --catalog seeds/catalog.json --output crawler_seeds.txt
   ```
   Это соберёт официальные домены из заданных каталогов и подготовит `crawler_seeds.txt` для паука.
После каждой сборки `knowledge_pipeline.sh` запускается `scripts/sentinel_guard.py`, который проверяет целостность генома и сохраняет утверждённую копию в `build/knowledge/approved/`.

### Тематические модули
- Создайте файл `modules/<module>.txt` со списком корней (например, `modules/python_core.txt`). Пустые строки и строки с `#` игнорируются.
- Запускайте конвейер с конкретным модулем:
  ```
  ./scripts/knowledge_pipeline.sh --module python_core
  ```
  Все артефакты окажутся в `build/knowledge/modules/python_core/`, а Sentinel сохранит утверждённую копию в `.../approved/`.

3. **Подготовьте собственную обучающую модель**  
    Создайте сценарий в `training/custom_model.ks` (пример уже добавлен). Он будет объединён с автоматически сгенерированным bootstrap, чтобы зафиксировать собственную обучающую модель.

    При необходимости обновите пары `обучить связь` и свои правила эволюции. Сценарий должен завершаться сохранением результата в геном.

3a. **Список локальных моделей и форк без скачивания**  
    Для требования «сам составь список моделей и полностью форкни» используйте каталог `models/local_model_catalog.json` и скрипт `scripts/fork_local_models.sh`. Он копирует только локальные файлы, не выполняя сетевых запросов.

    ```
    mkdir -p models/local_sources
    # Скопируйте сюда свои .dat геномы, затем обновите models/local_model_catalog.json
    ./scripts/fork_local_models.sh --dry-run
    ./scripts/fork_local_models.sh --output-dir models/forked
    ```

    Скрипт пропускает отсутствующие источники, выводит предупреждения и продолжает работу.

4. **Запустите автообучение**  
   ```
   ./scripts/auto_train.sh --ticks 256 --seed 424242 --ingest-list urls.txt
   ```
   Скрипт:
   - собирает проект (если не указан `--skip-build`);
   - при указании `--ingest-list` вызывает `scripts/kolibri_fetch_docs.py`, чтобы скачать свежие тексты;
   - прогоняет `scripts/knowledge_pipeline.sh` для указанных корней (`docs data` по умолчанию);
   - формирует `build/knowledge/knowledge_genome.dat`, записывая каждый документ в виде ReasonBlock (`KNOWLEDGE_DOC`);
   - генерирует `build/training/bootstrap.ks` из свежего `build/knowledge/index.json`;
   - приклеивает сценарий `training/math_dialog.ks`, содержащий базовую арифметику и фразы общения;
   - запускает `kolibri_node` с bootstrap и записывает геном `build/training/auto_genome.dat`.

5. **Soak и анализ JSONL**  
   После тренировки автоскрипт выполняет:
   - `build/kolibri_sim soak --minutes 1 --log build/training/auto_trace.jsonl`;
   - Python-анализ лога: считает fallback-ответы, встречи приветствий и математических примеров.
   Метрики выводятся в консоль, чтобы можно было отслеживать прогресс от прогона к прогону.
   Дополнительно можно запустить
   ```
   ./scripts/build_summary_module.py --input build/training/auto_trace.jsonl --output docs/generated/kolibri_summary_module.md
   ```
   чтобы превратить удачные ответы в новый Markdown‑модуль, который затем поступает в pipeline.

6. **Проверка генома**  
   `kolibri_node --health --genome build/training/auto_genome.dat` показывает целостность и длину ReasonBlock цепочки.

## Extending
- Добавляйте новые сценарии в `training/` и обновляйте `auto_train.sh`, чтобы включать их в bootstrap. В KolibriScript используйте команды `обучить связь`, `создать формулу`, `оценить`, `верифицировать`.
- Расширяйте анализ JSONL, если нужны специфические сигналы (например, поиск фраз «колибри ещё учится» по конкретным задачам).
- Проверяйте изменения тестами (`ctest -R kolibri_tests`) — `tests/test_sim.c` гарантирует, что JSONL и формулы продолжают работать.

## Autonomous Mode
- **Crawler:** `scripts/kolibri_crawler.py` поддерживает очередь URL в SQLite и выгрузку очистленных текстов в `docs/ingested/`. Указывайте семена (`--seed-file seeds.txt`) и домены (`--allow-domain example.com`), чтобы контролировать область сбора.
- **Оркестратор:** запустите `scripts/run_autonomy.sh --seeds-file seeds.txt --allow-domain example.com --skip-build`. Скрипт повторяет цикл `[crawler → knowledge_pipeline → auto_train]` и дремлет между циклами. См. `run_autonomy.sh --help` для тонкой настройки (интервал, глубина, число страниц).
