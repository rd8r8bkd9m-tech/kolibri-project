// ═══════════════════════════════════════════════════════════════
//   KOLIBRI PPM v19.0 - PPM Order-3 без BWT
//   Прямое контекстное сжатие исходного кода
// ═══════════════════════════════════════════════════════════════

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>

#define MAGIC 0x4B50504D  // "KPPM"

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
//   PPM ORDER-3 MODEL (хеш-таблица)
// ═══════════════════════════════════════════════════════════════

#define HASH_SIZE 262144  // 256K entries
#define ADAPT 4

typedef struct Node {
    uint32_t ctx;
    uint16_t freq[256];
    uint16_t total;
    uint8_t seen;  // количество уникальных символов
    struct Node *next;
} Node;

typedef struct {
    Node *table[HASH_SIZE];
    uint16_t order0[256];
    uint16_t total0;
} PPM;

static void ppm_init(PPM *p) {
    memset(p->table, 0, sizeof(p->table));
    for (int i = 0; i < 256; i++) p->order0[i] = 1;
    p->total0 = 256;
}

static Node* ppm_get(PPM *p, uint32_t ctx) {
    uint32_t h = (ctx * 2654435761u) % HASH_SIZE;
    Node *n = p->table[h];
    while (n && n->ctx != ctx) n = n->next;
    if (!n) {
        n = calloc(1, sizeof(Node));
        n->ctx = ctx;
        // Начинаем с нулевых частот (exclusion)
        n->total = 0;
        n->seen = 0;
        n->next = p->table[h];
        p->table[h] = n;
    }
    return n;
}

static void ppm_update(PPM *p, uint32_t ctx, uint8_t sym) {
    Node *n = ppm_get(p, ctx);
    if (n->freq[sym] == 0) n->seen++;
    n->freq[sym] += ADAPT;
    n->total += ADAPT;
    
    if (n->total > 8000) {
        n->total = 0;
        n->seen = 0;
        for (int i = 0; i < 256; i++) {
            n->freq[i] = n->freq[i] >> 1;
            if (n->freq[i] > 0) n->seen++;
            n->total += n->freq[i];
        }
    }
    
    // Order-0
    p->order0[sym] += ADAPT;
    p->total0 += ADAPT;
    if (p->total0 > 16000) {
        p->total0 = 0;
        for (int i = 0; i < 256; i++) {
            p->order0[i] = (p->order0[i] >> 1) | 1;
            p->total0 += p->order0[i];
        }
    }
}

// Кодируем с escape: если символ не найден в контексте, кодируем escape и спускаемся
static void ppm_encode(RC *r, PPM *p, uint32_t ctx3, uint32_t ctx2, uint32_t ctx1, uint8_t sym) {
    // Order-3
    Node *n3 = ppm_get(p, ctx3);
    if (n3->total > 0 && n3->freq[sym] > 0) {
        uint32_t cum = 0;
        for (int i = 0; i < sym; i++) cum += n3->freq[i];
        rc_enc(r, cum, n3->freq[sym], n3->total + n3->seen);  // +seen для escape
        ppm_update(p, ctx3, sym);
        ppm_update(p, ctx2, sym);
        ppm_update(p, ctx1, sym);
        return;
    }
    // Escape order-3
    if (n3->total > 0) {
        rc_enc(r, n3->total, n3->seen, n3->total + n3->seen);
    }
    
    // Order-2
    Node *n2 = ppm_get(p, ctx2);
    if (n2->total > 0 && n2->freq[sym] > 0) {
        uint32_t cum = 0;
        for (int i = 0; i < sym; i++) cum += n2->freq[i];
        rc_enc(r, cum, n2->freq[sym], n2->total + n2->seen);
        ppm_update(p, ctx3, sym);
        ppm_update(p, ctx2, sym);
        ppm_update(p, ctx1, sym);
        return;
    }
    if (n2->total > 0) {
        rc_enc(r, n2->total, n2->seen, n2->total + n2->seen);
    }
    
    // Order-1
    Node *n1 = ppm_get(p, ctx1);
    if (n1->total > 0 && n1->freq[sym] > 0) {
        uint32_t cum = 0;
        for (int i = 0; i < sym; i++) cum += n1->freq[i];
        rc_enc(r, cum, n1->freq[sym], n1->total + n1->seen);
        ppm_update(p, ctx3, sym);
        ppm_update(p, ctx2, sym);
        ppm_update(p, ctx1, sym);
        return;
    }
    if (n1->total > 0) {
        rc_enc(r, n1->total, n1->seen, n1->total + n1->seen);
    }
    
    // Order-0 (всегда есть)
    uint32_t cum = 0;
    for (int i = 0; i < sym; i++) cum += p->order0[i];
    rc_enc(r, cum, p->order0[sym], p->total0);
    ppm_update(p, ctx3, sym);
    ppm_update(p, ctx2, sym);
    ppm_update(p, ctx1, sym);
}

static uint8_t ppm_decode(RC *r, PPM *p, uint32_t ctx3, uint32_t ctx2, uint32_t ctx1) {
    // Order-3
    Node *n3 = ppm_get(p, ctx3);
    if (n3->total > 0) {
        uint32_t f = rc_get(r, n3->total + n3->seen);
        if (f < n3->total) {
            uint32_t cum = 0;
            int sym = 0;
            while (cum + n3->freq[sym] <= f) { cum += n3->freq[sym]; sym++; }
            rc_dec(r, cum, n3->freq[sym]);
            ppm_update(p, ctx3, sym);
            ppm_update(p, ctx2, sym);
            ppm_update(p, ctx1, sym);
            return sym;
        }
        rc_dec(r, n3->total, n3->seen);  // escape
    }
    
    // Order-2
    Node *n2 = ppm_get(p, ctx2);
    if (n2->total > 0) {
        uint32_t f = rc_get(r, n2->total + n2->seen);
        if (f < n2->total) {
            uint32_t cum = 0;
            int sym = 0;
            while (cum + n2->freq[sym] <= f) { cum += n2->freq[sym]; sym++; }
            rc_dec(r, cum, n2->freq[sym]);
            ppm_update(p, ctx3, sym);
            ppm_update(p, ctx2, sym);
            ppm_update(p, ctx1, sym);
            return sym;
        }
        rc_dec(r, n2->total, n2->seen);
    }
    
    // Order-1
    Node *n1 = ppm_get(p, ctx1);
    if (n1->total > 0) {
        uint32_t f = rc_get(r, n1->total + n1->seen);
        if (f < n1->total) {
            uint32_t cum = 0;
            int sym = 0;
            while (cum + n1->freq[sym] <= f) { cum += n1->freq[sym]; sym++; }
            rc_dec(r, cum, n1->freq[sym]);
            ppm_update(p, ctx3, sym);
            ppm_update(p, ctx2, sym);
            ppm_update(p, ctx1, sym);
            return sym;
        }
        rc_dec(r, n1->total, n1->seen);
    }
    
    // Order-0
    uint32_t f = rc_get(r, p->total0);
    uint32_t cum = 0;
    int sym = 0;
    while (cum + p->order0[sym] <= f) { cum += p->order0[sym]; sym++; }
    rc_dec(r, cum, p->order0[sym]);
    ppm_update(p, ctx3, sym);
    ppm_update(p, ctx2, sym);
    ppm_update(p, ctx1, sym);
    return sym;
}

static void ppm_free(PPM *p) {
    for (int i = 0; i < HASH_SIZE; i++) {
        Node *n = p->table[i];
        while (n) { Node *x = n->next; free(n); n = x; }
    }
}

// ═══════════════════════════════════════════════════════════════
//   MAIN
// ═══════════════════════════════════════════════════════════════

typedef struct { uint32_t magic, version, orig_size, comp_size, reserved, checksum; } __attribute__((packed)) Header;

static uint32_t crc32(const uint8_t *d, size_t sz) {
    uint32_t c = 0xFFFFFFFF; for (size_t i = 0; i < sz; i++) { c ^= d[i]; for (int j = 0; j < 8; j++) c = (c >> 1) ^ (0xEDB88320 & -(c & 1)); } return ~c;
}
static double now(void) { struct timeval tv; gettimeofday(&tv, NULL); return tv.tv_sec + tv.tv_usec / 1e6; }

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("\n╔════════════════════════════════════════════════════════════════╗\n");
        printf("║  KOLIBRI PPM v19.0 - Order-3 PPM                               ║\n");
        printf("║  100%% внутренняя реализация                                    ║\n");
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
        printf("║  KOLIBRI PPM v19.0 COMPRESSION                                ║\n");
        printf("╚═══════════════════════════════════════════════════════════════╝\n\n");
        printf("📄 Input: %s (%.2f KB)\n\n", inp, sz / 1024.0);
        
        double t0 = now();
        uint8_t *out = malloc(sz * 2);
        
        printf("🔄 PPM Order-3..."); fflush(stdout);
        
        PPM ppm; ppm_init(&ppm);
        size_t op = 4;
        RC rc; rc_init_enc(&rc, out + op, sz * 2 - op);
        
        uint8_t c1 = 0, c2 = 0, c3 = 0;
        for (size_t i = 0; i < sz; i++) {
            uint32_t ctx3 = (c3 << 16) | (c2 << 8) | c1;
            uint32_t ctx2 = (c2 << 8) | c1;
            uint32_t ctx1 = c1;
            ppm_encode(&rc, &ppm, ctx3, ctx2, ctx1, data[i]);
            c3 = c2; c2 = c1; c1 = data[i];
        }
        rc_fin(&rc);
        ppm_free(&ppm);
        
        out[0] = (sz >> 24); out[1] = (sz >> 16); out[2] = (sz >> 8); out[3] = sz;
        size_t comp = op + rc.pos;
        printf(" %.2fx\n", (double)sz / comp);
        
        Header h = { MAGIC, 19, sz, comp, 0, crc32(data, sz) };
        FILE *fo = fopen(outp, "wb"); fwrite(&h, sizeof(h), 1, fo); fwrite(out, 1, comp, fo); fclose(fo);
        
        double elapsed = now() - t0;
        size_t arch = sizeof(h) + comp;
        
        printf("\n╔═══════════════════════════════════════════════════════════════╗\n");
        printf("║  РЕЗУЛЬТАТ                                                    ║\n");
        printf("╠═══════════════════════════════════════════════════════════════╣\n");
        printf("║  Исходник:    %10.2f KB                                  ║\n", sz / 1024.0);
        printf("║  Архив:       %10.2f KB                                  ║\n", arch / 1024.0);
        printf("║  Сжатие:      %10.2fx                                    ║\n", (double)sz / arch);
        printf("║  Время:       %10.3f сек                                 ║\n", elapsed);
        printf("╚═══════════════════════════════════════════════════════════════╝\n\n");
        
        free(data); free(out);
        
    } else if (strcmp(mode, "decompress") == 0) {
        FILE *f = fopen(inp, "rb"); if (!f) { printf("❌ Cannot open: %s\n", inp); return 1; }
        Header h; fread(&h, sizeof(h), 1, f);
        if (h.magic != MAGIC) { printf("❌ Invalid archive\n"); fclose(f); return 1; }
        uint8_t *comp = malloc(h.comp_size); fread(comp, 1, h.comp_size, f); fclose(f);
        
        printf("\n📦 Decompressing: %s (%.2fx)\n\n", inp, (double)h.orig_size / (h.comp_size + sizeof(h)));
        
        uint8_t *out = malloc(h.orig_size);
        
        printf("🔄 PPM Order-3..."); fflush(stdout);
        
        size_t orig = ((size_t)comp[0]<<24)|((size_t)comp[1]<<16)|((size_t)comp[2]<<8)|comp[3];
        
        PPM ppm; ppm_init(&ppm);
        RC rc; rc_init_dec(&rc, comp + 4, h.comp_size - 4);
        
        uint8_t c1 = 0, c2 = 0, c3 = 0;
        for (size_t i = 0; i < orig; i++) {
            uint32_t ctx3 = (c3 << 16) | (c2 << 8) | c1;
            uint32_t ctx2 = (c2 << 8) | c1;
            uint32_t ctx1 = c1;
            out[i] = ppm_decode(&rc, &ppm, ctx3, ctx2, ctx1);
            c3 = c2; c2 = c1; c1 = out[i];
        }
        ppm_free(&ppm);
        printf(" %zu\n", orig);
        
        if (crc32(out, h.orig_size) != h.checksum) { printf("❌ CRC mismatch!\n"); return 1; }
        
        FILE *fo = fopen(outp, "wb"); fwrite(out, 1, h.orig_size, fo); fclose(fo);
        printf("\n✅ Extracted: %s (%u bytes)\n\n", outp, h.orig_size);
        
        free(comp); free(out);
    }
    return 0;
}
