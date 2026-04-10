# Kolibri Project — QWEN Context

## Project Overview

**Kolibri** — an AI system with numeric thinking and a KLM knowledge base. Combines a C-core reasoning engine with a React/TypeScript chat frontend, deployed via automated `./start.sh`.

### Architecture Layers

| Layer | Technology | Port | Purpose |
|-------|-----------|------|---------|
| **C HTTP Server** | `kolibri_http` (C23) | `:8001` | REST API: chat, health, reasoning, math solver, corpus trainer, formula pool |
| **Swarm Node** | `kolibri_swarm_mac` (C) | `:8002` | Lightweight knowledge node with Q&A KB, peer sync to Node 1 (217.60.249.157:8001) |
| **Hybrid Proxy** | `kolibri_mac_proxy.js` (Node.js) | `:8003` | Routes: swarm → remote Node1 → kolibriai.ru (LLM fallback) |
| **Frontend** | React 18, TypeScript, Vite | `:3000` | Chat UI with workspace drawer |

### Key Directories

| Directory | Contents |
|-----------|----------|
| `backend/src/` | C modules (60+): reasoning_engine, math_solver, world_model, corpus_trainer, formula, etc. |
| `backend/include/kolibri/` | 53 C header files |
| `frontend/src/` | React app: api.ts, App.tsx, features/, store/, types/, hooks/ |
| `knowledge/` | Knowledge bases: `knowledge_base_qa.md` (120 Q&A), swarm shards |
| `docs/` | Documentation: swarm_node.md, plans/ |

---

## Building and Running

### One-Command Startup

```bash
./start.sh
```

This automatically:
1. Compiles WASM if missing
2. Compiles `kolibri_http` C server (fast startup with inline facts)
3. Starts `kolibri_http` on `:8001`
4. Starts `kolibri_swarm_mac` on `:8002` (swarms with Node 1)
5. Starts `kolibri_mac_proxy.js` on `:8003` (hybrid routing)
6. Starts Vite dev server on `:3000`

### Manual Commands

**Recompile C server:**
```bash
cc -O2 -I backend/include -I backend/include/kolibri \
   -I/opt/homebrew/opt/openssl@3/include \
   -o kolibri_http backend/src/kolibri_http_server.c \
   build/libkolibri_core.a \
   -L/opt/homebrew/opt/openssl@3/lib -lssl -lcrypto -lm -lpthread
```

**TypeScript check:**
```bash
cd frontend && npx tsc --noEmit
```

**Frontend dev server:**
```bash
cd frontend && npm run dev
```

---

## API Endpoints

### C HTTP Server (`:8001`)

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/v1/health` | GET | `{"status":"ok","backend":"C-core"}` |
| `/api/v1/ai/chat` | POST | `{"message":"...","conversation_id":"..."}` → `{"response":"...","method":"...","confidence":0.9}` |
| `/api/v1/swarm/export` | GET | NDJSON stream of Q&A facts |

### Hybrid Proxy (`:8003`)

Routes `/api/v1/ai/chat` → tries swarm → remote Node1 → kolibriai.ru fallback. Maps C node response shape to frontend `ChatApiResponse` format.

### Frontend (`:3000`)

Vite proxies all `/api/*` requests to `:8003`.

---

## Development Conventions

### C Code
- **Standard**: C23
- **Headers**: `backend/include/kolibri/`
- **Naming**: `kolibri_` prefix for public, `k_`/`kf_` for module-specific
- **Fast startup**: No large file I/O at startup; use inline facts

### TypeScript/Frontend
- **Framework**: React 18 + TypeScript
- **State**: Zustand (`store/`)
- **Data fetching**: React Query
- **Styling**: Tailwind CSS + Radix UI

### Lightweight Mode Detection
`isLightweightSwarmNode()` in `WorkspaceDrawerV3.tsx` detects C node. When detected:
- Hides tabs: packs, teach, quality, knowledge, learning
- Shows simplified view: status, facts, peers
- API fallbacks return zeroed defaults for missing endpoints

---

## Knowledge Base Format

```markdown
### Q: Question text?
Answer text (one line)
---
```

Key files:
- `knowledge/knowledge_base_qa.md` — 120 Q&A facts (14KB)
- `knowledge/swarm/node1_knowledge.md` — Node 1 shard
- `knowledge/swarm/node2_knowledge.md` — Node 2 shard

---

## Current Status

### Working
- ✅ Fast C HTTP server startup (no large file loading)
- ✅ Chat returns correct answers from inline knowledge base
- ✅ Swarm node peers with Node 1 (remote)
- ✅ Hybrid proxy routing with LLM fallback
- ✅ Frontend with lightweight mode detection
- ✅ Background auto-learning thread

### Known Limitations
- Swarm sync is NDJSON-only (no `.kpack` support on C node)
- Learning dashboard hidden in lightweight mode
- Knowledge graph returns empty on C node
