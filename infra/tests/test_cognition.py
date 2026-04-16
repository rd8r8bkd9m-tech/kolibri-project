"""
Тесты для модуля cognition — высшие когнитивные функции.
"""
from __future__ import annotations

import pytest

from backend.service.cognition import CausalIndex, CognitionResult, SwarmCognition
from backend.service.number_mind import KnowledgeGraph


@pytest.fixture
def trained_graph() -> KnowledgeGraph:
    g = KnowledgeGraph()
    g.train_text("кот домашнее животное")
    g.train_text("собака домашнее животное")
    g.train_text("кошка ловит мышей")
    g.train_text("физика наука о природе")
    g.train_text("химия наука о веществах")
    g.train_text("Python язык программирования")
    g.train_text("Java язык программирования")
    return g


@pytest.fixture
def cognition(trained_graph: KnowledgeGraph) -> SwarmCognition:
    return SwarmCognition(trained_graph)


# ---------------------------------------------------------------------------
# CausalIndex
# ---------------------------------------------------------------------------


class TestCausalIndex:
    def test_from_texts(self) -> None:
        texts = [
            "дождь идёт лужи появляются",
            "солнце светит тепло становится",
        ]
        ci = CausalIndex.from_texts(texts, window=5)
        assert ci.n_texts == 2
        assert isinstance(ci.pairs, dict)

    def test_from_graph(self, trained_graph: KnowledgeGraph) -> None:
        texts = ["кот домашнее животное", "собака домашнее животное"]
        ci = CausalIndex.from_graph(trained_graph, texts, window=5)
        assert ci.n_texts == 2

    def test_why_chain(self, trained_graph: KnowledgeGraph) -> None:
        texts = ["кот домашнее животное", "кот ловит мышей"]
        ci = CausalIndex.from_graph(trained_graph, texts, window=5)
        chain = ci.why(trained_graph, "кот", max_chain=3)
        assert isinstance(chain, list)

    def test_then_chain(self, trained_graph: KnowledgeGraph) -> None:
        texts = ["дождь лужи влажность"]
        ci = CausalIndex.from_graph(trained_graph, texts, window=5)
        chain = ci.then(trained_graph, "дождь", max_chain=3)
        assert isinstance(chain, list)

    def test_direction(self) -> None:
        ci = CausalIndex()
        # Неизвестная пара — нейтральное 0.5
        result = ci.direction("слово1", "слово2")
        assert result == 0.5


# ---------------------------------------------------------------------------
# CognitionResult
# ---------------------------------------------------------------------------


class TestCognitionResult:
    def test_basic_creation(self) -> None:
        r = CognitionResult(method="abstract", success=True, answer="тест", confidence=0.8)
        assert r.method == "abstract"
        assert r.success is True
        assert r.answer == "тест"
        assert r.confidence == 0.8

    def test_defaults(self) -> None:
        r = CognitionResult(method="causal", success=False)
        assert r.answer == ""
        assert r.confidence == 0.0
        assert r.chain == []


# ---------------------------------------------------------------------------
# SwarmCognition
# ---------------------------------------------------------------------------


class TestSwarmCognition:
    def test_abstract(self, cognition: SwarmCognition) -> None:
        result = cognition.abstract("кот", depth=2)
        assert isinstance(result, CognitionResult)
        assert result.method == "abstract"
        assert result.success is True

    def test_why(self, cognition: SwarmCognition) -> None:
        result = cognition.why("кот", max_chain=3)
        assert isinstance(result, CognitionResult)
        assert result.method == "causal"

    def test_then(self, cognition: SwarmCognition) -> None:
        result = cognition.then("кот", max_chain=3)
        assert isinstance(result, CognitionResult)
        assert result.method == "causal"

    def test_induce(self, cognition: SwarmCognition) -> None:
        result = cognition.induce(min_support=1, min_confidence=0.1)
        assert isinstance(result, CognitionResult)
        assert result.method == "induction"

    def test_analogy(self, cognition: SwarmCognition) -> None:
        result = cognition.analogy("кот", "кошка", "собака", max_results=3)
        assert isinstance(result, CognitionResult)
        assert result.method == "transfer"

    def test_introspect(self, cognition: SwarmCognition) -> None:
        result = cognition.introspect("кот собака")
        assert isinstance(result, CognitionResult)
        assert result.method == "self_model"
        assert result.introspection is not None

    def test_enhanced_answer(self, cognition: SwarmCognition) -> None:
        result = cognition.enhanced_answer("что такое животное?")
        assert isinstance(result, dict)

    def test_learn_causality(self, cognition: SwarmCognition) -> None:
        texts = ["кот домашнее животное", "собака тоже домашнее"]
        ci = cognition.learn_causality(texts, window=5)
        assert isinstance(ci, CausalIndex)
        assert cognition.causal_index is not None

    def test_causal_index_property(self, cognition: SwarmCognition) -> None:
        assert cognition.causal_index is None  # До learn_causality
        cognition.learn_causality(["текст один", "текст два"])
        assert cognition.causal_index is not None
