начало:
    показать "Kolibri выполняет автоматическое обучение"
    переменная источник_1 = "docs/russian_response_steps.md"
    обучить связь "Десять шагов к устойчивым ответам Колибри на русском языке" -> "# Десять шагов к устойчивым ответам Колибри на русском языке\n\n1. **Расширить кодировку символов для кириллицы.**\n   - Проверить, что `KolibriSymbolTable` содержит устойчивые соответствия для всех букв русского алфавита, включая `ё`, а также пробел и базовую пунктуацию. При необходимости прогнать утилиту, добавляющую недостающие символы в геном.\n\n2. **Обуч..."
    переменная источник_2 = "docs/early_access_program.md"
    обучить связь "Kolibri Early Access Program (EAP)" -> "# Kolibri Early Access Program (EAP)\n\n## 1. Цели программы\n- Получить обратную связь от 3–5 ключевых клиентов (финансы, промышленность, госсектор).\n- Валидировать ROI: сокращение времени поиска знаний, соблюдение требований локализации.\n- Подготовить публичные кейсы и тестовые данные для маркетинга и инвесторов.\n\n## 2. Пакет участника\n- Kolibri Enterprise..."
    переменная источник_3 = "docs/project_plan.md"
    обучить связь "Kolibri OS Release Plan" -> "# Kolibri OS Release Plan\n\n## Vision\nKolibri OS aims to deliver a lightweight, reliable, and scriptable operating environment with first-class support for KolibriScript applications and embedded simulators. The release will provide developers with a cohesive toolchain, validated runtime, and comprehensive documentation so that they can build and deploy Ko..."
    переменная источник_4 = "docs/user_guide.md"
    обучить связь "Kolibri User Guide / Руководство пользователя Kolibri" -> "# Kolibri User Guide / Руководство пользователя Kolibri\n\n## 1. Introduction / Введение\n\nKolibri is a distributed knowledge engine that executes KolibriScript programs\nand serves responses via a web UI. This guide helps end users install the stack,\nsend their first requests, and update the system safely.\n\nКолибри — распределённый движок знаний, исполняющий..."
    переменная источник_5 = "docs/web_interface.md"
    обучить связь "Web Interface & WASM Bridge / Веб-интерфейс и WASM-мост / Web 界面与 WASM 桥接" -> "# Web Interface & WASM Bridge / Веб-интерфейс и WASM-мост / Web 界面与 WASM 桥接\n\n**Copyright (c) 2025 Кочуров Владислав Евгеньевич**\n\n---\n\n## 1. Goals / Цели / 目标\n\n- Предоставить визуализацию фрактальной памяти и формул.\n- Поддержать офлайн-режим (PWA) и запуск ядра в браузере.\n- Обеспечить обмен данными между React-компонентами и WebAssembly.\n\n---\n\n## 2. Pro..."
    переменная источник_6 = "docs/project_status.md"
    обучить связь "Отчёт о завершении проекта Kolibri AI (Фазы 1–5)" -> "# Отчёт о завершении проекта Kolibri AI (Фазы 1–5)\n\n## Текущее состояние\n- Ядро Kolibri (Фаза 1) реализовано на C11: десятичная когниция, эволюция формул, цифровой геном, REPL-узел с командами `:teach`, `:ask`, `:tick`, `:evolve`, `:why`, `:canvas`, `:sync`, `:verify`.\n- Сетевой стек и протокол Kolibri Swarm (Фаза 2) активны: имеются UDP-потоки, обмен кад..."
    переменная источник_7 = "docs/kolibri_os.md"
    обучить связь "Kolibri OS Overview / Обзор Kolibri OS / Kolibri OS 概览" -> "# Kolibri OS Overview / Обзор Kolibri OS / Kolibri OS 概览\n\n**Copyright (c) 2025 Кочуров Владислав Евгеньевич**\n\n---\n\n## 1. Mission / Миссия / 使命\n\nОбеспечить автономный запуск ядра Kolibri на минимальном оборудовании без зависимости от полноценных ОС.\n\n---\n\n## 2. Boot Sequence / Последовательность загрузки / 启动流程\n\n1. BIOS передаёт управление загрузчику `kol..."
    переменная источник_8 = "docs/boevoy_roadmap_ru.md"
    обучить связь "Боевой план эволюции ИИ «Колибри»" -> "# Боевой план эволюции ИИ «Колибри»\n\nЭтот документ фиксирует десять первоочередных задач, необходимых для того, чтобы экспериментальный прототип превратился в боеспособную экосистему роя «Колибри», живущего по Четырём Великим Законам.\n\n## 1. Кристаллизация десятичного ядра ввода\n- **Суть:** завершить переход всех путей ввода (CLI, сетевые пакеты, файловые..."
    переменная источник_9 = "docs/adr/ADR-0001-template.md"
    обучить связь "ADR-0001: <краткое имя решения>" -> "# ADR-0001: <краткое имя решения>\n\nДата: YYYY-MM-DD\nСтатус: proposed | accepted | superseded | rejected\n\n## Контекст\nОпишите проблему или драйвер, заставившие принять решение.\n\n## Решение\nДетали принятого подхода и причины выбора.\n\n## Последствия\nПоложительные и отрицательные эффекты, риски и потребности в миграции.\n\n## Ссылки\nСвязанные PR/Issue, документ..."
    переменная источник_10 = "docs/architecture.md"
    обучить связь "Kolibri Architecture / Архитектура Kolibri / Kolibri 架构" -> "# Kolibri Architecture / Архитектура Kolibri / Kolibri 架构\n\n**Copyright (c) 2025 Кочуров Владислав Евгеньевич**\n\n---\n\n## 1. Overview / Обзор / 概览\n\n### Русский\nKolibri — это модульная система, в которой каждая подсистема реализована в чистом C и связана предсказуемыми интерфейсами. Ключевые слои:\n1. **Decimal Cognition** (`backend/src/decimal.c`) — преобраз..."
    переменная источник_11 = "docs/packaging_guide.md"
    обучить связь "Kolibri Packaging Guide / Руководство по упаковке / 发布打包指南" -> "# Kolibri Packaging Guide / Руководство по упаковке / 发布打包指南\n\n## Containers / Контейнеры / 容器\n\n| Component | Dockerfile | Image Tag |\n|-----------|------------|-----------|\n| Backend API | `backend/Dockerfile` | `ghcr.io/kolibri/kolibri-backend:<version>` |\n| Knowledge Indexer | `apps/Dockerfile.indexer` | `ghcr.io/kolibri/kolibri-indexer:<version>` |\n| F..."
    переменная источник_12 = "docs/live_learning.md"
    обучить связь "Kolibri Live Knowledge Loop" -> "# Kolibri Live Knowledge Loop\n\n## 1. Overview\nЭтот документ описывает архитектуру «Live Knowledge Loop» — механизма, позволяющего Kolibri\nадаптироваться к новым вопросам во время диалога, не нарушая концепцию цифрового генома и\nуправляемого знания.\n\n## 2. Поток событий\n1. **Capture**\n   - Все неизвестные вопросы фиксируются в журнале (`kolibri_node --heal..."
    создать формулу ответ из "ассоциация"
    вызвать эволюцию
    показать "Автоматическое обучение завершено"
конец.

начало:
    показать "Kolibri начинает тренировку математике и диалогам"

    # Математические ассоциации
    обучить связь "1+1" -> "2"
    обучить связь "2+3" -> "5"
    обучить связь "5*6" -> "30"
    обучить связь "квадрат пяти" -> "25"

    # Диалоговые паттерны
    обучить связь "привет" -> "здравствуй, чем могу помочь?"
    обучить связь "как тебя зовут?" -> "я Kolibri — цифровой исследователь"
    обучить связь "спасибо" -> "рада помочь, продолжим обучение?"

    создать формулу ответ из "ассоциация"

    вызвать эволюцию
    оценить ответ на задаче "привет"
    если фитнес ответ > 0.6 тогда
        сохранить ответ в геном
    иначе
        отбросить ответ
    конец

    оценить ответ на задаче "5*6"
    если фитнес ответ > 0.6 тогда
        сохранить ответ в геном
    конец

    показать "Тренировка завершена"
конец.
