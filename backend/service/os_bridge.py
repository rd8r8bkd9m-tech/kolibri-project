import os
import subprocess
import time

import psutil
from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel

app = FastAPI()

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


class CommandRequest(BaseModel):
    cmd: str


@app.get("/api/system/stats")
async def get_stats():
    # Real OS metrics
    return {
        "cpu": psutil.cpu_percent(interval=None),
        "memory": psutil.virtual_memory().percent,
        "memory_used_gb": round(psutil.virtual_memory().used / (1024**3), 2),
        "uptime": round(time.time() - psutil.boot_time(), 2),
        "processes": len(psutil.pids()),
    }


@app.get("/api/fs/genome")
async def get_genome():
    # Read the actual knowledge base file
    genome_path = "/workspaces/kolibri-project/kolibri.genome"
    if os.path.exists(genome_path):
        with open(genome_path, "r") as f:
            # Read first 100 lines to avoid payload explosion
            lines = [next(f) for _ in range(100)]
        return {"content": "".join(lines), "size": os.path.getsize(genome_path)}
    return {"content": "Genome file not found. Run 'kolibri_learn' first.", "size": 0}


@app.post("/api/terminal/exec")
async def exec_command(req: CommandRequest):
    # Security: Whitelist allowed commands or prefix
    # For this demo, we allow specific CLI tools we built
    cmd = req.cmd.strip()

    # Map 'frontend' commands to 'real' backend commands
    real_cmd = []

    if cmd == "status":
        return {
            "output": f"OS: Ubuntu 24.04 (Real)\nKernel: {subprocess.getoutput('uname -r')}\nKolibri Core: Active"
        }

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
            timeout=10,
        )
        output = result.stdout
        if result.stderr:
            output += "\n[STDERR]: " + result.stderr
        return {"output": output}
    except Exception as e:
        return {"output": f"Execution Error: {str(e)}"}


if __name__ == "__main__":
    import uvicorn

    uvicorn.run(app, host="0.0.0.0", port=3000)
