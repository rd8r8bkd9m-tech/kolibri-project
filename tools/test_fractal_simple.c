/* Простой тест фрактального сжатия на маленьких данных */
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

int main(void) {
    crc32_init();
    
    // Тест 1: BWT+MTF encode/decode
    const char *test = "banana";
    size_t len = strlen(test);
    uint8_t *bwt = malloc(len);
    uint8_t *dec = malloc(len);
    
    size_t primary = bwt_encode((uint8_t*)test, len, bwt);
    printf("BWT encode: '");
    for(size_t i=0;i<len;i++) printf("%c", bwt[i]);
    printf("' primary=%zu\n", primary);
    
    // MTF encode
    mtf_encode(bwt, len);
    printf("MTF encode: ");
    for(size_t i=0;i<len;i++) printf("%d ", bwt[i]);
    printf("\n");
    
    // MTF decode
    mtf_decode(bwt, len);
    printf("MTF decode: '");
    for(size_t i=0;i<len;i++) printf("%c", bwt[i]);
    printf("'\n");
    
    // BWT decode
    bwt_decode(bwt, len, primary, dec);
    printf("BWT decode: '");
    for(size_t i=0;i<len;i++) printf("%c", dec[i]);
    printf("'\n");
    
    if (memcmp(test, dec, len) == 0) {
        printf("OK: BWT+MTF roundtrip works!\n");
    } else {
        printf("FAIL: mismatch\n");
    }
    
    free(bwt);
    free(dec);
    
    // Тест 2: Фрактальное разделение
    printf("\n=== Тест фрактального разделения ===\n");
    uint8_t mtf_data[] = {0, 0, 1, 0, 2, 5, 0, 20, 3, 0};
    size_t n = sizeof(mtf_data);
    
    // Разделяем
    size_t cnt[4] = {0};
    for (size_t i = 0; i < n; i++) {
        uint8_t v = mtf_data[i];
        if (v == 0) cnt[0]++;
        else if (v <= 3) cnt[1]++;
        else if (v <= 15) cnt[2]++;
        else cnt[3]++;
    }
    printf("Counts: L0=%zu L1=%zu L2=%zu L3=%zu\n", cnt[0], cnt[1], cnt[2], cnt[3]);
    
    uint8_t *bits0 = malloc(n);
    uint8_t *bits1 = malloc(cnt[1]+cnt[2]+cnt[3]);
    uint8_t *bits2 = malloc(cnt[2]+cnt[3]);
    uint8_t *vals1 = malloc(cnt[1]);
    uint8_t *vals2 = malloc(cnt[2]);
    uint8_t *vals3 = malloc(cnt[3]);
    
    size_t b0=0, b1=0, b2=0, v1=0, v2=0, v3=0;
    for (size_t i = 0; i < n; i++) {
        uint8_t v = mtf_data[i];
        if (v == 0) {
            bits0[b0++] = 0;
        } else {
            bits0[b0++] = 1;
            if (v <= 3) {
                bits1[b1++] = 0;
                vals1[v1++] = v - 1;
            } else {
                bits1[b1++] = 1;
                if (v <= 15) {
                    bits2[b2++] = 0;
                    vals2[v2++] = v - 4;
                } else {
                    bits2[b2++] = 1;
                    vals3[v3++] = v - 16;
                }
            }
        }
    }
    
    printf("bits0: ");
    for(size_t i=0;i<n;i++) printf("%d", bits0[i]);
    printf("\nbits1: ");
    for(size_t i=0;i<b1;i++) printf("%d", bits1[i]);
    printf("\nbits2: ");
    for(size_t i=0;i<b2;i++) printf("%d", bits2[i]);
    printf("\nvals1: ");
    for(size_t i=0;i<v1;i++) printf("%d ", vals1[i]);
    printf("\nvals2: ");
    for(size_t i=0;i<v2;i++) printf("%d ", vals2[i]);
    printf("\nvals3: ");
    for(size_t i=0;i<v3;i++) printf("%d ", vals3[i]);
    printf("\n");
    
    // Восстановление
    printf("\nВосстановление:\n");
    uint8_t *restored = malloc(n);
    size_t i1=0, i2=0, i3=0;
    b1=0; b2=0;
    for (size_t i = 0; i < n; i++) {
        if (bits0[i] == 0) {
            restored[i] = 0;
        } else {
            if (bits1[b1] == 0) {
                restored[i] = vals1[i1++] + 1;
                b1++;
            } else {
                b1++;
                if (bits2[b2] == 0) {
                    restored[i] = vals2[i2++] + 4;
                } else {
                    restored[i] = vals3[i3++] + 16;
                }
                b2++;
            }
        }
    }
    
    printf("Restored: ");
    for(size_t i=0;i<n;i++) printf("%d ", restored[i]);
    printf("\nOriginal: ");
    for(size_t i=0;i<n;i++) printf("%d ", mtf_data[i]);
    printf("\n");
    
    if (memcmp(mtf_data, restored, n) == 0) {
        printf("OK: Fractal split/restore works!\n");
    } else {
        printf("FAIL: mismatch\n");
    }
    
    free(bits0); free(bits1); free(bits2);
    free(vals1); free(vals2); free(vals3);
    free(restored);
    
    return 0;
}
