from __future__ import annotations

import hashlib
import json
import os
import threading
import time
from pathlib import Path
from typing import Any
from urllib.parse import urlparse

from .project_paths import get_project_root
from .realtime_lookup import external_network_available
from .swarm_live_memory import ingest_urls_via_trainer


def _env_flag(name: str, default: bool = False) -> bool:
    value = os.environ.get(name)
    if value is None:
        return default
    return value.strip().lower() in {"1", "true", "yes", "on"}


def _env_int(name: str, default: int) -> int:
    value = os.environ.get(name)
    if value is None:
        return default
    try:
        return int(value.strip())
    except (TypeError, ValueError):
        return default


def _env_float(name: str, default: float) -> float:
    value = os.environ.get(name)
    if value is None:
        return default
    try:
        return float(value.strip())
    except (TypeError, ValueError):
        return default


def _get_swarm_manager():
    from .swarm_runtime_api import get_swarm_runtime_manager

    return get_swarm_runtime_manager()


def _sanitize_source(raw: dict[str, Any]) -> dict[str, Any]:
    url = str(raw.get("url", "") or "").strip()
    parsed = urlparse(url)
    if parsed.scheme not in {"http", "https"} or not parsed.netloc:
        raise ValueError(f"Invalid background learning URL: {url}")

    slug = hashlib.sha1(url.encode("utf-8")).hexdigest()[:12]
    source_id = str(raw.get("id", "") or "").strip() or f"src-{slug}"
    title = str(raw.get("title", "") or "").strip()[:200]
    domain = str(raw.get("domain", "") or "").strip().lower()[:120]
    return {
        "id": source_id[:64],
        "url": url,
        "title": title,
        "domain": domain,
        "enabled": bool(raw.get("enabled", True)),
        "crawl": bool(raw.get("crawl", False)),
        "depth": max(1, min(4, int(raw.get("depth", 1) or 1))),
        "max_pages": max(1, min(100, int(raw.get("max_pages", 12) or 12))),
        "delay_sec": max(0.0, min(3.0, float(raw.get("delay_sec", 0.25) or 0.25))),
    }


def _aggregate_domain_delta(items: list[dict[str, Any]]) -> list[dict[str, Any]]:
    merged: dict[str, dict[str, Any]] = {}
    for item in items:
        domain = str(item.get("domain", "") or "").strip()
        if not domain:
            continue
        current = merged.setdefault(
            domain,
            {"domain": domain, "before": 0, "after": 0, "delta": 0},
        )
        current["before"] += int(item.get("before", 0) or 0)
        current["after"] += int(item.get("after", 0) or 0)
        current["delta"] += int(item.get("delta", 0) or 0)
    return sorted(merged.values(), key=lambda item: (-int(item["delta"]), str(item["domain"])))


def _ingest_result_error_text(result: dict[str, Any]) -> str:
    stderr = str(result.get("stderr", "") or "").strip()
    stdout = str(result.get("stdout", "") or "").strip()
    if "failed to fetch" in stderr.lower():
        return stderr.splitlines()[0].strip()
    saved_documents = int(result.get("saved_documents", 0) or 0)
    domain_delta = list(result.get("domain_delta", []) or [])
    if saved_documents <= 0 and not domain_delta and stderr:
        return stderr.splitlines()[0].strip()
    if saved_documents <= 0 and not domain_delta and stdout and "failed to fetch" in stdout.lower():
        return stdout.splitlines()[0].strip()
    return ""


class BackgroundLearningManager:
    def __init__(self) -> None:
        project_root = get_project_root()
        self._enabled = _env_flag("KOLIBRI_ENABLE_BACKGROUND_LEARNING", default=False)
        self._interval_sec = max(60, _env_int("KOLIBRI_BACKGROUND_LEARNING_INTERVAL_SEC", 1800))
        self._max_sources_per_cycle = max(1, min(32, _env_int("KOLIBRI_BACKGROUND_LEARNING_MAX_SOURCES", 3)))
        self._max_history = max(5, min(200, _env_int("KOLIBRI_BACKGROUND_LEARNING_MAX_HISTORY", 40)))
        self._timeout_sec = max(60, _env_int("KOLIBRI_BACKGROUND_LEARNING_TIMEOUT_SEC", 600))
        self._sources_path = Path(
            os.environ.get(
                "KOLIBRI_BACKGROUND_LEARNING_SOURCES_PATH",
                project_root / "data" / "swarm" / "background_learning_sources.json",
            )
        )
        self._state_path = Path(
            os.environ.get(
                "KOLIBRI_BACKGROUND_LEARNING_STATUS_PATH",
                project_root / "data" / "swarm" / "background_learning_status.json",
            )
        )
        self._lock = threading.RLock()
        self._run_lock = threading.Lock()
        self._thread: threading.Thread | None = None
        self._stop_event = threading.Event()
        self._running = False
        self._cursor = 0
        self._last_cycle_started_at = 0.0
        self._last_cycle_finished_at = 0.0
        self._last_success_at = 0.0
        self._last_error = ""
        self._latest_result: dict[str, Any] | None = None
        self._recent_runs: list[dict[str, Any]] = []
        self._source_health: dict[str, dict[str, Any]] = {}
        self._load_state()
        self._ensure_sources_file()

    def _ensure_dirs(self) -> None:
        self._sources_path.parent.mkdir(parents=True, exist_ok=True)
        self._state_path.parent.mkdir(parents=True, exist_ok=True)

    def _ensure_sources_file(self) -> None:
        self._ensure_dirs()
        if self._sources_path.exists():
            return
        raw_urls = os.environ.get("KOLIBRI_BACKGROUND_LEARNING_URLS", "")
        urls = [item.strip() for item in raw_urls.split(",") if item.strip()]
        sources = []
        for url in urls:
            try:
                sources.append(_sanitize_source({"url": url}))
            except ValueError:
                continue
        self._write_sources(sources)

    def _load_state(self) -> None:
        try:
            payload = json.loads(self._state_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            return
        self._cursor = int(payload.get("cursor", 0) or 0)
        self._last_cycle_started_at = float(payload.get("last_cycle_started_at", 0.0) or 0.0)
        self._last_cycle_finished_at = float(payload.get("last_cycle_finished_at", 0.0) or 0.0)
        self._last_success_at = float(payload.get("last_success_at", 0.0) or 0.0)
        self._last_error = str(payload.get("last_error", "") or "")
        latest = payload.get("latest_result")
        self._latest_result = latest if isinstance(latest, dict) else None
        raw_runs = payload.get("recent_runs")
        if isinstance(raw_runs, list):
            self._recent_runs = [item for item in raw_runs if isinstance(item, dict)][-self._max_history :]
        raw_health = payload.get("source_health")
        if isinstance(raw_health, dict):
            self._source_health = {
                str(source_id): dict(item)
                for source_id, item in raw_health.items()
                if isinstance(item, dict)
            }

    def _persist_state(self) -> None:
        self._ensure_dirs()
        payload = {
            "cursor": self._cursor,
            "last_cycle_started_at": self._last_cycle_started_at,
            "last_cycle_finished_at": self._last_cycle_finished_at,
            "last_success_at": self._last_success_at,
            "last_error": self._last_error,
            "latest_result": self._latest_result,
            "recent_runs": self._recent_runs[-self._max_history :],
            "source_health": self._source_health,
        }
        self._state_path.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")

    def _write_sources(self, sources: list[dict[str, Any]]) -> None:
        self._ensure_dirs()
        payload = {"sources": sources, "updated_at": time.time()}
        self._sources_path.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")

    def list_sources(self) -> list[dict[str, Any]]:
        self._ensure_sources_file()
        try:
            payload = json.loads(self._sources_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            return []
        raw_sources = payload.get("sources")
        if not isinstance(raw_sources, list):
            return []
        sources: list[dict[str, Any]] = []
        seen_ids: set[str] = set()
        for raw in raw_sources:
            if not isinstance(raw, dict):
                continue
            try:
                item = _sanitize_source(raw)
            except ValueError:
                continue
            if item["id"] in seen_ids:
                continue
            seen_ids.add(str(item["id"]))
            sources.append(item)
        return sources

    def replace_sources(self, raw_sources: list[dict[str, Any]]) -> list[dict[str, Any]]:
        sanitized: list[dict[str, Any]] = []
        seen_ids: set[str] = set()
        for raw in raw_sources:
            item = _sanitize_source(raw)
            if item["id"] in seen_ids:
                raise ValueError(f"Duplicate background learning source id: {item['id']}")
            seen_ids.add(str(item["id"]))
            sanitized.append(item)
        keep_ids = {str(item["id"]) for item in sanitized}
        self._source_health = {
            source_id: entry
            for source_id, entry in self._source_health.items()
            if source_id in keep_ids
        }
        self._write_sources(sanitized)
        self._persist_state()
        return sanitized

    def _build_internet_runtime_status(
        self,
        *,
        sources_with_health: list[dict[str, Any]],
        enabled_source_count: int,
        now: float,
    ) -> dict[str, Any]:
        enabled_sources = [item for item in sources_with_health if item.get("enabled")]
        eligible_sources = [item for item in enabled_sources if item.get("eligible_now")]
        backoff_sources = [item for item in enabled_sources if not item.get("eligible_now")]
        failing_sources = [
            item
            for item in enabled_sources
            if int(((item.get("health") or {}).get("consecutive_failures", 0) or 0)) > 0
        ]
        no_change_sources = [
            item
            for item in enabled_sources
            if int(((item.get("health") or {}).get("consecutive_no_change", 0) or 0)) > 0
        ]
        recent_success_window_sec = max(float(self._interval_sec * 2), 900.0)
        recent_success_sources = [
            item
            for item in enabled_sources
            if float(((item.get("health") or {}).get("last_success_at", 0.0) or 0.0)) >= (now - recent_success_window_sec)
        ]
        next_attempt_at = min(
            (
                float(((item.get("health") or {}).get("next_eligible_at", 0.0) or 0.0))
                for item in backoff_sources
                if float(((item.get("health") or {}).get("next_eligible_at", 0.0) or 0.0)) > now
            ),
            default=0.0,
        )
        network_probe_ok = external_network_available()

        if enabled_source_count <= 0:
            source_state = "no-sources"
        elif not network_probe_ok:
            if recent_success_sources and backoff_sources:
                source_state = "backoff"
            elif recent_success_sources and eligible_sources:
                source_state = "probe-degraded"
            else:
                source_state = "degraded-no-network"
        elif eligible_sources:
            source_state = "ready"
        elif backoff_sources:
            source_state = "backoff"
        else:
            source_state = "idle"

        if self._running:
            daemon_state = "running"
        elif self._enabled:
            daemon_state = "stopped"
        else:
            daemon_state = "disabled"

        return {
            "daemon_state": daemon_state,
            "source_state": source_state,
            "network_probe_ok": network_probe_ok,
            "configured_source_count": len(sources_with_health),
            "enabled_source_count": enabled_source_count,
            "eligible_source_count": len(eligible_sources),
            "backoff_source_count": len(backoff_sources),
            "failing_source_count": len(failing_sources),
            "no_change_source_count": len(no_change_sources),
            "recent_success_source_count": len(recent_success_sources),
            "next_attempt_at": next_attempt_at,
        }

    def status(self) -> dict[str, Any]:
        with self._lock:
            sources = self.list_sources()
            enabled_sources = [item for item in sources if item.get("enabled")]
            now = time.time()
            sources_with_health = []
            for item in sources:
                health = dict(self._source_health.get(str(item["id"]), {}))
                next_eligible_at = float(health.get("next_eligible_at", 0.0) or 0.0)
                sources_with_health.append(
                    {
                        **item,
                        "eligible_now": next_eligible_at <= now,
                        "health": health,
                    }
                )
            internet_runtime = self._build_internet_runtime_status(
                sources_with_health=sources_with_health,
                enabled_source_count=len(enabled_sources),
                now=now,
            )
            return {
                "enabled": self._enabled,
                "running": self._running,
                "network_available": bool(internet_runtime["network_probe_ok"]),
                "internet_runtime": internet_runtime,
                "interval_sec": self._interval_sec,
                "max_sources_per_cycle": self._max_sources_per_cycle,
                "max_history": self._max_history,
                "timeout_sec": self._timeout_sec,
                "sources_path": str(self._sources_path),
                "status_path": str(self._state_path),
                "source_count": len(sources),
                "enabled_source_count": len(enabled_sources),
                "sources": sources_with_health,
                "last_cycle_started_at": self._last_cycle_started_at,
                "last_cycle_finished_at": self._last_cycle_finished_at,
                "last_success_at": self._last_success_at,
                "last_error": self._last_error,
                "latest_result": self._latest_result,
                "history_count": len(self._recent_runs),
                "source_health": {key: dict(value) for key, value in self._source_health.items()},
            }

    def history(self) -> dict[str, Any]:
        with self._lock:
            return {
                "history_count": len(self._recent_runs),
                "recent_runs": list(reversed([dict(item) for item in self._recent_runs])),
                "source_health": {key: dict(value) for key, value in self._source_health.items()},
            }

    def ensure_background(self, *, force: bool = False) -> dict[str, Any]:
        with self._lock:
            if self._thread and self._thread.is_alive():
                self._running = True
                return self.status()
            if not force and not self._enabled:
                return self.status()
            self._stop_event.clear()
            thread = threading.Thread(target=self._loop, daemon=True, name="background-learning")
            self._thread = thread
            self._running = True
            thread.start()
            return self.status()

    def _get_source_health(self, source: dict[str, Any]) -> dict[str, Any]:
        source_id = str(source["id"])
        entry = self._source_health.setdefault(
            source_id,
            {
                "source_id": source_id,
                "url": str(source.get("url", "") or ""),
                "title": str(source.get("title", "") or ""),
                "domain": str(source.get("domain", "") or ""),
                "last_attempt_at": 0.0,
                "last_success_at": 0.0,
                "last_change_at": 0.0,
                "last_saved_documents": 0,
                "total_runs": 0,
                "total_successes": 0,
                "total_failures": 0,
                "total_saved_documents": 0,
                "consecutive_failures": 0,
                "consecutive_no_change": 0,
                "last_error": "",
                "next_eligible_at": 0.0,
            },
        )
        entry["url"] = str(source.get("url", "") or "")
        entry["title"] = str(source.get("title", "") or "")
        entry["domain"] = str(source.get("domain", "") or "")
        return entry

    def _record_source_success(self, source: dict[str, Any], *, now: float, saved_documents: int) -> None:
        entry = self._get_source_health(source)
        entry["last_attempt_at"] = now
        entry["last_success_at"] = now
        entry["last_saved_documents"] = int(saved_documents)
        entry["total_runs"] = int(entry.get("total_runs", 0) or 0) + 1
        entry["total_successes"] = int(entry.get("total_successes", 0) or 0) + 1
        entry["total_saved_documents"] = int(entry.get("total_saved_documents", 0) or 0) + int(saved_documents)
        entry["consecutive_failures"] = 0
        entry["last_error"] = ""
        if int(saved_documents) > 0:
            entry["consecutive_no_change"] = 0
            entry["last_change_at"] = now
            entry["next_eligible_at"] = 0.0
        else:
            consecutive_no_change = int(entry.get("consecutive_no_change", 0) or 0) + 1
            entry["consecutive_no_change"] = consecutive_no_change
            entry["next_eligible_at"] = now + min(float(self._interval_sec * max(1, consecutive_no_change)), float(self._interval_sec * 8))

    def _record_source_error(self, source: dict[str, Any], *, now: float, error: str) -> None:
        entry = self._get_source_health(source)
        entry["last_attempt_at"] = now
        entry["last_saved_documents"] = 0
        entry["total_runs"] = int(entry.get("total_runs", 0) or 0) + 1
        entry["total_failures"] = int(entry.get("total_failures", 0) or 0) + 1
        consecutive_failures = int(entry.get("consecutive_failures", 0) or 0) + 1
        entry["consecutive_failures"] = consecutive_failures
        entry["consecutive_no_change"] = 0
        entry["last_error"] = error
        entry["next_eligible_at"] = now + min(float(self._interval_sec * (2 ** min(consecutive_failures - 1, 5))), 21600.0)

    def _append_recent_run(self, payload: dict[str, Any]) -> None:
        self._recent_runs.append(payload)
        if len(self._recent_runs) > self._max_history:
            self._recent_runs = self._recent_runs[-self._max_history :]

    def run_once(self, *, force: bool = False) -> dict[str, Any]:
        with self._run_lock:
            started_at = time.time()
            self._last_cycle_started_at = started_at
            sources = [item for item in self.list_sources() if item.get("enabled")]
            result_items: list[dict[str, Any]] = []
            total_saved_documents = 0
            aggregated_delta: list[dict[str, Any]] = []
            if not sources:
                self._last_cycle_finished_at = time.time()
                self._last_error = "No enabled background learning sources configured"
                self._latest_result = {
                    "started_at": started_at,
                    "finished_at": self._last_cycle_finished_at,
                    "sources_attempted": 0,
                    "sources_succeeded": 0,
                    "sources_skipped_backoff": 0,
                    "saved_documents": 0,
                    "domain_delta": [],
                    "results": [],
                }
                self._append_recent_run(dict(self._latest_result))
                self._persist_state()
                return self.status()

            batch, skipped_backoff = self._select_batch(sources, force=force, now=started_at)
            if not batch:
                self._last_cycle_finished_at = time.time()
                self._last_error = ""
                self._latest_result = {
                    "started_at": started_at,
                    "finished_at": self._last_cycle_finished_at,
                    "sources_attempted": 0,
                    "sources_succeeded": 0,
                    "sources_skipped_backoff": skipped_backoff,
                    "saved_documents": 0,
                    "domain_delta": [],
                    "results": [],
                }
                self._append_recent_run(dict(self._latest_result))
                self._persist_state()
                return self.status()
            all_domain_delta: list[dict[str, Any]] = []
            success_count = 0
            latest_error = ""

            for source in batch:
                source_now = time.time()
                entry = {
                    "id": source["id"],
                    "url": source["url"],
                    "title": source.get("title", ""),
                    "domain": source.get("domain", ""),
                    "crawl": bool(source.get("crawl")),
                }
                try:
                    ingest_result = ingest_urls_via_trainer(
                        [str(source["url"])],
                        crawl=bool(source.get("crawl")),
                        depth=int(source.get("depth", 1) or 1),
                        max_pages=int(source.get("max_pages", 12) or 12),
                        delay_sec=float(source.get("delay_sec", 0.25) or 0.25),
                        timeout_sec=self._timeout_sec,
                    )
                    ingest_error = _ingest_result_error_text(ingest_result)
                    if ingest_error:
                        raise RuntimeError(ingest_error)
                    saved_documents = int(ingest_result.get("saved_documents", 0) or 0)
                    domain_delta = list(ingest_result.get("domain_delta", []) or [])
                    total_saved_documents += saved_documents
                    all_domain_delta.extend(domain_delta)
                    success_count += 1
                    self._record_source_success(source, now=source_now, saved_documents=saved_documents)
                    entry.update(
                        {
                            "status": "ok",
                            "saved_documents": saved_documents,
                            "domain_delta": domain_delta,
                        }
                    )
                except Exception as exc:  # noqa: BLE001 - background worker should degrade gracefully
                    latest_error = str(exc)
                    self._record_source_error(source, now=source_now, error=str(exc))
                    entry.update({"status": "error", "error": str(exc)})
                result_items.append(entry)

            aggregated_delta = _aggregate_domain_delta(all_domain_delta)
            if success_count > 0:
                swarm_manager = _get_swarm_manager()
                swarm_manager.record_ingest_delta("background-url", aggregated_delta)
                swarm_manager.schedule_refresh()
                self._last_success_at = time.time()
                self._last_error = ""
            else:
                self._last_error = latest_error or "Background learning cycle failed"

            finished_at = time.time()
            self._last_cycle_finished_at = finished_at
            self._latest_result = {
                "started_at": started_at,
                "finished_at": finished_at,
                "sources_attempted": len(batch),
                "sources_succeeded": success_count,
                "sources_skipped_backoff": skipped_backoff,
                "saved_documents": total_saved_documents,
                "domain_delta": aggregated_delta,
                "results": result_items,
            }
            self._append_recent_run(dict(self._latest_result))
            self._persist_state()
            return self.status()

    def _select_batch(
        self,
        sources: list[dict[str, Any]],
        *,
        force: bool = False,
        now: float | None = None,
    ) -> tuple[list[dict[str, Any]], int]:
        if not sources:
            return [], 0
        current_time = float(now or time.time())
        limit = min(len(sources), self._max_sources_per_cycle)
        start = self._cursor % len(sources)
        ordered = [sources[(start + offset) % len(sources)] for offset in range(len(sources))]
        batch: list[dict[str, Any]] = []
        skipped_backoff = 0
        last_offset = -1
        for offset, source in enumerate(ordered):
            if not force:
                next_eligible_at = float(self._source_health.get(str(source["id"]), {}).get("next_eligible_at", 0.0) or 0.0)
                if next_eligible_at > current_time:
                    skipped_backoff += 1
                    continue
            batch.append(source)
            last_offset = offset
            if len(batch) >= limit:
                break
        if batch and last_offset >= 0:
            self._cursor = (start + last_offset + 1) % len(sources)
        elif sources:
            self._cursor = (start + 1) % len(sources)
        return batch, skipped_backoff

    def _loop(self) -> None:
        while not self._stop_event.is_set():
            try:
                self.run_once(force=False)
            except Exception as exc:  # noqa: BLE001 - daemon must stay alive
                with self._lock:
                    self._last_error = str(exc)
                    self._persist_state()
            if self._stop_event.wait(self._interval_sec):
                break
        with self._lock:
            self._running = False
            self._thread = None


_manager: BackgroundLearningManager | None = None


def get_background_learning_manager() -> BackgroundLearningManager:
    global _manager
    if _manager is None:
        _manager = BackgroundLearningManager()
    return _manager


def maybe_autostart_background_learning() -> None:
    get_background_learning_manager().ensure_background(force=False)
