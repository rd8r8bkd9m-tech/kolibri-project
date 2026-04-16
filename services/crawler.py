"""
Kolibri Web Crawler & Training API.

Эндпоинты для управления веб-краулером и обучением модели.
Вызывает C-бинарник kolibri_mass_trainer через subprocess.
"""
from __future__ import annotations

import asyncio
import os
import re
import shlex
import time
from pathlib import Path
from typing import Optional

from fastapi import APIRouter, HTTPException
from pydantic import BaseModel, Field
from .project_paths import get_project_root

router = APIRouter(prefix="/api/v1", tags=["crawler"])

# --- Пути ---
PROJECT_ROOT = get_project_root()
TRAINER_BIN = PROJECT_ROOT / "build" / "kolibri_mass_trainer"
MODEL_DIR = PROJECT_ROOT / "data" / "models"
DEFAULT_MODEL = MODEL_DIR / "kolibri_web.klm"


def _ensure_dirs() -> None:
    MODEL_DIR.mkdir(parents=True, exist_ok=True)


# --- Pydantic модели ---

class CrawlRequest(BaseModel):
    url: str = Field(description="Seed URL для обучения или краулинга")
    mode: str = Field(default="url", description="url | crawl")
    depth: int = Field(default=1, ge=0, le=5)
    max_pages: int = Field(default=10, ge=1, le=200)
    delay: float = Field(default=0.3, ge=0.0, le=5.0)
    model_path: Optional[str] = None


class CrawlResult(BaseModel):
    status: str
    pages_crawled: int = 0
    patterns: int = 0
    edges: int = 0
    tokens: int = 0
    model_size_mb: float = 0.0
    time_sec: float = 0.0
    output: str = ""


class ModelStats(BaseModel):
    exists: bool
    path: str
    size_mb: float = 0.0
    patterns: int = 0
    edges: int = 0
    max_patterns: int = 131072
    max_edges: int = 262144


class TrainStatusResponse(BaseModel):
    running: bool
    progress: float = 0.0
    current_url: str = ""
    pages_done: int = 0
    pages_total: int = 0
    log_lines: list[str] = []


# --- Глобальное состояние текущей задачи ---
_current_task: dict | None = None


def _parse_trainer_output(output: str) -> dict:
    """Парсим вывод kolibri_mass_trainer для извлечения статистики."""
    result: dict = {
        "pages_crawled": 0,
        "patterns": 0,
        "edges": 0,
        "tokens": 0,
        "model_size_mb": 0.0,
    }

    # Паттерны: "13479 / 131072"
    m = re.search(r"Паттернов в модели:\s*(\d+)", output)
    if m:
        result["patterns"] = int(m.group(1))

    m = re.search(r"Рёбер в графе:\s*(\d+)", output)
    if m:
        result["edges"] = int(m.group(1))

    m = re.search(r"Токенов обработано:\s*(\d+)", output)
    if m:
        result["tokens"] = int(m.group(1))

    m = re.search(r"Размер модели:\s*([\d.]+)\s*МБ", output)
    if m:
        result["model_size_mb"] = float(m.group(1))

    # Pages crawled
    m = re.search(r"Результат:\s*(\d+)\s*страниц", output)
    if m:
        result["pages_crawled"] = int(m.group(1))
    elif re.search(r"\[Train\].*URL:", output):
        result["pages_crawled"] = 1

    return result


@router.post("/crawl", response_model=CrawlResult)
async def start_crawl(req: CrawlRequest) -> CrawlResult:
    """Запуск краулинга и обучения модели."""
    global _current_task

    _ensure_dirs()

    if not TRAINER_BIN.exists():
        raise HTTPException(
            status_code=503,
            detail=f"Бинарник не найден: {TRAINER_BIN}. Соберите проект: cmake --build build",
        )

    model_path = req.model_path or str(DEFAULT_MODEL)

    # Формируем команду
    cmd = [
        str(TRAINER_BIN),
        "--model", model_path,
        "--verbose",
        "--generations", "0",
    ]

    if req.mode == "crawl":
        cmd.extend([
            "--crawl", req.url,
            "--depth", str(req.depth),
            "--max-pages", str(req.max_pages),
            "--delay", str(req.delay),
        ])
    else:
        cmd.extend(["--url", req.url])

    # Запускаем процесс
    _current_task = {
        "running": True,
        "progress": 0.0,
        "current_url": req.url,
        "pages_done": 0,
        "pages_total": req.max_pages if req.mode == "crawl" else 1,
        "log_lines": [],
    }

    t0 = time.monotonic()

    try:
        proc = await asyncio.create_subprocess_exec(
            *cmd,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.STDOUT,
            cwd=str(PROJECT_ROOT),
        )

        full_output = ""
        assert proc.stdout is not None

        while True:
            line = await proc.stdout.readline()
            if not line:
                break
            decoded = line.decode("utf-8", errors="replace").rstrip()
            full_output += decoded + "\n"

            # Обновляем прогресс
            if _current_task:
                _current_task["log_lines"].append(decoded)
                # Парсим прогресс "[Crawl] [5/20]"
                pm = re.search(r"\[Crawl\]\s*\[(\d+)/(\d+)\]", decoded)
                if pm:
                    done = int(pm.group(1))
                    total = int(pm.group(2))
                    _current_task["pages_done"] = done
                    _current_task["pages_total"] = total
                    _current_task["progress"] = done / total * 100
                # Парсим текущий URL
                um = re.search(r"https?://\S+", decoded)
                if um:
                    _current_task["current_url"] = um.group(0)

        await proc.wait()

    except Exception as e:
        _current_task = None
        raise HTTPException(status_code=500, detail=str(e))

    elapsed = time.monotonic() - t0
    _current_task = None

    # Парсим результат
    stats = _parse_trainer_output(full_output)

    return CrawlResult(
        status="ok" if proc.returncode == 0 else "error",
        pages_crawled=stats["pages_crawled"],
        patterns=stats["patterns"],
        edges=stats["edges"],
        tokens=stats["tokens"],
        model_size_mb=stats["model_size_mb"],
        time_sec=round(elapsed, 2),
        output=full_output[-3000:],  # Последние 3KB
    )


@router.get("/crawl/status", response_model=TrainStatusResponse)
async def crawl_status() -> TrainStatusResponse:
    """Текущий статус выполняемой задачи краулинга."""
    if _current_task is None:
        return TrainStatusResponse(running=False)

    return TrainStatusResponse(
        running=_current_task.get("running", False),
        progress=_current_task.get("progress", 0.0),
        current_url=_current_task.get("current_url", ""),
        pages_done=_current_task.get("pages_done", 0),
        pages_total=_current_task.get("pages_total", 0),
        log_lines=_current_task.get("log_lines", [])[-30:],
    )


@router.get("/model/stats", response_model=ModelStats)
async def model_stats(path: Optional[str] = None) -> ModelStats:
    """Статистика текущей обученной модели."""
    model_path = Path(path) if path else DEFAULT_MODEL

    if not model_path.exists():
        return ModelStats(exists=False, path=str(model_path))

    size_bytes = model_path.stat().st_size
    size_mb = round(size_bytes / (1024 * 1024), 2)

    # Читаем header файла .klm для получения counts
    patterns = 0
    edges = 0
    try:
        with open(model_path, "rb") as f:
            magic = f.read(4)
            if magic == b"KLM1":
                import struct
                version_data = f.read(4)
                patterns_data = f.read(4)
                edges_data = f.read(4)
                if len(patterns_data) == 4 and len(edges_data) == 4:
                    patterns = struct.unpack("<I", patterns_data)[0]
                    edges = struct.unpack("<I", edges_data)[0]
    except Exception:
        pass

    return ModelStats(
        exists=True,
        path=str(model_path),
        size_mb=size_mb,
        patterns=patterns,
        edges=edges,
    )


@router.get("/model/list")
async def list_models() -> list[dict]:
    """Список всех обученных моделей."""
    _ensure_dirs()
    models = []
    for p in MODEL_DIR.glob("*.klm"):
        size_mb = round(p.stat().st_size / (1024 * 1024), 2)
        models.append({
            "name": p.name,
            "path": str(p),
            "size_mb": size_mb,
            "modified": p.stat().st_mtime,
        })
    return sorted(models, key=lambda x: x["modified"], reverse=True)


@router.delete("/model/{name}")
async def delete_model(name: str) -> dict:
    """Удалить модель."""
    model_path = MODEL_DIR / name
    if not model_path.exists():
        raise HTTPException(404, "Модель не найдена")
    model_path.unlink()
    return {"status": "deleted", "name": name}


# --- Запрос к модели ---

class QueryRequest(BaseModel):
    query: str = Field(description="Текст запроса к модели")
    model_path: Optional[str] = None


class QueryResponse(BaseModel):
    status: str
    query: str
    results: list[str] = []
    raw_output: str = ""


@router.post("/model/query", response_model=QueryResponse)
async def query_model(req: QueryRequest) -> QueryResponse:
    """Запрос к обученной модели через kolibri_mass_trainer --query."""
    if not TRAINER_BIN.exists():
        raise HTTPException(503, "Бинарник не найден")

    model_path = req.model_path or str(DEFAULT_MODEL)
    if not Path(model_path).exists():
        raise HTTPException(404, "Модель не найдена. Сначала обучите модель.")

    cmd = [
        str(TRAINER_BIN),
        "--model", model_path,
        "--query", req.query,
    ]

    try:
        proc = await asyncio.create_subprocess_exec(
            *cmd,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.STDOUT,
            cwd=str(PROJECT_ROOT),
        )
        stdout, _ = await asyncio.wait_for(proc.communicate(), timeout=10.0)
        output = stdout.decode("utf-8", errors="replace").strip()
    except asyncio.TimeoutError:
        raise HTTPException(504, "Таймаут запроса")
    except Exception as e:
        raise HTTPException(500, str(e))

    # Парсим строки результата
    results = [line.strip() for line in output.split("\n") if line.strip() and not line.startswith("[")]

    return QueryResponse(
        status="ok" if proc.returncode == 0 else "error",
        query=req.query,
        results=results,
        raw_output=output[:5000],
    )


# --- Терминал (выполнение команд) ---

class TerminalRequest(BaseModel):
    command: str = Field(description="Команда для выполнения")
    cwd: Optional[str] = None
    timeout: float = Field(default=15.0, ge=1.0, le=60.0)


class TerminalResponse(BaseModel):
    status: str
    exit_code: int
    stdout: str
    stderr: str
    duration_ms: float


# Белый список безопасных команд
_ALLOWED_COMMANDS = {
    "ls", "cat", "head", "tail", "wc", "du", "df", "free",
    "uptime", "whoami", "hostname", "date", "pwd", "echo",
    "find", "grep", "file", "stat", "uname", "env", "which",
    "kolibri_mass_trainer", "cmake", "ctest",
}

# Чёрный список — категорически запрещённые
_BLOCKED_PATTERNS = {
    "rm -rf /",
    "mkfs",
    "dd if=",
    ":(){ :",
    "fork",
    "> /dev/sd",
    "$(",
    "`",
}
_SHELL_SPLIT_RE = re.compile(r"(?:&&|\|\||\||;)")


def _resolve_cwd(cwd: Optional[str]) -> Path:
    base = PROJECT_ROOT.resolve()
    if not cwd:
        return base
    candidate = Path(cwd)
    if not candidate.is_absolute():
        candidate = base / candidate
    resolved = candidate.resolve()
    try:
        resolved.relative_to(base)
    except ValueError:
        raise HTTPException(403, "cwd должен находиться внутри проекта Kolibri")
    if not resolved.exists() or not resolved.is_dir():
        raise HTTPException(400, f"cwd не существует: {resolved}")
    return resolved


def _extract_base_commands(command: str) -> list[str]:
    bases: list[str] = []
    for segment in _SHELL_SPLIT_RE.split(command):
        seg = segment.strip()
        if not seg:
            continue
        try:
            tokens = shlex.split(seg)
        except ValueError:
            continue
        if not tokens:
            continue
        bases.append(tokens[0].split("/")[-1])
    return bases


@router.post("/terminal/exec", response_model=TerminalResponse)
async def terminal_exec(req: TerminalRequest) -> TerminalResponse:
    """Выполнить безопасную команду в терминале."""
    command = req.command.strip()

    if not command:
        raise HTTPException(400, "Пустая команда")

    # Проверка на опасные паттерны
    for blocked in _BLOCKED_PATTERNS:
        if blocked in command:
            raise HTTPException(403, f"Команда заблокирована: {blocked}")

    base_cmds = _extract_base_commands(command)
    if not base_cmds:
        raise HTTPException(400, "Не удалось распознать команду")

    # Разрешаем только whitelist-команды (включая цепочки через &&/|)
    for base_cmd in base_cmds:
        if base_cmd in _ALLOWED_COMMANDS:
            continue
        if base_cmd.startswith("kolibri_"):
            # Поддержка бинарников проекта вроде ./build/kolibri_mass_trainer
            continue
        raise HTTPException(
            403,
            f"Команда '{base_cmd}' не в белом списке. "
            f"Разрешены: {', '.join(sorted(_ALLOWED_COMMANDS))}",
        )

    cwd = _resolve_cwd(req.cwd)
    t0 = time.monotonic()

    shell_executable = "/bin/bash" if Path("/bin/bash").exists() else "/bin/sh"

    try:
        # Запускаем через shell, чтобы отсутствующие бинарники отдавали stderr/127,
        # а не роняли API в HTTP 500.
        proc = await asyncio.create_subprocess_shell(
            command,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
            cwd=str(cwd),
            executable=shell_executable,
        )
        stdout_bytes, stderr_bytes = await asyncio.wait_for(
            proc.communicate(), timeout=req.timeout,
        )
    except asyncio.TimeoutError:
        return TerminalResponse(
            status="timeout",
            exit_code=-1,
            stdout="",
            stderr=f"Таймаут: команда не завершилась за {req.timeout}с",
            duration_ms=round((time.monotonic() - t0) * 1000, 1),
        )
    except FileNotFoundError as e:
        return TerminalResponse(
            status="error",
            exit_code=127,
            stdout="",
            stderr=f"Shell/команда не найдены: {e}",
            duration_ms=round((time.monotonic() - t0) * 1000, 1),
        )
    except Exception as e:
        return TerminalResponse(
            status="error",
            exit_code=1,
            stdout="",
            stderr=f"Ошибка выполнения: {e}",
            duration_ms=round((time.monotonic() - t0) * 1000, 1),
        )

    duration_ms = round((time.monotonic() - t0) * 1000, 1)

    return TerminalResponse(
        status="ok" if proc.returncode == 0 else "error",
        exit_code=proc.returncode or 0,
        stdout=stdout_bytes.decode("utf-8", errors="replace")[:10000],
        stderr=stderr_bytes.decode("utf-8", errors="replace")[:5000],
        duration_ms=duration_ms,
    )
