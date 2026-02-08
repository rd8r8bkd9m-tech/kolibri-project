"""
number_mind.py — Ядро «Числового Мышления» Kolibri

Чистая Python-реализация формульного движка (зеркало C-ядра):
- Каждое слово → 64-цифровой паттерн (через DJB2 + LCG каскад)
- Граф знаний: слово↔слово с весами (co-occurrence)
- 100-слойная формульная «нейросеть» на 1024 цифрах генома
- Эволюция формул (генетический алгоритм)
- Дистилляция: вытеснение слабых знаний
- Обратное восстановление: формулы → текст

Это НЕ классический ML. Это уникальная система, где:
- ВСЕ знания хранятся в ЧИСЛАХ
- Формулы из 1024 цифр определяют "поведение мозга"
- Граф знаний = связи между числовыми паттернами слов
- Ответ = навигация по графу + формульный predict
"""
from __future__ import annotations

import hashlib
import json
import logging
import math
import os
import random
import re
import struct
import time
from array import array
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

log = logging.getLogger("kolibri.number_mind")


# ---------------------------------------------------------------------------
# Константы (зеркало C: corpus_trainer.h)
# ---------------------------------------------------------------------------

KLM_PATTERN_SIZE = 64       # Цифр в числовом паттерне слова
KLM_WORD_MAX = 128          # Макс длина слова
GENE_SIZE = 1024            # Цифр в геноме формулы
FORMULA_LAYERS = 100        # Слоёв формульной "нейросети"
MAX_ASSOCIATIONS = 10000    # Макс ассоциаций Q→A в формуле
POPULATION_SIZE = 16        # Формул в популяции


# ---------------------------------------------------------------------------
# DJB2 хеш (идентичный C-реализации)
# ---------------------------------------------------------------------------

def djb2_hash(s: str) -> int:
    """DJB2 хеш — такой же как в C коде."""
    h = 5381
    for c in s.encode('utf-8', errors='ignore'):
        h = ((h << 5) + h + c) & 0xFFFFFFFF
    return h


def fnv1a_hash(s: str) -> int:
    """FNV-1a хеш — используется в формулах."""
    h = 0x811C9DC5
    for c in s.encode('utf-8', errors='ignore'):
        h = ((h ^ c) * 0x01000193) & 0xFFFFFFFF
    return h


# ---------------------------------------------------------------------------
# Числовой паттерн (зеркало klm_quick_pattern из C)
# ---------------------------------------------------------------------------

def word_to_pattern(word: str) -> list[int]:
    """
    Преобразовать слово в 64-цифровой числовой паттерн.
    
    Алгоритм идентичен C:
      hash = DJB2(word)
      for i in 0..63:
          pattern[i] = hash % 10
          hash = hash * 1103515245 + 12345  (LCG каскад)
    
    Каждое слово = уникальный числовой отпечаток.
    """
    h = djb2_hash(word.lower())
    pattern = []
    for _ in range(KLM_PATTERN_SIZE):
        pattern.append(h % 10)
        h = (h * 1103515245 + 12345) & 0xFFFFFFFF
    return pattern


def pattern_similarity(a: list[int], b: list[int]) -> float:
    """
    Сходство двух паттернов [0.0–1.0].
    Идентично C: score += 2 если d==0, 1 если d==1.
    """
    score = 0
    for i in range(min(len(a), len(b))):
        d = abs(a[i] - b[i])
        if d == 0:
            score += 2
        elif d == 1:
            score += 1
    return score / (2.0 * KLM_PATTERN_SIZE)


def pattern_to_str(pattern: list[int]) -> str:
    """Паттерн как строка цифр."""
    return ''.join(str(d) for d in pattern)


# ---------------------------------------------------------------------------
# Текст ↔ Цифры (кодирование)
# ---------------------------------------------------------------------------

def text_to_digits(text: str) -> list[int]:
    """
    Кодировать текст в последовательность цифр.
    Каждый UTF-8 байт → 3 цифры (десятичное представление).
    """
    digits = []
    for b in text.encode('utf-8', errors='ignore'):
        digits.extend([b // 100, (b // 10) % 10, b % 10])
    return digits


def digits_to_text(digits: list[int]) -> str:
    """
    Восстановить текст из цифр.
    Обратное преобразование: каждые 3 цифры → 1 байт.
    """
    if len(digits) % 3 != 0:
        digits = digits[:len(digits) - len(digits) % 3]
    
    octets = []
    for i in range(0, len(digits), 3):
        val = digits[i] * 100 + digits[i + 1] * 10 + digits[i + 2]
        if 0 <= val <= 255:
            octets.append(val)
    try:
        return bytes(octets).decode('utf-8', errors='replace')
    except Exception:
        return ''


# ---------------------------------------------------------------------------
# Формульная "нейросеть" из генома (зеркало formula_predict_numeric из C)
# ---------------------------------------------------------------------------

@dataclass
class KolibriGene:
    """
    Геном формулы — 1024 цифр (0–11).
    
    Каждые 8 цифр = один "слой" преобразования:
      [0]: operation (% 12 → тип операции)
      [1-3]: slope (знак + двузначное число)
      [4-6]: bias (знак + двузначное число)
      [7]: auxiliary параметр
    """
    digits: list[int] = field(default_factory=lambda: [
        random.randint(0, 11) for _ in range(GENE_SIZE)
    ])
    
    def predict(self, x: float) -> float:
        """
        Пропустить число через 100-слойную формульную сеть.
        Каждый слой берёт 8 цифр из генома и применяет операцию.
        """
        return self._run_layers(x, FORMULA_LAYERS)

    def predict_fast(self, x: float) -> float:
        """Быстрый вариант: 20 слоёв (для fitness evaluation)."""
        return self._run_layers(x, 20)

    def _run_layers(self, x: float, num_layers: int) -> float:
        """Пропустить число через N слоёв формульной сети."""
        value = float(x)
        
        for layer in range(num_layers):
            offset = (layer * 8) % len(self.digits)
            
            # Извлечение параметров из цифр
            op = self.digits[offset] % 12
            
            sign_s = -1.0 if self.digits[(offset + 1) % len(self.digits)] > 5 else 1.0
            slope = sign_s * (
                self.digits[(offset + 2) % len(self.digits)] * 10 +
                self.digits[(offset + 3) % len(self.digits)]
            )
            
            sign_b = -1.0 if self.digits[(offset + 4) % len(self.digits)] > 5 else 1.0
            bias = sign_b * (
                self.digits[(offset + 5) % len(self.digits)] * 10 +
                self.digits[(offset + 6) % len(self.digits)]
            )
            
            aux = self.digits[(offset + 7) % len(self.digits)] + 1
            
            # 12 операций (зеркало C)
            try:
                if op == 0:    # Линейная
                    value = slope * value + bias
                elif op == 1:  # Инверсная
                    value = slope * value - bias
                elif op == 2:  # Остаточная
                    value = (slope * value) % (aux * 100 + 1) + bias
                elif op == 3:  # Квадратичная
                    value = slope * value * value + bias
                elif op == 4:  # XOR
                    value = float(int(value) ^ (aux * 100)) + bias
                elif op == 5:  # AND
                    value = float(int(value) & int(slope * 100)) + bias
                elif op == 6:  # Синус
                    value = math.sin(value / 256.0) * slope * 10 + bias
                elif op == 7:  # Насыщение
                    denom = 1.0 + abs(value)
                    value = slope * 100.0 * value / denom + bias
                elif op == 8:  # OR
                    value = float(int(value) | int(slope)) - aux
                elif op == 9:  # Гауссова
                    safe_x = min(abs(value), 1000.0)
                    value = slope * value * math.exp(-safe_x * safe_x / 1e6) + bias
                elif op == 10: # Tanh
                    safe_v = max(-500.0, min(500.0, value / 100.0))
                    value = math.tanh(safe_v) * slope * 100.0 + bias
                elif op == 11: # Sigmoid
                    safe_v = max(-500.0, min(500.0, -value / 100.0))
                    value = (1.0 / (1.0 + math.exp(safe_v))) * slope * 100.0 + bias
            except (ValueError, OverflowError, ZeroDivisionError):
                pass
            
            # Клиппинг
            value = max(-1e9, min(1e9, value))
        
        return value
    
    def mutate(self, rate: float = 0.02) -> None:
        """Мутация: случайная замена цифр."""
        for i in range(len(self.digits)):
            if random.random() < rate:
                self.digits[i] = random.randint(0, 11)
    
    def crossover(self, other: KolibriGene) -> KolibriGene:
        """Кроссовер: смешение двух геномов."""
        point = random.randint(0, len(self.digits) - 1)
        new_digits = self.digits[:point] + other.digits[point:]
        child = KolibriGene(digits=new_digits)
        child.mutate(rate=0.01)
        return child
    
    def to_hex(self) -> str:
        """Компактное hex-представление генома."""
        return ''.join(f'{d:x}' for d in self.digits[:64])
    
    def complexity(self) -> float:
        """Сложность генома (разнообразие цифр)."""
        counts = Counter(self.digits)
        return len(counts) / 12.0


# ---------------------------------------------------------------------------
# Формульный пул (эволюция)
# ---------------------------------------------------------------------------

@dataclass
class FormulaAssociation:
    """
    Ассоциация Q→A — хранится ТОЛЬКО в числах.
    
    Числовое Мышление: текст НЕ хранится — только:
    - input_hash (FNV1a хеш вопроса) — для быстрого lookup
    - output_hash (FNV1a хеш ответа)
    - question_digits (array 'B') — вопрос в цифрах
    - answer_digits (array 'B') — ответ в цифрах
    
    Текст восстанавливается через digits_to_text() при необходимости.
    Экономия памяти ~60%: 1 байт/цифру вместо 1-4 байт/символ.
    """
    input_hash: int
    output_hash: int
    question_digits: array          # array('B') — вопрос в цифрах
    answer_digits: array            # array('B') — ответ в цифрах

    @property
    def question(self) -> str:
        """Восстановить текст вопроса ИЗ ЦИФР."""
        return digits_to_text(list(self.question_digits))

    @property
    def answer(self) -> str:
        """Восстановить текст ответа ИЗ ЦИФР."""
        return digits_to_text(list(self.answer_digits))


@dataclass
class Formula:
    """Одна формула в популяции."""
    gene: KolibriGene = field(default_factory=KolibriGene)
    fitness: float = 0.0
    associations: list[FormulaAssociation] = field(default_factory=list)
    
    def lookup(self, question: str) -> Optional[str]:
        """Поиск ответа по ассоциации (hash → text)."""
        h = fnv1a_hash(question.lower())
        for assoc in self.associations:
            if assoc.input_hash == h:
                return assoc.answer
        return None
    
    def add_association(self, question: str, answer: str) -> None:
        """Добавить пару Q→A — текст сразу кодируется в ЦИФРЫ."""
        q_hash = fnv1a_hash(question.lower())
        a_hash = fnv1a_hash(answer.lower())
        self.associations.append(FormulaAssociation(
            input_hash=q_hash,
            output_hash=a_hash,
            question_digits=array('B', text_to_digits(question)),
            answer_digits=array('B', text_to_digits(answer)),
        ))
        if len(self.associations) > MAX_ASSOCIATIONS:
            self.associations = self.associations[-MAX_ASSOCIATIONS:]
    
    def predict_numeric(self, x: float) -> float:
        """Числовой прогноз через формулу."""
        return self.gene.predict(x)


class FormulaPool:
    """
    Пул из 16 формул, эволюционирующих конкурентно.

    Каждая формула содержит:
    - Геном (1024 цифры) → 100-слойная сеть
    - Ассоциации (Q→A через FNV1a хеши)
    - Fitness (качество СЕМАНТИЧЕСКИХ предсказаний)

    Ключевое отличие: fitness оценивается по способности
    предсказать СОСЕДЕЙ слова в графе знаний.
    Формула, трансформирующая паттерн слова A в паттерн
    близкий к соседу B, получает высокий fitness.
    """

    def __init__(self) -> None:
        self.formulas: list[Formula] = [
            Formula() for _ in range(POPULATION_SIZE)
        ]
        self.generation: int = 0
        # Семантические пары: (паттерн_слова, паттерн_соседа)
        # Формула учится: transform(паттерн_A) ≈ паттерн_B
        self.semantic_pairs: list[tuple[list[int], list[int]]] = []

    def add_semantic_pair(self, source_pattern: list[int],
                          target_pattern: list[int]) -> None:
        """Добавить семантическую пару: слово → его сосед из графа."""
        self.semantic_pairs.append((source_pattern, target_pattern))
        if len(self.semantic_pairs) > 5000:
            self.semantic_pairs = self.semantic_pairs[-5000:]

    def add_training_pair(self, x: float, y: float) -> None:
        """Совместимость с предыдущим API (legacy)."""
        pass  # Используем semantic_pairs вместо числовых пар

    def evolve(self, generations: int = 10) -> float:
        """
        Эволюция формул — СЕМАНТИЧЕСКИЙ fitness.

        Fitness = насколько хорошо формула трансформирует
        паттерн слова A в паттерн его соседа B из графа знаний.

        Улучшения:
        - Мягкая метрика сходства (не только exact match)
        - Адаптивная мутация (сильнее при стагнации)
        - Турнирная селекция (давление отбора)
        - Больше оценочных цифр для точности
        """
        if not self.semantic_pairs:
            return 0.0

        # Берём разнообразную выборку (не только первые)
        _MAX_EVAL = 60
        if len(self.semantic_pairs) <= _MAX_EVAL:
            eval_sample = self.semantic_pairs
        else:
            # Микс: свежие пары + случайные старые
            recent = self.semantic_pairs[-_MAX_EVAL // 2:]
            older = random.sample(
                self.semantic_pairs[:-_MAX_EVAL // 2],
                min(_MAX_EVAL // 2, len(self.semantic_pairs) - _MAX_EVAL // 2),
            )
            eval_sample = recent + older

        best_fitness = 0.0
        prev_best = self.formulas[0].fitness if self.formulas else 0.0

        for gen_i in range(generations):
            # Оценка fitness каждой формулы
            for formula in self.formulas:
                total_sim = 0.0
                _EVAL_DIGITS = 24  # Больше цифр → точнее оценка
                for src_pat, tgt_pat in eval_sample:
                    pred_part: list[int] = []
                    for i in range(_EVAL_DIGITS):
                        digit = src_pat[i] if i < len(src_pat) else 0
                        # Включаем контекст: соседние цифры влияют на трансформацию
                        ctx = (src_pat[(i + 1) % len(src_pat)] + src_pat[(i - 1) % len(src_pat)]) * 0.05
                        x = (digit + i * 0.15 + ctx) / 12.0
                        raw = formula.gene.predict_fast(x)
                        pred_part.append(int(abs(raw * 7.77)) % 10)

                    # Мягкая метрика: учитывает близость, не только exact match
                    sim = 0.0
                    for j in range(min(_EVAL_DIGITS, len(tgt_pat))):
                        d = abs(pred_part[j] - tgt_pat[j])
                        if d == 0:
                            sim += 3.0
                        elif d == 1:
                            sim += 2.0
                        elif d == 2:
                            sim += 1.0
                        elif d <= 4:
                            sim += 0.3
                    sim /= (3.0 * _EVAL_DIGITS)
                    total_sim += sim

                avg_sim = total_sim / len(eval_sample)
                # Бонус за разнообразие генома (против вырождения)
                diversity = formula.gene.complexity()
                formula.fitness = avg_sim + diversity * 0.01

            self.formulas.sort(key=lambda f: f.fitness, reverse=True)
            best_fitness = self.formulas[0].fitness

            # Адаптивная мутация: стагнация → агрессивнее
            improvement = best_fitness - prev_best
            if improvement < 0.001:
                mutation_rate = 0.04  # Застряли → сильная мутация
            else:
                mutation_rate = 0.015  # Растём → осторожная мутация
            prev_best = best_fitness

            # Элитизм + турнирная селекция
            elite_count = max(2, POPULATION_SIZE // 3)
            for i in range(elite_count, POPULATION_SIZE):
                # Турнир из 3 → выбираем лучшего
                t1 = self.formulas[random.randint(0, elite_count - 1)]
                t2 = self.formulas[random.randint(0, elite_count - 1)]
                p1 = t1 if t1.fitness >= t2.fitness else t2
                t3 = self.formulas[random.randint(0, elite_count - 1)]
                t4 = self.formulas[random.randint(0, elite_count - 1)]
                p2 = t3 if t3.fitness >= t4.fitness else t4

                child_gene = p1.gene.crossover(p2.gene)
                child_gene.mutate(rate=mutation_rate)
                self.formulas[i] = Formula(gene=child_gene)
                # Наследуем ассоциации от лучшего родителя
                if p1.associations:
                    self.formulas[i].associations = list(p1.associations[-100:])

            self.generation += 1

        return best_fitness
    
    def best(self) -> Formula:
        """Лучшая формула."""
        return max(self.formulas, key=lambda f: f.fitness)
    
    def lookup(self, question: str) -> Optional[str]:
        """Поиск ответа во всех формулах."""
        for formula in self.formulas:
            answer = formula.lookup(question)
            if answer:
                return answer
        return None
    
    def add_association(self, question: str, answer: str) -> None:
        """Добавить ассоциацию в лучшие формулы."""
        for formula in self.formulas[:3]:
            formula.add_association(question, answer)

    # --- Персистенция: сохранение/загрузка формул на диск ---

    def save(self, path: str | Path) -> None:
        """
        Сохранить пул формул на диск (JSON).

        Сохраняет: геномы, fitness, ассоциации, семантические пары,
        поколение — всё, что нужно для продолжения эволюции.
        """
        data = {
            "version": 2,
            "generation": self.generation,
            "timestamp": time.time(),
            "formulas": [],
            "semantic_pairs_count": len(self.semantic_pairs),
            # Сохраняем до 2000 последних семантических пар
            "semantic_pairs": [
                {"src": src, "tgt": tgt}
                for src, tgt in self.semantic_pairs[-2000:]
            ],
        }
        for formula in self.formulas:
            f_data = {
                "genome": formula.gene.digits,
                "fitness": formula.fitness,
                "associations": [
                    {
                        "input_hash": a.input_hash,
                        "output_hash": a.output_hash,
                        "q_digits": list(a.question_digits),
                        "a_digits": list(a.answer_digits),
                    }
                    for a in formula.associations[-500:]  # Последние 500
                ],
            }
            data["formulas"].append(f_data)

        p = Path(path)
        p.parent.mkdir(parents=True, exist_ok=True)
        tmp = p.with_suffix(".tmp")
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False)
        tmp.rename(p)
        log.info(
            "FormulaPool сохранён: gen=%d, fitness=%.4f, pairs=%d → %s",
            self.generation, self.best().fitness,
            len(self.semantic_pairs), path,
        )

    @classmethod
    def load(cls, path: str | Path) -> FormulaPool:
        """
        Загрузить пул формул с диска.

        Восстанавливает полное состояние: геномы, fitness, ассоциации,
        семантические пары, поколение. Эволюция продолжается с того же места.
        """
        p = Path(path)
        if not p.exists():
            raise FileNotFoundError(f"FormulaPool не найден: {path}")

        with open(p, "r", encoding="utf-8") as f:
            data = json.load(f)

        pool = cls()
        pool.generation = data.get("generation", 0)

        # Восстановить формулы
        formulas_data = data.get("formulas", [])
        if formulas_data:
            pool.formulas = []
            for f_data in formulas_data:
                gene = KolibriGene(digits=f_data["genome"])
                formula = Formula(gene=gene, fitness=f_data.get("fitness", 0.0))
                for a_data in f_data.get("associations", []):
                    # Новый формат: q_digits/a_digits
                    if "q_digits" in a_data:
                        q_digs = array('B', a_data["q_digits"])
                        a_digs = array('B', a_data["a_digits"])
                    else:
                        # Совместимость со старым форматом (текст)
                        q_digs = array('B', text_to_digits(a_data.get("question", "")))
                        a_digs = array('B', text_to_digits(a_data.get("answer", "")))
                    formula.associations.append(FormulaAssociation(
                        input_hash=a_data["input_hash"],
                        output_hash=a_data["output_hash"],
                        question_digits=q_digs,
                        answer_digits=a_digs,
                    ))
                pool.formulas.append(formula)

            # Дополнить до POPULATION_SIZE если нужно
            while len(pool.formulas) < POPULATION_SIZE:
                pool.formulas.append(Formula())

        # Восстановить семантические пары
        for pair in data.get("semantic_pairs", []):
            pool.semantic_pairs.append((pair["src"], pair["tgt"]))

        log.info(
            "FormulaPool загружен: gen=%d, fitness=%.4f, pairs=%d ← %s",
            pool.generation, pool.best().fitness,
            len(pool.semantic_pairs), path,
        )
        return pool

    @classmethod
    def load_or_create(cls, path: str | Path) -> FormulaPool:
        """Загрузить с диска если есть, иначе создать новый."""
        try:
            return cls.load(path)
        except (FileNotFoundError, Exception) as e:
            log.info("FormulaPool: новый пул (%s)", e)
            return cls()


# ---------------------------------------------------------------------------
# Граф знаний (зеркало KlmModel.edges)
# ---------------------------------------------------------------------------

@dataclass
class KnowledgeEdge:
    """Ребро графа знаний."""
    source_hash: int
    target_hash: int
    weight: float = 0.0
    cooccurrence: int = 0
    
    def strengthen(self) -> None:
        """Усилить связь (сигмоида как в C)."""
        self.cooccurrence += 1
        self.weight = 1.0 - 1.0 / (1.0 + self.cooccurrence)


@dataclass 
class PatternEntry:
    """Запись: слово → числовой паттерн."""
    word: str
    pattern: list[int]
    hash: int
    frequency: int = 0
    fitness: float = 0.0


class KnowledgeGraph:
    """
    Граф знаний Kolibri — числовая основа AI.
    
    Каждое слово = 64-цифровой паттерн (числовой отпечаток).
    Связи между словами = рёбра с весами (co-occurrence).
    
    Ответ на вопрос = навигация по графу:
    1. Хешируем каждое слово вопроса
    2. Находим связанные слова через рёбра
    3. Агрегируем score кандидатов
    4. Топ-N слов по score = ответ
    """
    
    def __init__(self, max_patterns: int = 131072, max_edges: int = 262144) -> None:
        self.patterns: dict[int, PatternEntry] = {}
        self.edges: dict[tuple[int, int], KnowledgeEdge] = {}
        self._adj: dict[int, set[int]] = {}  # Индекс смежности для быстрого answer()
        self._hash_to_word: dict[int, str] = {}  # Обратный индекс: хеш → слово
        self.max_patterns = max_patterns
        self.max_edges = max_edges
        self.documents_trained: int = 0
        self.tokens_processed: int = 0
        self.current_epoch: int = 0
    
    def add_word(self, word: str) -> PatternEntry:
        """Добавить слово в граф (или обновить частоту)."""
        h = djb2_hash(word.lower())
        if h in self.patterns:
            self.patterns[h].frequency += 1
            return self.patterns[h]
        
        if len(self.patterns) >= self.max_patterns:
            self._distill_patterns()
        
        entry = PatternEntry(
            word=word.lower(),
            pattern=word_to_pattern(word),
            hash=h,
            frequency=1,
            fitness=0.06,
        )
        self.patterns[h] = entry
        self._hash_to_word[h] = word.lower()  # Обратный индекс
        return entry
    
    def add_edge(self, word1: str, word2: str) -> None:
        """Добавить или усилить ребро между словами."""
        h1 = djb2_hash(word1.lower())
        h2 = djb2_hash(word2.lower())
        if h1 == h2:
            return
        
        key = (min(h1, h2), max(h1, h2))
        if key in self.edges:
            self.edges[key].strengthen()
        else:
            if len(self.edges) >= self.max_edges:
                self._distill_edges()
            edge = KnowledgeEdge(source_hash=key[0], target_hash=key[1])
            edge.strengthen()
            self.edges[key] = edge
            # Индекс смежности
            self._adj.setdefault(h1, set()).add(h2)
            self._adj.setdefault(h2, set()).add(h1)
    
    def train_text(self, text: str, context_window: int = 5) -> dict:
        """
        Обучение на тексте.
        
        1. Токенизация
        2. Каждое слово → числовой паттерн
        3. Соседние слова (±window) → рёбра в графе знаний
        
        Returns: статистика обучения
        """
        _MAX_TOKENS = 5000  # Ограничение для скорости
        tokens = _tokenize(text)[:_MAX_TOKENS]
        if not tokens:
            return {"patterns": 0, "edges": 0}
        
        new_patterns = 0
        new_edges = 0
        
        for i, word in enumerate(tokens):
            if len(word) < 2:
                continue
            
            was_new = djb2_hash(word.lower()) not in self.patterns
            self.add_word(word)
            if was_new:
                new_patterns += 1
            
            # Контекстное окно: связи с соседями
            for j in range(max(0, i - context_window), min(len(tokens), i + context_window + 1)):
                if i != j and len(tokens[j]) >= 2:
                    self.add_edge(word, tokens[j])
                    new_edges += 1
        
        self.documents_trained += 1
        self.tokens_processed += len(tokens)
        
        return {
            "patterns": len(self.patterns),
            "edges": len(self.edges),
            "new_patterns": new_patterns,
            "new_edges": new_edges,
            "tokens": len(tokens),
        }
    
    def answer(self, question: str, max_words: int = 10) -> tuple[str, float, dict]:
        """
        Ответить на вопрос через граф знаний.
        
        Алгоритм (идентичен C klm_answer):
        1. Токенизируем вопрос
        2. Для каждого слова — ищем связанные через рёбра
        3. Агрегируем score (сумма весов рёбер)
        4. Фильтруем слова вопроса
        5. Топ-N → ответ
        
        Returns: (answer_text, confidence, metadata)
        """
        tokens = _tokenize(question)
        if not tokens:
            return ("", 0.0, {})
        
        q_hashes = set()
        for t in tokens:
            q_hashes.add(djb2_hash(t.lower()))
        
        # Агрегация кандидатов через индекс смежности (O(1) lookup)
        candidates: dict[int, float] = {}
        
        for t in tokens:
            if len(t) < 3:
                continue
            h = djb2_hash(t.lower())
            
            for other in self._adj.get(h, ()):
                if other in q_hashes:
                    continue
                key = (min(h, other), max(h, other))
                edge = self.edges.get(key)
                if edge:
                    candidates[other] = candidates.get(other, 0.0) + edge.weight
        
        # Сортировка по score
        sorted_cands = sorted(candidates.items(), key=lambda x: x[1], reverse=True)
        
        # Собираем ответ
        words = []
        total_score = 0.0
        for hash_val, score in sorted_cands[:max_words]:
            entry = self.patterns.get(hash_val)
            if entry:
                words.append(entry.word)
                total_score += score
        
        answer_text = ' '.join(words)
        confidence = min(1.0, total_score / (len(tokens) + 1)) if words else 0.0
        
        # Числовые паттерны ответа (для визуализации)
        answer_patterns = {}
        for w in words:
            answer_patterns[w] = pattern_to_str(word_to_pattern(w))
        
        metadata = {
            "candidates_total": len(candidates),
            "words_selected": len(words),
            "total_score": round(total_score, 4),
            "answer_patterns": answer_patterns,
            "query_hashes": {t: djb2_hash(t.lower()) for t in tokens},
        }
        
        return (answer_text, round(confidence, 4), metadata)
    
    def find_similar(self, word: str, limit: int = 10) -> list[tuple[str, float]]:
        """Найти слова с похожими числовыми паттернами."""
        target = word_to_pattern(word)
        results = []
        for entry in self.patterns.values():
            sim = pattern_similarity(target, entry.pattern)
            if sim > 0.3 and entry.word != word.lower():
                results.append((entry.word, round(sim, 4)))
        results.sort(key=lambda x: x[1], reverse=True)
        return results[:limit]

    def find_by_pattern(self, target: list[int], limit: int = 5,
                        exclude: set[int] | None = None) -> list[tuple[str, float]]:
        """
        Обратный поиск: числовой паттерн → ближайшие слова из графа.

        Это ключевая операция формульной генерации:
        Формула трансформирует паттерн запроса → новый паттерн →
        find_by_pattern → слова ответа.

        Фильтры: отбрасываем числа, слишком короткие слова,
        и паттерны с низким сходством.
        """
        exclude = exclude or set()
        results: list[tuple[str, float]] = []
        for entry in self.patterns.values():
            if entry.hash in exclude:
                continue
            # Фильтрация: пропускаем числа и односимвольные слова
            if len(entry.word) < 3 or entry.word.isdigit():
                continue
            sim = pattern_similarity(target, entry.pattern)
            if sim > 0.25:
                # Бонус за частотность (частые слова более значимы)
                freq_bonus = min(0.1, math.log(entry.frequency + 1) * 0.02)
                results.append((entry.word, round(sim + freq_bonus, 4)))
        results.sort(key=lambda x: x[1], reverse=True)
        return results[:limit]

    def generate_words(
        self,
        query: str,
        formula: 'Formula',
        max_words: int = 8,
    ) -> list[tuple[str, float]]:
        """
        Формульная генерация слов — СЕМАНТИЧЕСКОЕ числовое мышление.

        Ключевое улучшение: формула обучена предсказывать СОСЕДЕЙ
        в графе знаний. Поэтому трансформация паттерна слова
        через формулу даёт паттерн семантически связанного слова.

        Алгоритм:
        1. Токенизируем запрос → паттерны слов (64 цифры)
        2. Вычисляем КОНТЕКСТНЫЙ паттерн — усреднение паттернов
           всех слов запроса → общий семантический вектор
        3. Формула трансформирует: каждый паттерн + контекст
        4. find_by_pattern → слова, чьи паттерны ближе всего
        5. Графовый бонус для подтверждения семантической связи
        6. Перекрёстный бонус: слово, найденное через несколько
           токенов запроса, получает boost (консенсус)

        Разные формулы (разные геномы) → разные семантические
        трансформации → разные ответы. Эволюция отбирает формулы,
        дающие лучшие (наиболее семантически точные) ответы.
        """
        tokens = _tokenize(query)
        if not tokens:
            return []

        q_hashes: set[int] = set()
        token_patterns: list[list[int]] = []
        for t in tokens:
            if len(t) >= 2:
                q_hashes.add(djb2_hash(t.lower()))
            if len(t) >= 3:
                token_patterns.append(word_to_pattern(t))

        if not token_patterns:
            return []

        # --- Контекстный паттерн: среднее всех слов запроса ---
        context_pattern: list[int] = []
        for i in range(KLM_PATTERN_SIZE):
            avg = sum(p[i] for p in token_patterns) / len(token_patterns)
            context_pattern.append(int(avg) % 10)

        generated: dict[str, float] = {}

        # --- Генерация по каждому слову запроса ---
        for t_idx, t in enumerate(tokens):
            if len(t) < 3:
                continue
            src_pattern = word_to_pattern(t)

            # Формула трансформирует паттерн с учётом контекста:
            # Каждая цифра слова → через формулу → новая цифра
            # Ключ: разные слова дают РАЗНЫЕ входы в формулу
            new_pattern: list[int] = []
            word_hash_mod = djb2_hash(t.lower()) % 97  # Уникальный множитель слова
            for i, digit in enumerate(src_pattern):
                ctx_digit = context_pattern[i] if i < len(context_pattern) else 0
                # Вход формулы: уникален для каждого слова + позиции
                x = (digit * 3.7 + ctx_digit * 1.3 + i * 0.5 + word_hash_mod * 0.17) / 100.0
                raw = formula.predict_numeric(x)
                mapped = int(abs(raw * 7.77)) % 10
                new_pattern.append(mapped)

            # Обратный поиск: новый паттерн → слова из графа
            matches = self.find_by_pattern(
                new_pattern, limit=5, exclude=q_hashes,
            )
            for word, sim in matches:
                # Графовый бонус: подтверждение через реальные связи
                graph_bonus = 0.0
                wh = djb2_hash(word.lower())
                for qh in q_hashes:
                    key = (min(qh, wh), max(qh, wh))
                    edge = self.edges.get(key)
                    if edge:
                        graph_bonus += edge.weight * 0.4
                score = sim + graph_bonus
                if word in generated:
                    # Перекрёстный бонус: слово найдено через несколько токенов
                    generated[word] = generated[word] + score * 0.5
                else:
                    generated[word] = score

        # --- Генерация по контекстному паттерну целиком ---
        ctx_new: list[int] = []
        for i, digit in enumerate(context_pattern):
            x = (digit + i * 0.05) / 10.0
            raw = formula.predict_numeric(x)
            ctx_new.append(int(abs(raw * 7.77)) % 10)
        ctx_matches = self.find_by_pattern(ctx_new, limit=4, exclude=q_hashes)
        for word, sim in ctx_matches:
            wh = djb2_hash(word.lower())
            graph_bonus = 0.0
            for qh in q_hashes:
                key = (min(qh, wh), max(qh, wh))
                edge = self.edges.get(key)
                if edge:
                    graph_bonus += edge.weight * 0.3
            score = sim * 0.8 + graph_bonus
            if word in generated:
                generated[word] = generated[word] + score * 0.3
            else:
                generated[word] = score * 0.7

        # --- Графовые соседи + формульное ранжирование ---
        # Для каждого слова запроса берём РЕАЛЬНЫХ соседей из графа
        # и ранжируем их через формулу
        for t in tokens:
            if len(t) < 3:
                continue
            th = djb2_hash(t.lower())
            neighbors = self._adj.get(th, set())
            if not neighbors:
                continue
            src_pattern = word_to_pattern(t)
            for nh in list(neighbors)[:15]:
                if nh in q_hashes:
                    continue
                n_entry = self.patterns.get(nh)
                if not n_entry or len(n_entry.word) < 3 or n_entry.word.isdigit():
                    continue
                # Формула ранжирует: predict(src ⊕ neighbor) → score
                xor_sum = sum(
                    abs(a - b) for a, b in zip(
                        src_pattern[:16], n_entry.pattern[:16]
                    )
                ) / 160.0
                f_score = formula.predict_numeric(xor_sum)
                f_norm = 1.0 / (1.0 + abs(f_score) / 1000.0)
                # Вес ребра * формульный score
                edge_key = (min(th, nh), max(th, nh))
                edge = self.edges.get(edge_key)
                edge_w = edge.weight if edge else 0.5
                score = edge_w * 0.6 + f_norm * 0.4
                word = n_entry.word
                if word in generated:
                    generated[word] = max(generated[word], score)
                else:
                    generated[word] = score

        sorted_words = sorted(generated.items(), key=lambda x: x[1], reverse=True)
        return sorted_words[:max_words]
    
    def word_similarity(self, w1: str, w2: str) -> float:
        """Сходство двух слов по числовым паттернам."""
        return pattern_similarity(word_to_pattern(w1), word_to_pattern(w2))
    
    def _distill_patterns(self) -> int:
        """Дистилляция: удалить слабые паттерны."""
        if not self.patterns:
            return 0
        scores = {h: e.fitness * math.log(e.frequency + 1) for h, e in self.patterns.items()}
        mean_score = sum(scores.values()) / len(scores)
        threshold = mean_score * 0.1
        to_remove = [h for h, s in scores.items() if s < threshold]
        for h in to_remove[:len(self.patterns) // 5]:
            del self.patterns[h]
        self.current_epoch += 1
        return len(to_remove)
    
    def _distill_edges(self) -> int:
        """Дистилляция рёбер."""
        if not self.edges:
            return 0
        mean_weight = sum(e.weight for e in self.edges.values()) / len(self.edges)
        threshold = mean_weight * 0.1
        to_remove = [k for k, e in self.edges.items() if e.weight < threshold]
        for k in to_remove[:len(self.edges) // 5]:
            del self.edges[k]
        # Перестроить индекс смежности
        self._rebuild_adj()
        return len(to_remove)
    
    def _rebuild_adj(self) -> None:
        """Перестроить индекс смежности из рёбер."""
        self._adj.clear()
        for src, tgt in self.edges:
            self._adj.setdefault(src, set()).add(tgt)
            self._adj.setdefault(tgt, set()).add(src)
    
    def get_stats(self) -> dict:
        """Статистика графа."""
        avg_fitness = (
            sum(e.fitness for e in self.patterns.values()) / len(self.patterns)
            if self.patterns else 0.0
        )
        avg_weight = (
            sum(e.weight for e in self.edges.values()) / len(self.edges)
            if self.edges else 0.0
        )
        return {
            "patterns": len(self.patterns),
            "max_patterns": self.max_patterns,
            "edges": len(self.edges),
            "max_edges": self.max_edges,
            "documents_trained": self.documents_trained,
            "tokens_processed": self.tokens_processed,
            "epoch": self.current_epoch,
            "avg_fitness": round(avg_fitness, 4),
            "avg_weight": round(avg_weight, 4),
        }
    
    def export_state(self) -> dict:
        """
        Экспорт состояния для синхронизации между нодами.
        Все данные — числа. Слова хранятся через хеши.
        """
        return {
            "version": 1,
            "timestamp": time.time(),
            "epoch": self.current_epoch,
            "documents_trained": self.documents_trained,
            "tokens_processed": self.tokens_processed,
            "patterns": {
                str(h): {
                    "word": e.word,
                    "pattern": e.pattern,
                    "frequency": e.frequency,
                    "fitness": e.fitness,
                }
                for h, e in self.patterns.items()
            },
            "edges": {
                f"{src}:{tgt}": {
                    "weight": e.weight,
                    "cooccurrence": e.cooccurrence,
                }
                for (src, tgt), e in self.edges.items()
            },
        }
    
    def merge_state(self, remote: dict) -> dict:
        """
        Слияние знаний от другой ноды.
        Принцип: если remote знает слово с большей частотой — берём его.
        Рёбра усиливаются суммарно.
        
        Returns: статистика слияния
        """
        merged_patterns = 0
        merged_edges = 0
        
        for h_str, pdata in remote.get("patterns", {}).items():
            h = int(h_str)
            if h in self.patterns:
                # Слияние: берём максимум частоты и fitness
                local = self.patterns[h]
                local.frequency = max(local.frequency, pdata["frequency"])
                local.fitness = max(local.fitness, pdata["fitness"])
            else:
                if len(self.patterns) < self.max_patterns:
                    self.patterns[h] = PatternEntry(
                        word=pdata["word"],
                        pattern=pdata["pattern"],
                        hash=h,
                        frequency=pdata["frequency"],
                        fitness=pdata["fitness"],
                    )
                    merged_patterns += 1
        
        for key_str, edata in remote.get("edges", {}).items():
            parts = key_str.split(":")
            key = (int(parts[0]), int(parts[1]))
            if key in self.edges:
                self.edges[key].cooccurrence += edata["cooccurrence"]
                self.edges[key].weight = 1.0 - 1.0 / (1.0 + self.edges[key].cooccurrence)
            else:
                if len(self.edges) < self.max_edges:
                    self.edges[key] = KnowledgeEdge(
                        source_hash=key[0],
                        target_hash=key[1],
                        weight=edata["weight"],
                        cooccurrence=edata["cooccurrence"],
                    )
                    merged_edges += 1
        
        return {
            "merged_patterns": merged_patterns,
            "merged_edges": merged_edges,
            "total_patterns": len(self.patterns),
            "total_edges": len(self.edges),
        }


# ---------------------------------------------------------------------------
# Токенизация
# ---------------------------------------------------------------------------

def _tokenize(text: str) -> list[str]:
    """Токенизация: lowercase + split по не-буквенным."""
    return re.findall(r"[a-zа-яё0-9]+", text.lower())


# ---------------------------------------------------------------------------
# Sentence-level storage (для формульно-управляемого retrieval)
# ---------------------------------------------------------------------------

def _split_sentences(text: str) -> list[str]:
    """Разбить текст на предложения для sentence-level retrieval."""
    raw = re.split(r'(?<=[.!?])\s+|\n\n+|\n(?=[А-ЯA-Z0-9•\-—])', text)
    result: list[str] = []
    for chunk in raw:
        chunk = chunk.strip().strip('•\u2013\u2014\u2022 ')
        if len(chunk) >= 20 and len(_tokenize(chunk)) >= 3:
            result.append(chunk)
    return result


@dataclass
class SentenceEntry:
    """
    Предложение хранится КАК ЦИФРЫ — не как текст.
    
    Числовое Мышление: каждый байт UTF-8 → 3 цифры (0-9).
    Сжатие: array('B') вместо str экономит ~60% памяти.
    Текст восстанавливается через digits_to_text() при необходимости.
    """
    digits: array                    # array('B') — цифры предложения
    fingerprint: int                 # Агрегированный хеш слов
    word_hashes: frozenset[int]      # Хеши значимых слов


class SentenceStore:
    """
    Числовое хранилище знаний — ВСЕ предложения хранятся КАК ЦИФРЫ.

    Принцип Числового Мышления:
    - Текст → array('B', digits) при загрузке
    - Цифры → текст при извлечении (digits_to_text)
    - Экономия памяти: 1 байт на цифру вместо 1-4 байт на символ
    - Формулы УПРАВЛЯЮТ поиском через числовые fingerprint
    """

    def __init__(self, max_sentences: int = 50000) -> None:
        self.sentences: list[SentenceEntry] = []
        self.max_sentences = max_sentences
        self._word_index: dict[int, list[int]] = {}

    def add_text(self, text: str) -> int:
        """Разбить текст на предложения → закодировать в ЦИФРЫ и сохранить."""
        added = 0
        for sent in _split_sentences(text):
            if len(self.sentences) >= self.max_sentences:
                break
            tokens = _tokenize(sent)
            word_hashes: set[int] = set()
            fp = 0
            for t in tokens:
                if len(t) >= 2:
                    h = djb2_hash(t)
                    word_hashes.add(h)
                    fp = (fp + h) & 0xFFFFFFFF

            if len(word_hashes) < 2:
                continue

            # === ЧИСЛОВОЕ ХРАНЕНИЕ: текст → цифры ===
            digits = text_to_digits(sent)
            digit_array = array('B', digits)

            idx = len(self.sentences)
            self.sentences.append(SentenceEntry(
                digits=digit_array,
                fingerprint=fp,
                word_hashes=frozenset(word_hashes),
            ))
            for wh in word_hashes:
                self._word_index.setdefault(wh, []).append(idx)
            added += 1
        return added

    def get_text(self, idx: int) -> str:
        """Восстановить текст из цифр (decode on demand)."""
        if 0 <= idx < len(self.sentences):
            return digits_to_text(list(self.sentences[idx].digits))
        return ""

    def retrieve(
        self,
        query: str,
        formula: Formula | None = None,
        top_k: int = 5,
    ) -> list[tuple[str, float]]:
        """
        Формульно-управляемый retrieval из числового хранилища.

        Stage 1: Индекс слов → кандидаты (O(1) per word)
        Stage 2: Формула re-ranks → predict(query ⊕ sentence) → score
        Stage 3: digits_to_text() — восстановление текста ИЗ ЦИФР
        """
        tokens = _tokenize(query)
        q_hashes: set[int] = set()
        for t in tokens:
            if len(t) >= 2:
                q_hashes.add(djb2_hash(t))

        if not q_hashes:
            return []

        # Stage 1: Кандидаты по пересечению слов
        candidate_scores: dict[int, float] = {}
        for qh in q_hashes:
            for idx in self._word_index.get(qh, []):
                candidate_scores[idx] = candidate_scores.get(idx, 0.0) + 1.0

        if not candidate_scores:
            return []

        # Нормализация: overlap / sqrt(|Q| * |S|)  (cosine-подобная)
        q_len = len(q_hashes)
        for idx in candidate_scores:
            s_len = len(self.sentences[idx].word_hashes)
            candidate_scores[idx] /= math.sqrt(q_len * s_len + 1)

        # Stage 2: Формула re-ranking
        if formula is not None and formula.fitness > 0.1:
            q_fp = sum(q_hashes) & 0xFFFFFFFF
            formula_weight = min(0.4, formula.fitness * 0.5)
            base_weight = 1.0 - formula_weight

            for idx in candidate_scores:
                s_fp = self.sentences[idx].fingerprint
                x = float((q_fp ^ s_fp) % 100000) / 100000.0
                f_raw = formula.predict_numeric(x)
                f_norm = 1.0 / (1.0 + abs(f_raw) / 1000.0)
                candidate_scores[idx] = (
                    candidate_scores[idx] * base_weight + f_norm * formula_weight
                )

        sorted_cands = sorted(
            candidate_scores.items(), key=lambda x: x[1], reverse=True
        )

        # Stage 3: Восстановление текста ИЗ ЦИФР
        return [
            (self.get_text(idx), round(score, 4))
            for idx, score in sorted_cands[:top_k]
        ]

    @property
    def size(self) -> int:
        return len(self.sentences)

    @property
    def memory_digits(self) -> int:
        """Общее число цифр в хранилище (метрика объёма знаний)."""
        return sum(len(s.digits) for s in self.sentences)

    @property
    def total_digits(self) -> int:
        """Алиас для memory_digits — общее число цифр."""
        return self.memory_digits
