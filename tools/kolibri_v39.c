/*
 * KOLIBRI FRACTAL v39 - 10 уровней фрактальной вложенности
 * 0 / 1-3 / 4-9 / 10-15 / 16-23 / 24-31 / 32-47 / 48-63 / 64-95 / 96-127 / 128+
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static uint32_t crc32_table[256];
static void init_crc32(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) c = (c >> 1) ^ ((c & 1) ? 0xEDB88320 : 0);
        crc32_table[i] = c;
    }
}
static uint32_t calc_crc32(const uint8_t *d, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) crc = crc32_table[(crc ^ d[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFF;
}

static const uint8_t *g_bwt_data;
static size_t g_bwt_len;
static int bwt_cmp(const void *a, const void *b) {
    size_t i = *(const size_t *)a, j = *(const size_t *)b;
    for (size_t k = 0; k < g_bwt_len; k++) {
        uint8_t ci = g_bwt_data[(i + k) % g_bwt_len];
        uint8_t cj = g_bwt_data[(j + k) % g_bwt_len];
        if (ci != cj) return (int)ci - (int)cj;
    }
    return 0;
}
static size_t bwt_encode(const uint8_t *in, size_t len, uint8_t *out) {
    size_t *idx = malloc(len * sizeof(size_t));
    for (size_t i = 0; i < len; i++) idx[i] = i;
    g_bwt_data = in; g_bwt_len = len;
    qsort(idx, len, sizeof(size_t), bwt_cmp);
    size_t orig = 0;
    for (size_t i = 0; i < len; i++) {
        out[i] = in[(idx[i] + len - 1) % len];
        if (idx[i] == 0) orig = i;
    }
    free(idx);
    return orig;
}
static void bwt_decode(const uint8_t *L, size_t len, size_t I, uint8_t *out) {
    size_t C[256] = {0}, K[256], sum = 0;
    for (size_t i = 0; i < len; i++) C[L[i]]++;
    for (int c = 0; c < 256; c++) { K[c] = sum; sum += C[c]; }
    size_t *P = malloc(len * sizeof(size_t));
    size_t cnt[256] = {0};
    for (size_t i = 0; i < len; i++) P[i] = cnt[L[i]]++;
    size_t j = I;
    for (size_t i = len; i > 0; i--) { out[i - 1] = L[j]; j = K[L[j]] + P[j]; }
    free(P);
}

static void mtf_encode(const uint8_t *in, size_t len, uint8_t *out) {
    uint8_t tbl[256];
    for (int i = 0; i < 256; i++) tbl[i] = (uint8_t)i;
    for (size_t i = 0; i < len; i++) {
        uint8_t c = in[i], r = 0;
        while (tbl[r] != c) r++;
        out[i] = r;
        memmove(&tbl[1], &tbl[0], r);
        tbl[0] = c;
    }
}
static void mtf_decode(const uint8_t *in, size_t len, uint8_t *out) {
    uint8_t tbl[256];
    for (int i = 0; i < 256; i++) tbl[i] = (uint8_t)i;
    for (size_t i = 0; i < len; i++) {
        uint8_t r = in[i], c = tbl[r];
        out[i] = c;
        memmove(&tbl[1], &tbl[0], r);
        tbl[0] = c;
    }
}

#define RC_TOP (1u << 24)
#define RC_BOT (1u << 16)

typedef struct { 
    uint32_t low, range, code; 
    const uint8_t *in_ptr; 
    uint8_t *out_ptr; 
    size_t out_size, out_cap; 
} RC;

static void rc_enc_init(RC *rc, uint8_t *out, size_t cap) {
    rc->low = 0; rc->range = 0xFFFFFFFF; 
    rc->out_ptr = out; rc->out_size = 0; rc->out_cap = cap;
}
static void rc_enc_norm(RC *rc) {
    while ((rc->low ^ (rc->low + rc->range)) < RC_TOP ||
           (rc->range < RC_BOT && ((rc->range = (uint32_t)(-(int32_t)rc->low) & (RC_BOT - 1)), 1))) {
        if (rc->out_size < rc->out_cap) rc->out_ptr[rc->out_size++] = (uint8_t)(rc->low >> 24);
        rc->low <<= 8; rc->range <<= 8;
    }
}
static void rc_enc_flush(RC *rc) {
    for (int i = 0; i < 4; i++) {
        if (rc->out_size < rc->out_cap) rc->out_ptr[rc->out_size++] = (uint8_t)(rc->low >> 24);
        rc->low <<= 8;
    }
}
static void rc_dec_init(RC *rc, const uint8_t *in) {
    rc->low = 0; rc->range = 0xFFFFFFFF; rc->code = 0; rc->in_ptr = in;
    for (int i = 0; i < 4; i++) rc->code = (rc->code << 8) | *rc->in_ptr++;
}
static void rc_dec_norm(RC *rc) {
    while ((rc->low ^ (rc->low + rc->range)) < RC_TOP ||
           (rc->range < RC_BOT && ((rc->range = (uint32_t)(-(int32_t)rc->low) & (RC_BOT - 1)), 1))) {
        rc->code = (rc->code << 8) | *rc->in_ptr++; 
        rc->low <<= 8; rc->range <<= 8;
    }
}

/* Binary model Order-4 */
typedef struct { uint16_t prob[16]; } BinModel;
static void bm_init(BinModel *m) { 
    for (int i = 0; i < 16; i++) m->prob[i] = 2048; 
}
static void bm_update(BinModel *m, int ctx, int bit) {
    ctx &= 15;
    if (bit) m->prob[ctx] += (uint16_t)((4096 - m->prob[ctx]) >> 5);
    else m->prob[ctx] -= m->prob[ctx] >> 5;
}
static void rc_enc_bit(RC *rc, BinModel *m, int ctx, int bit) {
    ctx &= 15;
    uint32_t bound = (rc->range >> 12) * m->prob[ctx];
    if (bit) rc->range = bound;
    else { rc->low += bound; rc->range -= bound; }
    rc_enc_norm(rc);
    bm_update(m, ctx, bit);
}
static int rc_dec_bit(RC *rc, BinModel *m, int ctx) {
    ctx &= 15;
    uint32_t bound = (rc->range >> 12) * m->prob[ctx];
    int bit = (rc->code < rc->low + bound) ? 1 : 0;
    if (bit) rc->range = bound;
    else { rc->low += bound; rc->range -= bound; }
    rc_dec_norm(rc);
    bm_update(m, ctx, bit);
    return bit;
}

/* N-symbol model Order-2 */
typedef struct {
    uint16_t freq[64][33];
    int nsym;
} ModelN;
static void mn_init(ModelN *m, int nsym) {
    m->nsym = nsym;
    for (int c = 0; c < 64; c++) {
        for (int s = 0; s < nsym; s++) m->freq[c][s] = 1;
        m->freq[c][32] = (uint16_t)nsym;
    }
}
static void mn_update(ModelN *m, int ctx, int sym) {
    if (ctx >= 64) ctx = 63;
    m->freq[ctx][sym] += 16;
    m->freq[ctx][32] += 16;
    if (m->freq[ctx][32] > 0x3FFF) {
        m->freq[ctx][32] = 0;
        for (int i = 0; i < m->nsym; i++) {
            m->freq[ctx][i] = (m->freq[ctx][i] >> 1) | 1;
            m->freq[ctx][32] += m->freq[ctx][i];
        }
    }
}
static void rc_enc_symN(RC *rc, ModelN *m, int ctx, int sym) {
    if (ctx >= 64) ctx = 63;
    uint32_t cum = 0;
    for (int i = 0; i < sym; i++) cum += m->freq[ctx][i];
    rc->range /= m->freq[ctx][32];
    rc->low += cum * rc->range;
    rc->range *= m->freq[ctx][sym];
    rc_enc_norm(rc);
    mn_update(m, ctx, sym);
}
static int rc_dec_symN(RC *rc, ModelN *m, int ctx) {
    if (ctx >= 64) ctx = 63;
    rc->range /= m->freq[ctx][32];
    uint32_t target = (rc->code - rc->low) / rc->range;
    uint32_t cum = 0;
    int sym = 0;
    while (sym < m->nsym - 1 && cum + m->freq[ctx][sym] <= target) {
        cum += m->freq[ctx][sym];
        sym++;
    }
    rc->low += cum * rc->range;
    rc->range *= m->freq[ctx][sym];
    rc_dec_norm(rc);
    mn_update(m, ctx, sym);
    return sym;
}

/* Order-1 256-symbol */
typedef struct { uint16_t freq[256][257]; } Model256;
static void m256_init(Model256 *m) {
    for (int c = 0; c < 256; c++) {
        for (int s = 0; s < 256; s++) m->freq[c][s] = 1;
        m->freq[c][256] = 256;
    }
}
static void m256_update(Model256 *m, uint8_t c, uint8_t s) {
    m->freq[c][s] += 8; m->freq[c][256] += 8;
    if (m->freq[c][256] > 0x3FFF) {
        uint16_t t = 0;
        for (int i = 0; i < 256; i++) { m->freq[c][i] = (m->freq[c][i] >> 1) | 1; t += m->freq[c][i]; }
        m->freq[c][256] = t;
    }
}
static void rc_enc_sym256(RC *rc, Model256 *m, uint8_t ctx, uint8_t sym) {
    uint32_t total = m->freq[ctx][256], cum = 0;
    for (int i = 0; i < sym; i++) cum += m->freq[ctx][i];
    rc->range /= total; rc->low += cum * rc->range; rc->range *= m->freq[ctx][sym];
    rc_enc_norm(rc);
    m256_update(m, ctx, sym);
}
static uint8_t rc_dec_sym256(RC *rc, Model256 *m, uint8_t ctx) {
    uint32_t total = m->freq[ctx][256];
    rc->range /= total;
    uint32_t target = (rc->code - rc->low) / rc->range;
    uint32_t cum = 0; uint8_t sym = 0;
    while (cum + m->freq[ctx][sym] <= target) { cum += m->freq[ctx][sym]; sym++; }
    rc->low += cum * rc->range; rc->range *= m->freq[ctx][sym];
    rc_dec_norm(rc);
    m256_update(m, ctx, sym);
    return sym;
}

static void write32(uint8_t *out, size_t *pos, uint32_t x) {
    out[(*pos)++] = (uint8_t)(x >> 24);
    out[(*pos)++] = (uint8_t)(x >> 16);
    out[(*pos)++] = (uint8_t)(x >> 8);
    out[(*pos)++] = (uint8_t)x;
}
static uint32_t read32(const uint8_t *in, size_t *pos) {
    uint32_t x = ((uint32_t)in[*pos] << 24) | ((uint32_t)in[*pos+1] << 16) | 
                 ((uint32_t)in[*pos+2] << 8) | in[*pos+3];
    *pos += 4;
    return x;
}

#define MAGIC 0x4B463339u
#define NLEV 10

/* Границы групп: 0 / 1-3 / 4-9 / 10-15 / 16-23 / 24-31 / 32-47 / 48-63 / 64-95 / 96-127 / 128+ */
static const uint8_t bounds[NLEV] = {0, 3, 9, 15, 23, 31, 47, 63, 95, 127};
static const int gsizes[NLEV] = {1, 3, 6, 6, 8, 8, 16, 16, 32, 32}; /* размер алфавита группы */

static int get_group(uint8_t v) {
    if (v == 0) return 0;
    for (int g = 1; g < NLEV; g++) {
        if (v <= bounds[g]) return g;
    }
    return NLEV;
}

static int compress_file(const char *in_path, const char *out_path) {
    FILE *fin = fopen(in_path, "rb");
    if (!fin) return 1;
    fseek(fin, 0, SEEK_END);
    size_t in_size = (size_t)ftell(fin);
    fseek(fin, 0, SEEK_SET);
    uint8_t *in_data = malloc(in_size);
    if (fread(in_data, 1, in_size, fin) != in_size) { fclose(fin); free(in_data); return 1; }
    fclose(fin);
    
    uint32_t crc = calc_crc32(in_data, in_size);
    
    uint8_t *bwt_out = malloc(in_size);
    size_t bwt_idx = bwt_encode(in_data, in_size, bwt_out);
    
    uint8_t *mtf = malloc(in_size);
    mtf_encode(bwt_out, in_size, mtf);
    free(bwt_out);
    
    /* Статистика по группам */
    size_t cnt[NLEV + 1] = {0};
    for (size_t i = 0; i < in_size; i++) {
        cnt[get_group(mtf[i])]++;
    }
    
    printf("=== v39 (10 уровней) ===\n");
    printf("Группы: ");
    for (int g = 0; g <= NLEV; g++) printf("%zu ", cnt[g]);
    printf("\n");
    
    size_t buf = in_size + 1024;
    RC rc;
    
    /* Битовые потоки B0..B9 */
    uint8_t *bits[NLEV];
    size_t sb[NLEV];
    BinModel bm[NLEV];
    
    for (int lev = 0; lev < NLEV; lev++) {
        bits[lev] = malloc(buf);
        bm_init(&bm[lev]);
        rc_enc_init(&rc, bits[lev], buf);
        int ctx = 0;
        
        for (size_t i = 0; i < in_size; i++) {
            int g = get_group(mtf[i]);
            /* Проверяем нужно ли кодировать этот бит */
            int need = 1;
            for (int l = 0; l < lev; l++) {
                if (get_group(mtf[i]) <= l) { need = 0; break; }
            }
            if (need && g >= lev) {
                int b = (g > lev) ? 1 : 0;
                rc_enc_bit(&rc, &bm[lev], ctx, b);
                ctx = (ctx << 1) | b;
            }
        }
        rc_enc_flush(&rc);
        sb[lev] = rc.out_size;
    }
    
    /* Потоки значений V1..V9 (группы 1-9, для 0 и 10 не нужны) */
    uint8_t *vals[NLEV];
    size_t sv[NLEV];
    ModelN mn[NLEV];
    
    for (int g = 1; g < NLEV; g++) {
        vals[g] = malloc(buf);
        mn_init(&mn[g], gsizes[g]);
        rc_enc_init(&rc, vals[g], buf);
        int p1 = 0, p2 = 0;
        
        uint8_t lo = (g == 1) ? 1 : (bounds[g-1] + 1);
        uint8_t hi = bounds[g];
        
        for (size_t i = 0; i < in_size; i++) {
            if (mtf[i] >= lo && mtf[i] <= hi) {
                int sym = mtf[i] - lo;
                int ctx2 = (p2 % 8) * 8 + (p1 % 8);
                if (ctx2 >= 64) ctx2 = 63;
                rc_enc_symN(&rc, &mn[g], ctx2, sym);
                p2 = p1; p1 = sym;
            }
        }
        rc_enc_flush(&rc);
        sv[g] = rc.out_size;
    }
    
    /* V10: 128+ Order-1 */
    uint8_t *v10 = malloc(buf);
    Model256 *m256 = malloc(sizeof(Model256));
    m256_init(m256);
    rc_enc_init(&rc, v10, buf);
    uint8_t ctx256 = 0;
    for (size_t i = 0; i < in_size; i++) {
        if (mtf[i] >= 128) {
            uint8_t sym = mtf[i] - 128;
            rc_enc_sym256(&rc, m256, ctx256, sym);
            ctx256 = sym;
        }
    }
    rc_enc_flush(&rc);
    size_t sv10 = rc.out_size;
    
    /* Итого */
    size_t total = 16 + 4 * NLEV + 4 * (NLEV - 1) + 4;  /* header + sizes */
    for (int l = 0; l < NLEV; l++) total += sb[l];
    for (int g = 1; g < NLEV; g++) total += sv[g];
    total += sv10;
    
    printf("Биты: ");
    for (int l = 0; l < NLEV; l++) printf("B%d=%zu ", l, sb[l]);
    printf("\n");
    printf("Значения: ");
    for (int g = 1; g < NLEV; g++) {
        if (cnt[g] > 0) printf("V%d=%zu(%.2fbpv) ", g, sv[g], 8.0*sv[g]/cnt[g]);
    }
    printf("V10=%zu(%.2fbpv)\n", sv10, cnt[NLEV] > 0 ? 8.0*sv10/cnt[NLEV] : 0.0);
    printf("ИТОГО: %zu (%.2fx)\n", total, (double)in_size/total);
    
    /* Запись */
    uint8_t *out = malloc(total + 100);
    size_t pos = 0;
    
    write32(out, &pos, MAGIC);
    write32(out, &pos, (uint32_t)in_size);
    write32(out, &pos, (uint32_t)bwt_idx);
    write32(out, &pos, crc);
    
    for (int l = 0; l < NLEV; l++) write32(out, &pos, (uint32_t)sb[l]);
    for (int g = 1; g < NLEV; g++) write32(out, &pos, (uint32_t)sv[g]);
    write32(out, &pos, (uint32_t)sv10);
    
    for (int l = 0; l < NLEV; l++) { memcpy(out + pos, bits[l], sb[l]); pos += sb[l]; }
    for (int g = 1; g < NLEV; g++) { memcpy(out + pos, vals[g], sv[g]); pos += sv[g]; }
    memcpy(out + pos, v10, sv10); pos += sv10;
    
    FILE *fout = fopen(out_path, "wb");
    fwrite(out, 1, pos, fout);
    fclose(fout);
    
    printf("Выход: %zu (%.2fx)\n", pos, (double)in_size/pos);
    
    free(m256);
    for (int l = 0; l < NLEV; l++) free(bits[l]);
    for (int g = 1; g < NLEV; g++) free(vals[g]);
    free(v10);
    free(in_data); free(mtf); free(out);
    return 0;
}

static int decompress_file(const char *in_path, const char *out_path) {
    FILE *fin = fopen(in_path, "rb");
    if (!fin) return 1;
    fseek(fin, 0, SEEK_END);
    size_t in_size = (size_t)ftell(fin);
    fseek(fin, 0, SEEK_SET);
    uint8_t *in_data = malloc(in_size);
    if (fread(in_data, 1, in_size, fin) != in_size) { fclose(fin); free(in_data); return 1; }
    fclose(fin);
    
    size_t pos = 0;
    
    uint32_t magic = read32(in_data, &pos);
    if (magic != MAGIC) { fprintf(stderr, "Bad magic\n"); free(in_data); return 1; }
    
    uint32_t orig = read32(in_data, &pos);
    uint32_t bwt_idx = read32(in_data, &pos);
    uint32_t stored_crc = read32(in_data, &pos);
    
    uint32_t sb[NLEV], sv[NLEV], sv10;
    for (int l = 0; l < NLEV; l++) sb[l] = read32(in_data, &pos);
    for (int g = 1; g < NLEV; g++) sv[g] = read32(in_data, &pos);
    sv10 = read32(in_data, &pos);
    
    RC rc;
    
    /* Декодируем биты и определяем группу каждого символа */
    uint8_t *groups = malloc(orig);
    memset(groups, (int)NLEV, orig); /* по умолчанию - группа 10 (128+) */
    
    /* Декодируем B0: 0 vs non-0 */
    BinModel bm0; bm_init(&bm0);
    rc_dec_init(&rc, in_data + pos);
    int ctx = 0;
    for (size_t i = 0; i < orig; i++) {
        int b = rc_dec_bit(&rc, &bm0, ctx);
        ctx = (ctx << 1) | b;
        if (!b) groups[i] = 0;
    }
    pos += sb[0];
    
    /* Декодируем B1..B9 */
    for (int lev = 1; lev < NLEV; lev++) {
        BinModel bm; bm_init(&bm);
        rc_dec_init(&rc, in_data + pos);
        ctx = 0;
        
        for (size_t i = 0; i < orig; i++) {
            if (groups[i] >= lev) {  /* ещё не определена окончательно */
                int b = rc_dec_bit(&rc, &bm, ctx);
                ctx = (ctx << 1) | b;
                if (!b) groups[i] = (uint8_t)lev;
            }
        }
        pos += sb[lev];
    }
    
    /* Подсчёт элементов в каждой группе */
    size_t gcnt[NLEV + 1] = {0};
    for (size_t i = 0; i < orig; i++) gcnt[groups[i]]++;
    
    /* Декодируем значения V1..V9 */
    uint8_t *dv[NLEV];
    ModelN mn[NLEV];
    
    for (int g = 1; g < NLEV; g++) {
        dv[g] = malloc(gcnt[g] + 1);
        mn_init(&mn[g], gsizes[g]);
        rc_dec_init(&rc, in_data + pos);
        int p1 = 0, p2 = 0;
        
        for (size_t i = 0; i < gcnt[g]; i++) {
            int ctx2 = (p2 % 8) * 8 + (p1 % 8);
            if (ctx2 >= 64) ctx2 = 63;
            dv[g][i] = (uint8_t)rc_dec_symN(&rc, &mn[g], ctx2);
            p2 = p1; p1 = dv[g][i];
        }
        pos += sv[g];
    }
    
    /* Декодируем V10 */
    uint8_t *dv10 = malloc(gcnt[NLEV] + 1);
    Model256 *m256 = malloc(sizeof(Model256));
    m256_init(m256);
    rc_dec_init(&rc, in_data + pos);
    uint8_t ctx256 = 0;
    for (size_t i = 0; i < gcnt[NLEV]; i++) {
        dv10[i] = rc_dec_sym256(&rc, m256, ctx256);
        ctx256 = dv10[i];
    }
    free(m256);
    
    /* Реконструкция MTF */
    uint8_t *mtf = malloc(orig);
    size_t idx[NLEV + 1] = {0};
    
    for (size_t i = 0; i < orig; i++) {
        int g = groups[i];
        if (g == 0) {
            mtf[i] = 0;
        } else if (g < NLEV) {
            uint8_t lo = (g == 1) ? 1 : (bounds[g-1] + 1);
            mtf[i] = lo + dv[g][idx[g]++];
        } else {
            mtf[i] = 128 + dv10[idx[NLEV]++];
        }
    }
    
    free(groups);
    for (int g = 1; g < NLEV; g++) free(dv[g]);
    free(dv10);
    
    uint8_t *bwt_out = malloc(orig);
    mtf_decode(mtf, orig, bwt_out);
    free(mtf);
    
    uint8_t *out = malloc(orig);
    bwt_decode(bwt_out, orig, bwt_idx, out);
    free(bwt_out);
    
    uint32_t calc_crc = calc_crc32(out, orig);
    printf("CRC: %08X vs %08X %s\n", stored_crc, calc_crc, stored_crc==calc_crc?"OK":"FAIL");
    
    FILE *fout = fopen(out_path, "wb");
    fwrite(out, 1, orig, fout);
    fclose(fout);
    
    printf("Распаковано: %u\n", orig);
    
    free(in_data); free(out);
    return stored_crc == calc_crc ? 0 : 1;
}

int main(int argc, char **argv) {
    init_crc32();
    if (argc < 4) {
        printf("Kolibri v39 (10 levels)\nUsage: %s compress|decompress in out\n", argv[0]);
        return 1;
    }
    return strcmp(argv[1], "compress") == 0 ? 
           compress_file(argv[2], argv[3]) : 
           decompress_file(argv[2], argv[3]);
}
