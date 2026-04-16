/*
 * Веб-сборка Kolibri не включает цифровой геном: для работы с HMAC
 * требуется OpenSSL, который недоступен в окружении Emscripten.
 * Чтобы ядро оставалось функциональным, мы предоставляем минимальные
 * заглушки API генома, используемые KolibriScript. При необходимости
 * полной поддержки установите OpenSSL и соберите с
 * KOLIBRI_WASM_INCLUDE_GENOME=1.
 */

#include "kolibri/genome.h"
#include "kolibri/decimal.h"

int kg_append(KolibriGenome *ctx, const char *event_type, const char *payload, ReasonBlock *out_block) {
    (void)ctx;
    (void)event_type;
    (void)payload;
    (void)out_block;
    return 0;
}

int kg_open(KolibriGenome *ctx, const char *path, const unsigned char *key, size_t key_len) {
    (void)ctx;
    (void)path;
    (void)key;
    (void)key_len;
    return -1;
}

void kg_close(KolibriGenome *ctx) {
    (void)ctx;
}

int kg_verify_file(const char *path, const unsigned char *key, size_t key_len) {
    (void)path;
    (void)key;
    (void)key_len;
    return -1;
}

int kg_encode_payload(const char *utf8, char *out, size_t out_len) {
    if (!out || out_len == 0) {
        return -1;
    }
    if (!utf8) {
        if (out_len > 0) {
            out[0] = '\0';
        }
        return 0;
    }
    return k_encode_text(utf8, out, out_len);
}
