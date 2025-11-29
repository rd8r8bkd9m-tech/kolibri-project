#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

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

/* Исправленный BWT decode */
static void bwt_decode(const uint8_t *L, size_t len, size_t I, uint8_t *out) {
    /* L - последний столбец BWT матрицы, I - строка с оригиналом */
    
    /* Считаем частоты символов */
    size_t C[256] = {0};
    for (size_t i = 0; i < len; i++) C[L[i]]++;
    
    /* Вычисляем начальные позиции каждого символа в отсортированном F (первый столбец) */
    size_t K[256];
    size_t sum = 0;
    for (int c = 0; c < 256; c++) {
        K[c] = sum;
        sum += C[c];
    }
    
    /* P[i] = количество символов L[i] до позиции i */
    size_t *P = malloc(len * sizeof(size_t));
    size_t cnt[256] = {0};
    for (size_t i = 0; i < len; i++) {
        P[i] = cnt[L[i]]++;
    }
    
    /* Восстанавливаем строку с конца */
    size_t j = I;
    for (size_t i = len; i > 0; i--) {
        out[i - 1] = L[j];
        j = K[L[j]] + P[j];
    }
    
    free(P);
}

int main() {
    /* Тест */
    const char *test = "banana";
    size_t len = strlen(test);
    
    uint8_t *bwt_out = malloc(len);
    size_t idx = bwt_encode((const uint8_t *)test, len, bwt_out);
    
    printf("Original: %s\n", test);
    printf("BWT output: ");
    for (size_t i = 0; i < len; i++) printf("%c", bwt_out[i]);
    printf("\n");
    printf("Index: %zu\n", idx);
    
    uint8_t *restored = malloc(len + 1);
    bwt_decode(bwt_out, len, idx, restored);
    restored[len] = '\0';
    
    printf("Restored: %s\n", restored);
    printf("Match: %s\n", strcmp(test, (char *)restored) == 0 ? "YES" : "NO");
    
    free(bwt_out);
    free(restored);
    return 0;
}
