#!/bin/bash

# Тестирование на реальных данных
FILES=(
    "core/formula.c"          # C Source
    "core/kolibri_http_server.c" # Large C Source
    "README.md"               # Markdown
    "CMakeLists.txt"          # Build script
    "package.json"            # JSON (if exists)
    "frontend/src/App.tsx"    # TSX (if exists)
)

echo "=== Running real data compression tests ==="

for f in "${FILES[@]}"; do
    if [ -f "$f" ]; then
        echo "Testing $f..."
        ./build/test_real_file_compression "$f" | grep -E "Размер файла|КОМПРЕССИЯ:|ИТОГОВЫЙ РЕЗУЛЬТАТ"
        echo "-----------------------------------"
    else
        echo "Skipping $f (not found)"
    fi
done
