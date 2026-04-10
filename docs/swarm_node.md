# 📚 Kolibri Swarm Node — Documentation for Developers

This document describes the architecture, build process, API, and known issues of the **Kolibri Swarm Node**.

## 🏗 Architecture

The **Kolibri Swarm Node** is a standalone, lightweight HTTP server written in pure C (C99). It requires **no external dependencies** (only libc and libm) and is designed for:
1.  **Local Knowledge Storage:** Loads Q&A pairs from a text file.
2.  **Offline Q&A:** Answers queries using keyword matching.
3.  **Swarm Synchronization:** Exports/Imports knowledge to/from other nodes in NDJSON format.

### File Structure
```text
kolibri-project/
├── kolibri_swarm_node.c   # Source code (Single-file binary)
├── kolibri_swarm_new      # Compiled binary (macOS/Linux)
└── knowledge/
    └── knowledge_base.md  # Knowledge Base file
```

---

## 🛠 Build & Run

### 1. Compilation
Requires a C compiler (GCC, Clang, etc.). No build system (Make/CMake) is needed for this specific module.

```bash
cc -O2 -o kolibri_swarm_new kolibri_swarm_node.c -lm
```

### 2. Running the Node
```bash
# Basic startup on port 8002
./kolibri_swarm_new 8002

# Startup with Peer Sync (connects to another node)
./kolibri_swarm_new 8002 --peer 217.60.249.157:8001
```

---

## 📡 API Reference

### 1. Health Check
Returns node status and fact count.
*   `GET /api/v1/health`
*   **Response:**
    ```json
    {
      "status": "ok",
      "facts": 120,
      "peers": 0,
      "requests": 0
    }
    ```

### 2. Chat / Q&A
Queries the local knowledge base.
*   `POST /api/v1/ai/chat`
*   **Body:**
    ```json
    {
      "message": "Столица Франции?",
      "conversation_id": "test-123"
    }
    ```
*   **Response:**
    ```json
    {
      "response": "Париж",
      "conversation_id": "test-123",
      "method": "knowledge_base",
      "confidence": 0.95,
      "duration_ms": 0
    }
    ```

### 3. Swarm Export
Exports the entire knowledge base for synchronization.
*   `GET /api/v1/swarm/export`
*   **Response:** Stream of JSON lines (NDJSON).
    ```json
    {"q":"Столица Франции","a":"Париж"}
    {"q":"Сколько планет","a":"8 планет"}
    ```

---

## 📝 Knowledge Base Format

The node loads knowledge from `knowledge/knowledge_base.md` at startup.

**Strict Format Requirements:**
```markdown
### Q: Question Text
Answer Text (One line)
---

### Q: Second Question
Second Answer
---
```

**⚠️ Critical Rules:**
1.  Lines **must** start with `### Q:`.
2.  The **Answer** must be on the line immediately following the question.
3.  The entry **must** end with `---`.
4.  Avoid empty lines between Question and Answer.

---

## 🐛 Known Issues (Current State)

### 🔴 Critical: Crash on POST Request
*   **Symptom:** The server crashes (Segfault) or hangs when receiving a POST request to `/api/v1/ai/chat`.
*   **Cause:** The HTTP parser in `kolibri_swarm_node.c` (specifically `handle_request`) likely fails to correctly parse the request body or JSON payload. It attempts to parse the entire buffer as headers.
*   **Fix Required:** Rewrite the HTTP parser to strictly separate Headers (ending with `\r\n\r\n`) from the Body (JSON data).

### ⚠️ Missing Import Logic
*   **Status:** The node can export data (`/export`), but it **does not automatically import** data from peers upon connection.
*   **Task:** Implement a background thread or startup routine to fetch `/api/v1/swarm/export` from peers and merge the data.

### ⚠️ Naive Search
*   **Status:** Keyword matching is basic.
*   **Task:** Implement TF-IDF or weighted keyword scoring for better relevance.

---

## 🚀 Roadmap

1.  **Fix HTTP Parser:** Handle HTTP/1.1 headers and body correctly.
2.  **Implement Import:** Sync logic to pull data from peers.
3.  **Persistent Storage:** Save merged knowledge back to `knowledge_base.md` automatically.
4.  **Security:** Add API Key authentication for swarm peers.
