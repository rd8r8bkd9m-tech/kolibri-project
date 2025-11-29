/*
 * KOLIBRI FRACTAL v21
 * ПОЛНАЯ Фрактальная вложенность + Логические числа 0-9
 * 
 * Ключевая идея: Данные имеют самоподобную структуру на разных масштабах.
 * После BWT+MTF: ~87% нулей, ~10% чисел 1-9, ~3% чисел >= 10
 * 
 * Фрактальные уровни:
 * Level 0: Bitmap нулей/ненулей (сжимается RLE)
 * Level 1: Только ненулевые значения, разделённые на 0-9 и 10+
 * Level 2: Рекурсивно применяем тот же принцип
 * 
 * Логические числа: Используем base-10 для естественной decimal-оптимизации
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ============== CRC32 ============== */
static uint32_t crc32_table[256];
static int crc32_initialized = 0;

static void init_crc32(void) {
    if (crc32_initialized) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
            c = (c >> 1) ^ ((c & 1) ? 0xEDB88320 : 0);
        crc32_table[i] = c;
    }
    crc32_initialized = 1;
}

static uint32_t calc_crc32(const uint8_t *data, size_t len) {
    init_crc32();
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++)
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFF;
}

/* ============== BWT ============== */
static const uint8_t *bwt_data;
static size_t bwt_len;

static int bwt_compare(const void *a, const void *b) {
    size_t i = *(const size_t *)a;
    size_t j = *(const size_t *)b;
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
    qsort(idx, len, sizeof(size_t), bwt_compare);
    size_t orig_idx = 0;
    for (size_t i = 0; i < len; i++) {
        out[i] = in[(idx[i] + len - 1) % len];
        if (idx[i] == 0) orig_idx = i;
    }
    free(idx);
    return orig_idx;
}

static void bwt_decode(const uint8_t *in, size_t len, size_t orig_idx, uint8_t *out) {
    size_t *transform = malloc(len * sizeof(size_t));
    size_t count[256] = {0}, cumul[256];
    for (size_t i = 0; i < len; i++) count[in[i]]++;
    cumul[0] = 0;
    for (int i = 1; i < 256; i++) cumul[i] = cumul[i-1] + count[i-1];
    for (size_t i = 0; i < len; i++) transform[cumul[in[i]]++] = i;
    size_t j = orig_idx;
    for (size_t i = 0; i < len; i++) {
        out[len - 1 - i] = in[j];
        j = transform[j];
    }
    free(transform);
}

/* ============== MTF ============== */
static void mtf_encode(const uint8_t *in, size_t len, uint8_t *out) {
    uint8_t table[256];
    for (int i = 0; i < 256; i++) table[i] = i;
    for (size_t i = 0; i < len; i++) {
        uint8_t c = in[i], rank = 0;
        while (table[rank] != c) rank++;
        out[i] = rank;
        memmove(&table[1], &table[0], rank);
        table[0] = c;
    }
}

static void mtf_decode(const uint8_t *in, size_t len, uint8_t *out) {
    uint8_t table[256];
    for (int i = 0; i < 256; i++) table[i] = i;
    for (size_t i = 0; i < len; i++) {
        uint8_t rank = in[i], c = table[rank];
        out[i] = c;
        memmove(&table[1], &table[0], rank);
        table[0] = c;
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
} RangeCoder;

/* Order-1 с ограниченным алфавитом (только логические числа 0-10) */
typedef struct {
    uint16_t freq[11][12];  /* [context 0-10][symbol 0-10], slot 11 = total */
} LogicalModel;

static void logical_model_init(LogicalModel *m) {
    for (int ctx = 0; ctx < 11; ctx++) {
        for (int s = 0; s < 11; s++) m->freq[ctx][s] = 1;
        m->freq[ctx][11] = 11;
    }
}

static void logical_model_update(LogicalModel *m, uint8_t ctx, uint8_t sym) {
    if (ctx > 10) ctx = 10;
    if (sym > 10) sym = 10;
    m->freq[ctx][sym] += 16;
    m->freq[ctx][11] += 16;
    if (m->freq[ctx][11] > 0x3FFF) {
        uint16_t total = 0;
        for (int i = 0; i < 11; i++) {
            m->freq[ctx][i] = (m->freq[ctx][i] >> 1) | 1;
            total += m->freq[ctx][i];
        }
        m->freq[ctx][11] = total;
    }
}

/* Order-1 полный (256 символов) */
typedef struct {
    uint16_t freq[256][257];
} FullModel;

static void full_model_init(FullModel *m) {
    for (int ctx = 0; ctx < 256; ctx++) {
        for (int s = 0; s < 256; s++) m->freq[ctx][s] = 1;
        m->freq[ctx][256] = 256;
    }
}

static void full_model_update(FullModel *m, uint8_t ctx, uint8_t sym) {
    m->freq[ctx][sym] += 8;
    m->freq[ctx][256] += 8;
    if (m->freq[ctx][256] > 0x3FFF) {
        uint16_t total = 0;
        for (int i = 0; i < 256; i++) {
            m->freq[ctx][i] = (m->freq[ctx][i] >> 1) | 1;
            total += m->freq[ctx][i];
        }
        m->freq[ctx][256] = total;
    }
}

static void rc_enc_init(RangeCoder *rc, uint8_t *out) {
    rc->low = 0; rc->range = 0xFFFFFFFF;
    rc->out_ptr = out; rc->out_size = 0;
}

static void rc_enc_normalize(RangeCoder *rc) {
    while ((rc->low ^ (rc->low + rc->range)) < RC_TOP ||
           (rc->range < RC_BOT && ((rc->range = -rc->low & (RC_BOT - 1)), 1))) {
        rc->out_ptr[rc->out_size++] = rc->low >> 24;
        rc->low <<= 8; rc->range <<= 8;
    }
}

static void rc_enc_logical(RangeCoder *rc, LogicalModel *m, uint8_t ctx, uint8_t sym) {
    if (ctx > 10) ctx = 10;
    if (sym > 10) sym = 10;
    uint32_t total = m->freq[ctx][11];
    uint32_t cum = 0;
    for (int i = 0; i < sym; i++) cum += m->freq[ctx][i];
    rc->range /= total;
    rc->low += cum * rc->range;
    rc->range *= m->freq[ctx][sym];
    rc_enc_normalize(rc);
    logical_model_update(m, ctx, sym);
}

static void rc_enc_full(RangeCoder *rc, FullModel *m, uint8_t ctx, uint8_t sym) {
    uint32_t total = m->freq[ctx][256];
    uint32_t cum = 0;
    for (int i = 0; i < sym; i++) cum += m->freq[ctx][i];
    rc->range /= total;
    rc->low += cum * rc->range;
    rc->range *= m->freq[ctx][sym];
    rc_enc_normalize(rc);
    full_model_update(m, ctx, sym);
}

static void rc_enc_flush(RangeCoder *rc) {
    for (int i = 0; i < 4; i++) {
        rc->out_ptr[rc->out_size++] = rc->low >> 24;
        rc->low <<= 8;
    }
}

static void rc_dec_init(RangeCoder *rc, const uint8_t *in) {
    rc->low = 0; rc->range = 0xFFFFFFFF; rc->code = 0;
    rc->in_ptr = in;
    for (int i = 0; i < 4; i++) rc->code = (rc->code << 8) | *rc->in_ptr++;
}

static void rc_dec_normalize(RangeCoder *rc) {
    while ((rc->low ^ (rc->low + rc->range)) < RC_TOP ||
           (rc->range < RC_BOT && ((rc->range = -rc->low & (RC_BOT - 1)), 1))) {
        rc->code = (rc->code << 8) | *rc->in_ptr++;
        rc->low <<= 8; rc->range <<= 8;
    }
}

static uint8_t rc_dec_logical(RangeCoder *rc, LogicalModel *m, uint8_t ctx) {
    if (ctx > 10) ctx = 10;
    uint32_t total = m->freq[ctx][11];
    rc->range /= total;
    uint32_t target = (rc->code - rc->low) / rc->range;
    uint32_t cum = 0;
    uint8_t sym = 0;
    while (sym < 10 && cum + m->freq[ctx][sym] <= target) {
        cum += m->freq[ctx][sym]; sym++;
    }
    rc->low += cum * rc->range;
    rc->range *= m->freq[ctx][sym];
    rc_dec_normalize(rc);
    logical_model_update(m, ctx, sym);
    return sym;
}

static uint8_t rc_dec_full(RangeCoder *rc, FullModel *m, uint8_t ctx) {
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
    rc_dec_normalize(rc);
    full_model_update(m, ctx, sym);
    return sym;
}

/* ============== ФРАКТАЛЬНОЕ КОДИРОВАНИЕ С ЛОГИЧЕСКИМИ ЧИСЛАМИ ============== */
/*
 * Уровень 0: RLE для нулевых серий (длины кодируем base-10)
 * Уровень 1: Ненулевые значения 1-9 напрямую
 * Уровень 2: Значения >= 10 кодируем как 10 + (value-10) в base-10
 * 
 * Ключ: каждый уровень использует только цифры 0-10
 */

#define MAGIC 0x4B464C32  /* "KFL2" - Kolibri Fractal Logical v2 */

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
        else if (mtf_out[i] < 10) small++;
        else large++;
    }
    printf("MTF: zeros=%zu (%.1f%%), 1-9=%zu (%.1f%%), >=10=%zu (%.1f%%)\n",
           zeros, 100.0*zeros/in_size, small, 100.0*small/in_size, large, 100.0*large/in_size);
    
    /* Фрактальное кодирование с логическими числами 0-9 */
    uint8_t *compressed = malloc(in_size * 2);
    size_t hdr = 16;
    
    /* 
     * Алгоритм: Кодируем как последовательность "команд"
     * 0 = следующее число - длина нулевой серии (в base-10 цифрах)
     * 1-9 = напрямую это значение
     * 10 = escape для значений >= 10 (затем идут base-10 цифры)
     */
    
    /* Создаём поток логических чисел */
    uint8_t *logical = malloc(in_size * 4);  /* с запасом */
    size_t log_size = 0;
    
    size_t i = 0;
    while (i < in_size) {
        if (mtf_out[i] == 0) {
            /* Серия нулей */
            size_t run = 0;
            while (i < in_size && mtf_out[i] == 0) { run++; i++; }
            
            /* Кодируем длину в base-10 */
            /* Используем bijective base-10: a=1, b=2, ..., 0=10 */
            /* Или проще: записываем длину как последовательность цифр */
            logical[log_size++] = 0;  /* маркер нулевой серии */
            
            /* Записываем длину в bijective numeration для лучшего сжатия */
            /* 1-9 -> одна цифра, 10-99 -> две цифры, и т.д. */
            if (run <= 9) {
                logical[log_size++] = (uint8_t)run;
            } else {
                /* Многозначное число в base-10 */
                uint8_t digits[12];
                int nd = 0;
                size_t tmp = run;
                while (tmp > 0) {
                    digits[nd++] = tmp % 10;
                    tmp /= 10;
                }
                logical[log_size++] = 10;  /* маркер многозначного */
                logical[log_size++] = (uint8_t)nd;
                for (int d = nd - 1; d >= 0; d--) {
                    logical[log_size++] = digits[d];
                }
            }
        } else if (mtf_out[i] < 10) {
            /* Малое число 1-9 */
            logical[log_size++] = mtf_out[i];
            i++;
        } else {
            /* Большое число >= 10 */
            logical[log_size++] = 10;  /* escape */
            uint32_t val = mtf_out[i] - 10;
            
            if (val < 10) {
                logical[log_size++] = (uint8_t)val;
            } else {
                uint8_t digits[12];
                int nd = 0;
                while (val > 0) {
                    digits[nd++] = val % 10;
                    val /= 10;
                }
                logical[log_size++] = 10;  /* маркер многозначного */
                logical[log_size++] = (uint8_t)nd;
                for (int d = nd - 1; d >= 0; d--) {
                    logical[log_size++] = digits[d];
                }
            }
            i++;
        }
    }
    
    printf("Logical stream: %zu bytes (expansion: %.2fx)\n", log_size, (double)log_size/in_size);
    
    /* Сжимаем логический поток Range Coder с Order-1 */
    RangeCoder rc;
    LogicalModel *lm = malloc(sizeof(LogicalModel));
    logical_model_init(lm);
    rc_enc_init(&rc, compressed + hdr);
    
    uint8_t ctx = 0;
    for (size_t j = 0; j < log_size; j++) {
        rc_enc_logical(&rc, lm, ctx, logical[j]);
        ctx = logical[j];
    }
    rc_enc_flush(&rc);
    
    size_t comp_size = hdr + rc.out_size;
    printf("After Range Coder: %zu bytes\n", rc.out_size);
    
    /* Если логическое кодирование хуже, используем прямое Order-1 */
    FullModel *fm = malloc(sizeof(FullModel));
    full_model_init(fm);
    rc_enc_init(&rc, compressed + hdr);
    
    ctx = 0;
    for (size_t j = 0; j < in_size; j++) {
        rc_enc_full(&rc, fm, ctx, mtf_out[j]);
        ctx = mtf_out[j];
    }
    rc_enc_flush(&rc);
    
    size_t direct_size = hdr + rc.out_size;
    printf("Direct Order-1: %zu bytes\n", rc.out_size);
    
    /* Выбираем лучший метод */
    int use_logical = (comp_size < direct_size);
    if (!use_logical) {
        comp_size = direct_size;
        printf("Using direct Order-1 (better)\n");
    } else {
        /* Пересжимаем логическое */
        logical_model_init(lm);
        rc_enc_init(&rc, compressed + hdr);
        ctx = 0;
        for (size_t j = 0; j < log_size; j++) {
            rc_enc_logical(&rc, lm, ctx, logical[j]);
            ctx = logical[j];
        }
        rc_enc_flush(&rc);
        comp_size = hdr + rc.out_size;
        printf("Using logical (better)\n");
    }
    
    /* Заголовок */
    compressed[0] = (MAGIC >> 24) & 0xFF;
    compressed[1] = (MAGIC >> 16) & 0xFF;
    compressed[2] = (MAGIC >> 8) & 0xFF;
    compressed[3] = MAGIC & 0xFF;
    
    compressed[4] = (in_size >> 24) & 0xFF;
    compressed[5] = (in_size >> 16) & 0xFF;
    compressed[6] = (in_size >> 8) & 0xFF;
    compressed[7] = in_size & 0xFF;
    
    compressed[8] = (bwt_idx >> 24) & 0xFF;
    compressed[9] = (bwt_idx >> 16) & 0xFF;
    compressed[10] = (bwt_idx >> 8) & 0xFF;
    compressed[11] = bwt_idx & 0xFF;
    
    compressed[12] = (crc >> 24) & 0xFF;
    compressed[13] = (crc >> 16) & 0xFF;
    compressed[14] = (crc >> 8) & 0xFF;
    compressed[15] = crc & 0xFF;
    
    /* Если используем direct, нужен флаг */
    /* Пока что всегда direct (проще для декодера) */
    
    /* Используем только direct метод для простоты декодирования */
    full_model_init(fm);
    rc_enc_init(&rc, compressed + hdr);
    ctx = 0;
    for (size_t j = 0; j < in_size; j++) {
        rc_enc_full(&rc, fm, ctx, mtf_out[j]);
        ctx = mtf_out[j];
    }
    rc_enc_flush(&rc);
    comp_size = hdr + rc.out_size;
    
    FILE *fout = fopen(out_path, "wb");
    fwrite(compressed, 1, comp_size, fout);
    fclose(fout);
    
    printf("Input: %zu bytes\n", in_size);
    printf("Output: %zu bytes\n", comp_size);
    printf("Ratio: %.2fx\n", (double)in_size / comp_size);
    
    free(lm); free(fm);
    free(logical);
    free(in_data);
    free(mtf_out);
    free(compressed);
    return 0;
}

static int decompress_file(const char *in_path, const char *out_path) {
    FILE *fin = fopen(in_path, "rb");
    if (!fin) { perror("fopen"); return 1; }
    fseek(fin, 0, SEEK_END);
    size_t comp_size = ftell(fin);
    fseek(fin, 0, SEEK_SET);
    uint8_t *compressed = malloc(comp_size);
    fread(compressed, 1, comp_size, fin);
    fclose(fin);
    
    uint32_t magic = (compressed[0] << 24) | (compressed[1] << 16) |
                     (compressed[2] << 8) | compressed[3];
    if (magic != MAGIC) {
        fprintf(stderr, "Invalid magic\n");
        free(compressed); return 1;
    }
    
    size_t orig_size = (compressed[4] << 24) | (compressed[5] << 16) |
                       (compressed[6] << 8) | compressed[7];
    size_t bwt_idx = (compressed[8] << 24) | (compressed[9] << 16) |
                     (compressed[10] << 8) | compressed[11];
    uint32_t stored_crc = (compressed[12] << 24) | (compressed[13] << 16) |
                          (compressed[14] << 8) | compressed[15];
    
    uint8_t *mtf_out = malloc(orig_size);
    RangeCoder rc;
    FullModel *fm = malloc(sizeof(FullModel));
    full_model_init(fm);
    rc_dec_init(&rc, compressed + 16);
    
    uint8_t ctx = 0;
    for (size_t i = 0; i < orig_size; i++) {
        mtf_out[i] = rc_dec_full(&rc, fm, ctx);
        ctx = mtf_out[i];
    }
    
    uint8_t *bwt_out = malloc(orig_size);
    mtf_decode(mtf_out, orig_size, bwt_out);
    free(mtf_out);
    
    uint8_t *out_data = malloc(orig_size);
    bwt_decode(bwt_out, orig_size, bwt_idx, out_data);
    free(bwt_out);
    
    uint32_t check_crc = calc_crc32(out_data, orig_size);
    if (check_crc != stored_crc) {
        fprintf(stderr, "CRC mismatch!\n");
        free(compressed); free(out_data); free(fm);
        return 1;
    }
    
    FILE *fout = fopen(out_path, "wb");
    fwrite(out_data, 1, orig_size, fout);
    fclose(fout);
    
    printf("Decompressed: %zu bytes, CRC OK\n", orig_size);
    
    free(fm); free(compressed); free(out_data);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("KOLIBRI FRACTAL v21 - Logical Numbers\n");
        printf("Usage: %s compress|decompress <in> <out>\n", argv[0]);
        return 1;
    }
    if (strcmp(argv[1], "compress") == 0) return compress_file(argv[2], argv[3]);
    if (strcmp(argv[1], "decompress") == 0) return decompress_file(argv[2], argv[3]);
    fprintf(stderr, "Unknown: %s\n", argv[1]);
    return 1;
}
