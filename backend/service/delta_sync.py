"""
Дельта-синхронизация между нодами Kolibri.

Протокол:
  1. Нода A → Нода B: «дай дельту с версии X»
  2. Нода B: извлекает все записи с version > X
  3. Нода B → Нода A: пакет KlmDeltaPacket (только изменённые)
  4. Нода A: применяет дельту с разрешением конфликтов по version

Это заменяет полную пересылку графа, снижая трафик на 95%+.
"""
from __future__ import annotations

import asyncio
import os
import json
import time
from dataclasses import dataclass, field
from typing import Optional

import aiohttp
from fastapi import APIRouter, Depends, HTTPException, Request
from pydantic import BaseModel, Field

from .ai_engine import get_engine
from .swarm_security import require_swarm_token

router = APIRouter(prefix="/api/v1/sync", tags=["sync"])


# ============================================================
# Дельта-пакет (Python эквивалент KlmDeltaPacket)
# ============================================================

@dataclass
class PatternDelta:
    """Изменение паттерна."""
    word: str
    hash: int
    pattern: list[int]
    fitness: float
    frequency: int
    version: int


@dataclass
class EdgeDelta:
    """Изменение ребра."""
    source_hash: int
    target_hash: int
    weight: float
    cooccurrence: int
    version: int


@dataclass
class DeltaPacket:
    """Пакет дельта-синхронизации."""
    from_version: int
    to_version: int
    node_id: str
    patterns: list[PatternDelta] = field(default_factory=list)
    edges: list[EdgeDelta] = field(default_factory=list)
    timestamp: float = field(default_factory=time.time)

    def to_json(self) -> str:
        return json.dumps({
            "from_version": self.from_version,
            "to_version": self.to_version,
            "node_id": self.node_id,
            "timestamp": self.timestamp,
            "patterns": [
                {
                    "word": p.word, "hash": p.hash, "pattern": p.pattern,
                    "fitness": p.fitness, "frequency": p.frequency,
                    "version": p.version,
                }
                for p in self.patterns
            ],
            "edges": [
                {
                    "source_hash": e.source_hash, "target_hash": e.target_hash,
                    "weight": e.weight, "cooccurrence": e.cooccurrence,
                    "version": e.version,
                }
                for e in self.edges
            ],
        })

    @staticmethod
    def from_json(data: str) -> DeltaPacket:
        d = json.loads(data)
        return DeltaPacket(
            from_version=d["from_version"],
            to_version=d["to_version"],
            node_id=d["node_id"],
            timestamp=d.get("timestamp", 0),
            patterns=[
                PatternDelta(**p) for p in d.get("patterns", [])
            ],
            edges=[
                EdgeDelta(**e) for e in d.get("edges", [])
            ],
        )

    @property
    def size_bytes(self) -> int:
        """Примерный размер пакета в байтах."""
        return len(self.patterns) * 200 + len(self.edges) * 40

    @property
    def is_empty(self) -> bool:
        return len(self.patterns) == 0 and len(self.edges) == 0


# ============================================================
# Преобразования форматов (graph delta <-> DeltaPacket)
# ============================================================

def _graph_delta_to_packet(graph_delta: dict, node_id: str) -> tuple[DeltaPacket, dict]:
    """
    Превратить KnowledgeGraph.export_delta() в DeltaPacket (+ meta поля).

    KnowledgeGraph.export_delta() возвращает patterns/edges как dict:
      patterns[str(hash)] = {word, pattern, frequency, fitness, version}
      edges["a:b"] = {weight, cooccurrence, version}
    """
    meta = {
        "needs_full_sync": bool(graph_delta.get("needs_full_sync", False)),
        "truncated": bool(graph_delta.get("truncated", False)),
    }
    if "oldest_available_version" in graph_delta:
        meta["oldest_available_version"] = int(graph_delta["oldest_available_version"])

    from_v = int(graph_delta.get("from_version", 0))
    to_v = int(graph_delta.get("to_version", 0))

    if meta["needs_full_sync"]:
        # Пир слишком отстал: дельта уже не хранится в логах.
        return (
            DeltaPacket(
                from_version=from_v,
                to_version=to_v,
                node_id=node_id,
                patterns=[],
                edges=[],
                timestamp=time.time(),
            ),
            meta,
        )

    patterns: list[PatternDelta] = []
    for h_str, pdata in (graph_delta.get("patterns") or {}).items():
        try:
            h = int(h_str)
        except Exception:
            continue
        patterns.append(
            PatternDelta(
                word=str(pdata.get("word", "")),
                hash=h,
                pattern=list(pdata.get("pattern", [])),
                fitness=float(pdata.get("fitness", 0.0)),
                frequency=int(pdata.get("frequency", 0)),
                version=int(pdata.get("version", 0)),
            )
        )

    edges: list[EdgeDelta] = []
    for key_str, edata in (graph_delta.get("edges") or {}).items():
        parts = str(key_str).split(":")
        if len(parts) != 2:
            continue
        try:
            a = int(parts[0])
            b = int(parts[1])
        except Exception:
            continue
        edges.append(
            EdgeDelta(
                source_hash=min(a, b),
                target_hash=max(a, b),
                weight=float(edata.get("weight", 0.0)),
                cooccurrence=int(edata.get("cooccurrence", 0)),
                version=int(edata.get("version", 0)),
            )
        )

    return (
        DeltaPacket(
            from_version=from_v,
            to_version=to_v,
            node_id=node_id,
            patterns=patterns,
            edges=edges,
            timestamp=time.time(),
        ),
        meta,
    )


def _packet_to_merge_state(delta: DeltaPacket) -> dict:
    """DeltaPacket -> формат для KnowledgeGraph.merge_state()."""
    patterns: dict[str, dict] = {}
    for p in delta.patterns:
        patterns[str(int(p.hash))] = {
            "word": p.word,
            "pattern": list(p.pattern),
            "frequency": int(p.frequency),
            "fitness": float(p.fitness),
            "version": int(p.version),
        }

    edges: dict[str, dict] = {}
    for e in delta.edges:
        a = int(e.source_hash)
        b = int(e.target_hash)
        key = f"{min(a, b)}:{max(a, b)}"
        edges[key] = {
            "weight": float(e.weight),
            "cooccurrence": int(e.cooccurrence),
            "version": int(e.version),
        }

    return {"patterns": patterns, "edges": edges}


# ============================================================
# DeltaSyncManager — управление дельта-синхронизацией
# ============================================================

class DeltaSyncManager:
    """Менеджер дельта-синхронизации между нодами."""

    def __init__(self, node_id: str, sync_interval: float = 5.0) -> None:
        self.node_id = node_id
        self.sync_interval = sync_interval
        self._peer_versions: dict[str, int] = {}  # peer_id → last_known_version
        self._peers: dict[str, str] = {}  # peer_id → url
        self._running = False
        self._stats = {
            "deltas_sent": 0,
            "deltas_received": 0,
            "bytes_sent": 0,
            "bytes_received": 0,
            "conflicts_resolved": 0,
        }
        self._lock = asyncio.Lock()

    def register_peer(self, peer_id: str, url: str) -> None:
        """Зарегистрировать ноду-соседа."""
        self._peers[peer_id] = url
        if peer_id not in self._peer_versions:
            self._peer_versions[peer_id] = 0

    def unregister_peer(self, peer_id: str) -> None:
        """Убрать ноду из списка."""
        self._peers.pop(peer_id, None)
        self._peer_versions.pop(peer_id, None)

    def extract_delta(self, since_version: int, max_items: int = 10000) -> tuple[DeltaPacket, dict]:
        """Извлечь дельту от since_version из локального KnowledgeGraph."""
        engine = get_engine()
        graph_delta = engine.graph.export_delta(since_version, max_items=max_items)
        return _graph_delta_to_packet(graph_delta, node_id=self.node_id)

    async def push_delta(self, peer_id: str, delta: DeltaPacket) -> bool:
        """Отправить дельту конкретному пиру."""
        url = self._peers.get(peer_id)
        if not url:
            return False

        payload = delta.to_json()
        headers = {"Content-Type": "application/json"}
        token = os.getenv("KOLIBRI_SWARM_TOKEN", "").strip()
        if token:
            headers["X-Kolibri-Swarm-Token"] = token
        try:
            async with aiohttp.ClientSession() as session:
                async with session.post(
                    f"{url}/api/v1/sync/apply",
                    json={"data": payload},
                    headers=headers,
                    timeout=aiohttp.ClientTimeout(total=10),
                ) as resp:
                    if resp.status == 200:
                        self._stats["deltas_sent"] += 1
                        self._stats["bytes_sent"] += delta.size_bytes
                        self._peer_versions[peer_id] = delta.to_version
                        return True
        except Exception:
            pass
        return False

    async def pull_delta(self, peer_id: str) -> Optional[DeltaPacket]:
        """Запросить дельту у пира."""
        url = self._peers.get(peer_id)
        if not url:
            return None

        since = self._peer_versions.get(peer_id, 0)
        headers: dict[str, str] = {}
        token = os.getenv("KOLIBRI_SWARM_TOKEN", "").strip()
        if token:
            headers["X-Kolibri-Swarm-Token"] = token
        try:
            async with aiohttp.ClientSession() as session:
                async with session.get(
                    f"{url}/api/v1/sync/delta",
                    params={"since_version": since},
                    headers=headers or None,
                    timeout=aiohttp.ClientTimeout(total=10),
                ) as resp:
                    if resp.status == 200:
                        data = await resp.text()
                        delta = DeltaPacket.from_json(data)
                        self._stats["deltas_received"] += 1
                        self._stats["bytes_received"] += delta.size_bytes
                        self._peer_versions[peer_id] = delta.to_version
                        return delta
        except Exception:
            pass
        return None

    async def sync_all(self) -> dict:
        """Синхронизировать со всеми пирами."""
        results: dict[str, str] = {}
        for peer_id in list(self._peers):
            delta = await self.pull_delta(peer_id)
            if delta and not delta.is_empty:
                engine = get_engine()
                merge_result = engine.graph.merge_state(_packet_to_merge_state(delta))
                results[peer_id] = (
                    f"applied {len(delta.patterns)} patterns, {len(delta.edges)} edges "
                    f"(merged_patterns={merge_result.get('merged_patterns', 0)}, "
                    f"merged_edges={merge_result.get('merged_edges', 0)})"
                )
            else:
                results[peer_id] = "up-to-date"

        return results

    async def run_sync_loop(self) -> None:
        """Фоновый цикл синхронизации."""
        self._running = True
        while self._running:
            await asyncio.sleep(self.sync_interval)
            await self.sync_all()

    def stop(self) -> None:
        self._running = False

    @property
    def stats(self) -> dict:
        engine = get_engine()
        graph_v = engine.graph.version
        return {
            **self._stats,
            "global_version": graph_v,
            "peers": len(self._peers),
            "peer_versions": dict(self._peer_versions),
        }


# ============================================================
# Глобальный экземпляр
# ============================================================

_sync_manager: Optional[DeltaSyncManager] = None


def get_sync_manager() -> DeltaSyncManager:
    global _sync_manager
    if _sync_manager is None:
        import uuid
        _sync_manager = DeltaSyncManager(node_id=str(uuid.uuid4())[:8])
    return _sync_manager


# ============================================================
# Pydantic модели
# ============================================================

class RegisterPeerRequest(BaseModel):
    peer_id: str
    url: str


class DeltaApplyRequest(BaseModel):
    data: object = Field(description="DeltaPacket JSON (string/dict) или raw payload")


class SyncStatusResponse(BaseModel):
    enabled: bool = True
    detail: str | None = None
    node_id: str
    global_version: int
    peers: int
    deltas_sent: int
    deltas_received: int
    bytes_sent: int
    bytes_received: int


# ============================================================
# API эндпоинты
# ============================================================

@router.post("/peer/register")
async def register_peer(req: RegisterPeerRequest, _auth: None = Depends(require_swarm_token)) -> dict:
    """Зарегистрировать ноду-соседа для синхронизации."""
    mgr = get_sync_manager()
    mgr.register_peer(req.peer_id, req.url)
    return {"status": "registered", "peer_id": req.peer_id}


@router.delete("/peer/{peer_id}")
async def unregister_peer(peer_id: str, _auth: None = Depends(require_swarm_token)) -> dict:
    """Убрать ноду из синхронизации."""
    mgr = get_sync_manager()
    mgr.unregister_peer(peer_id)
    return {"status": "unregistered"}


@router.get("/delta")
async def get_delta(
    since_version: int = 0,
    max_items: int = 10000,
    _auth: None = Depends(require_swarm_token),
) -> dict:
    """Получить дельту с указанной версии."""
    mgr = get_sync_manager()
    delta, meta = mgr.extract_delta(since_version, max_items=max_items)
    payload = json.loads(delta.to_json())
    payload.update(meta)
    return payload


@router.post("/apply")
async def apply_delta(request: Request, _auth: None = Depends(require_swarm_token)) -> dict:
    """Применить входящую дельту от другой ноды."""
    try:
        body = await request.json()
    except Exception:
        raise HTTPException(400, "Ожидается JSON body")

    data_obj: object = body.get("data", body) if isinstance(body, dict) else body

    # 1) data может быть JSON-строкой (DeltaPacket.to_json())
    if isinstance(data_obj, str):
        try:
            parsed = json.loads(data_obj)
        except Exception as e:
            raise HTTPException(400, f"Некорректный JSON в поле data: {e}")
    # 2) data может быть уже dict payload
    elif isinstance(data_obj, dict):
        parsed = data_obj
    else:
        raise HTTPException(400, "Некорректный формат: data должен быть string или object")

    engine = get_engine()

    # DeltaPacket формат (patterns/edges как списки)
    if isinstance(parsed.get("patterns"), list) and isinstance(parsed.get("edges"), list):
        try:
            delta = DeltaPacket.from_json(json.dumps(parsed))
        except Exception as e:
            raise HTTPException(400, f"Некорректный DeltaPacket: {e}")
        merge_result = engine.graph.merge_state(_packet_to_merge_state(delta))
        return {
            "status": "applied",
            "from_node": delta.node_id,
            "delta_patterns": len(delta.patterns),
            "delta_edges": len(delta.edges),
            **merge_result,
            "graph_version": engine.graph.version,
        }

    # KnowledgeGraph экспорт (patterns/edges как dict) — принимаем напрямую
    if isinstance(parsed.get("patterns"), dict) and isinstance(parsed.get("edges"), dict):
        remote_state = {"patterns": parsed.get("patterns", {}), "edges": parsed.get("edges", {})}
        merge_result = engine.graph.merge_state(remote_state)
        return {
            "status": "applied",
            "from_node": str(parsed.get("node_id", "")),
            "delta_patterns": len(remote_state["patterns"]),
            "delta_edges": len(remote_state["edges"]),
            **merge_result,
            "graph_version": engine.graph.version,
        }

    raise HTTPException(400, "Неизвестный формат дельты: ожидается DeltaPacket или граф-экспорт")


@router.post("/sync/all")
async def sync_with_all(_auth: None = Depends(require_swarm_token)) -> dict:
    """Синхронизировать со всеми зарегистрированными пирами."""
    mgr = get_sync_manager()
    results = await mgr.sync_all()
    return {"status": "synced", "peers": results}


@router.get("/status", response_model=SyncStatusResponse)
async def sync_status() -> SyncStatusResponse:
    """Статус синхронизации."""
    mgr = get_sync_manager()
    s = mgr.stats
    token_configured = bool(os.getenv("KOLIBRI_SWARM_TOKEN", "").strip())
    return SyncStatusResponse(
        enabled=token_configured,
        detail=None if token_configured else "KOLIBRI_SWARM_TOKEN is not configured",
        node_id=mgr.node_id,
        global_version=s["global_version"],
        peers=s["peers"],
        deltas_sent=s["deltas_sent"],
        deltas_received=s["deltas_received"],
        bytes_sent=s["bytes_sent"],
        bytes_received=s["bytes_received"],
    )
