from __future__ import annotations

import json
import os
import subprocess
import tempfile
import threading
import time
from pathlib import Path
from typing import Any, Optional

from fastapi import APIRouter, File, Form, HTTPException, UploadFile
from pydantic import BaseModel, Field
from fastapi.responses import FileResponse

from .background_learning import get_background_learning_manager
from .kpack import export_kpack, import_kpack
from .project_paths import get_project_root
from .swarm_live_memory import (
    count_formula_docs,
    diff_formula_domain_counts,
    ensure_live_formula_memory_seeded,
    get_live_formula_memory_dir,
    get_seed_formula_memory_dir,
    ingest_text_document,
    ingest_urls_via_trainer,
    summarize_formula_docs_by_domain,
    summarize_formula_docs_by_domain_map,
)


def _env_flag(name: str, default: bool = False) -> bool:
    value = os.environ.get(name)
    if value is None:
        return default
    return value.strip().lower() in {"1", "true", "yes", "on"}


def _compute_knowledge_refresh_delta(
    before: dict[str, Any] | None,
    after: dict[str, Any] | None,
) -> dict[str, Any] | None:
    if not before or not after:
        return None
    before_single = float(before.get("single", {}).get("hit_ratio", 0.0))
    after_single = float(after.get("single", {}).get("hit_ratio", 0.0))
    before_isolated = float(before.get("isolated_final", {}).get("hit_ratio", 0.0))
    after_isolated = float(after.get("isolated_final", {}).get("hit_ratio", 0.0))
    before_swarm = float(before.get("swarm_final", {}).get("hit_ratio", 0.0))
    after_swarm = float(after.get("swarm_final", {}).get("hit_ratio", 0.0))
    before_cmp_single = float(before.get("comparison", {}).get("swarm_vs_single_delta", 0.0))
    after_cmp_single = float(after.get("comparison", {}).get("swarm_vs_single_delta", 0.0))
    before_cmp_isolated = float(before.get("comparison", {}).get("swarm_vs_isolated_delta", 0.0))
    after_cmp_isolated = float(after.get("comparison", {}).get("swarm_vs_isolated_delta", 0.0))

    def _r(value: float) -> float:
        return round(value, 6)

    return {
        "from_timestamp": float(before.get("timestamp", 0.0)),
        "to_timestamp": float(after.get("timestamp", 0.0)),
        "documents_delta": int(after.get("total_documents", 0)) - int(before.get("total_documents", 0)),
        "single_hit_delta": _r(after_single - before_single),
        "isolated_hit_delta": _r(after_isolated - before_isolated),
        "swarm_hit_delta": _r(after_swarm - before_swarm),
        "swarm_vs_single_delta_change": _r(after_cmp_single - before_cmp_single),
        "swarm_vs_isolated_delta_change": _r(after_cmp_isolated - before_cmp_isolated),
    }


def _domain_score_index(payload: dict[str, Any] | None) -> dict[str, dict[str, Any]]:
    if not payload:
        return {}
    result: dict[str, dict[str, Any]] = {}
    for item in payload.get("domain_scores", []) or []:
        domain = str(item.get("domain", "")).strip()
        if not domain:
            continue
        result[domain] = dict(item)
    return result


def _compute_domain_score_delta(
    before: dict[str, Any] | None,
    after: dict[str, Any] | None,
    *,
    focus_domains: set[str] | None = None,
) -> list[dict[str, Any]]:
    before_index = _domain_score_index(before)
    after_index = _domain_score_index(after)
    domains = sorted(set(before_index) | set(after_index))
    result: list[dict[str, Any]] = []
    for domain in domains:
        if focus_domains and domain not in focus_domains:
            continue
        before_item = before_index.get(domain, {})
        after_item = after_index.get(domain, {})
        before_single = float(before_item.get("single_hit_ratio", 0.0))
        after_single = float(after_item.get("single_hit_ratio", 0.0))
        before_swarm = float(before_item.get("swarm_hit_ratio", 0.0))
        after_swarm = float(after_item.get("swarm_hit_ratio", 0.0))
        before_isolated = float(before_item.get("isolated_hit_ratio", 0.0))
        after_isolated = float(after_item.get("isolated_hit_ratio", 0.0))
        delta = {
            "domain": domain,
            "documents_before": int(before_item.get("documents", 0)),
            "documents_after": int(after_item.get("documents", 0)),
            "documents_delta": int(after_item.get("documents", 0)) - int(before_item.get("documents", 0)),
            "single_hit_delta": round(after_single - before_single, 6),
            "isolated_hit_delta": round(after_isolated - before_isolated, 6),
            "swarm_hit_delta": round(after_swarm - before_swarm, 6),
            "swarm_vs_single_delta_change": round(
                float(after_item.get("swarm_vs_single_delta", 0.0)) - float(before_item.get("swarm_vs_single_delta", 0.0)),
                6,
            ),
            "swarm_vs_isolated_delta_change": round(
                float(after_item.get("swarm_vs_isolated_delta", 0.0)) - float(before_item.get("swarm_vs_isolated_delta", 0.0)),
                6,
            ),
        }
        if any(value != 0 for key, value in delta.items() if key.endswith("_delta") or key.endswith("_change")):
            result.append(delta)
    result.sort(
        key=lambda item: (
            -float(item["swarm_hit_delta"]),
            -float(item["swarm_vs_single_delta_change"]),
            str(item["domain"]),
        )
    )
    return result


def _build_demo_comparison_summary(
    before: dict[str, Any] | None,
    after: dict[str, Any] | None,
    domain_score_delta: list[dict[str, Any]] | None = None,
) -> dict[str, Any] | None:
    if not before or not after:
        return None

    summary: dict[str, Any] = {
        "documents_before": int(before.get("total_documents", 0) or 0),
        "documents_after": int(after.get("total_documents", 0) or 0),
        "documents_delta": int(after.get("total_documents", 0) or 0) - int(before.get("total_documents", 0) or 0),
        "single_hit_before": round(float(before.get("single", {}).get("hit_ratio", 0.0) or 0.0), 6),
        "single_hit_after": round(float(after.get("single", {}).get("hit_ratio", 0.0) or 0.0), 6),
        "isolated_hit_before": round(float(before.get("isolated_final", {}).get("hit_ratio", 0.0) or 0.0), 6),
        "isolated_hit_after": round(float(after.get("isolated_final", {}).get("hit_ratio", 0.0) or 0.0), 6),
        "swarm_hit_before": round(float(before.get("swarm_final", {}).get("hit_ratio", 0.0) or 0.0), 6),
        "swarm_hit_after": round(float(after.get("swarm_final", {}).get("hit_ratio", 0.0) or 0.0), 6),
        "swarm_vs_single_before": round(float(before.get("comparison", {}).get("swarm_vs_single_delta", 0.0) or 0.0), 6),
        "swarm_vs_single_after": round(float(after.get("comparison", {}).get("swarm_vs_single_delta", 0.0) or 0.0), 6),
        "swarm_vs_isolated_before": round(float(before.get("comparison", {}).get("swarm_vs_isolated_delta", 0.0) or 0.0), 6),
        "swarm_vs_isolated_after": round(float(after.get("comparison", {}).get("swarm_vs_isolated_delta", 0.0) or 0.0), 6),
    }

    if domain_score_delta:
        top = domain_score_delta[0]
        summary["focus_domain"] = str(top.get("domain", "") or "").strip()
        summary["focus_domain_documents_delta"] = int(top.get("documents_delta", 0) or 0)
        summary["focus_domain_single_hit_delta"] = round(float(top.get("single_hit_delta", 0.0) or 0.0), 6)
        summary["focus_domain_swarm_hit_delta"] = round(float(top.get("swarm_hit_delta", 0.0) or 0.0), 6)
        summary["focus_domain_advantage_delta"] = round(float(top.get("swarm_vs_single_delta_change", 0.0) or 0.0), 6)

    return summary


class SwarmRuntimeManager:
    def __init__(self) -> None:
        self._last_error = ""
        self._lock = threading.Lock()
        self._refresh_state_lock = threading.Lock()
        self._refresh_exec_lock = threading.Lock()
        self._project_root = get_project_root()
        self._status_path = Path(
            os.environ.get(
                "KOLIBRI_SWARM_STATUS_PATH",
                self._project_root / "data" / "swarm" / "benchmark_status.json",
            )
        )
        self._knowledge_status_path = Path(
            os.environ.get(
                "KOLIBRI_SWARM_KNOWLEDGE_STATUS_PATH",
                self._project_root / "data" / "swarm" / "knowledge_benchmark_status.json",
            )
        )
        self._log_path = Path(
            os.environ.get(
                "KOLIBRI_SWARM_LOG_PATH",
                self._project_root / "data" / "swarm" / "benchmark.log",
            )
        )
        self._knowledge_log_path = Path(
            os.environ.get(
                "KOLIBRI_SWARM_KNOWLEDGE_LOG_PATH",
                self._project_root / "data" / "swarm" / "knowledge_benchmark.log",
            )
        )
        self._pid_path = Path(
            os.environ.get(
                "KOLIBRI_SWARM_PID_PATH",
                self._project_root / "data" / "swarm" / "benchmark.pid",
            )
        )
        self._knowledge_pid_path = Path(
            os.environ.get(
                "KOLIBRI_SWARM_KNOWLEDGE_PID_PATH",
                self._project_root / "data" / "swarm" / "knowledge_benchmark.pid",
            )
        )
        self._latest_demo_path = Path(
            os.environ.get(
                "KOLIBRI_SWARM_LATEST_DEMO_PATH",
                self._project_root / "data" / "swarm" / "latest_demo.json",
            )
        )
        self._interval_sec = max(
            30,
            int(os.environ.get("KOLIBRI_SWARM_RUNTIME_INTERVAL_SEC", "300")),
        )
        self._autostart = _env_flag("KOLIBRI_ENABLE_SWARM_RUNTIME", default=False)
        self._live_memory_dir = get_live_formula_memory_dir()
        self._seed_memory_dir = get_seed_formula_memory_dir()
        self._refresh_thread: threading.Thread | None = None
        self._refresh_running = False
        self._refresh_pending = False
        self._last_refresh_started_at = 0.0
        self._last_refresh_finished_at = 0.0
        self._last_refresh_reason = ""
        self._last_ingest_at = 0.0
        self._last_ingest_kind = ""
        self._last_ingest_domain_delta: list[dict[str, Any]] = []
        self._last_knowledge_refresh_delta: dict[str, Any] | None = None
        self._target_node_count = max(
            3,
            int(os.environ.get("KOLIBRI_SWARM_TARGET_NODE_COUNT", "50")),
        )
        self._anchor_node_count = max(
            1,
            int(os.environ.get("KOLIBRI_SWARM_ANCHOR_NODE_COUNT", "10")),
        )
        self._validator_node_count = max(
            1,
            int(os.environ.get("KOLIBRI_SWARM_VALIDATOR_NODE_COUNT", "10")),
        )

    def _bin_candidates(self) -> list[Path]:
        return [
            self._project_root / "build" / "kolibri_swarm_benchmark",
            self._project_root / "build-logic" / "kolibri_swarm_benchmark",
        ]

    def _knowledge_bin_candidates(self) -> list[Path]:
        return [
            self._project_root / "build" / "kolibri_swarm_knowledge_benchmark",
            self._project_root / "build-logic" / "kolibri_swarm_knowledge_benchmark",
        ]

    def _resolve_bin(self) -> Optional[Path]:
        for candidate in self._bin_candidates():
            if candidate.exists() and os.access(candidate, os.X_OK):
                return candidate
        return None

    def _resolve_knowledge_bin(self) -> Optional[Path]:
        for candidate in self._knowledge_bin_candidates():
            if candidate.exists() and os.access(candidate, os.X_OK):
                return candidate
        return None

    def _ensure_dirs(self) -> None:
        ensure_live_formula_memory_seeded()
        self._status_path.parent.mkdir(parents=True, exist_ok=True)
        self._log_path.parent.mkdir(parents=True, exist_ok=True)
        self._pid_path.parent.mkdir(parents=True, exist_ok=True)
        self._knowledge_status_path.parent.mkdir(parents=True, exist_ok=True)
        self._knowledge_log_path.parent.mkdir(parents=True, exist_ok=True)
        self._knowledge_pid_path.parent.mkdir(parents=True, exist_ok=True)

    def _knowledge_command(self, binary: Path) -> list[str]:
        return [
            str(binary),
            "--json-out",
            str(self._knowledge_status_path),
            "--docs-root",
            str(ensure_live_formula_memory_seeded()),
        ]

    def _read_pid(self) -> Optional[int]:
        try:
            raw = self._pid_path.read_text(encoding="utf-8").strip()
        except OSError:
            return None
        if not raw:
            return None
        try:
            return int(raw)
        except ValueError:
            return None

    def _is_pid_running(self, pid: Optional[int]) -> bool:
        if not pid or pid <= 0:
            return False
        try:
            os.kill(pid, 0)
        except OSError:
            return False
        return True

    def _load_latest(self) -> Optional[dict[str, Any]]:
        try:
            return json.loads(self._status_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            return None

    def _load_latest_knowledge(self) -> Optional[dict[str, Any]]:
        try:
            return json.loads(self._knowledge_status_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            return None

    def _load_latest_demo(self) -> Optional[dict[str, Any]]:
        try:
            return json.loads(self._latest_demo_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            return None

    def record_demo_snapshot(self, payload: dict[str, Any]) -> None:
        self._latest_demo_path.parent.mkdir(parents=True, exist_ok=True)
        self._latest_demo_path.write_text(
            json.dumps(payload, ensure_ascii=False, indent=2),
            encoding="utf-8",
        )

    def _swarm_role_counts(self) -> tuple[int, int, int]:
        total = max(3, int(self._target_node_count))
        anchor = min(max(1, int(self._anchor_node_count)), total - 2)
        validator = min(max(1, int(self._validator_node_count)), total - anchor - 1)
        learner = max(1, total - anchor - validator)
        return anchor, learner, validator

    def _build_swarm_topology(
        self,
        *,
        running: bool,
        knowledge_running: bool,
        latest: dict[str, Any] | None,
        latest_knowledge: dict[str, Any] | None,
        latest_demo: dict[str, Any] | None,
        refresh_state: dict[str, Any],
        background_learning: dict[str, Any],
    ) -> dict[str, Any]:
        anchor_count, learner_count, validator_count = self._swarm_role_counts()
        quorum = (validator_count // 2) + 1
        latest_hit = float((latest_knowledge or {}).get("swarm_final", {}).get("hit_ratio", 0.0) or 0.0)
        latest_advantage = float((latest_knowledge or {}).get("comparison", {}).get("swarm_vs_single_delta", 0.0) or 0.0)
        network_available = bool(background_learning.get("network_available", False))
        validators_active = validator_count if (knowledge_running or latest_knowledge or refresh_state.get("refresh_running")) else 0
        learners_active = learner_count if (
            background_learning.get("enabled")
            or background_learning.get("running")
            or self._last_ingest_at > 0.0
            or latest_demo
            or refresh_state.get("refresh_running")
        ) else 0
        anchors_active = anchor_count if (running or knowledge_running or latest or latest_knowledge or self._last_ingest_at > 0.0) else 0
        active_count = anchors_active + learners_active + validators_active
        healthy_count = active_count
        consensus_score = 0.0
        if validator_count > 0:
            consensus_score = min(
                1.0,
                max(
                    0.0,
                    0.25
                    + latest_hit * 0.35
                    + latest_advantage * 0.9
                    + (validators_active / float(validator_count)) * 0.2,
                ),
            )
        disagreement_count = max(0, validator_count - int(round(consensus_score * validator_count)))
        last_activity_at = max(
            float(self._last_ingest_at or 0.0),
            float(refresh_state.get("last_refresh_finished_at", 0.0) or 0.0),
            float(refresh_state.get("last_refresh_started_at", 0.0) or 0.0),
        )

        nodes: list[dict[str, Any]] = []
        role_specs = (
            ("anchor", anchor_count, anchors_active, 1.0),
            ("learner", learner_count, learners_active, 0.8),
            ("validator", validator_count, validators_active, 1.2),
        )
        node_id = 1
        for role, count, active_for_role, weight in role_specs:
            for index in range(count):
                active = index < active_for_role
                if role == "anchor":
                    state = "anchored" if active else "idle"
                elif role == "learner":
                    state = "learning" if background_learning.get("running") and active else ("ready" if active else "idle")
                else:
                    state = "validating" if refresh_state.get("refresh_running") and active else ("ready" if active else "idle")
                nodes.append(
                    {
                        "node_id": node_id,
                        "name": f"{role}-{index + 1:02d}",
                        "role": role,
                        "active": active,
                        "healthy": active,
                        "weight": weight,
                        "state": state,
                        "last_activity_at": last_activity_at if active else 0.0,
                    }
                )
                node_id += 1

        return {
            "target_node_count": anchor_count + learner_count + validator_count,
            "anchor_node_count": anchor_count,
            "learner_node_count": learner_count,
            "validator_node_count": validator_count,
            "active_node_count": active_count,
            "healthy_node_count": healthy_count,
            "validator_quorum": quorum,
            "validator_active_count": validators_active,
            "validator_disagreement_count": disagreement_count,
            "consensus_score": round(consensus_score, 6),
            "last_propagation_at": last_activity_at,
            "comparison_targets": [
                {"node_count": 1, "label": "single", "available": bool(latest_knowledge)},
                {"node_count": 10, "label": "validated", "available": bool(latest_knowledge)},
                {
                    "node_count": anchor_count + learner_count + validator_count,
                    "label": "production",
                    "available": True,
                    "consensus_score": round(consensus_score, 6),
                },
            ],
            "nodes": nodes,
            "network_available": network_available,
        }

    def _status_payload(
        self,
        *,
        running: bool,
        pid: int | None,
        knowledge_running: bool,
        knowledge_pid: int | None,
        binary: Path | None,
        knowledge_binary: Path | None,
        live_memory_path: Path,
        latest: dict[str, Any] | None,
        latest_knowledge: dict[str, Any] | None,
        latest_demo: dict[str, Any] | None,
        refresh_state: dict[str, Any],
    ) -> dict[str, Any]:
        background_learning = get_background_learning_manager().status()
        topology = self._build_swarm_topology(
            running=running,
            knowledge_running=knowledge_running,
            latest=latest,
            latest_knowledge=latest_knowledge,
            latest_demo=latest_demo,
            refresh_state=refresh_state,
            background_learning=background_learning,
        )
        return {
            "binary_available": binary is not None,
            "binary_path": str(binary) if binary else "",
            "knowledge_binary_available": knowledge_binary is not None,
            "knowledge_binary_path": str(knowledge_binary) if knowledge_binary else "",
            "running": running,
            "pid": pid,
            "knowledge_running": knowledge_running,
            "knowledge_pid": knowledge_pid,
            "interval_sec": self._interval_sec,
            "status_path": str(self._status_path),
            "knowledge_status_path": str(self._knowledge_status_path),
            "live_memory_path": str(live_memory_path),
            "seed_memory_path": str(self._seed_memory_dir),
            "live_memory_document_count": count_formula_docs(live_memory_path),
            "live_memory_domains": summarize_formula_docs_by_domain(live_memory_path),
            "last_ingest_at": self._last_ingest_at,
            "last_ingest_kind": self._last_ingest_kind,
            "last_ingest_domain_delta": list(self._last_ingest_domain_delta),
            "last_knowledge_refresh_delta": self._last_knowledge_refresh_delta,
            "background_learning": background_learning,
            "latest_demo": latest_demo,
            "last_error": self._last_error,
            "swarm_topology": topology,
            "swarm_nodes": topology.get("nodes", []),
            **refresh_state,
            "latest": latest,
            "latest_knowledge": latest_knowledge,
        }

    def status(self) -> dict[str, Any]:
        if self._autostart:
            self.ensure_background()
        binary = self._resolve_bin()
        knowledge_binary = self._resolve_knowledge_bin()
        live_memory_path = ensure_live_formula_memory_seeded()
        pid = self._read_pid()
        knowledge_pid = self._read_pid_from(self._knowledge_pid_path)
        running = self._is_pid_running(pid)
        knowledge_running = self._is_pid_running(knowledge_pid)
        if not running and self._pid_path.exists():
            try:
                self._pid_path.unlink()
            except OSError:
                pass
            pid = None
        if not knowledge_running and self._knowledge_pid_path.exists():
            try:
                self._knowledge_pid_path.unlink()
            except OSError:
                pass
            knowledge_pid = None
        latest = self._load_latest()
        latest_knowledge = self._load_latest_knowledge()
        latest_demo = self._load_latest_demo()
        refresh_state = self._refresh_state_snapshot()
        return self._status_payload(
            running=running,
            pid=pid,
            knowledge_running=knowledge_running,
            knowledge_pid=knowledge_pid,
            binary=binary,
            knowledge_binary=knowledge_binary,
            live_memory_path=live_memory_path,
            latest=latest,
            latest_knowledge=latest_knowledge,
            latest_demo=latest_demo,
            refresh_state=refresh_state,
        )

    def _read_pid_from(self, path: Path) -> Optional[int]:
        try:
            raw = path.read_text(encoding="utf-8").strip()
        except OSError:
            return None
        if not raw:
            return None
        try:
            return int(raw)
        except ValueError:
            return None

    def ensure_background(self) -> dict[str, Any]:
        with self._lock:
            current = self.status_without_autostart()
            if current["running"] and current["knowledge_running"]:
                return current
            binary = self._resolve_bin()
            knowledge_binary = self._resolve_knowledge_bin()
            if binary is None or knowledge_binary is None:
                self._last_error = "kolibri_swarm_benchmark binary not found"
                return current
            self._ensure_dirs()
            if not current["running"]:
                with self._log_path.open("ab") as log_file:
                    proc = subprocess.Popen(
                        [
                            str(binary),
                            "--json-out",
                            str(self._status_path),
                            "--loop",
                            "--interval-sec",
                            str(self._interval_sec),
                        ],
                        cwd=self._project_root,
                        stdout=log_file,
                        stderr=subprocess.STDOUT,
                        start_new_session=True,
                    )
                self._pid_path.write_text(f"{proc.pid}\n", encoding="utf-8")
            if not current["knowledge_running"]:
                with self._knowledge_log_path.open("ab") as log_file:
                    proc = subprocess.Popen(
                        self._knowledge_command(knowledge_binary)
                        + [
                            "--loop",
                            "--interval-sec",
                            str(self._interval_sec),
                        ],
                        cwd=self._project_root,
                        stdout=log_file,
                        stderr=subprocess.STDOUT,
                        start_new_session=True,
                    )
                self._knowledge_pid_path.write_text(f"{proc.pid}\n", encoding="utf-8")
            self._last_error = ""
            time.sleep(0.2)
            return self.status_without_autostart()

    def status_without_autostart(self) -> dict[str, Any]:
        binary = self._resolve_bin()
        knowledge_binary = self._resolve_knowledge_bin()
        live_memory_path = ensure_live_formula_memory_seeded()
        pid = self._read_pid()
        knowledge_pid = self._read_pid_from(self._knowledge_pid_path)
        running = self._is_pid_running(pid)
        knowledge_running = self._is_pid_running(knowledge_pid)
        latest = self._load_latest()
        latest_knowledge = self._load_latest_knowledge()
        latest_demo = self._load_latest_demo()
        refresh_state = self._refresh_state_snapshot()
        return self._status_payload(
            running=running,
            pid=pid if running else None,
            knowledge_running=knowledge_running,
            knowledge_pid=knowledge_pid if knowledge_running else None,
            binary=binary,
            knowledge_binary=knowledge_binary,
            live_memory_path=live_memory_path,
            latest=latest,
            latest_knowledge=latest_knowledge,
            latest_demo=latest_demo,
            refresh_state=refresh_state,
        )

    def record_ingest_delta(self, kind: str, domain_delta: list[dict[str, Any]]) -> None:
        self._last_ingest_at = time.time()
        self._last_ingest_kind = kind
        self._last_ingest_domain_delta = list(domain_delta)

    def _refresh_state_snapshot(self) -> dict[str, Any]:
        with self._refresh_state_lock:
            return {
                "refresh_running": self._refresh_running,
                "refresh_pending": self._refresh_pending,
                "last_refresh_started_at": self._last_refresh_started_at,
                "last_refresh_finished_at": self._last_refresh_finished_at,
                "last_refresh_reason": self._last_refresh_reason,
            }

    def _mark_refresh_start(self, reason: str) -> None:
        with self._refresh_state_lock:
            self._refresh_running = True
            self._last_refresh_started_at = time.time()
            self._last_refresh_reason = reason

    def _mark_refresh_finish(self) -> None:
        with self._refresh_state_lock:
            self._last_refresh_finished_at = time.time()

    def _execute_refresh(self, timeout_sec: int, reason: str) -> None:
        self._mark_refresh_start(reason)
        try:
            with self._refresh_exec_lock:
                self._run_benchmarks_once(timeout_sec)
            self._last_error = ""
        finally:
            self._mark_refresh_finish()

    def _run_benchmarks_once(self, timeout_sec: int) -> None:
        binary = self._resolve_bin()
        knowledge_binary = self._resolve_knowledge_bin()
        if binary is None or knowledge_binary is None:
            raise FileNotFoundError("kolibri_swarm_benchmark binary not found")
        self._ensure_dirs()
        before_knowledge = self._load_latest_knowledge()
        subprocess.run(
            [str(binary), "--json-out", str(self._status_path)],
            cwd=self._project_root,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=True,
            timeout=timeout_sec,
        )
        subprocess.run(
            self._knowledge_command(knowledge_binary),
            cwd=self._project_root,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=True,
            timeout=timeout_sec,
        )
        self._last_knowledge_refresh_delta = _compute_knowledge_refresh_delta(
            before_knowledge,
            self._load_latest_knowledge(),
        )

    def run_once(self, timeout_sec: int = 180) -> dict[str, Any]:
        current = self.status_without_autostart()
        if current["running"] or current["knowledge_running"]:
            self._last_error = ""
            return current
        try:
            self._execute_refresh(timeout_sec, "manual-run")
        except (FileNotFoundError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as exc:
            self._last_error = f"swarm benchmark run failed: {exc}"
        return self.status_without_autostart()

    def force_refresh(self, timeout_sec: int = 180) -> dict[str, Any]:
        with self._lock:
            try:
                self._execute_refresh(timeout_sec, "force-refresh")
            except (FileNotFoundError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as exc:
                self._last_error = f"swarm benchmark refresh failed: {exc}"
            return self.status_without_autostart()

    def _queued_refresh_worker(self, timeout_sec: int) -> None:
        while True:
            try:
                self._execute_refresh(timeout_sec, "auto-ingest-refresh")
            except (FileNotFoundError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as exc:
                self._last_error = f"swarm benchmark auto-refresh failed: {exc}"

            with self._refresh_state_lock:
                if self._refresh_pending:
                    self._refresh_pending = False
                    continue
                self._refresh_running = False
                self._refresh_thread = None
                break

    def schedule_refresh(self, timeout_sec: int = 180) -> dict[str, Any]:
        should_start = False
        with self._refresh_state_lock:
            if self._refresh_running:
                self._refresh_pending = True
                self._last_refresh_reason = "auto-ingest-refresh"
            else:
                self._refresh_running = True
                self._refresh_pending = False
                should_start = True
        if should_start:
            thread = threading.Thread(
                target=self._queued_refresh_worker,
                args=(timeout_sec,),
                daemon=True,
                name="swarm-auto-refresh",
            )
            with self._refresh_state_lock:
                self._refresh_thread = thread
            thread.start()
        return self.status_without_autostart()


_manager: Optional[SwarmRuntimeManager] = None


def get_swarm_runtime_manager() -> SwarmRuntimeManager:
    global _manager
    if _manager is None:
        _manager = SwarmRuntimeManager()
    return _manager


def maybe_autostart_swarm_runtime() -> None:
    manager = get_swarm_runtime_manager()
    if manager._autostart:
        manager.ensure_background()


router = APIRouter(prefix="/api/v1/swarm/runtime", tags=["swarm-runtime"])


class SwarmTextIngestRequest(BaseModel):
    text: str = Field(min_length=10, max_length=200_000)
    title: str = Field(default="", max_length=200)
    source: str = Field(default="manual", max_length=120)
    category: str = Field(default="manual", max_length=120)


class SwarmUrlIngestRequest(BaseModel):
    url: str | None = Field(default=None, max_length=2000)
    urls: list[str] = Field(default_factory=list, max_length=32)
    crawl: bool = False
    depth: int = Field(default=1, ge=1, le=4)
    max_pages: int = Field(default=12, ge=1, le=100)
    delay_sec: float = Field(default=0.25, ge=0.0, le=3.0)


class SwarmDemoTextRequest(SwarmTextIngestRequest):
    refresh_timeout_sec: int = Field(default=180, ge=30, le=900)


class SwarmKpackExportRequest(BaseModel):
    package_id: str = Field(min_length=3, max_length=120)
    title: str = Field(min_length=3, max_length=200)
    language: str = Field(default="ru", min_length=2, max_length=16)
    domains: list[str] = Field(default_factory=list, max_length=32)
    description: str = Field(default="", max_length=500)
    default_query: str = Field(default="", max_length=500)


class BackgroundLearningSourceRequest(BaseModel):
    id: str = Field(default="", max_length=64)
    url: str = Field(min_length=8, max_length=2000)
    title: str = Field(default="", max_length=200)
    domain: str = Field(default="", max_length=120)
    enabled: bool = True
    crawl: bool = False
    depth: int = Field(default=1, ge=1, le=4)
    max_pages: int = Field(default=12, ge=1, le=100)
    delay_sec: float = Field(default=0.25, ge=0.0, le=3.0)


class BackgroundLearningSourcesRequest(BaseModel):
    sources: list[BackgroundLearningSourceRequest] = Field(default_factory=list, max_length=64)


def _demo_snapshot(status: dict[str, Any]) -> dict[str, Any]:
    return {
        "live_memory_document_count": int(status.get("live_memory_document_count", 0)),
        "live_memory_domains": list(status.get("live_memory_domains", []) or []),
        "latest_knowledge": status.get("latest_knowledge"),
        "last_knowledge_refresh_delta": status.get("last_knowledge_refresh_delta"),
    }


def _status_with_ingest(result: dict[str, Any]) -> dict[str, Any]:
    payload = get_swarm_runtime_manager().status_without_autostart()
    payload["ingest"] = result
    return payload


def run_text_ingest_demo(
    *,
    text: str,
    title: str = "",
    source: str = "manual",
    category: str = "manual",
    refresh_timeout_sec: int = 180,
) -> dict[str, Any]:
    manager = get_swarm_runtime_manager()
    before_status = manager.status_without_autostart()
    before_knowledge = before_status.get("latest_knowledge")
    live_dir = ensure_live_formula_memory_seeded()
    before_domains = summarize_formula_docs_by_domain_map(live_dir)

    path = ingest_text_document(
        text,
        title=title,
        source=source,
        category=category,
    )

    after_domain_counts = summarize_formula_docs_by_domain_map(live_dir)
    domain_delta = diff_formula_domain_counts(before_domains, after_domain_counts)
    focus_domains = {str(item.get("domain", "")).strip() for item in domain_delta if str(item.get("domain", "")).strip()}
    manager.record_ingest_delta("text", domain_delta)
    refreshed = manager.force_refresh(timeout_sec=refresh_timeout_sec)
    after_knowledge = refreshed.get("latest_knowledge")

    demo = {
        "kind": "text",
        "saved_documents": 1,
        "saved_paths": [str(path)],
        "domain_delta": domain_delta,
        "before": _demo_snapshot(before_status),
        "after": _demo_snapshot(refreshed),
        "knowledge_delta": _compute_knowledge_refresh_delta(before_knowledge, after_knowledge),
        "domain_score_delta": _compute_domain_score_delta(
            before_knowledge,
            after_knowledge,
            focus_domains=focus_domains or None,
        ),
        "message": "Текст добавлен в живую память, рой принудительно пересчитан, before/after отчёт собран.",
    }
    demo["comparison_summary"] = _build_demo_comparison_summary(
        before_knowledge,
        after_knowledge,
        list(demo.get("domain_score_delta", []) or []),
    )
    latest_demo = {
        "created_at": time.time(),
        "title": title.strip(),
        "source": source.strip(),
        "category": category.strip(),
        "saved_documents": 1,
        "message": demo["message"],
        "domain_delta": list(demo.get("domain_delta", []) or []),
        "knowledge_delta": demo.get("knowledge_delta"),
        "comparison_summary": demo.get("comparison_summary"),
    }
    manager.record_demo_snapshot(latest_demo)

    payload = dict(refreshed)
    payload["demo"] = demo
    payload["latest_demo"] = latest_demo
    return payload


@router.get("/status")
async def swarm_runtime_status() -> dict[str, Any]:
    return get_swarm_runtime_manager().status()


@router.post("/start")
async def swarm_runtime_start() -> dict[str, Any]:
    return get_swarm_runtime_manager().ensure_background()


@router.post("/run")
async def swarm_runtime_run() -> dict[str, Any]:
    return get_swarm_runtime_manager().run_once()


@router.post("/refresh")
async def swarm_runtime_refresh() -> dict[str, Any]:
    return get_swarm_runtime_manager().force_refresh()


@router.post("/ingest/text")
async def swarm_runtime_ingest_text(req: SwarmTextIngestRequest) -> dict[str, Any]:
    live_dir = ensure_live_formula_memory_seeded()
    before_domains = summarize_formula_docs_by_domain_map(live_dir)
    path = ingest_text_document(
        req.text,
        title=req.title,
        source=req.source,
        category=req.category,
    )
    domain_delta = diff_formula_domain_counts(before_domains, summarize_formula_docs_by_domain_map(live_dir))
    manager = get_swarm_runtime_manager()
    manager.record_ingest_delta("text", domain_delta)
    manager.schedule_refresh()
    return _status_with_ingest(
        {
            "kind": "text",
            "saved_documents": 1,
            "saved_paths": [str(path)],
            "domain_delta": domain_delta,
            "message": "Текст сохранён в живую формульную память. Пересчёт роя поставлен в очередь.",
        }
    )


@router.post("/ingest/url")
async def swarm_runtime_ingest_url(req: SwarmUrlIngestRequest) -> dict[str, Any]:
    urls = [req.url] if req.url else []
    urls.extend(req.urls)
    try:
        result = ingest_urls_via_trainer(
            urls,
            crawl=req.crawl,
            depth=req.depth,
            max_pages=req.max_pages,
            delay_sec=req.delay_sec,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=503, detail=str(exc)) from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail=f"URL ingest timeout: {exc}") from exc
    except subprocess.CalledProcessError as exc:
        detail = exc.stderr.strip() or exc.stdout.strip() or str(exc)
        raise HTTPException(status_code=502, detail=f"URL ingest failed: {detail}") from exc
    except ValueError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc

    result.update(
        {
            "kind": "url",
            "message": "URL добавлены в живую формульную память. Пересчёт роя поставлен в очередь.",
        }
    )
    manager = get_swarm_runtime_manager()
    manager.record_ingest_delta("url", list(result.get("domain_delta", []) or []))
    manager.schedule_refresh()
    return _status_with_ingest(result)


@router.post("/demo/ingest/text")
async def swarm_runtime_demo_ingest_text(req: SwarmDemoTextRequest) -> dict[str, Any]:
    return run_text_ingest_demo(
        text=req.text,
        title=req.title,
        source=req.source,
        category=req.category,
        refresh_timeout_sec=req.refresh_timeout_sec,
    )


@router.post("/kpack/export")
async def swarm_runtime_kpack_export(req: SwarmKpackExportRequest) -> dict[str, Any]:
    try:
        result = export_kpack(
            package_id=req.package_id,
            title=req.title,
            language=req.language,
            domains=req.domains,
            description=req.description,
            default_query=req.default_query,
        )
    except ValueError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc
    result["download_url"] = f"/api/v1/swarm/runtime/kpack/download/{result['filename']}"
    return result


@router.get("/kpack/download/{filename}")
async def swarm_runtime_kpack_download(filename: str) -> FileResponse:
    safe_name = os.path.basename(filename)
    if safe_name != filename or not safe_name.endswith(".kpack"):
        raise HTTPException(status_code=400, detail="Invalid kpack filename")
    path = get_project_root() / "data" / "swarm" / "kpacks" / safe_name
    if not path.exists():
        raise HTTPException(status_code=404, detail="kpack not found")
    return FileResponse(path, media_type="application/zip", filename=safe_name)


@router.post("/kpack/import")
async def swarm_runtime_kpack_import(
    file: UploadFile = File(...),
    refresh: bool = Form(True),
    refresh_timeout_sec: int = Form(180),
) -> dict[str, Any]:
    suffix = Path(file.filename or "upload.kpack").suffix or ".kpack"
    with tempfile.NamedTemporaryFile("wb", suffix=suffix, delete=False) as handle:
        temp_path = Path(handle.name)
        try:
            while True:
                chunk = await file.read(1024 * 1024)
                if not chunk:
                    break
                handle.write(chunk)
        finally:
            await file.close()

    try:
        result = import_kpack(pack_path=temp_path)
    except FileNotFoundError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc
    except ValueError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc
    finally:
        try:
            temp_path.unlink()
        except OSError:
            pass

    manager = get_swarm_runtime_manager()
    manager.record_ingest_delta("kpack", list(result.get("domain_delta", []) or []))
    if refresh:
        status = manager.force_refresh(timeout_sec=refresh_timeout_sec)
    else:
        status = manager.schedule_refresh()
    payload = dict(status)
    payload["import"] = result
    return payload


@router.get("/learning/status")
async def background_learning_status() -> dict[str, Any]:
    return get_background_learning_manager().status()


@router.post("/learning/start")
async def background_learning_start() -> dict[str, Any]:
    return get_background_learning_manager().ensure_background(force=True)


@router.post("/learning/run")
async def background_learning_run() -> dict[str, Any]:
    return get_background_learning_manager().run_once(force=True)


@router.get("/learning/history")
async def background_learning_history() -> dict[str, Any]:
    return get_background_learning_manager().history()


@router.get("/learning/sources")
async def background_learning_sources() -> dict[str, Any]:
    manager = get_background_learning_manager()
    return {
        "sources": manager.list_sources(),
        "source_count": len(manager.list_sources()),
    }


@router.put("/learning/sources")
async def background_learning_replace_sources(req: BackgroundLearningSourcesRequest) -> dict[str, Any]:
    manager = get_background_learning_manager()
    try:
        sources = manager.replace_sources([item.model_dump() for item in req.sources])
    except ValueError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc
    return {
        "sources": sources,
        "source_count": len(sources),
        "status": manager.status(),
    }
