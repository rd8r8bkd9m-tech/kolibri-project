#!/bin/bash
set -e

echo "============================================="
echo "   KOLIBRI ARTIFICIAL INTELLIGENCE SYSTEM    "
echo "        NO-LLM AUTONOMOUS KERNEL             "
echo "============================================="

# 1. Setup
echo "[1] Compiling Core Components..."
gcc -Ibackend/include -o kolibri_ingest apps/kolibri_ingest.c backend/src/digit_text.c backend/src/decimal.c backend/src/digits.c
gcc -Ibackend/include -o kolibri_learn apps/kolibri_learn.c
# We link the objects for gen, or just recompile sources
gcc -Ibackend/include -o kolibri_gen apps/kolibri_gen.c backend/src/digit_text.c backend/src/decimal.c backend/src/digits.c

# 2. Ingestion
echo ""
echo "[2] Running DATA INGESTION Pipeline..."
# Create a robust test file with enough repetition for learning
cat > demo_corpus.html <<EOF
<!DOCTYPE html>
<html>
<body>
<p>Kolibri is a self-learning system. Kolibri is a self-learning system.</p>
<p>Intelligence is compression. Intelligence is compression.</p>
<p>The universe is a number. The universe is a number.</p>
<p>Kolibri creates patterns from data. Kolibri creates patterns from data.</p>
<p>Algorithmic generation is deterministic. Algorithmic generation is deterministic.</p>
<p>Kolibri.</p>
</body>
</html>
EOF

./kolibri_ingest raw_data.dat demo_corpus.html
echo "    -> Output: raw_data.dat (Decimal Stream)"

# 3. Learning
echo ""
echo "[3] Running PATTERN LEARNING (Evolutionary Mining)..."
./kolibri_learn raw_data.dat
echo "    -> Output: kolibri.genome (Knowledge Base)"

# 4. Generation
echo ""
echo "[4] Running ALGORITHMIC GENERATION..."
echo "    Query Seed: 'Kolibri'"
./kolibri_gen "Kolibri"

echo ""
echo "    Query Seed: 'Intelligence'"
./kolibri_gen "Intelligence"

echo ""
echo "============================================="
echo "   DEMONSTRATION COMPLETE"
echo "============================================="
