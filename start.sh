#!/bin/bash
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
FE="$ROOT/frontend"

# Kill old
pkill -f kolibri_http 2>/dev/null || true
pkill -f vite 2>/dev/null || true
sleep 1

# Compile WASM if missing
if [ ! -f "$FE/public/kolibri.wasm" ]; then
    echo "Compiling WASM..."
    cd "$ROOT/build" 2>/dev/null || { mkdir -p "$ROOT/build"; cd "$ROOT/build"; cmake .. -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1; make -j4 kolibri_core >/dev/null 2>&1; }
    cd "$ROOT"
    emcc -O2 -I backend/include -I backend/include/kolibri \
        wasm/kolibri_core_wasm.c build/libkolibri_core.a \
        -s EXPORT_ALL=1 -s ALLOW_MEMORY_GROWTH=1 -s ERROR_ON_UNDEFINED_SYMBOLS=0 \
        -o "$FE/public/kolibri.wasm" -lm 2>/dev/null
fi

# Compile C HTTP if missing or source is newer
HTTP_NEED_BUILD=0
if [ ! -f "$ROOT/kolibri_http" ]; then
    HTTP_NEED_BUILD=1
elif [ "$ROOT/backend/src/kolibri_http_server.c" -nt "$ROOT/kolibri_http" ]; then
    HTTP_NEED_BUILD=1
fi

if [ $HTTP_NEED_BUILD -eq 1 ]; then
    echo "Compiling C HTTP..."
    # Build kolibri_core if library is missing
    if [ ! -f "$ROOT/build/libkolibri_core.a" ]; then
        cd "$ROOT/build" 2>/dev/null || { mkdir -p "$ROOT/build"; cd "$ROOT/build"; cmake .. -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1; }
        cmake --build "$ROOT/build" --target kolibri_core -j4 >/dev/null 2>&1
    fi
    # Find OpenSSL
    OPENSSL_INC=""
    OPENSSL_LIB=""
    for p in /opt/homebrew/opt/openssl@3 /opt/homebrew/opt/openssl /usr/local/opt/openssl; do
        if [ -d "$p/include" ]; then
            OPENSSL_INC="-I$p/include"
            OPENSSL_LIB="-L$p/lib -lssl -lcrypto"
            break
        fi
    done
    cc -O2 -I "$ROOT/backend/include" -I "$ROOT/backend/include/kolibri" $OPENSSL_INC \
        -o "$ROOT/kolibri_http" \
        "$ROOT/backend/src/kolibri_http_server.c" "$ROOT/build/libkolibri_core.a" \
        $OPENSSL_LIB -lm -lpthread
fi

# Start backend
cd "$ROOT" && ./kolibri_http 8001 &
BPID=$!
sleep 2
echo "✅ Kolibri HTTP Server: Full C-Core"
echo "   Modules: reasoning, world_model, corpus, formula, fractal, autolearn"
echo "   Endpoints: /api/v1/ai/chat, /api/v1/world_model/*, /api/v1/corpus/*,"
echo "              /api/v1/fractal/*, /api/v1/autolearn/status"
sleep 1

# Start frontend
cd "$FE" && npx vite --port 3000 --host 0.0.0.0 &
VPID=$!
sleep 3

echo "✅ http://localhost:3000"
trap "kill $BPID $VPID 2>/dev/null; exit 0" INT TERM
wait $VPID
