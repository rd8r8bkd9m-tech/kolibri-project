from __future__ import annotations

from typing import Any

from fastapi import APIRouter
from pydantic import BaseModel, Field

from .concept_runtime import ConceptRunConfig, run_concept_cycle

router = APIRouter(prefix="/api/v1/concept", tags=["concept"])


class ConceptRunRequest(BaseModel):
    query: str = Field(min_length=1, max_length=4096)
    corpus: list[str] = Field(default_factory=list, max_length=64)
    peer_count: int = Field(default=3, ge=1, le=8)
    swarm_rounds: int = Field(default=1, ge=0, le=5)
    formula_generations: int = Field(default=6, ge=1, le=64)
    cognition_depth: int = Field(default=2, ge=1, le=5)
    seed: int = Field(default=20260309, ge=0, le=2**31 - 1)


class ConceptRunResponse(BaseModel):
    query: str
    intent: dict[str, Any]
    decimal_layer: dict[str, Any]
    knowledge_layer: dict[str, Any]
    formula_layer: dict[str, Any]
    cognition_layer: dict[str, Any]
    genome_layer: dict[str, Any]
    swarm_layer: dict[str, Any]
    human_response: str


@router.post("/run", response_model=ConceptRunResponse)
async def run_concept(req: ConceptRunRequest) -> ConceptRunResponse:
    result = run_concept_cycle(
        ConceptRunConfig(
            query=req.query,
            corpus=req.corpus,
            peer_count=req.peer_count,
            swarm_rounds=req.swarm_rounds,
            formula_generations=req.formula_generations,
            cognition_depth=req.cognition_depth,
            seed=req.seed,
        )
    )
    return ConceptRunResponse(**result)
