"""
cognition_api.py — FastAPI роутер для когнитивных функций Kolibri

Эндпоинты высшего мышления:
  /abstract   — абстрактное мышление (N-хоповое обобщение)
  /causal     — причинное рассуждение (почему / что потом)
  /induce     — индуктивный вывод (извлечение правил)
  /analogy    — перенос структуры (A:B :: C:?)
  /introspect — самомоделирование (рефлексия)
  /enhanced   — комбинированный когнитивный ответ
"""
from __future__ import annotations

import time
from typing import Optional

from fastapi import APIRouter, HTTPException
from pydantic import BaseModel, Field

from .ai_engine import get_engine
from .cognition import SwarmCognition, CausalIndex

router = APIRouter(prefix="/api/v1/cognition", tags=["cognition"])


# ---------------------------------------------------------------------------
# Модели запросов/ответов
# ---------------------------------------------------------------------------

class AbstractRequest(BaseModel):
    query: str = Field(min_length=1, max_length=4096,
                       description="Запрос для абстрактного обобщения")
    depth: int = Field(default=2, ge=1, le=5,
                       description="Глубина хопов по графу")
    max_words: int = Field(default=10, ge=1, le=50)


class AbstractResponse(BaseModel):
    query: str
    abstract_answer: str
    confidence: float
    depth: int
    duration_ms: float


class CausalRequest(BaseModel):
    query: str = Field(min_length=1, max_length=4096,
                       description="Запрос для причинного рассуждения")
    direction: str = Field(default="why",
                           description="'why' — причины, 'then' — следствия")
    max_chain: int = Field(default=3, ge=1, le=10)


class CausalChainItem(BaseModel):
    word: str
    score: float


class CausalResponse(BaseModel):
    query: str
    direction: str
    chain: list[CausalChainItem]
    chain_length: int
    duration_ms: float


class InduceRequest(BaseModel):
    min_support: int = Field(default=3, ge=1, le=100,
                             description="Минимальная поддержка правила")
    min_confidence: float = Field(default=0.6, ge=0.0, le=1.0,
                                  description="Минимальная уверенность правила")


class RuleItem(BaseModel):
    premise: str
    conclusion: str
    support: int
    confidence: float


class InduceResponse(BaseModel):
    rules: list[RuleItem]
    total_rules: int
    duration_ms: float


class AnalogyRequest(BaseModel):
    a: str = Field(min_length=1, max_length=256, description="Слово A")
    b: str = Field(min_length=1, max_length=256, description="Слово B")
    c: str = Field(min_length=1, max_length=256, description="Слово C")
    max_results: int = Field(default=5, ge=1, le=20)


class AnalogyItem(BaseModel):
    word: str
    score: float


class AnalogyResponse(BaseModel):
    a: str
    b: str
    c: str
    analogies: list[AnalogyItem]
    description: str
    duration_ms: float


class IntrospectRequest(BaseModel):
    query: str = Field(min_length=1, max_length=4096,
                       description="Запрос для самоанализа")


class IntrospectResponse(BaseModel):
    query: str
    predicted_confidence: float
    coverage: float
    edge_density: float
    known_words: list[str]
    unknown_words: list[str]
    duration_ms: float


class EnhancedRequest(BaseModel):
    query: str = Field(min_length=1, max_length=4096,
                       description="Запрос для полного когнитивного ответа")


class EnhancedResponse(BaseModel):
    query: str
    answer_1hop: str
    answer_2hop: str
    confidence_1hop: float
    confidence_2hop: float
    causal_chain: list[CausalChainItem]
    self_model: dict
    duration_ms: float


class CausalLearnRequest(BaseModel):
    texts: list[str] = Field(min_items=1,
                             description="Тексты для построения каузального индекса")
    window: int = Field(default=5, ge=2, le=20)


class CausalLearnResponse(BaseModel):
    pairs: int
    directed: int
    duration_ms: float


# ---------------------------------------------------------------------------
# Хелпер — получить SwarmCognition из engine
# ---------------------------------------------------------------------------

def _get_cognition() -> SwarmCognition:
    """Возвращает SwarmCognition, привязанный к графу движка."""
    engine = get_engine()
    return engine.get_cognition()


# ---------------------------------------------------------------------------
# Эндпоинты
# ---------------------------------------------------------------------------

@router.post("/abstract", response_model=AbstractResponse)
async def abstract_reasoning(req: AbstractRequest) -> AbstractResponse:
    """
    Абстрактное мышление — N-хоповое обобщение по графу знаний.

    Обобщает понятия через промежуточные связи:
    «кот» → (1-хоп) «животное» → (2-хоп) «биология», «природа»
    """
    t0 = time.time()
    cog = _get_cognition()
    result = cog.abstract(req.query, depth=req.depth, max_words=req.max_words)

    return AbstractResponse(
        query=req.query,
        abstract_answer=result.answer,
        confidence=result.confidence,
        depth=req.depth,
        duration_ms=round((time.time() - t0) * 1000, 1),
    )


@router.post("/causal", response_model=CausalResponse)
async def causal_reasoning(req: CausalRequest) -> CausalResponse:
    """
    Причинное рассуждение — поиск причин (why) или следствий (then).

    Использует направленный каузальный граф, построенный
    из порядка слов в обучающих текстах.
    """
    if req.direction not in ("why", "then"):
        raise HTTPException(status_code=400,
                            detail="direction must be 'why' or 'then'")
    t0 = time.time()
    cog = _get_cognition()
    result = cog.why(req.query, max_chain=req.max_chain) \
        if req.direction == "why" \
        else cog.then(req.query, max_chain=req.max_chain)

    chain = [CausalChainItem(word=w, score=round(s, 4))
             for w, s in (result.chain or [])]

    return CausalResponse(
        query=req.query,
        direction=req.direction,
        chain=chain,
        chain_length=len(chain),
        duration_ms=round((time.time() - t0) * 1000, 1),
    )


@router.post("/causal/learn", response_model=CausalLearnResponse)
async def causal_learn(req: CausalLearnRequest) -> CausalLearnResponse:
    """
    Обучить каузальный индекс на текстах.

    Строит направленный граф причинности из порядка слов.
    Нужно вызвать ПЕРЕД /causal, /causal/why, /causal/then.
    """
    t0 = time.time()
    cog = _get_cognition()
    idx = cog.learn_causality(req.texts, window=req.window)

    return CausalLearnResponse(
        pairs=len(idx.pairs),
        directed=idx.n_directed,
        duration_ms=round((time.time() - t0) * 1000, 1),
    )


@router.post("/induce", response_model=InduceResponse)
async def induce_rules(req: InduceRequest) -> InduceResponse:
    """
    Индуктивный вывод — автоматическое извлечение правил из графа.

    Находит закономерности вида:
    «если X связано с Y, то с высокой вероятностью X связано с Z»
    """
    t0 = time.time()
    cog = _get_cognition()
    result = cog.induce(min_support=req.min_support,
                        min_confidence=req.min_confidence)

    rules = [RuleItem(premise=p, conclusion=c,
                      support=s, confidence=round(cf, 4))
             for p, c, s, cf in (result.rules or [])]

    return InduceResponse(
        rules=rules,
        total_rules=len(rules),
        duration_ms=round((time.time() - t0) * 1000, 1),
    )


@router.post("/analogy", response_model=AnalogyResponse)
async def find_analogy(req: AnalogyRequest) -> AnalogyResponse:
    """
    Перенос структуры — аналогии A:B :: C:?.

    Ищет D такое что отношение C:D структурно похоже на A:B.
    Использует Jaccard-сходство профилей соседей.
    """
    t0 = time.time()
    cog = _get_cognition()
    result = cog.analogy(req.a, req.b, req.c,
                         max_results=req.max_results)

    analogies = [AnalogyItem(word=w, score=round(s, 4))
                 for w, s in (result.analogies or [])]

    return AnalogyResponse(
        a=req.a,
        b=req.b,
        c=req.c,
        analogies=analogies,
        description=f"{req.a}:{req.b} :: {req.c}:?",
        duration_ms=round((time.time() - t0) * 1000, 1),
    )


@router.post("/introspect", response_model=IntrospectResponse)
async def introspect(req: IntrospectRequest) -> IntrospectResponse:
    """
    Самомоделирование — система оценивает свою компетентность.

    Предсказывает уверенность ответа ДО его генерации.
    Показывает покрытие запроса, плотность рёбер, пробелы.
    """
    t0 = time.time()
    cog = _get_cognition()
    result = cog.introspect(req.query)

    intro = result.introspection or {}

    return IntrospectResponse(
        query=req.query,
        predicted_confidence=intro.get("predicted_confidence", 0.0),
        coverage=intro.get("coverage", 0.0),
        edge_density=intro.get("edge_density", 0.0),
        known_words=intro.get("known_words", []),
        unknown_words=intro.get("unknown_words", []),
        duration_ms=round((time.time() - t0) * 1000, 1),
    )


@router.post("/enhanced", response_model=EnhancedResponse)
async def enhanced_answer(req: EnhancedRequest) -> EnhancedResponse:
    """
    Комбинированный когнитивный ответ.

    Объединяет:
    1. Самомоделирование (предсказание компетентности)
    2. 1-хоповый ответ (прямые связи)
    3. 2-хоповый ответ (абстрактное обобщение)
    4. Каузальную цепочку (причины)
    """
    t0 = time.time()
    cog = _get_cognition()
    result = cog.enhanced_answer(req.query)

    causal_chain = [CausalChainItem(word=w, score=round(s, 4))
                    for w, s in result.get("causal_chain", [])]

    return EnhancedResponse(
        query=req.query,
        answer_1hop=result.get("answer_1hop", ""),
        answer_2hop=result.get("answer_2hop", ""),
        confidence_1hop=result.get("confidence_1hop", 0.0),
        confidence_2hop=result.get("confidence_2hop", 0.0),
        causal_chain=causal_chain,
        self_model=result.get("self_model", {}),
        duration_ms=round((time.time() - t0) * 1000, 1),
    )
