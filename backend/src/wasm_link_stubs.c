#include "kolibri/fractal_memory.h"
#include "kolibri/net.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Lightweight stubs used only in WebAssembly build.
 * They satisfy symbols required by script.c when networking/fractal modules
 * are not linked into kolibri.wasm.
 */

int kfm_init(KfmContext *ctx, uint32_t seed)
{
    if (!ctx) {
        return -1;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->seed = seed;
    return 0;
}

void kfm_free(KfmContext *ctx)
{
    (void)ctx;
}

int kfm_insert(KfmContext *ctx,
               const uint8_t *path, size_t path_len,
               const void *payload, size_t payload_size)
{
    (void)ctx;
    (void)path;
    (void)path_len;
    (void)payload;
    (void)payload_size;
    return 0;
}

int kfm_search(KfmContext *ctx,
               const uint8_t *query, size_t query_len,
               KfmSearchResult *results, size_t max_results)
{
    (void)ctx;
    (void)query;
    (void)query_len;
    if (results && max_results > 0) {
        memset(results, 0, sizeof(KfmSearchResult) * max_results);
    }
    return 0;
}

size_t kfm_text_to_path(const char *text, size_t text_len,
                        uint8_t *path, size_t max_path)
{
    size_t i;
    size_t n;

    if (!text || !path || max_path == 0) {
        return 0;
    }

    n = text_len < max_path ? text_len : max_path;
    if (n == 0) {
        path[0] = 0;
        return 1;
    }

    for (i = 0; i < n; i++) {
        path[i] = (uint8_t)(((unsigned char)text[i]) % 10U);
    }

    return n;
}

int kn_share_formula(const char *host, uint16_t port, uint32_t node_id, const KolibriFormula *formula)
{
    (void)host;
    (void)port;
    (void)node_id;
    (void)formula;
    return -1;
}

