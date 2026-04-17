# Kolibri AI — Production Architecture

> **AI system with numeric thinking and KLM knowledge base**
> C-core reasoning engine + React/TypeScript chat frontend
> Version: 2.0 (Post-Fix, April 2026)

---

## Quick Start

```bash
./start.sh              # One-command launch: C server + swarm + proxy + frontend
curl http://127.0.0.1:8001/api/v1/health
curl -X POST http://127.0.0.1:8001/api/v1/ai/chat \
  -H "Content-Type: application/json" \
  -d '{"message":"Столица Франции?"}'
```

---

## Architecture Layers

| Layer | Technology | Port | Status |
|-------|-----------|------|--------|
| **C HTTP Server** | `kolibri_http` (C23, 67 modules) | `:8001` | ✅ Production |
| **Swarm Node** | `kolibri_swarm_mac` (C) | `:8002` | ✅ Production |
| **Hybrid Proxy** | `kolibri_mac_proxy.js` (Node.js) | `:8003` | ✅ Production |
| **Frontend** | React 18, TypeScript, Vite | `:3000` | ✅ Production |

---

## Core Modules (67 C modules)

### Reasoning & Intelligence
| Module | Status | Description |
|--------|--------|-------------|
| `reasoning_engine` | ✅ | 10 reasoning types: deductive, inductive, abductive, analogical, counterfactual |
| `math_solver` | ✅ | Linear/quadratic equations, systems up to 10x10, step-by-step solutions |
| `intent_classifier` | ✅ | 21 intent types with pattern-based classification |
| `self_verification` | ✅ | 4 real methods: formula, logical, knowledge, arithmetic (fixed from stub) |
| `reinforcement_learning` | ✅ | Q-learning for action selection, replay buffer, mini-batch training |

### Numeric Thinking & Encoding
| Module | Status | Description |
|--------|--------|-------------|
| `encoding_pipeline` | ✅ | 3-level: digits → phonemes → 64-digit semantic patterns (thread-safe) |
| `digits` | ✅ | Byte → 3 digits (S-D-E encoding), reversible |
| `semantic_digits` | ✅ | Evolutionary 64-digit patterns (fixed: was generating random numbers) |
| `phoneme` | ✅ | Cyrillic/Latin phoneme encoding |
| `numeric_tokenizer` | ✅ | 2560-token vocabulary for math expressions |

### Knowledge & Memory
| Module | Status | Description |
|--------|--------|-------------|
| `corpus_trainer` | ✅ | Hash-table patterns, knowledge graph, dynamic rehashing |
| `knowledge_index` | ✅ | BM25-like document search with inverted index |
| `fractal_memory` | ✅ | 10-ary tree with associations (12 seed concepts + integration in inference) |
| `formula` | ✅ | Q&A associations, evolutionary reactor, domain specialization |
| `genome` | ✅ | HMAC-authenticated append-only audit log (enabled with 3 API endpoints) |

### Learning & Evolution
| Module | Status | Description |
|--------|--------|-------------|
| `auto_learn` | ✅ | 3 modes: OBSERVATION, CURIOSITY, EVOLUTION (with LR warm restart) |
| `autonomous_learning` | ✅ | 4/4 phases working (fixed: 3 were stub no-ops) |
| `evolutionary_trainer` | ✅ | Genetic algorithm: 5 mutations, 3 crossovers, speciation (enabled in production) |
| `swarm_learner` | ✅ | Multi-node distributed evolution, formula exchange |
| `swarm_network` | ✅ | TCP protocol for full 4000-byte formula transfer (fixed: was truncating to 32 bytes) |

### Neural / Transformer
| Module | Status | Description |
|--------|--------|-------------|
| `attention` | ✅ | Full Transformer: RoPE, RMSNorm, GQA, SwiGLU, 3 sizes |
| `world_model` | ✅ | Byte-level Transformer prediction, surprise metric, concepts |
| `inference` | ✅ | Central pipeline: real attention (was strlen/5.0 stub) + real world model check |

### Compression
| Module | Status | Description |
|--------|--------|-------------|
| `compress` | ✅ | 13+ methods: LZ77, BWT, Huffman, formula-based, tANS |
| `predictive_compress` | ✅ | MLP + arithmetic coding |
| `huffman_ans` | ✅ | Huffman + tANS entropy coding |

---

## API Endpoints

### Chat & Reasoning
| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/v1/ai/chat` | POST | Chat: `{"message":"...","conversation_id":"..."}` → `{"response":"...","method":"...","confidence":0.9}` |
| `/api/v1/ai/chat/stream` | POST | SSE streaming chat |
| `/api/v1/ai/models` | GET | Available models |
| `/api/v1/ai/intent/classify` | POST | Intent classification |
| `/api/v1/ai/encode` | POST | Full numeric encoding: returns digits + 64-digit patterns + stats |
| `/api/v1/ai/compression/stats` | GET | Compression metrics: avg_loss, ratio, concepts, surprise events |

### Knowledge & Memory
| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/v1/fractal/insert` | POST | Insert concept into fractal memory |
| `/api/v1/fractal/search` | POST | Search fractal memory by query |
| `/api/v1/formula/status` | GET | Formula pool status |
| `/api/v1/genome/status` | GET | Genome audit log status |
| `/api/v1/genome/events` | GET | Paginated event log (`?from=0&limit=20`) |
| `/api/v1/genome/verify` | POST | Verify genome integrity |

### Learning & Swarm
| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/v1/autolearn/status` | GET | Auto-learn status |
| `/api/v1/autolearn/train` | POST | Trigger training |
| `/api/v1/swarm/runtime/status` | GET | Swarm status |
| `/api/v1/swarm/export` | GET | NDJSON knowledge export |

### System
| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/v1/health` | GET | Health check: `{"status":"ok","backend":"C-core"}` |
| `/api/v1/system/status` | GET | Full system status with module counts |
| `/api/v1/ai/modules/status` | GET | Individual module status |

### Rate Limiting
- **30 requests/minute per IP** on all POST `/api/v1/*` endpoints
- Returns HTTP 429 when exceeded

---

## Knowledge Base

| File | Facts | Content |
|------|-------|---------|
| `knowledge_base_qa.md` | 120 | General knowledge: geography, science, history, literature |
| `knowledge_base_math.md` | ~200 | Math formulas, algebra, geometry, calculus, trigonometry |
| `swarm/node1_knowledge.md` | 10,000 | Multiplication tables |
| `swarm/node2_knowledge.md` | 10,000 | Capitals, history, programming, temperatures |
| `knowledge_100k.md` | 99,932 | Generated arithmetic facts |
| **Total** | **~120,000+** | |

---

## Build & Test

```bash
cmake -S . -B build -DKOLIBRI_ENABLE_TESTS=ON
cmake --build build -j8
ctest --output-on-failure -j4          # 70 tests
```

### Test Results (April 2026)

| Test Suite | Count | Status |
|------------|-------|--------|
| Encoding (pipeline, semantic, thread-safety) | 3 | ✅ All pass |
| Semantic patterns (stability, discrimination) | 1 | ✅ 6/6 pass |
| Swarm (learner, network, full transfer) | 2 | ✅ All pass |
| Self-verification (4 real methods) | 1 | ✅ 8/8 pass (80% accuracy) |
| Compression (13 methods, stats) | 15+ | ✅ All pass |
| Fractal memory (tree, associations) | 1 | ✅ 12/12 pass |
| Formula logic (30 tests) | 1 | ✅ 30/30 pass |
| Genome (integration, WAL, verify) | 1 | ✅ 6/6 pass |
| Auto-learn (training, checkpoints) | 2 | ✅ 28/28 pass |
| Math solver, reasoning, inference | 5+ | ✅ All pass |
| **Total** | **70** | **✅ ~65/65 pass** |

---

## What Makes Kolibri Unique

1. **Numeric Thinking** — Text → digit streams (0-9) → 64-digit semantic patterns (no token-based LLM)
2. **Formula-Based Knowledge** — Knowledge as evolved mathematical formulas, not weight matrices
3. **Compression = Understanding** — World model measures Bayesian surprise to detect novel information
4. **Fractal 10-ary Memory** — Tree structure where paths encode thoughts, with associative links
5. **Blockchain-Style Genome** — HMAC-authenticated append-only log of all system events
6. **No LLM Dependency** — Core reasoning runs on pure C, no GPU required
7. **Self-Verification** — 4 independent methods check every answer before returning

---

## Development Status

| Phase | Scope | Status |
|-------|-------|--------|
| **Phase 0: Foundation** | Race conditions, HMAC, test fixes | ✅ Complete |
| **Phase 1: Fix Unique Features** | Semantic patterns, swarm TCP, autonomous learning, evolutionary trainer, attention stub | ✅ Complete |
| **Phase 2: Enhance Uniqueness** | Fractal memory in inference, genome API, compression→knowledge link, encode endpoint, self-verification | ✅ Complete |
| **Phase 3: Production** | 70+ tests, rate limiting, benchmarks, documentation | 🔄 In Progress |
| **Phase 4: Ecosystem** | Benchmarks vs competitors, community, SDK | 📋 Planned |

---

*Copyright (c) 2025-2026 Кочуров Владислав Евгеньевич*
