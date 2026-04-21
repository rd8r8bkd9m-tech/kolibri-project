#!/bin/bash
# Запуск Kolibri AI System

echo "🚀 Запуск Kolibri AI System..."

# 1. Генерация знаний (база для C-ядра)
python3 scripts/generate_knowledge.py

# 2. Компиляция C-ядра (WASM)
emcc -I./core -I./core/kolibri wasm/kolibri_core_wasm.c core/inference.c core/decimal.c core/logical_memory.c core/knowledge_loader.c \
  -s WASM=1 -s EXPORT_NAME=createKolibriModule -s MODULARIZE=1 -s INITIAL_MEMORY=134217728 \
  -s EXPORTED_FUNCTIONS='["_kolibri_bridge_init", "_kolibri_bridge_query_json", "_kolibri_mem_store", "_malloc", "_free"]' \
  -s EXPORTED_RUNTIME_METHODS='["ccall", "cwrap", "stringToUTF8", "UTF8ToString", "HEAPU8"]' \
  -o web/public/kolibri_engine.js

# 3. Запуск фронтенда (Vite)
cd web
npm run dev -- --port 3000 --host
