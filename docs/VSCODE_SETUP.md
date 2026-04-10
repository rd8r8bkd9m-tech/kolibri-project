# Kolibri VS Code Development Setup

## 📦 Installed Extensions

VS Code automatically recommends:
- **C/C++** (ms-vscode.cpptools) — C code intelligence and debugging
- **C/C++ Extension Pack** — Additional C tools
- **ESLint** — JavaScript/TypeScript linting
- **Jest Runner** — Run tests from VS Code
- **GitLens** — Git supercharged
- **Python** — Python support
- **Makefile Tools** — Makefile support
- **CMake Tools** — CMake support
- **Code Runner** — Run code snippets
- **TODO Tree** — Track TODOs

## 🚀 Quick Start

### Run All Servers
Press `Cmd+Shift+P` → `Tasks: Run Task` → `Run All Servers`

Or run in terminal:
```bash
./start_servers.sh
```

### Build C Server
Press `Cmd+Shift+P` → `Tasks: Run Task` → `Build C Server`

Or:
```bash
# Build
cc -O2 -I backend/include -I backend/include/kolibri \
   -I/opt/homebrew/opt/openssl@3/include \
   -o kolibri_http backend/src/kolibri_http_server.c \
   build/libkolibri_core.a \
   -L/opt/homebrew/opt/openssl@3/lib -lssl -lcrypto -lm -lpthread
```

### Debug C Server
Press `F5` → Select `Debug C Server`

### Debug Proxy
Press `F5` → Select `Debug Proxy (Node.js)`

## 📁 Project Structure

```
kolibri-project/
├── .vscode/
│   ├── settings.json       # C/C++ IntelliSense, formatting
│   ├── tasks.json          # Build/run tasks
│   ├── launch.json         # Debug configurations
│   └── extensions.json     # Recommended extensions
├── backend/
│   ├── src/                # C source files
│   └── include/kolibri/    # C header files
├── frontend/               # React/TypeScript frontend
├── kolibri_http            # Compiled C server
├── kolibri_proxy.js        # Node.js proxy
└── start_servers.sh        # Start all servers
```

## 🔧 Key Bindings

| Shortcut | Action |
|----------|--------|
| `Cmd+Shift+B` | Build C server |
| `F5` | Start debugging |
| `Cmd+Shift+P` → `Tasks: Run Task` | Run tasks |
| `Ctrl+`` ` | Toggle terminal |

## 🚀 Servers

| Server | Port | Purpose |
|--------|------|---------|
| C Server | 8001 | Core AI engine (C) |
| Proxy | 8003 | Smart proxy with LLM fallback |
| Frontend | 3000 | React UI (run `npm run dev` in frontend/) |

## 💡 Tips

1. **Edit C code**: IntelliSense works automatically
2. **Debug C code**: Set breakpoints, press F5
3. **Run tests**: `Tasks: Run Task` → `Test Chat`
4. **Stop servers**: `pkill -f kolibri_http; pkill -f kolibri_proxy`
