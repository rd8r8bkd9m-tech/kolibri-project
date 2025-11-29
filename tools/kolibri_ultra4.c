// ═══════════════════════════════════════════════════════════════
//   KOLIBRI ULTRA v17.0 - BWT + MTF + RLE-0 + Order-1 RC
//   100% внутренняя реализация - ФИНАЛЬНАЯ ВЕРСИЯ
// ═══════════════════════════════════════════════════════════════

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>

#define MAGIC 0x4B554C37  // "KUL7"

// ═══════════════════════════════════════════════════════════════
//   RANGE CODER
// ═══════════════════════════════════════════════════════════════

#define RC_TOP (1u << 24)
#define RC_BOT (1u << 16)

typedef struct { uint32_t low, range, code; uint8_t *buf; size_t pos, size; } RangeCoder;

static void rc_enc_init(RangeCoder *rc, uint8_t *buf, size_t sz) { rc->low = 0; rc->range = 0xFFFFFFFF; rc->buf = buf; rc->pos = 0; rc->size = sz; }
static void rc_enc_byte(RangeCoder *rc, uint8_t b) { if (rc->pos < rc->size) rc->buf[rc->pos++] = b; }
static void rc_enc_norm(RangeCoder *rc) {
    while ((rc->low ^ (rc->low + rc->range)) < RC_TOP || (rc->range < RC_BOT && ((rc->range = -rc->low & (RC_BOT - 1)), 1))) {
        rc_enc_byte(rc, rc->low >> 24); rc->low <<= 8; rc->range <<= 8;
    }
}
static void rc_enc(RangeCoder *rc, uint32_t cum, uint32_t fr, uint32_t total) { rc->range /= total; rc->low += cum * rc->range; rc->range *= fr; rc_enc_norm(rc); }
static void rc_enc_fin(RangeCoder *rc) { for (int i = 0; i < 4; i++) { rc_enc_byte(rc, rc->low >> 24); rc->low <<= 8; } }

static void rc_dec_init(RangeCoder *rc, const uint8_t *buf, size_t sz) {
    rc->low = 0; rc->range = 0xFFFFFFFF; rc->code = 0; rc->buf = (uint8_t*)buf; rc->pos = 0; rc->size = sz;
    for (int i = 0; i < 4 && rc->pos < sz; i++) rc->code = (rc->code << 8) | buf[rc->pos++];
}
static void rc_dec_norm(RangeCoder *rc) {
    while ((rc->low ^ (rc->low + rc->range)) < RC_TOP || (rc->range < RC_BOT && ((rc->range = -rc->low & (RC_BOT - 1)), 1))) {
        rc->code = (rc->code << 8) | (rc->pos < rc->size ? rc->buf[rc->pos++] : 0); rc->low <<= 8; rc->range <<= 8;
    }
}
static uint32_t rc_get(RangeCoder *rc, uint32_t total) { rc->range /= total; return (rc->code - rc->low) / rc->range; }
static void rc_dec(RangeCoder *rc, uint32_t cum, uint32_t fr) { rc->low += cum * rc->range; rc->range *= fr; rc_dec_norm(rc); }

// ═══════════════════════════════════════════════════════════════
//   ADAPTIVE ORDER-1 CONTEXT
// ═══════════════════════════════════════════════════════════════

#define ADAPT 4

typedef struct { uint16_t freq[256]; uint16_t total; } Ctx;

static void ctx_init(Ctx *c) { for (int i = 0; i < 256; i++) c->freq[i] = 1; c->total = 256; }

static void ctx_upd(Ctx *c, uint8_t s) {
    c->freq[s] += ADAPT; c->total += ADAPT;
    if (c->total > 16000) { c->total = 0; for (int i = 0; i < 256; i++) { c->freq[i] = (c->freq[i] >> 1) | 1; c->total += c->freq[i]; } }
}

static void ctx_enc(RangeCoder *rc, Ctx *c, uint8_t s) {
    uint32_t cum = 0; for (int i = 0; i < s; i++) cum += c->freq[i];
    rc_enc(rc, cum, c->freq[s], c->total); ctx_upd(c, s);
}

static uint8_t ctx_dec(RangeCoder *rc, Ctx *c) {
    uint32_t f = rc_get(rc, c->total), cum = 0; int s = 0;
    while (cum + c->freq[s] <= f) { cum += c->freq[s]; s++; }
    rc_dec(rc, cum, c->freq[s]); ctx_upd(c, s); return s;
}

// ═══════════════════════════════════════════════════════════════
//   BWT
// ═══════════════════════════════════════════════════════════════

static const uint8_t *g_bwt; static size_t g_bwt_sz;

static int bwt_cmp(const void *a, const void *b) {
    size_t i = *(const size_t*)a, j = *(const size_t*)b;
    for (size_t k = 0; k < g_bwt_sz; k++) { uint8_t ci = g_bwt[(i + k) % g_bwt_sz], cj = g_bwt[(j + k) % g_bwt_sz]; if (ci != cj) return ci - cj; }
    return 0;
}

static size_t bwt_enc(const uint8_t *in, size_t sz, uint8_t *out, uint32_t *idx) {
    if (!sz) return 0;
    size_t *ind = malloc(sz * sizeof(size_t)); for (size_t i = 0; i < sz; i++) ind[i] = i;
    g_bwt = in; g_bwt_sz = sz; qsort(ind, sz, sizeof(size_t), bwt_cmp);
    *idx = 0; for (size_t i = 0; i < sz; i++) { out[i] = in[(ind[i] + sz - 1) % sz]; if (ind[i] == 0) *idx = i; }
    free(ind); return sz;
}

static size_t bwt_dec(const uint8_t *in, size_t sz, uint32_t idx, uint8_t *out) {
    if (!sz) return 0;
    uint32_t cnt[256] = {0}, first[256], sum = 0;
    for (size_t i = 0; i < sz; i++) cnt[in[i]]++;
    for (int i = 0; i < 256; i++) { first[i] = sum; sum += cnt[i]; }
    uint32_t *T = malloc(sz * sizeof(uint32_t)); memset(cnt, 0, sizeof(cnt));
    for (size_t i = 0; i < sz; i++) { T[i] = first[in[i]] + cnt[in[i]]; cnt[in[i]]++; }
    size_t pos = idx; for (size_t i = sz; i > 0; i--) { out[i-1] = in[pos]; pos = T[pos]; }
    free(T); return sz;
}

// ═══════════════════════════════════════════════════════════════
//   MTF
// ═══════════════════════════════════════════════════════════════

static size_t mtf_enc(const uint8_t *in, size_t sz, uint8_t *out) {
    uint8_t list[256]; for (int i = 0; i < 256; i++) list[i] = i;
    for (size_t i = 0; i < sz; i++) {
        uint8_t c = in[i]; int p = 0; while (list[p] != c) p++;
        out[i] = p; while (p > 0) { list[p] = list[p-1]; p--; } list[0] = c;
    }
    return sz;
}

static size_t mtf_dec(const uint8_t *in, size_t sz, uint8_t *out) {
    uint8_t list[256]; for (int i = 0; i < 256; i++) list[i] = i;
    for (size_t i = 0; i < sz; i++) {
        int p = in[i]; uint8_t c = list[p]; out[i] = c;
        while (p > 0) { list[p] = list[p-1]; p--; } list[0] = c;
    }
    return sz;
}

// ═══════════════════════════════════════════════════════════════
//   RLE-0 (нули кодируются как RUNA/RUNB)
// ═══════════════════════════════════════════════════════════════

// bzip2-style: нули -> last non-zero symbol + (run length in binary using RUNA=1, RUNB=2)
// Упрощённо: 0 -> 0, много нулей -> 0xFF + len(2 bytes)

static size_t rle0_enc(const uint8_t *in, size_t sz, uint8_t *out, size_t max) {
    size_t op = 0, i = 0;
    while (i < sz && op + 4 < max) {
        if (in[i] == 0) {
            size_t run = 0; while (i + run < sz && in[i + run] == 0 && run < 65535) run++;
            if (run >= 4) { out[op++] = 0xFF; out[op++] = (run >> 8); out[op++] = run & 0xFF; }
            else { for (size_t j = 0; j < run; j++) out[op++] = 0; }
            i += run;
        } else {
            uint8_t c = in[i++];
            if (c == 0xFF) { out[op++] = 0xFE; out[op++] = 0xFF; }
            else if (c == 0xFE) { out[op++] = 0xFE; out[op++] = 0xFE; }
            else out[op++] = c;
        }
    }
    return op;
}

static size_t rle0_dec(const uint8_t *in, size_t sz, uint8_t *out, size_t max) {
    size_t op = 0, i = 0;
    while (i < sz && op < max) {
        if (in[i] == 0xFF && i + 2 < sz) { size_t run = ((size_t)in[i+1] << 8) | in[i+2]; for (size_t j = 0; j < run && op < max; j++) out[op++] = 0; i += 3; }
        else if (in[i] == 0xFE && i + 1 < sz) { out[op++] = in[i+1]; i += 2; }
        else out[op++] = in[i++];
    }
    return op;
}

// ═══════════════════════════════════════════════════════════════
//   ORDER-1 ENCODE/DECODE
// ═══════════════════════════════════════════════════════════════

static size_t order1_enc(const uint8_t *in, size_t sz, uint8_t *out, size_t max) {
    if (!sz) return 0;
    Ctx *ctx = calloc(256, sizeof(Ctx)); for (int i = 0; i < 256; i++) ctx_init(&ctx[i]);
    size_t op = 0;
    out[op++] = (sz >> 24); out[op++] = (sz >> 16); out[op++] = (sz >> 8); out[op++] = sz;
    RangeCoder rc; rc_enc_init(&rc, out + op, max - op);
    uint8_t prev = 0;
    for (size_t i = 0; i < sz; i++) { ctx_enc(&rc, &ctx[prev], in[i]); prev = in[i]; }
    rc_enc_fin(&rc);
    free(ctx); return op + rc.pos;
}

static size_t order1_dec(const uint8_t *in, size_t sz, uint8_t *out, size_t max) {
    size_t ip = 0;
    size_t orig = ((size_t)in[ip]<<24)|((size_t)in[ip+1]<<16)|((size_t)in[ip+2]<<8)|in[ip+3]; ip += 4;
    if (orig > max) return 0;
    Ctx *ctx = calloc(256, sizeof(Ctx)); for (int i = 0; i < 256; i++) ctx_init(&ctx[i]);
    RangeCoder rc; rc_dec_init(&rc, in + ip, sz - ip);
    uint8_t prev = 0;
    for (size_t i = 0; i < orig; i++) { out[i] = ctx_dec(&rc, &ctx[prev]); prev = out[i]; }
    free(ctx); return orig;
}

// ═══════════════════════════════════════════════════════════════
//   MAIN
// ═══════════════════════════════════════════════════════════════

typedef struct { uint32_t magic, version, orig_size, comp_size, bwt_idx, checksum; } __attribute__((packed)) Header;

static uint32_t crc32(const uint8_t *d, size_t sz) {
    uint32_t c = 0xFFFFFFFF; for (size_t i = 0; i < sz; i++) { c ^= d[i]; for (int j = 0; j < 8; j++) c = (c >> 1) ^ (0xEDB88320 & -(c & 1)); } return ~c;
}

static double now(void) { struct timeval tv; gettimeofday(&tv, NULL); return tv.tv_sec + tv.tv_usec / 1e6; }

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("\n╔════════════════════════════════════════════════════════════════╗\n");
        printf("║  KOLIBRI ULTRA v17.0 - BWT + MTF + RLE-0 + Order-1 RC          ║\n");
        printf("║  100%% внутренняя реализация без внешних библиотек              ║\n");
        printf("╚════════════════════════════════════════════════════════════════╝\n\n");
        printf("Usage: %s compress|decompress <input> <output>\n\n", argv[0]);
        return 1;
    }
    
    const char *mode = argv[1], *inp = argv[2], *outp = argv[3];
    
    if (strcmp(mode, "compress") == 0) {
        FILE *f = fopen(inp, "rb"); if (!f) { printf("❌ Cannot open: %s\n", inp); return 1; }
        fseek(f, 0, SEEK_END); size_t sz = ftell(f); fseek(f, 0, SEEK_SET);
        uint8_t *data = malloc(sz); fread(data, 1, sz, f); fclose(f);
        
        printf("\n╔═══════════════════════════════════════════════════════════════╗\n");
        printf("║  KOLIBRI ULTRA v17.0 COMPRESSION                              ║\n");
        printf("╚═══════════════════════════════════════════════════════════════╝\n\n");
        printf("📄 Input: %s (%.2f KB)\n\n", inp, sz / 1024.0);
        
        double t0 = now();
        uint8_t *b1 = malloc(sz), *b2 = malloc(sz), *b3 = malloc(sz * 2), *b4 = malloc(sz * 2);
        
        printf("🔄 BWT..."); fflush(stdout);
        uint32_t bwt_idx; bwt_enc(data, sz, b1, &bwt_idx);
        printf(" idx=%u\n", bwt_idx);
        
        printf("🔄 MTF..."); fflush(stdout);
        mtf_enc(b1, sz, b2);
        size_t zeros = 0; for (size_t i = 0; i < sz; i++) if (b2[i] == 0) zeros++;
        printf(" zeros=%.1f%%\n", 100.0 * zeros / sz);
        
        printf("🔄 RLE-0..."); fflush(stdout);
        size_t s3 = rle0_enc(b2, sz, b3, sz * 2);
        printf(" %.2fx\n", (double)sz / s3);
        
        printf("🔄 Order-1 RC..."); fflush(stdout);
        size_t s4 = order1_enc(b3, s3, b4, sz * 2);
        printf(" %.2fx total\n", (double)sz / s4);
        
        Header h = { MAGIC, 17, sz, s4, bwt_idx, crc32(data, sz) };
        FILE *fo = fopen(outp, "wb"); fwrite(&h, sizeof(h), 1, fo); fwrite(b4, 1, s4, fo); fclose(fo);
        
        double elapsed = now() - t0;
        size_t arch = sizeof(h) + s4;
        
        printf("\n╔═══════════════════════════════════════════════════════════════╗\n");
        printf("║  РЕЗУЛЬТАТ                                                    ║\n");
        printf("╠═══════════════════════════════════════════════════════════════╣\n");
        printf("║  Исходник:    %10.2f KB                                  ║\n", sz / 1024.0);
        printf("║  Архив:       %10.2f KB                                  ║\n", arch / 1024.0);
        printf("║  Сжатие:      %10.2fx                                    ║\n", (double)sz / arch);
        printf("║  Время:       %10.3f сек                                 ║\n", elapsed);
        printf("╚═══════════════════════════════════════════════════════════════╝\n\n");
        
        free(data); free(b1); free(b2); free(b3); free(b4);
        
    } else if (strcmp(mode, "decompress") == 0) {
        FILE *f = fopen(inp, "rb"); if (!f) { printf("❌ Cannot open: %s\n", inp); return 1; }
        Header h; fread(&h, sizeof(h), 1, f);
        if (h.magic != MAGIC) { printf("❌ Invalid archive\n"); fclose(f); return 1; }
        uint8_t *comp = malloc(h.comp_size); fread(comp, 1, h.comp_size, f); fclose(f);
        
        printf("\n📦 Decompressing: %s (%.2fx)\n\n", inp, (double)h.orig_size / (h.comp_size + sizeof(h)));
        
        uint8_t *b1 = malloc(h.orig_size * 2), *b2 = malloc(h.orig_size), *b3 = malloc(h.orig_size), *out = malloc(h.orig_size);
        
        printf("🔄 Order-1 RC..."); fflush(stdout); size_t s1 = order1_dec(comp, h.comp_size, b1, h.orig_size * 2); printf(" %zu\n", s1);
        printf("🔄 RLE-0..."); fflush(stdout); size_t s2 = rle0_dec(b1, s1, b2, h.orig_size); printf(" %zu\n", s2);
        printf("🔄 MTF..."); fflush(stdout); size_t s3 = mtf_dec(b2, s2, b3); printf(" %zu\n", s3);
        printf("🔄 BWT..."); fflush(stdout); bwt_dec(b3, s3, h.bwt_idx, out); printf(" %u\n", h.orig_size);
        
        if (crc32(out, h.orig_size) != h.checksum) { printf("❌ CRC mismatch!\n"); return 1; }
        
        FILE *fo = fopen(outp, "wb"); fwrite(out, 1, h.orig_size, fo); fclose(fo);
        printf("\n✅ Extracted: %s (%u bytes)\n\n", outp, h.orig_size);
        
        free(comp); free(b1); free(b2); free(b3); free(out);
    }
    return 0;
}
