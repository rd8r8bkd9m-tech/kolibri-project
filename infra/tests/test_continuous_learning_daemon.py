from __future__ import annotations

import json
import time
from types import SimpleNamespace

from backend.service.continuous_learning_daemon import ContinuousLearningDaemon


def test_swarm_sync_skips_during_startup_grace(tmp_path):
    daemon = ContinuousLearningDaemon()
    swarm_dir = tmp_path / "swarm"
    swarm_dir.mkdir(parents=True)
    (swarm_dir / "nodes.json").write_text(
        json.dumps(
            {
                "nodes": [
                    {"name": "node_a", "port": 8001, "peers": ["node_b"]},
                    {"name": "node_b", "port": 8002, "peers": ["node_a"]},
                ]
            }
        ),
        encoding="utf-8",
    )
    daemon._project_root = tmp_path
    daemon._daemon_started_at = time.time()
    daemon._swarm_sync_startup_grace_sec = 300

    result = daemon._task_swarm_sync()

    assert result["status"] == "skipped"
    assert result["reason"] == "startup_grace"
    assert result["grace_remaining_sec"] > 0


def test_dialogue_learning_falls_back_to_add_edge(monkeypatch):
    daemon = ContinuousLearningDaemon()
    daemon._dialogue_buffer = [
        {"user": "Колибри использует Python", "assistant": "Да, backend написан на Python."}
    ]
    calls: list[tuple[str, str]] = []
    monkeypatch.setattr(
        "backend.service.continuous_learning_daemon.extract_facts_from_text",
        lambda _text: [{"subject": "kolibri", "predicate": "python"}],
    )
    daemon._graph = SimpleNamespace(
        add_edge=lambda subject, predicate: calls.append((subject, predicate))
    )

    result = daemon._task_dialogue_learning()

    assert result["status"] == "ok"
    assert result["facts_extracted"] >= 1
    assert calls
