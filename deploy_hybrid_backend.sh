#!/bin/bash
# ═══════════════════════════════════════════════════════════════
#  KOLIBRI HYBRID BACKEND — Python + Swarm
#  kolibriai.ru отвечает через ОБА движка:
#    1. Python AI (uvicorn :8001) — LLM-style ответы
#    2. Kolibri Swarm (:8002) — точные факты 30K+
#  Маршрутизация через Nginx:
#    /api/v1/ai/chat → swarm → python (fallback)
# ═══════════════════════════════════════════════════════════════

set -e
KEY="$HOME/.ssh/id_ed25519"
N2="178.207.11.90"
N2_PORT="2222"
N2_USER="ladik"

echo "╔══════════════════════════════════════════════════════╗"
echo "║   KOLIBRI HYBRID BACKEND — Python + Swarm           ║"
echo "╚══════════════════════════════════════════════════════╝"

# ─── 1. Ensure kolibri_swarm is running on Node 2 ───
echo ""
echo "=== Step 1: kolibri_swarm on Node 2 (:8002) ==="

ssh -i $KEY -p $N2_PORT $N2_USER@$N2 '
    # Ensure binary and knowledge exist
    ls ~/kolibri/kolibri_swarm ~/kolibri/knowledge/knowledge_base.md 2>/dev/null || {
        echo "⚠️  Missing files on Node 2"
        exit 1
    }
    
    pkill -f "kolibri_swarm 8002" 2>/dev/null || true
    sleep 1
    
    cd ~/kolibri
    nohup ./kolibri_swarm 8002 --peer 217.60.249.157:8001 > node2.log 2>&1 &
    echo "Swarm PID: $!"
'

sleep 8

echo ""
echo "Swarm Health:"
ssh -i $KEY -p $N2_PORT $N2_USER@$N2 'curl -s -m 5 http://localhost:8002/api/v1/health' 2>/dev/null | python3 -m json.tool 2>/dev/null || \
ssh -i $KEY -p $N2_PORT $N2_USER@$N2 'curl -s -m 5 http://localhost:8002/api/v1/health'

# ─── 2. Create hybrid proxy script ───
echo ""
echo "=== Step 2: Hybrid proxy (swarm → python fallback) ==="

# Create the hybrid proxy on Node 2
ssh -i $KEY -p $N2_PORT $N2_USER@$N2 '
mkdir -p ~/kolibri-hybrid
cat > ~/kolibri-hybrid/proxy.py << "PYEOF"
"""
Hybrid Kolibri Backend — kolibriai.ru
Routes: 
  1. kolibri_swarm (:8002) — fast factual answers
  2. Python uvicorn (:8001) — LLM fallback
  3. kolibri_http on Mac — another fallback
"""
import httpx
import json
from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse
from fastapi.middleware.cors import CORSMiddleware

app = FastAPI()
app.add_middleware(CORSMiddleware, allow_origins=["*"], allow_methods=["*"], allow_headers=["*"])

SWARM_URL = "http://localhost:8002"
PYTHON_URL = "http://localhost:8001"
# Mac URL via reverse tunnel if available
MAC_URL = "http://217.60.249.157:8001"

async def try_swarm(body: dict) -> dict | None:
    """Try kolibri_swarm first"""
    try:
        async with httpx.AsyncClient(timeout=5.0) as client:
            resp = await client.post(f"{SWARM_URL}/api/v1/ai/chat", json=body)
            if resp.status_code == 200:
                data = resp.json()
                ans = data.get("response", "")
                # Check if it is a real answer or "no answer" fallback
                if ans and "Нет точного ответа" not in ans:
                    data["source"] = "kolibri_swarm"
                    data["swarm_facts"] = await get_swarm_facts()
                    return data
    except Exception as e:
        print(f"Swarm error: {e}")
    return None

async def try_python(body: dict) -> dict | None:
    """Fallback to Python AI backend"""
    try:
        async with httpx.AsyncClient(timeout=30.0) as client:
            resp = await client.post(f"{PYTHON_URL}/api/v1/ai/chat", json=body)
            if resp.status_code == 200:
                data = resp.json()
                data["source"] = "python_ai"
                return data
    except Exception as e:
        print(f"Python error: {e}")
    return None

async def try_node1(body: dict) -> dict | None:
    """Fallback to Node 1 swarm"""
    try:
        async with httpx.AsyncClient(timeout=10.0) as client:
            resp = await client.post(f"{MAC_URL}/api/v1/ai/chat", json=body)
            if resp.status_code == 200:
                data = resp.json()
                data["source"] = "node1_swarm"
                return data
    except Exception as e:
        print(f"Node 1 error: {e}")
    return None

async def get_swarm_facts() -> dict:
    try:
        async with httpx.AsyncClient(timeout=3.0) as client:
            resp = await client.get(f"{SWARM_URL}/api/v1/health")
            return resp.json() if resp.status_code == 200 else {}
    except:
        return {}

@app.get("/api/v1/health")
async def health():
    swarm_ok = False
    python_ok = False
    node1_ok = False
    
    try:
        async with httpx.AsyncClient(timeout=3.0) as client:
            r = await client.get(f"{SWARM_URL}/api/v1/health")
            swarm_ok = r.status_code == 200
            swarm_data = r.json() if swarm_ok else {}
    except:
        swarm_data = {}
    
    try:
        async with httpx.AsyncClient(timeout=3.0) as client:
            r = await client.get(f"{PYTHON_URL}/api/v1/health")
            python_ok = r.status_code == 200
    except:
        pass
    
    try:
        async with httpx.AsyncClient(timeout=5.0) as client:
            r = await client.get(f"{MAC_URL}/api/v1/health")
            node1_ok = r.status_code == 200
            node1_data = r.json() if node1_ok else {}
    except:
        node1_data = {}
    
    return {
        "status": "hybrid",
        "swarm": swarm_ok,
        "python_ai": python_ok,
        "node1_swarm": node1_ok,
        "swarm_facts": swarm_data.get("facts", 0),
        "node1_facts": node1_data.get("facts", 0),
    }

@app.post("/api/v1/ai/chat")
@app.post("/api/v1/ai/chat/stream")
async def chat(request: Request):
    body = await request.json()
    
    # 1. Try swarm (fast, factual)
    result = await try_swarm(body)
    if result:
        return result
    
    # 2. Try Node 1 swarm (more facts)
    result = await try_node1(body)
    if result:
        return result
    
    # 3. Fallback to Python AI
    result = await try_python(body)
    if result:
        return result
    
    return JSONResponse(
        status_code=503,
        content={"response": "Нет доступных движков для ответа.", "source": "none"}
    )

# Pass through other endpoints to Python
@app.api_route("/api/v1/{path:path}", methods=["GET", "POST", "PUT", "DELETE"])
async def passthrough(path: str, request: Request):
    """Forward all other API calls to Python backend"""
    body = None
    try:
        body = await request.json()
    except:
        pass
    
    try:
        async with httpx.AsyncClient(timeout=30.0) as client:
            method = request.method
            url = f"{PYTHON_URL}/api/v1/{path}"
            
            if method == "GET":
                resp = await client.get(url, params=dict(request.query_params))
            elif method == "POST":
                resp = await client.post(url, json=body)
            else:
                resp = await client.request(method, url, json=body)
            
            return JSONResponse(content=resp.json() if resp.content else {}, status_code=resp.status_code)
    except Exception as e:
        return JSONResponse(content={"error": str(e)}, status_code=502)

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="127.0.0.1", port=8003)
PYEOF

echo "Hybrid proxy script created"
ls -la ~/kolibri-hybrid/proxy.py
'

# ─── 3. Install dependencies & start hybrid proxy ───
echo ""
echo "=== Step 3: Install dependencies & start ==="

ssh -i $KEY -p $N2_PORT $N2_USER@$N2 '
    # Check if fastapi/httpx already installed
    python3 -c "import fastapi, httpx" 2>/dev/null || {
        echo "Installing fastapi httpx..."
        pip3 install fastapi httpx uvicorn 2>&1 | tail -3
    }
    
    pkill -f "proxy.py" 2>/dev/null || true
    sleep 1
    
    cd ~/kolibri-hybrid
    nohup python3 proxy.py > hybrid.log 2>&1 &
    echo "Hybrid PID: $!"
'

sleep 6

# ─── 4. Test hybrid backend ───
echo ""
echo "=== Step 4: Test hybrid backend (:8003) ==="

echo "Health:"
ssh -i $KEY -p $N2_PORT $N2_USER@$N2 'curl -s -m 5 http://localhost:8003/api/v1/health' 2>/dev/null | python3 -m json.tool 2>/dev/null || \
ssh -i $KEY -p $N2_PORT $N2_USER@$N2 'curl -s -m 5 http://localhost:8003/api/v1/health'

echo ""
echo "Test factual (should come from swarm):"
ssh -i $KEY -p $N2_PORT $N2_USER@$N2 '
    curl -s -X POST http://localhost:8003/api/v1/ai/chat \
      -d "{\"message\":\"Столица Австралии\",\"conversation_id\":\"t\"}" \
      -H "Content-Type: application/json" | python3 -c "
import sys,json
d=json.load(sys.stdin)
print(f\"Source: {d.get(\"source\",\"?\")}\")
print(f\"Answer: {d.get(\"response\",\"?\")[:80]}\")
" 2>/dev/null || echo "ERR"
'

echo ""
echo "Test AI (should fallback to Python):"
ssh -i $KEY -p $N2_PORT $N2_USER@$N2 '
    curl -s -X POST http://localhost:8003/api/v1/ai/chat \
      -d "{\"message\":\"Напиши стих про Москву\",\"conversation_id\":\"t\"}" \
      -H "Content-Type: application/json" | python3 -c "
import sys,json
d=json.load(sys.stdin)
print(f\"Source: {d.get(\"source\",\"?\")}\")
print(f\"Answer: {d.get(\"response\",\"?\")[:80]}\")
" 2>/dev/null || echo "ERR"
'

echo ""
echo "═══════════════════════════════════════════════════════"
echo "  HYBRID BACKEND READY"
echo "═══════════════════════════════════════════════════════"
echo ""
echo "  kolibriai.ru → Nginx → Hybrid Proxy (:8003)"
echo "    ↓"
echo "    1. kolibri_swarm (:8002) — факты 30K+"
echo "    2. Node 1 swarm (217.60.249.157:8001) — 20K фактов"
echo "    3. Python AI (:8001) — LLM fallback"
echo ""
