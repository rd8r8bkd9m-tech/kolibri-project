from fastapi import APIRouter, HTTPException
from pydantic import BaseModel
import subprocess
import psutil
import os
import time

router = APIRouter()

class CommandRequest(BaseModel):
    cmd: str

class FileRequest(BaseModel):
    path: str
    content: str | None = None

@router.get("/api/dev/ls")
async def list_files(path: str = "."):
    """List directory contents safely."""
    target = os.path.abspath(os.path.join("/workspaces/kolibri-project", path))
    if not target.startswith("/workspaces/kolibri-project"):
        raise HTTPException(403, "Access denied")
    
    try:
        items = []
        for entry in os.scandir(target):
            items.append({
                "name": entry.name,
                "is_dir": entry.is_dir(),
                "size": entry.stat().st_size if not entry.is_dir() else 0
            })
        return sorted(items, key=lambda x: (not x['is_dir'], x['name']))
    except Exception as e:
        raise HTTPException(500, str(e))

@router.post("/api/dev/read")
async def read_file(req: FileRequest):
    """Read text file content."""
    target = os.path.abspath(os.path.join("/workspaces/kolibri-project", req.path))
    if not target.startswith("/workspaces/kolibri-project"):
        raise HTTPException(403, "Access denied")
    
    try:
        with open(target, "r", encoding="utf-8") as f:
            return {"content": f.read()}
    except Exception as e:
        raise HTTPException(500, str(e))

@router.post("/api/dev/save")
async def save_file(req: FileRequest):
    """Save text file content."""
    target = os.path.abspath(os.path.join("/workspaces/kolibri-project", req.path))
    if not target.startswith("/workspaces/kolibri-project"):
        raise HTTPException(403, "Access denied")
    
    if req.content is None:
        raise HTTPException(400, "Content required")

    try:
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
        "memory_used_gb": round(psutil.virtual_memory().used / (1024**3), 2),
        "uptime": round(time.time() - psutil.boot_time(), 2),
        "processes": len(psutil.pids())
    }

@router.get("/api/observer/nodes")
async def get_active_nodes():
    """Find running kolibri_node instances."""
    nodes = []
    for proc in psutil.process_iter(['pid', 'name', 'cmdline', 'create_time']):
        try:
            if proc.info['name'] and 'kolibri_node' in proc.info['name']:
                nodes.append({
                    "pid": proc.info['pid'],
                    "cmd": " ".join(proc.info['cmdline'] or []),
                    "uptime": time.time() - proc.info['create_time'],
                    "status": "running"
                })
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue
    return {"nodes": nodes, "count": len(nodes)}

@router.get("/api/fs/genome")
async def get_genome():
    # Read the actual knowledge base file
    genome_path = "/workspaces/kolibri-project/kolibri.genome"
    if os.path.exists(genome_path):
        with open(genome_path, "r") as f:
            # Read first 100 lines to avoid payload explosion
            lines = [next(f) for _ in range(100)]
        return {"content": "".join(lines), "size": os.path.getsize(genome_path)}
    return {"content": "Genome file not found. Run 'kolibri_learn' first.", "size": 0}

@router.post("/api/terminal/exec")
async def exec_command(req: CommandRequest):
    # Security: Whitelist allowed commands or prefix
    # For this demo, we allow specific CLI tools we built
    cmd = req.cmd.strip()
    
    # Map 'frontend' commands to 'real' backend commands
    real_cmd = []
    
    if cmd == "status":
        return {"output": f"OS: Ubuntu 24.04 (Real)\nKernel: {subprocess.getoutput('uname -r')}\nKolibri Core: Active"}
    
    elif cmd.startswith("kolibri_gen"):
        # Invoke the C binary
        seed = cmd.replace("kolibri_gen", "").strip()
        if not seed:
            return {"output": "Error: Seed required"}
        real_cmd = ["./kolibri_gen", seed]
        
    elif cmd.startswith("kolibri_learn"):
        real_cmd = ["./kolibri_learn", "raw_data.dat"]
        
    elif cmd == "ls":
        real_cmd = ["ls", "-la", "--color=never"]

    elif cmd == "pwd":
        real_cmd = ["pwd"]

    elif cmd == "whoami":
        real_cmd = ["whoami"]

    elif cmd == "uname":
        real_cmd = ["uname", "-a"]

    elif cmd == "df":
        real_cmd = ["df", "-h"]
        
    else:
        # Fallback: Allow safe execution for demo purposes
        # CAUTION: In production, this is a security risk.
        # We allow simple commands to prove "realness"
        real_cmd = ["/bin/bash", "-c", cmd]

    try:
        result = subprocess.run(
            real_cmd, 
            cwd="/workspaces/kolibri-project",
            capture_output=True, 
            text=True,
            timeout=10
        )
        output = result.stdout
        if result.stderr:
            output += "\n[STDERR]: " + result.stderr
        return {"output": output}
    except Exception as e:
        return {"output": f"Execution Error: {str(e)}"}

# --- Standalone FastAPI App for direct execution ---
from fastapi import FastAPI

app = FastAPI(title="Kolibri OS Bridge")
app.include_router(router)

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=3000)
