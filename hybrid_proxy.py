"""
Hybrid Kolibri Backend — kolibriai.ru
Routes: 
  1. kolibri_swarm (:8002) — fast factual answers
  2. Node 1 swarm (:8001 on 217.60.249.157) — 30K facts
  3. Python uvicorn (:8001) — LLM fallback
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
NODE1_URL = "http://217.60.249.157:8001"

async def try_endpoint(url: str, body: dict, timeout: float) -> dict | None:
    try:
        async with httpx.AsyncClient(timeout=timeout) as client:
            resp = await client.post(f"{url}/api/v1/ai/chat", json=body)
            if resp.status_code == 200:
                data = resp.json()
                ans = data.get("response", "")
                if ans and "Нет точного ответа" not in ans:
                    return data
    except:
        pass
    return None

@app.get("/api/v1/health")
async def health():
    result = {}
    for name, url in [("swarm", SWARM_URL), ("python", PYTHON_URL), ("node1", NODE1_URL)]:
        try:
            async with httpx.AsyncClient(timeout=3.0) as client:
                r = await client.get(f"{url}/api/v1/health")
                result[name] = r.json() if r.status_code == 200 else {"ok": False}
        except:
            result[name] = {"ok": False}
    return result

@app.post("/api/v1/ai/chat")
@app.post("/api/v1/ai/chat/stream")
async def chat(request: Request):
    body = await request.json()
    
    # 1. Try local swarm (fast, factual)
    result = await try_endpoint(SWARM_URL, body, timeout=5.0)
    if result:
        result["source"] = "kolibri_swarm"
        return result
    
    # 2. Try Node 1 swarm (30K facts)
    result = await try_endpoint(NODE1_URL, body, timeout=10.0)
    if result:
        result["source"] = "node1_swarm"
        return result
    
    # 3. Fallback to Python AI
    result = await try_endpoint(PYTHON_URL, body, timeout=30.0)
    if result:
        result["source"] = "python_ai"
        return result
    
    return JSONResponse(
        status_code=503,
        content={"response": "Нет доступных движков для ответа.", "source": "none"}
    )

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
