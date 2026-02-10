# Деплой и скрипты Kolibri OS

## Деплой

### Локальная разработка

```bash
# Backend (порт 8001)
python -m uvicorn backend.service.main:app --host 0.0.0.0 --port 8001

# Frontend (порт 3000)
cd frontend && npx vite --host 0.0.0.0 --port 3000

# Или всё одной командой
./scripts/run_kolibri_stack.sh
```

### Docker Compose

```bash
cd deploy
docker-compose up -d
```

Сервисы:
- `backend` — FastAPI на порту 8001
- `frontend` — Vite/React на порту 3000
- `training-worker` — Фоновое обучение (опционально)

### Kubernetes

```bash
# Namespace
kubectl apply -f deploy/k8s/namespace.yaml

# Компоненты
kubectl apply -f deploy/k8s/backend.yaml
kubectl apply -f deploy/k8s/frontend.yaml

# CronJob для автоматического обучения
kubectl apply -f deploy/k8s/training-cronjob.yaml
```

### Мониторинг

| Инструмент | Конфигурация | Описание |
|-----------|-------------|----------|
| Prometheus | `deploy/monitoring/prometheus.yml` | Сбор метрик |
| Grafana | `deploy/monitoring/grafana_dashboard.json` | Визуализация |

### Переменные окружения

| Переменная | Описание | По умолчанию |
|------------|----------|-------------|
| `KOLIBRI_RESPONSE_MODE` | Режим ответа (`script` / `llm`) | `script` |
| `KOLIBRI_LLM_ENDPOINT` | URL внешнего LLM | — |
| `KOLIBRI_LLM_MODEL` | Имя модели LLM | — |
| `KOLIBRI_LLM_API_KEY` | API-ключ LLM | — |
| `KOLIBRI_SKIP_WASM_AUTOBUILD` | Пропустить сборку WASM | — |
| `KOLIBRI_ALLOW_WASM_STUB` | Разрешить WASM-заглушку | — |

---

## Скрипты (`scripts/`)

### Сборка

| Скрипт | Описание |
|--------|----------|
| `build_wasm.sh` | Сборка WASM (Emscripten → `build/wasm/kolibri.wasm`) |
| `build_iso.sh` | Сборка ISO-образа ОС (GRUB + kernel) |
| `build_summary_module.py` | Генерация сводки модулей |
| `package_release.sh` | Упаковка релиза |

### Деплой

| Скрипт | Описание |
|--------|----------|
| `deploy_linux.sh` | Деплой на Linux |
| `deploy_macos.sh` | Деплой на macOS |
| `deploy_windows.ps1` | Деплой на Windows (PowerShell) |

### Обучение

| Скрипт | Описание |
|--------|----------|
| `auto_train.sh` | Автоматическое обучение (`--ticks 500`) |
| `train_corpus.py` | Обучение на корпусе текстов |
| `llm_teacher.py` | Обучение от внешнего LLM (дистилляция) |
| `ingest_knowledge.py` | Загрузка знаний (файлы → модель) |
| `knowledge_pipeline.sh` | Полный конвейер знаний |

### Рой (Swarm)

| Скрипт | Описание |
|--------|----------|
| `run_swarm_10.sh` | Рой из 10 узлов |
| `run_swarm_50.sh` | Рой из 50 узлов |
| `run_swarm_1000.sh` | Рой из 1000 узлов |
| `run_swarm_100k.sh` | Рой из 100000 узлов |
| `mega_swarm_highperf.sh` | Высокопроизводительный рой |
| `swarm_orchestrator.py` | Python-оркестратор роя |
| `swarm_exchange.py` | Обмен знаниями между узлами |

### Доказательства (Proof)

| Скрипт | Описание |
|--------|----------|
| `proof_1000nodes.sh` | Доказательство работы 1000 узлов |
| `proof_64genome.sh` | Доказательство 64-цифрового генома |
| `prove_learning.sh` | Доказательство обучения |
| `run_proof.py` | Python-запуск доказательств |

### Краулинг

| Скрипт | Описание |
|--------|----------|
| `kolibri_crawler.py` | Python веб-краулер |
| `kolibri_fetch_docs.py` | Загрузка документации |
| `collect_seeds.py` | Сбор seed-URL для обучения |

### Анализ

| Скрипт | Описание |
|--------|----------|
| `spectral_fingerprint.py` | Спектральный анализ входных знаний |
| `kolibri_code_features.py` | Анализ кодовых фич |
| `kolibri_entity_relations.py` | Извлечение сущностей и связей |

### CI/CD

| Скрипт | Описание |
|--------|----------|
| `ci_bootstrap.sh` | Настройка CI-окружения |
| `policy_validate.py` | Валидация политик (размер WASM, тесты) |
| `post_pr_comment.py` | Комментарий к PR |
| `resolve_conflicts.py` | Разрешение конфликтов Git |

### Запуск

| Скрипт | Описание |
|--------|----------|
| `run_backend.sh` | Запуск backend |
| `run_kolibri_stack.sh` | Запуск всего стека (backend + frontend) |
| `run_cluster.sh` | Запуск кластера |
| `run_heavy_ai.sh` | Запуск с тяжёлым AI |
| `run_real_os.sh` | Запуск ОС |
| `run_qemu.sh` | Запуск в QEMU |
