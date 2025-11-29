#!/bin/bash

# Комплексное тестирование всех архиваторов Kolibri на реальных данных
# Дата: 2025-11-29

set -e

PROJECT_ROOT="/workspaces/kolibri-project"
TEST_DIR="$PROJECT_ROOT/test_results/archiver_comparison_$(date +%Y%m%d_%H%M%S)"
ARCHIVER="$PROJECT_ROOT/build/kolibri_archiver"

mkdir -p "$TEST_DIR"

# Цвета для вывода
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo "════════════════════════════════════════════════════════════════════════"
echo "  КОМПЛЕКСНОЕ ТЕСТИРОВАНИЕ АРХИВАТОРОВ KOLIBRI"
echo "  На реальных данных проекта"
echo "════════════════════════════════════════════════════════════════════════"
echo ""
echo "Архиватор: $ARCHIVER"
echo "Директория результатов: $TEST_DIR"
echo ""

# Проверяем наличие архиватора
if [ ! -f "$ARCHIVER" ]; then
    echo -e "${RED}❌ Архиватор не найден: $ARCHIVER${NC}"
    echo "Попробуйте собрать проект: cmake --build build"
    exit 1
fi

# Функция для тестирования файла
test_file() {
    local file="$1"
    local name="$2"
    local category="$3"
    
    if [ ! -f "$file" ]; then
        echo -e "${YELLOW}⚠️  Файл не найден: $file${NC}"
        return
    fi
    
    local original_size=$(stat -c%s "$file" 2>/dev/null || stat -f%z "$file" 2>/dev/null)
    local original_size_h=$(du -h "$file" | cut -f1)
    
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${GREEN}📄 Тест: $name${NC}"
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo "Категория: $category"
    echo "Файл: $(basename "$file")"
    echo "Исходный размер: $original_size_h ($original_size байт)"
    echo ""
    
    # Тест Kolibri Archiver
    echo "🔹 Kolibri Archiver v40:"
    local kolibri_archive="$TEST_DIR/${name// /_}.klb"
    local kolibri_restored="$TEST_DIR/${name// /_}_restored.txt"
    
    # Сжатие
    local start_time=$(date +%s.%N)
    if "$ARCHIVER" compress "$file" "$kolibri_archive" > /dev/null 2>&1; then
        local end_time=$(date +%s.%N)
        local compress_time=$(echo "$end_time - $start_time" | bc)
        local compressed_size=$(stat -c%s "$kolibri_archive" 2>/dev/null || stat -f%z "$kolibri_archive" 2>/dev/null)
        local compressed_size_h=$(du -h "$kolibri_archive" | cut -f1)
        local ratio=$(echo "scale=2; $original_size / $compressed_size" | bc)
        
        echo "   Сжатый размер: $compressed_size_h ($compressed_size байт)"
        echo "   Коэффициент: ${ratio}x"
        echo "   Время сжатия: ${compress_time}s"
        
        # Распаковка
        local start_time=$(date +%s.%N)
        if "$ARCHIVER" decompress "$kolibri_archive" "$kolibri_restored" > /dev/null 2>&1; then
            local end_time=$(date +%s.%N)
            local decompress_time=$(echo "$end_time - $start_time" | bc)
            
            # Проверка целостности
            if cmp -s "$file" "$kolibri_restored"; then
                echo -e "   ${GREEN}✅ Целостность: OK${NC}"
            else
                echo -e "   ${RED}❌ Целостность: FAILED${NC}"
            fi
            echo "   Время распаковки: ${decompress_time}s"
        else
            echo -e "   ${RED}❌ Ошибка распаковки${NC}"
        fi
    else
        echo -e "   ${RED}❌ Ошибка сжатия${NC}"
    fi
    echo ""
    
    # Сравнение с gzip
    echo "🔹 GZIP (для сравнения):"
    local gzip_archive="$TEST_DIR/${name// /_}.gz"
    
    local start_time=$(date +%s.%N)
    if gzip -c "$file" > "$gzip_archive" 2>/dev/null; then
        local end_time=$(date +%s.%N)
        local gzip_time=$(echo "$end_time - $start_time" | bc)
        local gzip_size=$(stat -c%s "$gzip_archive" 2>/dev/null || stat -f%z "$gzip_archive" 2>/dev/null)
        local gzip_size_h=$(du -h "$gzip_archive" | cut -f1)
        local gzip_ratio=$(echo "scale=2; $original_size / $gzip_size" | bc)
        
        echo "   Сжатый размер: $gzip_size_h ($gzip_size байт)"
        echo "   Коэффициент: ${gzip_ratio}x"
        echo "   Время сжатия: ${gzip_time}s"
    else
        echo -e "   ${RED}❌ Ошибка сжатия${NC}"
    fi
    echo ""
    
    # Сравнение с xz
    echo "🔹 XZ (для сравнения):"
    local xz_archive="$TEST_DIR/${name// /_}.xz"
    
    local start_time=$(date +%s.%N)
    if xz -c "$file" > "$xz_archive" 2>/dev/null; then
        local end_time=$(date +%s.%N)
        local xz_time=$(echo "$end_time - $start_time" | bc)
        local xz_size=$(stat -c%s "$xz_archive" 2>/dev/null || stat -f%z "$xz_archive" 2>/dev/null)
        local xz_size_h=$(du -h "$xz_archive" | cut -f1)
        local xz_ratio=$(echo "scale=2; $original_size / $xz_size" | bc)
        
        echo "   Сжатый размер: $xz_size_h ($xz_size байт)"
        echo "   Коэффициент: ${xz_ratio}x"
        echo "   Время сжатия: ${xz_time}s"
    else
        echo -e "   ${RED}❌ Ошибка сжатия${NC}"
    fi
    echo ""
    
    # Итоговое сравнение
    if [ -f "$kolibri_archive" ] && [ -f "$gzip_archive" ] && [ -f "$xz_archive" ]; then
        echo "📊 Итоговое сравнение:"
        echo "   Исходный:   $original_size_h"
        echo "   Kolibri v40: $compressed_size_h (${ratio}x)"
        echo "   GZIP:       $gzip_size_h (${gzip_ratio}x)"
        echo "   XZ:         $xz_size_h (${xz_ratio}x)"
        
        # Определяем победителя
        if (( $(echo "$compressed_size < $gzip_size" | bc -l) )) && (( $(echo "$compressed_size < $xz_size" | bc -l) )); then
            echo -e "   ${GREEN}🏆 Лучший: Kolibri v40${NC}"
        elif (( $(echo "$gzip_size < $compressed_size" | bc -l) )) && (( $(echo "$gzip_size < $xz_size" | bc -l) )); then
            echo -e "   ${YELLOW}🏆 Лучший: GZIP${NC}"
        else
            echo -e "   ${YELLOW}🏆 Лучший: XZ${NC}"
        fi
    fi
    echo ""
}

# ============================================================================
# НАБОР ТЕСТОВ НА РЕАЛЬНЫХ ДАННЫХ
# ============================================================================

echo ""
echo "════════════════════════════════════════════════════════════════════════"
echo "  КАТЕГОРИЯ 1: ИСХОДНЫЙ КОД C"
echo "════════════════════════════════════════════════════════════════════════"
echo ""

test_file "$PROJECT_ROOT/backend/src/compress.c" "compress.c - core module" "C Source"
test_file "$PROJECT_ROOT/apps/kolibri_archiver.c" "kolibri_archiver.c - CLI tool" "C Source"
test_file "$PROJECT_ROOT/backend/src/knowledge.c" "knowledge.c - knowledge base" "C Source"

echo ""
echo "════════════════════════════════════════════════════════════════════════"
echo "  КАТЕГОРИЯ 2: ЗАГОЛОВОЧНЫЕ ФАЙЛЫ"
echo "════════════════════════════════════════════════════════════════════════"
echo ""

test_file "$PROJECT_ROOT/backend/include/kolibri/compress.h" "compress.h - API header" "C Header"
test_file "$PROJECT_ROOT/backend/include/kolibri/knowledge.h" "knowledge.h - API header" "C Header"

echo ""
echo "════════════════════════════════════════════════════════════════════════"
echo "  КАТЕГОРИЯ 3: ДОКУМЕНТАЦИЯ"
echo "════════════════════════════════════════════════════════════════════════"
echo ""

test_file "$PROJECT_ROOT/README.md" "README.md - main docs" "Markdown"
test_file "$PROJECT_ROOT/ARCHIVER_SUMMARY_RU.md" "ARCHIVER_SUMMARY_RU.md" "Markdown"
test_file "$PROJECT_ROOT/apps/README_ARCHIVER_RU.md" "README_ARCHIVER_RU.md" "Markdown"

echo ""
echo "════════════════════════════════════════════════════════════════════════"
echo "  КАТЕГОРИЯ 4: КОНФИГУРАЦИОННЫЕ ФАЙЛЫ"
echo "════════════════════════════════════════════════════════════════════════"
echo ""

test_file "$PROJECT_ROOT/CMakeLists.txt" "CMakeLists.txt - build config" "CMake"
test_file "$PROJECT_ROOT/Makefile" "Makefile - build script" "Makefile"

echo ""
echo "════════════════════════════════════════════════════════════════════════"
echo "  КАТЕГОРИЯ 5: СКРИПТЫ"
echo "════════════════════════════════════════════════════════════════════════"
echo ""

test_file "$PROJECT_ROOT/install.sh" "install.sh - installer" "Shell Script"
test_file "$PROJECT_ROOT/test_all_archivers.sh" "test_all_archivers.sh" "Shell Script"

# ============================================================================
# ИТОГОВАЯ СТАТИСТИКА
# ============================================================================

echo ""
echo "════════════════════════════════════════════════════════════════════════"
echo "  📊 ИТОГОВАЯ СТАТИСТИКА"
echo "════════════════════════════════════════════════════════════════════════"
echo ""

# Подсчет результатов
total_original=0
total_kolibri=0
total_gzip=0
total_xz=0
count=0

for klb_file in "$TEST_DIR"/*.klb; do
    if [ -f "$klb_file" ]; then
        base_name="${klb_file%.klb}"
        original_file=$(echo "$base_name" | sed 's/_restored$//')
        
        klb_size=$(stat -c%s "$klb_file" 2>/dev/null || stat -f%z "$klb_file" 2>/dev/null || echo 0)
        gz_size=$(stat -c%s "${base_name}.gz" 2>/dev/null || stat -f%z "${base_name}.gz" 2>/dev/null || echo 0)
        xz_size=$(stat -c%s "${base_name}.xz" 2>/dev/null || stat -f%z "${base_name}.xz" 2>/dev/null || echo 0)
        
        total_kolibri=$((total_kolibri + klb_size))
        total_gzip=$((total_gzip + gz_size))
        total_xz=$((total_xz + xz_size))
        count=$((count + 1))
    fi
done

if [ $count -gt 0 ]; then
    echo "Протестировано файлов: $count"
    echo ""
    echo "Общий размер сжатых данных:"
    echo "  Kolibri v40: $(numfmt --to=iec $total_kolibri 2>/dev/null || echo "${total_kolibri} bytes")"
    echo "  GZIP:        $(numfmt --to=iec $total_gzip 2>/dev/null || echo "${total_gzip} bytes")"
    echo "  XZ:          $(numfmt --to=iec $total_xz 2>/dev/null || echo "${total_xz} bytes")"
    echo ""
    
    # Процентное сравнение
    if [ $total_gzip -gt 0 ]; then
        kolibri_vs_gzip=$(echo "scale=2; ($total_gzip - $total_kolibri) * 100 / $total_gzip" | bc)
        if (( $(echo "$kolibri_vs_gzip > 0" | bc -l) )); then
            echo -e "  Kolibri v40 на ${GREEN}${kolibri_vs_gzip}%${NC} лучше GZIP"
        else
            echo -e "  GZIP на ${YELLOW}$(echo "$kolibri_vs_gzip * -1" | bc)%${NC} лучше Kolibri v40"
        fi
    fi
    
    if [ $total_xz -gt 0 ]; then
        kolibri_vs_xz=$(echo "scale=2; ($total_xz - $total_kolibri) * 100 / $total_xz" | bc)
        if (( $(echo "$kolibri_vs_xz > 0" | bc -l) )); then
            echo -e "  Kolibri v40 на ${GREEN}${kolibri_vs_xz}%${NC} лучше XZ"
        else
            echo -e "  XZ на ${YELLOW}$(echo "$kolibri_vs_xz * -1" | bc)%${NC} лучше Kolibri v40"
        fi
    fi
fi

echo ""
echo "════════════════════════════════════════════════════════════════════════"
echo -e "${GREEN}✅ ТЕСТИРОВАНИЕ ЗАВЕРШЕНО${NC}"
echo "════════════════════════════════════════════════════════════════════════"
echo ""
echo "Результаты сохранены в: $TEST_DIR"
echo ""
echo "Детали:"
ls -lh "$TEST_DIR" | head -20
echo ""
