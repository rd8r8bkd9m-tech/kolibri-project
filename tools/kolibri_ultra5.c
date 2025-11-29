// ═══════════════════════════════════════════════════════════════
//   KOLIBRI ULTRA v18.0 - BWT + MTF + Order-2 Range Coder
//   100% внутренняя реализация - MAXIMUM COMPRESSION
// ═══════════════════════════════════════════════════════════════

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>

#define MAGIC 0x4B554C38  // "KUL8"

// ═══════════════════════════════════════════════════════════════
//   RANGE CODER
// ═══════════════════════════════════════════════════════════════

#define RC_TOP (1u << 24)
#define RC_BOT (1u << 16)

typedef struct { uint32_t low, range, code; uint8_t *buf; size_t pos, size; } RC;

static void rc_init_enc(RC *r, uint8_t *b, size_t s) { r->low=0; r->range=~0u; r->buf=b; r->pos=0; r->size=s; }
static void rc_out(RC *r, uint8_t b) { if(r->pos<r->size) r->buf[r->pos++]=b; }
static void rc_norm_enc(RC *r) { while((r->low^(r->low+r->range))<RC_TOP||(r->range<RC_BOT&&((r->range=-r->low&(RC_BOT-1)),1))) { rc_out(r,r->low>>24); r->low<<=8; r->range<<=8; } }
static void rc_enc(RC *r, uint32_t c, uint32_t f, uint32_t t) { r->range/=t; r->low+=c*r->range; r->range*=f; rc_norm_enc(r); }
static void rc_fin(RC *r) { for(int i=0;i<4;i++){ rc_out(r,r->low>>24); r->low<<=8; } }

static void rc_init_dec(RC *r, const uint8_t *b, size_t s) { r->low=0; r->range=~0u; r->code=0; r->buf=(uint8_t*)b; r->pos=0; r->size=s; for(int i=0;i<4&&r->pos<s;i++) r->code=(r->code<<8)|b[r->pos++]; }
static void rc_norm_dec(RC *r) { while((r->low^(r->low+r->range))<RC_TOP||(r->range<RC_BOT&&((r->range=-r->low&(RC_BOT-1)),1))) { r->code=(r->code<<8)|(r->pos<r->size?r->buf[r->pos++]:0); r->low<<=8; r->range<<=8; } }
static uint32_t rc_get(RC *r, uint32_t t) { r->range/=t; return (r->code-r->low)/r->range; }
static void rc_dec(RC *r, uint32_t c, uint32_t f) { r->low+=c*r->range; r->range*=f; rc_norm_dec(r); }

// ═══════════════════════════════════════════════════════════════
//   ORDER-2 ADAPTIVE MODEL (65536 контекстов)
// ═══════════════════════════════════════════════════════════════

// Используем хеш-таблицу для контекстов (экономия памяти)
#define CTX_HASH_SIZE 65536
#define CTX_ADAPT 2

typedef struct CtxEntry {
    uint16_t ctx;      // контекст (prev2<<8 | prev1)
    uint16_t freq[256];
    uint16_t total;
    struct CtxEntry *next;
} CtxEntry;

typedef struct {
    CtxEntry *table[CTX_HASH_SIZE];
    uint16_t order0_freq[256];  // fallback order-0
    uint16_t order0_total;
} Model;

static void model_init(Model *m) {
    memset(m->table, 0, sizeof(m->table));
    for (int i = 0; i < 256; i++) m->order0_freq[i] = 1;
    m->order0_total = 256;
}

static CtxEntry* model_get(Model *m, uint16_t ctx) {
    uint16_t h = ctx % CTX_HASH_SIZE;
    CtxEntry *e = m->table[h];
    while (e && e->ctx != ctx) e = e->next;
    if (!e) {
        e = calloc(1, sizeof(CtxEntry));
        e->ctx = ctx;
        for (int i = 0; i < 256; i++) e->freq[i] = 1;
        e->total = 256;
        e->next = m->table[h];
        m->table[h] = e;
    }
    return e;
}

static void model_update(Model *m, uint16_t ctx, uint8_t sym) {
    CtxEntry *e = model_get(m, ctx);
    e->freq[sym] += CTX_ADAPT;
    e->total += CTX_ADAPT;
    if (e->total > 16000) {
        e->total = 0;
        for (int i = 0; i < 256; i++) { e->freq[i] = (e->freq[i] >> 1) | 1; e->total += e->freq[i]; }
    }
    // Order-0 update
    m->order0_freq[sym] += CTX_ADAPT;
    m->order0_total += CTX_ADAPT;
    if (m->order0_total > 16000) {
        m->order0_total = 0;
        for (int i = 0; i < 256; i++) { m->order0_freq[i] = (m->order0_freq[i] >> 1) | 1; m->order0_total += m->order0_freq[i]; }
    }
}

static void model_encode(RC *r, Model *m, uint16_t ctx, uint8_t sym) {
    CtxEntry *e = model_get(m, ctx);
    uint32_t cum = 0;
    for (int i = 0; i < sym; i++) cum += e->freq[i];
    rc_enc(r, cum, e->freq[sym], e->total);
    model_update(m, ctx, sym);
}

static uint8_t model_decode(RC *r, Model *m, uint16_t ctx) {
    CtxEntry *e = model_get(m, ctx);
    uint32_t f = rc_get(r, e->total);
    uint32_t cum = 0;
    int sym = 0;
    while (cum + e->freq[sym] <= f) { cum += e->freq[sym]; sym++; }
    rc_dec(r, cum, e->freq[sym]);
    model_update(m, ctx, sym);
    return sym;
}

static void model_free(Model *m) {
    for (int i = 0; i < CTX_HASH_SIZE; i++) {
        CtxEntry *e = m->table[i];
        while (e) { CtxEntry *n = e->next; free(e); e = n; }
    }
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
        printf("║  KOLIBRI ULTRA v18.0 - BWT + MTF + Order-2 Range Coder         ║\n");
        printf("║  100%% внутренняя реализация - MAXIMUM COMPRESSION              ║\n");
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
        printf("║  KOLIBRI ULTRA v18.0 COMPRESSION                              ║\n");
        printf("╚═══════════════════════════════════════════════════════════════╝\n\n");
        printf("📄 Input: %s (%.2f KB)\n\n", inp, sz / 1024.0);
        
        double t0 = now();
        uint8_t *b1 = malloc(sz), *b2 = malloc(sz), *b3 = malloc(sz * 2);
        
        printf("🔄 BWT..."); fflush(stdout);
        uint32_t bwt_idx; bwt_enc(data, sz, b1, &bwt_idx);
        printf(" idx=%u\n", bwt_idx);
        
        printf("🔄 MTF..."); fflush(stdout);
        mtf_enc(b1, sz, b2);
        size_t zeros = 0; for (size_t i = 0; i < sz; i++) if (b2[i] == 0) zeros++;
        printf(" zeros=%.1f%%\n", 100.0 * zeros / sz);
        
        printf("🔄 Order-2 RC..."); fflush(stdout);
        
        // Order-2 encoding
        Model m; model_init(&m);
        size_t op = 4; // reserve for size
        RC rc; rc_init_enc(&rc, b3 + op, sz * 2 - op);
        
        uint8_t prev1 = 0, prev2 = 0;
        for (size_t i = 0; i < sz; i++) {
            uint16_t ctx = (prev2 << 8) | prev1;
            model_encode(&rc, &m, ctx, b2[i]);
            prev2 = prev1;
            prev1 = b2[i];
        }
        rc_fin(&rc);
        model_free(&m);
        
        // Write size
        b3[0] = (sz >> 24); b3[1] = (sz >> 16); b3[2] = (sz >> 8); b3[3] = sz;
        size_t s3 = op + rc.pos;
        
        printf(" %.2fx\n", (double)sz / s3);
        
        Header h = { MAGIC, 18, sz, s3, bwt_idx, crc32(data, sz) };
        FILE *fo = fopen(outp, "wb"); fwrite(&h, sizeof(h), 1, fo); fwrite(b3, 1, s3, fo); fclose(fo);
        
        double elapsed = now() - t0;
        size_t arch = sizeof(h) + s3;
        
        printf("\n╔═══════════════════════════════════════════════════════════════╗\n");
        printf("║  РЕЗУЛЬТАТ                                                    ║\n");
        printf("╠═══════════════════════════════════════════════════════════════╣\n");
        printf("║  Исходник:    %10.2f KB                                  ║\n", sz / 1024.0);
        printf("║  Архив:       %10.2f KB                                  ║\n", arch / 1024.0);
        printf("║  Сжатие:      %10.2fx                                    ║\n", (double)sz / arch);
        printf("║  Время:       %10.3f сек                                 ║\n", elapsed);
        printf("╚═══════════════════════════════════════════════════════════════╝\n\n");
        
        free(data); free(b1); free(b2); free(b3);
        
    } else if (strcmp(mode, "decompress") == 0) {
        FILE *f = fopen(inp, "rb"); if (!f) { printf("❌ Cannot open: %s\n", inp); return 1; }
        Header h; fread(&h, sizeof(h), 1, f);
        if (h.magic != MAGIC) { printf("❌ Invalid archive\n"); fclose(f); return 1; }
        uint8_t *comp = malloc(h.comp_size); fread(comp, 1, h.comp_size, f); fclose(f);
        
        printf("\n📦 Decompressing: %s (%.2fx)\n\n", inp, (double)h.orig_size / (h.comp_size + sizeof(h)));
        
        uint8_t *b1 = malloc(h.orig_size), *b2 = malloc(h.orig_size), *out = malloc(h.orig_size);
        
        printf("🔄 Order-2 RC..."); fflush(stdout);
        size_t orig = ((size_t)comp[0]<<24)|((size_t)comp[1]<<16)|((size_t)comp[2]<<8)|comp[3];
        
        Model m; model_init(&m);
        RC rc; rc_init_dec(&rc, comp + 4, h.comp_size - 4);
        
        uint8_t prev1 = 0, prev2 = 0;
        for (size_t i = 0; i < orig; i++) {
            uint16_t ctx = (prev2 << 8) | prev1;
            b1[i] = model_decode(&rc, &m, ctx);
            prev2 = prev1;
            prev1 = b1[i];
        }
        model_free(&m);
        printf(" %zu\n", orig);
        
        printf("🔄 MTF..."); fflush(stdout); mtf_dec(b1, orig, b2); printf(" %zu\n", orig);
        printf("🔄 BWT..."); fflush(stdout); bwt_dec(b2, orig, h.bwt_idx, out); printf(" %u\n", h.orig_size);
        
        if (crc32(out, h.orig_size) != h.checksum) { printf("❌ CRC mismatch!\n"); return 1; }
        
        FILE *fo = fopen(outp, "wb"); fwrite(out, 1, h.orig_size, fo); fclose(fo);
        printf("\n✅ Extracted: %s (%u bytes)\n\n", outp, h.orig_size);
        
        free(comp); free(b1); free(b2); free(out);
    }
    return 0;
}
