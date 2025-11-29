/*
 * KOLIBRI FRACTAL v36 - Order-3 bits + Order-2 values
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
           (rc->range < RC_BOT && ((rc->range = -rc->low & (RC_BOT - 1)), 1))) {
        if (rc->out_size < rc->out_cap) rc->out_ptr[rc->out_size++] = rc->low >> 24;
        rc->low <<= 8; rc->range <<= 8;
    }
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
        rc->code = (rc->code << 8) | *rc->in_ptr++; 
        rc->low <<= 8; rc->range <<= 8;
    }
}

/* Order-3 binary model (8 контекстов) */
typedef struct { uint16_t prob[8]; } BinModel3;
static void bm3_init(BinModel3 *m) { 
    for (int i = 0; i < 8; i++) m->prob[i] = 2048; 
}
static void bm3_update(BinModel3 *m, int ctx, int bit) {
    if (bit) m->prob[ctx] += (4096 - m->prob[ctx]) >> 5;
    else m->prob[ctx] -= m->prob[ctx] >> 5;
}
static void rc_enc_bit3(RC *rc, BinModel3 *m, int ctx, int bit) {
    uint32_t bound = (rc->range >> 12) * m->prob[ctx];
    if (bit) rc->range = bound;
    else { rc->low += bound; rc->range -= bound; }
    rc_enc_norm(rc);
    bm3_update(m, ctx, bit);
}
static int rc_dec_bit3(RC *rc, BinModel3 *m, int ctx) {
    uint32_t bound = (rc->range >> 12) * m->prob[ctx];
    int bit = (rc->code < rc->low + bound) ? 1 : 0;
    if (bit) rc->range = bound;
    else { rc->low += bound; rc->range -= bound; }
    rc_dec_norm(rc);
    bm3_update(m, ctx, bit);
    return bit;
}

/* N-symbol model Order-2 */
typedef struct {
    uint16_t freq[64][17]; /* ctx = prev2*3 + prev1, max 64 */
    int nsym;
} ModelN2;
static void mn2_init(ModelN2 *m, int nsym) {
    m->nsym = nsym;
    for (int c = 0; c < 64; c++) {
        for (int s = 0; s < nsym; s++) m->freq[c][s] = 1;
        m->freq[c][16] = nsym;
    }
}
static void mn2_update(ModelN2 *m, int ctx, int sym) {
    m->freq[ctx][sym] += 16;
    m->freq[ctx][16] += 16;
    if (m->freq[ctx][16] > 0x3FFF) {
        m->freq[ctx][16] = 0;
        for (int i = 0; i < m->nsym; i++) {
            m->freq[ctx][i] = (m->freq[ctx][i] >> 1) | 1;
            m->freq[ctx][16] += m->freq[ctx][i];
        }
    }
}
static void rc_enc_symN2(RC *rc, ModelN2 *m, int ctx, int sym) {
    if (ctx >= 64) ctx = 63;
    uint32_t cum = 0;
    for (int i = 0; i < sym; i++) cum += m->freq[ctx][i];
    rc->range /= m->freq[ctx][16];
    rc->low += cum * rc->range;
    rc->range *= m->freq[ctx][sym];
    rc_enc_norm(rc);
    mn2_update(m, ctx, sym);
}
static int rc_dec_symN2(RC *rc, ModelN2 *m, int ctx) {
    if (ctx >= 64) ctx = 63;
    rc->range /= m->freq[ctx][16];
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
    mn2_update(m, ctx, sym);
    return sym;
}

/* Order-2 256-symbol model (смешанный контекст) */
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

#define MAGIC 0x4B463336

static int compress_file(const char *in_path, const char *out_path) {
    FILE *fin = fopen(in_path, "rb");
    if (!fin) return 1;
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
    
    size_t buf = in_size + 1024;
    RC rc;
    
    /* B0: Order-3 */
    uint8_t *bits0 = malloc(buf);
    BinModel3 bm0; bm3_init(&bm0);
    rc_enc_init(&rc, bits0, buf);
    int ctx = 0;
    for (size_t i = 0; i < in_size; i++) {
        int b = (mtf[i] != 0);
        rc_enc_bit3(&rc, &bm0, ctx, b);
        ctx = ((ctx << 1) | b) & 7;
    }
    rc_enc_flush(&rc);
    size_t sb0 = rc.out_size;
    
    /* B1: Order-3 */
    uint8_t *bits1 = malloc(buf);
    BinModel3 bm1; bm3_init(&bm1);
    rc_enc_init(&rc, bits1, buf);
    ctx = 0;
    for (size_t i = 0; i < in_size; i++) {
        if (mtf[i] > 0) {
            int b = (mtf[i] > 3);
            rc_enc_bit3(&rc, &bm1, ctx, b);
            ctx = ((ctx << 1) | b) & 7;
        }
    }
    rc_enc_flush(&rc);
    size_t sb1 = rc.out_size;
    
    /* B2: Order-3 */
    uint8_t *bits2 = malloc(buf);
    BinModel3 bm2; bm3_init(&bm2);
    rc_enc_init(&rc, bits2, buf);
    ctx = 0;
    for (size_t i = 0; i < in_size; i++) {
        if (mtf[i] > 3) {
            int b = (mtf[i] > 9);
            rc_enc_bit3(&rc, &bm2, ctx, b);
            ctx = ((ctx << 1) | b) & 7;
        }
    }
    rc_enc_flush(&rc);
    size_t sb2 = rc.out_size;
    
    /* V1: тритовый Order-2 */
    uint8_t *v1 = malloc(buf);
    ModelN2 m3; mn2_init(&m3, 3);
    rc_enc_init(&rc, v1, buf);
    int p1 = 0, p2 = 0;
    size_t cnt1 = 0;
    for (size_t i = 0; i < in_size; i++) {
        if (mtf[i] >= 1 && mtf[i] <= 3) {
            int sym = mtf[i] - 1;
            int c = p2 * 3 + p1;
            rc_enc_symN2(&rc, &m3, c, sym);
            p2 = p1; p1 = sym;
            cnt1++;
        }
    }
    rc_enc_flush(&rc);
    size_t sv1 = rc.out_size;
    
    /* V2: 6 символов Order-2 */
    uint8_t *v2 = malloc(buf);
    ModelN2 m6; mn2_init(&m6, 6);
    rc_enc_init(&rc, v2, buf);
    p1 = 0; p2 = 0;
    size_t cnt2 = 0;
    for (size_t i = 0; i < in_size; i++) {
        if (mtf[i] >= 4 && mtf[i] <= 9) {
            int sym = mtf[i] - 4;
            int c = p2 * 6 + p1;
            if (c >= 36) c = 35;
            rc_enc_symN2(&rc, &m6, c, sym);
            p2 = p1; p1 = sym;
            cnt2++;
        }
    }
    rc_enc_flush(&rc);
    size_t sv2 = rc.out_size;
    
    /* V3: 10+ Order-1 */
    uint8_t *v3 = malloc(buf);
    Model256 *m256 = malloc(sizeof(Model256));
    m256_init(m256);
    rc_enc_init(&rc, v3, buf);
    uint8_t ctx10 = 0;
    size_t cnt3 = 0;
    for (size_t i = 0; i < in_size; i++) {
        if (mtf[i] >= 10) {
            uint8_t sym = mtf[i] - 10;
            rc_enc_sym256(&rc, m256, ctx10, sym);
            ctx10 = sym;
            cnt3++;
        }
    }
    rc_enc_flush(&rc);
    size_t sv3 = rc.out_size;
    
    size_t sf = sb0 + sb1 + sb2 + sv1 + sv2 + sv3 + 24;
    
    printf("=== v36 ===\n");
    printf("B0: %zu, B1: %zu, B2: %zu\n", sb0, sb1, sb2);
    printf("V1: %zu (%.2f bpv)\n", sv1, cnt1 ? 8.0*sv1/cnt1 : 0);
    printf("V2: %zu (%.2f bpv)\n", sv2, cnt2 ? 8.0*sv2/cnt2 : 0);
    printf("V3: %zu (%.2f bpv)\n", sv3, cnt3 ? 8.0*sv3/cnt3 : 0);
    printf("ИТОГО: %zu (%.2fx)\n", sf, (double)in_size/sf);
    
    /* Запись */
    uint8_t *out = malloc(64 + sf);
    size_t pos = 0;
    
    #define W32(x) out[pos++]=(x>>24);out[pos++]=(x>>16);out[pos++]=(x>>8);out[pos++]=x
    W32(MAGIC);
    W32(in_size);
    W32(bwt_idx);
    W32(crc);
    W32(sb0); W32(sb1); W32(sb2);
    W32(sv1); W32(sv2);
    memcpy(out+pos, bits0, sb0); pos += sb0;
    memcpy(out+pos, bits1, sb1); pos += sb1;
    memcpy(out+pos, bits2, sb2); pos += sb2;
    memcpy(out+pos, v1, sv1); pos += sv1;
    memcpy(out+pos, v2, sv2); pos += sv2;
    memcpy(out+pos, v3, sv3); pos += sv3;
    #undef W32
    
    FILE *fout = fopen(out_path, "wb");
    fwrite(out, 1, pos, fout);
    fclose(fout);
    
    printf("Вход: %zu, Выход: %zu (%.2fx)\n", in_size, pos, (double)in_size/pos);
    
    free(m256);
    free(bits0); free(bits1); free(bits2);
    free(v1); free(v2); free(v3);
    free(in_data); free(mtf); free(out);
    return 0;
}

static int decompress_file(const char *in_path, const char *out_path) {
    FILE *fin = fopen(in_path, "rb");
    if (!fin) return 1;
    fseek(fin, 0, SEEK_END);
    size_t in_size = ftell(fin);
    fseek(fin, 0, SEEK_SET);
    uint8_t *in_data = malloc(in_size);
    fread(in_data, 1, in_size, fin);
    fclose(fin);
    
    size_t pos = 0;
    #define R32(x) x=(in_data[pos]<<24)|(in_data[pos+1]<<16)|(in_data[pos+2]<<8)|in_data[pos+3];pos+=4
    
    uint32_t magic; R32(magic);
    if (magic != MAGIC) { fprintf(stderr, "Bad magic\n"); return 1; }
    
    uint32_t orig; R32(orig);
    uint32_t bwt_idx; R32(bwt_idx);
    uint32_t stored_crc; R32(stored_crc);
    uint32_t sb0, sb1, sb2, sv1, sv2;
    R32(sb0); R32(sb1); R32(sb2); R32(sv1); R32(sv2);
    
    RC rc;
    
    /* Decode bits0 */
    uint8_t *db0 = malloc(orig);
    BinModel3 bm0; bm3_init(&bm0);
    rc_dec_init(&rc, in_data + pos);
    int ctx = 0;
    for (size_t i = 0; i < orig; i++) {
        db0[i] = rc_dec_bit3(&rc, &bm0, ctx);
        ctx = ((ctx << 1) | db0[i]) & 7;
    }
    pos += sb0;
    
    size_t nz = 0;
    for (size_t i = 0; i < orig; i++) if (db0[i]) nz++;
    
    /* Decode bits1 */
    uint8_t *db1 = malloc(nz + 1);
    BinModel3 bm1; bm3_init(&bm1);
    rc_dec_init(&rc, in_data + pos);
    ctx = 0;
    for (size_t i = 0; i < nz; i++) {
        db1[i] = rc_dec_bit3(&rc, &bm1, ctx);
        ctx = ((ctx << 1) | db1[i]) & 7;
    }
    pos += sb1;
    
    size_t n4p = 0;
    for (size_t i = 0; i < nz; i++) if (db1[i]) n4p++;
    
    /* Decode bits2 */
    uint8_t *db2 = malloc(n4p + 1);
    BinModel3 bm2; bm3_init(&bm2);
    rc_dec_init(&rc, in_data + pos);
    ctx = 0;
    for (size_t i = 0; i < n4p; i++) {
        db2[i] = rc_dec_bit3(&rc, &bm2, ctx);
        ctx = ((ctx << 1) | db2[i]) & 7;
    }
    pos += sb2;
    
    size_t c13 = nz - n4p;
    size_t c49 = 0, c10 = 0;
    for (size_t i = 0; i < n4p; i++) {
        if (!db2[i]) c49++; else c10++;
    }
    
    /* Decode V1 */
    uint8_t *dv1 = malloc(c13 + 1);
    ModelN2 m3; mn2_init(&m3, 3);
    rc_dec_init(&rc, in_data + pos);
    int p1 = 0, p2 = 0;
    for (size_t i = 0; i < c13; i++) {
        int c = p2 * 3 + p1;
        dv1[i] = rc_dec_symN2(&rc, &m3, c);
        p2 = p1; p1 = dv1[i];
    }
    pos += sv1;
    
    /* Decode V2 */
    uint8_t *dv2 = malloc(c49 + 1);
    ModelN2 m6; mn2_init(&m6, 6);
    rc_dec_init(&rc, in_data + pos);
    p1 = 0; p2 = 0;
    for (size_t i = 0; i < c49; i++) {
        int c = p2 * 6 + p1;
        if (c >= 36) c = 35;
        dv2[i] = rc_dec_symN2(&rc, &m6, c);
        p2 = p1; p1 = dv2[i];
    }
    pos += sv2;
    
    /* Decode V3 */
    uint8_t *dv3 = malloc(c10 + 1);
    Model256 *m256 = malloc(sizeof(Model256));
    m256_init(m256);
    rc_dec_init(&rc, in_data + pos);
    uint8_t ctx10 = 0;
    for (size_t i = 0; i < c10; i++) {
        dv3[i] = rc_dec_sym256(&rc, m256, ctx10);
        ctx10 = dv3[i];
    }
    free(m256);
    
    /* Reconstruct */
    uint8_t *mtf = malloc(orig);
    size_t i1 = 0, i2 = 0, i3 = 0, inz = 0, i4p = 0;
    for (size_t i = 0; i < orig; i++) {
        if (!db0[i]) {
            mtf[i] = 0;
        } else {
            if (!db1[inz]) {
                mtf[i] = dv1[i1++] + 1;
            } else {
                if (!db2[i4p]) {
                    mtf[i] = dv2[i2++] + 4;
                } else {
                    mtf[i] = dv3[i3++] + 10;
                }
                i4p++;
            }
            inz++;
        }
    }
    
    free(db0); free(db1); free(db2);
    free(dv1); free(dv2); free(dv3);
    #undef R32
    
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
    
    free(in_data); free(out);
    return stored_crc == calc_crc ? 0 : 1;
}

int main(int argc, char **argv) {
    init_crc32();
    if (argc < 4) {
        printf("Usage: %s compress|decompress in out\n", argv[0]);
        return 1;
    }
    return strcmp(argv[1], "compress") == 0 ? 
           compress_file(argv[2], argv[3]) : 
           decompress_file(argv[2], argv[3]);
}
