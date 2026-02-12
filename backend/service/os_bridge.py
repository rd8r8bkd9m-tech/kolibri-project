from __future__ import annotations

import os
import subprocess
import time
from itertools import islice
from pathlib import Path

import psutil
from fastapi import APIRouter, FastAPI, HTTPException
from pydantic import BaseModel

from .project_paths import get_project_root

router = APIRouter()
PROJECT_ROOT = get_project_root().resolve()
LEGACY_WORKSPACES_ROOT = "/workspaces/kolibri-project"


class CommandRequest(BaseModel):
    cmd: str


class FileRequest(BaseModel):
    path: str
    content: str | None = None


def _resolve_project_path(path: str) -> Path:
    raw = path.strip()
    if raw.startswith(LEGACY_WORKSPACES_ROOT):
        suffix = raw[len(LEGACY_WORKSPACES_ROOT):].lstrip("/")
        candidate = PROJECT_ROOT / suffix
    else:
        candidate = Path(raw)
    if not candidate.is_absolute():
        candidate = PROJECT_ROOT / candidate
    resolved = candidate.resolve()
    try:
        resolved.relative_to(PROJECT_ROOT)
    except ValueError:
        raise HTTPException(403, "Access denied")
    return resolved


@router.get("/api/dev/ls")
async def list_files(path: str = "."):
    """List directory contents safely."""
    target = _resolve_project_path(path)
    if not target.exists():
        raise HTTPException(404, f"Path not found: {target}")
    if not target.is_dir():
        raise HTTPException(400, f"Not a directory: {target}")

    try:
        items = []
        for entry in os.scandir(target):
            items.append({
                "name": entry.name,
                "is_dir": entry.is_dir(),
                "size": entry.stat().st_size if not entry.is_dir() else 0,
            })
        return sorted(items, key=lambda x: (not x["is_dir"], x["name"]))
    except Exception as e:
        raise HTTPException(500, str(e))


@router.post("/api/dev/read")
async def read_file(req: FileRequest):
    """Read text file content."""
    target = _resolve_project_path(req.path)
    if not target.exists():
        raise HTTPException(404, f"File not found: {target}")
    if not target.is_file():
        raise HTTPException(400, f"Not a file: {target}")

    try:
        with open(target, "r", encoding="utf-8") as f:
            return {"content": f.read()}
    except Exception as e:
        raise HTTPException(500, str(e))


@router.post("/api/dev/save")
async def save_file(req: FileRequest):
    """Save text file content."""
    if req.content is None:
        raise HTTPException(400, "Content required")
    target = _resolve_project_path(req.path)

    try:
        target.parent.mkdir(parents=True, exist_ok=True)
        with open(target, "w", encoding="utf-8") as f:
            f.write(req.content)
        return {"status": "ok"}
    except Exception as e:
        raise HTTPException(500, str(e))


@router.get("/api/system/stats")
async def get_stats():
    # Real OS metrics
    return {
        "cpu": psutil.cpu_percent(interval=None),
        "memory": psutil.virtual_memory().percent,
        "memory_used_gb": round(psutil.virtual_memory().used / (1024 ** 3), 2),
        "uptime": round(time.time() - psutil.boot_time(), 2),
        "processes": len(psutil.pids()),
    }


@router.get("/api/observer/nodes")
async def get_active_nodes():
    """Find running kolibri_node instances."""
    nodes = []
    for proc in psutil.process_iter(["pid", "name", "cmdline", "create_time"]):
        try:
            if proc.info["name"] and "kolibri_node" in proc.info["name"]:
                nodes.append({
                    "pid": proc.info["pid"],
                    "cmd": " ".join(proc.info["cmdline"] or []),
                    "uptime": time.time() - proc.info["create_time"],
                    "status": "running",
                })
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue
    return {"nodes": nodes, "count": len(nodes)}


@router.get("/api/fs/genome")
async def get_genome():
    # Read the actual knowledge base file
    genome_path = PROJECT_ROOT / "kolibri.genome"
    if genome_path.exists():
        with open(genome_path, "r", encoding="utf-8", errors="replace") as f:
            lines = list(islice(f, 100))
        return {"content": "".join(lines), "size": genome_path.stat().st_size}
    return {"content": "Genome file not found. Run 'kolibri_learn' first.", "size": 0}


@router.post("/api/terminal/exec")
async def exec_command(req: CommandRequest):
    # Security: whitelist only specific commands/binaries.
    cmd = req.cmd.strip()

    if cmd == "status":
        return {
            "output":
            "OS: Ubuntu 24.04 (Real)\n"
            f"Kernel: {subprocess.getoutput('uname -r')}\n"
            "Kolibri Core: Active"
        }

    real_cmd: list[str]
    if cmd.startswith("kolibri_gen"):
        seed = cmd.replace("kolibri_gen", "").strip()
        if not seed:
            return {"output": "Error: Seed required"}
        real_cmd = ["./kolibri_gen", seed]
    elif cmd.startswith("kolibri_learn"):
        real_cmd = ["./kolibri_learn", "raw_data.dat"]
    elif cmd == "ls":
        real_cmd = ["ls", "-la"]
    elif cmd == "pwd":
        real_cmd = ["pwd"]
    elif cmd == "whoami":
        real_cmd = ["whoami"]
    elif cmd == "uname":
        real_cmd = ["uname", "-a"]
    elif cmd == "df":
        real_cmd = ["df", "-h"]
    else:
        return {
            "output":
            f"Error: Unknown command '{cmd}'. "
            "Allowed: status, kolibri_gen, kolibri_learn, ls, pwd, whoami, uname, df"
        }

    try:
        result = subprocess.run(
            real_cmd,
            cwd=str(PROJECT_ROOT),
            capture_output=True,
            text=True,
            timeout=10,
        )
        output = result.stdout
        if result.stderr:
            output += "\n[STDERR]: " + result.stderr
        return {"output": output}
    except Exception as e:
        return {"output": f"Execution Error: {str(e)}"}


# --- Standalone FastAPI App for direct execution ---
app = FastAPI(title="Kolibri OS Bridge")
app.include_router(router)

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=3000)
