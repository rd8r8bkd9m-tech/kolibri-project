/*
 * KOLIBRI FRACTAL v32
 * Трёхуровневая фрактальная вложенность
 * Уровни:
 *   L0: 0 / non-0
 *   L1: 1-3 / 4+
 *   L2: 4-9 / 10+
 * Значения: vals13 (1-3), vals49 (4-9), vals10+ (>=10)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* CRC32 */
static uint32_t crc32_table[256];
static void init_crc32(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) c = (c >> 1) ^ ((c & 1) ? 0xEDB88320 : 0);
        crc32_table[i] = c;
    }
}
static uint32_t calc_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFF;
}

/* BWT */
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

/* MTF */
static void mtf_encode(const uint8_t *in, size_t len, uint8_t *out) {
    uint8_t tbl[256];
    for (int i = 0; i < 256; i++) tbl[i] = i;
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
    for (int i = 0; i < 256; i++) tbl[i] = i;
    for (size_t i = 0; i < len; i++) {
        uint8_t r = in[i], c = tbl[r];
        out[i] = c;
        memmove(&tbl[1], &tbl[0], r);
        tbl[0] = c;
    }
}

/* Range Coder */
#define RC_TOP (1u << 24)
#define RC_BOT (1u << 16)

typedef struct { uint32_t low, range, code; const uint8_t *in_ptr; uint8_t *out_ptr; size_t out_size, out_cap; } RC;

/* Binary model with context */
typedef struct { uint16_t prob[2]; } BinModel;
static void bm_init(BinModel *m) { m->prob[0] = m->prob[1] = 2048; }
static void bm_update(BinModel *m, int ctx, int bit) {
    if (bit) m->prob[ctx] += (4096 - m->prob[ctx]) >> 5;
    else m->prob[ctx] -= m->prob[ctx] >> 5;
}

/* Order-1 256-symbol model */
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

/* 3-symbol model (для 1-3) */
typedef struct { uint16_t freq[4][5]; } Model3;
static void m3_init(Model3 *m) {
    for (int c = 0; c < 4; c++) {
        for (int s = 0; s < 3; s++) m->freq[c][s] = 1;
        m->freq[c][3] = 3;
    }
}
static void m3_update(Model3 *m, uint8_t c, uint8_t s) {
    if (c > 3) c = 3;
    m->freq[c][s] += 16; m->freq[c][3] += 16;
    if (m->freq[c][3] > 0x3FFF) {
        uint16_t t = 0;
        for (int i = 0; i < 3; i++) { m->freq[c][i] = (m->freq[c][i] >> 1) | 1; t += m->freq[c][i]; }
        m->freq[c][3] = t;
    }
}

/* 6-symbol model (для 4-9) */
typedef struct { uint16_t freq[7][8]; } Model6;
static void m6_init(Model6 *m) {
    for (int c = 0; c < 7; c++) {
        for (int s = 0; s < 6; s++) m->freq[c][s] = 1;
        m->freq[c][6] = 6;
    }
}
static void m6_update(Model6 *m, uint8_t c, uint8_t s) {
    if (c > 6) c = 6;
    m->freq[c][s] += 16; m->freq[c][6] += 16;
    if (m->freq[c][6] > 0x3FFF) {
        uint16_t t = 0;
        for (int i = 0; i < 6; i++) { m->freq[c][i] = (m->freq[c][i] >> 1) | 1; t += m->freq[c][i]; }
        m->freq[c][6] = t;
    }
}

static void rc_enc_init(RC *rc, uint8_t *out, size_t cap) {
    rc->low = 0; rc->range = 0xFFFFFFFF; rc->out_ptr = out; rc->out_size = 0; rc->out_cap = cap;
}
static void rc_enc_norm(RC *rc) {
    while ((rc->low ^ (rc->low + rc->range)) < RC_TOP ||
           (rc->range < RC_BOT && ((rc->range = -rc->low & (RC_BOT - 1)), 1))) {
        if (rc->out_size < rc->out_cap) rc->out_ptr[rc->out_size++] = rc->low >> 24;
        rc->low <<= 8; rc->range <<= 8;
    }
}
static void rc_enc_bit(RC *rc, BinModel *m, int ctx, int bit) {
    uint32_t bound = (rc->range >> 12) * m->prob[ctx];
    if (bit) { rc->range = bound; }
    else { rc->low += bound; rc->range -= bound; }
    rc_enc_norm(rc);
    bm_update(m, ctx, bit);
}
static void rc_enc_sym3(RC *rc, Model3 *m, uint8_t ctx, uint8_t sym) {
    if (ctx > 3) ctx = 3;
    uint32_t total = m->freq[ctx][3], cum = 0;
    for (int i = 0; i < sym; i++) cum += m->freq[ctx][i];
    rc->range /= total; rc->low += cum * rc->range; rc->range *= m->freq[ctx][sym];
    rc_enc_norm(rc);
    m3_update(m, ctx, sym);
}
static void rc_enc_sym6(RC *rc, Model6 *m, uint8_t ctx, uint8_t sym) {
    if (ctx > 6) ctx = 6;
    uint32_t total = m->freq[ctx][6], cum = 0;
    for (int i = 0; i < sym; i++) cum += m->freq[ctx][i];
    rc->range /= total; rc->low += cum * rc->range; rc->range *= m->freq[ctx][sym];
    rc_enc_norm(rc);
    m6_update(m, ctx, sym);
}
static void rc_enc_sym256(RC *rc, Model256 *m, uint8_t ctx, uint8_t sym) {
    uint32_t total = m->freq[ctx][256], cum = 0;
    for (int i = 0; i < sym; i++) cum += m->freq[ctx][i];
    rc->range /= total; rc->low += cum * rc->range; rc->range *= m->freq[ctx][sym];
    rc_enc_norm(rc);
    m256_update(m, ctx, sym);
}
static void rc_enc_flush(RC *rc) {
    for (int i = 0; i < 4; i++) {
        if (rc->out_size < rc->out_cap) rc->out_ptr[rc->out_size++] = rc->low >> 24;
        rc->low <<= 8;
    }
}

static void rc_dec_init(RC *rc, const uint8_t *in) {
    rc->low = 0; rc->range = 0xFFFFFFFF; rc->code = 0; rc->in_ptr = in;
    for (int i = 0; i < 4; i++) rc->code = (rc->code << 8) | *rc->in_ptr++;
}
static void rc_dec_norm(RC *rc) {
    while ((rc->low ^ (rc->low + rc->range)) < RC_TOP ||
           (rc->range < RC_BOT && ((rc->range = -rc->low & (RC_BOT - 1)), 1))) {
        rc->code = (rc->code << 8) | *rc->in_ptr++; rc->low <<= 8; rc->range <<= 8;
    }
}
static int rc_dec_bit(RC *rc, BinModel *m, int ctx) {
    uint32_t bound = (rc->range >> 12) * m->prob[ctx];
    int bit = (rc->code < rc->low + bound) ? 1 : 0;
    if (bit) rc->range = bound;
    else { rc->low += bound; rc->range -= bound; }
    rc_dec_norm(rc);
    bm_update(m, ctx, bit);
    return bit;
}
static uint8_t rc_dec_sym3(RC *rc, Model3 *m, uint8_t ctx) {
    if (ctx > 3) ctx = 3;
    uint32_t total = m->freq[ctx][3];
    rc->range /= total;
    uint32_t target = (rc->code - rc->low) / rc->range;
    uint32_t cum = 0; uint8_t sym = 0;
    while (sym < 2 && cum + m->freq[ctx][sym] <= target) { cum += m->freq[ctx][sym]; sym++; }
    rc->low += cum * rc->range; rc->range *= m->freq[ctx][sym];
    rc_dec_norm(rc);
    m3_update(m, ctx, sym);
    return sym;
}
static uint8_t rc_dec_sym6(RC *rc, Model6 *m, uint8_t ctx) {
    if (ctx > 6) ctx = 6;
    uint32_t total = m->freq[ctx][6];
    rc->range /= total;
    uint32_t target = (rc->code - rc->low) / rc->range;
    uint32_t cum = 0; uint8_t sym = 0;
    while (sym < 5 && cum + m->freq[ctx][sym] <= target) { cum += m->freq[ctx][sym]; sym++; }
    rc->low += cum * rc->range; rc->range *= m->freq[ctx][sym];
    rc_dec_norm(rc);
    m6_update(m, ctx, sym);
    return sym;
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

#define MAGIC 0x4B463332  /* "KF32" */

static int compress_file(const char *in_path, const char *out_path) {
    FILE *fin = fopen(in_path, "rb");
    if (!fin) { perror("fopen"); return 1; }
    fseek(fin, 0, SEEK_END);
    size_t in_size = ftell(fin);
    fseek(fin, 0, SEEK_SET);
    uint8_t *in_data = malloc(in_size);
    fread(in_data, 1, in_size, fin);
    fclose(fin);
    
    uint32_t crc = calc_crc32(in_data, in_size);
    
    uint8_t *bwt_out = malloc(in_size);
    size_t bwt_idx = bwt_encode(in_data, in_size, bwt_out);
    
    uint8_t *mtf_out = malloc(in_size);
    mtf_encode(bwt_out, in_size, mtf_out);
    free(bwt_out);
    
    /* Анализ */
    size_t cnt[4] = {0}; /* 0, 1-3, 4-9, 10+ */
    for (size_t i = 0; i < in_size; i++) {
        if (mtf_out[i] == 0) cnt[0]++;
        else if (mtf_out[i] <= 3) cnt[1]++;
        else if (mtf_out[i] <= 9) cnt[2]++;
        else cnt[3]++;
    }
    printf("=== 3-УРОВНЕВАЯ ФРАКТАЛЬНАЯ СТРУКТУРА ===\n");
    printf("L0 (=0):   %zu (%.1f%%)\n", cnt[0], 100.0*cnt[0]/in_size);
    printf("L1 (1-3):  %zu (%.1f%%)\n", cnt[1], 100.0*cnt[1]/in_size);
    printf("L2 (4-9):  %zu (%.1f%%)\n", cnt[2], 100.0*cnt[2]/in_size);
    printf("L3 (10+):  %zu (%.1f%%)\n", cnt[3], 100.0*cnt[3]/in_size);
    
    size_t buf = in_size + 1024;
    RC rc;
    
    /* Baseline Order-1 */
    uint8_t *comp_base = malloc(buf);
    Model256 *m256 = malloc(sizeof(Model256));
    m256_init(m256);
    rc_enc_init(&rc, comp_base, buf);
    uint8_t ctx256 = 0;
    for (size_t i = 0; i < in_size; i++) {
        rc_enc_sym256(&rc, m256, ctx256, mtf_out[i]);
        ctx256 = mtf_out[i];
    }
    rc_enc_flush(&rc);
    size_t size_base = rc.out_size;
    printf("Baseline Order-1: %zu байт (%.2fx)\n", size_base, (double)in_size/size_base);
    
    /* Fractal encoding */
    /* Bits L0: 0 vs non-0 */
    uint8_t *bits0 = malloc(buf);
    BinModel bm0; bm_init(&bm0);
    rc_enc_init(&rc, bits0, buf);
    int bctx = 0;
    for (size_t i = 0; i < in_size; i++) {
        int b = (mtf_out[i] != 0) ? 1 : 0;
        rc_enc_bit(&rc, &bm0, bctx, b);
        bctx = b;
    }
    rc_enc_flush(&rc);
    size_t size_bits0 = rc.out_size;
    
    /* Bits L1: 1-3 vs 4+ (for non-zero) */
    uint8_t *bits1 = malloc(buf);
    BinModel bm1; bm_init(&bm1);
    rc_enc_init(&rc, bits1, buf);
    bctx = 0;
    for (size_t i = 0; i < in_size; i++) {
        if (mtf_out[i] > 0) {
            int b = (mtf_out[i] > 3) ? 1 : 0;
            rc_enc_bit(&rc, &bm1, bctx, b);
            bctx = b;
        }
    }
    rc_enc_flush(&rc);
    size_t size_bits1 = rc.out_size;
    
    /* Bits L2: 4-9 vs 10+ (for 4+) */
    uint8_t *bits2 = malloc(buf);
    BinModel bm2; bm_init(&bm2);
    rc_enc_init(&rc, bits2, buf);
    bctx = 0;
    for (size_t i = 0; i < in_size; i++) {
        if (mtf_out[i] > 3) {
            int b = (mtf_out[i] > 9) ? 1 : 0;
            rc_enc_bit(&rc, &bm2, bctx, b);
            bctx = b;
        }
    }
    rc_enc_flush(&rc);
    size_t size_bits2 = rc.out_size;
    
    /* Values 1-3 */
    uint8_t *vals13 = malloc(buf);
    Model3 *m3 = malloc(sizeof(Model3));
    m3_init(m3);
    rc_enc_init(&rc, vals13, buf);
    uint8_t ctx3 = 0;
    for (size_t i = 0; i < in_size; i++) {
        if (mtf_out[i] >= 1 && mtf_out[i] <= 3) {
            rc_enc_sym3(&rc, m3, ctx3, mtf_out[i] - 1);  /* 1-3 -> 0-2 */
            ctx3 = mtf_out[i] - 1;
        }
    }
    rc_enc_flush(&rc);
    size_t size_vals13 = rc.out_size;
    
    /* Values 4-9 */
    uint8_t *vals49 = malloc(buf);
    Model6 *m6 = malloc(sizeof(Model6));
    m6_init(m6);
    rc_enc_init(&rc, vals49, buf);
    uint8_t ctx6 = 0;
    for (size_t i = 0; i < in_size; i++) {
        if (mtf_out[i] >= 4 && mtf_out[i] <= 9) {
            rc_enc_sym6(&rc, m6, ctx6, mtf_out[i] - 4);  /* 4-9 -> 0-5 */
            ctx6 = mtf_out[i] - 4;
        }
    }
    rc_enc_flush(&rc);
    size_t size_vals49 = rc.out_size;
    
    /* Values 10+ */
    uint8_t *vals10 = malloc(buf);
    m256_init(m256);
    rc_enc_init(&rc, vals10, buf);
    uint8_t ctx10 = 0;
    for (size_t i = 0; i < in_size; i++) {
        if (mtf_out[i] >= 10) {
            rc_enc_sym256(&rc, m256, ctx10, mtf_out[i] - 10);  /* 10-255 -> 0-245 */
            ctx10 = mtf_out[i] - 10;
        }
    }
    rc_enc_flush(&rc);
    size_t size_vals10 = rc.out_size;
    
    size_t size_frac = size_bits0 + size_bits1 + size_bits2 + size_vals13 + size_vals49 + size_vals10 + 24;
    printf("3-Level Fractal:\n");
    printf("  bits0 (0/non0):  %zu\n", size_bits0);
    printf("  bits1 (1-3/4+):  %zu\n", size_bits1);
    printf("  bits2 (4-9/10+): %zu\n", size_bits2);
    printf("  vals 1-3:        %zu\n", size_vals13);
    printf("  vals 4-9:        %zu\n", size_vals49);
    printf("  vals 10+:        %zu\n", size_vals10);
    printf("  ИТОГО: %zu байт (%.2fx)\n", size_frac, (double)in_size/size_frac);
    
    int method = (size_frac < size_base) ? 2 : 1;
    size_t best = (method == 2) ? size_frac : size_base;
    printf("Выбран метод %d\n", method);
    
    /* Write output */
    uint8_t *out = malloc(40 + best);
    size_t pos = 0;
    
    out[pos++] = (MAGIC >> 24) & 0xFF; out[pos++] = (MAGIC >> 16) & 0xFF;
    out[pos++] = (MAGIC >> 8) & 0xFF; out[pos++] = MAGIC & 0xFF;
    out[pos++] = (in_size >> 24) & 0xFF; out[pos++] = (in_size >> 16) & 0xFF;
    out[pos++] = (in_size >> 8) & 0xFF; out[pos++] = in_size & 0xFF;
    out[pos++] = (bwt_idx >> 24) & 0xFF; out[pos++] = (bwt_idx >> 16) & 0xFF;
    out[pos++] = (bwt_idx >> 8) & 0xFF; out[pos++] = bwt_idx & 0xFF;
    out[pos++] = (crc >> 24) & 0xFF; out[pos++] = (crc >> 16) & 0xFF;
    out[pos++] = (crc >> 8) & 0xFF; out[pos++] = crc & 0xFF;
    out[pos++] = method; out[pos++] = 0; out[pos++] = 0; out[pos++] = 0;
    
    if (method == 2) {
        /* 6 sizes */
        out[pos++] = (size_bits0 >> 24) & 0xFF; out[pos++] = (size_bits0 >> 16) & 0xFF;
        out[pos++] = (size_bits0 >> 8) & 0xFF; out[pos++] = size_bits0 & 0xFF;
        out[pos++] = (size_bits1 >> 24) & 0xFF; out[pos++] = (size_bits1 >> 16) & 0xFF;
        out[pos++] = (size_bits1 >> 8) & 0xFF; out[pos++] = size_bits1 & 0xFF;
        out[pos++] = (size_bits2 >> 24) & 0xFF; out[pos++] = (size_bits2 >> 16) & 0xFF;
        out[pos++] = (size_bits2 >> 8) & 0xFF; out[pos++] = size_bits2 & 0xFF;
        out[pos++] = (size_vals13 >> 24) & 0xFF; out[pos++] = (size_vals13 >> 16) & 0xFF;
        out[pos++] = (size_vals13 >> 8) & 0xFF; out[pos++] = size_vals13 & 0xFF;
        out[pos++] = (size_vals49 >> 24) & 0xFF; out[pos++] = (size_vals49 >> 16) & 0xFF;
        out[pos++] = (size_vals49 >> 8) & 0xFF; out[pos++] = size_vals49 & 0xFF;
        out[pos++] = (size_vals10 >> 24) & 0xFF; out[pos++] = (size_vals10 >> 16) & 0xFF;
        out[pos++] = (size_vals10 >> 8) & 0xFF; out[pos++] = size_vals10 & 0xFF;
        
        memcpy(out + pos, bits0, size_bits0); pos += size_bits0;
        memcpy(out + pos, bits1, size_bits1); pos += size_bits1;
        memcpy(out + pos, bits2, size_bits2); pos += size_bits2;
        memcpy(out + pos, vals13, size_vals13); pos += size_vals13;
        memcpy(out + pos, vals49, size_vals49); pos += size_vals49;
        memcpy(out + pos, vals10, size_vals10); pos += size_vals10;
    } else {
        memcpy(out + pos, comp_base, size_base); pos += size_base;
    }
    
    FILE *fout = fopen(out_path, "wb");
    fwrite(out, 1, pos, fout);
    fclose(fout);
    
    printf("\n=== РЕЗУЛЬТАТ ===\n");
    printf("Вход: %zu байт\n", in_size);
    printf("Выход: %zu байт\n", pos);
    printf("Степень сжатия: %.2fx\n", (double)in_size / pos);
    
    free(m256); free(m3); free(m6);
    free(comp_base); free(bits0); free(bits1); free(bits2);
    free(vals13); free(vals49); free(vals10);
    free(in_data); free(mtf_out); free(out);
    return 0;
}

static int decompress_file(const char *in_path, const char *out_path) {
    FILE *fin = fopen(in_path, "rb");
    if (!fin) { perror("fopen"); return 1; }
    fseek(fin, 0, SEEK_END);
    size_t in_size = ftell(fin);
    fseek(fin, 0, SEEK_SET);
    uint8_t *in_data = malloc(in_size);
    fread(in_data, 1, in_size, fin);
    fclose(fin);
    
    uint32_t magic = (in_data[0] << 24) | (in_data[1] << 16) | (in_data[2] << 8) | in_data[3];
    if (magic != MAGIC) { fprintf(stderr, "Invalid magic\n"); return 1; }
    
    uint32_t orig_size = (in_data[4] << 24) | (in_data[5] << 16) | (in_data[6] << 8) | in_data[7];
    uint32_t bwt_idx = (in_data[8] << 24) | (in_data[9] << 16) | (in_data[10] << 8) | in_data[11];
    uint32_t stored_crc = (in_data[12] << 24) | (in_data[13] << 16) | (in_data[14] << 8) | in_data[15];
    uint8_t method = in_data[16];
    
    uint8_t *mtf_out = malloc(orig_size);
    RC rc;
    size_t pos = 20;
    
    if (method == 2) {
        /* Read sizes */
        size_t size_bits0 = (in_data[pos] << 24) | (in_data[pos+1] << 16) | (in_data[pos+2] << 8) | in_data[pos+3]; pos += 4;
        size_t size_bits1 = (in_data[pos] << 24) | (in_data[pos+1] << 16) | (in_data[pos+2] << 8) | in_data[pos+3]; pos += 4;
        size_t size_bits2 = (in_data[pos] << 24) | (in_data[pos+1] << 16) | (in_data[pos+2] << 8) | in_data[pos+3]; pos += 4;
        size_t size_vals13 = (in_data[pos] << 24) | (in_data[pos+1] << 16) | (in_data[pos+2] << 8) | in_data[pos+3]; pos += 4;
        size_t size_vals49 = (in_data[pos] << 24) | (in_data[pos+1] << 16) | (in_data[pos+2] << 8) | in_data[pos+3]; pos += 4;
        size_t size_vals10 = (in_data[pos] << 24) | (in_data[pos+1] << 16) | (in_data[pos+2] << 8) | in_data[pos+3]; pos += 4;
        
        /* Decode bits0 */
        uint8_t *dec_bits0 = malloc(orig_size);
        BinModel bm0; bm_init(&bm0);
        rc_dec_init(&rc, in_data + pos);
        int bctx = 0;
        for (size_t i = 0; i < orig_size; i++) {
            dec_bits0[i] = rc_dec_bit(&rc, &bm0, bctx);
            bctx = dec_bits0[i];
        }
        pos += size_bits0;
        
        /* Count non-zeros */
        size_t nz = 0;
        for (size_t i = 0; i < orig_size; i++) if (dec_bits0[i]) nz++;
        
        /* Decode bits1 */
        uint8_t *dec_bits1 = malloc(nz + 1);
        BinModel bm1; bm_init(&bm1);
        rc_dec_init(&rc, in_data + pos);
        bctx = 0;
        for (size_t i = 0; i < nz; i++) {
            dec_bits1[i] = rc_dec_bit(&rc, &bm1, bctx);
            bctx = dec_bits1[i];
        }
        pos += size_bits1;
        
        /* Count level2 */
        size_t n2 = 0;
        for (size_t i = 0; i < nz; i++) if (dec_bits1[i]) n2++;
        
        /* Decode bits2 */
        uint8_t *dec_bits2 = malloc(n2 + 1);
        BinModel bm2; bm_init(&bm2);
        rc_dec_init(&rc, in_data + pos);
        bctx = 0;
        for (size_t i = 0; i < n2; i++) {
            dec_bits2[i] = rc_dec_bit(&rc, &bm2, bctx);
            bctx = dec_bits2[i];
        }
        pos += size_bits2;
        
        /* Count per group */
        size_t c13 = 0, c49 = 0, c10 = 0;
        for (size_t i = 0; i < nz; i++) {
            if (!dec_bits1[i]) c13++;
            else {
                size_t idx = 0;
                for (size_t j = 0; j < i; j++) if (dec_bits1[j]) idx++;
                if (!dec_bits2[idx]) c49++;
                else c10++;
            }
        }
        
        /* Decode vals13 */
        uint8_t *dec_vals13 = malloc(c13 + 1);
        Model3 *m3 = malloc(sizeof(Model3)); m3_init(m3);
        rc_dec_init(&rc, in_data + pos);
        uint8_t ctx3 = 0;
        for (size_t i = 0; i < c13; i++) {
            dec_vals13[i] = rc_dec_sym3(&rc, m3, ctx3);
            ctx3 = dec_vals13[i];
        }
        pos += size_vals13;
        
        /* Decode vals49 */
        uint8_t *dec_vals49 = malloc(c49 + 1);
        Model6 *m6 = malloc(sizeof(Model6)); m6_init(m6);
        rc_dec_init(&rc, in_data + pos);
        uint8_t ctx6 = 0;
        for (size_t i = 0; i < c49; i++) {
            dec_vals49[i] = rc_dec_sym6(&rc, m6, ctx6);
            ctx6 = dec_vals49[i];
        }
        pos += size_vals49;
        
        /* Decode vals10 */
        uint8_t *dec_vals10 = malloc(c10 + 1);
        Model256 *m256 = malloc(sizeof(Model256)); m256_init(m256);
        rc_dec_init(&rc, in_data + pos);
        uint8_t ctx10 = 0;
        for (size_t i = 0; i < c10; i++) {
            dec_vals10[i] = rc_dec_sym256(&rc, m256, ctx10);
            ctx10 = dec_vals10[i];
        }
        
        /* Reconstruct MTF */
        size_t i1 = 0, i2 = 0, i3 = 0, inz = 0, in2 = 0;
        for (size_t i = 0; i < orig_size; i++) {
            if (!dec_bits0[i]) {
                mtf_out[i] = 0;
            } else {
                if (!dec_bits1[inz]) {
                    mtf_out[i] = dec_vals13[i1++] + 1;  /* 0-2 -> 1-3 */
                } else {
                    if (!dec_bits2[in2]) {
                        mtf_out[i] = dec_vals49[i2++] + 4;  /* 0-5 -> 4-9 */
                    } else {
                        mtf_out[i] = dec_vals10[i3++] + 10;  /* 0-245 -> 10-255 */
                    }
                    in2++;
                }
                inz++;
            }
        }
        
        free(m3); free(m6); free(m256);
        free(dec_bits0); free(dec_bits1); free(dec_bits2);
        free(dec_vals13); free(dec_vals49); free(dec_vals10);
    } else {
        /* Baseline */
        Model256 *m256 = malloc(sizeof(Model256)); m256_init(m256);
        rc_dec_init(&rc, in_data + pos);
        uint8_t ctx = 0;
        for (size_t i = 0; i < orig_size; i++) {
            mtf_out[i] = rc_dec_sym256(&rc, m256, ctx);
            ctx = mtf_out[i];
        }
        free(m256);
    }
    
    /* MTF decode */
    uint8_t *bwt_out = malloc(orig_size);
    mtf_decode(mtf_out, orig_size, bwt_out);
    free(mtf_out);
    
    /* BWT decode */
    uint8_t *out_data = malloc(orig_size);
    bwt_decode(bwt_out, orig_size, bwt_idx, out_data);
    free(bwt_out);
    
    uint32_t calc_crc = calc_crc32(out_data, orig_size);
    printf("CRC: stored=%08X calc=%08X %s\n", stored_crc, calc_crc,
           stored_crc == calc_crc ? "OK" : "MISMATCH");
    
    FILE *fout = fopen(out_path, "wb");
    fwrite(out_data, 1, orig_size, fout);
    fclose(fout);
    
    printf("Распаковано: %u байт\n", orig_size);
    
    free(in_data); free(out_data);
    return (stored_crc == calc_crc) ? 0 : 1;
}

int main(int argc, char **argv) {
    init_crc32();
    
    if (argc < 4) {
        printf("Kolibri Fractal v32\n");
        printf("Usage: %s compress|decompress input output\n", argv[0]);
        return 1;
    }
    
    if (strcmp(argv[1], "compress") == 0) {
        return compress_file(argv[2], argv[3]);
    } else {
        return decompress_file(argv[2], argv[3]);
    }
}
