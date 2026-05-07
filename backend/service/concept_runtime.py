from __future__ import annotations

import random
from dataclasses import dataclass
from typing import Any

from core.kolibri_sim import (
    KolibriSim,
    preobrazovat_tekst_v_cifry,
    vosstanovit_tekst_iz_cifr,
)

from .cognition import SwarmCognition
from .number_mind import (
    FormulaPool,
    KnowledgeGraph,
    _tokenize,
    djb2_hash,
    fnv1a_hash,
    pattern_to_str,
    text_to_digits,
    word_to_pattern,
)


@dataclass
class ConceptRunConfig:
    query: str
    corpus: list[str]
    peer_count: int = 3
    swarm_rounds: int = 1
    formula_generations: int = 6
    cognition_depth: int = 2
    seed: int = 20260309


def _significant_tokens(text: str) -> list[str]:
    return [token for token in _tokenize(text) if len(token) >= 3]


def _build_intent(query: str, tokens: list[str], peer_count: int) -> dict[str, Any]:
    return {
        "id": f"intent-{djb2_hash(query):08x}",
        "source": "agent://concept/runtime",
        "targets": ["topic://knowledge", "topic://formula", "topic://swarm"],
        "scope": ["read", "teach", "query", "sync"],
        "op": "Query",
        "priority": min(10, max(1, len(tokens))),
        "ttl": 60_000,
        "payload": {
            "text": query,
            "token_count": len(tokens),
            "peer_count": peer_count,
        },
    }


def _seed_formula_pool(graph: KnowledgeGraph, pool: FormulaPool, max_pairs: int = 256) -> int:
    seeded = 0
    ranked_edges = sorted(
        graph.edges.items(),
        key=lambda item: item[1].weight,
        reverse=True,
    )
    for (src_hash, tgt_hash), _edge in ranked_edges[:max_pairs]:
        src = graph.patterns.get(src_hash)
        tgt = graph.patterns.get(tgt_hash)
        if src is None or tgt is None:
            continue
        pool.add_semantic_pair(src.pattern, tgt.pattern)
        pool.add_semantic_pair(tgt.pattern, src.pattern)
        seeded += 2
    return seeded


def _compose_human_response(
    query: str,
    graph_answer: str,
    multi_hop_answer: str,
    generated_words: list[tuple[str, float]],
) -> str:
    pieces: list[str] = []
    if graph_answer:
        pieces.append(graph_answer)
    if multi_hop_answer and multi_hop_answer != graph_answer:
        pieces.append(multi_hop_answer)

    formula_words: list[str] = []
    seen: set[str] = set()
    for word, _score in generated_words:
        if word not in seen:
            seen.add(word)
            formula_words.append(word)
        if len(formula_words) >= 6:
            break

    if formula_words:
        pieces.append("формульные ассоциации: " + ", ".join(formula_words))

    if not pieces:
        return f"Kolibri обработал запрос «{query}», но знаний пока недостаточно."

    return ". ".join(piece for piece in pieces if piece).strip()


def _teach_sim_from_graph(sim: KolibriSim, graph: KnowledgeGraph, tokens: list[str]) -> int:
    taught = 0
    for token in tokens[:6]:
        answer, _confidence, _meta = graph.answer(token, max_words=4)
        if answer:
            sim.obuchit_svjaz(token, answer)
            taught += 1
    return taught


def run_concept_cycle(config: ConceptRunConfig) -> dict[str, Any]:
    state = random.getstate()
    random.seed(config.seed)
    try:
        corpus = [chunk.strip() for chunk in config.corpus if chunk.strip()]
        if not corpus:
            corpus = [config.query]

        graph = KnowledgeGraph()
        for chunk in corpus:
            graph.train_text(chunk)

        tokens = _significant_tokens(config.query)
        token_views = []
        for token in tokens:
            token_views.append(
                {
                    "word": token,
                    "hash_djb2": djb2_hash(token),
                    "hash_fnv1a": fnv1a_hash(token),
                    "pattern": pattern_to_str(word_to_pattern(token)),
                    "similar": [
                        {"word": word, "score": score, "method": method}
                        for word, score, method in graph.find_similar_semantic(token, limit=5)
                    ],
                }
            )

        pool = FormulaPool()
        semantic_pairs = _seed_formula_pool(graph, pool)
        best_fitness = pool.evolve(generations=config.formula_generations)
        best_formula = max(pool.formulas, key=lambda formula: formula.fitness)

        graph_answer, graph_confidence, graph_meta = graph.answer(config.query, max_words=8)
        multi_hop_answer, multi_hop_confidence, multi_hop_meta = graph.multi_hop_answer(
            config.query,
            max_hops=max(1, config.cognition_depth),
            max_words=8,
        )
        generated_words = graph.generate_words(config.query, best_formula, max_words=8)

        cognition = SwarmCognition(graph)
        causal_index = cognition.learn_causality(corpus, window=5)
        abstract = cognition.abstract(config.query, depth=config.cognition_depth)
        why = cognition.why(config.query, max_chain=3)
        then = cognition.then(config.query, max_chain=3)
        induction = cognition.induce(min_support=1, min_confidence=0.1)
        introspection = cognition.introspect(config.query)
        enhanced = cognition.enhanced_answer(config.query)

        human_response = _compose_human_response(
            config.query,
            graph_answer,
            multi_hop_answer,
            generated_words,
        )

        best_formula.add_association(config.query, human_response)

        sim = KolibriSim(zerno=config.seed)
        taught_main = _teach_sim_from_graph(sim, graph, tokens)
        sim.obuchit_svjaz(config.query, human_response)
        formula_name = sim.evolyuciya_formul("concept-runtime")
        sim.ocenit_formulu(formula_name, best_formula.fitness)

        peers: list[KolibriSim] = []
        for index in range(max(0, config.peer_count - 1)):
            peer = KolibriSim(zerno=config.seed + index + 1)
            _teach_sim_from_graph(peer, graph, tokens[index:] or tokens)
            peer_formula = peer.evolyuciya_formul(f"peer-{index}")
            peer.ocenit_formulu(peer_formula, best_formula.fitness * 0.8)
            peers.append(peer)
        swarm = sim.zapustit_roj(peers, cikly=config.swarm_rounds)

        digits = text_to_digits(config.query)
        digits_text = preobrazovat_tekst_v_cifry(config.query)
        journal = sim.poluchit_zhurnal()

        return {
            "query": config.query,
            "intent": _build_intent(config.query, tokens, config.peer_count),
            "decimal_layer": {
                "digits": digits,
                "digits_text": digits_text,
                "decoded_text": vosstanovit_tekst_iz_cifr(digits_text),
                "token_count": len(tokens),
                "tokens": token_views,
            },
            "knowledge_layer": {
                "trained_documents": len(corpus),
                "graph_stats": graph.get_stats(),
                "graph_answer": graph_answer,
                "graph_confidence": graph_confidence,
                "graph_meta": graph_meta,
                "multi_hop_answer": multi_hop_answer,
                "multi_hop_confidence": multi_hop_confidence,
                "multi_hop_meta": multi_hop_meta,
            },
            "formula_layer": {
                "semantic_pairs": semantic_pairs,
                "generation": pool.generation,
                "best_fitness": round(best_fitness, 4),
                "associations": len(best_formula.associations),
                "generated_words": [
                    {"word": word, "score": round(score, 4)}
                    for word, score in generated_words
                ],
            },
            "cognition_layer": {
                "causal_pairs": len(causal_index.pairs),
                "abstract": {
                    "answer": abstract.answer,
                    "confidence": abstract.confidence,
                },
                "why": why.chain,
                "then": then.chain,
                "induction_rules": induction.rules[:5],
                "introspection": introspection.introspection,
                "enhanced": enhanced,
            },
            "genome_layer": {
                "valid": sim.proverit_genom(),
                "blocks": len(sim.genom),
                "journal_offset": journal["offset"],
                "journal_size": len(journal["zapisi"]),
                "recent_events": journal["zapisi"][-6:],
                "canvas": sim.poluchit_canvas(),
                "taught_pairs": taught_main + 1,
            },
            "swarm_layer": {
                "peer_count": config.peer_count,
                "rounds": swarm["rounds"],
                "knowledge_imported": swarm["knowledge"],
                "formulas_imported": swarm["formulas"],
            },
            "human_response": human_response,
        }
    finally:
        random.setstate(state)
