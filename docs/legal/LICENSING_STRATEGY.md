# 🎯 KOLIBRI - СТРАТЕГИЯ ЛИЦЕНЗИРОВАНИЯ

**Автор:** Владислав Евгеньевич Кочуров  
**Страна:** Россия 🇷🇺  
**Сайт:** https://kolibriai.ru  
**Дата:** 12 ноября 2025 г.  
**Версия:** v1.0  
**Статус:** Коммерческий продукт

---

## 📋 РЕКОМЕНДУЕМЫЙ ПОДХОД

Для коммерческого продукта рекомендуется **двойное лицензирование (Dual Licensing)**:

```
┌─────────────────────────────────────────────────┐
│          KOLIBRI Licensing Model                │
├─────────────────────────────────────────────────┤
│                                                 │
│  ОПЦИЯ 1: Коммерческая лицензия                │
│  ├─ Для компаний и предприятий                 │
│  ├─ Платная (от $10K/год)                      │
│  ├─ Полная поддержка и SLA                     │
│  ├─ Никаких ограничений на распространение     │
│  └─ Исходный код НЕ нужно распространять       │
│                                                 │
│  ОПЦИЯ 2: Открытая лицензия (GPL/AGPL)        │
│  ├─ Для open-source проектов                   │
│  ├─ Бесплатно                                  │
│  ├─ Исходный код открыт                        │
│  └─ Модификации нужно делиться                 │
│                                                 │
│  ОПЦИЯ 3: Лицензия сообщества (Community)      │
│  ├─ Для стартапов/студентов                    │
│  ├─ Бесплатно (с согласием)                    │
│  ├─ Исходный код закрыт                        │
│  └─ Лимиты на использование                    │
│                                                 │
└─────────────────────────────────────────────────┘
```

---

## 🏢 ВАРИАНТ 1: КОММЕРЧЕСКАЯ ЛИЦЕНЗИЯ

### Структура

```
LICENSE-COMMERCIAL.md
├─ Права (what you get)
├─ Ограничения (what you cannot do)
├─ Условия (terms & conditions)
└─ Support & SLA (гарантии)
```

### Текст лицензии

```
KOLIBRI COMMERCIAL LICENSE AGREEMENT
Version 1.0 (November 2025)

GRANT OF RIGHTS
==============

Subject to the terms and conditions of this Agreement, Licensor grants 
Licensee a non-exclusive, non-transferable right to:

1. Use the Software in commercial and internal business purposes
2. Create derivative works
3. Deploy on unlimited number of servers/instances
4. Distribute the Software as part of your product (binary-only)
5. Modify source code for internal use

RESTRICTIONS
============

You may NOT:

1. Reverse engineer or decompile the Software
2. Remove or alter copyright/license notices
3. Transfer/sublicense rights to third parties
4. Use for competing data compression products
5. Publish benchmarks without written consent

PAYMENT TERMS
=============

License Fee: $10,000 - $250,000 per year (depending on company size)
                OR
Revenue share: 2-5% (for SaaS/Cloud products)

Tiers:
- Startup:     < $1M revenue  → $10K/year
- SMB:         $1M-$10M      → $50K/year
- Enterprise:  > $10M        → $250K/year

SUPPORT & SLA
=============

- Priority support: 24/7
- Response time: < 4 hours (critical issues)
- Bug fixes: within 2 weeks
- Uptime SLA: 99.9%

TERM & TERMINATION
==================

- Initial term: 1 year
- Auto-renewal annually
- Either party can terminate with 30 days notice
- Upon termination: license expires, but existing deployments continue

LIMITATION OF LIABILITY
=======================

Licensor is NOT liable for:
- Indirect, incidental, consequential damages
- Lost profits or business interruption
- Third-party claims

Maximum liability: Amount paid under this Agreement

DISCLAIMER
==========

SOFTWARE PROVIDED "AS-IS" WITHOUT WARRANTIES OF ANY KIND

GOVERNING LAW
=============

This Agreement governed by laws of [Your Jurisdiction]
```

---

## 🔓 ВАРИАНТ 2: OPEN-SOURCE ЛИЦЕНЗИЯ (Гибридная)

Для open-source компонентов рекомендуется **AGPL-3.0**:

### Почему AGPL?

| Лицензия | Исходный код | Network Use | Business Model |
|----------|-------------|-------------|----------------|
| MIT | Открыт | Нет требований | Любой |
| GPL | Открыт | Нет требований | Open-source |
| **AGPL** | **Открыт** | **Требует раскрытия** | **SaaS лучше** |
| Proprietary | Закрыт | Нет требований | Коммерческий |

**AGPL идеальна для:**
- SaaS приложений (API доступны = используется сетевое взаимодействие)
- Облачных сервисов
- Защиты от присвоения

### AGPL-3.0 текст

```
KOLIBRI - GNU AFFERO GENERAL PUBLIC LICENSE v3

Copyright (c) 2025 Kolibri Contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as
published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

KEY TERMS:
==========

1. SOURCE CODE
   You must provide source code to anyone who interacts with your
   program over a network.

2. MODIFICATIONS
   Any changes you make must be distributed under same license.

3. PATENT RIGHTS
   Contributors grant you patent rights to their contributions.

4. COMPATIBILITY
   Compatible with: GPL v3 only
   NOT compatible with: Apache 2.0, MIT, BSD

For full license text, see:
https://www.gnu.org/licenses/agpl-3.0.html
```

---

## 💼 ВАРИАНТ 3: COMMUNITY EDITION ЛИЦЕНЗИЯ

Для привлечения разработчиков:

```
KOLIBRI COMMUNITY LICENSE

FREE for:
=========
- Individual developers
- Students and educational institutions
- Open-source projects
- Non-profit organizations
- Startups (< $100K revenue)

TERMS:
======
1. Non-commercial use only
2. Limited to 1 deployment
3. Community support only (no SLA)
4. 20GB data limit
5. No production guarantee

UPGRADE TO COMMERCIAL:
=====================
When your revenue exceeds $100K, you must upgrade to:
- Commercial License ($10K+/year)
- OR Open-source with AGPL

For compliance check, annual audit required.
```

---

## 🎯 РЕКОМЕНДУЕМАЯ СТРАТЕГИЯ

### Фаза 1: СЕЙЧАС (v1.0.0)

**Лицензирование**: Переходная

```
src/decimal.c
src/logical_memory.c
src/formula_logic.c
    ↓
АГPL-3.0 (open-source)
```

**LICENSE файл**:
```
KOLIBRI - Dual License Model

1. COMMERCIAL LICENSE (Proprietary)
   For companies and enterprises
   See: LICENSE-COMMERCIAL.md

2. OPEN-SOURCE LICENSE (AGPL-3.0)
   For open-source projects
   See: LICENSE-AGPL.md

3. COMMUNITY LICENSE (Free)
   For developers, students, startups
   See: LICENSE-COMMUNITY.md

Choose one that fits your use case.
```

### Фаза 2: ALPHA (v1.1.0 - v2.0.0)

**Продвигать коммерческую лицензию:**
- Начните принимать платежи
- Ограничьте открытые компоненты
- Переместите IP в proprietary слой

### Фаза 3: BETA (v2.5.0)

**Разделение кода:**

```
/core/          → AGPL-3.0 (open)
├─ decimal.c
├─ logical_memory.c
└─ formula_logic.c

/api/           → Proprietary (commercial)
├─ rest_server.c
├─ db_layer.c
└─ auth.c

/ui/            → Proprietary (commercial)
├─ dashboard/
├─ cli/
└─ mobile/

/ml/            → Proprietary (commercial)
└─ pattern_detector.py
```

**Модель доходов:**
- Open-source core: привлекает разработчиков
- Proprietary features: зарабатывает деньги

### Фаза 4: PRODUCTION (v4.0.0)

**Полная коммерциализация:**

```
┌─────────────────────────────────────┐
│      KOLIBRI Enterprise v4.0.0      │
├─────────────────────────────────────┤
│                                     │
│  Core Engine (AGPL-3.0)            │
│  ├─ Compression algorithm ✓         │
│  └─ Decompression algorithm ✓       │
│                                     │
│  Enterprise Features ($)            │
│  ├─ Cloud synchronization           │
│  ├─ Web dashboard                   │
│  ├─ Mobile applications             │
│  ├─ Enterprise API                  │
│  ├─ Audit & compliance              │
│  ├─ 24/7 Support                    │
│  └─ SLA guarantees                  │
│                                     │
│  Revenue Model:                     │
│  - Free Community Edition           │
│  - $10K-$250K/year Commercial       │
│  - 2-5% Revenue share for SaaS      │
│                                     │
└─────────────────────────────────────┘
```

---

## 📝 ФАЙЛЫ ДЛЯ СОЗДАНИЯ

### 1. LICENSE-COMMERCIAL.md (Коммерческая)

```
KOLIBRI COMMERCIAL LICENSE AGREEMENT
Version 1.0 (2025)

GRANT OF RIGHTS:
- Право на использование в коммерческих целях
- Создание производных работ
- Развёртывание на неограниченное количество серверов
- Распространение в двоичной форме
- Изменение исходного кода

ОГРАНИЧЕНИЯ:
- Обратная разработка запрещена
- Передача лицензии не допускается
- Удаление уведомлений об авторских правах запрещено

СТОИМОСТЬ:
- Стартапы (< $1M):    $10,000/год
- SMB ($1M-$10M):      $50,000/год
- Enterprise (> $10M): $250,000/год

ПОДДЕРЖКА:
- 24/7 техническая поддержка
- SLA 99.9% uptime
- Приоритет на исправление ошибок

ДЕЙСТВИТЕЛЕН:
- Начальный срок: 1 год
- Автоматическое продление
```

### 2. LICENSE-AGPL.md (Open-source)

```
GNU AFFERO GENERAL PUBLIC LICENSE v3

Используется для:
- Open-source проектов
- Некоммерческого использования
- Проектов, распространяющих код

Ключевые условия:
- Исходный код должен быть открыт
- Модификации должны распространяться под AGPL
- Network use требует раскрытия исходного кода
```

### 3. LICENSE-COMMUNITY.md (Community)

```
KOLIBRI COMMUNITY LICENSE

БЕСПЛАТНО для:
- Отдельных разработчиков
- Студентов
- Некоммерческих организаций
- Open-source проектов
- Стартапов (< $100K revenue)

ОГРАНИЧЕНИЯ:
- Только некоммерческое использование
- 1 развёртывание
- Лимит 20 GB данных
- Только community поддержка
- Нет SLA гарантий

ОБНОВЛЕНИЕ:
Когда ваш доход превысит $100K, обновитесь на:
- Commercial License ($10K+/год)
- Или AGPL с открытием исходного кода
```

---

## 🔄 ПЕРЕХОД ОТ MIT К DUAL LICENSING

### Шаг 1: Подготовка файлов

```bash
# Создать новые лицензии
touch LICENSE-COMMERCIAL.md
touch LICENSE-AGPL.md
touch LICENSE-COMMUNITY.md

# Обновить главный LICENSE (как указатель)
echo "DUAL LICENSING MODEL - See LICENSE-* files" > LICENSE
```

### Шаг 2: Обновить заголовки файлов

```c
// БЫЛО (MIT):
/*
 * Copyright (c) 2025 Kolibri Contributors
 * Licensed under MIT License
 */

// ДОЛЖНО БЫТЬ (Dual):
/*
 * KOLIBRI - Hierarchical Data Abstraction System
 * 
 * DUAL LICENSE:
 * - Commercial: LICENSE-COMMERCIAL.md
 * - Open-source: AGPL-3.0 (see LICENSE-AGPL.md)
 * - Community: LICENSE-COMMUNITY.md
 * 
 * Copyright (c) 2025 Kolibri Contributors
 * All rights reserved.
 */
```

### Шаг 3: Обновить README

```markdown
## Лицензирование

KOLIBRI использует модель Dual Licensing:

### Коммерческая лицензия
Для компаний и предприятий - $10,000-$250,000/год
[Подробнее](LICENSE-COMMERCIAL.md)

### Open-source лицензия (AGPL-3.0)
Для open-source проектов - бесплатно
[Подробнее](LICENSE-AGPL.md)

### Community лицензия
Для разработчиков и студентов - бесплатно
[Подробнее](LICENSE-COMMUNITY.md)

Выберите лицензию, которая подходит вашему случаю.
```

### Шаг 4: Добавить в CONTRIBUTING.md

```markdown
## Лицензирование для контрибьюторов

Все контрибьюции к Kolibri должны быть:

1. Согласованы с Dual Licensing моделью
2. Подписаны Contributor License Agreement (CLA)
3. Лицензированы под той же лицензией

При выборе коммерческой лицензии, вы даёте право:
- Использовать ваш код в коммерческих целях
- Распространять без указания вас как автора
- Модифицировать без согласия
```

---

## 💰 ЦЕНОВАЯ МОДЕЛЬ

### Коммерческая лицензия

```
TIER 1: Startup Edition
- Компании с доходом < $1M
- Цена: $10,000/год
- Включает: Core engine, API, basic support
- Deployment: до 10 серверов

TIER 2: Business Edition
- Компании с доходом $1M-$10M
- Цена: $50,000/год
- Включает: Tier 1 + Web dashboard, CLI, 24/7 support
- Deployment: неограниченное

TIER 3: Enterprise Edition
- Компании с доходом > $10M
- Цена: $250,000/год
- Включает: Tier 2 + Mobile apps, Priority support, Custom integrations
- Deployment: неограниченное
- SLA: 99.99% + 4-hour response time

TIER 4: SaaS/Cloud
- Модель: 2-5% от месячного дохода
- Минимум: $1,000/месяц
- Включает: Все features, commercial API, белый ярлык
```

### Revenue Projections

```
Assumptions:
- 100 компаний в Year 1 (40% Startup, 50% SMB, 10% Enterprise)
- 200 компаний в Year 2 (рост 100%)
- 500 компаний в Year 3 (рост 150%)

Year 1 Revenue:
- 40 Startups × $10K = $400K
- 50 SMB × $50K = $2.5M
- 10 Enterprise × $250K = $2.5M
- TOTAL: $5.4M

Year 2 Revenue:
- 80 Startups × $10K = $800K
- 100 SMB × $50K = $5M
- 20 Enterprise × $250K = $5M
- TOTAL: $10.8M

Year 3 Revenue:
- 200 Startups × $10K = $2M
- 250 SMB × $50K = $12.5M
- 50 Enterprise × $250K = $12.5M
- TOTAL: $27M
```

---

## 📋 ЧЕКЛИСТ РЕАЛИЗАЦИИ

### Фаза 1: Немедленно

- [ ] Создать `LICENSE-COMMERCIAL.md`
- [ ] Создать `LICENSE-AGPL.md`
- [ ] Создать `LICENSE-COMMUNITY.md`
- [ ] Обновить заголовки файлов в коде
- [ ] Обновить `README.md` (лицензирование)
- [ ] Создать `CONTRIBUTING.md` с CLA
- [ ] Обновить `LICENSE` (указатель)

### Фаза 2: v1.1.0 (Неделя 3)

- [ ] Запустить коммерческую программу
- [ ] Собрать первые 10 платных клиентов
- [ ] Настроить счета и платежи
- [ ] Создать SLA документацию

### Фаза 3: v2.0.0 (Месяц 2)

- [ ] Разделить код (Core vs Enterprise)
- [ ] Выделить proprietary features
- [ ] Запустить Enterprise program

### Фаза 4: v4.0.0 (Месяц 6)

- [ ] Полная коммерциализация
- [ ] Partner программа
- [ ] Enterprise + SaaS модели

---

## 🎯 РЕКОМЕНДАЦИИ

**1. Начните с Dual Licensing СЕЙЧАС**
- Это даст вам гибкость
- Привлечет разработчиков (AGPL)
- Позволит зарабатывать (Commercial)

**2. Ограничьте core функциональность AGPL**
- Основные алгоритмы открыты
- Proprietary features закрыты
- Лучший баланс open/commercial

**3. Используйте SaaS как основной доход**
- Enterprise не платят часто
- Recurring revenue от облака надежнее
- Cloud-first стратегия

**4. Инвестируйте в юридическую защиту**
- Правильные лицензионные тексты
- CLA от всех контрибьюторов
- Товарный знак (TM) для "Kolibri"

---

## 📞 СЛЕДУЮЩИЕ ШАГИ

1. **Утвердить выбор лицензии** (вы выбираете)
   - Dual Licensing (рекомендуется)
   - Proprietary + Commercial
   - Другой вариант

2. **Создать лицензионные файлы**
   - Я создам все необходимые документы

3. **Обновить код и README**
   - Добавлю заголовки и ссылки

4. **Настроить платежи**
   - Stripe, PayPal, выставление счетов

5. **Запустить коммерческую программу**
   - Найти первых клиентов
   - Собрать feedback

**Выбранная стратегия**: ________________

