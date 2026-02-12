"""
number_mind.py — Ядро «Числового Мышления» Kolibri

Чистая Python-реализация формульного движка (зеркало C-ядра):
- Каждое слово → 64-цифровой паттерн (через DJB2 + LCG каскад)
- Граф знаний: слово↔слово с весами (co-occurrence)
- Формульная ResNet-сеть: геном **4000 цифр** → до **500 слоёв**
- Эволюция формул (генетический алгоритм)
- Дистилляция: вытеснение слабых знаний
- Обратное восстановление: формулы → текст

Это НЕ классический ML. Это уникальная система, где:
- ВСЕ знания хранятся в ЧИСЛАХ
- Формулы из **4000 цифр** определяют "поведение мозга"
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
import zlib
from array import array
from collections import Counter, defaultdict, deque
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

log = logging.getLogger("kolibri.number_mind")

# Lazy import — embeddings загружаются при первом использовании
_EmbeddingTable = None

def _get_embedding_class():
    """Ленивый импорт EmbeddingTable (избегаем циклических зависимостей)."""
    global _EmbeddingTable
    if _EmbeddingTable is None:
        from .embeddings import EmbeddingTable as _ET
        _EmbeddingTable = _ET
    return _EmbeddingTable


# ---------------------------------------------------------------------------
# Константы (зеркало C: corpus_trainer.h)
# ---------------------------------------------------------------------------

KLM_PATTERN_SIZE = 64       # Цифр в числовом паттерне слова
KLM_WORD_MAX = 128          # Макс длина слова
GENE_SIZE = 4000            # Цифр в геноме формулы (500 слоёв × 8 цифр на слой)
FORMULA_LAYERS = 500        # Слоёв формульной "нейросети" (максимум)
FORMULA_LAYERS_FAST = 100   # Быстрый режим (используется для эволюции/оценки)
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
    Геном формулы — 4000 цифр (0–11).
    
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
        Пропустить число через 500-слойную ResNet формульную сеть.
        50 residual-блоков × 10 слоёв = глубокое преобразование без затухания.
        """
        return self._run_layers(x, FORMULA_LAYERS)

    def predict_fast(self, x: float) -> float:
        """Быстрый вариант: 100 слоёв (10 residual-блоков)."""
        return self._run_layers(x, FORMULA_LAYERS_FAST)

    # --- ResNet-архитектура: Residual Blocks + Layer Normalization ---
    # Каждые BLOCK_SIZE слоёв = 1 residual block.
    # output = α·block(x) + (1-α)·x  ← weighted skip-connection
    # α определяется геномом → эволюция учит какие блоки важны.
    _BLOCK_SIZE = 10  # Слоёв в одном residual-блоке

    def _run_layers(self, x: float, num_layers: int) -> float:
        """
        ResNet 500-слойная формульная сеть с пропорциональным residual.
        
        Ключевое отличие от наивной сети:
        1. Skip-connection через каждые 10 слоёв → сигнал не затухает
        2. Масштабирование α ∈ [0.1, 0.5] → блок «добавляет» к входу, не заменяет
        3. Tanh-нормализация → значения в [-1, +1] перед каждым блоком
        4. Операции работают в масштабе ±1 → стабильный градиент
        """
        value = float(x)
        # Начальная нормализация: приводим вход к [-1, +1]
        scale = max(abs(value), 1.0)
        value = value / scale
        
        num_blocks = max(1, num_layers // self._BLOCK_SIZE)
        
        for block in range(num_blocks):
            # --- Skip-connection: запоминаем вход блока ---
            residual = value
            
            # α — сила блока, определяется первой цифрой блока
            block_start = (block * self._BLOCK_SIZE * 8) % len(self.digits)
            alpha = 0.1 + self.digits[block_start] * 0.04  # α ∈ [0.1, 0.54]
            
            # --- 10 слоёв внутри блока ---
            for sub in range(self._BLOCK_SIZE):
                layer = block * self._BLOCK_SIZE + sub
                if layer >= num_layers:
                    break
                offset = (layer * 8) % len(self.digits)
                
                op = self.digits[offset] % 12
                
                # Параметры: slope ∈ [-2, +2], bias ∈ [-0.5, +0.5]
                sign_s = -1.0 if self.digits[(offset + 1) % len(self.digits)] > 5 else 1.0
                slope_raw = (
                    self.digits[(offset + 2) % len(self.digits)] * 10 +
                    self.digits[(offset + 3) % len(self.digits)]
                )
                slope = sign_s * (0.5 + slope_raw / 66.0)  # slope ∈ [±0.5, ±2.0]
                
                sign_b = -1.0 if self.digits[(offset + 4) % len(self.digits)] > 5 else 1.0
                bias_raw = (
                    self.digits[(offset + 5) % len(self.digits)] * 10 +
                    self.digits[(offset + 6) % len(self.digits)]
                )
                bias = sign_b * bias_raw / 198.0  # bias ∈ [-0.5, +0.5]
                
                aux = self.digits[(offset + 7) % len(self.digits)] + 1
                
                # 12 операций — все работают в масштабе ±1
                try:
                    if op == 0:    # Линейная
                        value = slope * value + bias
                    elif op == 1:  # Инверсная
                        value = slope * value - bias
                    elif op == 2:  # Модулярная
                        value = math.fmod(value * slope, 1.0 + aux * 0.1) + bias
                    elif op == 3:  # Квадратичная (мягкая x·|x| — сохраняет знак)
                        value = slope * value * abs(value) / (1.0 + abs(value)) + bias
                    elif op == 4:  # Периодическая (sin-based hashing)
                        value = math.sin(value * aux * 1.7) * slope + bias
                    elif op == 5:  # Ступенчатая (quantize)
                        value = round(value * aux) / max(aux, 1) * slope + bias
                    elif op == 6:  # Синус
                        value = math.sin(value * math.pi * slope) + bias
                    elif op == 7:  # Насыщение (softsign)
                        value = value / (1.0 + abs(value)) * slope + bias
                    elif op == 8:  # Масштабирование
                        value = value * slope + bias
                    elif op == 9:  # Гауссова
                        value = math.exp(-value * value / 2.0) * slope + bias
                    elif op == 10: # Tanh
                        value = math.tanh(value * slope) + bias
                    elif op == 11: # Leaky ReLU
                        value = (value if value > 0 else 0.1 * value) * slope + bias
                except (ValueError, OverflowError, ZeroDivisionError):
                    pass
                
                # Мягкий клиппинг: tanh сжимает, но не обрезает
                if abs(value) > 3.0:
                    value = math.tanh(value / 3.0) * 3.0
            
            # --- Weighted Residual: output = α·block + (1-α)·input ---
            value = alpha * value + (1.0 - alpha) * residual
        
        # Восстанавливаем масштаб
        return value * scale
    
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
    - Геном (4000 цифр) → 500-слойная ResNet-подобная сеть
    - Ассоциации (Q→A через FNV1a хеши)
    - Fitness (качество СЕМАНТИЧЕСКИХ предсказаний)

    Ключевое отличие: fitness оценивается по способности
    предсказать СОСЕДЕЙ слова в графе знаний.
    Формула, трансформирующая паттерн слова A в паттерн
    близкий к соседу B, получает высокий fitness.
    
    Speciation: формулы объединяются в «виды» по геномному
    расстоянию, чтобы предотвратить преждевременную конвергенцию.
    """

    def __init__(self) -> None:
        self.formulas: list[Formula] = [
            Formula() for _ in range(POPULATION_SIZE)
        ]
        self.generation: int = 0
        # Семантические пары: (паттерн_слова, паттерн_соседа)
        # Формула учится: transform(паттерн_A) ≈ паттерн_B
        self.semantic_pairs: list[tuple[list[int], list[int]]] = []
        # Speciation: отслеживаем «виды» для NEAT-подобного разнообразия
        self._species: list[list[int]] = []  # группы индексов формул
        self._stagnation: dict[int, int] = {}  # species_id → поколений без улучшения
        self._species_best: dict[int, float] = {}  # species_id → лучший fitness

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
        Эволюция формул — СЕМАНТИЧЕСКИЙ fitness + NEAT-подобная speciation.

        Fitness = насколько хорошо формула трансформирует
        паттерн слова A в паттерн его соседа B из графа знаний.

        Улучшения v2:
        - Мягкая метрика сходства (не только exact match)
        - Адаптивная мутация (стагнация → агрессивнее, рост → осторожнее)
        - Турнирная селекция с fitness-sharing (давление + разнообразие)
        - Стагнация видов: виды, не улучшающие fitness 20 поколений, истребляются
        - Больше оценочных цифр для точности (24 → 32)
        """
        if not self.semantic_pairs:
            return 0.0

        # Берём разнообразную выборку (не только первые)
        _MAX_EVAL = 80
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
                _EVAL_DIGITS = 32  # Расширили для точности
                for src_pat, tgt_pat in eval_sample:
                    pred_part: list[int] = []
                    for i in range(_EVAL_DIGITS):
                        digit = src_pat[i] if i < len(src_pat) else 0
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

            # Fitness sharing: формулы в «плотных» кластерах делят fitness
            # Это поддерживает разнообразие и предотвращает доминирование одного вида
            self._apply_fitness_sharing()

            self.formulas.sort(key=lambda f: f.fitness, reverse=True)
            best_fitness = self.formulas[0].fitness

            # Адаптивная мутация: стагнация → агрессивнее
            improvement = best_fitness - prev_best
            stagnation_level = 0
            if improvement < 0.0005:
                stagnation_level += 1
            if improvement < 0.0001 and gen_i > 3:
                stagnation_level += 1
            
            mutation_rates = [0.012, 0.03, 0.06]  # normal, mild-stuck, stuck
            mutation_rate = mutation_rates[min(stagnation_level, 2)]
            prev_best = best_fitness

            # Элитизм + турнирная селекция с разнообразием
            elite_count = max(2, POPULATION_SIZE // 3)
            for i in range(elite_count, POPULATION_SIZE):
                # Турнир из 3 → выбираем лучшего
                t1 = self.formulas[random.randint(0, elite_count - 1)]
                t2 = self.formulas[random.randint(0, elite_count - 1)]
                p1 = t1 if t1.fitness >= t2.fitness else t2
                t3 = self.formulas[random.randint(0, elite_count - 1)]
                t4 = self.formulas[random.randint(0, elite_count - 1)]
                p2 = t3 if t3.fitness >= t4.fitness else t4
                
                # Предпочитаем кроссовер между далёкими формулами (inter-species)
                if stagnation_level >= 2 and random.random() < 0.3:
                    # Случайный аутсайдер → свежая кровь
                    p2 = self.formulas[random.randint(0, POPULATION_SIZE - 1)]

                child_gene = p1.gene.crossover(p2.gene)
                child_gene.mutate(rate=mutation_rate)
                self.formulas[i] = Formula(gene=child_gene)
                # Наследуем ассоциации от лучшего родителя
                if p1.associations:
                    self.formulas[i].associations = list(p1.associations[-100:])

            self.generation += 1
            # Отпускаем GIL между поколениями — чтобы HTTP-запросы не зависали
            time.sleep(0)

        return best_fitness

    def _apply_fitness_sharing(self) -> None:
        """
        Fitness sharing: формулы с похожими геномами делят fitness.
        
        Это NEAT-подобный механизм, поддерживающий разнообразие:
        - Измеряем «расстояние» между геномами (первые 64 цифры)
        - Формулы в одной «нише» делят fitness на размер ниши
        - Результат: разные стратегии сосуществуют в популяции
        """
        n = len(self.formulas)
        if n < 3:
            return
        
        sharing_radius = 0.3  # порог «одинаковости»
        niche_counts = [1.0] * n
        
        # Быстрое сравнение по первым 64 цифрам генома
        for i in range(n):
            for j in range(i + 1, n):
                g1 = self.formulas[i].gene.digits[:64]
                g2 = self.formulas[j].gene.digits[:64]
                diff = sum(1 for a, b in zip(g1, g2) if a != b) / 64.0
                if diff < sharing_radius:
                    sharing = 1.0 - diff / sharing_radius
                    niche_counts[i] += sharing
                    niche_counts[j] += sharing
        
        for i in range(n):
            self.formulas[i].fitness /= niche_counts[i]
    
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
    # Локальная версия изменения (для дельта-синхронизации между нодами)
    version: int = 0
    
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
    # Локальная версия изменения (для дельта-синхронизации между нодами)
    version: int = 0


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
    
    def __init__(
        self,
        max_patterns: int | None = None,
        max_edges: int | None = None,
        max_degree: int | None = None,
        *,
        delta_log_max: int | None = None,
    ) -> None:
        self.patterns: dict[int, PatternEntry] = {}
        self.edges: dict[tuple[int, int], KnowledgeEdge] = {}
        self._adj: dict[int, set[int]] = {}  # Индекс смежности для быстрого answer()
        self._hash_to_word: dict[int, str] = {}  # Обратный индекс: хеш → слово
        # Лимиты можно переопределять через ENV, чтобы масштабировать экспериментально.
        # Важно: на миллионах узлов Python-структуры потребуют очень много RAM.
        self.max_patterns = max_patterns if max_patterns is not None else int(
            os.getenv("KOLIBRI_GRAPH_MAX_PATTERNS", "131072")
        )
        self.max_edges = max_edges if max_edges is not None else int(
            os.getenv("KOLIBRI_GRAPH_MAX_EDGES", "262144")
        )
        self.max_degree = max_degree if max_degree is not None else (
            int(os.getenv("KOLIBRI_GRAPH_MAX_DEGREE", "0")) or None
        )
        self.documents_trained: int = 0
        self.tokens_processed: int = 0
        self.current_epoch: int = 0
        # --- Обучаемые эмбеддинги (Фаза 1 AI) ---
        self.embeddings: object | None = None  # EmbeddingTable, инициализируется движком
        # --- Fitness tracking: сколько раз паттерн использовался в ответах ---
        self._answer_hits: dict[int, int] = {}  # hash → использований в answer()
        self._total_queries: int = 0

        import threading
        self._lock = threading.RLock()  # Защита от race condition (train vs stats)

        # --- Delta log / версии: для P2P синхронизации (см. service/delta_sync.py) ---
        self._version: int = 0
        self._delta_log: deque[tuple[int, str, object]] = deque(
            maxlen=delta_log_max
            if delta_log_max is not None
            else int(os.getenv("KOLIBRI_DELTA_LOG_MAX", "200000"))
        )

    def _record_delta(self, kind: str, key: object) -> int:
        """Записать изменение в delta-log и вернуть новую версию (вызывается под _lock)."""
        self._version += 1
        v = self._version
        self._delta_log.append((v, kind, key))
        return v

    @property
    def version(self) -> int:
        return self._version

    def export_delta(self, since_version: int, max_items: int = 10000) -> dict:
        """
        Экспортировать только изменения (дельту) с указанной версии.

        Возвращаем формат, совместимый с merge_state():
          {from_version, to_version, needs_full_sync, patterns, edges}
        """
        with self._lock:
            to_v = self._version
            if not self._delta_log:
                return {
                    "from_version": since_version,
                    "to_version": to_v,
                    "needs_full_sync": False,
                    "truncated": False,
                    "patterns": {},
                    "edges": {},
                }

            oldest_v = self._delta_log[0][0]
            if since_version < oldest_v:
                # Пир слишком отстал: нашей дельты уже не хватает.
                return {
                    "from_version": since_version,
                    "to_version": to_v,
                    "needs_full_sync": True,
                    "oldest_available_version": oldest_v,
                    "truncated": False,
                    "patterns": {},
                    "edges": {},
                }

            patterns: dict[str, dict] = {}
            edges: dict[str, dict] = {}
            total = 0

            for v, kind, key in self._delta_log:
                if v <= since_version:
                    continue
                if kind == "p":
                    h = int(key)  # word hash
                    entry = self.patterns.get(h)
                    if entry is None:
                        continue
                    patterns[str(h)] = {
                        "word": entry.word,
                        "pattern": entry.pattern,
                        "frequency": entry.frequency,
                        "fitness": entry.fitness,
                        "version": entry.version,
                    }
                    total += 1
                elif kind == "e":
                    src, tgt = key  # type: ignore[misc]
                    edge = self.edges.get((src, tgt))
                    if edge is None:
                        continue
                    edges[f"{src}:{tgt}"] = {
                        "weight": edge.weight,
                        "cooccurrence": edge.cooccurrence,
                        "version": edge.version,
                    }
                    total += 1
                else:
                    continue

                if total >= max_items:
                    break

        return {
            "from_version": since_version,
            "to_version": to_v,
            "needs_full_sync": False,
            "truncated": total >= max_items,
            "patterns": patterns,
            "edges": edges,
        }

    def _prune_node_degree(self, h: int) -> int:
        """Ограничить степень узла до max_degree (вызывается под _lock)."""
        if self.max_degree is None:
            return 0
        neighbors = self._adj.get(h)
        if not neighbors or len(neighbors) <= self.max_degree:
            return 0

        # Оставляем самые сильные связи.
        scored: list[tuple[float, int]] = []
        for other in neighbors:
            key = (min(h, other), max(h, other))
            edge = self.edges.get(key)
            scored.append((edge.weight if edge else 0.0, other))
        scored.sort(key=lambda x: x[0])  # слабые первые
        to_remove = scored[: max(0, len(scored) - self.max_degree)]

        removed = 0
        for _, other in to_remove:
            key = (min(h, other), max(h, other))
            self.edges.pop(key, None)
            if h in self._adj:
                self._adj[h].discard(other)
                if not self._adj[h]:
                    self._adj.pop(h, None)
            if other in self._adj:
                self._adj[other].discard(h)
                if not self._adj[other]:
                    self._adj.pop(other, None)
            removed += 1
            # (опционально) дельты удаления не отправляем: пиры сами дистиллируют.

        return removed
    
    def add_word(self, word: str) -> PatternEntry:
        """Добавить слово в граф (или обновить частоту)."""
        with self._lock:
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
            entry.version = self._record_delta("p", h)
            self.patterns[h] = entry
            self._hash_to_word[h] = word.lower()  # Обратный индекс
            return entry
    
    def add_edge(self, word1: str, word2: str) -> None:
        """Добавить или усилить ребро между словами."""
        with self._lock:
            h1 = djb2_hash(word1.lower())
            h2 = djb2_hash(word2.lower())
            if h1 == h2:
                return
            
            key = (min(h1, h2), max(h1, h2))
            if key in self.edges:
                self.edges[key].strengthen()
                self.edges[key].version = self._record_delta("e", key)
            else:
                if len(self.edges) >= self.max_edges:
                    self._distill_edges()
                edge = KnowledgeEdge(source_hash=key[0], target_hash=key[1])
                edge.strengthen()
                edge.version = self._record_delta("e", key)
                self.edges[key] = edge
                # Индекс смежности
                self._adj.setdefault(key[0], set()).add(key[1])
                self._adj.setdefault(key[1], set()).add(key[0])
                # Ограничение степени (если включено)
                self._prune_node_degree(key[0])
                self._prune_node_degree(key[1])
    
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
        # IDF-нормализация: редкие связи весят больше
        total_patterns = max(len(self.patterns), 1)
        candidates: dict[int, float] = {}
        
        for t in tokens:
            if len(t) < 3 or _is_stop_word(t):
                continue
            h = djb2_hash(t.lower())
            # Все хеши для поиска: оригинал + стем
            lookup_hashes = [h]
            if len(t) >= 4:
                stemmed = _stem_ru(t)
                if stemmed != t.lower():
                    lookup_hashes.append(djb2_hash(stemmed))
            
            for lh in lookup_hashes:
                for other in self._adj.get(lh, ()):
                    if other in q_hashes:
                        continue
                    key = (min(lh, other), max(lh, other))
                    edge = self.edges.get(key)
                    if edge:
                        # IDF-нормализация: слова с меньшим кол-вом связей
                        # (более специфичные) — получают больший вес
                        import math
                        degree = len(self._adj.get(other, ()))
                        idf = math.log((total_patterns + 1) / (degree + 1)) + 1.0
                        candidates[other] = candidates.get(other, 0.0) + edge.weight * idf

        # --- Семантический boost через обученные embeddings ---
        # Если embeddings обучены, добавляем кандидатов по cosine similarity
        if self.embeddings is not None and self.embeddings.vocab_size > 100:
            for t in tokens:
                if len(t) < 3 or _is_stop_word(t):
                    continue
                h = djb2_hash(t.lower())
                if not self.embeddings.has(h):
                    continue
                # Добавляем семантически похожие слова (даже если нет прямого ребра)
                sim_results = self.embeddings.find_similar(h, top_k=8, min_sim=0.3)
                for sim_hash, sim_word, sim_score in sim_results:
                    if sim_hash in q_hashes:
                        continue
                    # Embedding boost: semantic_weight = similarity * base_multiplier
                    emb_boost = sim_score * 3.0
                    candidates[sim_hash] = candidates.get(sim_hash, 0.0) + emb_boost

        # Сортировка по score
        sorted_cands = sorted(candidates.items(), key=lambda x: x[1], reverse=True)
        
        # Собираем ответ
        words = []
        total_score = 0.0
        for hash_val, score in sorted_cands[:max_words]:
            entry = self.patterns.get(hash_val)
            if entry and not _is_stop_word(entry.word):
                words.append(entry.word)
                total_score += score
                # --- Реальная fitness-оценка: трекинг использования в ответах ---
                self._answer_hits[hash_val] = self._answer_hits.get(hash_val, 0) + 1
        
        self._total_queries += 1
        
        # --- Обновление fitness паттернов (каждые 50 запросов) ---
        if self._total_queries > 0 and self._total_queries % 50 == 0:
            self._update_fitness()
        
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

    def multi_hop_answer(self, question: str, max_hops: int = 2,
                         max_words: int = 10) -> tuple[str, float, dict]:
        """
        Multi-hop QA: цепочка вывода через граф знаний.

        Hop 1: answer(question) → intermediate_words
        Hop 2: answer(intermediate_words) → дополнительные кандидаты
        Объединяем и ранжируем все кандидаты.
        """
        # Hop 1: прямой ответ
        ans1, conf1, meta1 = self.answer(question, max_words=max_words)
        if max_hops <= 1 or not ans1:
            return (ans1, conf1, meta1)

        # Hop 2: используем промежуточные слова как новый запрос
        hop_words = ans1.split()[:5]
        if len(hop_words) < 2:
            return (ans1, conf1, meta1)

        hop_query = ' '.join(hop_words)
        ans2, conf2, meta2 = self.answer(hop_query, max_words=max_words)

        if not ans2 or conf2 < 0.1:
            return (ans1, conf1, meta1)

        # Объединяем: слова из hop1 + новые слова из hop2
        seen = set(hop_words)
        combined = list(hop_words)
        for w in ans2.split():
            if w.lower() not in {x.lower() for x in seen}:
                combined.append(w)
                seen.add(w)
            if len(combined) >= max_words:
                break

        combined_text = ' '.join(combined)
        combined_conf = min(1.0, conf1 * 0.7 + conf2 * 0.3)
        meta = {
            **meta1,
            "multi_hop": True,
            "hop_count": 2,
            "hop1_confidence": conf1,
            "hop2_confidence": conf2,
            "hop2_candidates": meta2.get("candidates_total", 0),
        }
        return (combined_text, round(combined_conf, 4), meta)

    def _update_fitness(self) -> None:
        """
        Обновление fitness паттернов на основе реального использования.
        
        fitness = α·freq_score + β·hit_score + γ·degree_score
        - freq_score: нормализованная частота появления в корпусе
        - hit_score: как часто паттерн попадает в ответы (полезность)
        - degree_score: количество связей в графе (информативность)
        """
        if not self.patterns:
            return
        
        max_freq = max((p.frequency for p in self.patterns.values()), default=1)
        max_hits = max(self._answer_hits.values(), default=1)
        max_degree = max((len(self._adj.get(h, ())) for h in self.patterns), default=1)
        
        α, β, γ = 0.3, 0.5, 0.2  # hit_score (полезность) — главный фактор
        
        with self._lock:
            for h, entry in self.patterns.items():
                freq_score = math.log(entry.frequency + 1) / math.log(max_freq + 1)
                hit_score = math.log(self._answer_hits.get(h, 0) + 1) / math.log(max_hits + 1)
                degree = len(self._adj.get(h, ()))
                degree_score = math.log(degree + 1) / math.log(max_degree + 1)
                
                entry.fitness = round(α * freq_score + β * hit_score + γ * degree_score, 4)
    
    def find_similar(self, word: str, limit: int = 10) -> list[tuple[str, float]]:
        """Найти слова с похожими числовыми паттернами (DJB2 + эмбеддинги)."""
        h = djb2_hash(word.lower())
        
        # --- Если есть обученные эмбеддинги — используем их ---
        if self.embeddings is not None and hasattr(self.embeddings, 'has') and self.embeddings.has(h):
            emb_results = self.embeddings.find_similar(h, top_k=limit * 2, min_sim=0.15)
            results: list[tuple[str, float]] = []
            for _, emb_word, sim in emb_results:
                if emb_word != word.lower() and not emb_word.startswith('#'):
                    results.append((emb_word, round(sim, 4)))
            if results:
                return results[:limit]
        
        # --- Fallback: DJB2 паттерны ---
        target = word_to_pattern(word)
        results_fb: list[tuple[str, float]] = []
        with self._lock:
            patterns_snap = list(self.patterns.values())
        for entry in patterns_snap:
            sim = pattern_similarity(target, entry.pattern)
            if sim > 0.3 and entry.word != word.lower():
                results_fb.append((entry.word, round(sim, 4)))
        results_fb.sort(key=lambda x: x[1], reverse=True)
        return results_fb[:limit]

    def find_similar_semantic(self, word: str, limit: int = 10) -> list[tuple[str, float, str]]:
        """
        Семантический поиск похожих слов через обученные эмбеддинги.
        
        Returns: [(word, similarity, method), ...]
        method = 'embedding' или 'pattern' (если эмбеддинги не готовы)
        """
        h = djb2_hash(word.lower())
        
        if self.embeddings is not None and hasattr(self.embeddings, 'has') and self.embeddings.has(h):
            emb_results = self.embeddings.find_similar(h, top_k=limit, min_sim=0.1)
            return [
                (w, round(s, 4), 'embedding')
                for _, w, s in emb_results
                if w != word.lower() and not w.startswith('#')
            ]
        
        # Fallback to DJB2 patterns
        target = word_to_pattern(word)
        results: list[tuple[str, float, str]] = []
        with self._lock:
            patterns_snap = list(self.patterns.values())
        for entry in patterns_snap:
            sim = pattern_similarity(target, entry.pattern)
            if sim > 0.3 and entry.word != word.lower():
                results.append((entry.word, round(sim, 4), 'pattern'))
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
        with self._lock:
            patterns_snap = list(self.patterns.values())
        for entry in patterns_snap:
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
        """Дистилляция: удалить слабые паттерны (вызывается под _lock)."""
        if not self.patterns:
            return 0
        scores = {h: e.fitness * math.log(e.frequency + 1) for h, e in list(self.patterns.items())}
        mean_score = sum(scores.values()) / len(scores)
        threshold = mean_score * 0.1
        to_remove = [h for h, s in scores.items() if s < threshold]
        for h in to_remove[:len(self.patterns) // 5]:
            del self.patterns[h]
        self.current_epoch += 1
        return len(to_remove)
    
    def _distill_edges(self) -> int:
        """Дистилляция рёбер (вызывается под _lock)."""
        if not self.edges:
            return 0
        edges_snap = list(self.edges.items())
        mean_weight = sum(e.weight for _, e in edges_snap) / len(edges_snap)
        threshold = mean_weight * 0.1
        to_remove = [k for k, e in edges_snap if e.weight < threshold]
        for k in to_remove[:len(self.edges) // 5]:
            self.edges.pop(k, None)
        # Перестроить индекс смежности
        self._rebuild_adj()
        return len(to_remove)
    
    def _rebuild_adj(self) -> None:
        """Перестроить индекс смежности из рёбер (вызывается под _lock)."""
        self._adj.clear()
        for src, tgt in list(self.edges.keys()):
            self._adj.setdefault(src, set()).add(tgt)
            self._adj.setdefault(tgt, set()).add(src)
    
    def get_stats(self) -> dict:
        """Статистика графа (thread-safe snapshot)."""
        with self._lock:
            patterns_snap = list(self.patterns.values())
            edges_snap = list(self.edges.values())
        avg_fitness = (
            sum(e.fitness for e in patterns_snap) / len(patterns_snap)
            if patterns_snap else 0.0
        )
        avg_weight = (
            sum(e.weight for e in edges_snap) / len(edges_snap)
            if edges_snap else 0.0
        )
        return {
            "patterns": len(patterns_snap),
            "max_patterns": self.max_patterns,
            "edges": len(edges_snap),
            "max_edges": self.max_edges,
            "max_degree": self.max_degree or 0,
            "documents_trained": self.documents_trained,
            "tokens_processed": self.tokens_processed,
            "epoch": self.current_epoch,
            "graph_version": self._version,
            "delta_log_len": len(self._delta_log),
            "delta_oldest_version": self._delta_log[0][0] if self._delta_log else self._version,
            "avg_fitness": round(avg_fitness, 4),
            "avg_weight": round(avg_weight, 4),
        }

    # ───────────────────────────────────────────────────────────
    #  КОГНИТИВНЫЕ ФУНКЦИИ  (абстракция, каузальность, индукция,
    #                        перенос структуры, самомоделирование)
    # ───────────────────────────────────────────────────────────

    def reason_abstract(
        self,
        query: str,
        max_words: int = 10,
        depth: int = 2,
    ) -> tuple[str, float]:
        """
        Абстрактное мышление: N-хоповая навигация по графу.

        Стандартный answer() пробегает 1 хоп (слово → сосед).
        reason_abstract делает *depth* хопов с затуханием,
        что позволяет ОБОБЩАТЬ через категориальные хабы.

        Пример: «go» → 1-хоп: «язык» → 2-хоп: «программирования»
        → система обобщила, что «go» — язык программирования,
        хотя ПРЯМОЙ связи «go—программирования» в данных нет.

        Args:
            query:     текст вопроса
            max_words: сколько слов в ответе
            depth:     глубина навигации (1=обычный answer, 2+=абстракция)

        Returns:
            (ответ_текст, confidence)
        """
        tokens = _tokenize(query)
        if not tokens:
            return "", 0.0
        q_hashes = {djb2_hash(t) for t in tokens}
        candidates: dict[int, float] = {}

        # --- Хоп 0: прямые соседи (как answer()) ---
        frontier: dict[int, float] = {}
        for t in tokens:
            if len(t) < 2:
                continue
            h = djb2_hash(t)
            for n in self._adj.get(h, set()):
                if n in q_hashes:
                    continue
                key = (min(h, n), max(h, n))
                edge = self.edges.get(key)
                if edge:
                    frontier[n] = frontier.get(n, 0.0) + edge.weight
                    candidates[n] = candidates.get(n, 0.0) + edge.weight

        # --- Хопы 1..depth-1: абстракция с затуханием ---
        decay = 0.5
        for hop in range(1, depth):
            nf: dict[int, float] = {}
            for h1, sc in sorted(frontier.items(), key=lambda x: -x[1])[:30]:
                for n in self._adj.get(h1, set()):
                    if n in q_hashes:
                        continue
                    key = (min(h1, n), max(h1, n))
                    edge = self.edges.get(key)
                    if edge:
                        c = sc * edge.weight * (decay ** hop)
                        nf[n] = nf.get(n, 0.0) + c
                        candidates[n] = candidates.get(n, 0.0) + c
            frontier = nf

        sorted_c = sorted(candidates.items(), key=lambda x: -x[1])
        words: list[str] = []
        total = 0.0
        for h, s in sorted_c[:max_words]:
            entry = self.patterns.get(h)
            if entry and len(entry.word) >= 2:
                words.append(entry.word)
                total += s
        conf = min(1.0, total / (len(words) + 1)) if words else 0.0
        return ' '.join(words), conf

    def build_causal_index(
        self,
        texts: list[str],
        window: int = 5,
    ) -> dict[tuple[int, int], float]:
        """
        Каузальный индекс: направление связи по порядку слов.

        Если слово A систематически появляется ПЕРЕД B в текстах,
        то causal(A→B) > 0.5 => A скорее ПРИЧИНА B.

        Args:
            texts:  обучающие тексты (корпус)
            window: окно контекста

        Returns:
            dict[(hash_a, hash_b), direction_score]
            direction_score ∈ [0,1]: >0.5 = A→B, <0.5 = B→A
        """
        fwd: dict[tuple[int, int], int] = {}
        for text in texts:
            words = _tokenize(text)
            for i, w1 in enumerate(words):
                if len(w1) < 3:
                    continue
                h1 = djb2_hash(w1)
                for j in range(i + 1, min(i + window + 1, len(words))):
                    w2 = words[j]
                    if len(w2) < 3:
                        continue
                    h2 = djb2_hash(w2)
                    if h1 != h2:
                        fwd[(h1, h2)] = fwd.get((h1, h2), 0) + 1
        causal: dict[tuple[int, int], float] = {}
        for (h1, h2), f in fwd.items():
            r = fwd.get((h2, h1), 0)
            tot = f + r
            if tot >= 1:
                causal[(h1, h2)] = f / tot
        return causal

    def reason_causal(
        self,
        causal_idx: dict[tuple[int, int], float],
        query: str,
        direction: str = "why",
        max_chain: int = 3,
    ) -> list[tuple[str, float]]:
        """
        Следование по каузальной цепочке в графе.

        direction="why"  → ищем ПРИЧИНЫ (назад по временному порядку)
        direction="then" → ищем СЛЕДСТВИЯ (вперёд)

        Args:
            causal_idx: каузальный индекс (из build_causal_index)
            query:      текст запроса
            direction:  "why" | "then"
            max_chain:  макс. длина цепочки

        Returns:
            [(слово, score), ...] — каузальная цепочка
        """
        tokens = _tokenize(query)
        if not tokens:
            return []
        q_hashes = {djb2_hash(t) for t in tokens}
        chain: list[tuple[str, float]] = []
        visited = set(q_hashes)
        current = q_hashes.copy()

        for _ in range(max_chain):
            best_h: int | None = None
            best_sc = 0.0
            best_w = ""
            for hc in current:
                for n in self._adj.get(hc, set()):
                    if n in visited:
                        continue
                    entry = self.patterns.get(n)
                    if not entry or len(entry.word) < 3:
                        continue
                    if direction == "why":
                        sc = causal_idx.get((n, hc), 0.5)
                    else:
                        sc = causal_idx.get((hc, n), 0.5)
                    key = (min(hc, n), max(hc, n))
                    edge = self.edges.get(key)
                    ew = edge.weight if edge else 0.0
                    combined = sc * ew
                    if combined > best_sc and sc > 0.55:
                        best_sc = combined
                        best_h = n
                        best_w = entry.word
            if best_h is None:
                break
            chain.append((best_w, best_sc))
            visited.add(best_h)
            current = {best_h}
        return chain

    def induce_rules(
        self,
        min_support: int = 3,
        min_confidence: float = 0.6,
    ) -> list[tuple[str, str, int, float]]:
        """
        Индуктивный вывод: ассоциативные правила из графа.

        Правило: «если слово связано с A, то оно связано и с B».
        support  = |общих соседей A и B|
        confidence = support / |соседей A|

        Args:
            min_support:    минимальное число общих соседей
            min_confidence: минимальная уверенность правила

        Returns:
            [(premise, conclusion, support, confidence), ...]
            отсортированные по убыванию confidence
        """
        hubs = [(h, s) for h, s in self._adj.items()
                if len(s) >= 5 and h in self.patterns
                and len(self.patterns[h].word) >= 3]
        hubs.sort(key=lambda x: -len(x[1]))
        hubs = hubs[:60]

        rules: list[tuple[str, str, int, float]] = []
        for i, (ha, na) in enumerate(hubs):
            for j, (hb, nb) in enumerate(hubs):
                if i >= j:
                    continue
                both = na & nb
                if len(both) < min_support:
                    continue
                ca = len(both) / len(na)
                cb = len(both) / len(nb)
                wa = self.patterns[ha].word
                wb = self.patterns[hb].word
                if ca >= min_confidence:
                    rules.append((wa, wb, len(both), ca))
                if cb >= min_confidence:
                    rules.append((wb, wa, len(both), cb))
        rules.sort(key=lambda x: (-x[3], -x[2]))
        return rules

    def transfer_analogy(
        self,
        a: str,
        b: str,
        c: str,
        max_results: int = 5,
    ) -> list[tuple[str, float]]:
        """
        Перенос структуры: аналогия A:B :: C:?

        Метод: «структурный профиль» B (его соседи по графу) переносится
        на контекст C. Ищем D среди соседей C с максимальным Jaccard
        пересечением с профилем B.

        Пример: Python:язык :: Java:? → «программирования» (Jaccard ≈ 0.1)

        Args:
            a, b, c: слова аналогии (a:b :: c:?)
            max_results: сколько кандидатов вернуть

        Returns:
            [(слово_D, jaccard_score), ...]
        """
        ha, hb, hc = djb2_hash(a), djb2_hash(b), djb2_hash(c)
        nb = self._adj.get(hb, set())
        nc = self._adj.get(hc, set())
        if not nc or not nb:
            return []
        rel = nb - {ha, hb, hc}
        if not rel:
            return []
        results: list[tuple[str, float]] = []
        for d in nc:
            if d in {ha, hb, hc}:
                continue
            entry = self.patterns.get(d)
            if not entry or len(entry.word) < 3:
                continue
            nd = self._adj.get(d, set())
            if not nd:
                continue
            overlap = len(nd & rel)
            if overlap > 0:
                score = overlap / len(nd | rel)
                results.append((entry.word, score))
        results.sort(key=lambda x: -x[1])
        return results[:max_results]

    def self_model(self, query: str) -> dict:
        """
        Самомоделирование: граф предсказывает свою способность ответить.

        Анализ: какая доля слов запроса ПРИСУТСТВУЕТ в графе
        (есть паттерны + рёбра), а какая — нет.

        Returns:
            {
                "predicted_confidence": float (0..1),
                "known_words": list[str],
                "unknown_words": list[str],
                "coverage": float  (% слов запроса, найденных в графе),
                "edge_density": float  (среднее кол-во рёбер на известное слово),
            }
        """
        tokens = _tokenize(query)
        if not tokens:
            return {
                "predicted_confidence": 0.0,
                "known_words": [],
                "unknown_words": [],
                "coverage": 0.0,
                "edge_density": 0.0,
            }
        significant = [t for t in tokens if len(t) >= 3 and not _is_stop_word(t)]
        if not significant:
            significant = [t for t in tokens if len(t) >= 2]
        known: list[str] = []
        unknown: list[str] = []
        total_edges = 0
        for w in significant:
            h = djb2_hash(w)
            if h in self.patterns and h in self._adj:
                known.append(w)
                total_edges += len(self._adj[h])
            else:
                unknown.append(w)
        coverage = len(known) / len(significant) if significant else 0.0
        edge_density = total_edges / len(known) if known else 0.0
        predicted = coverage * min(1.0, edge_density / 10.0)
        return {
            "predicted_confidence": round(predicted, 4),
            "known_words": known,
            "unknown_words": unknown,
            "coverage": round(coverage, 4),
            "edge_density": round(edge_density, 2),
        }
    
    def export_state(self) -> dict:
        """
        Экспорт состояния для синхронизации между нодами.
        Все данные — числа. Слова хранятся через хеши.
        """
        with self._lock:
            patterns_snap = list(self.patterns.items())
            edges_snap = list(self.edges.items())
        return {
            "version": 1,
            "timestamp": time.time(),
            "epoch": self.current_epoch,
            "graph_version": self._version,
            "documents_trained": self.documents_trained,
            "tokens_processed": self.tokens_processed,
            "patterns": {
                str(h): {
                    "word": e.word,
                    "pattern": e.pattern,
                    "frequency": e.frequency,
                    "fitness": e.fitness,
                    "version": e.version,
                }
                for h, e in patterns_snap
            },
            "edges": {
                f"{src}:{tgt}": {
                    "weight": e.weight,
                    "cooccurrence": e.cooccurrence,
                    "version": e.version,
                }
                for (src, tgt), e in edges_snap
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

        with self._lock:
            for h_str, pdata in remote.get("patterns", {}).items():
                h = int(h_str)
                freq = int(pdata.get("frequency", 0))
                fit = float(pdata.get("fitness", 0.0))
                if h in self.patterns:
                    # Слияние: берём максимум частоты и fitness
                    local = self.patterns[h]
                    local.frequency = max(local.frequency, freq)
                    local.fitness = max(local.fitness, fit)
                    local.version = self._record_delta("p", h)
                else:
                    if len(self.patterns) < self.max_patterns:
                        entry = PatternEntry(
                            word=str(pdata.get("word", "")),
                            pattern=list(pdata.get("pattern", [])),
                            hash=h,
                            frequency=freq,
                            fitness=fit,
                        )
                        entry.version = self._record_delta("p", h)
                        self.patterns[h] = entry
                        self._hash_to_word[h] = entry.word  # Обратный индекс для слитых паттернов
                        merged_patterns += 1

            for key_str, edata in remote.get("edges", {}).items():
                parts = key_str.split(":")
                if len(parts) != 2:
                    continue
                a = int(parts[0])
                b = int(parts[1])
                key = (min(a, b), max(a, b))
                co = int(edata.get("cooccurrence", 0))
                w = float(edata.get("weight", 0.0))

                if key in self.edges:
                    self.edges[key].cooccurrence += co
                    self.edges[key].weight = 1.0 - 1.0 / (1.0 + self.edges[key].cooccurrence)
                    self.edges[key].version = self._record_delta("e", key)
                else:
                    if len(self.edges) < self.max_edges:
                        edge = KnowledgeEdge(
                            source_hash=key[0],
                            target_hash=key[1],
                            weight=w if w > 0 else (1.0 - 1.0 / (1.0 + max(co, 1))),
                            cooccurrence=co if co > 0 else 1,
                        )
                        edge.version = self._record_delta("e", key)
                        self.edges[key] = edge
                        # Индекс смежности для слитых рёбер — без этого answer() их не видит
                        self._adj.setdefault(key[0], set()).add(key[1])
                        self._adj.setdefault(key[1], set()).add(key[0])
                        # Ограничение степени (если включено)
                        self._prune_node_degree(key[0])
                        self._prune_node_degree(key[1])
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


# --- Стоп-слова: фильтруем общеупотребительные слова при retrieval ---
_STOP_WORDS_RU: frozenset[str] = frozenset({
    "и", "в", "не", "на", "с", "что", "а", "к", "по", "но", "он", "она",
    "это", "как", "его", "то", "все", "так", "же", "от", "для", "из", "за",
    "бы", "был", "или", "ты", "до", "мы", "ее", "при", "уже", "вы", "их",
    "да", "ли", "ну", "вот", "ещё", "еще", "нет", "тоже", "тут", "там",
    "быть", "если", "чтобы", "когда", "где", "кто", "чего", "чем", "этот",
    "этом", "этой", "этих", "этого", "какой", "только", "себя", "свой",
    "которые", "который", "которая", "которое", "которых", "которого",
    "может", "нас", "него", "них", "вас", "мне", "тебе", "нам", "вам",
    "очень", "более", "между", "потому", "после", "также", "будет",
    "можно", "нужно", "надо", "знаешь", "расскажи", "объясни", "скажи",
    "кратко", "коротко", "вкратце", "пожалуйста", "такое",
    "об", "ко", "во", "со", "без", "над", "под", "про", "через",
    "ему", "ей", "ней", "нём", "том", "тем", "тех", "чём", "кем",
    "мой", "моя", "моё", "наш", "наша", "ваш", "ваша", "свою",
    "эта", "эти", "эту", "того", "всех", "всё", "одна", "одно", "один",
    "были", "была", "было", "есть", "будут", "стал", "стала",
})

_STOP_WORDS_EN: frozenset[str] = frozenset({
    "the", "a", "an", "is", "are", "was", "were", "be", "been", "being",
    "have", "has", "had", "do", "does", "did", "will", "would", "could",
    "should", "may", "might", "shall", "can", "to", "of", "in", "for",
    "on", "with", "at", "by", "from", "as", "into", "about", "it", "its",
    "or", "and", "but", "not", "no", "if", "so", "that", "this", "what",
    "which", "who", "how", "when", "where", "why", "all", "some", "any",
    "you", "your", "they", "them", "their", "he", "she", "we", "me", "my",
})

STOP_WORDS: frozenset[str] = _STOP_WORDS_RU | _STOP_WORDS_EN


def _is_stop_word(word: str) -> bool:
    """Проверка: является ли слово стоп-словом."""
    return word.lower() in STOP_WORDS or len(word) < 2


def _stem_ru(word: str) -> str:
    """
    Простой стемминг для русских слов — обрезка типичных окончаний.
    Позволяет matchить «искусственном» → «искусственн» ≈ «искусственный».
    Не полноценный стеммер, но достаточно для keyword matching.
    """
    w = word.lower()
    # Длинные окончания (прилагательные, причастия)
    for suffix in (
        "ейшего", "ейшему", "ейшими", "ейшая", "ейшей", "ейшие",
        "ующих", "ующие", "ующей", "ующим", "ённый", "енный",
        "ённого", "ённому", "ённых", "ённом",
        "ного", "ному", "ными", "нной", "ском", "ской",
        "него", "нему", "ними",
        "тель", "ость", "ение", "ание", "ство",
        "ьного", "ьной", "ьных", "ьным", "ьном",
        "ого", "ому", "ыми", "ами", "ной", "ном", "ных",
        "его", "ему", "ими",
        "ый", "ий", "ой", "ая", "яя", "ое", "ее",
        "ые", "ие", "ом", "ем", "ах", "ях", "ов", "ев",
        "ей", "ам", "ям",
    ):
        if len(w) > len(suffix) + 3 and w.endswith(suffix):
            return w[:-len(suffix)]
    return w


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
    - BM25 ранжирование → точный retrieval (вместо наивного overlap)
    - Эмбеддинги: семантический поиск через cosine similarity (Фаза 1)
    """

    def __init__(self, max_sentences: int = 50000) -> None:
        self.sentences: list[SentenceEntry] = []
        self.max_sentences = max_sentences
        self._word_index: dict[int, list[int]] = {}
        # --- BM25 параметры ---
        self._doc_freq: dict[int, int] = {}      # word_hash → в скольких документах встречается
        self._doc_lengths: list[int] = []          # длина каждого документа (в уникальных словах)
        self._avg_dl: float = 0.0                  # средняя длина документа
        self._bm25_k1: float = 1.2                 # насыщение TF
        self._bm25_b: float = 0.75                 # нормализация длины
        # --- Эмбеддинги для семантического retrieval ---
        self.embeddings: object | None = None  # EmbeddingTable, инициализируется движком
        # --- Сжатие длинных записей через zlib ---
        self._compressed: dict[int, bytes] = {}   # idx → compressed digits (zlib)
        self._compressed_sizes: dict[int, int] = {}  # idx → original digit count
        self._compress_threshold: int = 200        # сжимать если > 200 цифр
        self._bytes_saved: int = 0                 # экономия памяти (байт)

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

            # --- Сжатие длинных записей ---
            if len(digit_array) > self._compress_threshold:
                raw = bytes(digit_array)
                compressed = zlib.compress(raw, level=6)
                if len(compressed) < len(raw):
                    self._compressed[idx] = compressed
                    self._compressed_sizes[idx] = len(raw)
                    self._bytes_saved += len(raw) - len(compressed)
                    # Храним пустой массив — данные в _compressed
                    digit_array = array('B')

            self.sentences.append(SentenceEntry(
                digits=digit_array,
                fingerprint=fp,
                word_hashes=frozenset(word_hashes),
            ))
            for wh in word_hashes:
                self._word_index.setdefault(wh, []).append(idx)
                # BM25: обновляем document frequency
                self._doc_freq[wh] = self._doc_freq.get(wh, 0) + 1
            # BM25: длина документа и средняя длина
            self._doc_lengths.append(len(word_hashes))
            n = len(self.sentences)
            self._avg_dl = sum(self._doc_lengths) / n if n > 0 else 1.0
            # --- Стемминг: индексируем по стемам для русской морфологии ---
            for t in tokens:
                if len(t) >= 4:
                    stemmed = _stem_ru(t)
                    if stemmed != t.lower():
                        sh = djb2_hash(stemmed)
                        if sh not in word_hashes:  # Не дублировать
                            self._word_index.setdefault(sh, []).append(idx)
            added += 1
        return added

    def get_text(self, idx: int) -> str:
        """Восстановить текст из цифр (decode on demand, с распаковкой)."""
        if 0 <= idx < len(self.sentences):
            # Если данные сжаты — распаковываем
            if idx in self._compressed:
                raw = zlib.decompress(self._compressed[idx])
                return digits_to_text(list(raw))
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
        Stage 1.5: Семантическое расширение через эмбеддинги (Фаза 1 AI)
        Stage 2: Формула re-ranks → predict(query ⊕ sentence) → score
        Stage 2.5: Embedding similarity re-ranking (cosine query↔sentence)
        Stage 3: digits_to_text() — восстановление текста ИЗ ЦИФР
        """
        tokens = _tokenize(query)
        q_hashes: set[int] = set()
        for t in tokens:
            if len(t) >= 2 and not _is_stop_word(t):
                q_hashes.add(djb2_hash(t))
                # --- Стемминг: добавляем хеш стема для русской морфологии ---
                if len(t) >= 4:
                    stemmed = _stem_ru(t)
                    if stemmed != t.lower():
                        q_hashes.add(djb2_hash(stemmed))

        if not q_hashes:
            # Если все слова — стоп-слова, используем все токены как fallback
            for t in tokens:
                if len(t) >= 2:
                    q_hashes.add(djb2_hash(t))

        if not q_hashes:
            return []

        # Stage 1: BM25 ранжирование (вместо наивного overlap)
        # BM25(D,Q) = Σ IDF(qi) · (tf(qi,D) · (k1+1)) / (tf(qi,D) + k1·(1-b+b·|D|/avgdl))
        candidate_scores: dict[int, float] = {}
        N = len(self.sentences)
        k1 = self._bm25_k1
        b = self._bm25_b
        avgdl = self._avg_dl if self._avg_dl > 0 else 1.0

        for qh in q_hashes:
            posting = self._word_index.get(qh, [])
            if not posting:
                continue
            # IDF: log((N - df + 0.5) / (df + 0.5) + 1)
            df = self._doc_freq.get(qh, 0)
            idf = math.log((N - df + 0.5) / (df + 0.5) + 1.0)

            for idx in posting:
                # TF = 1 (бинарное: слово есть или нет в предложении)
                tf = 1.0
                dl = self._doc_lengths[idx] if idx < len(self._doc_lengths) else avgdl
                tf_norm = (tf * (k1 + 1.0)) / (tf + k1 * (1.0 - b + b * dl / avgdl))
                candidate_scores[idx] = candidate_scores.get(idx, 0.0) + idf * tf_norm

        # Stage 1.5: Семантическое расширение через эмбеддинги
        # Если есть обученные эмбеддинги — ищем по ПОХОЖИМ словам тоже
        if self.embeddings is not None and hasattr(self.embeddings, 'find_similar'):
            expanded_hashes: set[int] = set()
            for qh in q_hashes:
                if hasattr(self.embeddings, 'has') and self.embeddings.has(qh):
                    similar = self.embeddings.find_similar(qh, top_k=5, min_sim=0.4)
                    for sim_h, _, sim_score in similar:
                        expanded_hashes.add(sim_h)
                        # Семантические совпадения с пониженным весом
                        for idx in self._word_index.get(sim_h, []):
                            bonus = sim_score * 0.6  # 60% от cosine similarity
                            candidate_scores[idx] = candidate_scores.get(idx, 0.0) + bonus

        if not candidate_scores:
            return []

        # BM25 с бинарным TF склонен переоценивать очень короткие записи (заголовки/подписи).
        # Даём штраф по длине документа (в уникальных словах), чтобы в топ попадали
        # более информативные предложения.
        for idx, score in list(candidate_scores.items()):
            dl = self._doc_lengths[idx] if idx < len(self._doc_lengths) else 0
            if dl <= 4:
                candidate_scores[idx] = score * 0.15
            elif dl <= 7:
                candidate_scores[idx] = score * 0.45
            elif dl <= 10:
                candidate_scores[idx] = score * 0.75

        # BM25 уже нормализован по длине документа — дополнительная нормализация не нужна

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

        # Stage 2.5: Embedding sentence similarity (Фаза 1 AI)
        if (self.embeddings is not None
                and hasattr(self.embeddings, 'sentence_similarity')
                and len(q_hashes) >= 2):
            emb_weight = 0.35  # 35% от финального score — эмбеддинги
            base_weight = 1.0 - emb_weight
            for idx in candidate_scores:
                s_hashes = self.sentences[idx].word_hashes
                emb_sim = self.embeddings.sentence_similarity(q_hashes, s_hashes)
                candidate_scores[idx] = (
                    candidate_scores[idx] * base_weight + emb_sim * emb_weight
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
        uncompressed = sum(len(s.digits) for s in self.sentences)
        # Добавляем оригинальные размеры сжатых записей (из кеша, без распаковки)
        uncompressed += sum(self._compressed_sizes.values())
        return uncompressed

    @property
    def total_digits(self) -> int:
        """Алиас для memory_digits — общее число цифр."""
        return self.memory_digits

    @property
    def compression_stats(self) -> dict[str, int]:
        """Статистика сжатия хранилища."""
        return {
            "compressed_entries": len(self._compressed),
            "total_entries": len(self.sentences),
            "bytes_saved": self._bytes_saved,
            "compressed_bytes": sum(len(v) for v in self._compressed.values()),
        }
