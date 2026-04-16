#!/usr/bin/env bash
set -euo pipefail

# Скрипт компиляции ядра Kolibri в WebAssembly.
# По умолчанию собирает вычислительное ядро (десятичный слой,
# эволюцию формул и генератор случайных чисел) и проверяет,
# что итоговый модуль укладывается в бюджет < 1 МБ.

proekt_koren="$(cd "$(dirname "$0")/.." && pwd)"
vyhod_dir="$proekt_koren/build/wasm"
mkdir -p "$vyhod_dir"

vyhod_wasm="$vyhod_dir/kolibri.wasm"
vremennaja_map="$vyhod_dir/kolibri.map"
vremennaja_js="$vyhod_dir/kolibri.js"

emscripten_cache_dir="${KOLIBRI_WASM_CACHE_DIR:-$proekt_koren/build/emscripten_cache}"
mkdir -p "$emscripten_cache_dir"
export EM_CACHE="$emscripten_cache_dir"

opredelit_razmer() {
    local file="$1"

    if command -v python3 >/dev/null 2>&1; then
        python3 - "$file" <<'PY'
import os
import sys

path = sys.argv[1]
print(os.path.getsize(path))
PY
        return 0
    fi

    if command -v stat >/dev/null 2>&1; then
        if stat -c '%s' "$file" >/dev/null 2>&1; then
            stat -c '%s' "$file"
            return 0
        fi
        if stat -f '%z' "$file" >/dev/null 2>&1; then
            stat -f '%z' "$file"
            return 0
        fi
    fi

    if command -v wc >/dev/null 2>&1; then
        wc -c <"$file" | tr -d ' ' \
            || wc -c "${file}" | awk '{print $1}'
        return 0
    fi

    echo 0
    return 0
}

EMCC="${EMCC:-emcc}"
sobranov_docker=0

vychislit_sha256_stroku() {
    local file="$1"

    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$file"
        return 0
    fi

    if command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$file"
        return 0
    fi

    if command -v python3 >/dev/null 2>&1; then
        python3 - "$file" <<'PY'
import hashlib
import os
import sys

path = sys.argv[1]
with open(path, "rb") as handle:
    digest = hashlib.sha256(handle.read()).hexdigest()

print(f"{digest}  {os.path.basename(path)}")
PY
        return 0
    fi

    return 1
}

zapisat_sha256() {
    local file="$1"
    local target="$2"

    if vychislit_sha256_stroku "$file" >"$target.tmp"; then
        mv "$target.tmp" "$target"
        return 0
    fi

    rm -f "$target.tmp"
    cat >"$target" <<EOF
sha256 недоступна: отсутствуют утилиты sha256sum/shasum и python3
EOF
    echo "[ПРЕДУПРЕЖДЕНИЕ] Не удалось вычислить SHA256 для $file: отсутствуют необходимые утилиты." >&2
    return 1
}

ensure_emcc() {
    if command -v "$EMCC" >/dev/null 2>&1; then
        return 0
    fi

    if [[ "${KOLIBRI_WASM_INVOKED_VIA_DOCKER:-0}" == "1" ]]; then
        echo "[ОШИБКА] Не найден emcc внутри Docker-окружения. Проверьте образ ${KOLIBRI_WASM_DOCKER_IMAGE:-emscripten/emsdk:3.1.61}." >&2
        return 1
    fi

    if command -v docker >/dev/null 2>&1; then
        docker_image="${KOLIBRI_WASM_DOCKER_IMAGE:-emscripten/emsdk:3.1.61}"
        echo "[Kolibri] emcc не найден. Пытаюсь собрать kolibri.wasm через Docker (${docker_image})."
        docker run --rm \
            -v "$proekt_koren":/project \
            -w /project/scripts \
            -e KOLIBRI_WASM_INVOKED_VIA_DOCKER=1 \
            -e KOLIBRI_WASM_INCLUDE_GENOME \
            -e KOLIBRI_WASM_GENERATE_MAP \
            "$docker_image" \
            bash -lc "./build_wasm.sh"
        local docker_status=$?
        if (( docker_status == 0 )); then
            sobranov_docker=1
        else
            echo "[ОШИБКА] Сборка kolibri.wasm внутри Docker завершилась с ошибкой." >&2
        fi
        return $docker_status
    fi

    echo "[ОШИБКА] emcc не найден и Docker недоступен. Деградационный режим отключён." >&2
    return 1
}

ensure_emcc || exit 1

if (( sobranov_docker )) && [[ "${KOLIBRI_WASM_INVOKED_VIA_DOCKER:-0}" != "1" ]] && ! command -v "$EMCC" >/dev/null 2>&1; then
    # Docker fallback уже собрал артефакт, на хосте больше делать нечего.
    exit 0
fi

istochniki=(
    "$proekt_koren/core/decimal.c"
    "$proekt_koren/core/digits.c"
    "$proekt_koren/core/digit_text.c"
    "$proekt_koren/core/formula.c"
    "$proekt_koren/core/random.c"
    "$proekt_koren/core/symbol_table.c"
    "$proekt_koren/core/script.c"
    "$proekt_koren/core/wasm_bridge.c"
    "$proekt_koren/core/wasm_link_stubs.c"
    "$proekt_koren/core/sim.c"
    "$proekt_koren/wasm/kolibri_sim_wasm.c"
)

if [[ "${KOLIBRI_WASM_INCLUDE_GENOME:-0}" == "1" ]]; then
    istochniki+=("$proekt_koren/core/genome.c")
else
    istochniki+=("$proekt_koren/core/wasm_genome_stub.c")
fi

flags=(
    -Os
    -std=gnu99
    -s STANDALONE_WASM=1
    -s SIDE_MODULE=0
    -s ALLOW_MEMORY_GROWTH=1
    -s EXPORTED_RUNTIME_METHODS='[]'
    -s EXPORTED_FUNCTIONS='["_kolibri_bridge_init","_kolibri_bridge_reset","_kolibri_bridge_execute","_kolibri_bridge_query_json","_kolibri_bridge_health","_kolibri_bridge_send_message","_kolibri_bridge_cancel_query","_kolibri_bridge_is_cancelled","_kolibri_bridge_get_progress_state","_kolibri_bridge_get_progress_value","_kolibri_bridge_get_progress_detail","_kolibri_bridge_get_thinking","_kolibri_sim_wasm_init","_kolibri_sim_wasm_tick","_kolibri_sim_wasm_get_logs","_kolibri_sim_wasm_reset","_kolibri_sim_wasm_free","_malloc","_free"]'
    -s DEFAULT_LIBRARY_FUNCS_TO_INCLUDE='[]'
    --no-entry
    -I"$proekt_koren/core"
    -o "$vyhod_wasm"
)

if [[ "${KOLIBRI_WASM_GENERATE_MAP:-0}" == "1" ]]; then
    flags+=(--emit-symbol-map)
fi

"$EMCC" "${istochniki[@]}" "${flags[@]}"

razmer=$(opredelit_razmer "$vyhod_wasm")
if (( razmer > 1024 * 1024 )); then
    printf '[ОШИБКА] kolibri.wasm превышает бюджет: %.2f МБ\n' "$(awk -v b="$razmer" 'BEGIN {printf "%.2f", b/1048576}')" >&2
    exit 1
fi

ekport_info="$vyhod_dir/kolibri.wasm.txt"
cat >"$ekport_info" <<EOF_INFO
kolibri.wasm: $(awk -v b="$razmer" 'BEGIN {printf "%.2f МБ", b/1048576}')
Эта сборка включает вычислительное ядро (десятичные трансдукции,
эволюцию формул и генератор случайных чисел). Для включения цифрового
генома запустите скрипт с KOLIBRI_WASM_INCLUDE_GENOME=1 и добавьте
поддержку HMAC в окружении.
EOF_INFO

zapisat_sha256 "$vyhod_wasm" "$vyhod_dir/kolibri.wasm.sha256"

rm -f "$vremennaja_js" "$vremennaja_map"

# Артефакты для разработки (public) и продакшена (dist)
target_dirs=("$proekt_koren/web/public" "$proekt_koren/web/dist")

for dir in "${target_dirs[@]}"; do
    mkdir -p "$dir"
    cp "$vyhod_wasm" "$dir/kolibri.wasm"
    cp "$ekport_info" "$dir/kolibri.wasm.txt"
    cp "$vyhod_dir/kolibri.wasm.sha256" "$dir/kolibri.wasm.sha256"
    echo "  -> Copied to $dir"
done

echo "[ГОТОВО] kolibri.wasm собрано и доставлено."
