#!/usr/bin/env bash
set -euo pipefail

OUT=kolibri_1gb_proof_package.tar.gz
ARCHIVE="test_1gb_v3.kolibri"
ORIG="test_1gb_perfect.bin"
DECOMP="/Users/kolibri/Documents/os/tools/kolibri-archive-v3"
VERIFY="./verify_1gb.sh"
README="./README_proof.md"

echo "Packing proof artifacts..."

# Optional: create orig.tar.gz if prefer tar instead of raw original
if [ ! -f "orig.tar.gz" ]; then
  echo "Creating orig.tar.gz..."
  tar -czf orig.tar.gz "$ORIG"
fi

# checksums
shasum -a 256 orig.tar.gz "$ARCHIVE" > checksums.sha256 || sha256sum orig.tar.gz "$ARCHIVE" > checksums.sha256 || true

# copy required files to temp dir
tmpdir=$(mktemp -d)
cp "$ARCHIVE" "$tmpdir/" 2>/dev/null || true
cp orig.tar.gz "$tmpdir/" 2>/dev/null || true
cp "$DECOMP" "$tmpdir/" 2>/dev/null || true
cp "$VERIFY" "$tmpdir/" 2>/dev/null || true
cp "$README" "$tmpdir/" 2>/dev/null || true
cp checksums.sha256 "$tmpdir/" 2>/dev/null || true

# pack
tar -czvf "$OUT" -C "$tmpdir" .
rm -rf "$tmpdir"
echo "Packed into $OUT"
ls -lh "$OUT"
