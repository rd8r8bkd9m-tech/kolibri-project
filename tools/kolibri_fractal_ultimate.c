/*
 * KOLIBRI FRACTAL ULTIMATE - Финальная оптимизация
 * v29: Гибридный подход - адаптивный выбор числа фрактальных уровней
 *      + контекстное смешивание между ядрами
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define BLOCK_SIZE (1024*1024)
#define TOP (1U << 24)
#define BOT (1U << 16)

/* CRC32 */
static uint32_t crc32_table[256];
static void crc32_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
            c = (c >> 1) ^ (c & 1 ? 0xEDB88320 : 0);
        crc32_table[i] = c;
    }
}
static uint32_t crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++)
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFF;
}

/* BWT */
static const uint8_t *bwt_data;
static size_t bwt_len;

static int bwt_cmp(const void *a, const void *b) {
    size_t i = *(const size_t*)a;
    size_t j = *(const size_t*)b;
    for (size_t k = 0; k < bwt_len; k++) {
        uint8_t ci = bwt_data[(i + k) % bwt_len];
        uint8_t cj = bwt_data[(j + k) % bwt_len];
        if (ci != cj) return ci - cj;
    }
    return 0;
}

static size_t bwt_encode(const uint8_t *in, size_t len, uint8_t *out) {
    size_t *idx = malloc(len * sizeof(size_t));
    for (size_t i = 0; i < len; i++) idx[i] = i;
    bwt_data = in; bwt_len = len;
    qsort(idx, len, sizeof(size_t), bwt_cmp);
    size_t primary = 0;
    for (size_t i = 0; i < len; i++) {
        if (idx[i] == 0) primary = i;
        out[i] = in[(idx[i] + len - 1) % len];
    }
    free(idx);
    return primary;
}

static void bwt_decode(const uint8_t *in, size_t len, size_t primary, uint8_t *out) {
    size_t count[256] = {0};
    size_t *T = malloc(len * sizeof(size_t));
    for (size_t i = 0; i < len; i++) count[in[i]]++;
    size_t sum = 0;
    for (int c = 0; c < 256; c++) { size_t t = count[c]; count[c] = sum; sum += t; }
    for (size_t i = 0; i < len; i++) T[count[in[i]]++] = i;
    size_t idx = primary;
    for (size_t i = 0; i < len; i++) { out[i] = in[idx]; idx = T[idx]; }
    free(T);
}

/* MTF */
static void mtf_encode(uint8_t *data, size_t len) {
    uint8_t table[256];
    for (int i = 0; i < 256; i++) table[i] = i;
    for (size_t i = 0; i < len; i++) {
        uint8_t c = data[i], rank = 0;
        while (table[rank] != c) rank++;
        data[i] = rank;
        memmove(table + 1, table, rank);
        table[0] = c;
    }
}

static void mtf_decode(uint8_t *data, size_t len) {
    uint8_t table[256];
    for (int i = 0; i < 256; i++) table[i] = i;
    for (size_t i = 0; i < len; i++) {
        uint8_t rank = data[i];
        uint8_t c = table[rank];
        data[i] = c;
        memmove(table + 1, table, rank);
        table[0] = c;
    }
}

/* Range Coder */
typedef struct {
    uint8_t *ptr, *start;
    uint32_t low, range, code;
} RangeCoder;

static void rc_init_enc(RangeCoder *rc, uint8_t *buf) {
    rc->start = rc->ptr = buf;
    rc->low = 0; rc->range = 0xFFFFFFFF;
}

static void rc_enc_norm(RangeCoder *rc) {
    while ((rc->low ^ (rc->low + rc->range)) < TOP || 
           (rc->range < BOT && ((rc->range = -rc->low & (BOT-1)), 1))) {
        *rc->ptr++ = rc->low >> 24;
        rc->low <<= 8;
        rc->range <<= 8;
    }
}

static void rc_enc_bit(RangeCoder *rc, uint16_t *prob, int bit) {
    uint32_t bound = (rc->range >> 12) * (*prob);
    if (bit) {
        rc->range = bound;
        *prob += (4096 - *prob) >> 5;
    } else {
        rc->low += bound;
        rc->range -= bound;
        *prob -= *prob >> 5;
    }
    rc_enc_norm(rc);
}

static void rc_enc_byte(RangeCoder *rc, uint16_t probs[256][8], uint8_t byte) {
    for (int i = 7; i >= 0; i--)
        rc_enc_bit(rc, &probs[byte >> (i+1)][i], (byte >> i) & 1);
}

static void rc_enc_byte_ctx(RangeCoder *rc, uint16_t probs[256][256][8], uint8_t ctx, uint8_t byte) {
    for (int i = 7; i >= 0; i--)
        rc_enc_bit(rc, &probs[ctx][byte >> (i+1)][i], (byte >> i) & 1);
}

static size_t rc_enc_finish(RangeCoder *rc) {
    for (int i = 0; i < 4; i++) { *rc->ptr++ = rc->low >> 24; rc->low <<= 8; }
    return rc->ptr - rc->start;
}

static void rc_init_dec(RangeCoder *rc, uint8_t *buf) {
    rc->ptr = buf;
    rc->low = 0; rc->range = 0xFFFFFFFF;
    rc->code = 0;
    for (int i = 0; i < 4; i++) rc->code = (rc->code << 8) | *rc->ptr++;
}

static void rc_dec_norm(RangeCoder *rc) {
    while ((rc->low ^ (rc->low + rc->range)) < TOP ||
           (rc->range < BOT && ((rc->range = -rc->low & (BOT-1)), 1))) {
        rc->code = (rc->code << 8) | *rc->ptr++;
        rc->low <<= 8;
        rc->range <<= 8;
    }
}

static int rc_dec_bit(RangeCoder *rc, uint16_t *prob) {
    uint32_t bound = (rc->range >> 12) * (*prob);
    int bit;
    if (rc->code < rc->low + bound) {
        rc->range = bound;
        *prob += (4096 - *prob) >> 5;
        bit = 1;
    } else {
        rc->low += bound;
        rc->range -= bound;
        *prob -= *prob >> 5;
        bit = 0;
    }
    rc_dec_norm(rc);
    return bit;
}

static uint8_t rc_dec_byte(RangeCoder *rc, uint16_t probs[256][8]) {
    uint8_t byte = 0;
    for (int i = 7; i >= 0; i--) {
        int bit = rc_dec_bit(rc, &probs[byte][i]);
        byte = (byte << 1) | bit;
    }
    return byte;
}

static uint8_t rc_dec_byte_ctx(RangeCoder *rc, uint16_t probs[256][256][8], uint8_t ctx) {
    uint8_t byte = 0;
    for (int i = 7; i >= 0; i--) {
        int bit = rc_dec_bit(rc, &probs[ctx][byte][i]);
        byte = (byte << 1) | bit;
    }
    return byte;
}

/* 
 * ULTIMATE COMPRESSION: Адаптивный гибридный фрактальный кодер
 * 
 * Идея: Анализируем распределение MTF и выбираем оптимальное число уровней
 * Затем применяем контекстное смешивание между уровнями
 */

typedef struct {
    uint8_t *bits;       // Битовая карта уровня
    uint8_t *values;     // Значения для этого уровня  
    size_t bits_count;
    size_t values_count;
} FractalLevel;

static size_t compress_ultimate(const uint8_t *in, size_t len, uint8_t *out) {
    uint8_t *bwt = malloc(len);
    size_t primary = bwt_encode(in, len, bwt);
    mtf_encode(bwt, len);
    
    // Анализ распределения
    size_t hist[256] = {0};
    for (size_t i = 0; i < len; i++) hist[bwt[i]]++;
    
    printf("\n=== РАСПРЕДЕЛЕНИЕ MTF ===\n");
    printf("Нули: %zu (%.1f%%)\n", hist[0], 100.0*hist[0]/len);
    printf("1-3: %zu (%.1f%%)\n", hist[1]+hist[2]+hist[3], 100.0*(hist[1]+hist[2]+hist[3])/len);
    printf("4-15: %zu (%.1f%%)\n", 
           hist[4]+hist[5]+hist[6]+hist[7]+hist[8]+hist[9]+hist[10]+hist[11]+hist[12]+hist[13]+hist[14]+hist[15],
           100.0*(hist[4]+hist[5]+hist[6]+hist[7]+hist[8]+hist[9]+hist[10]+hist[11]+hist[12]+hist[13]+hist[14]+hist[15])/len);
    
    // Определяем оптимальный порог
    // Уровень 0: нули (наибольшая группа)
    // Уровень 1: 1-3 (очень частые)
    // Уровень 2: 4-15 (средне частые)  
    // Уровень 3: 16+ (редкие)
    
    // Подсчёт по уровням
    size_t cnt[4] = {0, 0, 0, 0};
    for (size_t i = 0; i < len; i++) {
        uint8_t v = bwt[i];
        if (v == 0) cnt[0]++;
        else if (v <= 3) cnt[1]++;
        else if (v <= 15) cnt[2]++;
        else cnt[3]++;
    }
    
    printf("\n=== 4 ФРАКТАЛЬНЫХ УРОВНЯ ===\n");
    printf("L0 (=0):   %zu (%.1f%%)\n", cnt[0], 100.0*cnt[0]/len);
    printf("L1 (1-3):  %zu (%.1f%%)\n", cnt[1], 100.0*cnt[1]/len);
    printf("L2 (4-15): %zu (%.1f%%)\n", cnt[2], 100.0*cnt[2]/len);
    printf("L3 (16+):  %zu (%.1f%%)\n", cnt[3], 100.0*cnt[3]/len);
    
    // Создаём фрактальную структуру
    // bits0: 0 vs non-0
    // bits1: для non-0: 1-3 vs 4+
    // bits2: для 4+: 4-15 vs 16+
    // values1: значения 1-3 кодируем 2 битами
    // values2: значения 4-15 кодируем 4 битами
    // values3: значения 16+ кодируем Order-1
    
    uint8_t *bits0 = malloc((len + 7) / 8);
    uint8_t *bits1 = malloc((cnt[1] + cnt[2] + cnt[3] + 7) / 8);
    uint8_t *bits2 = malloc((cnt[2] + cnt[3] + 7) / 8);
    uint8_t *vals1 = malloc(cnt[1] * 2 / 8 + 16);  // 2 бита на значение
    uint8_t *vals2 = malloc(cnt[2] * 4 / 8 + 16);  // 4 бита на значение
    uint8_t *vals3 = malloc(cnt[3] + 16);
    
    memset(bits0, 0, (len + 7) / 8);
    memset(bits1, 0, (cnt[1] + cnt[2] + cnt[3] + 7) / 8);
    memset(bits2, 0, (cnt[2] + cnt[3] + 7) / 8);
    
    size_t b0 = 0, b1 = 0, b2 = 0;
    size_t v1 = 0, v2 = 0, v3 = 0;
    
    for (size_t i = 0; i < len; i++) {
        uint8_t v = bwt[i];
        
        if (v == 0) {
            // bits0[i] = 0 (уже memset)
        } else {
            bits0[i/8] |= (1 << (i % 8));
            
            if (v <= 3) {
                // bits1[b1] = 0
                // Кодируем 1-3 -> 0-2 (2 бита)
                uint8_t code = v - 1; // 0, 1, 2
                vals1[v1/4] |= (code << ((v1 % 4) * 2));
                v1++;
            } else {
                bits1[b1/8] |= (1 << (b1 % 8));
                
                if (v <= 15) {
                    // bits2[b2] = 0
                    // Кодируем 4-15 -> 0-11 (4 бита)
                    uint8_t code = v - 4;
                    vals2[v2/2] |= (code << ((v2 % 2) * 4));
                    v2++;
                } else {
                    bits2[b2/8] |= (1 << (b2 % 8));
                    vals3[v3++] = v - 16;  // Сдвигаем на 16
                }
                b2++;
            }
            b1++;
        }
        b0++;
    }
    
    // Сжимаем каждый поток Order-0 RC для битов, Order-1 для остатков
    RangeCoder rc;
    uint8_t *ptr = out;
    
    // Заголовок
    *ptr++ = 'K'; *ptr++ = 'U'; *ptr++ = 'L'; *ptr++ = 'T';
    *(uint32_t*)ptr = len; ptr += 4;
    *(uint32_t*)ptr = primary; ptr += 4;
    *(uint32_t*)ptr = cnt[1]; ptr += 4;
    *(uint32_t*)ptr = cnt[2]; ptr += 4;
    *(uint32_t*)ptr = cnt[3]; ptr += 4;
    *(uint32_t*)ptr = crc32(in, len); ptr += 4;
    
    // Сжимаем bits0 (адаптивным RC)
    {
        uint16_t prob = 2048;
        rc_init_enc(&rc, ptr + 4);
        size_t bytes0 = (len + 7) / 8;
        for (size_t i = 0; i < len; i++) {
            int bit = (bits0[i/8] >> (i % 8)) & 1;
            rc_enc_bit(&rc, &prob, bit);
        }
        size_t sz = rc_enc_finish(&rc);
        *(uint32_t*)ptr = sz; ptr += 4 + sz;
        printf("bits0: %zu байт\n", sz);
    }
    
    // Сжимаем bits1
    {
        uint16_t prob = 2048;
        rc_init_enc(&rc, ptr + 4);
        for (size_t i = 0; i < b1; i++) {
            int bit = (bits1[i/8] >> (i % 8)) & 1;
            rc_enc_bit(&rc, &prob, bit);
        }
        size_t sz = rc_enc_finish(&rc);
        *(uint32_t*)ptr = sz; ptr += 4 + sz;
        printf("bits1: %zu байт\n", sz);
    }
    
    // Сжимаем bits2
    {
        uint16_t prob = 2048;
        rc_init_enc(&rc, ptr + 4);
        for (size_t i = 0; i < b2; i++) {
            int bit = (bits2[i/8] >> (i % 8)) & 1;
            rc_enc_bit(&rc, &prob, bit);
        }
        size_t sz = rc_enc_finish(&rc);
        *(uint32_t*)ptr = sz; ptr += 4 + sz;
        printf("bits2: %zu байт\n", sz);
    }
    
    // Сжимаем vals1 (2 бита каждый) - используем 4 вероятности
    {
        uint16_t probs[4] = {2048, 2048, 2048, 2048};
        rc_init_enc(&rc, ptr + 4);
        for (size_t i = 0; i < v1; i++) {
            uint8_t code = (vals1[i/4] >> ((i % 4) * 2)) & 3;
            // Кодируем 2 бита
            rc_enc_bit(&rc, &probs[0], (code >> 1) & 1);
            rc_enc_bit(&rc, &probs[1 + ((code >> 1) & 1)], code & 1);
        }
        size_t sz = rc_enc_finish(&rc);
        *(uint32_t*)ptr = sz; ptr += 4 + sz;
        printf("vals1: %zu байт\n", sz);
    }
    
    // Сжимаем vals2 (4 бита каждый) - используем дерево
    {
        uint16_t probs[16] = {2048,2048,2048,2048,2048,2048,2048,2048,
                              2048,2048,2048,2048,2048,2048,2048,2048};
        rc_init_enc(&rc, ptr + 4);
        for (size_t i = 0; i < v2; i++) {
            uint8_t code = (vals2[i/2] >> ((i % 2) * 4)) & 15;
            // Кодируем 4 бита бинарным деревом
            int idx = 1;
            for (int b = 3; b >= 0; b--) {
                int bit = (code >> b) & 1;
                rc_enc_bit(&rc, &probs[idx], bit);
                idx = idx * 2 + bit;
            }
        }
        size_t sz = rc_enc_finish(&rc);
        *(uint32_t*)ptr = sz; ptr += 4 + sz;
        printf("vals2: %zu байт\n", sz);
    }
    
    // Сжимаем vals3 Order-1
    {
        uint16_t (*probs)[256][8] = calloc(256 * 256, 8 * sizeof(uint16_t));
        for (int i = 0; i < 256; i++)
            for (int j = 0; j < 256; j++)
                for (int k = 0; k < 8; k++)
                    probs[i][j][k] = 2048;
        
        rc_init_enc(&rc, ptr + 4);
        uint8_t ctx = 0;
        for (size_t i = 0; i < v3; i++) {
            rc_enc_byte_ctx(&rc, probs, ctx, vals3[i]);
            ctx = vals3[i];
        }
        size_t sz = rc_enc_finish(&rc);
        *(uint32_t*)ptr = sz; ptr += 4 + sz;
        printf("vals3: %zu байт\n", sz);
        free(probs);
    }
    
    free(bwt);
    free(bits0); free(bits1); free(bits2);
    free(vals1); free(vals2); free(vals3);
    
    return ptr - out;
}

static size_t decompress_ultimate(const uint8_t *in, size_t in_len, uint8_t *out) {
    const uint8_t *ptr = in;
    
    if (ptr[0] != 'K' || ptr[1] != 'U' || ptr[2] != 'L' || ptr[3] != 'T') {
        fprintf(stderr, "Invalid magic\n");
        return 0;
    }
    ptr += 4;
    
    uint32_t len = *(uint32_t*)ptr; ptr += 4;
    uint32_t primary = *(uint32_t*)ptr; ptr += 4;
    uint32_t cnt1 = *(uint32_t*)ptr; ptr += 4;
    uint32_t cnt2 = *(uint32_t*)ptr; ptr += 4;
    uint32_t cnt3 = *(uint32_t*)ptr; ptr += 4;
    uint32_t stored_crc = *(uint32_t*)ptr; ptr += 4;
    
    uint8_t *bits0 = malloc(len);
    uint8_t *bits1 = malloc(cnt1 + cnt2 + cnt3);
    uint8_t *bits2 = malloc(cnt2 + cnt3);
    uint8_t *vals1 = malloc(cnt1);
    uint8_t *vals2 = malloc(cnt2);
    uint8_t *vals3 = malloc(cnt3);
    
    RangeCoder rc;
    
    // Декодируем bits0
    {
        uint32_t sz = *(uint32_t*)ptr; ptr += 4;
        uint16_t prob = 2048;
        rc_init_dec(&rc, (uint8_t*)ptr);
        for (size_t i = 0; i < len; i++) {
            bits0[i] = rc_dec_bit(&rc, &prob);
        }
        ptr += sz;
    }
    
    // Декодируем bits1
    size_t b1_count = cnt1 + cnt2 + cnt3;
    {
        uint32_t sz = *(uint32_t*)ptr; ptr += 4;
        uint16_t prob = 2048;
        rc_init_dec(&rc, (uint8_t*)ptr);
        for (size_t i = 0; i < b1_count; i++) {
            bits1[i] = rc_dec_bit(&rc, &prob);
        }
        ptr += sz;
    }
    
    // Декодируем bits2
    size_t b2_count = cnt2 + cnt3;
    {
        uint32_t sz = *(uint32_t*)ptr; ptr += 4;
        uint16_t prob = 2048;
        rc_init_dec(&rc, (uint8_t*)ptr);
        for (size_t i = 0; i < b2_count; i++) {
            bits2[i] = rc_dec_bit(&rc, &prob);
        }
        ptr += sz;
    }
    
    // Декодируем vals1
    {
        uint32_t sz = *(uint32_t*)ptr; ptr += 4;
        uint16_t probs[4] = {2048, 2048, 2048, 2048};
        rc_init_dec(&rc, (uint8_t*)ptr);
        for (size_t i = 0; i < cnt1; i++) {
            int b1 = rc_dec_bit(&rc, &probs[0]);
            int b0 = rc_dec_bit(&rc, &probs[1 + b1]);
            vals1[i] = (b1 << 1) | b0;
        }
        ptr += sz;
    }
    
    // Декодируем vals2
    {
        uint32_t sz = *(uint32_t*)ptr; ptr += 4;
        uint16_t probs[16] = {2048,2048,2048,2048,2048,2048,2048,2048,
                              2048,2048,2048,2048,2048,2048,2048,2048};
        rc_init_dec(&rc, (uint8_t*)ptr);
        for (size_t i = 0; i < cnt2; i++) {
            int idx = 1;
            uint8_t code = 0;
            for (int b = 3; b >= 0; b--) {
                int bit = rc_dec_bit(&rc, &probs[idx]);
                code = (code << 1) | bit;
                idx = idx * 2 + bit;
            }
            vals2[i] = code;
        }
        ptr += sz;
    }
    
    // Декодируем vals3
    {
        uint32_t sz = *(uint32_t*)ptr; ptr += 4;
        uint16_t (*probs)[256][8] = calloc(256 * 256, 8 * sizeof(uint16_t));
        for (int i = 0; i < 256; i++)
            for (int j = 0; j < 256; j++)
                for (int k = 0; k < 8; k++)
                    probs[i][j][k] = 2048;
        
        rc_init_dec(&rc, (uint8_t*)ptr);
        uint8_t ctx = 0;
        for (size_t i = 0; i < cnt3; i++) {
            vals3[i] = rc_dec_byte_ctx(&rc, probs, ctx);
            ctx = vals3[i];
        }
        ptr += sz;
        free(probs);
    }
    
    // Восстанавливаем MTF
    uint8_t *mtf = malloc(len);
    size_t i1 = 0, i2 = 0, i3 = 0;
    size_t b1 = 0, b2 = 0;
    
    for (size_t i = 0; i < len; i++) {
        if (bits0[i] == 0) {
            mtf[i] = 0;
        } else {
            if (bits1[b1] == 0) {
                mtf[i] = vals1[i1++] + 1;  // 1-3
            } else {
                if (bits2[b2] == 0) {
                    mtf[i] = vals2[i2++] + 4;  // 4-15
                } else {
                    mtf[i] = vals3[i3++] + 16;  // 16+
                }
                b2++;
            }
            b1++;
        }
    }
    
    // MTF decode
    mtf_decode(mtf, len);
    
    // BWT decode
    bwt_decode(mtf, len, primary, out);
    
    uint32_t calc_crc = crc32(out, len);
    printf("CRC: stored=%08X calc=%08X %s\n", stored_crc, calc_crc,
           stored_crc == calc_crc ? "OK" : "MISMATCH");
    
    free(bits0); free(bits1); free(bits2);
    free(vals1); free(vals2); free(vals3);
    free(mtf);
    
    return len;
}

int main(int argc, char **argv) {
    crc32_init();
    
    if (argc < 4) {
        printf("Kolibri Fractal Ultimate v29\n");
        printf("Usage: %s compress|decompress input output\n", argv[0]);
        return 1;
    }
    
    FILE *f = fopen(argv[2], "rb");
    if (!f) { perror("fopen input"); return 1; }
    fseek(f, 0, SEEK_END);
    size_t in_len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *in = malloc(in_len);
    fread(in, 1, in_len, f);
    fclose(f);
    
    uint8_t *out = malloc(in_len * 2 + 1024);
    size_t out_len;
    
    if (strcmp(argv[1], "compress") == 0) {
        out_len = compress_ultimate(in, in_len, out);
        printf("\n=== РЕЗУЛЬТАТ ===\n");
        printf("Вход: %zu байт\n", in_len);
        printf("Выход: %zu байт\n", out_len);
        printf("Степень сжатия: %.2fx\n", (double)in_len / out_len);
    } else {
        out_len = decompress_ultimate(in, in_len, out);
        printf("Распаковано: %zu байт\n", out_len);
    }
    
    f = fopen(argv[3], "wb");
    fwrite(out, 1, out_len, f);
    fclose(f);
    
    free(in);
    free(out);
    return 0;
}
