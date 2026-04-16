"""
unified_knowledge_hub.py — Единый слой доступа ко всем подсистемам знаний

Объединяет:
- .klm паттерны (corpus_trainer)
- Граф знаний (co-occurrence edges)
- Формулы (4000-digit genomes)
- World Model embeddings
- Фрактальная память
- Логическая память
- Embeddings (word2vec)
- Sentence Store

Один API: «что мы знаем о X» → мульти-ретривал → голосование → лучший ответ
"""
from __future__ import annotations

import logging
import math
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Optional

from .number_mind import (
    KnowledgeGraph,
    FormulaPool,
    word_to_pattern,
    pattern_similarity,
    pattern_to_str,
    _tokenize,
)
from .project_paths import get_project_root

log = logging.getLogger("kolibri.unified_knowledge_hub")


# ============================================================================
# Data Structures
# ============================================================================

@dataclass
class KnowledgeSource:
    """Результат от одного источника знаний."""
    name: str
    score: float
    content: str
    metadata: dict = field(default_factory=dict)


@dataclass
class KnowledgeResponse:
    """Объединённый ответ от всех источников."""
    query: str
    sources: list[KnowledgeSource]
    best_answer: str
    confidence: float
    total_sources: int
    fusion_method: str
    duration_ms: float


# ============================================================================
# Unified Knowledge Hub
# ============================================================================

class UnifiedKnowledgeHub:
    """
    Единая точка доступа ко всем подсистемам знаний.

    При запросе «что такое вода»:
    1. Ищет паттерн в .klm
    2. Ищет формулу с похожим fitness
    3. Ищет embedding в World Model
    4. Ищет узел во фрактальной памяти
    5. Ищет логическое выражение
    6. Голосование → лучший ответ
    """

    def __init__(
        self,
        graph: Optional[KnowledgeGraph] = None,
        formula_pool: Optional[FormulaPool] = None,
    ):
        self._graph = graph
        self._formula_pool = formula_pool
        self._project_root = get_project_root()

        # Weights for voting
        self._source_weights = {
            "klm_pattern": 1.0,
            "knowledge_graph": 1.5,
            "formula": 1.2,
            "embedding": 0.8,
            "sentence_store": 0.6,
            "logical_memory": 1.0,
        }

        log.info("[UKH] Unified Knowledge Hub initialized")

    # -----------------------------------------------------------------------
    # Main Query Interface
    # -----------------------------------------------------------------------

    def query(self, text: str, top_k: int = 5) -> KnowledgeResponse:
        """
        Главный метод: «что мы знаем о X?»

        Запускает параллельный поиск во всех подсистемах,
        затем объединяет результаты через взвешенное голосование.
        """
        start = time.time()
        sources: list[KnowledgeSource] = []

        # 1. Поиск по .klm паттернам
        try:
            klm_result = self._query_klm_patterns(text)
            sources.extend(klm_result)
        except Exception as exc:
            log.warning("[UKH] KLM pattern query failed: %s", exc)

        # 2. Поиск по графу знаний
        try:
            graph_result = self._query_knowledge_graph(text)
            sources.extend(graph_result)
        except Exception as exc:
            log.warning("[UKH] Knowledge graph query failed: %s", exc)

        # 3. Поиск по формулам
        try:
            formula_result = self._query_formulas(text)
            sources.extend(formula_result)
        except Exception as exc:
            log.warning("[UKH] Formula query failed: %s", exc)

        # 4. Поиск по embeddings
        try:
            embedding_result = self._query_embeddings(text)
            sources.extend(embedding_result)
        except Exception as exc:
            log.warning("[UKH] Embedding query failed: %s", exc)

        # 5. Поиск в sentence store
        try:
            sentence_result = self._query_sentence_store(text)
            sources.extend(sentence_result)
        except Exception as exc:
            log.warning("[UKH] Sentence store query failed: %s", exc)

        # Fusion: взвешенное голосование
        best_answer, confidence, method = self._fuse_sources(sources, top_k)

        elapsed = (time.time() - start) * 1000

        return KnowledgeResponse(
            query=text,
            sources=sources,
            best_answer=best_answer,
            confidence=confidence,
            total_sources=len(sources),
            fusion_method=method,
            duration_ms=round(elapsed, 2),
        )

    # -----------------------------------------------------------------------
    # Source: KLM Patterns
    # -----------------------------------------------------------------------

    def _query_klm_patterns(self, text: str) -> list[KnowledgeSource]:
        """Поиск по .klm паттернам (corpus_trainer)."""
        graph = self._get_graph()
        tokens = _tokenize(text)
        if not tokens:
            return []

        results: list[KnowledgeSource] = []
        for token in tokens[:10]:  # Ограничиваем число токенов
            if len(token) < 2:
                continue

            # Ищем паттерн
            pattern = word_to_pattern(token)
            if token in graph.patterns:
                entry = graph.patterns[token]
                results.append(KnowledgeSource(
                    name="klm_pattern",
                    score=entry.fitness,
                    content=f"{token} (fitness={entry.fitness:.4f}, freq={entry.frequency})",
                    metadata={
                        "pattern": entry.pattern[:20],
                        "frequency": entry.frequency,
                        "fitness": entry.fitness,
                    },
                ))

        return results[:5]

    # -----------------------------------------------------------------------
    # Source: Knowledge Graph
    # -----------------------------------------------------------------------

    def _query_knowledge_graph(self, text: str) -> list[KnowledgeSource]:
        """Поиск по графу знаний (co-occurrence edges)."""
        graph = self._get_graph()
        tokens = _tokenize(text)
        if not tokens:
            return []

        results: list[KnowledgeSource] = []
        for token in tokens[:10]:
            if len(token) < 2:
                continue

            # Ищем связанные узлы
            neighbors = graph.get_neighbors(token, top_k=5)
            for neighbor, weight in neighbors:
                results.append(KnowledgeSource(
                    name="knowledge_graph",
                    score=weight,
                    content=f"{token} → {neighbor} (weight={weight:.4f})",
                    metadata={
                        "source": token,
                        "target": neighbor,
                        "weight": weight,
                    },
                ))

        return results[:10]

    # -----------------------------------------------------------------------
    # Source: Formulas
    # -----------------------------------------------------------------------

    def _query_formulas(self, text: str) -> list[KnowledgeSource]:
        """Поиск по формулам (4000-digit genomes)."""
        pool = self._get_formula_pool()
        if not pool or not pool.formulas:
            return []

        # Хеш запроса для поиска ассоциаций
        from .number_mind import fnv1a_hash
        query_hash = fnv1a_hash(text.lower())

        results: list[KnowledgeSource] = []
        for formula in pool.formulas[:20]:  # Топ-20 формул
            # Проверяем ассоциации
            for assoc in formula.associations[:5]:
                if assoc.input_hash == query_hash or assoc.output_hash == query_hash:
                    results.append(KnowledgeSource(
                        name="formula",
                        score=formula.fitness,
                        content=f"Formula #{formula.gene.to_hex()[:16]} (fitness={formula.fitness:.4f})",
                        metadata={
                            "formula_hex": formula.gene.to_hex()[:32],
                            "fitness": formula.fitness,
                            "association_input": assoc.input_text[:50] if assoc.input_text else "",
                            "association_output": assoc.output_text[:50] if assoc.output_text else "",
                        },
                    ))

        return results[:5]

    # -----------------------------------------------------------------------
    # Source: Embeddings
    # -----------------------------------------------------------------------

    def _query_embeddings(self, text: str) -> list[KnowledgeSource]:
        """Поиск по embeddings (word2vec)."""
        graph = self._get_graph()
        if not graph.embeddings:
            return []

        tokens = _tokenize(text)
        if not tokens:
            return []

        results: list[KnowledgeSource] = []
        for token in tokens[:5]:
            if len(token) < 2:
                continue

            # Ищем похожие слова через embeddings
            try:
                similar = graph.embeddings.find_similar(token, top_k=5)
                for word, similarity_score in similar:
                    results.append(KnowledgeSource(
                        name="embedding",
                        score=similarity_score,
                        content=f"{token} ~ {word} (sim={similarity_score:.4f})",
                        metadata={
                            "query": token,
                            "similar": word,
                            "similarity": similarity_score,
                        },
                    ))
            except Exception:
                pass

        return results[:5]

    # -----------------------------------------------------------------------
    # Source: Sentence Store
    # -----------------------------------------------------------------------

    def _query_sentence_store(self, text: str) -> list[KnowledgeSource]:
        """Поиск в хранилище предложений."""
        graph = self._get_graph()
        tokens = _tokenize(text)
        if not tokens:
            return []

        results: list[KnowledgeSource] = []
        # Ищем предложения содержащие токены
        for token in tokens[:5]:
            if len(token) < 2:
                continue

            try:
                sentences = graph.sentence_store.search(token, top_k=3)
                for sentence, score in sentences:
                    results.append(KnowledgeSource(
                        name="sentence_store",
                        score=score,
                        content=sentence[:200],
                        metadata={
                            "query_token": token,
                            "score": score,
                        },
                    ))
            except Exception:
                pass

        return results[:5]

    # -----------------------------------------------------------------------
    # Fusion
    # -----------------------------------------------------------------------

    def _fuse_sources(
        self,
        sources: list[KnowledgeSource],
        top_k: int,
    ) -> tuple[str, float, str]:
        """
        Объединяет результаты через взвешенное голосование.

        Returns: (best_answer, confidence, method)
        """
        if not sources:
            return "Нет данных", 0.0, "no_sources"

        # Нормализуем scores по весам источников
        weighted_scores = []
        for source in sources:
            weight = self._source_weights.get(source.name, 0.5)
            weighted_scores.append((source, source.score * weight))

        # Сортируем
        weighted_scores.sort(key=lambda x: -x[1])

        # Берём top_k
        top_sources = weighted_scores[:top_k]

        if not top_sources:
            return "Нет данных", 0.0, "no_results"

        # Лучший ответ
        best_source = top_sources[0][0]
        best_answer = best_source.content
        confidence = min(1.0, top_sources[0][1])

        # Если несколько источников с близкими scores — объединяем
        if len(top_sources) > 1:
            top_score = top_sources[0][1]
            close_sources = [
                s for s, ws in top_sources
                if ws >= top_score * 0.7
            ]
            if len(close_sources) > 1:
                best_answer = " | ".join(s.content for s in close_sources[:3])
                confidence = min(1.0, confidence * 1.1)

        method = "weighted_voting"
        return best_answer, round(confidence, 4), method

    # -----------------------------------------------------------------------
    # Helpers
    # -----------------------------------------------------------------------

    def _get_graph(self) -> KnowledgeGraph:
        if self._graph is None:
            from .ai_engine import get_engine
            engine = get_engine()
            self._graph = engine.graph
        return self._graph

    def _get_formula_pool(self) -> FormulaPool | None:
        if self._formula_pool is None:
            try:
                from .ai_engine import get_engine
                engine = get_engine()
                self._formula_pool = engine.formula_pool
            except Exception:
                pass
        return self._formula_pool

    # -----------------------------------------------------------------------
    # Analytics
    # -----------------------------------------------------------------------

    def get_analytics(self) -> dict[str, Any]:
        """Возвращает аналитику по всем подсистемам знаний."""
        graph = self._get_graph()
        pool = self._get_formula_pool()

        analytics: dict[str, Any] = {
            "knowledge_graph": {
                "patterns": len(graph.patterns),
                "edges": len(graph.edges),
                "documents": graph.documents_trained,
                "tokens": graph.tokens_processed,
            },
            "formula_pool": {
                "size": len(pool.formulas) if pool else 0,
                "generation": pool.generation if pool else 0,
                "best_fitness": max((f.fitness for f in (pool.formulas if pool else [])), default=0.0),
            },
            "embeddings": {
                "vocab_size": graph.embeddings.vocab_size if graph.embeddings else 0,
                "trained_pairs": graph.embeddings.trained_pairs if graph.embeddings else 0,
                "epochs": graph.embeddings.epochs_completed if graph.embeddings else 0,
            },
        }

        return analytics


# ============================================================================
# Singleton
# ============================================================================

_hub_instance: UnifiedKnowledgeHub | None = None


def get_unified_knowledge_hub(
    graph: Optional[KnowledgeGraph] = None,
    formula_pool: Optional[FormulaPool] = None,
) -> UnifiedKnowledgeHub:
    """Возвращает singleton Unified Knowledge Hub."""
    global _hub_instance
    if _hub_instance is None:
        _hub_instance = UnifiedKnowledgeHub(
            graph=graph,
            formula_pool=formula_pool,
        )
    return _hub_instance
