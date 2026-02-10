#!/usr/bin/env bash
# ============================================================
#  Kolibri Archiver Benchmark — сравнение с gzip/bzip2/xz/zstd/lz4
# ============================================================
set -uo pipefail

KOLIBRI="./build/kolibri_archiver"
TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

B='\033[1m'
C='\033[0;36m'
G='\033[0;32m'
N='\033[0m'

echo -e "${B}${C}═══════════════════════════════════════════════════════════════════════${N}"
echo -e "${B}${C}     Kolibri Archiver Benchmark vs gzip / bzip2 / xz / zstd / lz4    ${N}"
echo -e "${B}${C}═══════════════════════════════════════════════════════════════════════${N}"
echo ""

# ── Генерация тестовых данных ──
python3 -c "print('Kolibri OS uses evolutionary compression for data. ' * 2000)" > "$TMPDIR/1_repeat.txt"

python3 -c "
t = '''Колибри — уникальная AI-система с эволюционным подходом к обработке данных.
Kolibri OS применяет формулы, которые конкурируют друг с другом за право предсказывать токен.
Лучшие формулы выживают и мутируют, худшие отбрасываются — естественный отбор.
Сжатие реализовано через предиктивную модель — MLP-формула предсказывает следующий байт.
Арифметическое кодирование преобразует предсказания в компактный поток бит.
'''
print(t * 100)
" > "$TMPDIR/2_russian.txt"

head -c 140000 backend/src/compress.c > "$TMPDIR/3_source.c"

python3 -c "
import json
data=[{'id':i,'name':f'item_{i}','val':i*3.14,'tags':['kolibri','bench'],'n':{'x':i}} for i in range(500)]
print(json.dumps(data,indent=2))
" > "$TMPDIR/4_data.json"

python3 -c "
import struct
buf=bytearray()
for i in range(5000):
    buf+=struct.pack('I',i%256)+bytes([i%128]*4)
open('$TMPDIR/5_binary.dat','wb').write(buf)
"

dd if=/dev/urandom bs=1024 count=40 of="$TMPDIR/6_random.bin" 2>/dev/null

NAMES=("Повторяющийся текст" "Русский текст" "Исходный код (C)" "JSON данные" "Бинарные паттерны" "Случайные данные")
FNAMES=(1_repeat.txt 2_russian.txt 3_source.c 4_data.json 5_binary.dat 6_random.bin)

run_ext() {
  local name="$1" input="$2" comp_cmd="$3" dec_cmd="$4" out="$TMPDIR/_c_out" dec="$TMPDIR/_d_out"
  local orig=$(stat -c%s "$input")

  local t0=$(date +%s%N)
  eval "$comp_cmd" >/dev/null 2>&1 || true
  local t1=$(date +%s%N)
  local cms=$(( (t1 - t0) / 1000000 ))

  [[ ! -f "$out" ]] && { printf "  %-14s  %10s  %7s  %8s  %10s\n" "$name" "FAIL" "-" "-" "-"; return; }
  local csize=$(stat -c%s "$out")
  local ratio=$(python3 -c "print(f'{$orig/$csize:.2f}')" 2>/dev/null)

  local t2=$(date +%s%N)
  eval "$dec_cmd" >/dev/null 2>&1 || true
  local t3=$(date +%s%N)
  local dms=$(( (t3 - t2) / 1000000 ))

  printf "  %-14s  %9d B  %7sx  %6d ms  %8d ms\n" "$name" "$csize" "$ratio" "$cms" "$dms"
  rm -f "$out" "$dec" 2>/dev/null
}

for idx in "${!FNAMES[@]}"; do
  f="${FNAMES[$idx]}"
  desc="${NAMES[$idx]}"
  input="$TMPDIR/$f"
  orig=$(stat -c%s "$input")

  echo ""
  echo -e "${B}📁 $desc${N} — $(numfmt --to=iec $orig)"
  printf "  %-14s  %10s  %8s  %8s  %10s\n" "Архиватор" "Размер" "Ratio" "Сжатие" "Распаковка"
  echo "  ──────────────────────────────────────────────────────────────"

  # ★ KOLIBRI
  ko="$TMPDIR/_kolibri_out"; kr="$TMPDIR/_kolibri_dec"
  t0=$(date +%s%N)
  $KOLIBRI compress "$input" "$ko" >/dev/null 2>&1 || true
  t1=$(date +%s%N); cms=$(( (t1 - t0) / 1000000 ))
  if [[ -f "$ko" ]]; then
    csize=$(stat -c%s "$ko")
    ratio=$(python3 -c "print(f'{$orig/$csize:.2f}')" 2>/dev/null)
    t2=$(date +%s%N)
    $KOLIBRI decompress "$ko" "$kr" >/dev/null 2>&1 || true
    t3=$(date +%s%N); dms=$(( (t3 - t2) / 1000000 ))
    vfy=""; diff -q "$input" "$kr" >/dev/null 2>&1 && vfy="✅" || vfy="❌"
    printf "  ${G}★ KOLIBRI${N}       %9d B  %7sx  %6d ms  %8d ms  %s\n" "$csize" "$ratio" "$cms" "$dms" "$vfy"
  fi
  rm -f "$ko" "$kr" 2>/dev/null

  O="$TMPDIR/_c_out"; D="$TMPDIR/_d_out"
  run_ext "gzip -1"  "$input" "gzip -1 -c $input > $O" "gzip -d -c $O > $D"
  run_ext "gzip -9"  "$input" "gzip -9 -c $input > $O" "gzip -d -c $O > $D"
  run_ext "bzip2 -9" "$input" "bzip2 -9 -c $input > $O" "bzip2 -d -c $O > $D"
  run_ext "xz -6"    "$input" "xz -6 -c $input > $O" "xz -d -c $O > $D"
  run_ext "zstd -1"  "$input" "zstd -1 -f -c $input > $O" "zstd -d -f -c $O > $D"
  run_ext "zstd -3"  "$input" "zstd -3 -f -c $input > $O" "zstd -d -f -c $O > $D"
  run_ext "zstd -19" "$input" "zstd -19 -f -c $input > $O" "zstd -d -f -c $O > $D"
  run_ext "lz4"      "$input" "lz4 -f -c $input > $O" "lz4 -d -f -c $O > $D"
done

echo ""
echo -e "${B}${C}═══════════════════════════════════════════════════════════════════════${N}"
echo -e "${B}  Ratio = оригинал / сжатое (больше = лучше)${N}"
echo -e "${B}  ★ KOLIBRI = LZ77 + Huffman + Formula (адаптивный выбор)${N}"
echo -e "${B}${C}═══════════════════════════════════════════════════════════════════════${N}"
