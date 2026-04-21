#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/wasm_bridge.c"

/* Заглушки для линковки */
#include "core/formula.c"
#include "core/script.c"
#include "core/random.c"
#include "core/decimal.c"
#include "core/digit_text.c"
#include "core/digits.c"
#include "core/symbol_table.c"
#include "core/wasm_link_stubs.c"
#include "core/sim.c"

int main() {
    char out_json[8192];
    printf("--- Тест 1: Простой запрос ---\n");
    kolibri_bridge_init();
    kolibri_bridge_query_json("2 + 2", out_json, sizeof(out_json));
    printf("JSON: %s\n\n", out_json);

    printf("--- Тест 2: Запрос без ответа ---\n");
    kolibri_bridge_query_json("неизвестная команда", out_json, sizeof(out_json));
    printf("JSON: %s\n", out_json);

    return 0;
}
