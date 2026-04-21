#!/bin/bash
# Находит все текстовые и кодовые файлы на Desktop и в Documents
# Пропускает системные папки и мусор

BASE_DIR="/Users/kolibri"
OUT_FILE="knowledge_files.list"

echo "[FINDER] Поиск знаний в $BASE_DIR..."
find "$BASE_DIR/Desktop" "$BASE_DIR/Documents" -maxdepth 4 \
    -type f \( -name "*.c" -o -name "*.h" -o -name "*.py" -o -name "*.ts" -o -name "*.tsx" -o -name "*.js" -o -name "*.md" -o -name "*.txt" -o -name "*.rs" -o -name "*.go" \) \
    -not -path "*/node_modules/*" \
    -not -path "*/dist/*" \
    -not -path "*/build/*" \
    -not -path "*/.*" \
    > "$OUT_FILE"

echo "[FINDER] Найдено $(wc -l < "$OUT_FILE") файлов для обучения."
