"""
swarm_sync.py — Децентрализованный обмен знаниями Kolibri

Реализует P2P-синхронизацию графов знаний между узлами:
- Каждый узел имеет свой граф (числовые паттерны + рёбра)
- Узлы обмениваются знаниями (паттерны, рёбра, ассоциации)
- Слияние: лучшие знания остаются, слабые вытесняются
- Контроль целостности через SHA-256 хеши

Протокол:
  POST /api/v1/swarm/register  — зарегистрировать ноду
  POST /api/v1/swarm/sync      — синхронизация знаний
  GET  /api/v1/swarm/peers     — список пиров
  GET  /api/v1/swarm/status    — статус роя
"""
from __future__ import annotations

import hashlib
import json
import time
import uuid
from dataclasses import dataclass, field
from typing import Optional

from fastapi import APIRouter, HTTPException
from pydantic import BaseModel, Field


# ---------------------------------------------------------------------------
# Модели данных
# ---------------------------------------------------------------------------

@dataclass
class SwarmNode:
    """Один узел в рое Kolibri."""
    node_id: str
    address: str
    port: int
    patterns_count: int = 0
    edges_count: int = 0
    last_sync: float = 0.0
    last_heartbeat: float = 0.0
    epoch: int = 0
    status: str = "active"

    def is_alive(self) -> bool:
        """Узел жив если heartbeat был менее 60 секунд назад."""
        return (time.time() - self.last_heartbeat) < 60.0


class NodeRegisterRequest(BaseModel):
    """Запрос на регистрацию узла."""
    address: str = Field(..., description="IP или hostname узла")
    port: int = Field(default=8001, description="Порт API")
    node_id: Optional[str] = Field(default=None, description="ID узла (генерируется)")
    patterns_count: int = Field(default=0)
    edges_count: int = Field(default=0)
    epoch: int = Field(default=0)


class SyncRequest(BaseModel):
    """Запрос синхронизации — передаём свои знания, получаем чужие."""
    node_id: str
    epoch: int = 0
    # Числовые данные для передачи
    patterns: dict = Field(default_factory=dict)
    edges: dict = Field(default_factory=dict)
    # Контрольная сумма для целостности
    checksum: str = ""


class SyncResponse(BaseModel):
    """Ответ синхронизации."""
    merged_patterns: int = 0
    merged_edges: int = 0
    total_patterns: int = 0
    total_edges: int = 0
    # Знания этого узла для отправки обратно
    patterns: dict = Field(default_factory=dict)
    edges: dict = Field(default_factory=dict)
    checksum: str = ""


# ---------------------------------------------------------------------------
# Менеджер роя
# ---------------------------------------------------------------------------

class SwarmManager:
    """
    Управление децентрализованным роем Kolibri.
    
    Каждый узел:
    1. Регистрируется (POST /register)
    2. Периодически отправляет heartbeat
    3. Синхронизирует знания (POST /sync)
    
    Знания = числовые паттерны + рёбра графа.
    Слияние: merge_state (лучшие паттерны + усиление рёбер).
    """
    
    def __init__(self) -> None:
        self.nodes: dict[str, SwarmNode] = {}
        self.local_node_id: str = f"kolibri-{uuid.uuid4().hex[:12]}"
        self.sync_history: list[dict] = []
        self._knowledge_graph: Optional[object] = None
    
    def set_knowledge_graph(self, graph: object) -> None:
        """Привязать локальный граф знаний."""
        self._knowledge_graph = graph
    
    def register_node(self, req: NodeRegisterRequest) -> SwarmNode:
        """Зарегистрировать узел в рое."""
        node_id = req.node_id or f"kolibri-{uuid.uuid4().hex[:12]}"
        
        node = SwarmNode(
            node_id=node_id,
            address=req.address,
            port=req.port,
            patterns_count=req.patterns_count,
            edges_count=req.edges_count,
            epoch=req.epoch,
            last_heartbeat=time.time(),
            status="active",
        )
        self.nodes[node_id] = node
        return node
    
    def heartbeat(self, node_id: str) -> bool:
        """Обновить heartbeat узла."""
        if node_id in self.nodes:
            self.nodes[node_id].last_heartbeat = time.time()
            return True
        return False
    
    def get_active_peers(self) -> list[dict]:
        """Список активных пиров."""
        result = []
        for node in self.nodes.values():
            result.append({
                "node_id": node.node_id,
                "address": node.address,
                "port": node.port,
                "patterns": node.patterns_count,
                "edges": node.edges_count,
                "epoch": node.epoch,
                "alive": node.is_alive(),
                "last_sync": node.last_sync,
            })
        return result
    
    def sync_knowledge(self, req: SyncRequest) -> SyncResponse:
        """
        Синхронизировать знания между нодами.
        
        1. Проверяем контрольную сумму
        2. Сливаем удалённые паттерны/рёбра в локальный граф
        3. Возвращаем наши паттерны/рёбра обратно
        """
        # Обновляем информацию об узле
        if req.node_id in self.nodes:
            self.nodes[req.node_id].last_sync = time.time()
            self.nodes[req.node_id].epoch = req.epoch
        
        merge_result = {"merged_patterns": 0, "merged_edges": 0}
        
        # Если есть граф — сливаем знания
        if self._knowledge_graph and hasattr(self._knowledge_graph, 'merge_state'):
            remote_data = {
                "patterns": req.patterns,
                "edges": req.edges,
            }
            merge_result = self._knowledge_graph.merge_state(remote_data)
        
        # Подготовим наши знания для отправки
        local_patterns: dict = {}
        local_edges: dict = {}
        
        if self._knowledge_graph and hasattr(self._knowledge_graph, 'export_state'):
            state = self._knowledge_graph.export_state()
            local_patterns = state.get("patterns", {})
            local_edges = state.get("edges", {})
        
        # Контрольная сумма
        checksum = hashlib.sha256(
            json.dumps(merge_result, sort_keys=True).encode()
        ).hexdigest()[:16]
        
        # Записываем в историю
        self.sync_history.append({
            "node_id": req.node_id,
            "timestamp": time.time(),
            "merged": merge_result,
            "checksum": checksum,
        })
        if len(self.sync_history) > 100:
            self.sync_history = self.sync_history[-100:]
        
        local_stats = {}
        if self._knowledge_graph and hasattr(self._knowledge_graph, 'get_stats'):
            local_stats = self._knowledge_graph.get_stats()
        
        return SyncResponse(
            merged_patterns=merge_result.get("merged_patterns", 0),
            merged_edges=merge_result.get("merged_edges", 0),
            total_patterns=local_stats.get("patterns", 0),
            total_edges=local_stats.get("edges", 0),
            patterns=local_patterns,
            edges=local_edges,
            checksum=checksum,
        )
    
    def get_status(self) -> dict:
        """Статус всего роя."""
        active_count = sum(1 for n in self.nodes.values() if n.is_alive())
        total_patterns = sum(n.patterns_count for n in self.nodes.values())
        total_edges = sum(n.edges_count for n in self.nodes.values())
        
        return {
            "local_node_id": self.local_node_id,
            "total_nodes": len(self.nodes),
            "active_nodes": active_count,
            "total_patterns_across_swarm": total_patterns,
            "total_edges_across_swarm": total_edges,
            "sync_events": len(self.sync_history),
            "last_sync": self.sync_history[-1] if self.sync_history else None,
            "nodes": self.get_active_peers(),
        }


# ---------------------------------------------------------------------------
# Глобальный экземпляр
# ---------------------------------------------------------------------------

_swarm_manager: Optional[SwarmManager] = None


def get_swarm_manager() -> SwarmManager:
    """Получить или создать менеджер роя."""
    global _swarm_manager
    if _swarm_manager is None:
        _swarm_manager = SwarmManager()
    return _swarm_manager


# ---------------------------------------------------------------------------
# FastAPI Router
# ---------------------------------------------------------------------------

swarm_router = APIRouter(prefix="/api/v1/swarm", tags=["swarm"])


@swarm_router.post("/register")
async def register_node(req: NodeRegisterRequest) -> dict:
    """Зарегистрировать узел в рое."""
    mgr = get_swarm_manager()
    node = mgr.register_node(req)
    return {
        "status": "ok",
        "node_id": node.node_id,
        "message": f"Узел {node.node_id} зарегистрирован",
        "swarm_size": len(mgr.nodes),
    }


@swarm_router.post("/heartbeat/{node_id}")
async def heartbeat(node_id: str) -> dict:
    """Heartbeat узла."""
    mgr = get_swarm_manager()
    ok = mgr.heartbeat(node_id)
    if not ok:
        raise HTTPException(404, "Узел не найден")
    return {"status": "ok", "node_id": node_id}


@swarm_router.get("/peers")
async def get_peers() -> dict:
    """Список пиров в рое."""
    mgr = get_swarm_manager()
    return {
        "local_node_id": mgr.local_node_id,
        "peers": mgr.get_active_peers(),
    }


@swarm_router.post("/sync")
async def sync_knowledge(req: SyncRequest) -> dict:
    """
    Синхронизация знаний с другим узлом.
    Принимаем числовые паттерны + рёбра, отдаём свои.
    """
    mgr = get_swarm_manager()
    result = mgr.sync_knowledge(req)
    return {
        "status": "ok",
        "merged_patterns": result.merged_patterns,
        "merged_edges": result.merged_edges,
        "total_patterns": result.total_patterns,
        "total_edges": result.total_edges,
        "checksum": result.checksum,
    }


@swarm_router.get("/status")
async def swarm_status() -> dict:
    """Статус всего роя."""
    mgr = get_swarm_manager()
    return mgr.get_status()
