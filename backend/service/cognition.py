"""
Kolibri Cognition — высшие когнитивные функции ядра.

Модуль объединяет 5 когнитивных способностей KnowledgeGraph
в единый API для использования SwarmNode, агентами и сервисами.

Когнитивные функции:
  1. Абстрактное мышление  (reason_abstract)   — N-хоповое обобщение
  2. Причинное рассуждение (reason_causal)      — направленные цепочки
  3. Индуктивный вывод     (induce_rules)       — ассоциативные правила
  4. Перенос структуры     (transfer_analogy)   — A:B :: C:?
  5. Самомоделирование     (self_model)         — предсказание компетентности

Все функции реализованы на уровне co-occurrence графов.
Механизмы:
  • Абстракция   — через N-хоповую навигацию
  • Каузальность — через статистику порядка слов
  • Индукция     — через ассоциативные правила
  • Аналогии     — через структурное сходство (Jaccard)
  • Рефлексия    — через анализ покрытия графа
"""
from __future__ import annotations

from dataclasses import dataclass, field

from .number_mind import KnowledgeGraph, _tokenize, djb2_hash


# ═══════════════════════════════════════════════════════════════
# CausalIndex — направленный граф причинности
# ═══════════════════════════════════════════════════════════════

@dataclass
class CausalIndex:
    """
    Каузальный индекс: направление связей по порядку слов.

    Если A систематически стоит ПЕРЕД B → A скорее ПРИЧИНА B.
    Индекс строится из корпуса текстов и хранится отдельно
    от KnowledgeGraph для повторного использования.

    Использование:
        idx = CausalIndex.from_texts(["текст1", "текст2", ...])
        chain = graph.reason_causal(idx.pairs, "нейросети слои", direction="why")
    """
    pairs: dict[tuple[int, int], float] = field(default_factory=dict)
    n_texts: int = 0
    n_directed: int = 0  # пар с |score-0.5| > 0.1

    @staticmethod
    def from_texts(texts: list[str], window: int = 5) -> CausalIndex:
        """Построить каузальный индекс из корпуса текстов."""
        g = KnowledgeGraph()
        pairs = g.build_causal_index(texts, window=window)
        n_dir = sum(1 for s in pairs.values() if abs(s - 0.5) > 0.1)
        return CausalIndex(pairs=pairs, n_texts=len(texts), n_directed=n_dir)

    @staticmethod
    def from_graph(graph: KnowledgeGraph,
                   texts: list[str],
                   window: int = 5) -> CausalIndex:
        """Построить каузальный индекс с привязкой к конкретному графу."""
        pairs = graph.build_causal_index(texts, window=window)
        n_dir = sum(1 for s in pairs.values() if abs(s - 0.5) > 0.1)
        return CausalIndex(pairs=pairs, n_texts=len(texts), n_directed=n_dir)

    def why(self, graph: KnowledgeGraph,
            query: str, max_chain: int = 4) -> list[tuple[str, float]]:
        """Найти причинную цепочку: ПОЧЕМУ?"""
        return graph.reason_causal(self.pairs, query,
                                   direction="why", max_chain=max_chain)

    def then(self, graph: KnowledgeGraph,
             query: str, max_chain: int = 4) -> list[tuple[str, float]]:
        """Найти следственную цепочку: ЧТО ДАЛЬШЕ?"""
        return graph.reason_causal(self.pairs, query,
                                   direction="then", max_chain=max_chain)

    def direction(self, word_a: str, word_b: str) -> float:
        """Направление связи A→B. >0.5 = A причина B, <0.5 = B причина A."""
        ha, hb = djb2_hash(word_a), djb2_hash(word_b)
        return self.pairs.get((ha, hb), 0.5)


# ═══════════════════════════════════════════════════════════════
# CognitionResult — результат когнитивной операции
# ═══════════════════════════════════════════════════════════════

@dataclass
class CognitionResult:
    """
    Унифицированный результат когнитивной операции.

    Используется для возврата результатов любой из 5 функций.
    """
    method: str                          # "abstract" | "causal" | "induction" | "transfer" | "self_model"
    success: bool                        # Успех операции
    answer: str = ""                     # Текстовый ответ (если есть)
    confidence: float = 0.0              # Уверенность
    chain: list[tuple[str, float]] = field(default_factory=list)  # Цепочка (каузал)
    rules: list[tuple[str, str, int, float]] = field(default_factory=list)  # Правила (индукция)
    analogies: list[tuple[str, float]] = field(default_factory=list)  # Аналогии (перенос)
    introspection: dict = field(default_factory=dict)  # Самомоделирование
    metadata: dict = field(default_factory=dict)       # Доп. информация


# ═══════════════════════════════════════════════════════════════
# SwarmCognition — единый API когнитивных функций
# ═══════════════════════════════════════════════════════════════

class SwarmCognition:
    """
    Высокоуровневый когнитивный движок Kolibri.

    Оборачивает KnowledgeGraph и CausalIndex в единый API:

        cog = SwarmCognition(graph)
        cog.learn_causality(corpus_texts)

        # Абстрактное мышление
        result = cog.abstract("go горутины", depth=2)

        # Причинное рассуждение
        result = cog.why("нейросети слои")
        result = cog.then("docker образ")

        # Индуктивный вывод
        result = cog.induce(min_support=3)

        # Перенос структуры
        result = cog.analogy("python", "язык", "java")

        # Самомоделирование
        result = cog.introspect("квантовые вычисления")
    """

    def __init__(self, graph: KnowledgeGraph) -> None:
        self.graph = graph
        self._causal: CausalIndex | None = None

    @property
    def causal_index(self) -> CausalIndex | None:
        """Текущий каузальный индекс (None если не обучен)."""
        return self._causal

    def learn_causality(self, texts: list[str], window: int = 5) -> CausalIndex:
        """
        Обучить каузальный индекс из корпуса текстов.

        Вызовите перед reason_causal / why / then.
        """
        self._causal = CausalIndex.from_graph(self.graph, texts, window=window)
        return self._causal

    # ─── Абстрактное мышление ─────────────────────────────────

    def abstract(
        self,
        query: str,
        max_words: int = 10,
        depth: int = 2,
    ) -> CognitionResult:
        """
        Абстрактное мышление: N-хоповое обобщение.

        Расширяет answer() на *depth* хопов с затуханием.
        Позволяет находить связи, ОТСУТСТВУЮЩИЕ в обучающих данных.
        """
        answer, conf = self.graph.reason_abstract(
            query, max_words=max_words, depth=depth,
        )
        return CognitionResult(
            method="abstract",
            success=conf > 0.0,
            answer=answer,
            confidence=conf,
            metadata={"depth": depth},
        )

    # ─── Причинное рассуждение ────────────────────────────────

    def why(self, query: str, max_chain: int = 4) -> CognitionResult:
        """
        Почему? → каузальная цепочка назад.

        Требует предварительного вызова learn_causality().
        """
        if self._causal is None:
            return CognitionResult(
                method="causal",
                success=False,
                metadata={"error": "CausalIndex не построен. Вызовите learn_causality() сначала."},
            )
        chain = self._causal.why(self.graph, query, max_chain=max_chain)
        return CognitionResult(
            method="causal",
            success=len(chain) > 0,
            chain=chain,
            confidence=chain[0][1] if chain else 0.0,
            answer=' ← '.join(w for w, _ in chain) if chain else "",
        )

    def then(self, query: str, max_chain: int = 4) -> CognitionResult:
        """
        Что дальше? → каузальная цепочка вперёд.

        Требует предварительного вызова learn_causality().
        """
        if self._causal is None:
            return CognitionResult(
                method="causal",
                success=False,
                metadata={"error": "CausalIndex не построен."},
            )
        chain = self._causal.then(self.graph, query, max_chain=max_chain)
        return CognitionResult(
            method="causal",
            success=len(chain) > 0,
            chain=chain,
            confidence=chain[0][1] if chain else 0.0,
            answer=' → '.join(w for w, _ in chain) if chain else "",
        )

    # ─── Индуктивный вывод ────────────────────────────────────

    def induce(
        self,
        min_support: int = 3,
        min_confidence: float = 0.6,
    ) -> CognitionResult:
        """
        Индуктивный вывод: автоматическое извлечение правил.

        Правило: «если X связано с A, то X связано и с B» (confidence %).
        """
        rules = self.graph.induce_rules(
            min_support=min_support,
            min_confidence=min_confidence,
        )
        return CognitionResult(
            method="induction",
            success=len(rules) > 0,
            rules=rules,
            confidence=rules[0][3] if rules else 0.0,
            metadata={"n_rules": len(rules)},
        )

    # ─── Перенос структуры ────────────────────────────────────

    def analogy(
        self,
        a: str,
        b: str,
        c: str,
        max_results: int = 5,
    ) -> CognitionResult:
        """
        Перенос структуры: A:B :: C:?

        Структурный профиль B переносится на контекст C.
        """
        results = self.graph.transfer_analogy(a, b, c, max_results=max_results)
        top = results[0] if results else ("", 0.0)
        return CognitionResult(
            method="transfer",
            success=len(results) > 0,
            analogies=results,
            answer=top[0],
            confidence=top[1],
            metadata={"a": a, "b": b, "c": c},
        )

    # ─── Самомоделирование ────────────────────────────────────

    def introspect(self, query: str) -> CognitionResult:
        """
        Самомоделирование: граф предсказывает свою компетентность.

        Возвращает какие слова запроса граф знает / не знает,
        и предсказанную уверенность ответа.
        """
        info = self.graph.self_model(query)
        return CognitionResult(
            method="self_model",
            success=info["coverage"] > 0.0,
            confidence=info["predicted_confidence"],
            introspection=info,
            metadata={"coverage": info["coverage"],
                       "edge_density": info["edge_density"]},
        )

    # ─── Универсальный ответ с когнитивным расширением ────────

    def enhanced_answer(
        self,
        query: str,
        max_words: int = 10,
        use_abstract: bool = True,
        use_causal: bool = True,
        use_introspect: bool = True,
    ) -> dict:
        """
        Расширенный ответ с когнитивными функциями:

        1. self_model → предсказание компетентности
        2. answer (1-хоп) + reason_abstract (2-хоп)
        3. causal_chain (если CausalIndex обучен)

        Returns:
            {
                "answer_1hop": str,
                "answer_2hop": str,
                "confidence_1hop": float,
                "confidence_2hop": float,
                "causal_why": [...],
                "causal_then": [...],
                "self_model": {...},
                "gain_words": int  (сколько новых слов дал 2-хоп),
            }
        """
        result: dict = {}

        # Самомоделирование
        if use_introspect:
            sm = self.graph.self_model(query)
            result["self_model"] = sm
        else:
            result["self_model"] = {}

        # 1-хоп (стандартный answer)
        a1, c1, meta1 = self.graph.answer(query, max_words=max_words)
        result["answer_1hop"] = a1
        result["confidence_1hop"] = c1

        # 2-хоп (абстрактное мышление)
        if use_abstract:
            a2, c2 = self.graph.reason_abstract(query, max_words=max_words, depth=2)
            result["answer_2hop"] = a2
            result["confidence_2hop"] = c2
            w1 = set(_tokenize(a1))
            w2 = set(_tokenize(a2))
            result["gain_words"] = len(w2 - w1)
        else:
            result["answer_2hop"] = a1
            result["confidence_2hop"] = c1
            result["gain_words"] = 0

        # Каузальные цепочки
        if use_causal and self._causal is not None:
            result["causal_why"] = self._causal.why(self.graph, query, max_chain=3)
            result["causal_then"] = self._causal.then(self.graph, query, max_chain=3)
        else:
            result["causal_why"] = []
            result["causal_then"] = []

        return result
