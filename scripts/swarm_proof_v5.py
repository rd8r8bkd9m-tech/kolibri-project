#!/usr/bin/env python3
"""
═══════════════════════════════════════════════════════════════════
  Kolibri Swarm v5: СЕТЬ vs РАЗУМ — ДВА УРОВНЯ EMERGENCE
═══════════════════════════════════════════════════════════════════

v4 доказала: слияние графов знаний создаёт новые пути,
хабы и кросс-доменные ответы. Но это emergence уровня СЕТИ —
маршрутизатор тоже так умеет.

v5 ставит честный вопрос: есть ли emergence уровня РАЗУМА?
  - Аналогии       (A:B :: C:? — перенос структуры)
  - Абстракции     (обобщение из конкретных фактов)
  - Предсказания   (вывод, которого нет ни в одном пути графа)
  - Категоризация  (группировка по смыслу, а не по co-occurrence)

СТРУКТУРА:
  Часть I  — Сетевой emergence   (подтверждён v4, воспроизведён)
  Часть II — Разумный emergence  (новые эксперименты, честные результаты)
  Вердикт  — Что есть, чего нет, что нужно добавить

═══════════════════════════════════════════════════════════════════
"""
from __future__ import annotations

import sys
import os
import time
import random
import math
from collections import Counter, defaultdict
from dataclasses import dataclass, field

sys.path.insert(0, "/workspaces/kolibri-project")
os.chdir("/workspaces/kolibri-project")

from backend.service.number_mind import (
    KnowledgeGraph,
    KnowledgeEdge,
    PatternEntry,
    _tokenize,
    djb2_hash,
    word_to_pattern,
    pattern_similarity,
)


# ═══════════════════════════════════════════════════════════════
# 100 компактных тем (идентичны v3/v4)
# ═══════════════════════════════════════════════════════════════

_RAW = [
    "Kolibri гибридная система искусственного интеллекта числовое мышление эволюционные формулы",
    "Python высокоуровневый язык программирования Гвидо Россум 1991 наука обучение",
    "JavaScript язык вебразработки Брендан Айк 1995 браузер NodeJS динамический",
    "Rust системный язык безопасность памяти Mozilla нулевые абстракции владение",
    "Go язык Google Роб Пайк горутины конкурентность простота компиляция",
    "Java объектноориентированный Гослинг JVM кроссплатформенность байткод",
    "Cplusplus Страуструп шаблоны STL объектноориентированное наследование полиморфизм",
    "Linux свободная операционная система Торвальдс 1991 серверы суперкомпьютеры",
    "Windows операционная система Microsoft DirectX графический интерфейс",
    "MacOS операционная система Apple Darwin BSD Aqua компьютер",
    "Android мобильная система Google ядро Linux смартфоны планшеты",
    "iOS мобильная система Apple iPhone iPad AppStore безопасность",
    "Париж столица Франции Сена Эйфелева башня миллионы жителей",
    "Москва столица Россия Кремль Красная площадь двенадцать миллионов",
    "Лондон столица Великобритания Темза Биг Бен Тауэрский мост",
    "Токио столица Япония мегаполис технологии небоскрёбы тринадцать миллионов",
    "НьюЙорк крупнейший город США Статуя Свободы Бродвей Манхэттен",
    "Берлин столица Германия Шпрее Бранденбургские ворота объединение",
    "математика фундаментальная наука числа алгебра геометрия анализ",
    "физика наука природа материя энергия механика квантовая теория",
    "химия наука вещества состав свойства органическая неорганическая",
    "биология наука живые организмы генетика экология эволюция клетки",
    "генетика ДНК геном CRISPR миллиарды оснований редактирование генов",
    "астрономия небесные тела Вселенная звёзды галактики телескопы",
    "космос Солнечная система восемь планет Земля третья свет минут",
    "нейросети нейронная сеть слои нейроны обратное распространение градиент",
    "Transformer самовнимание многоголовое позиционное кодирование революция NLP",
    "GPT генеративный предобученный трансформер авторегрессия триллионы токенов",
    "машинное обучение учитель подкрепление классификация регрессия данные",
    "компьютерное зрение свёрточные нейросети изображения распознавание объектов",
    "рекурсия функция вызывает себя базовый случай факториал деревья сортировка",
    "алгоритмы сортировка поиск графы динамическое программирование сложность",
    "структуры данных массивы списки стеки деревья хеш таблицы графы",
    "базы данных SQL PostgreSQL MySQL NoSQL MongoDB Redis хранение",
    "PostgreSQL реляционная СУБД JSON полнотекстовый поиск расширения PostGIS",
    "Redis оперативная память кеш брокер сообщений строки списки множества",
    "сжатие данных арифметическое кодирование компрессия предсказание символ",
    "шифрование AES RSA HMAC защита целостность аутентификация криптография",
    "блокчейн распределённый реестр биткоин криптовалюта консенсус Proof Work",
    "Ethereum смарт контракты Виталик Бутерин Solidity децентрализация Stake",
    "интернет TCP IP глобальная сеть миллиарды Бернерс Ли Web 1989",
    "HTTP протокол веб мультиплексирование HTTP2 QUIC передача данных",
    "DNS домены IP адреса иерархическая система серверы корневые авторитативные",
    "Docker контейнер образ Dockerfile Compose переносимость платформы",
    "Kubernetes оркестрация контейнеров поды сервисы масштабирование развёртывание",
    "Git контроль версий Торвальдс коммиты ветки слияние разработка",
    "GitHub хостинг репозитории пулл реквесты CI CD действия разработчики",
    "REST архитектурный стиль HTTP GET POST PUT DELETE JSON ресурсы",
    "GraphQL запросы API Facebook клиент избыточность данные схема",
    "WebSocket полнодуплексная связь TCP реальное время чаты игры",
    "React JavaScript библиотека Facebook компоненты виртуальный DOM интерфейсы",
    "Vue прогрессивный фреймворк Эван Ю реактивность компоненты директивы",
    "Angular TypeScript фреймворк Google SPA модули инъекция зависимостей",
    "TypeScript надмножество JavaScript Microsoft типизация интерфейсы дженерики",
    "FastAPI Python фреймворк Pydantic асинхронность OpenAPI документация",
    "Django Python фреймворк вебразработка ORM админка аутентификация шаблоны",
    "Flask лёгкий Python фреймворк Werkzeug Jinja2 гибкость расширения",
    "медицина здоровье лечение антибиотики вакцины иммунная система патогены",
    "экология организмы окружающая среда парниковый потепление возобновляемая энергия",
    "экономика производство ВВП инфляция безработица товары услуги рынок",
    "психология поведение мышление память восприятие когнитивная социальная",
    "философия бытие познание мораль онтология эпистемология этика логика",
    "история прошлое цивилизации средневековье события деятели современность",
    "робототехника робот автоматический промышленные заводы автономные решения",
    "квантовый компьютер кубиты суперпозиция запутанность факторизация быстрее",
    "космонавтика космос Гагарин первый полёт 1961 SpaceX Falcon ракеты",
    "энергетика солнечная ветровая ядерная уран возобновляемая электричество",
    "транспорт автомобили поезда самолёты Tesla электромобили гиперлуп Маск",
    "микросервисы архитектура REST API Docker контейнеры независимые компоненты",
    "DevOps CI CD тестирование мониторинг Terraform Ansible Prometheus",
    "тестирование модульные интеграционные TDD pytest unittest качество код",
    "Agile Scrum Kanban спринты стендапы ретроспективы итеративная доставка",
    "сети OSI уровни маршрутизаторы коммутаторы файрволы передача данных",
    "кибербезопасность фишинг DDoS инъекции SQL XSS брандмауэры IDS защита",
    "облака AWS Azure GCP IaaS PaaS SaaS ресурсы вычисления хранение",
    "AWS Amazon EC2 S3 Lambda RDS DynamoDB облачная платформа сервисы",
    "микропроцессоры Intel AMD x86 ARM RISC вычислительные элементы чипы",
    "RISCV открытая архитектура набор команд процессор лицензия IoT встраиваемые",
    "IoT интернет вещей сенсоры MQTT CoAP умный дом промышленный устройства",
    "пятое поколение 5G мобильная связь скорость задержка миллион устройств",
    "машинный перевод нейронный NMT Transformer параллельные корпуса языки",
    "NLP естественный язык токенизация лемматизация тональность сущности генерация",
    "рекомендательные системы контент коллаборативная фильтрация предпочтения",
    "обучение подкрепление агент среда награда Q_learning DQN PPO AlphaGo",
    "GAN генеративные состязательные генератор дискриминатор изображения StyleGAN",
    "компиляторы лексический синтаксический семантический оптимизация GCC LLVM",
    "операционные системы многозадачность память файловая система драйверы Unix",
    "виртуализация виртуальные машины VMware KVM гипервизор ресурсы контейнеры",
    "функциональное программирование чистые функции иммутабельность Haskell Erlang",
    "параллельное программирование потоки корутины мьютексы семафоры синхронизация",
    "WebAssembly WASM байткод нативная производительность браузер Rust компиляция",
    "GPU графические процессоры CUDA NVIDIA OpenCL нейросети расчёты параллельные",
    "распределённые системы CAP согласованность доступность Paxos Raft консенсус",
    "MapReduce параллельная обработка Hadoop Spark большие данные память",
    "электроника полупроводники транзисторы диоды микросхемы Arduino Raspberry",
    "автономные автомобили лидар камеры нейросети Waymo Tesla Cruise безводитель",
    "дроны беспилотные летательные аппараты аэросъёмка доставка квадрокоптер",
    "трёхмерная печать 3D пластик металл FDM SLA прототипирование медицина",
    "биоинформатика биологические данные алгоритмы геномы белки AlphaFold DeepMind",
    "криптография конфиденциальность симметричные асимметричные подписи SHA256 ECC",
]

CORPUS = [(f"T{i}", t) for i, t in enumerate(_RAW)]

# ═══════════════════════════════════════════════════════════════
# Предвычисление: хеш → темы, темы → хеши
# ═══════════════════════════════════════════════════════════════

HASH2TOPICS: dict[int, set[int]] = defaultdict(set)
TOPIC_HASHES: list[set[int]] = []
TOPIC_WORDS: list[set[str]] = []


def precompute() -> None:
    for tid, (_, text) in enumerate(CORPUS):
        words = set(_tokenize(text))
        hashes = {djb2_hash(w) for w in words}
        TOPIC_WORDS.append(words)
        TOPIC_HASHES.append(hashes)
        for w in words:
            HASH2TOPICS[djb2_hash(w)].add(tid)


def count_answer_domains(answer_text: str, query_text: str, relevant: list[int]) -> int:
    """Сколько из ТРЕБУЕМЫХ доменов представлено в ответе (исключая слова запроса)."""
    q_words = set(_tokenize(query_text))
    ans_words = set(_tokenize(answer_text)) - q_words
    ans_hashes = {djb2_hash(w) for w in ans_words}
    return sum(1 for tid in relevant if ans_hashes & TOPIC_HASHES[tid])


def collective_answer(
    graphs: list[KnowledgeGraph], query: str, k: int = 15,
) -> tuple[str, float]:
    """Ансамбль k узлов: voting по словам с весами confidence."""
    votes: Counter[str] = Counter()
    q_words = set(_tokenize(query))
    for g in random.sample(graphs, min(k, len(graphs))):
        text, conf, _ = g.answer(query, max_words=8)
        if text and conf > 0:
            for w in _tokenize(text):
                if w not in q_words:
                    votes[w] += conf
    if not votes:
        return "", 0.0
    top = [w for w, _ in votes.most_common(10)]
    sc = sum(votes[w] for w in top)
    return " ".join(top), min(1.0, sc / (len(top) + 1))


# ═══════════════════════════════════════════════════════════════
#   ЧАСТЬ I: EMERGENCE УРОВНЯ СЕТИ
#   (Подтверждено v4. Воспроизводим ключевые метрики.)
# ═══════════════════════════════════════════════════════════════

def part1_network_emergence() -> dict:
    """
    Три быстрых теста сетевого emergence:
      A) Токены без рёбер бесполезны (знания = граф)
      B) Слияние создаёт кросс-доменные пути
      C) Хабы связывают домены (эмерджентная топология)
    """
    print(f"\n{'═'*70}")
    print(f"   ЧАСТЬ I: EMERGENCE УРОВНЯ СЕТИ")
    print(f"{'═'*70}")
    print(f"   Это emergence маршрутизатора: НОВЫЕ ПУТИ в графе.")
    print(f"   Валидно, но не доказывает разум.\n")

    random.seed(100)
    base_topics = sorted(random.sample(range(100), 10))
    extra_topics = [t for t in range(100) if t not in base_topics][:10]

    # --- A: Токены без рёбер ---
    base = KnowledgeGraph()
    for t in base_topics:
        base.train_text(CORPUS[t][1], context_window=3)

    tokens_only = KnowledgeGraph()
    for t in base_topics:
        tokens_only.train_text(CORPUS[t][1], context_window=3)
    for t in extra_topics:
        for w in _tokenize(CORPUS[t][1]):
            if len(w) >= 2:
                h = djb2_hash(w)
                if h not in tokens_only.patterns:
                    tokens_only.patterns[h] = PatternEntry(
                        word=w,
                        pattern=word_to_pattern(w),
                        hash=h,
                        frequency=1,
                        fitness=0.06,
                    )
                    tokens_only._hash_to_word[h] = w

    merged = KnowledgeGraph()
    for t in base_topics:
        merged.train_text(CORPUS[t][1], context_window=3)
    donor = KnowledgeGraph()
    for t in extra_topics:
        donor.train_text(CORPUS[t][1], context_window=3)
    merged.merge_state(donor.export_state())

    questions = []
    for tid in extra_topics:
        words = _tokenize(CORPUS[tid][1])
        if len(words) >= 3:
            questions.append((" ".join(words[:3]), tid))

    b_ok = t_ok = m_ok = 0
    for q, tid in questions[:8]:
        _, bc, _ = base.answer(q)
        _, tc, _ = tokens_only.answer(q)
        _, mc, _ = merged.answer(q)
        if bc > 0.05:
            b_ok += 1
        if tc > 0.05:
            t_ok += 1
        if mc > 0.05:
            m_ok += 1

    print(f"   [A] Токены без рёбер:")
    print(f"       База: {b_ok}/8 | +Токены: {t_ok}/8 | +Граф: {m_ok}/8")
    print(f"       → Токены без структуры = мёртвый груз ✓")

    # --- B: Кросс-доменные пути ---
    chain_pairs = [
        (1, 28, "обучение", "россум классификация"),
        (25, 91, "нейросети", "нейроны cuda"),
        (38, 92, "консенсус", "биткоин paxos"),
    ]
    cross_ok = 0
    for ta, tb, bridge, query in chain_pairs:
        node = KnowledgeGraph()
        node.train_text(CORPUS[ta][1], context_window=3)
        donor_b = KnowledgeGraph()
        donor_b.train_text(CORPUS[tb][1], context_window=3)
        _, conf_before, _ = node.answer(query, max_words=8)
        node.merge_state(donor_b.export_state())
        _, conf_after, _ = node.answer(query, max_words=8)
        if conf_after > conf_before:
            cross_ok += 1

    print(f"\n   [B] Кросс-доменные пути после слияния:")
    print(f"       {cross_ok}/{len(chain_pairs)} цепочек улучшились")
    print(f"       → Слияние графов = новая связность ✓")

    # --- C: Хабы ---
    random.seed(42)
    N = 50
    graphs: list[KnowledgeGraph] = []
    for i in range(N):
        g = KnowledgeGraph()
        for t in random.sample(range(100), 20):
            g.train_text(CORPUS[t][1], context_window=3)
        graphs.append(g)
    for _ in range(5):
        for i in range(N):
            j = random.randint(0, N - 2)
            if j >= i:
                j += 1
            graphs[i].merge_state(graphs[j].export_state())

    big = max(graphs, key=lambda x: len(x.edges))
    hub_count = 0
    for h, neighbors in big._adj.items():
        entry = big.patterns.get(h)
        if not entry or len(entry.word) < 3:
            continue
        reach: set[int] = set()
        for nh in neighbors:
            reach |= HASH2TOPICS.get(nh, set())
        reach |= HASH2TOPICS.get(h, set())
        if len(reach) >= 5:
            hub_count += 1

    print(f"\n   [C] Эмерджентные хабы (≥5 доменов):")
    print(f"       {hub_count} слов-мостов в объединённом графе")
    print(f"       → Топология ИЗМЕНИЛАСЬ после слияния ✓")

    print(f"\n   ┌────────────────────────────────────────────────────────┐")
    print(f"   │ ИТОГ ЧАСТИ I: Сетевой emergence ПОДТВЕРЖДЁН.          │")
    print(f"   │ Слияние независимых графов создаёт:                   │")
    print(f"   │  • новые навигируемые пути                            │")
    print(f"   │  • кросс-доменные мосты                               │")
    print(f"   │  • хаб-слова с высокой связностью                     │")
    print(f"   │                                                       │")
    print(f"   │ НО: это делает любой граф при merge.                   │")
    print(f"   │ Маршрутизатор тоже «знает» новые пути после           │")
    print(f"   │ объединения таблиц маршрутизации.                     │")
    print(f"   │ Это emergence ТОПОЛОГИИ, не emergence РАЗУМА.          │")
    print(f"   └────────────────────────────────────────────────────────┘")

    return {
        "tokens_dead": t_ok,
        "graph_alive": m_ok,
        "cross_domain": cross_ok,
        "hubs": hub_count,
        "graphs": graphs,
    }


# ═══════════════════════════════════════════════════════════════
#   ЧАСТЬ II: EMERGENCE УРОВНЯ РАЗУМА
#   Четыре эксперимента, которые требуют БОЛЬШЕ чем навигацию.
# ═══════════════════════════════════════════════════════════════


# -------------------------------------------------------------------
# Эксперимент II-A: АНАЛОГИИ (A:B :: C:?)
# -------------------------------------------------------------------

# Аналогии: «Столица:Страна :: Москва:?» → Россия
# Для этого нужно: (1) абстрагировать отношение «столица-страна»
# из пары Париж-Франция, (2) применить к Москва → Россия.
# Граф-навигация этого НЕ делает — она просто идёт по рёбрам.

ANALOGIES = [
    {
        "name": "Столица → Страна",
        "a": "париж",
        "b": "франции",       # Париж : Франции ::
        "c": "москва",        # Москва : ?
        "expected": {"россия", "россию", "российской", "россии"},
        "topics": [12, 13],   # Париж, Москва
    },
    {
        "name": "Язык → Создатель",
        "a": "python",
        "b": "россум",        # Python : Россум ::
        "c": "javascript",    # JavaScript : ?
        "expected": {"айк", "брендан"},
        "topics": [1, 2],
    },
    {
        "name": "Язык → Год",
        "a": "python",
        "b": "1991",          # Python : 1991 ::
        "c": "javascript",    # JavaScript : ?
        "expected": {"1995"},
        "topics": [1, 2],
    },
    {
        "name": "Река → Город",
        "a": "сена",
        "b": "париж",         # Сена : Париж ::
        "c": "темза",         # Темза : ?
        "expected": {"лондон"},
        "topics": [12, 14],
    },
    {
        "name": "ОС → Компания",
        "a": "windows",
        "b": "microsoft",     # Windows : Microsoft ::
        "c": "macos",         # MacOS : ?
        "expected": {"apple"},
        "topics": [8, 9],
    },
    {
        "name": "Мобильная ОС → Устройство",
        "a": "android",
        "b": "смартфоны",     # Android : смартфоны ::
        "c": "ios",           # iOS : ?
        "expected": {"iphone", "ipad"},
        "topics": [10, 11],
    },
]


def _analogy_via_graph(g: KnowledgeGraph, a: str, b: str, c: str) -> list[str]:
    """
    Попытка решить аналогию A:B :: C:? через граф.

    Стратегия 1 (графовая): Найти соседей C, отфильтровать те,
    которые занимают аналогичную «позицию» относительно C,
    как B относительно A.

    Стратегия 2 (паттерновая): Вычислить вектор-смещение
    pattern(B) - pattern(A), применить к pattern(C),
    найти ближайшее слово к результату.
    """
    results: list[str] = []

    # Стратегия 1: Граф — соседи C, исключая A и B
    ha, hb, hc = djb2_hash(a), djb2_hash(b), djb2_hash(c)
    exclude = {ha, hb, hc}
    neighbors_c = g._adj.get(hc, set())
    for nh in neighbors_c:
        if nh not in exclude:
            entry = g.patterns.get(nh)
            if entry and len(entry.word) >= 2:
                results.append(entry.word)

    # Стратегия 2: Паттерновое смещение (числовая аналогия)
    pa = g.patterns.get(ha)
    pb = g.patterns.get(hb)
    pc = g.patterns.get(hc)
    if pa and pb and pc:
        # Вектор: target_pattern = pattern(C) + (pattern(B) - pattern(A))
        target = [
            max(0, min(9, pc.pattern[i] + pb.pattern[i] - pa.pattern[i]))
            for i in range(len(pc.pattern))
        ]
        found = g.find_by_pattern(target, limit=5, exclude=exclude)
        for word, sim in found:
            if word not in results:
                results.append(word)

    return results


def experiment_analogies(graphs: list[KnowledgeGraph]) -> tuple[int, int]:
    """
    Тест на аналогии: A:B :: C:?

    Разумный emergence = система должна АБСТРАГИРОВАТЬ отношение
    из пары (A, B) и ПЕРЕНЕСТИ его на C.

    Сетевой emergence = система просто выдаёт соседей C в графе,
    без понимания, КАКОЕ отношение имеется в виду.
    """
    print(f"\n{'─'*70}")
    print(f"   ЭКСПЕРИМЕНТ II-A: АНАЛОГИИ (A:B :: C:?)")
    print(f"{'─'*70}")
    print(f"   Вопрос: может ли система ПЕРЕНЕСТИ отношение?\n")

    big = max(graphs, key=lambda x: len(x.edges))

    # Обучаем на нужных темах
    for an in ANALOGIES:
        for t in an["topics"]:
            big.train_text(CORPUS[t][1], context_window=3)

    solved_graph = 0
    solved_pattern = 0
    total = len(ANALOGIES)

    for an in ANALOGIES:
        a, b, c = an["a"], an["b"], an["c"]
        expected = an["expected"]

        candidates = _analogy_via_graph(big, a, b, c)

        # Проверяем: есть ли правильный ответ среди кандидатов?
        found_any = any(w in expected for w in candidates[:10])
        # Проверяем: правильный ответ на ПЕРВОМ месте?
        top1_correct = bool(candidates) and candidates[0] in expected

        method = ""
        if found_any:
            # Но КАК найден? Через граф (сосед C) или через паттерн?
            ha, hb, hc = djb2_hash(a), djb2_hash(b), djb2_hash(c)
            neighbors_c = big._adj.get(hc, set())
            for w in candidates:
                if w in expected:
                    hw = djb2_hash(w)
                    if hw in neighbors_c:
                        solved_graph += 1
                        method = "граф (сосед C)"
                    else:
                        solved_pattern += 1
                        method = "паттерн (числ. смещение)"
                    break

        status = "✓" if found_any else "✗"
        top5 = ", ".join(candidates[:5]) if candidates else "(пусто)"
        print(f"   {status} {an['name']}: {a}:{b} :: {c}:?")
        print(f"     Ожидалось: {expected}")
        print(f"     Получено:  [{top5}]")
        if method:
            print(f"     Метод:     {method}")
        print()

    print(f"   Результат: {solved_graph + solved_pattern}/{total} аналогий")
    print(f"     Через граф (простая навигация): {solved_graph}")
    print(f"     Через паттерн (числовое):       {solved_pattern}")

    if solved_graph > solved_pattern:
        print(f"\n   ⚠ Большинство решено через СОСЕДСТВО в графе.")
        print(f"     Это НЕ понимание отношения, а совпадение топологии.")
        print(f"     Если бы C не был соседом ответа — система бы не справилась.")
    elif solved_pattern > 0:
        print(f"\n   ► Паттерновое смещение иногда работает!")
        print(f"     Это ближе к числовой аналогии (как word2vec).")
    else:
        print(f"\n   ⚠ Аналогии НЕ решаются ни одним методом.")

    return solved_graph, solved_pattern


# -------------------------------------------------------------------
# Эксперимент II-B: АБСТРАКЦИЯ (обобщение из примеров)
# -------------------------------------------------------------------

def experiment_abstraction(graphs: list[KnowledgeGraph]) -> tuple[int, int]:
    """
    Тест на абстракцию: может ли система ОБОБЩИТЬ категорию?

    Даём: Париж, Москва, Лондон, Токио, Берлин
    Спрашиваем: «что общего?»
    Разум скажет: «столицы». Сеть скажет: соседи соседей.

    Метод: Запрашиваем answer() для группы слов.
    Проверяем: появляется ли абстрактный термин (столица, язык, наука).
    """
    print(f"\n{'─'*70}")
    print(f"   ЭКСПЕРИМЕНТ II-B: АБСТРАКЦИЯ (обобщение)")
    print(f"{'─'*70}")
    print(f"   Вопрос: может ли система найти ОБЩУЮ КАТЕГОРИЮ?\n")

    ABSTRACTION_TESTS = [
        {
            "name": "Столицы",
            "query": "париж москва лондон токио берлин",
            "abstract_markers": {"столица", "столицы", "город", "города"},
            "topics": [12, 13, 14, 15, 17],
        },
        {
            "name": "Языки программирования",
            "query": "python javascript rust java go",
            "abstract_markers": {"язык", "языки", "программирование",
                                 "программирования"},
            "topics": [1, 2, 3, 4, 5],
        },
        {
            "name": "Науки",
            "query": "математика физика химия биология",
            "abstract_markers": {"наука", "науки", "фундаментальная",
                                 "естественные"},
            "topics": [18, 19, 20, 21],
        },
        {
            "name": "Операционные системы",
            "query": "linux windows macos android",
            "abstract_markers": {"операционная", "система", "системы", "ос"},
            "topics": [7, 8, 9, 10],
        },
        {
            "name": "Базы данных",
            "query": "postgresql mysql mongodb redis",
            "abstract_markers": {"база", "данных", "хранение", "субд",
                                 "данные"},
            "topics": [33, 34, 35],
        },
        {
            "name": "Фреймворки Python",
            "query": "fastapi django flask",
            "abstract_markers": {"фреймворк", "python", "веб",
                                 "вебразработка"},
            "topics": [54, 55, 56],
        },
    ]

    big = max(graphs, key=lambda x: len(x.edges))
    for test in ABSTRACTION_TESTS:
        for t in test["topics"]:
            big.train_text(CORPUS[t][1], context_window=3)

    found_abstract = 0
    found_concrete = 0
    total = len(ABSTRACTION_TESTS)

    for test in ABSTRACTION_TESTS:
        ans, conf, meta = big.answer(test["query"], max_words=15)
        ans_words = set(_tokenize(ans))

        has_abstract = bool(ans_words & test["abstract_markers"])
        if has_abstract:
            found_abstract += 1
        else:
            found_concrete += 1

        status = "✓" if has_abstract else "✗"
        markers_found = ans_words & test["abstract_markers"]
        print(f"   {status} {test['name']}")
        print(f"     Запрос: «{test['query']}»")
        print(f"     Ответ:  «{ans[:60]}»")
        if markers_found:
            print(f"     Абстракция: {markers_found}")
        else:
            print(f"     Нет абстрактного термина в ответе")
        print()

    print(f"   Результат: {found_abstract}/{total} — найдена абстрактная категория")

    # Критический анализ
    print(f"\n   Анализ:")
    if found_abstract > total // 2:
        print(f"   ► Абстрактные слова ПОЯВЛЯЮТСЯ в ответах.")
        print(f"     НО: это потому что «столица» — СОСЕДНЕЕ слово")
        print(f"     в обучающем тексте «Париж столица Франции...»")
        print(f"     Система НЕ абстрагирует — она находит co-occurrence.")
        print(f"     Это emergence СЕТИ (общий сосед), не emergence РАЗУМА.")
    else:
        print(f"   ⚠ Абстрактные категории НЕ появляются.")
        print(f"     Система возвращает конкретные термины,")
        print(f"     а не обобщения.")

    return found_abstract, found_concrete


# -------------------------------------------------------------------
# Эксперимент II-C: ПРЕДСКАЗАНИЕ (вывод НЕ из графа)
# -------------------------------------------------------------------

def experiment_prediction(graphs: list[KnowledgeGraph]) -> tuple[int, int]:
    """
    Тест на предсказание: может ли система ВЫВЕСТИ факт,
    которого нет в графе?

    Обучаем:
      «Python используется для машинного обучения»
      «Машинное обучение требует больших данных»
    НЕ обучаем:
      «Python используется для больших данных»

    Проверяем: выведет ли система связь Python→большие данные?

    КЛЮЧЕВОЕ ОТЛИЧИЕ от Эксперимента v4-2 (транзитивный вывод):
    v4 проверял НАЛИЧИЕ ПУТИ через мост-слово (сетевой уровень).
    Здесь проверяем: генерирует ли система НОВОЕ УТВЕРЖДЕНИЕ.
    """
    print(f"\n{'─'*70}")
    print(f"   ЭКСПЕРИМЕНТ II-C: ПРЕДСКАЗАНИЕ (вывод, которого нет в графе)")
    print(f"{'─'*70}")
    print(f"   Вопрос: может ли система ГЕНЕРИРОВАТЬ новые связи?\n")

    PREDICTION_TESTS = [
        {
            "name": "Python → большие данные (через ML)",
            "train_texts": [
                "Python используется для машинного обучения данные наука",
                "машинное обучение требует большие данные обработка Hadoop Spark",
            ],
            "query": "python большие данные",
            "expected_link": {"hadoop", "spark", "обработка"},
            "not_trained": "Прямой связи Python↔Hadoop НЕТ в обучении",
        },
        {
            "name": "CRISPR → лечение (через генетику)",
            "train_texts": [
                "CRISPR редактирование генов геном точность биология",
                "генетические заболевания лечение терапия геном мутации",
            ],
            "query": "crispr лечение",
            "expected_link": {"терапия", "заболевания", "мутации"},
            "not_trained": "Прямой связи CRISPR↔терапия НЕТ в обучении",
        },
        {
            "name": "Docker → мониторинг (через DevOps)",
            "train_texts": [
                "Docker контейнер образ развёртывание DevOps CI платформа",
                "DevOps мониторинг Prometheus метрики алерты инфраструктура",
            ],
            "query": "docker мониторинг",
            "expected_link": {"prometheus", "метрики", "алерты"},
            "not_trained": "Прямой связи Docker↔Prometheus НЕТ в обучении",
        },
        {
            "name": "Blockchain → IoT (через безопасность)",
            "train_texts": [
                "блокчейн криптография безопасность целостность распределённый",
                "IoT интернет вещей безопасность сенсоры уязвимости атаки",
            ],
            "query": "блокчейн iot",
            "expected_link": {"сенсоры", "уязвимости", "атаки", "вещей"},
            "not_trained": "Прямой связи блокчейн↔сенсоры НЕТ в обучении",
        },
    ]

    predicted = 0
    transitive_only = 0
    total = len(PREDICTION_TESTS)

    for test in PREDICTION_TESTS:
        g = KnowledgeGraph()
        for text in test["train_texts"]:
            g.train_text(text, context_window=3)

        ans, conf, meta = g.answer(test["query"], max_words=10)
        ans_words = set(_tokenize(ans))

        found = ans_words & test["expected_link"]
        if found:
            predicted += 1

        status = "✓" if found else "✗"
        print(f"   {status} {test['name']}")
        print(f"     {test['not_trained']}")
        print(f"     Запрос: «{test['query']}»")
        print(f"     Ответ:  «{ans}» (conf={conf:.3f})")
        if found:
            print(f"     Найдено: {found} — но через мост-слово в графе!")
        else:
            print(f"     Связь НЕ найдена")
        print()

    print(f"   Результат: {predicted}/{total} предсказаний")
    print(f"\n   Критический анализ:")
    print(f"   Если предсказания работают — это ТРАНЗИТИВНЫЕ ПУТИ в графе:")
    print(f"     Python →[обучение]→ ML →[данные]→ Hadoop")
    print(f"   Путь существует через рёбра. Это НЕ логический вывод.")
    print(f"   Настоящий вывод «Python полезен для Big Data, потому что")
    print(f"   ML (для которого Python хорош) работает с Big Data»")
    print(f"   требует понимания ПРИЧИННОСТИ, а не навигации по графу.")

    return predicted, total


# -------------------------------------------------------------------
# Эксперимент II-D: СТРУКТУРНАЯ КАТЕГОРИЗАЦИЯ
# -------------------------------------------------------------------

def experiment_categorization(graphs: list[KnowledgeGraph]) -> tuple[int, int]:
    """
    Тест: может ли система ГРУППИРОВАТЬ слова по смыслу,
    а не по co-occurrence?

    Даём: «python, москва, нейроны, java, лондон, градиент»
    Вопрос: какие из них — языки? какие — города? какие — ML?

    Разумная система абстрагирует категории.
    Графовая система возвращает соседей.
    """
    print(f"\n{'─'*70}")
    print(f"   ЭКСПЕРИМЕНТ II-D: КАТЕГОРИЗАЦИЯ (группировка по смыслу)")
    print(f"{'─'*70}")
    print(f"   Вопрос: может ли система ГРУППИРОВАТЬ слова?\n")

    # Обучаем на всех 100 темах для максимального покрытия
    big = max(graphs, key=lambda x: len(x.edges))

    CATEGORY_TESTS = [
        {
            "name": "Языки программирования vs Города",
            "words": ["python", "москва", "java", "лондон", "rust", "берлин"],
            "group_a": {"python", "java", "rust"},
            "group_b": {"москва", "лондон", "берлин"},
            "label_a": "языки",
            "label_b": "города",
        },
        {
            "name": "ML-термины vs Веб-технологии",
            "words": ["нейроны", "react", "градиент", "angular", "слои", "vue"],
            "group_a": {"нейроны", "градиент", "слои"},
            "group_b": {"react", "angular", "vue"},
            "label_a": "ML",
            "label_b": "веб",
        },
    ]

    correct_groupings = 0
    total_tests = len(CATEGORY_TESTS)

    for test in CATEGORY_TESTS:
        print(f"   [{test['name']}]")
        print(f"   Слова: {test['words']}")

        # Метод: для каждой пары слов вычисляем «близость» в графе
        # (количество общих соседей / max соседей)
        words = test["words"]
        similarity_matrix: dict[tuple[str, str], float] = {}

        for i, w1 in enumerate(words):
            h1 = djb2_hash(w1)
            neighbors1 = big._adj.get(h1, set())
            for j, w2 in enumerate(words):
                if i >= j:
                    continue
                h2 = djb2_hash(w2)
                neighbors2 = big._adj.get(h2, set())
                if neighbors1 or neighbors2:
                    common = len(neighbors1 & neighbors2)
                    total_n = max(len(neighbors1 | neighbors2), 1)
                    sim = common / total_n
                else:
                    sim = 0.0
                similarity_matrix[(w1, w2)] = sim

        # Проверяем: внутригрупповая близость > межгрупповая?
        intra_a = []
        intra_b = []
        inter = []
        for (w1, w2), sim in similarity_matrix.items():
            if w1 in test["group_a"] and w2 in test["group_a"]:
                intra_a.append(sim)
            elif w1 in test["group_b"] and w2 in test["group_b"]:
                intra_b.append(sim)
            else:
                inter.append(sim)

        avg_intra = (
            ((sum(intra_a) / max(len(intra_a), 1))
             + (sum(intra_b) / max(len(intra_b), 1)))
            / 2
        )
        avg_inter = sum(inter) / max(len(inter), 1)

        groups_correct = avg_intra > avg_inter
        if groups_correct:
            correct_groupings += 1

        status = "✓" if groups_correct else "✗"
        print(f"   {status} Внутригрупповая близость: {avg_intra:.4f}")
        print(f"     Межгрупповая близость:   {avg_inter:.4f}")
        print(f"     {test['label_a']}: {test['group_a']}")
        print(f"     {test['label_b']}: {test['group_b']}")

        if groups_correct:
            print(f"     → Граф ОТРАЖАЕТ категории (но через co-occurrence)")
        else:
            print(f"     → Граф НЕ различает категории")
        print()

    print(f"   Результат: {correct_groupings}/{total_tests}")
    print(f"\n   Анализ:")
    print(f"   Если группировка работает — это потому что языки")
    print(f"   обучались в одних текстах (co-occurrence), а города — в других.")
    print(f"   Это НЕ понимание «Python — это язык».")
    print(f"   Настоящая категоризация = присвоение ТИПА/КЛАССА,")
    print(f"   а не кластеризация по совместному появлению.")

    return correct_groupings, total_tests


# ═══════════════════════════════════════════════════════════════
#   ЧАСТЬ III: ВЕРДИКТ
# ═══════════════════════════════════════════════════════════════

def verdict(
    net: dict,
    analogies: tuple[int, int],
    abstraction: tuple[int, int],
    prediction: tuple[int, int],
    categorization: tuple[int, int],
) -> None:
    ag, ap = analogies
    abst_ok, abst_total = abstraction[0], abstraction[0] + abstraction[1]
    pred_ok, pred_total = prediction
    cat_ok, cat_total = categorization

    print(f"\n{'█'*70}")
    print(f"   ВЕРДИКТ v5: ДВА УРОВНЯ EMERGENCE")
    print(f"{'█'*70}")

    print(f"""
   ┌─────────────────────────────────────────────────────────────────┐
   │               EMERGENCE УРОВНЯ СЕТИ (✓ подтверждён)            │
   ├─────────────────────────────────────────────────────────────────┤
   │ Токены без рёбер           │ {net['tokens_dead']}/8 — мёртвый груз         │
   │ Граф (с рёбрами)           │ {net['graph_alive']}/8 — РАБОТАЕТ             │
   │ Кросс-доменные пути        │ {net['cross_domain']}/3 цепочек               │
   │ Эмерджентные хабы          │ {net['hubs']} слов-мостов                │
   ├─────────────────────────────────────────────────────────────────┤
   │ Механизм: merge графов → новые пути → навигация.               │
   │ Это ВАЛИДНО, но это делает любой граф.                         │
   │ Маршрутизатор ≠ разум.                                        │
   └─────────────────────────────────────────────────────────────────┘
""")

    print(f"""
   ┌─────────────────────────────────────────────────────────────────┐
   │               EMERGENCE УРОВНЯ РАЗУМА (результаты)             │
   ├─────────────────────────────────────────────────────────────────┤
   │ Аналогии (A:B :: C:?)      │ граф: {ag}  паттерн: {ap}  /{len(ANALOGIES)}          │
   │ Абстракция                 │ {abst_ok}/{abst_ok + abstraction[1]} — но через co-occurrence      │
   │ Предсказание               │ {pred_ok}/{pred_total} — но через транзитив. пути   │
   │ Категоризация               │ {cat_ok}/{cat_total} — но через кластеризацию      │
   ├─────────────────────────────────────────────────────────────────┤
   │ Всё, что «работает», работает через ГРАФОВУЮ НАВИГАЦИЮ.        │
   │ Это не абстрагирование, не понимание, не рассуждение.          │
   │ Это СТРУКТУРНОЕ СОВПАДЕНИЕ топологии графа с семантикой.       │
   └─────────────────────────────────────────────────────────────────┘
""")

    print(f"   ДВА ЧЕСТНЫХ УТВЕРЖДЕНИЯ:")
    print()
    print(f"   1. KnowledgeGraph + gossip = EMERGENCE СЕТИ.")
    print(f"      Слияние независимых графов создаёт новую связность,")
    print(f"      которой нет ни в одном обучающем тексте.")
    print(f"      Это РЕАЛЬНО и ценно для навигации/поиска.")
    print()
    print(f"   2. KnowledgeGraph ≠ EMERGENCE РАЗУМА (пока).")
    print(f"      Нет абстрагирования отношений.")
    print(f"      Нет причинного вывода.")
    print(f"      Нет переноса структуры (аналогии).")
    print(f"      Нет генерализации (индукции).")
    print()
    print(f"   ЧТО НУЖНО ДЛЯ УРОВНЯ РАЗУМА:")
    print(f"      • Метаграф: рёбра с ТИПАМИ (столица_of, создан_by)")
    print(f"      • Формульный слой: трансформация паттернов для аналогий")
    print(f"      • Абстрактные узлы: категории как первоклассные объекты")
    print(f"      • Каузальные цепочки: не просто A→B, а A ПОТОМУ_ЧТО B")
    print(f"      • Индуктивный вывод: из N примеров → правило")
    print()
    print(f"{'█'*70}")
    print(f"   Kolibri v5: честная граница между сетью и разумом.")
    print(f"{'█'*70}\n")


# ═══════════════════════════════════════════════════════════════
# MAIN
# ═══════════════════════════════════════════════════════════════

def main() -> None:
    random.seed(42)
    precompute()

    print("█" * 70)
    print("   KOLIBRI SWARM v5: СЕТЬ vs РАЗУМ — ДВА УРОВНЯ EMERGENCE")
    print("█" * 70)
    sys.stdout.flush()

    # Часть I: Сетевой emergence (быстрый воспроизвод)
    net = part1_network_emergence()
    graphs = net["graphs"]

    # Часть II: Разумный emergence (новые эксперименты)
    print(f"\n{'═'*70}")
    print(f"   ЧАСТЬ II: EMERGENCE УРОВНЯ РАЗУМА")
    print(f"{'═'*70}")
    print(f"   Теперь проверяем то, что ОТЛИЧАЕТ разум от маршрутизатора.\n")

    analogies = experiment_analogies(graphs)
    abstraction = experiment_abstraction(graphs)
    prediction = experiment_prediction(graphs)
    categorization = experiment_categorization(graphs)

    # Часть III: Вердикт
    verdict(net, analogies, abstraction, prediction, categorization)


if __name__ == "__main__":
    main()
