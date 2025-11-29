/*
 * KOLIBRI FRACTAL v27
 * Двухуровневая фрактальная вложенность + Логические числа 0-9
 * 
 * Улучшение: После разделения на биты и значения,
 * применяем то же разделение к значениям (ненулевые < 10 и >= 10)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* CRC32 */
static uint32_t crc32_table[256];
static int crc32_init = 0;
static void init_crc32(void) {
    if (crc32_init) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) c = (c >> 1) ^ ((c & 1) ? 0xEDB88320 : 0);
        crc32_table[i] = c;
    }
    crc32_init = 1;
}
static uint32_t calc_crc32(const uint8_t *data, size_t len) {
    init_crc32();
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

/* Binary model */
typedef struct { uint16_t f0, f1, total; } BinModel;
static void bm_init(BinModel *m) { m->f0 = m->f1 = 1; m->total = 2; }
static void bm_update(BinModel *m, uint8_t bit) {
    if (bit) m->f1 += 16; else m->f0 += 16;
    m->total += 16;
    if (m->total > 0x3FFF) {
        m->f0 = (m->f0 >> 1) | 1; m->f1 = (m->f1 >> 1) | 1;
        m->total = m->f0 + m->f1;
    }
}

/* Order-1 binary model */
typedef struct { BinModel ctx[2]; } BinModel1;
static void bm1_init(BinModel1 *m) { bm_init(&m->ctx[0]); bm_init(&m->ctx[1]); }

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

/* Order-1 10-symbol model (для логических чисел 1-9) */
typedef struct { uint16_t freq[10][11]; } Model10;
static void m10_init(Model10 *m) {
    for (int c = 0; c < 10; c++) {
        for (int s = 0; s < 10; s++) m->freq[c][s] = 1;
        m->freq[c][10] = 10;
    }
}
static void m10_update(Model10 *m, uint8_t c, uint8_t s) {
    if (c > 9) c = 9; if (s > 9) s = 9;
    m->freq[c][s] += 16; m->freq[c][10] += 16;
    if (m->freq[c][10] > 0x3FFF) {
        uint16_t t = 0;
        for (int i = 0; i < 10; i++) { m->freq[c][i] = (m->freq[c][i] >> 1) | 1; t += m->freq[c][i]; }
        m->freq[c][10] = t;
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
static void rc_enc_bit(RC *rc, BinModel *m, uint8_t bit) {
    rc->range /= m->total;
    if (bit) { rc->low += m->f0 * rc->range; rc->range *= m->f1; }
    else { rc->range *= m->f0; }
    rc_enc_norm(rc);
    bm_update(m, bit);
}
static void rc_enc_sym256(RC *rc, Model256 *m, uint8_t ctx, uint8_t sym) {
    uint32_t total = m->freq[ctx][256], cum = 0;
    for (int i = 0; i < sym; i++) cum += m->freq[ctx][i];
    rc->range /= total; rc->low += cum * rc->range; rc->range *= m->freq[ctx][sym];
    rc_enc_norm(rc);
    m256_update(m, ctx, sym);
}
static void rc_enc_sym10(RC *rc, Model10 *m, uint8_t ctx, uint8_t sym) {
    if (ctx > 9) ctx = 9; if (sym > 9) sym = 9;
    uint32_t total = m->freq[ctx][10], cum = 0;
    for (int i = 0; i < sym; i++) cum += m->freq[ctx][i];
    rc->range /= total; rc->low += cum * rc->range; rc->range *= m->freq[ctx][sym];
    rc_enc_norm(rc);
    m10_update(m, ctx, sym);
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
static uint8_t rc_dec_bit(RC *rc, BinModel *m) {
    rc->range /= m->total;
    uint32_t th = m->f0 * rc->range;
    uint8_t bit = (rc->code - rc->low >= th) ? 1 : 0;
    if (bit) { rc->low += th; rc->range *= m->f1; } else { rc->range *= m->f0; }
    rc_dec_norm(rc);
    bm_update(m, bit);
    return bit;
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
static uint8_t rc_dec_sym10(RC *rc, Model10 *m, uint8_t ctx) {
    if (ctx > 9) ctx = 9;
    uint32_t total = m->freq[ctx][10];
    rc->range /= total;
    uint32_t target = (rc->code - rc->low) / rc->range;
    uint32_t cum = 0; uint8_t sym = 0;
    while (sym < 9 && cum + m->freq[ctx][sym] <= target) { cum += m->freq[ctx][sym]; sym++; }
    rc->low += cum * rc->range; rc->range *= m->freq[ctx][sym];
    rc_dec_norm(rc);
    m10_update(m, ctx, sym);
    return sym;
}

#define MAGIC 0x4B465237  /* "KFR7" */

static int compress_file(const char *in_path, const char *out_path) {
    FILE *fin = fopen(in_path, "rb");
    if (!fin) { perror("fopen"); return 1; }
    fseek(fin, 0, SEEK_END);
    size_t in_size = ftell(fin);
    fseek(fin, 0, SEEK_SET);
    uint8_t *in_data = malloc(in_size);
    if (fread(in_data, 1, in_size, fin) != in_size) { fclose(fin); return 1; }
    fclose(fin);
    
    uint32_t crc = calc_crc32(in_data, in_size);
    
    uint8_t *bwt_out = malloc(in_size);
    size_t bwt_idx = bwt_encode(in_data, in_size, bwt_out);
    
    uint8_t *mtf_out = malloc(in_size);
    mtf_encode(bwt_out, in_size, mtf_out);
    free(bwt_out);
    
    /* Анализ */
    size_t z = 0, s = 0, l = 0;  /* zeros, small(1-9), large(>=10) */
    for (size_t i = 0; i < in_size; i++) {
        if (mtf_out[i] == 0) z++;
        else if (mtf_out[i] < 10) s++;
        else l++;
    }
    printf("=== ФРАКТАЛЬНАЯ СТРУКТУРА ===\n");
    printf("Уровень 0 (нули): %zu (%.1f%%)\n", z, 100.0*z/in_size);
    printf("Уровень 1 (1-9):  %zu (%.1f%%)\n", s, 100.0*s/in_size);
    printf("Уровень 2 (>=10): %zu (%.1f%%)\n", l, 100.0*l/in_size);
    
    size_t buf = in_size + 1024;
    
    /* Метод 1: Order-1 baseline */
    uint8_t *comp1 = malloc(buf);
    RC rc;
    Model256 *m256 = malloc(sizeof(Model256));
    m256_init(m256);
    rc_enc_init(&rc, comp1, buf);
    uint8_t ctx = 0;
    for (size_t i = 0; i < in_size; i++) {
        rc_enc_sym256(&rc, m256, ctx, mtf_out[i]);
        ctx = mtf_out[i];
    }
    rc_enc_flush(&rc);
    size_t size1 = rc.out_size;
    printf("Baseline Order-1: %zu байт (%.2fx)\n", size1, (double)in_size/size1);
    
    /* Метод 2: Двухуровневое фрактальное */
    /* Уровень 1: бит нуль/ненуль */
    uint8_t *comp_b0 = malloc(buf);
    BinModel1 bm0;
    bm1_init(&bm0);
    rc_enc_init(&rc, comp_b0, buf);
    uint8_t bctx = 0;
    for (size_t i = 0; i < in_size; i++) {
        uint8_t b = (mtf_out[i] != 0) ? 1 : 0;
        rc_enc_bit(&rc, &bm0.ctx[bctx], b);
        bctx = b;
    }
    rc_enc_flush(&rc);
    size_t size_b0 = rc.out_size;
    
    /* Уровень 2: для ненулевых - бит малый/большой (< 10 или >= 10) */
    size_t nz = s + l;
    uint8_t *comp_b1 = malloc(buf);
    BinModel1 bm1;
    bm1_init(&bm1);
    rc_enc_init(&rc, comp_b1, buf);
    bctx = 0;
    for (size_t i = 0; i < in_size; i++) {
        if (mtf_out[i] != 0) {
            uint8_t b = (mtf_out[i] >= 10) ? 1 : 0;
            rc_enc_bit(&rc, &bm1.ctx[bctx], b);
            bctx = b;
        }
    }
    rc_enc_flush(&rc);
    size_t size_b1 = rc.out_size;
    
    /* Малые значения (1-9) - используем Model10 */
    uint8_t *comp_small = malloc(buf);
    Model10 *m10 = malloc(sizeof(Model10));
    m10_init(m10);
    rc_enc_init(&rc, comp_small, buf);
    ctx = 0;
    for (size_t i = 0; i < in_size; i++) {
        if (mtf_out[i] > 0 && mtf_out[i] < 10) {
            rc_enc_sym10(&rc, m10, ctx, mtf_out[i] - 1);  /* 1-9 -> 0-8 */
            ctx = mtf_out[i] - 1;
        }
    }
    rc_enc_flush(&rc);
    size_t size_small = rc.out_size;
    
    /* Большие значения (>=10) - используем Model256 */
    uint8_t *comp_large = malloc(buf);
    m256_init(m256);
    rc_enc_init(&rc, comp_large, buf);
    ctx = 0;
    for (size_t i = 0; i < in_size; i++) {
        if (mtf_out[i] >= 10) {
            rc_enc_sym256(&rc, m256, ctx, mtf_out[i] - 10);  /* 10-255 -> 0-245 */
            ctx = mtf_out[i] - 10;
        }
    }
    rc_enc_flush(&rc);
    size_t size_large = rc.out_size;
    
    size_t size2 = size_b0 + size_b1 + size_small + size_large + 16;  /* +16 для размеров */
    printf("Фрактальный v2:\n");
    printf("  Биты0 (нуль): %zu байт\n", size_b0);
    printf("  Биты1 (<10):  %zu байт\n", size_b1);
    printf("  Малые (1-9):  %zu байт\n", size_small);
    printf("  Большие:      %zu байт\n", size_large);
    printf("  ИТОГО: %zu байт (%.2fx)\n", size2, (double)in_size/size2);
    
    /* Выбираем лучший */
    int method = (size2 < size1) ? 2 : 1;
    size_t best = (method == 2) ? size2 : size1;
    printf("Выбран метод %d\n", method);
    
    /* Запись */
    size_t hdr = 20;
    uint8_t *out = malloc(hdr + best);
    
    out[0] = (MAGIC >> 24) & 0xFF; out[1] = (MAGIC >> 16) & 0xFF;
    out[2] = (MAGIC >> 8) & 0xFF; out[3] = MAGIC & 0xFF;
    out[4] = (in_size >> 24) & 0xFF; out[5] = (in_size >> 16) & 0xFF;
    out[6] = (in_size >> 8) & 0xFF; out[7] = in_size & 0xFF;
    out[8] = (bwt_idx >> 24) & 0xFF; out[9] = (bwt_idx >> 16) & 0xFF;
    out[10] = (bwt_idx >> 8) & 0xFF; out[11] = bwt_idx & 0xFF;
    out[12] = (crc >> 24) & 0xFF; out[13] = (crc >> 16) & 0xFF;
    out[14] = (crc >> 8) & 0xFF; out[15] = crc & 0xFF;
    out[16] = method; out[17] = out[18] = out[19] = 0;
    
    size_t pos = 20;
    if (method == 2) {
        /* 4 размера + 4 потока */
        out[pos++] = (size_b0 >> 24) & 0xFF; out[pos++] = (size_b0 >> 16) & 0xFF;
        out[pos++] = (size_b0 >> 8) & 0xFF; out[pos++] = size_b0 & 0xFF;
        out[pos++] = (size_b1 >> 24) & 0xFF; out[pos++] = (size_b1 >> 16) & 0xFF;
        out[pos++] = (size_b1 >> 8) & 0xFF; out[pos++] = size_b1 & 0xFF;
        out[pos++] = (size_small >> 24) & 0xFF; out[pos++] = (size_small >> 16) & 0xFF;
        out[pos++] = (size_small >> 8) & 0xFF; out[pos++] = size_small & 0xFF;
        out[pos++] = (size_large >> 24) & 0xFF; out[pos++] = (size_large >> 16) & 0xFF;
        out[pos++] = (size_large >> 8) & 0xFF; out[pos++] = size_large & 0xFF;
        memcpy(out + pos, comp_b0, size_b0); pos += size_b0;
        memcpy(out + pos, comp_b1, size_b1); pos += size_b1;
        memcpy(out + pos, comp_small, size_small); pos += size_small;
        memcpy(out + pos, comp_large, size_large); pos += size_large;
    } else {
        memcpy(out + pos, comp1, size1); pos += size1;
    }
    
    FILE *fout = fopen(out_path, "wb");
    fwrite(out, 1, pos, fout);
    fclose(fout);
    
    printf("\n=== РЕЗУЛЬТАТ ===\n");
    printf("Вход: %zu байт\n", in_size);
    printf("Выход: %zu байт\n", pos);
    printf("Степень сжатия: %.2fx\n", (double)in_size / pos);
    
    free(m256); free(m10);
    free(comp1); free(comp_b0); free(comp_b1); free(comp_small); free(comp_large);
    free(in_data); free(mtf_out); free(out);
    return 0;
}

static int decompress_file(const char *in_path, const char *out_path) {
    FILE *fin = fopen(in_path, "rb");
    if (!fin) { perror("fopen"); return 1; }
    fseek(fin, 0, SEEK_END);
    size_t comp_size = ftell(fin);
    fseek(fin, 0, SEEK_SET);
    uint8_t *compressed = malloc(comp_size);
    if (fread(compressed, 1, comp_size, fin) != comp_size) { fclose(fin); return 1; }
    fclose(fin);
    
    uint32_t magic = (compressed[0] << 24) | (compressed[1] << 16) |
                     (compressed[2] << 8) | compressed[3];
    if (magic != MAGIC) { fprintf(stderr, "Invalid magic\n"); free(compressed); return 1; }
    
    size_t orig = (compressed[4] << 24) | (compressed[5] << 16) |
                  (compressed[6] << 8) | compressed[7];
    size_t bwt_idx = (compressed[8] << 24) | (compressed[9] << 16) |
                     (compressed[10] << 8) | compressed[11];
    uint32_t stored_crc = (compressed[12] << 24) | (compressed[13] << 16) |
                          (compressed[14] << 8) | compressed[15];
    uint8_t method = compressed[16];
    
    uint8_t *mtf_out = malloc(orig);
    RC rc;
    
    if (method == 1) {
        Model256 *m256 = malloc(sizeof(Model256));
        m256_init(m256);
        rc_dec_init(&rc, compressed + 20);
        uint8_t ctx = 0;
        for (size_t i = 0; i < orig; i++) {
            mtf_out[i] = rc_dec_sym256(&rc, m256, ctx);
            ctx = mtf_out[i];
        }
        free(m256);
    } else {
        size_t pos = 20;
        size_t size_b0 = (compressed[pos] << 24) | (compressed[pos+1] << 16) |
                         (compressed[pos+2] << 8) | compressed[pos+3]; pos += 4;
        size_t size_b1 = (compressed[pos] << 24) | (compressed[pos+1] << 16) |
                         (compressed[pos+2] << 8) | compressed[pos+3]; pos += 4;
        size_t size_small = (compressed[pos] << 24) | (compressed[pos+1] << 16) |
                            (compressed[pos+2] << 8) | compressed[pos+3]; pos += 4;
        size_t size_large = (compressed[pos] << 24) | (compressed[pos+1] << 16) |
                            (compressed[pos+2] << 8) | compressed[pos+3]; pos += 4;
        
        /* Декодируем биты0 */
        uint8_t *bits0 = malloc(orig);
        BinModel1 bm0; bm1_init(&bm0);
        rc_dec_init(&rc, compressed + pos);
        uint8_t bctx = 0;
        for (size_t i = 0; i < orig; i++) {
            bits0[i] = rc_dec_bit(&rc, &bm0.ctx[bctx]);
            bctx = bits0[i];
        }
        pos += size_b0;
        
        /* Подсчитываем ненулевые */
        size_t nz = 0;
        for (size_t i = 0; i < orig; i++) if (bits0[i]) nz++;
        
        /* Декодируем биты1 */
        uint8_t *bits1 = malloc(nz + 1);
        BinModel1 bm1; bm1_init(&bm1);
        rc_dec_init(&rc, compressed + pos);
        bctx = 0;
        for (size_t i = 0; i < nz; i++) {
            bits1[i] = rc_dec_bit(&rc, &bm1.ctx[bctx]);
            bctx = bits1[i];
        }
        pos += size_b1;
        
        /* Подсчитываем малые и большие */
        size_t ns = 0, nl = 0;
        for (size_t i = 0; i < nz; i++) {
            if (bits1[i]) nl++; else ns++;
        }
        
        /* Декодируем малые */
        uint8_t *small_vals = malloc(ns + 1);
        Model10 *m10 = malloc(sizeof(Model10)); m10_init(m10);
        rc_dec_init(&rc, compressed + pos);
        uint8_t ctx = 0;
        for (size_t i = 0; i < ns; i++) {
            small_vals[i] = rc_dec_sym10(&rc, m10, ctx) + 1;  /* 0-8 -> 1-9 */
            ctx = small_vals[i] - 1;
        }
        pos += size_small;
        
        /* Декодируем большие */
        uint8_t *large_vals = malloc(nl + 1);
        Model256 *m256 = malloc(sizeof(Model256)); m256_init(m256);
        rc_dec_init(&rc, compressed + pos);
        ctx = 0;
        for (size_t i = 0; i < nl; i++) {
            large_vals[i] = rc_dec_sym256(&rc, m256, ctx) + 10;  /* 0-245 -> 10-255 */
            ctx = large_vals[i] - 10;
        }
        
        /* Собираем */
        size_t ni = 0, si = 0, li = 0;
        for (size_t i = 0; i < orig; i++) {
            if (bits0[i] == 0) {
                mtf_out[i] = 0;
            } else {
                if (bits1[ni] == 0) {
                    mtf_out[i] = small_vals[si++];
                } else {
                    mtf_out[i] = large_vals[li++];
                }
                ni++;
            }
        }
        
        free(m10); free(m256);
        free(bits0); free(bits1); free(small_vals); free(large_vals);
    }
    
    uint8_t *bwt_out = malloc(orig);
    mtf_decode(mtf_out, orig, bwt_out);
    free(mtf_out);
    
    uint8_t *out_data = malloc(orig);
    bwt_decode(bwt_out, orig, bwt_idx, out_data);
    free(bwt_out);
    
    uint32_t check = calc_crc32(out_data, orig);
    if (check != stored_crc) {
        fprintf(stderr, "CRC mismatch!\n");
        free(compressed); free(out_data); return 1;
    }
    
    FILE *fout = fopen(out_path, "wb");
    fwrite(out_data, 1, orig, fout);
    fclose(fout);
    
    printf("Распаковано: %zu байт, CRC OK\n", orig);
    free(compressed); free(out_data);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("KOLIBRI FRACTAL v27 - Двухуровневая фрактальная вложенность\n");
        printf("Usage: %s compress|decompress <in> <out>\n", argv[0]);
        return 1;
    }
    if (strcmp(argv[1], "compress") == 0) return compress_file(argv[2], argv[3]);
    if (strcmp(argv[1], "decompress") == 0) return decompress_file(argv[2], argv[3]);
    return 1;
}
