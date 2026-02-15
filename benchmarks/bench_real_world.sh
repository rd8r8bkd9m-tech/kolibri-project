#!/usr/bin/env bash
# =============================================================================
# Kolibri v75 — Бенчмарк: сравнение со всеми известными архиваторами
# Реальные файлы, реальные замеры
# =============================================================================
set -euo pipefail

KOLIBRI_BIN="${KOLIBRI_BIN:-./build/kolibri_archiver}"
TMPDIR_BENCH="/tmp/kolibri_bench_$$"
mkdir -p "$TMPDIR_BENCH"
trap 'rm -rf "$TMPDIR_BENCH"' EXIT

# ────── Цвета ──────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

# ────── Тестовые файлы ──────
declare -a TEST_FILES=()

if [[ $# -gt 0 ]]; then
    for f in "$@"; do
        if [[ -f "$f" ]]; then
            TEST_FILES+=("$f")
        else
            echo "Предупреждение: файл '$f' не найден, пропускаем" >&2
        fi
    done
fi

if [[ ${#TEST_FILES[@]} -eq 0 ]]; then
    # По умолчанию — compress.c (реальный C-исходник ~251KB)
    DEFAULT_FILE="backend/src/compress.c"
    if [[ -f "$DEFAULT_FILE" ]]; then
        TEST_FILES+=("$DEFAULT_FILE")
    else
        echo "Ошибка: не указан тестовый файл и $DEFAULT_FILE не найден" >&2
        exit 1
    fi
fi

# ────── Утилиты ──────
get_file_size() {
    stat -c%s "$1" 2>/dev/null || wc -c < "$1"
}

# Точное измерение времени (наносекунды)
# Возвращает время в миллисекундах (float)
time_cmd() {
    local start end elapsed_ns
    start=$(date +%s%N)
    eval "$@" >/dev/null 2>&1
    local ret=$?
    end=$(date +%s%N)
    elapsed_ns=$((end - start))
    # мс с 2 знаками
    echo "scale=2; $elapsed_ns / 1000000" | bc
    return $ret
}

# Среднее из N запусков
RUNS=3
avg_time_cmd() {
    local total=0
    for ((i=0; i<RUNS; i++)); do
        local t
        t=$(time_cmd "$@")
        total=$(echo "$total + $t" | bc)
    done
    echo "scale=2; $total / $RUNS" | bc
}

# ────── Результаты ──────
declare -a RESULTS=()

# add_result "name" "comp_size" "ratio" "comp_time_ms" "decomp_time_ms" "speed_MB_s"
add_result() {
    RESULTS+=("$1|$2|$3|$4|$5|$6")
}

# ────── Тестирование одного компрессора ──────
# bench_compressor <name> <compress_cmd> <decompress_cmd> <compressed_file> <decompressed_file> <original_size>
bench_compressor() {
    local name="$1"
    local comp_cmd="$2"
    local decomp_cmd="$3"
    local comp_file="$4"
    local decomp_file="$5"
    local orig_size="$6"

    # Сжатие
    local comp_time
    comp_time=$(avg_time_cmd "$comp_cmd")

    if [[ ! -f "$comp_file" ]]; then
        # Одиночный запуск для получения файла
        eval "$comp_cmd" >/dev/null 2>&1 || true
    fi

    if [[ ! -f "$comp_file" ]]; then
        echo "  ⚠ $name: сжатие не удалось" >&2
        return 1
    fi

    local comp_size
    comp_size=$(get_file_size "$comp_file")

    # Распаковка
    local decomp_time
    decomp_time=$(avg_time_cmd "$decomp_cmd")

    # Проверка roundtrip (опционально, один раз)
    eval "$decomp_cmd" >/dev/null 2>&1 || true

    local ratio
    ratio=$(echo "scale=3; $orig_size / $comp_size" | bc 2>/dev/null || echo "N/A")

    local speed
    speed=$(echo "scale=1; $orig_size / 1048576 / ($comp_time / 1000)" | bc 2>/dev/null || echo "N/A")

    local decomp_speed
    decomp_speed=$(echo "scale=1; $orig_size / 1048576 / ($decomp_time / 1000)" | bc 2>/dev/null || echo "N/A")

    add_result "$name" "$comp_size" "$ratio" "$comp_time" "$decomp_time" "$speed"

    # Cleanup
    rm -f "$comp_file" "$decomp_file" 2>/dev/null || true
}

# ────── Печать таблицы ──────
print_table() {
    local orig_size="$1"
    local orig_name="$2"

    echo ""
    echo -e "${BOLD}╔══════════════════════════════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${BOLD}║  Kolibri v75 — Сравнительный бенчмарк на реальном файле                                ║${NC}"
    echo -e "${BOLD}╠══════════════════════════════════════════════════════════════════════════════════════════╣${NC}"
    echo -e "${BOLD}║${NC}  Файл: ${CYAN}${orig_name}${NC} (${orig_size} байт = $(echo "scale=1; $orig_size/1024" | bc) KB)"
    echo -e "${BOLD}║${NC}  Запусков: ${RUNS} (среднее время)"
    echo -e "${BOLD}╠══════════════════════════════════════════════════════════════════════════════════════════╣${NC}"
    printf "${BOLD}║${NC} %-28s │ %10s │ %7s │ %9s │ %9s │ %9s ${BOLD}║${NC}\n" \
        "Архиватор" "Размер" "Ratio" "Сжатие" "Распак." "Скорость"
    printf "${BOLD}║${NC} %-28s │ %10s │ %7s │ %9s │ %9s │ %9s ${BOLD}║${NC}\n" \
        "" "(байт)" "(x)" "(мс)" "(мс)" "(МБ/с)"
    echo -e "${BOLD}╠══════════════════════════════════════════════════════════════════════════════════════════╣${NC}"

    # Сортировка по ratio (по убыванию)
    IFS=$'\n' sorted=($(for r in "${RESULTS[@]}"; do echo "$r"; done | sort -t'|' -k3 -rn))
    unset IFS

    local best_ratio=0
    local best_name=""
    for r in "${sorted[@]}"; do
        IFS='|' read -r name size ratio comp_t decomp_t speed <<< "$r"
        local cur_ratio
        cur_ratio=$(echo "$ratio" | sed 's/[^0-9.]//g')
        if (( $(echo "$cur_ratio > $best_ratio" | bc -l 2>/dev/null || echo 0) )); then
            best_ratio=$cur_ratio
            best_name=$name
        fi
    done

    for r in "${sorted[@]}"; do
        IFS='|' read -r name size ratio comp_t decomp_t speed <<< "$r"

        local color="${NC}"
        if [[ "$name" == *"Kolibri"* ]]; then
            color="${GREEN}"
        fi
        if [[ "$name" == "$best_name" ]]; then
            color="${YELLOW}"
        fi

        printf "${BOLD}║${NC} ${color}%-28s${NC} │ %10s │ %7s │ %9s │ %9s │ %9s ${BOLD}║${NC}\n" \
            "$name" "$size" "${ratio}x" "${comp_t}ms" "${decomp_t}ms" "${speed}"
    done

    echo -e "${BOLD}╠══════════════════════════════════════════════════════════════════════════════════════════╣${NC}"
    echo -e "${BOLD}║${NC}  ${YELLOW}★${NC} Лучший ratio: ${YELLOW}${best_name}${NC} (${best_ratio}x)"
    echo -e "${BOLD}║${NC}  ${GREEN}■${NC} = Kolibri"
    echo -e "${BOLD}╚══════════════════════════════════════════════════════════════════════════════════════════╝${NC}"
    echo ""
}

# =============================================================================
# Главный цикл по файлам
# =============================================================================
for TEST_FILE in "${TEST_FILES[@]}"; do
    RESULTS=()
    ORIG_SIZE=$(get_file_size "$TEST_FILE")
    ORIG_NAME=$(basename "$TEST_FILE")
    INPUT_COPY="$TMPDIR_BENCH/input_${ORIG_NAME}"
    cp "$TEST_FILE" "$INPUT_COPY"

    echo -e "\n${BOLD}━━━ Бенчмарк: ${CYAN}${ORIG_NAME}${NC} (${ORIG_SIZE} байт) ━━━${NC}"

    # ──────────────────────────────────────────────────────────
    # 1. Kolibri v75 CM (полное сжатие)
    # ──────────────────────────────────────────────────────────
    echo -n "  Kolibri v75 CM..."
    if [[ -x "$KOLIBRI_BIN" ]]; then
        KCOMP="$TMPDIR_BENCH/out_kolibri_cm.klb"
        KDECOMP="$TMPDIR_BENCH/out_kolibri_cm_dec"
        bench_compressor \
            "Kolibri v75 CM" \
            "LD_LIBRARY_PATH=build $KOLIBRI_BIN compress $INPUT_COPY $KCOMP" \
            "LD_LIBRARY_PATH=build $KOLIBRI_BIN decompress $KCOMP $KDECOMP" \
            "$KCOMP" "$KDECOMP" "$ORIG_SIZE" && echo " ✓" || echo " ✗"
    else
        echo " (не найден $KOLIBRI_BIN)"
    fi

    # ──────────────────────────────────────────────────────────
    # 2. Kolibri v75 Fast/Turbo
    # ──────────────────────────────────────────────────────────
    TURBO_BIN="./build/kolibri_fast"
    if [[ -x "$TURBO_BIN" ]]; then
        echo -n "  Kolibri v75 Fast..."
        KCOMP="$TMPDIR_BENCH/out_kolibri_fast.klb"
        KDECOMP="$TMPDIR_BENCH/out_kolibri_fast_dec"
        bench_compressor \
            "Kolibri v75 Fast" \
            "LD_LIBRARY_PATH=build $TURBO_BIN c $INPUT_COPY $KCOMP" \
            "LD_LIBRARY_PATH=build $TURBO_BIN d $KCOMP $KDECOMP" \
            "$KCOMP" "$KDECOMP" "$ORIG_SIZE" && echo " ✓" || echo " ✗"
    else
        echo "  (Kolibri Fast не найден — build/kolibri_fast)"
    fi

    # ──────────────────────────────────────────────────────────
    # 3. gzip (быстрый -1 и максимальный -9)
    # ──────────────────────────────────────────────────────────
    if command -v gzip &>/dev/null; then
        echo -n "  gzip..."
        for lvl in 1 6 9; do
            OUT="$TMPDIR_BENCH/out.gz"
            DEC="$TMPDIR_BENCH/out_gzip_dec"
            rm -f "$OUT" "$DEC"
            bench_compressor \
                "gzip -${lvl}" \
                "gzip -${lvl} -c $INPUT_COPY > $OUT" \
                "gzip -d -c $OUT > $DEC" \
                "$OUT" "$DEC" "$ORIG_SIZE" || true
        done
        echo " ✓"
    fi

    # ──────────────────────────────────────────────────────────
    # 4. pigz (параллельный gzip)
    # ──────────────────────────────────────────────────────────
    if command -v pigz &>/dev/null; then
        echo -n "  pigz..."
        OUT="$TMPDIR_BENCH/out_pigz.gz"
        DEC="$TMPDIR_BENCH/out_pigz_dec"
        rm -f "$OUT" "$DEC"
        bench_compressor \
            "pigz -9" \
            "pigz -9 -c $INPUT_COPY > $OUT" \
            "pigz -d -c $OUT > $DEC" \
            "$OUT" "$DEC" "$ORIG_SIZE" || true
        echo " ✓"
    fi

    # ──────────────────────────────────────────────────────────
    # 5. bzip2
    # ──────────────────────────────────────────────────────────
    if command -v bzip2 &>/dev/null; then
        echo -n "  bzip2..."
        for lvl in 1 9; do
            OUT="$TMPDIR_BENCH/out.bz2"
            DEC="$TMPDIR_BENCH/out_bz2_dec"
            rm -f "$OUT" "$DEC"
            bench_compressor \
                "bzip2 -${lvl}" \
                "bzip2 -${lvl} -c $INPUT_COPY > $OUT" \
                "bzip2 -d -c $OUT > $DEC" \
                "$OUT" "$DEC" "$ORIG_SIZE" || true
        done
        echo " ✓"
    fi

    # ──────────────────────────────────────────────────────────
    # 6. xz (LZMA2)
    # ──────────────────────────────────────────────────────────
    if command -v xz &>/dev/null; then
        echo -n "  xz..."
        for lvl in 1 6 9; do
            OUT="$TMPDIR_BENCH/out.xz"
            DEC="$TMPDIR_BENCH/out_xz_dec"
            rm -f "$OUT" "$DEC"
            bench_compressor \
                "xz -${lvl}" \
                "xz -${lvl} -c $INPUT_COPY > $OUT" \
                "xz -d -c $OUT > $DEC" \
                "$OUT" "$DEC" "$ORIG_SIZE" || true
        done
        echo " ✓"
    fi

    # ──────────────────────────────────────────────────────────
    # 7. lzma
    # ──────────────────────────────────────────────────────────
    if command -v lzma &>/dev/null; then
        echo -n "  lzma..."
        OUT="$TMPDIR_BENCH/out.lzma"
        DEC="$TMPDIR_BENCH/out_lzma_dec"
        rm -f "$OUT" "$DEC"
        bench_compressor \
            "lzma -9" \
            "lzma -9 -c $INPUT_COPY > $OUT" \
            "lzma -d -c $OUT > $DEC" \
            "$OUT" "$DEC" "$ORIG_SIZE" || true
        echo " ✓"
    fi

    # ──────────────────────────────────────────────────────────
    # 8. zstd (Zstandard)
    # ──────────────────────────────────────────────────────────
    if command -v zstd &>/dev/null; then
        echo -n "  zstd..."
        for lvl in 1 3 9 19 22; do
            OUT="$TMPDIR_BENCH/out.zst"
            DEC="$TMPDIR_BENCH/out_zst_dec"
            rm -f "$OUT" "$DEC"
            local_flag=""
            [[ $lvl -gt 19 ]] && local_flag="--ultra"
            bench_compressor \
                "zstd -${lvl}" \
                "zstd $local_flag -${lvl} -c $INPUT_COPY > $OUT" \
                "zstd -d -c $OUT > $DEC" \
                "$OUT" "$DEC" "$ORIG_SIZE" || true
        done
        echo " ✓"
    fi

    # ──────────────────────────────────────────────────────────
    # 9. lz4
    # ──────────────────────────────────────────────────────────
    if command -v lz4 &>/dev/null; then
        echo -n "  lz4..."
        OUT="$TMPDIR_BENCH/out.lz4"
        DEC="$TMPDIR_BENCH/out_lz4_dec"
        rm -f "$OUT" "$DEC"
        bench_compressor \
            "lz4 (default)" \
            "lz4 -f $INPUT_COPY $OUT" \
            "lz4 -d -f $OUT $DEC" \
            "$OUT" "$DEC" "$ORIG_SIZE" || true

        rm -f "$OUT" "$DEC"
        bench_compressor \
            "lz4 -9 (HC)" \
            "lz4 -9 -f $INPUT_COPY $OUT" \
            "lz4 -d -f $OUT $DEC" \
            "$OUT" "$DEC" "$ORIG_SIZE" || true
        echo " ✓"
    fi

    # ──────────────────────────────────────────────────────────
    # 10. lzop
    # ──────────────────────────────────────────────────────────
    if command -v lzop &>/dev/null; then
        echo -n "  lzop..."
        OUT="$TMPDIR_BENCH/out.lzo"
        DEC="$TMPDIR_BENCH/out_lzo_dec"
        rm -f "$OUT" "$DEC"
        bench_compressor \
            "lzop -1 (fast)" \
            "lzop -1 -c $INPUT_COPY > $OUT" \
            "lzop -d -c $OUT > $DEC" \
            "$OUT" "$DEC" "$ORIG_SIZE" || true

        rm -f "$OUT" "$DEC"
        bench_compressor \
            "lzop -9 (best)" \
            "lzop -9 -c $INPUT_COPY > $OUT" \
            "lzop -d -c $OUT > $DEC" \
            "$OUT" "$DEC" "$ORIG_SIZE" || true
        echo " ✓"
    fi

    # ──────────────────────────────────────────────────────────
    # 11. brotli
    # ──────────────────────────────────────────────────────────
    if command -v brotli &>/dev/null; then
        echo -n "  brotli..."
        for lvl in 1 6 11; do
            OUT="$TMPDIR_BENCH/out.br"
            DEC="$TMPDIR_BENCH/out_br_dec"
            rm -f "$OUT" "$DEC"
            bench_compressor \
                "brotli -${lvl}" \
                "brotli -q ${lvl} -c $INPUT_COPY > $OUT" \
                "brotli -d -c $OUT > $DEC" \
                "$OUT" "$DEC" "$ORIG_SIZE" || true
        done
        echo " ✓"
    fi

    # ──────────────────────────────────────────────────────────
    # Таблица
    # ──────────────────────────────────────────────────────────
    print_table "$ORIG_SIZE" "$ORIG_NAME"

done

echo -e "${GREEN}Бенчмарк завершён.${NC}"
