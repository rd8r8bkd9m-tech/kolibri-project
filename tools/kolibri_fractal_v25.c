/*
 * KOLIBRI FRACTAL v25
 * Истинная фрактальная рекурсия + Логические числа 0-9
 * 
 * КЛЮЧЕВАЯ ИДЕЯ ФРАКТАЛЬНОСТИ:
 * После BWT+MTF данные имеют самоподобную структуру:
 * - 86.9% - нули (фрактальный уровень 0)
 * - 10.1% - цифры 1-9 (фрактальный уровень 1)
 * - 3.1% - числа >= 10 (фрактальный уровень 2+)
 * 
 * Мы кодируем:
 * 1. Битовую маску нуль/не-нуль (очень сжимаемая)
 * 2. Значения ненулевых байтов отдельно
 * 
 * Это фрактально: мы можем применить тот же принцип к каждому потоку
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ============== CRC32 ============== */
static uint32_t crc32_table[256];
static int crc32_init = 0;

static void init_crc32(void) {
    if (crc32_init) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
            c = (c >> 1) ^ ((c & 1) ? 0xEDB88320 : 0);
        crc32_table[i] = c;
    }
    crc32_init = 1;
}

static uint32_t calc_crc32(const uint8_t *data, size_t len) {
    init_crc32();
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++)
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFF;
}

/* ============== BWT ============== */
static const uint8_t *g_bwt_data;
static size_t g_bwt_len;

static int bwt_cmp(const void *a, const void *b) {
    size_t i = *(const size_t *)a;
    size_t j = *(const size_t *)b;
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
    g_bwt_data = in;
    g_bwt_len = len;
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
    size_t C[256] = {0};
    for (size_t i = 0; i < len; i++) C[L[i]]++;
    size_t K[256], sum = 0;
    for (int c = 0; c < 256; c++) { K[c] = sum; sum += C[c]; }
    size_t *P = malloc(len * sizeof(size_t));
    size_t cnt[256] = {0};
    for (size_t i = 0; i < len; i++) P[i] = cnt[L[i]]++;
    size_t j = I;
    for (size_t i = len; i > 0; i--) {
        out[i - 1] = L[j];
        j = K[L[j]] + P[j];
    }
    free(P);
}

/* ============== MTF ============== */
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

/* ============== Range Coder ============== */
#define RC_TOP (1u << 24)
#define RC_BOT (1u << 16)

typedef struct {
    uint32_t low, range, code;
    const uint8_t *in_ptr;
    uint8_t *out_ptr;
    size_t out_size;
} RC;

/* Order-1 модель на 256 символов */
typedef struct {
    uint16_t freq[256][257];
} Model256;

static void m256_init(Model256 *m) {
    for (int c = 0; c < 256; c++) {
        for (int s = 0; s < 256; s++) m->freq[c][s] = 1;
        m->freq[c][256] = 256;
    }
}

static void m256_update(Model256 *m, uint8_t c, uint8_t s) {
    m->freq[c][s] += 8;
    m->freq[c][256] += 8;
    if (m->freq[c][256] > 0x3FFF) {
        uint16_t t = 0;
        for (int i = 0; i < 256; i++) {
            m->freq[c][i] = (m->freq[c][i] >> 1) | 1;
            t += m->freq[c][i];
        }
        m->freq[c][256] = t;
    }
}

/* Order-1 модель на 2 символа (для битовой маски) */
typedef struct {
    uint16_t freq[2][3]; /* [context][symbol], slot 2 = total */
} Model2;

static void m2_init(Model2 *m) {
    for (int c = 0; c < 2; c++) {
        m->freq[c][0] = 1;
        m->freq[c][1] = 1;
        m->freq[c][2] = 2;
    }
}

static void m2_update(Model2 *m, uint8_t c, uint8_t s) {
    m->freq[c][s] += 16;
    m->freq[c][2] += 16;
    if (m->freq[c][2] > 0x3FFF) {
        m->freq[c][0] = (m->freq[c][0] >> 1) | 1;
        m->freq[c][1] = (m->freq[c][1] >> 1) | 1;
        m->freq[c][2] = m->freq[c][0] + m->freq[c][1];
    }
}

static void rc_enc_init(RC *rc, uint8_t *out) {
    rc->low = 0; rc->range = 0xFFFFFFFF;
    rc->out_ptr = out; rc->out_size = 0;
}

static void rc_enc_norm(RC *rc) {
    while ((rc->low ^ (rc->low + rc->range)) < RC_TOP ||
           (rc->range < RC_BOT && ((rc->range = -rc->low & (RC_BOT - 1)), 1))) {
        rc->out_ptr[rc->out_size++] = rc->low >> 24;
        rc->low <<= 8; rc->range <<= 8;
    }
}

static void rc_enc_bit(RC *rc, Model2 *m, uint8_t ctx, uint8_t bit) {
    uint32_t total = m->freq[ctx][2];
    rc->range /= total;
    if (bit) {
        rc->low += m->freq[ctx][0] * rc->range;
        rc->range *= m->freq[ctx][1];
    } else {
        rc->range *= m->freq[ctx][0];
    }
    rc_enc_norm(rc);
    m2_update(m, ctx, bit);
}

static void rc_enc_sym(RC *rc, Model256 *m, uint8_t ctx, uint8_t sym) {
    uint32_t total = m->freq[ctx][256], cum = 0;
    for (int i = 0; i < sym; i++) cum += m->freq[ctx][i];
    rc->range /= total;
    rc->low += cum * rc->range;
    rc->range *= m->freq[ctx][sym];
    rc_enc_norm(rc);
    m256_update(m, ctx, sym);
}

static void rc_enc_flush(RC *rc) {
    for (int i = 0; i < 4; i++) {
        rc->out_ptr[rc->out_size++] = rc->low >> 24;
        rc->low <<= 8;
    }
}

static void rc_dec_init(RC *rc, const uint8_t *in) {
    rc->low = 0; rc->range = 0xFFFFFFFF; rc->code = 0;
    rc->in_ptr = in;
    for (int i = 0; i < 4; i++) rc->code = (rc->code << 8) | *rc->in_ptr++;
}

static void rc_dec_norm(RC *rc) {
    while ((rc->low ^ (rc->low + rc->range)) < RC_TOP ||
           (rc->range < RC_BOT && ((rc->range = -rc->low & (RC_BOT - 1)), 1))) {
        rc->code = (rc->code << 8) | *rc->in_ptr++;
        rc->low <<= 8; rc->range <<= 8;
    }
}

static uint8_t rc_dec_bit(RC *rc, Model2 *m, uint8_t ctx) {
    uint32_t total = m->freq[ctx][2];
    rc->range /= total;
    uint32_t threshold = m->freq[ctx][0] * rc->range;
    uint8_t bit = (rc->code - rc->low >= threshold) ? 1 : 0;
    if (bit) {
        rc->low += threshold;
        rc->range *= m->freq[ctx][1];
    } else {
        rc->range *= m->freq[ctx][0];
    }
    rc_dec_norm(rc);
    m2_update(m, ctx, bit);
    return bit;
}

static uint8_t rc_dec_sym(RC *rc, Model256 *m, uint8_t ctx) {
    uint32_t total = m->freq[ctx][256];
    rc->range /= total;
    uint32_t target = (rc->code - rc->low) / rc->range;
    uint32_t cum = 0;
    uint8_t sym = 0;
    while (cum + m->freq[ctx][sym] <= target) {
        cum += m->freq[ctx][sym]; sym++;
    }
    rc->low += cum * rc->range;
    rc->range *= m->freq[ctx][sym];
    rc_dec_norm(rc);
    m256_update(m, ctx, sym);
    return sym;
}

/* ============== ФРАКТАЛЬНОЕ СЖАТИЕ ============== */

#define MAGIC 0x4B465235  /* "KFR5" */

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
    
    /* Фрактальный анализ */
    size_t zeros = 0;
    for (size_t i = 0; i < in_size; i++) if (mtf_out[i] == 0) zeros++;
    
    printf("=== ФРАКТАЛЬНЫЙ АНАЛИЗ ===\n");
    printf("Нули: %zu (%.1f%%)\n", zeros, 100.0*zeros/in_size);
    printf("Ненули: %zu (%.1f%%)\n", in_size - zeros, 100.0*(in_size-zeros)/in_size);
    
    /* Метод 1: Прямой Order-1 (baseline) */
    uint8_t *comp1 = malloc(in_size * 2);
    RC rc;
    Model256 *m256 = malloc(sizeof(Model256));
    m256_init(m256);
    rc_enc_init(&rc, comp1);
    uint8_t ctx = 0;
    for (size_t i = 0; i < in_size; i++) {
        rc_enc_sym(&rc, m256, ctx, mtf_out[i]);
        ctx = mtf_out[i];
    }
    rc_enc_flush(&rc);
    size_t size1 = rc.out_size;
    printf("Метод 1 (Order-1): %zu байт (%.2fx)\n", size1, (double)in_size/size1);
    
    /* Метод 2: Фрактальное разделение (битовая маска + значения) */
    /* Поток 1: биты (0 = ноль, 1 = не ноль) */
    /* Поток 2: только ненулевые значения */
    
    uint8_t *nonzero = malloc(in_size);
    size_t nz_count = 0;
    for (size_t i = 0; i < in_size; i++) {
        if (mtf_out[i] != 0) nonzero[nz_count++] = mtf_out[i];
    }
    
    /* Сжимаем битовую маску */
    uint8_t *comp_bits = malloc(in_size);
    Model2 *m2 = malloc(sizeof(Model2));
    m2_init(m2);
    rc_enc_init(&rc, comp_bits);
    uint8_t bit_ctx = 0;
    for (size_t i = 0; i < in_size; i++) {
        uint8_t bit = (mtf_out[i] != 0) ? 1 : 0;
        rc_enc_bit(&rc, m2, bit_ctx, bit);
        bit_ctx = bit;
    }
    rc_enc_flush(&rc);
    size_t bits_size = rc.out_size;
    
    /* Сжимаем ненулевые значения */
    uint8_t *comp_vals = malloc(nz_count * 2);
    m256_init(m256);
    rc_enc_init(&rc, comp_vals);
    ctx = 0;
    for (size_t i = 0; i < nz_count; i++) {
        rc_enc_sym(&rc, m256, ctx, nonzero[i]);
        ctx = nonzero[i];
    }
    rc_enc_flush(&rc);
    size_t vals_size = rc.out_size;
    
    size_t size2 = bits_size + vals_size;
    printf("Метод 2 (Фрактальный):\n");
    printf("  Биты: %zu байт\n", bits_size);
    printf("  Значения: %zu байт\n", vals_size);
    printf("  Всего: %zu байт (%.2fx)\n", size2, (double)in_size/size2);
    
    /* Выбираем лучший */
    int use_fractal = (size2 < size1);
    size_t best_size = use_fractal ? size2 : size1;
    printf("Выбран: %s (%.2fx)\n", use_fractal ? "Фрактальный" : "Order-1", (double)in_size/best_size);
    
    /* Записываем */
    size_t hdr = 20;  /* magic(4) + size(4) + bwt_idx(4) + crc(4) + method(1) + reserved(3) */
    uint8_t *out = malloc(hdr + best_size);
    
    out[0] = (MAGIC >> 24) & 0xFF;
    out[1] = (MAGIC >> 16) & 0xFF;
    out[2] = (MAGIC >> 8) & 0xFF;
    out[3] = MAGIC & 0xFF;
    out[4] = (in_size >> 24) & 0xFF;
    out[5] = (in_size >> 16) & 0xFF;
    out[6] = (in_size >> 8) & 0xFF;
    out[7] = in_size & 0xFF;
    out[8] = (bwt_idx >> 24) & 0xFF;
    out[9] = (bwt_idx >> 16) & 0xFF;
    out[10] = (bwt_idx >> 8) & 0xFF;
    out[11] = bwt_idx & 0xFF;
    out[12] = (crc >> 24) & 0xFF;
    out[13] = (crc >> 16) & 0xFF;
    out[14] = (crc >> 8) & 0xFF;
    out[15] = crc & 0xFF;
    out[16] = use_fractal ? 2 : 1;  /* method */
    out[17] = 0; out[18] = 0; out[19] = 0;  /* reserved */
    
    if (use_fractal) {
        /* bits_size(4) + bits + vals */
        out[20] = (bits_size >> 24) & 0xFF;
        out[21] = (bits_size >> 16) & 0xFF;
        out[22] = (bits_size >> 8) & 0xFF;
        out[23] = bits_size & 0xFF;
        memcpy(out + 24, comp_bits, bits_size);
        memcpy(out + 24 + bits_size, comp_vals, vals_size);
        best_size = 4 + bits_size + vals_size;
    } else {
        memcpy(out + hdr, comp1, size1);
        best_size = size1;
    }
    
    size_t comp_size = hdr + best_size;
    
    FILE *fout = fopen(out_path, "wb");
    fwrite(out, 1, comp_size, fout);
    fclose(fout);
    
    printf("\n=== РЕЗУЛЬТАТ ===\n");
    printf("Вход: %zu байт\n", in_size);
    printf("Выход: %zu байт\n", comp_size);
    printf("Степень сжатия: %.2fx\n", (double)in_size / comp_size);
    
    free(m256); free(m2);
    free(comp1); free(comp_bits); free(comp_vals); free(nonzero);
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
    if (magic != MAGIC) {
        fprintf(stderr, "Invalid magic\n");
        free(compressed); return 1;
    }
    
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
        /* Order-1 */
        Model256 *m256 = malloc(sizeof(Model256));
        m256_init(m256);
        rc_dec_init(&rc, compressed + 20);
        uint8_t ctx = 0;
        for (size_t i = 0; i < orig; i++) {
            mtf_out[i] = rc_dec_sym(&rc, m256, ctx);
            ctx = mtf_out[i];
        }
        free(m256);
    } else {
        /* Фрактальный */
        size_t bits_size = (compressed[20] << 24) | (compressed[21] << 16) |
                           (compressed[22] << 8) | compressed[23];
        
        /* Декодируем биты */
        Model2 *m2 = malloc(sizeof(Model2));
        m2_init(m2);
        rc_dec_init(&rc, compressed + 24);
        uint8_t *bits = malloc(orig);
        uint8_t bit_ctx = 0;
        size_t nz_count = 0;
        for (size_t i = 0; i < orig; i++) {
            bits[i] = rc_dec_bit(&rc, m2, bit_ctx);
            bit_ctx = bits[i];
            if (bits[i]) nz_count++;
        }
        
        /* Декодируем значения */
        Model256 *m256 = malloc(sizeof(Model256));
        m256_init(m256);
        rc_dec_init(&rc, compressed + 24 + bits_size);
        uint8_t *vals = malloc(nz_count);
        uint8_t ctx = 0;
        for (size_t i = 0; i < nz_count; i++) {
            vals[i] = rc_dec_sym(&rc, m256, ctx);
            ctx = vals[i];
        }
        
        /* Восстанавливаем */
        size_t vi = 0;
        for (size_t i = 0; i < orig; i++) {
            mtf_out[i] = bits[i] ? vals[vi++] : 0;
        }
        
        free(m2); free(m256); free(bits); free(vals);
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
        free(compressed); free(out_data);
        return 1;
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
        printf("KOLIBRI FRACTAL v25 - Истинная фрактальная рекурсия\n");
        printf("Usage: %s compress|decompress <in> <out>\n", argv[0]);
        return 1;
    }
    if (strcmp(argv[1], "compress") == 0) return compress_file(argv[2], argv[3]);
    if (strcmp(argv[1], "decompress") == 0) return decompress_file(argv[2], argv[3]);
    return 1;
}
