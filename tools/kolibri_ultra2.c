/*
 * KOLIBRI ULTRA v30 - Исправленный гибридный фрактальный кодер
 * Пороги: 0 / 1-3 / 4-15 / 16+
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

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

static void rc_enc_byte_o1(RangeCoder *rc, uint16_t probs[256][256][8], uint8_t ctx, uint8_t byte) {
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

static uint8_t rc_dec_byte_o1(RangeCoder *rc, uint16_t probs[256][256][8], uint8_t ctx) {
    uint8_t byte = 0;
    for (int i = 7; i >= 0; i--) {
        int bit = rc_dec_bit(rc, &probs[ctx][byte][i]);
        byte = (byte << 1) | bit;
    }
    return byte;
}

/* 
 * ULTRA COMPRESSION - 4 уровня фрактального сжатия
 * Используем: биты (0 vs non-0) -> биты (1-3 vs 4+) -> биты (4-15 vs 16+) -> значения
 */

static size_t compress_ultra(const uint8_t *in, size_t len, uint8_t *out) {
    uint8_t *bwt = malloc(len);
    size_t primary = bwt_encode(in, len, bwt);
    mtf_encode(bwt, len);
    
    // Подсчёт по уровням
    size_t cnt[4] = {0, 0, 0, 0};
    for (size_t i = 0; i < len; i++) {
        uint8_t v = bwt[i];
        if (v == 0) cnt[0]++;
        else if (v <= 3) cnt[1]++;
        else if (v <= 15) cnt[2]++;
        else cnt[3]++;
    }
    
    printf("=== 4 УРОВНЯ ===\n");
    printf("L0 (=0):   %zu (%.1f%%)\n", cnt[0], 100.0*cnt[0]/len);
    printf("L1 (1-3):  %zu (%.1f%%)\n", cnt[1], 100.0*cnt[1]/len);
    printf("L2 (4-15): %zu (%.1f%%)\n", cnt[2], 100.0*cnt[2]/len);
    printf("L3 (16+):  %zu (%.1f%%)\n", cnt[3], 100.0*cnt[3]/len);
    
    // Аллоцируем массивы
    size_t non_zero = cnt[1] + cnt[2] + cnt[3];
    size_t lvl2_cnt = cnt[2] + cnt[3];
    
    uint8_t *bits0 = malloc(len);           // 0 vs non-0
    uint8_t *bits1 = malloc(non_zero);      // 1-3 vs 4+  
    uint8_t *bits2 = malloc(lvl2_cnt);      // 4-15 vs 16+
    uint8_t *vals1 = malloc(cnt[1]);        // значения 1-3 (как 0-2)
    uint8_t *vals2 = malloc(cnt[2]);        // значения 4-15 (как 0-11)
    uint8_t *vals3 = malloc(cnt[3]);        // значения 16+ (как 0-239)
    
    size_t b0 = 0, b1 = 0, b2 = 0;
    size_t v1 = 0, v2 = 0, v3 = 0;
    
    for (size_t i = 0; i < len; i++) {
        uint8_t v = bwt[i];
        
        if (v == 0) {
            bits0[b0++] = 0;
        } else {
            bits0[b0++] = 1;
            
            if (v <= 3) {
                bits1[b1++] = 0;
                vals1[v1++] = v - 1;  // 1-3 -> 0-2
            } else {
                bits1[b1++] = 1;
                
                if (v <= 15) {
                    bits2[b2++] = 0;
                    vals2[v2++] = v - 4;  // 4-15 -> 0-11
                } else {
                    bits2[b2++] = 1;
                    vals3[v3++] = v - 16;  // 16-255 -> 0-239
                }
            }
        }
    }
    
    RangeCoder rc;
    uint8_t *ptr = out;
    
    // Header
    *ptr++ = 'K'; *ptr++ = 'U'; *ptr++ = 'L'; *ptr++ = '2';
    *(uint32_t*)ptr = len; ptr += 4;
    *(uint32_t*)ptr = primary; ptr += 4;
    *(uint32_t*)ptr = cnt[1]; ptr += 4;  // v1
    *(uint32_t*)ptr = cnt[2]; ptr += 4;  // v2
    *(uint32_t*)ptr = cnt[3]; ptr += 4;  // v3
    *(uint32_t*)ptr = crc32(in, len); ptr += 4;
    
    // Сжимаем bits0 адаптивным RC
    {
        uint16_t prob = 2048;
        rc_init_enc(&rc, ptr + 4);
        for (size_t i = 0; i < len; i++) {
            rc_enc_bit(&rc, &prob, bits0[i]);
        }
        size_t sz = rc_enc_finish(&rc);
        *(uint32_t*)ptr = sz; ptr += 4 + sz;
        printf("bits0: %zu байт\n", sz);
    }
    
    // Сжимаем bits1 
    {
        uint16_t prob = 2048;
        rc_init_enc(&rc, ptr + 4);
        for (size_t i = 0; i < non_zero; i++) {
            rc_enc_bit(&rc, &prob, bits1[i]);
        }
        size_t sz = rc_enc_finish(&rc);
        *(uint32_t*)ptr = sz; ptr += 4 + sz;
        printf("bits1: %zu байт\n", sz);
    }
    
    // Сжимаем bits2
    {
        uint16_t prob = 2048;
        rc_init_enc(&rc, ptr + 4);
        for (size_t i = 0; i < lvl2_cnt; i++) {
            rc_enc_bit(&rc, &prob, bits2[i]);
        }
        size_t sz = rc_enc_finish(&rc);
        *(uint32_t*)ptr = sz; ptr += 4 + sz;
        printf("bits2: %zu байт\n", sz);
    }
    
    // Сжимаем vals1 (0-2, 2 бита) с деревом
    {
        uint16_t probs[4] = {2048, 2048, 2048, 2048};
        rc_init_enc(&rc, ptr + 4);
        for (size_t i = 0; i < cnt[1]; i++) {
            uint8_t v = vals1[i];  // 0, 1 или 2
            int b1 = (v >> 1) & 1;
            int b0 = v & 1;
            rc_enc_bit(&rc, &probs[0], b1);
            rc_enc_bit(&rc, &probs[1 + b1], b0);
        }
        size_t sz = rc_enc_finish(&rc);
        *(uint32_t*)ptr = sz; ptr += 4 + sz;
        printf("vals1: %zu байт\n", sz);
    }
    
    // Сжимаем vals2 (0-11, 4 бита)
    {
        uint16_t probs[16] = {2048,2048,2048,2048,2048,2048,2048,2048,
                              2048,2048,2048,2048,2048,2048,2048,2048};
        rc_init_enc(&rc, ptr + 4);
        for (size_t i = 0; i < cnt[2]; i++) {
            uint8_t v = vals2[i];
            int idx = 1;
            for (int b = 3; b >= 0; b--) {
                int bit = (v >> b) & 1;
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
        for (size_t i = 0; i < cnt[3]; i++) {
            rc_enc_byte_o1(&rc, probs, ctx, vals3[i]);
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

static size_t decompress_ultra(const uint8_t *in, size_t in_len, uint8_t *out) {
    const uint8_t *ptr = in;
    
    if (ptr[0] != 'K' || ptr[1] != 'U' || ptr[2] != 'L' || ptr[3] != '2') {
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
    
    size_t non_zero = cnt1 + cnt2 + cnt3;
    size_t lvl2_cnt = cnt2 + cnt3;
    
    uint8_t *bits0 = malloc(len);
    uint8_t *bits1 = malloc(non_zero);
    uint8_t *bits2 = malloc(lvl2_cnt);
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
    {
        uint32_t sz = *(uint32_t*)ptr; ptr += 4;
        uint16_t prob = 2048;
        rc_init_dec(&rc, (uint8_t*)ptr);
        for (size_t i = 0; i < non_zero; i++) {
            bits1[i] = rc_dec_bit(&rc, &prob);
        }
        ptr += sz;
    }
    
    // Декодируем bits2
    {
        uint32_t sz = *(uint32_t*)ptr; ptr += 4;
        uint16_t prob = 2048;
        rc_init_dec(&rc, (uint8_t*)ptr);
        for (size_t i = 0; i < lvl2_cnt; i++) {
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
            uint8_t v = 0;
            for (int b = 3; b >= 0; b--) {
                int bit = rc_dec_bit(&rc, &probs[idx]);
                v = (v << 1) | bit;
                idx = idx * 2 + bit;
            }
            vals2[i] = v;
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
            vals3[i] = rc_dec_byte_o1(&rc, probs, ctx);
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
                mtf[i] = vals1[i1++] + 1;  // 0-2 -> 1-3
                b1++;
            } else {
                b1++;
                if (bits2[b2] == 0) {
                    mtf[i] = vals2[i2++] + 4;  // 0-11 -> 4-15
                } else {
                    mtf[i] = vals3[i3++] + 16;  // 0-239 -> 16-255
                }
                b2++;
            }
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
    
    return stored_crc == calc_crc ? len : 0;
}

int main(int argc, char **argv) {
    crc32_init();
    
    if (argc < 4) {
        printf("Kolibri Ultra v30\n");
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
        out_len = compress_ultra(in, in_len, out);
        printf("\n=== РЕЗУЛЬТАТ ===\n");
        printf("Вход: %zu байт\n", in_len);
        printf("Выход: %zu байт\n", out_len);
        printf("Степень сжатия: %.2fx\n", (double)in_len / out_len);
    } else {
        out_len = decompress_ultra(in, in_len, out);
        printf("Распаковано: %zu байт\n", out_len);
    }
    
    f = fopen(argv[3], "wb");
    fwrite(out, 1, out_len, f);
    fclose(f);
    
    free(in);
    free(out);
    return 0;
}
