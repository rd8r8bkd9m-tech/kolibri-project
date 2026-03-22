#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT}/build-logic"
OUT_ROOT="${ROOT}/data/formula_domains"

cmake -S "${ROOT}" -B "${BUILD_DIR}" >/dev/null
cmake --build "${BUILD_DIR}" --target kolibri_formula_trainer >/dev/null

mkdir -p \
  "${OUT_ROOT}/astronomy" \
  "${OUT_ROOT}/math" \
  "${OUT_ROOT}/medicine" \
  "${OUT_ROOT}/geography" \
  "${OUT_ROOT}/philosophy" \
  "${OUT_ROOT}/physics" \
  "${OUT_ROOT}/biology" \
  "${OUT_ROOT}/programming" \
  "${OUT_ROOT}/chemistry" \
  "${OUT_ROOT}/history" \
  "${OUT_ROOT}/economics" \
  "${OUT_ROOT}/law"

"${BUILD_DIR}/kolibri_formula_trainer" \
  --out-dir "${OUT_ROOT}/astronomy" \
  --urls "${ROOT}/seeds/formula_domains/astronomy_ru.txt"

"${BUILD_DIR}/kolibri_formula_trainer" \
  --out-dir "${OUT_ROOT}/math" \
  --urls "${ROOT}/seeds/formula_domains/math_ru.txt"

"${BUILD_DIR}/kolibri_formula_trainer" \
  --out-dir "${OUT_ROOT}/medicine" \
  --urls "${ROOT}/seeds/formula_domains/medicine_ru.txt"

"${BUILD_DIR}/kolibri_formula_trainer" \
  --out-dir "${OUT_ROOT}/geography" \
  --urls "${ROOT}/seeds/formula_domains/geography_ru.txt"

"${BUILD_DIR}/kolibri_formula_trainer" \
  --out-dir "${OUT_ROOT}/philosophy" \
  --urls "${ROOT}/seeds/formula_domains/philosophy_ru.txt"

"${BUILD_DIR}/kolibri_formula_trainer" \
  --out-dir "${OUT_ROOT}/physics" \
  --urls "${ROOT}/seeds/formula_domains/physics_ru.txt"

"${BUILD_DIR}/kolibri_formula_trainer" \
  --out-dir "${OUT_ROOT}/biology" \
  --urls "${ROOT}/seeds/formula_domains/biology_ru.txt"

"${BUILD_DIR}/kolibri_formula_trainer" \
  --out-dir "${OUT_ROOT}/programming" \
  --urls "${ROOT}/seeds/formula_domains/programming_ru.txt"

"${BUILD_DIR}/kolibri_formula_trainer" \
  --out-dir "${OUT_ROOT}/chemistry" \
  --urls "${ROOT}/seeds/formula_domains/chemistry_ru.txt"

"${BUILD_DIR}/kolibri_formula_trainer" \
  --out-dir "${OUT_ROOT}/history" \
  --urls "${ROOT}/seeds/formula_domains/history_ru.txt"

"${BUILD_DIR}/kolibri_formula_trainer" \
  --out-dir "${OUT_ROOT}/economics" \
  --urls "${ROOT}/seeds/formula_domains/economics_ru.txt"

"${BUILD_DIR}/kolibri_formula_trainer" \
  --out-dir "${OUT_ROOT}/law" \
  --urls "${ROOT}/seeds/formula_domains/law_ru.txt"

printf 'Formula domains saved under %s\n' "${OUT_ROOT}"
