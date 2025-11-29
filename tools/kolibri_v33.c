/*
 * KOLIBRI FRACTAL v33 - 4-уровневый с оптимизированным кодированием
 * Добавляем уровень 4-7 vs 8-9 для более точного моделирования
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

/* Binary model Order-1 */
typedef struct { uint16_t prob[2]; } BinModel;
static void bm_init(BinModel *m) { m->prob[0] = m->prob[1] = 2048; }
static void bm_update(BinModel *m, int ctx, int bit) {
    if (bit) m->prob[ctx] += (4096 - m->prob[ctx]) >> 5;
    else m->prob[ctx] -= m->prob[ctx] >> 5;
}

/* Order-1 multi-symbol model */
typedef struct { uint16_t freq[256][257]; } Model;
static Model* model_new(int nsym) {
    Model *m = calloc(1, sizeof(Model));
    for (int c = 0; c < 256; c++) {
        for (int s = 0; s < nsym; s++) m->freq[c][s] = 1;
        m->freq[c][256] = nsym;
    }
    return m;
}
static void model_update(Model *m, int ctx, int sym) {
    m->freq[ctx][sym] += 16;
    m->freq[ctx][256] += 16;
    if (m->freq[ctx][256] > 0x3FFF) {
        uint16_t t = 0;
        for (int i = 0; i < 256; i++) {
            m->freq[ctx][i] = (m->freq[ctx][i] >> 1) | 1;
            t += m->freq[ctx][i];
        }
        m->freq[ctx][256] = t;
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
static void rc_enc_sym(RC *rc, Model *m, int nsym, int ctx, int sym) {
    if (ctx > 255) ctx = 255;
    uint32_t total = m->freq[ctx][256], cum = 0;
    for (int i = 0; i < sym; i++) cum += m->freq[ctx][i];
    rc->range /= total; rc->low += cum * rc->range; rc->range *= m->freq[ctx][sym];
    rc_enc_norm(rc);
    model_update(m, ctx, sym);
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
static int rc_dec_sym(RC *rc, Model *m, int nsym, int ctx) {
    if (ctx > 255) ctx = 255;
    uint32_t total = m->freq[ctx][256];
    rc->range /= total;
    uint32_t target = (rc->code - rc->low) / rc->range;
    uint32_t cum = 0; int sym = 0;
    while (sym < nsym - 1 && cum + m->freq[ctx][sym] <= target) { cum += m->freq[ctx][sym]; sym++; }
    rc->low += cum * rc->range; rc->range *= m->freq[ctx][sym];
    rc_dec_norm(rc);
    model_update(m, ctx, sym);
    return sym;
}

#define MAGIC 0x4B463333  /* "KF33" */

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
    
    uint8_t *mtf = malloc(in_size);
    mtf_encode(bwt_out, in_size, mtf);
    free(bwt_out);
    
    /* Анализ: 0 / 1-2 / 3-5 / 6-9 / 10+ */
    size_t cnt[5] = {0};
    for (size_t i = 0; i < in_size; i++) {
        uint8_t v = mtf[i];
        if (v == 0) cnt[0]++;
        else if (v <= 2) cnt[1]++;
        else if (v <= 5) cnt[2]++;
        else if (v <= 9) cnt[3]++;
        else cnt[4]++;
    }
    printf("=== 4-УРОВНЕВАЯ СТРУКТУРА ===\n");
    printf("L0 (=0):    %zu (%.1f%%)\n", cnt[0], 100.0*cnt[0]/in_size);
    printf("L1 (1-2):   %zu (%.1f%%)\n", cnt[1], 100.0*cnt[1]/in_size);
    printf("L2 (3-5):   %zu (%.1f%%)\n", cnt[2], 100.0*cnt[2]/in_size);
    printf("L3 (6-9):   %zu (%.1f%%)\n", cnt[3], 100.0*cnt[3]/in_size);
    printf("L4 (10+):   %zu (%.1f%%)\n", cnt[4], 100.0*cnt[4]/in_size);
    
    size_t buf = in_size + 1024;
    RC rc;
    
    /* Baseline */
    uint8_t *comp_base = malloc(buf);
    Model *m256 = model_new(256);
    rc_enc_init(&rc, comp_base, buf);
    uint8_t ctx = 0;
    for (size_t i = 0; i < in_size; i++) {
        rc_enc_sym(&rc, m256, 256, ctx, mtf[i]);
        ctx = mtf[i];
    }
    rc_enc_flush(&rc);
    size_t size_base = rc.out_size;
    printf("Baseline: %zu (%.2fx)\n", size_base, (double)in_size/size_base);
    free(m256);
    
    /* Fractal */
    /* B0: 0 vs non-0 */
    uint8_t *bits0 = malloc(buf);
    BinModel bm0; bm_init(&bm0);
    rc_enc_init(&rc, bits0, buf);
    int bctx = 0;
    for (size_t i = 0; i < in_size; i++) {
        int b = (mtf[i] != 0);
        rc_enc_bit(&rc, &bm0, bctx, b);
        bctx = b;
    }
    rc_enc_flush(&rc);
    size_t sb0 = rc.out_size;
    
    /* B1: 1-2 vs 3+ */
    uint8_t *bits1 = malloc(buf);
    BinModel bm1; bm_init(&bm1);
    rc_enc_init(&rc, bits1, buf);
    bctx = 0;
    for (size_t i = 0; i < in_size; i++) {
        if (mtf[i] > 0) {
            int b = (mtf[i] > 2);
            rc_enc_bit(&rc, &bm1, bctx, b);
            bctx = b;
        }
    }
    rc_enc_flush(&rc);
    size_t sb1 = rc.out_size;
    
    /* B2: 3-5 vs 6+ */
    uint8_t *bits2 = malloc(buf);
    BinModel bm2; bm_init(&bm2);
    rc_enc_init(&rc, bits2, buf);
    bctx = 0;
    for (size_t i = 0; i < in_size; i++) {
        if (mtf[i] > 2) {
            int b = (mtf[i] > 5);
            rc_enc_bit(&rc, &bm2, bctx, b);
            bctx = b;
        }
    }
    rc_enc_flush(&rc);
    size_t sb2 = rc.out_size;
    
    /* B3: 6-9 vs 10+ */
    uint8_t *bits3 = malloc(buf);
    BinModel bm3; bm_init(&bm3);
    rc_enc_init(&rc, bits3, buf);
    bctx = 0;
    for (size_t i = 0; i < in_size; i++) {
        if (mtf[i] > 5) {
            int b = (mtf[i] > 9);
            rc_enc_bit(&rc, &bm3, bctx, b);
            bctx = b;
        }
    }
    rc_enc_flush(&rc);
    size_t sb3 = rc.out_size;
    
    /* V1: 1-2 (2 symbols) */
    uint8_t *v1 = malloc(buf);
    Model *m2 = model_new(2);
    rc_enc_init(&rc, v1, buf);
    ctx = 0;
    for (size_t i = 0; i < in_size; i++) {
        if (mtf[i] >= 1 && mtf[i] <= 2) {
            rc_enc_sym(&rc, m2, 2, ctx, mtf[i] - 1);
            ctx = mtf[i] - 1;
        }
    }
    rc_enc_flush(&rc);
    size_t sv1 = rc.out_size;
    free(m2);
    
    /* V2: 3-5 (3 symbols) */
    uint8_t *v2 = malloc(buf);
    Model *m3 = model_new(3);
    rc_enc_init(&rc, v2, buf);
    ctx = 0;
    for (size_t i = 0; i < in_size; i++) {
        if (mtf[i] >= 3 && mtf[i] <= 5) {
            rc_enc_sym(&rc, m3, 3, ctx, mtf[i] - 3);
            ctx = mtf[i] - 3;
        }
    }
    rc_enc_flush(&rc);
    size_t sv2 = rc.out_size;
    free(m3);
    
    /* V3: 6-9 (4 symbols) */
    uint8_t *v3 = malloc(buf);
    Model *m4 = model_new(4);
    rc_enc_init(&rc, v3, buf);
    ctx = 0;
    for (size_t i = 0; i < in_size; i++) {
        if (mtf[i] >= 6 && mtf[i] <= 9) {
            rc_enc_sym(&rc, m4, 4, ctx, mtf[i] - 6);
            ctx = mtf[i] - 6;
        }
    }
    rc_enc_flush(&rc);
    size_t sv3 = rc.out_size;
    free(m4);
    
    /* V4: 10+ (246 symbols) */
    uint8_t *v4 = malloc(buf);
    Model *m246 = model_new(246);
    rc_enc_init(&rc, v4, buf);
    ctx = 0;
    for (size_t i = 0; i < in_size; i++) {
        if (mtf[i] >= 10) {
            rc_enc_sym(&rc, m246, 246, ctx, mtf[i] - 10);
            ctx = mtf[i] - 10;
        }
    }
    rc_enc_flush(&rc);
    size_t sv4 = rc.out_size;
    free(m246);
    
    size_t sf = sb0 + sb1 + sb2 + sb3 + sv1 + sv2 + sv3 + sv4 + 32;
    printf("Fractal:\n");
    printf("  B0: %zu, B1: %zu, B2: %zu, B3: %zu\n", sb0, sb1, sb2, sb3);
    printf("  V1: %zu, V2: %zu, V3: %zu, V4: %zu\n", sv1, sv2, sv3, sv4);
    printf("  ИТОГО: %zu (%.2fx)\n", sf, (double)in_size/sf);
    
    int method = (sf < size_base) ? 2 : 1;
    size_t best = (method == 2) ? sf : size_base;
    printf("Выбран: %d\n", method);
    
    /* Write */
    uint8_t *out = malloc(64 + best);
    size_t pos = 0;
    
    out[pos++] = (MAGIC >> 24); out[pos++] = (MAGIC >> 16);
    out[pos++] = (MAGIC >> 8); out[pos++] = MAGIC;
    out[pos++] = (in_size >> 24); out[pos++] = (in_size >> 16);
    out[pos++] = (in_size >> 8); out[pos++] = in_size;
    out[pos++] = (bwt_idx >> 24); out[pos++] = (bwt_idx >> 16);
    out[pos++] = (bwt_idx >> 8); out[pos++] = bwt_idx;
    out[pos++] = (crc >> 24); out[pos++] = (crc >> 16);
    out[pos++] = (crc >> 8); out[pos++] = crc;
    out[pos++] = method;
    out[pos++] = 0; out[pos++] = 0; out[pos++] = 0;
    
    if (method == 2) {
        /* 8 sizes */
        #define W32(x) out[pos++]=(x>>24);out[pos++]=(x>>16);out[pos++]=(x>>8);out[pos++]=x
        W32(sb0); W32(sb1); W32(sb2); W32(sb3);
        W32(sv1); W32(sv2); W32(sv3); W32(sv4);
        #undef W32
        memcpy(out+pos,bits0,sb0);pos+=sb0;
        memcpy(out+pos,bits1,sb1);pos+=sb1;
        memcpy(out+pos,bits2,sb2);pos+=sb2;
        memcpy(out+pos,bits3,sb3);pos+=sb3;
        memcpy(out+pos,v1,sv1);pos+=sv1;
        memcpy(out+pos,v2,sv2);pos+=sv2;
        memcpy(out+pos,v3,sv3);pos+=sv3;
        memcpy(out+pos,v4,sv4);pos+=sv4;
    } else {
        memcpy(out+pos,comp_base,size_base);pos+=size_base;
    }
    
    FILE *fout = fopen(out_path, "wb");
    fwrite(out, 1, pos, fout);
    fclose(fout);
    
    printf("\n=== РЕЗУЛЬТАТ ===\n");
    printf("Вход: %zu\n", in_size);
    printf("Выход: %zu\n", pos);
    printf("Сжатие: %.2fx\n", (double)in_size/pos);
    
    free(comp_base);free(bits0);free(bits1);free(bits2);free(bits3);
    free(v1);free(v2);free(v3);free(v4);
    free(in_data);free(mtf);free(out);
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
    
    size_t pos = 0;
    uint32_t magic = (in_data[pos]<<24)|(in_data[pos+1]<<16)|(in_data[pos+2]<<8)|in_data[pos+3]; pos+=4;
    if (magic != MAGIC) { fprintf(stderr, "Bad magic\n"); return 1; }
    
    uint32_t orig = (in_data[pos]<<24)|(in_data[pos+1]<<16)|(in_data[pos+2]<<8)|in_data[pos+3]; pos+=4;
    uint32_t bwt_idx = (in_data[pos]<<24)|(in_data[pos+1]<<16)|(in_data[pos+2]<<8)|in_data[pos+3]; pos+=4;
    uint32_t stored_crc = (in_data[pos]<<24)|(in_data[pos+1]<<16)|(in_data[pos+2]<<8)|in_data[pos+3]; pos+=4;
    uint8_t method = in_data[pos++]; pos += 3;
    
    uint8_t *mtf = malloc(orig);
    RC rc;
    
    if (method == 2) {
        #define R32(x) x=(in_data[pos]<<24)|(in_data[pos+1]<<16)|(in_data[pos+2]<<8)|in_data[pos+3];pos+=4
        size_t sb0,sb1,sb2,sb3,sv1,sv2,sv3,sv4;
        R32(sb0);R32(sb1);R32(sb2);R32(sb3);
        R32(sv1);R32(sv2);R32(sv3);R32(sv4);
        #undef R32
        
        /* Decode B0 */
        uint8_t *db0 = malloc(orig);
        BinModel bm0; bm_init(&bm0);
        rc_dec_init(&rc, in_data+pos);
        int bctx = 0;
        for (size_t i = 0; i < orig; i++) {
            db0[i] = rc_dec_bit(&rc, &bm0, bctx);
            bctx = db0[i];
        }
        pos += sb0;
        
        size_t nz = 0;
        for (size_t i = 0; i < orig; i++) if (db0[i]) nz++;
        
        /* Decode B1 */
        uint8_t *db1 = malloc(nz+1);
        BinModel bm1; bm_init(&bm1);
        rc_dec_init(&rc, in_data+pos);
        bctx = 0;
        for (size_t i = 0; i < nz; i++) {
            db1[i] = rc_dec_bit(&rc, &bm1, bctx);
            bctx = db1[i];
        }
        pos += sb1;
        
        size_t n3p = 0;
        for (size_t i = 0; i < nz; i++) if (db1[i]) n3p++;
        
        /* Decode B2 */
        uint8_t *db2 = malloc(n3p+1);
        BinModel bm2; bm_init(&bm2);
        rc_dec_init(&rc, in_data+pos);
        bctx = 0;
        for (size_t i = 0; i < n3p; i++) {
            db2[i] = rc_dec_bit(&rc, &bm2, bctx);
            bctx = db2[i];
        }
        pos += sb2;
        
        size_t n6p = 0;
        for (size_t i = 0; i < n3p; i++) if (db2[i]) n6p++;
        
        /* Decode B3 */
        uint8_t *db3 = malloc(n6p+1);
        BinModel bm3; bm_init(&bm3);
        rc_dec_init(&rc, in_data+pos);
        bctx = 0;
        for (size_t i = 0; i < n6p; i++) {
            db3[i] = rc_dec_bit(&rc, &bm3, bctx);
            bctx = db3[i];
        }
        pos += sb3;
        
        /* Counts */
        size_t c12 = nz - n3p;
        size_t c35 = n3p - n6p;
        size_t c69 = 0, c10 = 0;
        for (size_t i = 0; i < n6p; i++) {
            if (!db3[i]) c69++; else c10++;
        }
        
        /* Decode V1 */
        uint8_t *dv1 = malloc(c12+1);
        Model *m2 = model_new(2);
        rc_dec_init(&rc, in_data+pos);
        uint8_t ctx = 0;
        for (size_t i = 0; i < c12; i++) {
            dv1[i] = rc_dec_sym(&rc, m2, 2, ctx);
            ctx = dv1[i];
        }
        pos += sv1;
        free(m2);
        
        /* Decode V2 */
        uint8_t *dv2 = malloc(c35+1);
        Model *m3 = model_new(3);
        rc_dec_init(&rc, in_data+pos);
        ctx = 0;
        for (size_t i = 0; i < c35; i++) {
            dv2[i] = rc_dec_sym(&rc, m3, 3, ctx);
            ctx = dv2[i];
        }
        pos += sv2;
        free(m3);
        
        /* Decode V3 */
        uint8_t *dv3 = malloc(c69+1);
        Model *m4 = model_new(4);
        rc_dec_init(&rc, in_data+pos);
        ctx = 0;
        for (size_t i = 0; i < c69; i++) {
            dv3[i] = rc_dec_sym(&rc, m4, 4, ctx);
            ctx = dv3[i];
        }
        pos += sv3;
        free(m4);
        
        /* Decode V4 */
        uint8_t *dv4 = malloc(c10+1);
        Model *m246 = model_new(246);
        rc_dec_init(&rc, in_data+pos);
        ctx = 0;
        for (size_t i = 0; i < c10; i++) {
            dv4[i] = rc_dec_sym(&rc, m246, 246, ctx);
            ctx = dv4[i];
        }
        free(m246);
        
        /* Reconstruct */
        size_t i1=0,i2=0,i3=0,i4=0,inz=0,i3p=0,i6p=0;
        for (size_t i = 0; i < orig; i++) {
            if (!db0[i]) {
                mtf[i] = 0;
            } else {
                if (!db1[inz]) {
                    mtf[i] = dv1[i1++] + 1;  /* 1-2 */
                } else {
                    if (!db2[i3p]) {
                        mtf[i] = dv2[i2++] + 3;  /* 3-5 */
                    } else {
                        if (!db3[i6p]) {
                            mtf[i] = dv3[i3++] + 6;  /* 6-9 */
                        } else {
                            mtf[i] = dv4[i4++] + 10;  /* 10+ */
                        }
                        i6p++;
                    }
                    i3p++;
                }
                inz++;
            }
        }
        
        free(db0);free(db1);free(db2);free(db3);
        free(dv1);free(dv2);free(dv3);free(dv4);
    } else {
        Model *m256 = model_new(256);
        rc_dec_init(&rc, in_data+pos);
        uint8_t ctx = 0;
        for (size_t i = 0; i < orig; i++) {
            mtf[i] = rc_dec_sym(&rc, m256, 256, ctx);
            ctx = mtf[i];
        }
        free(m256);
    }
    
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
    
    free(in_data);free(out);
    return stored_crc==calc_crc ? 0 : 1;
}

int main(int argc, char **argv) {
    init_crc32();
    if (argc < 4) {
        printf("Kolibri v33\nUsage: %s compress|decompress in out\n", argv[0]);
        return 1;
    }
    return strcmp(argv[1], "compress") == 0 ? compress_file(argv[2], argv[3]) : decompress_file(argv[2], argv[3]);
}
