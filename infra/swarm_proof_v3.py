#!/usr/bin/env python3
"""
═══════════════════════════════════════════════════════════════════
  Kolibri Swarm v3: 1000 узлов × 20 тем каждый (из 100)
═══════════════════════════════════════════════════════════════════
"""
from __future__ import annotations
import sys, os, time, random, math
from collections import Counter
from dataclasses import dataclass, field

sys.path.insert(0, "/workspaces/kolibri-project")
os.chdir("/workspaces/kolibri-project")

from backend.service.number_mind import KnowledgeGraph, _tokenize, djb2_hash

# ═══════════════════════════════════════════════════════════════
# 100 тем — компактные, но различимые
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

CORPUS = [(f"Тема{i}", t) for i, t in enumerate(_RAW)]
TOPIC_NAMES = [n for n, _ in CORPUS]

CONTROL_QUESTIONS = [
    ("Kolibri искусственный интеллект числовое", 0),
    ("Python язык программирования Гвидо", 1),
    ("Rust безопасность памяти Mozilla", 3),
    ("Linux операционная система Торвальдс", 7),
    ("столица Франции Париж Эйфелева", 12),
    ("столица Россия Москва Кремль", 13),
    ("нейронная сеть нейроны обучение", 25),
    ("Transformer самовнимание революция", 26),
    ("GPT генеративный трансформер", 27),
    ("Docker контейнер Kubernetes", 43),
    ("Git контроль версий коммиты", 45),
    ("React JavaScript интерфейс", 50),
    ("FastAPI Python Pydantic", 54),
    ("квантовый компьютер кубиты суперпозиция", 64),
    ("генетика ДНК CRISPR геном", 22),
    ("блокчейн биткоин криптовалюта", 38),
    ("облака AWS Azure GCP", 74),
    ("WebAssembly WASM браузер", 90),
    ("распределённые системы CAP Paxos", 92),
    ("автономные автомобили лидар Waymo", 95),
]


@dataclass
class SwarmNode:
    node_id: int
    graph: KnowledgeGraph = field(default_factory=KnowledgeGraph)
    known_topics: set = field(default_factory=set)
    peers: list = field(default_factory=list)

    def train(self, text: str, tid: int):
        self.graph.train_text(text, context_window=3)
        self.known_topics.add(tid)

    def export_knowledge(self) -> dict:
        return self.graph.export_state()

    def import_knowledge(self, state: dict) -> int:
        before = len(self.graph.patterns)
        self.graph.merge_state(state)
        return len(self.graph.patterns) - before

    def answer(self, q: str, max_words: int = 12) -> tuple[str, float]:
        text, conf, _ = self.graph.answer(q, max_words=max_words)
        return text, conf

    @property
    def size(self) -> int:
        return len(self.graph.patterns)


def collective_answer(nodes: list[SwarmNode], q: str, k: int = 15) -> tuple[str, float, int]:
    votes: Counter = Counter()
    answered = 0
    for n in random.sample(nodes, min(k, len(nodes))):
        t, c = n.answer(q, max_words=8)
        if t and c > 0:
            answered += 1
            for i, w in enumerate(t.split()):
                votes[w] += c * max(1.0 - i * 0.08, 0.1)
    if not votes:
        return "", 0.0, 0
    top = [w for w, _ in votes.most_common(10)]
    sc = sum(votes[w] for w in top)
    return ' '.join(top), min(1.0, sc / (len(top) + 1)), answered


class Swarm:
    def __init__(self, n: int, tpn: int = 20, fan: int = 3):
        self.n = n
        self.tpn = tpn
        self.fan = fan
        self.nodes: list[SwarmNode] = []
        self.rd = 0
        self.hist: list[dict] = []
        self._th: list[list[int]] = []

    def _precompute(self):
        self._th = []
        for _, text in CORPUS:
            toks = _tokenize(text)
            self._th.append([djb2_hash(t) for t in toks[:8]])

    def init(self):
        print(f"\n{'═'*70}")
        print(f"  ИНИЦИАЛИЗАЦИЯ: {self.n} узлов × {self.tpn} тем (из {len(CORPUS)})")
        print(f"{'═'*70}")
        t0 = time.time()
        self._precompute()
        for i in range(self.n):
            nd = SwarmNode(node_id=i)
            for t in random.sample(range(len(CORPUS)), self.tpn):
                nd.train(CORPUS[t][1], t)
            self.nodes.append(nd)
            if (i + 1) % 200 == 0:
                print(f"    ...{i+1}/{self.n}")
        for nd in self.nodes:
            cs = [x.node_id for x in self.nodes if x.node_id != nd.node_id]
            nd.peers = random.sample(cs, min(self.fan, len(cs)))
        print(f"  Готово за {time.time()-t0:.1f}с | fanout={self.fan}")

    def gossip(self) -> tuple[int, float]:
        self.rd += 1
        t0 = time.time()
        total = 0
        for nd in self.nodes:
            pid = random.choice(nd.peers)
            total += nd.import_knowledge(self.nodes[pid].export_knowledge())
        return total, time.time() - t0

    def measure(self) -> dict:
        covs = []
        for nd in self.nodes:
            k = sum(1 for h in self._th if sum(1 for x in h if x in nd.graph.patterns) >= 2)
            covs.append(k)
        ac = sum(covs) / len(covs)
        full = sum(1 for c in covs if c >= 95)
        sizes = [nd.size for nd in self.nodes]
        samp = random.sample(self.nodes, min(20, len(self.nodes)))
        confs = []
        for q, _ in CONTROL_QUESTIONS:
            nc = [n.answer(q)[1] for n in samp]
            confs.append(sum(nc) / len(nc))
        aconf = sum(confs) / len(confs)
        m = {"rd": self.rd, "cov": round(ac, 1), "maxcov": max(covs),
             "full": full, "sz": round(sum(sizes)/len(sizes)),
             "maxsz": max(sizes), "conf": round(aconf, 4)}
        self.hist.append(m)
        return m

    def run(self, rounds: int = 12):
        print(f"\n  {'R':>3s} │ {'Новых':>8s} │ {'Покр':>8s} │ "
              f"{'Полн≥95':>9s} │ {'Разм':>6s} │ {'Увер':>6s} │ {'t':>5s}")
        print(f"  {'─'*3}─┼─{'─'*8}─┼─{'─'*8}─┼─{'─'*9}─┼─{'─'*6}─┼─{'─'*6}─┼─{'─'*5}")

        m0 = self.measure()
        print(f"  R 0 │  начало │ {m0['cov']:>5.1f}/100 │ "
              f"{m0['full']:>4d}/{self.n:<4d} │ {m0['sz']:>6d} │ {m0['conf']:.3f} │   --")

        for r in range(rounds):
            new, el = self.gossip()
            m = self.measure()
            bar = "█" * min(int(m['cov'] / 2.5), 40)
            print(f"  R{m['rd']:>2d} │ {new:>+8d} │ {m['cov']:>5.1f}/100 │ "
                  f"{m['full']:>4d}/{self.n:<4d} │ {m['sz']:>6d} │ {m['conf']:.3f} │ {el:>4.1f}с {bar}")
            if new == 0 and r > 1:
                print(f"      │ *** СХОДИМОСТЬ ***")
                break

    def proof(self):
        print(f"\n{'═'*70}")
        print(f"  ОДИНОЧКА vs УЗЕЛ РОЯ vs КОЛЛЕКТИВ (15 узлов)")
        print(f"{'═'*70}")

        solo = SwarmNode(node_id=-1)
        st = random.sample(range(len(CORPUS)), self.tpn)
        for t in st:
            solo.train(CORPUS[t][1], t)

        best = max(self.nodes, key=lambda n: n.size)
        s_ok = r_ok = c_ok = 0

        print(f"\n  {'Вопрос':<40s} │ {'Solo':>5s} │ {'Рой1':>5s} │ {'Рой15':>6s}")
        print(f"  {'─'*40}─┼─{'─'*5}─┼─{'─'*5}─┼─{'─'*6}")

        for q, tid in CONTROL_QUESTIONS:
            _, sc = solo.answer(q)
            _, rc = best.answer(q)
            _, cc, _ = collective_answer(self.nodes, q, k=15)
            if sc > 0.1: s_ok += 1
            if rc > 0.1: r_ok += 1
            if cc > 0.1: c_ok += 1
            sf = f"{sc:.2f}" if sc > 0 else " --"
            rf = f"{rc:.2f}" if rc > 0 else " --"
            cf = f"{cc:.2f}" if cc > 0 else " --"
            sq = q[:38] + ".." if len(q) > 38 else q
            print(f"  {sq:<40s} │ {sf:>5s} │ {rf:>5s} │ {cf:>6s}")

        tt = len(CONTROL_QUESTIONS)
        print(f"  {'─'*40}─┼─{'─'*5}─┼─{'─'*5}─┼─{'─'*6}")
        print(f"  {'ИТОГО':>40s} │ {s_ok:>2d}/{tt} │ {r_ok:>2d}/{tt} │ {c_ok:>3d}/{tt}")
        print(f"\n  Одиночка знал {self.tpn} тем из {len(CORPUS)}")

    def proof_examples(self):
        print(f"\n{'═'*70}")
        print(f"  ПРИМЕРЫ КОЛЛЕКТИВНЫХ ОТВЕТОВ (ансамбль 15 узлов)")
        print(f"{'═'*70}")
        for idx in [0, 2, 4, 7, 9, 13, 15, 18]:
            q, tid = CONTROL_QUESTIONS[idx]
            ct, cc, ans = collective_answer(self.nodes, q, k=15)
            print(f"\n  ? {q}")
            print(f"    → {ct[:70]}")
            print(f"    увер={cc:.2f} | ответили={ans}/15")

    def proof_coverage(self):
        print(f"\n{'═'*70}")
        print(f"  ПОКРЫТИЕ ТЕМ (выборка 20 из 100)")
        print(f"{'═'*70}")
        print(f"\n  {'Тема':<20s} │ {'%':>6s} │ {'Визуально'}")
        print(f"  {'─'*20}─┼─{'─'*6}─┼─{'─'*40}")
        for tid in range(0, 100, 5):
            h = self._th[tid]
            kn = sum(1 for nd in self.nodes if sum(1 for x in h if x in nd.graph.patterns) >= 2)
            pct = kn / self.n * 100
            bar = "█" * int(pct / 2.5)
            print(f"  {TOPIC_NAMES[tid]:<20s} │ {pct:>5.1f}% │ {bar}")


def main():
    random.seed(42)
    N = 1000
    T = 20

    print("█" * 70)
    print(f"  KOLIBRI SWARM v3: {N} узлов × {T} тем (из {len(CORPUS)})")
    print("█" * 70)
    print(f"\n  Корпус: {len(CORPUS)} тем. Каждый узел обучен на {T}.")
    print(f"  Ни один узел не знает все {len(CORPUS)}. Gossip fanout=3.\n")
    sys.stdout.flush()

    sim = Swarm(n=N, tpn=T, fan=3)
    sim.init()
    sim.run(rounds=12)

    sim.proof()
    sim.proof_examples()
    sim.proof_coverage()

    f, l = sim.hist[0], sim.hist[-1]
    cx = l['cov'] / max(f['cov'], 0.1)

    print(f"\n{'█'*70}")
    print(f"  ВЕРДИКТ")
    print(f"{'█'*70}")
    print(f"  Корпус:      {len(CORPUS)} тем, каждый узел знал {T}")
    print(f"  Покрытие:    {f['cov']:.0f} → {l['cov']:.0f}/100 ({cx:.1f}x)")
    print(f"  Граф:        {f['sz']} → {l['sz']} паттернов")
    print(f"  Полное(≥95): {f['full']} → {l['full']}/{N}")
    print(f"\n  Каждый узел начинал с {T}% знаний.")
    print(f"  Через gossip каждый знает {l['cov']:.0f}% тем.")
    print(f"  На 1 млрд: ~{math.ceil(math.log(1e9)/math.log(5))+1} раундов.")
    print(f"{'█'*70}\n")


if __name__ == "__main__":
    main()
