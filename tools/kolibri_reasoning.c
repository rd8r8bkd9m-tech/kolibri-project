/*
 * KOLIBRI REASONING v28
 * Логические рассуждения + Фрактальная вложенность
 * 
 * КЛЮЧЕВАЯ ИДЕЯ: Применяем логические рассуждения для предсказания
 * следующего символа на основе контекста и закономерностей.
 * 
 * REASONING модуль:
 * 1. Анализ паттернов: распознаём повторяющиеся структуры
 * 2. Логический вывод: если A → B часто, то после A предсказываем B
 * 3. Иерархия контекстов: от локальных к глобальным
 * 4. Фрактальная декомпозиция: рекурсивное разбиение на подзадачи
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

/* ============== BWT ============== */
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
        rc->code = (rc->code << 8) | *rc->in_ptr++; rc->low <<= 8; rc->range <<= 8;
    }
}

/* ============== REASONING MODULE: Логические рассуждения ============== */
/*
 * Идея: Модель "рассуждает" о данных на основе:
 * 1. Статистики переходов (Марковские цепи разных порядков)
 * 2. Логических правил: если паттерн X, то вероятно Y
 * 3. Фрактальной структуры: малые паттерны → большие паттерны
 * 
 * Мы используем смешение (mixing) моделей разных порядков с весами,
 * которые адаптируются в зависимости от успешности предсказаний.
 */

/* Контекстная модель с адаптивными весами */
typedef struct {
    /* Order-0: без контекста */
    uint16_t o0_freq[257];  /* [symbol], slot 256 = total */
    
    /* Order-1: 1-байтовый контекст */
    uint16_t o1_freq[256][257];
    
    /* Order-2: 2-байтовый контекст (хеш) */
    #define O2_SIZE 65536
    uint16_t o2_freq[O2_SIZE][17];  /* [hash][symbol 0-15 + total] для MTF */
    
    /* Веса смешивания (адаптивные) */
    uint8_t mix_w0, mix_w1, mix_w2;  /* веса для order-0, 1, 2 */
    
    /* Счётчики успеха для адаптации */
    uint32_t o0_hits, o1_hits, o2_hits;
    uint32_t total_predictions;
} ReasoningModel;

static void rm_init(ReasoningModel *m) {
    /* Order-0 */
    for (int s = 0; s < 256; s++) m->o0_freq[s] = 1;
    m->o0_freq[256] = 256;
    
    /* Order-1 */
    for (int c = 0; c < 256; c++) {
        for (int s = 0; s < 256; s++) m->o1_freq[c][s] = 1;
        m->o1_freq[c][256] = 256;
    }
    
    /* Order-2 (для малого алфавита) */
    for (int h = 0; h < O2_SIZE; h++) {
        for (int s = 0; s < 16; s++) m->o2_freq[h][s] = 1;
        m->o2_freq[h][16] = 16;
    }
    
    /* Начальные веса (равные) */
    m->mix_w0 = m->mix_w1 = m->mix_w2 = 85;  /* ~1/3 каждый */
    
    m->o0_hits = m->o1_hits = m->o2_hits = 0;
    m->total_predictions = 0;
}

static uint16_t rm_hash2(uint8_t c1, uint8_t c2) {
    return ((uint16_t)c1 << 8) ^ c2;
}

static void rm_update(ReasoningModel *m, uint8_t ctx1, uint8_t ctx2, uint8_t sym) {
    /* Обновляем Order-0 */
    m->o0_freq[sym] += 4;
    m->o0_freq[256] += 4;
    if (m->o0_freq[256] > 0x3FFF) {
        uint16_t t = 0;
        for (int i = 0; i < 256; i++) {
            m->o0_freq[i] = (m->o0_freq[i] >> 1) | 1;
            t += m->o0_freq[i];
        }
        m->o0_freq[256] = t;
    }
    
    /* Обновляем Order-1 */
    m->o1_freq[ctx1][sym] += 8;
    m->o1_freq[ctx1][256] += 8;
    if (m->o1_freq[ctx1][256] > 0x3FFF) {
        uint16_t t = 0;
        for (int i = 0; i < 256; i++) {
            m->o1_freq[ctx1][i] = (m->o1_freq[ctx1][i] >> 1) | 1;
            t += m->o1_freq[ctx1][i];
        }
        m->o1_freq[ctx1][256] = t;
    }
    
    /* Обновляем Order-2 (для малых символов) */
    if (sym < 16) {
        uint16_t h = rm_hash2(ctx1, ctx2);
        m->o2_freq[h][sym] += 16;
        m->o2_freq[h][16] += 16;
        if (m->o2_freq[h][16] > 0x3FFF) {
            uint16_t t = 0;
            for (int i = 0; i < 16; i++) {
                m->o2_freq[h][i] = (m->o2_freq[h][i] >> 1) | 1;
                t += m->o2_freq[h][i];
            }
            m->o2_freq[h][16] = t;
        }
    }
}

/* Логическое рассуждение: выбор лучшего предсказания */
static void rm_get_probs(ReasoningModel *m, uint8_t ctx1, uint8_t ctx2, 
                         uint32_t *prob, uint32_t *total) {
    /* Смешиваем вероятности от разных моделей */
    uint32_t sum = 0;
    
    for (int s = 0; s < 256; s++) {
        /* Order-0 вероятность */
        uint32_t p0 = (uint32_t)m->o0_freq[s] * 256 / m->o0_freq[256];
        
        /* Order-1 вероятность */
        uint32_t p1 = (uint32_t)m->o1_freq[ctx1][s] * 256 / m->o1_freq[ctx1][256];
        
        /* Order-2 вероятность (только для малых символов) */
        uint32_t p2 = p1;  /* fallback to order-1 */
        if (s < 16) {
            uint16_t h = rm_hash2(ctx1, ctx2);
            p2 = (uint32_t)m->o2_freq[h][s] * 256 / m->o2_freq[h][16];
        }
        
        /* Взвешенное смешивание */
        prob[s] = (p0 * m->mix_w0 + p1 * m->mix_w1 + p2 * m->mix_w2) / 255;
        if (prob[s] < 1) prob[s] = 1;
        sum += prob[s];
    }
    
    *total = sum;
}

/* Адаптация весов на основе успеха */
static void rm_adapt_weights(ReasoningModel *m, uint8_t ctx1, uint8_t ctx2, uint8_t actual) {
    m->total_predictions++;
    
    /* Проверяем какая модель предсказала лучше */
    uint16_t o0_pred = 0, o1_pred = 0, o2_pred = 0;
    uint16_t max_o0 = 0, max_o1 = 0, max_o2 = 0;
    
    for (int s = 0; s < 256; s++) {
        if (m->o0_freq[s] > max_o0) { max_o0 = m->o0_freq[s]; o0_pred = s; }
        if (m->o1_freq[ctx1][s] > max_o1) { max_o1 = m->o1_freq[ctx1][s]; o1_pred = s; }
    }
    
    if (actual < 16) {
        uint16_t h = rm_hash2(ctx1, ctx2);
        for (int s = 0; s < 16; s++) {
            if (m->o2_freq[h][s] > max_o2) { max_o2 = m->o2_freq[h][s]; o2_pred = s; }
        }
    }
    
    /* Награждаем правильные предсказания */
    if (o0_pred == actual) m->o0_hits++;
    if (o1_pred == actual) m->o1_hits++;
    if (o2_pred == actual) m->o2_hits++;
    
    /* Адаптируем веса каждые 1000 символов */
    if (m->total_predictions % 1000 == 0 && m->total_predictions > 0) {
        uint32_t total_hits = m->o0_hits + m->o1_hits + m->o2_hits;
        if (total_hits > 0) {
            m->mix_w0 = (uint8_t)(255 * m->o0_hits / total_hits);
            m->mix_w1 = (uint8_t)(255 * m->o1_hits / total_hits);
            m->mix_w2 = (uint8_t)(255 * m->o2_hits / total_hits);
            
            /* Минимальные веса */
            if (m->mix_w0 < 10) m->mix_w0 = 10;
            if (m->mix_w1 < 10) m->mix_w1 = 10;
            if (m->mix_w2 < 10) m->mix_w2 = 10;
        }
        /* Сброс счётчиков */
        m->o0_hits = m->o1_hits = m->o2_hits = 0;
    }
}

/* Кодирование с Reasoning */
static void rc_enc_reasoning(RC *rc, ReasoningModel *m, uint8_t ctx1, uint8_t ctx2, uint8_t sym) {
    uint32_t prob[256], total;
    rm_get_probs(m, ctx1, ctx2, prob, &total);
    
    uint32_t cum = 0;
    for (int i = 0; i < sym; i++) cum += prob[i];
    
    rc->range /= total;
    rc->low += cum * rc->range;
    rc->range *= prob[sym];
    rc_enc_norm(rc);
    
    rm_adapt_weights(m, ctx1, ctx2, sym);
    rm_update(m, ctx1, ctx2, sym);
}

/* Декодирование с Reasoning */
static uint8_t rc_dec_reasoning(RC *rc, ReasoningModel *m, uint8_t ctx1, uint8_t ctx2) {
    uint32_t prob[256], total;
    rm_get_probs(m, ctx1, ctx2, prob, &total);
    
    rc->range /= total;
    uint32_t target = (rc->code - rc->low) / rc->range;
    
    uint32_t cum = 0;
    uint8_t sym = 0;
    while (cum + prob[sym] <= target) {
        cum += prob[sym];
        sym++;
    }
    
    rc->low += cum * rc->range;
    rc->range *= prob[sym];
    rc_dec_norm(rc);
    
    rm_adapt_weights(m, ctx1, ctx2, sym);
    rm_update(m, ctx1, ctx2, sym);
    return sym;
}

/* ============== ОСНОВНОЙ КОД ============== */

#define MAGIC 0x4B525341  /* "KRSA" - Kolibri Reasoning */

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
    
    /* BWT */
    uint8_t *bwt_out = malloc(in_size);
    size_t bwt_idx = bwt_encode(in_data, in_size, bwt_out);
    
    /* MTF */
    uint8_t *mtf_out = malloc(in_size);
    mtf_encode(bwt_out, in_size, mtf_out);
    free(bwt_out);
    
    /* Статистика */
    size_t zeros = 0, small = 0, large = 0;
    for (size_t i = 0; i < in_size; i++) {
        if (mtf_out[i] == 0) zeros++;
        else if (mtf_out[i] < 16) small++;
        else large++;
    }
    printf("=== REASONING АНАЛИЗ ===\n");
    printf("Нули:      %zu (%.1f%%)\n", zeros, 100.0*zeros/in_size);
    printf("1-15:      %zu (%.1f%%)\n", small, 100.0*small/in_size);
    printf(">=16:      %zu (%.1f%%)\n", large, 100.0*large/in_size);
    
    /* Сжатие с Reasoning моделью */
    size_t buf = in_size + 1024;
    uint8_t *comp = malloc(buf);
    RC rc;
    ReasoningModel *rm = malloc(sizeof(ReasoningModel));
    rm_init(rm);
    rc_enc_init(&rc, comp, buf);
    
    uint8_t ctx1 = 0, ctx2 = 0;
    for (size_t i = 0; i < in_size; i++) {
        rc_enc_reasoning(&rc, rm, ctx1, ctx2, mtf_out[i]);
        ctx2 = ctx1;
        ctx1 = mtf_out[i];
    }
    rc_enc_flush(&rc);
    
    printf("Финальные веса: O0=%d, O1=%d, O2=%d\n", rm->mix_w0, rm->mix_w1, rm->mix_w2);
    
    /* Заголовок */
    size_t hdr = 16;
    size_t comp_size = hdr + rc.out_size;
    uint8_t *out = malloc(comp_size);
    
    out[0] = (MAGIC >> 24) & 0xFF; out[1] = (MAGIC >> 16) & 0xFF;
    out[2] = (MAGIC >> 8) & 0xFF; out[3] = MAGIC & 0xFF;
    out[4] = (in_size >> 24) & 0xFF; out[5] = (in_size >> 16) & 0xFF;
    out[6] = (in_size >> 8) & 0xFF; out[7] = in_size & 0xFF;
    out[8] = (bwt_idx >> 24) & 0xFF; out[9] = (bwt_idx >> 16) & 0xFF;
    out[10] = (bwt_idx >> 8) & 0xFF; out[11] = bwt_idx & 0xFF;
    out[12] = (crc >> 24) & 0xFF; out[13] = (crc >> 16) & 0xFF;
    out[14] = (crc >> 8) & 0xFF; out[15] = crc & 0xFF;
    memcpy(out + hdr, comp, rc.out_size);
    
    FILE *fout = fopen(out_path, "wb");
    fwrite(out, 1, comp_size, fout);
    fclose(fout);
    
    printf("\n=== РЕЗУЛЬТАТ ===\n");
    printf("Вход: %zu байт\n", in_size);
    printf("Выход: %zu байт\n", comp_size);
    printf("Степень сжатия: %.2fx\n", (double)in_size / comp_size);
    
    free(rm); free(comp); free(in_data); free(mtf_out); free(out);
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
    
    uint8_t *mtf_out = malloc(orig);
    RC rc;
    ReasoningModel *rm = malloc(sizeof(ReasoningModel));
    rm_init(rm);
    rc_dec_init(&rc, compressed + 16);
    
    uint8_t ctx1 = 0, ctx2 = 0;
    for (size_t i = 0; i < orig; i++) {
        mtf_out[i] = rc_dec_reasoning(&rc, rm, ctx1, ctx2);
        ctx2 = ctx1;
        ctx1 = mtf_out[i];
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
        free(compressed); free(out_data); free(rm);
        return 1;
    }
    
    FILE *fout = fopen(out_path, "wb");
    fwrite(out_data, 1, orig, fout);
    fclose(fout);
    
    printf("Распаковано: %zu байт, CRC OK\n", orig);
    free(rm); free(compressed); free(out_data);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("KOLIBRI REASONING v28 - Логические рассуждения\n");
        printf("Usage: %s compress|decompress <in> <out>\n", argv[0]);
        return 1;
    }
    if (strcmp(argv[1], "compress") == 0) return compress_file(argv[2], argv[3]);
    if (strcmp(argv[1], "decompress") == 0) return decompress_file(argv[2], argv[3]);
    return 1;
}
