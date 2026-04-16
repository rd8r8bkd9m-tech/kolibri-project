#!/usr/bin/env bash
set -euo pipefail

# Настройки — подправь пути если нужно
ARCHIVE="./test_1gb_v3.kolibri"
DECOMP="/Users/kolibri/Documents/os/tools/kolibri-archive-v3"
DECOMP_CMD="$DECOMP extract"   # ожидается: kolibri-archive-v3 extract <archive> <outfile>
ORIG="./test_1gb_perfect.bin"
OUT="./test_1gb_recovered.bin"

echo "=== VERIFY 1GB PROOF ==="
echo "Archive: $ARCHIVE"
[ -f "$ARCHIVE" ] || { echo "ERROR: archive not found: $ARCHIVE"; exit 1; }
[ -f "$ORIG" ] || { echo "ERROR: original file not found: $ORIG"; exit 2; }
[ -x "$DECOMP" ] || { echo "ERROR: decompressor not found or not executable: $DECOMP"; exit 3; }

echo
echo "1) sizes:"
# macOS stat
orig_size=$(stat -f "%z" "$ORIG")
echo " Original: $ORIG -> $orig_size bytes"
arch_size=$(stat -f "%z" "$ARCHIVE")
echo " Archive : $ARCHIVE -> $arch_size bytes"
echo

echo "2) extract..."
# run decompressor; adapt args if different
"$DECOMP" extract "$ARCHIVE" "$OUT"
echo "Extraction finished."

if [ ! -f "$OUT" ]; then
  echo "ERROR: output file not created: $OUT"
  exit 4
fi

out_size=$(stat -f "%z" "$OUT")
echo " Recovered: $OUT -> $out_size bytes"
echo

echo "3) checksums (MD5 & SHA256):"
md5 "$ORIG" || true
md5 "$OUT" || true
(shasum -a 256 "$ORIG" 2>/dev/null || sha256sum "$ORIG")
(shasum -a 256 "$OUT" 2>/dev/null || sha256sum "$OUT")
echo

echo "4) bytewise compare (cmp):"
if cmp --silent "$ORIG" "$OUT"; then
  echo "✅ BYTEWISE MATCH: recovered == original"
else
  echo "❌ NO MATCH: recovered differs from original!"
  exit 5
fi
echo

echo "5) compression ratio:"
# compute ratio as double
ratio=$(awk -v o=$orig_size -v a=$arch_size 'BEGIN{printf "%.6f", o/a}')
echo " Ratio (orig / archive): $ratio x"
echo

echo "=== ✅ VERIFY DONE ==="
