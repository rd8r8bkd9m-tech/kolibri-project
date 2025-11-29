/*
 * KOLIBRI FRACTAL-10 v28
 * 10 уровней фрактальной вложенности + 10 логических ядер
 * 
 * АРХИТЕКТУРА:
 * - 10 логических ядер (0-9), каждое специализируется на своём диапазоне
 * - 10 уровней фрактальной вложенности: данные рекурсивно разделяются
 * - Каждый уровень использует свою специализированную модель
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* CRC32 */
static uint32_t crc32_table[256];
static int crc32_ready = 0;
static void crc32_init(void) {
    if (crc32_ready) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) c = (c >> 1) ^ ((c & 1) ? 0xEDB88320 : 0);
        crc32_table[i] = c;
    }
    crc32_ready = 1;
}
static uint32_t crc32(const uint8_t *d, size_t n) {
    crc32_init();
    uint32_t c = 0xFFFFFFFF;
    for (size_t i = 0; i < n; i++) c = crc32_table[(c ^ d[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFF;
}

/* BWT */
static const uint8_t *g_bwt; static size_t g_bwt_n;
static int bwt_cmp(const void *a, const void *b) {
    size_t i = *(const size_t *)a, j = *(const size_t *)b;
    for (size_t k = 0; k < g_bwt_n; k++) {
        uint8_t ci = g_bwt[(i + k) % g_bwt_n], cj = g_bwt[(j + k) % g_bwt_n];
        if (ci != cj) return (int)ci - (int)cj;
    }
    return 0;
}
static size_t bwt_enc(const uint8_t *in, size_t n, uint8_t *out) {
    size_t *idx = malloc(n * sizeof(size_t));
    for (size_t i = 0; i < n; i++) idx[i] = i;
    g_bwt = in; g_bwt_n = n;
    qsort(idx, n, sizeof(size_t), bwt_cmp);
    size_t orig = 0;
    for (size_t i = 0; i < n; i++) {
        out[i] = in[(idx[i] + n - 1) % n];
        if (idx[i] == 0) orig = i;
    }
    free(idx);
    return orig;
}
static void bwt_dec(const uint8_t *L, size_t n, size_t I, uint8_t *out) {
    size_t C[256] = {0}, K[256], sum = 0;
    for (size_t i = 0; i < n; i++) C[L[i]]++;
    for (int c = 0; c < 256; c++) { K[c] = sum; sum += C[c]; }
    size_t *P = malloc(n * sizeof(size_t));
    size_t cnt[256] = {0};
    for (size_t i = 0; i < n; i++) P[i] = cnt[L[i]]++;
    size_t j = I;
    for (size_t i = n; i > 0; i--) { out[i-1] = L[j]; j = K[L[j]] + P[j]; }
    free(P);
}

/* MTF */
static void mtf_enc(const uint8_t *in, size_t n, uint8_t *out) {
    uint8_t t[256]; for (int i = 0; i < 256; i++) t[i] = i;
    for (size_t i = 0; i < n; i++) {
        uint8_t c = in[i], r = 0;
        while (t[r] != c) r++;
        out[i] = r;
        memmove(&t[1], &t[0], r);
        t[0] = c;
    }
}
static void mtf_dec(const uint8_t *in, size_t n, uint8_t *out) {
    uint8_t t[256]; for (int i = 0; i < 256; i++) t[i] = i;
    for (size_t i = 0; i < n; i++) {
        uint8_t r = in[i], c = t[r];
        out[i] = c;
        memmove(&t[1], &t[0], r);
        t[0] = c;
    }
}

/* ============== 10 ЛОГИЧЕСКИХ ЯДЕР ============== */
/*
 * Ядро 0: Нули (самое важное - 86.9%)
 * Ядро 1: Значение 1
 * Ядро 2: Значение 2
 * ...
 * Ядро 9: Значения >= 9
 * 
 * Каждое ядро использует специализированную бинарную модель
 */

/* Range Coder */
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

/* 10-уровневая бинарная модель с контекстом */
typedef struct {
    uint16_t f0, f1;
} BinCtx;

typedef struct {
    BinCtx ctx[10][10];  /* [level][prev_decision] */
} FractalModel;

static void fm_init(FractalModel *m) {
    for (int l = 0; l < 10; l++) {
        for (int p = 0; p < 10; p++) {
            m->ctx[l][p].f0 = 1;
            m->ctx[l][p].f1 = 1;
        }
    }
}

static void fm_update(FractalModel *m, int level, int prev, uint8_t bit) {
    if (level > 9) level = 9;
    if (prev > 9) prev = 9;
    BinCtx *c = &m->ctx[level][prev];
    if (bit) c->f1 += 32; else c->f0 += 32;
    if (c->f0 + c->f1 > 0x3FFF) {
        c->f0 = (c->f0 >> 1) | 1;
        c->f1 = (c->f1 >> 1) | 1;
    }
}

static void rc_enc_bit(RC *rc, FractalModel *m, int level, int prev, uint8_t bit) {
    if (level > 9) level = 9;
    if (prev > 9) prev = 9;
    BinCtx *c = &m->ctx[level][prev];
    uint32_t total = c->f0 + c->f1;
    rc->range /= total;
    if (bit) {
        rc->low += c->f0 * rc->range;
        rc->range *= c->f1;
    } else {
        rc->range *= c->f0;
    }
    rc_enc_norm(rc);
    fm_update(m, level, prev, bit);
}

static uint8_t rc_dec_bit(RC *rc, FractalModel *m, int level, int prev) {
    if (level > 9) level = 9;
    if (prev > 9) prev = 9;
    BinCtx *c = &m->ctx[level][prev];
    uint32_t total = c->f0 + c->f1;
    rc->range /= total;
    uint32_t th = c->f0 * rc->range;
    uint8_t bit = (rc->code - rc->low >= th) ? 1 : 0;
    if (bit) {
        rc->low += th;
        rc->range *= c->f1;
    } else {
        rc->range *= c->f0;
    }
    rc_dec_norm(rc);
    fm_update(m, level, prev, bit);
    return bit;
}

/* Order-1 модель для остаточных данных */
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
    m->freq[c][s] += 8; m->freq[c][256] += 8;
    if (m->freq[c][256] > 0x3FFF) {
        uint16_t t = 0;
        for (int i = 0; i < 256; i++) { m->freq[c][i] = (m->freq[c][i] >> 1) | 1; t += m->freq[c][i]; }
        m->freq[c][256] = t;
    }
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
static uint8_t rc_dec_sym(RC *rc, Model256 *m, uint8_t ctx) {
    uint32_t total = m->freq[ctx][256];
    rc->range /= total;
    uint32_t target = (rc->code - rc->low) / rc->range;
    uint32_t cum = 0; uint8_t sym = 0;
    while (cum + m->freq[ctx][sym] <= target) { cum += m->freq[ctx][sym]; sym++; }
    rc->low += cum * rc->range;
    rc->range *= m->freq[ctx][sym];
    rc_dec_norm(rc);
    m256_update(m, ctx, sym);
    return sym;
}

/* ============== 10-УРОВНЕВОЕ ФРАКТАЛЬНОЕ КОДИРОВАНИЕ ============== */
/*
 * Уровень 0: Бит "равен 0?"
 * Уровень 1: Бит "равен 1?" (если не 0)
 * Уровень 2: Бит "равен 2?" (если не 0,1)
 * ...
 * Уровень 8: Бит "равен 8?" (если не 0-7)
 * Уровень 9: Бит "< 128?" (если >= 9)
 * + остаток кодируется Order-1
 * 
 * Это даёт 10 бинарных решений с адаптивными вероятностями
 */

#define MAGIC 0x4B463130  /* "KF10" */

static int compress_file(const char *in_path, const char *out_path) {
    FILE *fin = fopen(in_path, "rb");
    if (!fin) { perror("fopen"); return 1; }
    fseek(fin, 0, SEEK_END);
    size_t in_size = ftell(fin);
    fseek(fin, 0, SEEK_SET);
    uint8_t *in_data = malloc(in_size);
    if (fread(in_data, 1, in_size, fin) != in_size) { fclose(fin); return 1; }
    fclose(fin);
    
    uint32_t checksum = crc32(in_data, in_size);
    
    uint8_t *bwt_out = malloc(in_size);
    size_t bwt_idx = bwt_enc(in_data, in_size, bwt_out);
    
    uint8_t *mtf_out = malloc(in_size);
    mtf_enc(bwt_out, in_size, mtf_out);
    free(bwt_out);
    
    /* Статистика по 10 логическим ядрам */
    size_t core_count[10] = {0};
    for (size_t i = 0; i < in_size; i++) {
        int core = (mtf_out[i] < 9) ? mtf_out[i] : 9;
        core_count[core]++;
    }
    
    printf("=== 10 ЛОГИЧЕСКИХ ЯДЕР ===\n");
    for (int c = 0; c < 10; c++) {
        printf("Ядро %d%s: %zu (%.1f%%)\n", 
               c, c == 9 ? "+" : " ", 
               core_count[c], 100.0 * core_count[c] / in_size);
    }
    
    /* Буферы */
    size_t buf = in_size * 2;
    uint8_t *comp = malloc(buf);
    
    /* Метод 1: Baseline Order-1 */
    RC rc;
    Model256 *m256 = malloc(sizeof(Model256));
    m256_init(m256);
    rc_enc_init(&rc, comp, buf);
    uint8_t ctx = 0;
    for (size_t i = 0; i < in_size; i++) {
        rc_enc_sym(&rc, m256, ctx, mtf_out[i]);
        ctx = mtf_out[i];
    }
    rc_enc_flush(&rc);
    size_t baseline_size = rc.out_size;
    printf("\nBaseline Order-1: %zu байт (%.2fx)\n", baseline_size, (double)in_size/baseline_size);
    
    /* Метод 2: 10-уровневое фрактальное кодирование */
    FractalModel *fm = malloc(sizeof(FractalModel));
    fm_init(fm);
    
    uint8_t *comp_fractal = malloc(buf);
    rc_enc_init(&rc, comp_fractal, buf);
    
    int prev_level = 0;
    
    for (size_t i = 0; i < in_size; i++) {
        uint8_t v = mtf_out[i];
        int level_reached = 0;
        
        /* 10 уровней бинарных решений */
        for (int level = 0; level < 10; level++) {
            if (level < 9) {
                /* Уровни 0-8: "равен level?" */
                uint8_t bit = (v == level) ? 0 : 1;  /* 0 = да, 1 = нет */
                rc_enc_bit(&rc, fm, level, prev_level, bit);
                
                if (bit == 0) {
                    /* Значение = level, закончили */
                    level_reached = level;
                    break;
                }
                /* v > level, продолжаем */
            } else {
                /* Уровень 9: v >= 9, кодируем остаток */
                /* Сначала бит: < 128 или >= 128 */
                uint8_t bit = (v >= 128) ? 1 : 0;
                rc_enc_bit(&rc, fm, 9, prev_level, bit);
                level_reached = 9;
                break;
            }
        }
        prev_level = level_reached;
    }
    rc_enc_flush(&rc);
    size_t fractal_bits_size = rc.out_size;
    
    /* Кодируем остаточные значения (>= 9) отдельно */
    uint8_t *residuals = malloc(in_size);
    size_t res_count = 0;
    for (size_t i = 0; i < in_size; i++) {
        if (mtf_out[i] >= 9) {
            residuals[res_count++] = mtf_out[i] - 9;  /* 9-255 -> 0-246 */
        }
    }
    
    uint8_t *comp_res = malloc(buf);
    m256_init(m256);
    rc_enc_init(&rc, comp_res, buf);
    ctx = 0;
    for (size_t i = 0; i < res_count; i++) {
        rc_enc_sym(&rc, m256, ctx, residuals[i]);
        ctx = residuals[i];
    }
    rc_enc_flush(&rc);
    size_t residual_size = rc.out_size;
    
    size_t fractal_total = fractal_bits_size + residual_size + 8;  /* +8 для размеров */
    
    printf("\n10-уровневое фрактальное:\n");
    printf("  Биты (10 уровней): %zu байт\n", fractal_bits_size);
    printf("  Остатки (>= 9):    %zu байт (%zu значений)\n", residual_size, res_count);
    printf("  ИТОГО: %zu байт (%.2fx)\n", fractal_total, (double)in_size/fractal_total);
    
    /* Выбираем лучший метод */
    int method = (fractal_total < baseline_size) ? 2 : 1;
    size_t best_size = (method == 2) ? fractal_total : baseline_size;
    printf("\nВыбран метод: %d (%s)\n", method, method == 2 ? "Фрактальный" : "Baseline");
    
    /* Записываем результат */
    size_t hdr = 20;
    uint8_t *out = malloc(hdr + best_size);
    
    out[0] = (MAGIC >> 24) & 0xFF; out[1] = (MAGIC >> 16) & 0xFF;
    out[2] = (MAGIC >> 8) & 0xFF; out[3] = MAGIC & 0xFF;
    out[4] = (in_size >> 24) & 0xFF; out[5] = (in_size >> 16) & 0xFF;
    out[6] = (in_size >> 8) & 0xFF; out[7] = in_size & 0xFF;
    out[8] = (bwt_idx >> 24) & 0xFF; out[9] = (bwt_idx >> 16) & 0xFF;
    out[10] = (bwt_idx >> 8) & 0xFF; out[11] = bwt_idx & 0xFF;
    out[12] = (checksum >> 24) & 0xFF; out[13] = (checksum >> 16) & 0xFF;
    out[14] = (checksum >> 8) & 0xFF; out[15] = checksum & 0xFF;
    out[16] = method; out[17] = out[18] = out[19] = 0;
    
    size_t pos = 20;
    if (method == 2) {
        /* fractal_bits_size(4) + residual_size(4) + data */
        out[pos++] = (fractal_bits_size >> 24) & 0xFF;
        out[pos++] = (fractal_bits_size >> 16) & 0xFF;
        out[pos++] = (fractal_bits_size >> 8) & 0xFF;
        out[pos++] = fractal_bits_size & 0xFF;
        out[pos++] = (residual_size >> 24) & 0xFF;
        out[pos++] = (residual_size >> 16) & 0xFF;
        out[pos++] = (residual_size >> 8) & 0xFF;
        out[pos++] = residual_size & 0xFF;
        memcpy(out + pos, comp_fractal, fractal_bits_size); pos += fractal_bits_size;
        memcpy(out + pos, comp_res, residual_size); pos += residual_size;
    } else {
        m256_init(m256);
        rc_enc_init(&rc, out + pos, buf);
        ctx = 0;
        for (size_t i = 0; i < in_size; i++) {
            rc_enc_sym(&rc, m256, ctx, mtf_out[i]);
            ctx = mtf_out[i];
        }
        rc_enc_flush(&rc);
        pos += rc.out_size;
    }
    
    FILE *fout = fopen(out_path, "wb");
    fwrite(out, 1, pos, fout);
    fclose(fout);
    
    printf("\n=== РЕЗУЛЬТАТ ===\n");
    printf("Вход: %zu байт\n", in_size);
    printf("Выход: %zu байт\n", pos);
    printf("Степень сжатия: %.2fx\n", (double)in_size / pos);
    
    free(fm); free(m256);
    free(comp); free(comp_fractal); free(comp_res); free(residuals);
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
        /* Baseline Order-1 */
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
        /* Фрактальный метод */
        size_t pos = 20;
        size_t fractal_bits_size = (compressed[pos] << 24) | (compressed[pos+1] << 16) |
                                   (compressed[pos+2] << 8) | compressed[pos+3]; pos += 4;
        size_t residual_size = (compressed[pos] << 24) | (compressed[pos+1] << 16) |
                               (compressed[pos+2] << 8) | compressed[pos+3]; pos += 4;
        
        /* Декодируем фрактальные биты */
        FractalModel *fm = malloc(sizeof(FractalModel));
        fm_init(fm);
        rc_dec_init(&rc, compressed + pos);
        
        /* Первый проход: декодируем "каркас" (значения 0-8 и флаги для >=9) */
        uint8_t *skeleton = malloc(orig);
        size_t res_count = 0;
        int prev_level = 0;
        
        for (size_t i = 0; i < orig; i++) {
            int value = -1;
            int level_reached = 0;
            
            for (int level = 0; level < 10; level++) {
                if (level < 9) {
                    uint8_t bit = rc_dec_bit(&rc, fm, level, prev_level);
                    if (bit == 0) {
                        value = level;
                        level_reached = level;
                        break;
                    }
                } else {
                    /* Уровень 9: читаем бит но не используем */
                    rc_dec_bit(&rc, fm, 9, prev_level);
                    value = -1;  /* Будет заполнено из residuals */
                    level_reached = 9;
                    res_count++;
                    break;
                }
            }
            skeleton[i] = (uint8_t)(value >= 0 ? value : 255);  /* 255 = placeholder */
            prev_level = level_reached;
        }
        pos += fractal_bits_size;
        
        /* Декодируем остатки */
        Model256 *m256 = malloc(sizeof(Model256));
        m256_init(m256);
        rc_dec_init(&rc, compressed + pos);
        
        uint8_t *residuals = malloc(res_count + 1);
        uint8_t ctx = 0;
        for (size_t i = 0; i < res_count; i++) {
            residuals[i] = rc_dec_sym(&rc, m256, ctx) + 9;  /* 0-246 -> 9-255 */
            ctx = residuals[i] - 9;
        }
        
        /* Собираем финальный результат */
        size_t ri = 0;
        for (size_t i = 0; i < orig; i++) {
            if (skeleton[i] == 255) {
                mtf_out[i] = residuals[ri++];
            } else {
                mtf_out[i] = skeleton[i];
            }
        }
        
        free(fm); free(m256); free(skeleton); free(residuals);
    }
    
    uint8_t *bwt_out = malloc(orig);
    mtf_dec(mtf_out, orig, bwt_out);
    free(mtf_out);
    
    uint8_t *out_data = malloc(orig);
    bwt_dec(bwt_out, orig, bwt_idx, out_data);
    free(bwt_out);
    
    uint32_t check = crc32(out_data, orig);
    if (check != stored_crc) {
        fprintf(stderr, "CRC mismatch! Expected %08X got %08X\n", stored_crc, check);
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
        printf("KOLIBRI FRACTAL-10 v28\n");
        printf("10 уровней фрактальной вложенности + 10 логических ядер\n");
        printf("Usage: %s compress|decompress <in> <out>\n", argv[0]);
        return 1;
    }
    if (strcmp(argv[1], "compress") == 0) return compress_file(argv[2], argv[3]);
    if (strcmp(argv[1], "decompress") == 0) return decompress_file(argv[2], argv[3]);
    return 1;
}
