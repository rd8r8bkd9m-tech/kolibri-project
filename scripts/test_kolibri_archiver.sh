#!/bin/bash

# Всестороннее тестирование Kolibri Multi-level Archiver
# Тесты на разных типах данных

ARCHIVER="/Users/kolibri/Documents/os/kolibri-archiver/tools/multilevel_compress"
RESTORE="/Users/kolibri/Documents/os/kolibri-archiver/tools/multilevel_restore"
TEST_DIR="/Users/kolibri/Documents/os/test_results/kolibri_archiver"
PILOT_DIR="/Users/kolibri/Documents/pilot"

mkdir -p "$TEST_DIR"

echo "════════════════════════════════════════════════════════════════"
echo "  ВСЕСТОРОННЕЕ ТЕСТИРОВАНИЕ KOLIBRI MULTI-LEVEL ARCHIVER"
echo "════════════════════════════════════════════════════════════════"
echo ""

# ============================================================
# ТЕСТ 1: Реальная папка pilot (45 MB, 398 файлов)
# ============================================================
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "📦 ТЕСТ 1: Папка pilot (45 MB, 398 файлов)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

PILOT_SIZE=$(du -sk "$PILOT_DIR" | awk '{print $1}')
echo "Исходный размер: $(du -sh "$PILOT_DIR" | awk '{print $1}')"
echo ""

# Сжатие
echo "🔄 Сжатие..."
time "$ARCHIVER" "$PILOT_DIR" "$TEST_DIR/pilot.kolibri"
echo ""

# Размер архива
if [ -f "$TEST_DIR/pilot.kolibri" ]; then
    ARCHIVE_SIZE=$(ls -l "$TEST_DIR/pilot.kolibri" | awk '{print $5}')
    ARCHIVE_SIZE_KB=$((ARCHIVE_SIZE / 1024))
    RATIO=$(echo "scale=2; ($PILOT_SIZE * 1024) / $ARCHIVE_SIZE" | bc)
    echo "✅ Архив создан: $(ls -lh "$TEST_DIR/pilot.kolibri" | awk '{print $5}')"
    echo "📊 Коэффициент: ${RATIO}x"
else
    echo "❌ Ошибка создания архива"
fi
echo ""

# Восстановление
echo "🔄 Восстановление..."
rm -rf "$TEST_DIR/pilot_restored"
time "$RESTORE" "$TEST_DIR/pilot.kolibri" "$TEST_DIR/pilot_restored"
echo ""

# Проверка
if [ -d "$TEST_DIR/pilot_restored" ]; then
    RESTORED_SIZE=$(du -sh "$TEST_DIR/pilot_restored" | awk '{print $1}')
    FILE_COUNT=$(find "$TEST_DIR/pilot_restored" -type f | wc -l | xargs)
    echo "✅ Восстановлено: $RESTORED_SIZE ($FILE_COUNT файлов)"
    echo ""
    
    # Сравнение размеров
    echo "📊 Сравнение:"
    echo "   Оригинал:      $(du -sh "$PILOT_DIR" | awk '{print $1}')"
    echo "   Восстановлено: $RESTORED_SIZE"
else
    echo "❌ Ошибка восстановления"
fi
echo ""

# Сравнение с GZIP
echo "🔄 Сравнение с GZIP..."
tar -czf "$TEST_DIR/pilot.tar.gz" -C "$(dirname "$PILOT_DIR")" "$(basename "$PILOT_DIR")" 2>/dev/null
if [ -f "$TEST_DIR/pilot.tar.gz" ]; then
    GZIP_SIZE=$(ls -lh "$TEST_DIR/pilot.tar.gz" | awk '{print $5}')
    echo "   GZIP:    $GZIP_SIZE"
    echo "   Kolibri: $(ls -lh "$TEST_DIR/pilot.kolibri" | awk '{print $5}')"
fi
echo ""

# ============================================================
# ТЕСТ 2: Исходный код проекта (разные типы файлов)
# ============================================================
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "📦 ТЕСТ 2: Исходный код проекта (C файлы, заголовки, Makefile)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

SOURCE_DIR="/Users/kolibri/Documents/os/core"
if [ -d "$SOURCE_DIR" ]; then
    echo "Тестовая папка: $SOURCE_DIR"
    echo "Размер: $(du -sh "$SOURCE_DIR" | awk '{print $1}')"
    echo ""
    
    echo "🔄 Сжатие..."
    time "$ARCHIVER" "$SOURCE_DIR" "$TEST_DIR/core.kolibri" 2>&1 | tail -20
    
    if [ -f "$TEST_DIR/core.kolibri" ]; then
        echo "✅ Архив: $(ls -lh "$TEST_DIR/core.kolibri" | awk '{print $5}')"
        
        # GZIP для сравнения
        tar -czf "$TEST_DIR/core.tar.gz" -C "$(dirname "$SOURCE_DIR")" "$(basename "$SOURCE_DIR")" 2>/dev/null
        echo "   Kolibri: $(ls -lh "$TEST_DIR/core.kolibri" | awk '{print $5}')"
        echo "   GZIP:    $(ls -lh "$TEST_DIR/core.tar.gz" | awk '{print $5}')"
    fi
else
    echo "⚠️  Папка $SOURCE_DIR не найдена"
fi
echo ""

# ============================================================
# ТЕСТ 3: Документация (текстовые файлы)
# ============================================================
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "📦 ТЕСТ 3: Документация (Markdown файлы)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

DOCS_DIR="/Users/kolibri/Documents/os/docs"
if [ -d "$DOCS_DIR" ]; then
    echo "Тестовая папка: $DOCS_DIR"
    echo "Размер: $(du -sh "$DOCS_DIR" | awk '{print $1}')"
    echo ""
    
    echo "🔄 Сжатие..."
    time "$ARCHIVER" "$DOCS_DIR" "$TEST_DIR/docs.kolibri" 2>&1 | tail -20
    
    if [ -f "$TEST_DIR/docs.kolibri" ]; then
        echo "✅ Архив: $(ls -lh "$TEST_DIR/docs.kolibri" | awk '{print $5}')"
        
        # GZIP для сравнения
        tar -czf "$TEST_DIR/docs.tar.gz" -C "$(dirname "$DOCS_DIR")" "$(basename "$DOCS_DIR")" 2>/dev/null
        echo "   Kolibri: $(ls -lh "$TEST_DIR/docs.kolibri" | awk '{print $5}')"
        echo "   GZIP:    $(ls -lh "$TEST_DIR/docs.tar.gz" | awk '{print $5}')"
    fi
else
    echo "⚠️  Папка $DOCS_DIR не найдена"
fi
echo ""

# ============================================================
# ТЕСТ 4: Тесты проекта
# ============================================================
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "📦 ТЕСТ 4: Тесты проекта"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

TESTS_DIR="/Users/kolibri/Documents/os/tests"
if [ -d "$TESTS_DIR" ]; then
    echo "Тестовая папка: $TESTS_DIR"
    echo "Размер: $(du -sh "$TESTS_DIR" | awk '{print $1}')"
    echo ""
    
    echo "🔄 Сжатие..."
    time "$ARCHIVER" "$TESTS_DIR" "$TEST_DIR/tests.kolibri" 2>&1 | tail -20
    
    if [ -f "$TEST_DIR/tests.kolibri" ]; then
        echo "✅ Архив: $(ls -lh "$TEST_DIR/tests.kolibri" | awk '{print $5}')"
        
        # GZIP для сравнения
        tar -czf "$TEST_DIR/tests.tar.gz" -C "$(dirname "$TESTS_DIR")" "$(basename "$TESTS_DIR")" 2>/dev/null
        echo "   Kolibri: $(ls -lh "$TEST_DIR/tests.kolibri" | awk '{print $5}')"
        echo "   GZIP:    $(ls -lh "$TEST_DIR/tests.tar.gz" | awk '{print $5}')"
    fi
else
    echo "⚠️  Папка $TESTS_DIR не найдена"
fi
echo ""

# ============================================================
# ИТОГОВАЯ ТАБЛИЦА
# ============================================================
echo "════════════════════════════════════════════════════════════════"
echo "  📊 ИТОГОВАЯ ТАБЛИЦА РЕЗУЛЬТАТОВ"
echo "════════════════════════════════════════════════════════════════"
echo ""

printf "%-20s %-15s %-15s %-15s %-10s\n" "Тест" "Оригинал" "Kolibri" "GZIP" "Лучший"
echo "────────────────────────────────────────────────────────────────"

# Pilot
if [ -f "$TEST_DIR/pilot.kolibri" ] && [ -f "$TEST_DIR/pilot.tar.gz" ]; then
    ORIG=$(du -sh "$PILOT_DIR" | awk '{print $1}')
    KOL=$(ls -lh "$TEST_DIR/pilot.kolibri" | awk '{print $5}')
    GZIP=$(ls -lh "$TEST_DIR/pilot.tar.gz" | awk '{print $5}')
    
    # Определяем лучший
    KOL_BYTES=$(ls -l "$TEST_DIR/pilot.kolibri" | awk '{print $5}')
    GZIP_BYTES=$(ls -l "$TEST_DIR/pilot.tar.gz" | awk '{print $5}')
    if [ $KOL_BYTES -lt $GZIP_BYTES ]; then
        BEST="Kolibri 🏆"
    else
        BEST="GZIP 🏆"
    fi
    
    printf "%-20s %-15s %-15s %-15s %-10s\n" "pilot (398 files)" "$ORIG" "$KOL" "$GZIP" "$BEST"
fi

# Core
if [ -f "$TEST_DIR/core.kolibri" ] && [ -f "$TEST_DIR/core.tar.gz" ]; then
    ORIG=$(du -sh "$SOURCE_DIR" | awk '{print $1}')
    KOL=$(ls -lh "$TEST_DIR/core.kolibri" | awk '{print $5}')
    GZIP=$(ls -lh "$TEST_DIR/core.tar.gz" | awk '{print $5}')
    
    KOL_BYTES=$(ls -l "$TEST_DIR/core.kolibri" | awk '{print $5}')
    GZIP_BYTES=$(ls -l "$TEST_DIR/core.tar.gz" | awk '{print $5}')
    if [ $KOL_BYTES -lt $GZIP_BYTES ]; then
        BEST="Kolibri 🏆"
    else
        BEST="GZIP 🏆"
    fi
    
    printf "%-20s %-15s %-15s %-15s %-10s\n" "core (source)" "$ORIG" "$KOL" "$GZIP" "$BEST"
fi

# Docs
if [ -f "$TEST_DIR/docs.kolibri" ] && [ -f "$TEST_DIR/docs.tar.gz" ]; then
    ORIG=$(du -sh "$DOCS_DIR" | awk '{print $1}')
    KOL=$(ls -lh "$TEST_DIR/docs.kolibri" | awk '{print $5}')
    GZIP=$(ls -lh "$TEST_DIR/docs.tar.gz" | awk '{print $5}')
    
    KOL_BYTES=$(ls -l "$TEST_DIR/docs.kolibri" | awk '{print $5}')
    GZIP_BYTES=$(ls -l "$TEST_DIR/docs.tar.gz" | awk '{print $5}')
    if [ $KOL_BYTES -lt $GZIP_BYTES ]; then
        BEST="Kolibri 🏆"
    else
        BEST="GZIP 🏆"
    fi
    
    printf "%-20s %-15s %-15s %-15s %-10s\n" "docs (markdown)" "$ORIG" "$KOL" "$GZIP" "$BEST"
fi

# Tests
if [ -f "$TEST_DIR/tests.kolibri" ] && [ -f "$TEST_DIR/tests.tar.gz" ]; then
    ORIG=$(du -sh "$TESTS_DIR" | awk '{print $1}')
    KOL=$(ls -lh "$TEST_DIR/tests.kolibri" | awk '{print $5}')
    GZIP=$(ls -lh "$TEST_DIR/tests.tar.gz" | awk '{print $5}')
    
    KOL_BYTES=$(ls -l "$TEST_DIR/tests.kolibri" | awk '{print $5}')
    GZIP_BYTES=$(ls -l "$TEST_DIR/tests.tar.gz" | awk '{print $5}')
    if [ $KOL_BYTES -lt $GZIP_BYTES ]; then
        BEST="Kolibri 🏆"
    else
        BEST="GZIP 🏆"
    fi
    
    printf "%-20s %-15s %-15s %-15s %-10s\n" "tests (C files)" "$ORIG" "$KOL" "$GZIP" "$BEST"
fi

echo ""
echo "════════════════════════════════════════════════════════════════"
echo "✅ Всестороннее тестирование завершено!"
echo ""
echo "📁 Результаты: $TEST_DIR"
echo ""
