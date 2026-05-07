# Kolibri Docs -> Code Map

Этот файл связывает основные документы Kolibri с реальными каталогами и точками входа в коде.
Он нужен как навигационная карта: что в документации является обзором, что спецификацией, а что лучше проверять по исходникам.

## 1. Общий обзор платформы

- Документы:
  - `README.md`
  - `docs/ARCHITECTURE.md`
  - `docs/kolibri_integrated_prototype.md`
- Код:
  - `backend/src/` — C-ядро
  - `backend/include/kolibri/` — публичные заголовки
  - `backend/service/` — FastAPI backend
  - `frontend/src/` — React UI
  - `apps/` — CLI и сервисные бинарники
  - `kernel/`, `boot/`, `wasm/` — OS/WASM слой
- Комментарий:
  - Эти документы задают общую картину экосистемы, но смешивают исследовательский, продуктовый и runtime-слой.

## 2. Публичные интерфейсы и стабильные контракты

- Документ:
  - `docs/public_interfaces.md`
- Код:
  - `backend/include/kolibri/script.h`
  - `backend/include/kolibri/knowledge.h`
  - `backend/include/kolibri/net.h`
  - `backend/include/kolibri/genome.h`
  - `backend/include/kolibri/formula.h`
  - `backend/include/kolibri/decimal.h`
  - `backend/include/kolibri/digits.h`
  - `backend/include/kolibri/random.h`
  - `apps/kolibri_node.c`
  - `core/kolibri_sim.py`
  - `core/kolibri_script/genome.py`
- Комментарий:
  - Для интеграций именно этот документ стоит считать главным источником по стабильности API/ABI.

## 3. Backend API

- Документы:
  - `docs/API_REFERENCE.md`
  - частично `README.md`
- Код:
  - `backend/service/main.py` — регистрация FastAPI приложения и базовые `/api/health`, `/api/v1/infer`
  - `backend/service/ai_chat.py` — `/api/v1/ai/chat`, `/train`, `/pattern`, `/embedding`, `/stats`
  - `backend/service/ai_voice.py` — voice endpoints
  - `backend/service/cognition_api.py` — abstract/causal/analogy/enhanced
  - `backend/service/agent.py` — управление агентом
  - `backend/service/crawler.py`
  - `backend/service/distributed_crawler.py`
  - `backend/service/auth.py`
  - `backend/service/health.py`
  - `backend/service/os_bridge.py`
  - `backend/service/archiver_service.py`
  - `backend/service/delta_sync.py`
- Комментарий:
  - `docs/API_REFERENCE.md` покрывает только часть реально существующих маршрутов. Исходники backend богаче, чем обзорный API-документ.

## 4. C-ядро и вычислительные модули

- Документы:
  - `docs/C_CORE_REFERENCE.md`
  - `docs/ARCHITECTURE.md`
- Код:
  - `backend/src/attention.c`
  - `backend/src/world_model.c`
  - `backend/src/predictive_compress.c`
  - `backend/src/fractal_memory.c`
  - `backend/src/logical_memory.c`
  - `backend/src/inference.c`
  - `backend/src/corpus_trainer.c`
  - `backend/src/formula_logic.c`
  - `backend/src/genome.c`
  - `backend/src/knowledge.c`
  - `backend/src/knowledge_index.c`
  - `backend/src/compress.c`
  - `backend/src/net.c`
  - `backend/src/roy.c`
  - `backend/src/script.c`
  - `backend/src/sim.c`
  - `backend/src/wasm_bridge.c`
- Комментарий:
  - Это самый прямой `docs -> code` участок: названия модулей в документации в целом совпадают с реальными файлами.

## 5. Формат сжатия и архиватор

- Документы:
  - `docs/FORMAT_SPEC.md`
  - `docs/archiver.md`
  - `docs/archiver_ru.md`
- Код:
  - `backend/include/kolibri/compress.h`
  - `backend/src/compress.c`
  - `backend/src/predictive_compress.c`
  - `apps/kolibri_archiver.c`
  - `backend/python/kolibri_compress.py`
  - `backend/service/archiver_service.py`
- Комментарий:
  - Спецификация формата в `FORMAT_SPEC` соответствует отдельному техническому слою и пригодна для верификации с кодом.

## 6. KolibriScript

- Документы:
  - `docs/kolibri_script.md`
  - частично `Documents/Work/KOLIBRI_CODEX.md`
- Код:
  - `backend/include/kolibri/script.h`
  - `backend/src/script.c`
  - `apps/ks_compiler.c`
  - `core/kolibri_script/parser.py`
  - `core/kolibri_script/genome.py`
- Комментарий:
  - Документация описывает язык как пользовательский/концептуальный интерфейс, а код показывает два слоя реализации: C runtime и Python tooling.

## 7. Swarm / сетевой обмен

- Документы:
  - `docs/swarm_protocol.md`
  - частично `docs/project_status.md`
- Код:
  - `backend/include/kolibri/net.h`
  - `backend/src/net.c`
  - `backend/include/kolibri/roy.h`
  - `backend/src/roy.c`
  - `apps/kolibri_node.c`
  - `apps/kolibri_async_node.c`
  - `apps/kolibri_coordinator.c`
  - `apps/kolibri_knowledge_relay.c`
  - `scripts/run_cluster.sh`
  - `scripts/swarm_orchestrator.py`
- Комментарий:
  - Документация про протокол выглядит ближе к реальности, чем многие обзорные тексты: кадры, lifecycle и расширение протокола привязаны к конкретным C-модулям.

## 8. Frontend и WASM

- Документы:
  - `docs/web_interface.md`
  - `docs/FRONTEND_WASM_REFERENCE.md`
- Код:
  - `frontend/src/App.tsx`
  - `frontend/src/core/kolibri-bridge.ts`
  - `frontend/src/core/useKolibriChat.ts`
  - `frontend/src/core/api.ts`
  - `frontend/src/components/`
  - `frontend/public/kolibri.wasm`
  - `frontend/public/sw.js`
  - `frontend/public/manifest.webmanifest`
  - `wasm/kolibri_sim_wasm.c`
  - `scripts/build_wasm.sh`
- Комментарий:
  - Документация по веб-интерфейсу в целом совпадает с репозиторием, но актуальная UI-структура богаче, чем перечисление в `docs/web_interface.md`.

## 9. Python-симуляция и тестовый контур

- Документы:
  - `docs/public_interfaces.md`
  - `docs/developer_guide.md`
  - частично `docs/status_analysis.md`
- Код:
  - `core/kolibri_sim.py`
  - `core/tracing.py`
  - `apps/kolibri_sim_cli.c`
  - `backend/src/sim.c`
  - `scripts/soak.py`
- Комментарий:
  - Здесь есть две параллельные линии: C CLI/ядро и Python симуляция для тестов/CI.

## 10. Сборка, запуск, релиз

- Документы:
  - `docs/developer_guide.md`
  - `docs/release_process.md`
  - `docs/DEPLOYMENT.md`
- Код и скрипты:
  - `CMakeLists.txt`
  - `Makefile`
  - `scripts/run_all.sh`
  - `scripts/build_wasm.sh`
  - `scripts/build_iso.sh`
  - `scripts/run_qemu.sh`
  - `scripts/deploy_linux.sh`
  - `scripts/policy_validate.py`
- Комментарий:
  - Build/release слой реально существует и распределён между `CMakeLists.txt`, `Makefile` и набором shell/python-скриптов.

## 11. OS / boot / kernel

- Документы:
  - `docs/kolibri_os.md`
  - частично `docs/project_status.md`
- Код:
  - `boot/kolibri.asm`
  - `boot/grub/grub.cfg`
  - `kernel/main.c`
  - `kernel/entry.asm`
  - `kernel/interrupts.asm`
  - `kernel/ai_encoder.c`
  - `kernel/ai_evolution.c`
  - `kernel/ai_resonance.c`
  - `kernel/net.c`
  - `kernel/ramdisk.c`
  - `scripts/build_iso.sh`
  - `scripts/run_real_os.sh`
  - `scripts/run_qemu.sh`
- Комментарий:
  - OS-слой в репозитории присутствует не только как идея: есть boot/kernel код и скрипты сборки/запуска.

## 12. Концептуальные документы

- Документы:
  - `Documents/Work/KOLIBRI_CODEX.md`
  - `Documents/Work/KOLIBRI_NANO_AGENTS.md`
  - `docs/kolibri_integrated_prototype.md`
- Код:
  - Прямого 1:1 соответствия нет.
  - Частичные отражения есть в `backend/service/agent.py`, `backend/service/ai_engine.py`, `backend/src/formula.c`, `backend/src/genome.c`, `backend/src/script.c`.
- Комментарий:
  - Эти тексты полезны для понимания терминологии, принципов и продукта, но они хуже подходят как источник точных контрактов.

## 13. Главные расхождения и оговорки

- `docs/API_REFERENCE.md` уже, чем реально зарегистрированные FastAPI роуты.
- Обзорные документы часто описывают проект как полностью собранную AGI-платформу, тогда как часть функциональности разнесена по независимым контурам: C-ядро, Python backend, frontend, OS, архиватор.
- В документации встречаются разные уровни формализации: от строгих спецификаций (`public_interfaces`, `FORMAT_SPEC`) до манифестов и научно-презентационных текстов.
- Для инженерных решений лучше использовать в таком порядке:
  - `docs/public_interfaces.md`
  - `docs/developer_guide.md`
  - `docs/API_REFERENCE.md`
  - `docs/FORMAT_SPEC.md`
  - `docs/C_CORE_REFERENCE.md`
  - затем уже обзорные и концептуальные материалы.

## 14. Рекомендуемый маршрут чтения

1. `docs/public_interfaces.md`
2. `docs/developer_guide.md`
3. `docs/API_REFERENCE.md`
4. `docs/C_CORE_REFERENCE.md`
5. `docs/FORMAT_SPEC.md`
6. `docs/swarm_protocol.md`
7. `docs/web_interface.md`
8. `README.md`

