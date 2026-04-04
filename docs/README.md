# Kolibri Documentation Index / Индекс документации Kolibri / Kolibri 文档索引

**Copyright (c) 2025 Кочуров Владислав Евгеньевич**

## Русский

Этот каталог содержит полную подборку документации прототипа Kolibri. Каждая запись дополнена кратким описанием, чтобы упростить навигацию:

### Официальный публичный набор

- [PUBLIC_ARCHITECTURE.md](PUBLIC_ARCHITECTURE.md) — главный публичный архитектурный документ Kolibri.
- [PRODUCT_SPEC_V2.md](PRODUCT_SPEC_V2.md) — каноническая продуктовая спецификация chat-first приложения.
- [FORMULA_API_DSL.md](FORMULA_API_DSL.md) — публичный Formula API и честный DSL, который реально исполняется в C-core.
- [KPACK_FORMAT.md](KPACK_FORMAT.md) — пакетный формат знаний `.kpack` как продуктовая единица.
- [DEMO_PATH.md](DEMO_PATH.md) — один стабильный demo-path Kolibri.
- [API_REFERENCE.md](API_REFERENCE.md) — боевые API-контракты.
- [SWARM_50_ARCHITECTURE.md](SWARM_50_ARCHITECTURE.md) — целевая архитектура production-сворма из 50 узлов.
- [NUMERIC_VOTING_MODEL.md](NUMERIC_VOTING_MODEL.md) — модель голосования цифр `0..9`.
- [MORPHOLOGY_SEMANTICS_SPEC.md](MORPHOLOGY_SEMANTICS_SPEC.md) — спецификация морфологии и семантики.
- [QA_ACCEPTANCE.md](QA_ACCEPTANCE.md) — обязательные acceptance gates.
- [DEPLOY_RUNBOOK.md](DEPLOY_RUNBOOK.md) — единый runbook для home server и production.
- [public_interfaces.md](public_interfaces.md) — стабильные публичные интерфейсы.

### Глубокие внутренние справочники

- [plans/ROADMAP_SINGLE_SOURCE_OF_TRUTH.md](plans/ROADMAP_SINGLE_SOURCE_OF_TRUTH.md) — единый подтверждённый roadmap проекта с честными статусами `implemented/in_progress/planned`.
- [archive/unconfirmed_reports/README.md](archive/unconfirmed_reports/README.md) — архив отчётов и документов со статусом «требует повторной верификации».
- [kolibri_integrated_prototype.md](kolibri_integrated_prototype.md) — обзорный научный документ с изложением концепции, экспериментов и результатов.
- [master_prompt.md](master_prompt.md) — мастер-промпт «Prometheus», определяющий философию и дорожную карту.
- [architecture.md](architecture.md) — архитектурная модель ядра, подсистем и потоков данных.
- [developer_guide.md](developer_guide.md) — инструкция по сборке, тестированию и стандартам разработки.
- [swarm_protocol.md](swarm_protocol.md) — спецификация бинарного протокола роя Kolibri.
- [decimal_cognition.md](decimal_cognition.md) — описание слоя десятичного кодирования и API `k_encode_text`/`k_decode_text`.
- [formula_evolution.md](formula_evolution.md) — руководство по эволюции формул и работе пула `KolibriFormulaPool`.
- [genome_chain.md](genome_chain.md) — спецификация цифрового генома и структуры `ReasonBlock`.
- [kolibri_os.md](kolibri_os.md) — документация минимальной Kolibri OS и сценариев загрузки.
- [web_interface.md](web_interface.md) — детали PWA/Canvas-интерфейса и WebAssembly-моста.
- [research_agenda.md](research_agenda.md) — планы экспериментов, метрики и научная повестка.

## English

This directory aggregates every documentation artifact required for the Kolibri prototype. Quick references are listed below:

### Official public set

- [PUBLIC_ARCHITECTURE.md](PUBLIC_ARCHITECTURE.md) — official public architecture for Kolibri.
- [PRODUCT_SPEC_V2.md](PRODUCT_SPEC_V2.md) — canonical product specification for the chat-first application.
- [FORMULA_API_DSL.md](FORMULA_API_DSL.md) — public Formula API and the actual DSL executed by the C core.
- [KPACK_FORMAT.md](KPACK_FORMAT.md) — `.kpack` knowledge-pack format as a product unit.
- [DEMO_PATH.md](DEMO_PATH.md) — one stable Kolibri demo-path.
- [API_REFERENCE.md](API_REFERENCE.md) — production API contracts.
- [SWARM_50_ARCHITECTURE.md](SWARM_50_ARCHITECTURE.md) — target production swarm architecture with 50 nodes.
- [NUMERIC_VOTING_MODEL.md](NUMERIC_VOTING_MODEL.md) — the `0..9` numeric voting model.
- [MORPHOLOGY_SEMANTICS_SPEC.md](MORPHOLOGY_SEMANTICS_SPEC.md) — morphology and semantics specification.
- [QA_ACCEPTANCE.md](QA_ACCEPTANCE.md) — release acceptance gates.
- [DEPLOY_RUNBOOK.md](DEPLOY_RUNBOOK.md) — home-server and production deploy runbook.
- [public_interfaces.md](public_interfaces.md) — stable public interfaces.

### Deep internal references

- [plans/ROADMAP_SINGLE_SOURCE_OF_TRUTH.md](plans/ROADMAP_SINGLE_SOURCE_OF_TRUTH.md) — single verified roadmap with honest `implemented/in_progress/planned` statuses.
- [archive/unconfirmed_reports/README.md](archive/unconfirmed_reports/README.md) — archive of reports/documents marked as "requires re-verification".
- [kolibri_integrated_prototype.md](kolibri_integrated_prototype.md) — comprehensive paper-style overview with methodology and experiments.
- [master_prompt.md](master_prompt.md) — "Prometheus" master prompt capturing philosophy and roadmap.
- [architecture.md](architecture.md) — system architecture for the core, subsystems, and data flows.
- [developer_guide.md](developer_guide.md) — build, testing, and development standards guide.
- [swarm_protocol.md](swarm_protocol.md) — binary swarm protocol specification.
- [decimal_cognition.md](decimal_cognition.md) — decimal cognition layer and the `k_encode_text`/`k_decode_text` APIs.
- [formula_evolution.md](formula_evolution.md) — formula evolution handbook and `KolibriFormulaPool` lifecycle.
- [genome_chain.md](genome_chain.md) — digital genome specification detailing the `ReasonBlock` structure.
- [kolibri_os.md](kolibri_os.md) — minimal Kolibri OS boot process documentation.
- [web_interface.md](web_interface.md) — PWA/Canvas UI and WebAssembly bridge documentation.
- [research_agenda.md](research_agenda.md) — experimental plans, metrics, and research agenda.

## 中文

此目录收录 Kolibri 原型所需的全部文档，并提供快速导航：

- [PUBLIC_ARCHITECTURE.md](PUBLIC_ARCHITECTURE.md) —— 官方公开架构文档。
- [PRODUCT_SPEC_V2.md](PRODUCT_SPEC_V2.md) —— 官方产品规格说明。
- [FORMULA_API_DSL.md](FORMULA_API_DSL.md) —— 公开 Formula API 与实际可执行 DSL。
- [KPACK_FORMAT.md](KPACK_FORMAT.md) —— `.kpack` 知识包格式。
- [DEMO_PATH.md](DEMO_PATH.md) —— 官方稳定演示路径。
- [API_REFERENCE.md](API_REFERENCE.md) —— 当前生产 API 合同。
- [SWARM_50_ARCHITECTURE.md](SWARM_50_ARCHITECTURE.md) —— 50 节点群体架构。
- [NUMERIC_VOTING_MODEL.md](NUMERIC_VOTING_MODEL.md) —— `0..9` 数字投票模型。
- [MORPHOLOGY_SEMANTICS_SPEC.md](MORPHOLOGY_SEMANTICS_SPEC.md) —— 形态学与语义规格。
- [QA_ACCEPTANCE.md](QA_ACCEPTANCE.md) —— 发布验收标准。
- [DEPLOY_RUNBOOK.md](DEPLOY_RUNBOOK.md) —— 家庭服务器与生产部署手册。
- [public_interfaces.md](public_interfaces.md) —— 稳定公开接口。

- [kolibri_integrated_prototype.md](kolibri_integrated_prototype.md) —— 论文式综述，涵盖方法论与实验。
- [master_prompt.md](master_prompt.md) —— “Prometheus” 主提示词，阐述理念与路线。
- [architecture.md](architecture.md) —— 核心系统、子系统与数据流的架构说明。
- [developer_guide.md](developer_guide.md) —— 构建、测试与开发规范指引。
- [swarm_protocol.md](swarm_protocol.md) —— Kolibri 群体二进制协议规范。
- [decimal_cognition.md](decimal_cognition.md) —— 十进制认知层与 `k_encode_text`/`k_decode_text` API 说明。
- [formula_evolution.md](formula_evolution.md) —— 公式进化手册与 `KolibriFormulaPool` 生命周期。
- [genome_chain.md](genome_chain.md) —— 数字基因组与 `ReasonBlock` 结构规范。
- [kolibri_os.md](kolibri_os.md) —— 最小 Kolibri OS 启动流程文档。
- [web_interface.md](web_interface.md) —— PWA/Canvas 界面与 WebAssembly 桥接说明。
- [research_agenda.md](research_agenda.md) —— 实验计划、指标与研究议程。
