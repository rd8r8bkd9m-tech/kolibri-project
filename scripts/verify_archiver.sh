#!/bin/bash
set -e
INPUT="verify_test.dat"
OUTPUT="verify_test.klb"
RESTORED="verify_test.restored"

# Create random data with text
echo "Creating test data..."
# 100KB random
dd if=/dev/urandom of=$INPUT bs=1024 count=100 2>/dev/null
# Append compressible text
for i in {1..2000}; do echo "Repeated text line for compression testing LZ77 optimization $i" >> $INPUT; done

# Create pure mathematical sequence (Linear: 0, 1, 2, ... 255, 0...)
# This should be compressed to near-zero by Formula 1 or 2
perl -e 'for($i=0;$i<100000;$i++){print chr($i%256)}' >> $INPUT

# Create Stride-4 pattern (0,0,0,0, 1,1,1,1, ...)
perl -e 'for($i=0;$i<25000;$i++){print chr($i%256) x 4}' >> $INPUT

ORIG_SIZE=$(stat -c%s $INPUT)
echo "Original size: $ORIG_SIZE bytes"

echo "Compressing..."
./build/kolibri_archiver compress $INPUT $OUTPUT
COMP_SIZE=$(stat -c%s $OUTPUT)
echo "Compressed size: $COMP_SIZE bytes"

echo "Decompressing..."
./build/kolibri_archiver decompress $OUTPUT $RESTORED

echo "Verifying..."
if cmp -s $INPUT $RESTORED; then
    echo "SUCCESS: Files match."
    rm $INPUT $OUTPUT $RESTORED
    exit 0
else
    echo "FAILURE: Files do not match!"
    exit 1
fi
