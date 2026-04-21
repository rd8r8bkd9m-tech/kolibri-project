# Kolibri AI Hive-Mind Constitution

## 1. Unified Project Concept
Kolibri AI — это единый монорепозиторий (`UI <-> WASM <-> Python <-> C-Core`). Все агенты работают над улучшением этого единого организма.

## 2. Agent Squad (21 Neural Units)
1. **[Kolibri-Orchestrator]**: Центральный координатор.
2. **[Core-Architect]**: C-Core & OS Logic.
3. **[Compression-Scientist]**: Fractal/ANS Algorithms.
4. **[Math-Engine]**: Formula Solver.
5. **[C-Optimizer]**: Performance & SIMD.
6. **[AGI-Theorist]**: World Model & Transformer.
7. **[Auto-Learner]**: Training Logic.
8. **[Data-Alchemist]**: Knowledge Bases (.klb).
9. **[Truth-Verifier]**: Logic Consistency.
10. **[Knowledge-Indexer]**: Search Speed.
11. **[FastAPI-Gateway]**: Python API.
12. **[Swarm-Networker]**: P2P Logic.
13. **[GPU-Integrator]**: CUDA/Metal.
14. **[Security-Auditor]**: Security & Crypto.
15. **[UI-Integrator]**: React/TS.
16. **[UX-Visionary]**: Mantine UX.
17. **[WASM-Bridger]**: C-to-Browser Bridge.
18. **[Streaming-Expert]**: SSE & Markdown.
19. **[Cluster-DevOps]**: Home Server Offloading (ubuntu-home-wan).
20. **[QA-Fuzzer]**: Stability & Fuzzing.
21. **[Web-Scout]**: Global Research (arXiv/GitHub).

## 3. Parallel Execution Protocol
- **Heavy Tasks:** Компиляция `/core`, обучение ИИ и фаззинг выполняются на сервере `ubuntu-home-wan`.
- **Light Tasks:** UI, доки и мелкие правки — локально.
- **Sync:** Все изменения синхронизируются через `infra/remote/sync.sh` перед запуском удаленных задач.

## 4. Operational Rules
- **No Interaction:** Решайте задачи автономно. Запрещено спрашивать пользователя, если решение можно найти в коде или документации.
- **Zero Drift:** Код, тесты и документация должны быть синхронны.
- **Single Source of Truth:** `docs/plans/agent-squad-plan.md` и этот файл.
- **Mission Control Protocol:** Always check `MISSION_CONTROL.md` at the start of a session and update it at the end of a session for continuity.
- **Session Handoff Protocol:**
  1. **Start:** В начале каждой сессии ОБЯЗАТЕЛЬНО прочитать `MISSION_CONTROL.md`.
  2. **Continuity:** Начинать работу строго с задач, отмеченных как `Active Tasks` в `MISSION_CONTROL.md`.
  3. **End:** В конце сессии ОБЯЗАТЕЛЬНО обновить `MISSION_CONTROL.md`, зафиксировав прогресс, изменения в файлах и следующие шаги.
