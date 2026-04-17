#!/usr/bin/env python3
"""
Kolibri Mac Hybrid Proxy — объединяет ВСЕ источники:
  1. kolibri_swarm (localhost:8002) — 614+ фактов, swarm сеть
  2. Node 1 swarm (217.60.249.157:8001) — 30K фактов
  3. kolibriai.ru — LLM через SSE streaming
  4. kolibri_http (localhost:8001) — fallback

Запуск: python3 kolibri_mac_proxy.py
Порт: 8003
"""

import httpx
import json
import asyncio
from fastapi import FastAPI, Request
from fastapi.responses import StreamingResponse, JSONResponse
from fastapi.middleware.cors import CORSMiddleware

app = FastAPI(title="Kolibri Mac Hybrid Proxy")
app.add_middleware(CORSMiddleware, allow_origins=["*"], allow_methods=["*"], allow_headers=["*"])

# ─── Источники знаний ───
SWARM_LOCAL = "http://localhost:8002"
SWARM_NODE1 = "http://217.60.249.157:8001"
KOLIBRIAI = "https://kolibriai.ru"
KOLIBRI_HTTP = "http://localhost:8001"

TIMEOUT_SWARM = 5.0
TIMEOUT_NODE1 = 10.0
TIMEOUT_KOLIBRIAI = 60.0

async def try_swarm(url: str, body: dict, timeout: float) -> dict | None:
    """Попробовать kolibri_swarm endpoint"""
    try:
        async with httpx.AsyncClient(timeout=timeout) as client:
            resp = await client.post(f"{url}/api/v1/ai/chat", json=body)
            if resp.status_code == 200:
                data = resp.json()
                ans = data.get("response", "")
                # Проверяем что это реальный ответ, а не fallback
                if ans and len(ans) > 5 and "Нет точного ответа" not in ans:
                    return data
    except Exception as e:
        print(f"  Swarm {url}: {e}")
    return None

async def kolibriai_stream(body: dict):
    """SSE streaming с kolibriai.ru"""
    async with httpx.AsyncClient(timeout=TIMEOUT_KOLIBRIAI) as client:
        async with client.stream(
            "POST",
            f"{KOLIBRIAI}/api/v1/ai/chat/stream",
            json=body,
            headers={"Content-Type": "application/json"}
        ) as resp:
            async for line in resp.aiter_lines():
                if line:
                    yield line + "\n"

@app.get("/api/v1/health")
async def health():
    """Проверить все источники"""
    sources = {}
    for name, url in [
        ("swarm_local", SWARM_LOCAL),
        ("swarm_node1", SWARM_NODE1),
        ("kolibriai_ru", KOLIBRIAI),
        ("kolibri_http", KOLIBRI_HTTP),
    ]:
        try:
            async with httpx.AsyncClient(timeout=3.0) as client:
                r = await client.get(f"{url}/api/v1/health")
                if r.status_code == 200:
                    data = r.json()
                    sources[name] = {
                        "ok": True,
                        "facts": data.get("facts", data.get("status", "")),
                        "peers": data.get("peers", 0),
                    }
                else:
                    sources[name] = {"ok": False, "status": r.status_code}
        except Exception as e:
            sources[name] = {"ok": False, "error": str(e)}
    
    return {
        "status": "kolibri_mac_hybrid",
        "sources": sources,
        "total_facts": sum(
            s.get("facts", 0) for s in sources.values()
            if isinstance(s.get("facts"), int)
        ),
    }

@app.post("/api/v1/ai/chat")
async def chat(request: Request):
    """Обычный запрос — пробуем все источники по порядку"""
    body = await request.json()
    conversation_id = body.get("conversation_id", "default")
    
    # 1. Локальный swarm (быстро, 614+ фактов)
    result = await try_swarm(SWARM_LOCAL, body, TIMEOUT_SWARM)
    if result:
        result["source"] = "swarm_mac"
        result["swarm_peers"] = await _get_swarm_peers()
        return result
    
    # 2. Node 1 swarm (30K фактов)
    result = await try_swarm(SWARM_NODE1, body, TIMEOUT_NODE1)
    if result:
        result["source"] = "swarm_node1"
        return result
    
    # 3. kolibri_http fallback
    result = await try_swarm(KOLIBRI_HTTP, body, 5.0)
    if result:
        result["source"] = "kolibri_http"
        return result
    
    # 4. kolibriai.ru — блокирующий запрос
    try:
        async with httpx.AsyncClient(timeout=TIMEOUT_KOLIBRIAI) as client:
            resp = await client.post(f"{KOLIBRIAI}/api/v1/ai/chat", json=body)
            if resp.status_code == 200:
                data = resp.json()
                data["source"] = "kolibriai_ru"
                return data
    except Exception as e:
        print(f"kolibriai.ru error: {e}")
    
    return JSONResponse(
        status_code=503,
        content={
            "response": "Нет доступных источников для ответа. Проверьте подключение к сети.",
            "source": "none"
        }
    )

@app.post("/api/v1/ai/chat/stream")
async def chat_stream(request: Request):
    """Streaming запрос — SSE от лучшего источника"""
    body = await request.json()
    
    # 1. Сначала пробуем swarm (быстро)
    result = await try_swarm(SWARM_LOCAL, body, TIMEOUT_SWARM)
    if result:
        result["source"] = "swarm_mac"
        # Отдаём как SSE
        import json as j
        async def swarm_sse():
            yield f"event: message\ndata: {j.dumps({'token': result['response'], 'done': True, 'source': 'swarm_mac'})}\n\n"
            yield f"event: done\ndata: {j.dumps({'conversation_id': body.get('conversation_id', ''), 'source': 'swarm_mac'})}\n\n"
        return StreamingResponse(swarm_sse(), media_type="text/event-stream")
    
    # 2. Node 1 swarm
    result = await try_swarm(SWARM_NODE1, body, TIMEOUT_NODE1)
    if result:
        result["source"] = "swarm_node1"
        import json as j
        async def node1_sse():
            yield f"event: message\ndata: {j.dumps({'token': result['response'], 'done': True, 'source': 'swarm_node1'})}\n\n"
            yield f"event: done\ndata: {j.dumps({'conversation_id': body.get('conversation_id', ''), 'source': 'swarm_node1'})}\n\n"
        return StreamingResponse(node1_sse(), media_type="text/event-stream")
    
    # 3. kolibri.ru SSE streaming — проксируем напрямую
    return StreamingResponse(
        kolibriai_stream(body),
        media_type="text/event-stream",
        headers={
            "Cache-Control": "no-cache",
            "X-Accel-Buffering": "no",
            "X-Source": "kolibriai_ru",
        }
    )

async def _get_swarm_peers() -> dict:
    try:
        async with httpx.AsyncClient(timeout=3.0) as client:
            r = await client.get(f"{SWARM_LOCAL}/api/v1/swarm/peers")
            return r.json() if r.status_code == 200 else {}
    except:
        return {}

@app.get("/api/v1/swarm/status")
async def swarm_status():
    return await health()

# Passthrough для остальных эндпоинтов
@app.api_route("/api/v1/{path:path}", methods=["GET", "POST", "PUT", "DELETE"])
async def passthrough(path: str, request: Request):
    body = None
    try:
        body = await request.json()
    except:
        pass
    
    try:
        async with httpx.AsyncClient(timeout=30.0) as client:
            url = f"{KOLIBRIAI}/api/v1/{path}"
            if request.method == "GET":
                resp = await client.get(url, params=dict(request.query_params))
            else:
                resp = await client.request(request.method, url, json=body)
            return JSONResponse(
                content=resp.json() if resp.content else {},
                status_code=resp.status_code
            )
    except Exception as e:
        return JSONResponse(content={"error": str(e)}, status_code=502)

if __name__ == "__main__":
    import uvicorn
    print("🐦 Kolibri Mac Hybrid Proxy — все источники в одной сети")
    print(f"  Swarm Local: {SWARM_LOCAL}")
    print(f"  Swarm Node1: {SWARM_NODE1}")
    print(f"  KolibriAI:   {KOLIBRIAI}")
    print(f"  KolibriHTTP: {KOLIBRI_HTTP}")
    print(f"  Listening on :8003\n")
    uvicorn.run(app, host="0.0.0.0", port=8003)
