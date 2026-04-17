#!/usr/bin/env bash
# Kolibri Archiver - Quick Install Script for macOS/Linux
# Usage: curl -sSL https://raw.githubusercontent.com/rd8r8bkd9m-tech/kolibri-project/main/install.sh | bash

set -euo pipefail

echo "╔════════════════════════════════════════════════════════════════╗"
echo "║           KOLIBRI ARCHIVER v3.0 - БЫСТРАЯ УСТАНОВКА           ║"
echo "╚════════════════════════════════════════════════════════════════╝"
echo

# Detect OS
OS=$(uname -s)
ARCH=$(uname -m)

echo "🔍 Система: $OS $ARCH"
echo

# Check for compiler
if ! command -v gcc &> /dev/null; then
    echo "❌ GCC не найден. Установите Xcode Command Line Tools:"
    echo "   xcode-select --install"
    exit 1
fi

echo "✅ GCC найден: $(gcc --version | head -1)"
echo

# Download source
echo "📥 Скачивание исходного кода..."
SOURCE_URL="https://raw.githubusercontent.com/rd8r8bkd9m-tech/kolibri-project/main/tools/kolibri_archiver_v3.c"
curl -fsSL "$SOURCE_URL" -o kolibri_archiver_v3.c

if [ ! -f kolibri_archiver_v3.c ]; then
    echo "❌ Ошибка скачивания"
    exit 1
fi

echo "✅ Исходный код скачан ($(wc -c < kolibri_archiver_v3.c) байт)"
echo

# Compile
echo "🔨 Компиляция..."
if gcc -O3 -o kolibri-archive kolibri_archiver_v3.c; then
    echo "✅ Компиляция успешна"
else
    echo "❌ Ошибка компиляции"
    exit 1
fi

# Check binary
if [ ! -x kolibri-archive ]; then
    chmod +x kolibri-archive
fi

echo "✅ Бинарник создан: $(ls -lh kolibri-archive | awk '{print $5}')"
echo

# Test
echo "🧪 Проверка..."
if ./kolibri-archive 2>&1 | grep -q "KOLIBRI OS ARCHIVER"; then
    echo "✅ Архиватор работает!"
else
    echo "⚠️  Предупреждение: возможны проблемы с запуском"
fi

echo
echo "════════════════════════════════════════════════════════════════"
echo
echo "🎉 УСТАНОВКА ЗАВЕРШЕНА!"
echo
echo "📍 Расположение: $(pwd)/kolibri-archive"
echo
echo "🚀 Использование:"
echo "   ./kolibri-archive compress input.bin output.kolibri"
echo "   ./kolibri-archive extract archive.kolibri restored.bin"
echo
echo "💡 Переместить в PATH (опционально):"
echo "   sudo mv kolibri-archive /usr/local/bin/"
echo
echo "📖 Документация:"
echo "   https://github.com/rd8r8bkd9m-tech/kolibri-project"
echo
echo "════════════════════════════════════════════════════════════════"

# Cleanup
rm -f kolibri_archiver_v3.c

exit 0
