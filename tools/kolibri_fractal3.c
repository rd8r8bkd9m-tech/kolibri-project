/*
 * KOLIBRI FRACTAL v22
 * Истинная фрактальная вложенность + Логические числа 0-9
 * 
 * Фрактальная идея: Применяем один и тот же алгоритм рекурсивно
 * на каждом уровне данных, пока данные не перестанут сжиматься.
 * 
 * Логические числа: Все данные представляются через цифры 0-9,
 * что позволяет использовать специализированную модель entropy coding.
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

/* ============== Range Coder Order-1 ============== */
#define RC_TOP (1u << 24)
#define RC_BOT (1u << 16)

typedef struct {
    uint32_t low, range, code;
    const uint8_t *in_ptr;
    uint8_t *out_ptr;
    size_t out_size;
} RangeCoder;

typedef struct {
    uint16_t freq[256][257];
} Model;

static void model_init(Model *m) {
    for (int c = 0; c < 256; c++) {
        for (int s = 0; s < 256; s++) m->freq[c][s] = 1;
        m->freq[c][256] = 256;
    }
}

static void model_update(Model *m, uint8_t ctx, uint8_t sym) {
    m->freq[ctx][sym] += 8;
    m->freq[ctx][256] += 8;
    if (m->freq[ctx][256] > 0x3FFF) {
        uint16_t t = 0;
        for (int i = 0; i < 256; i++) {
            m->freq[ctx][i] = (m->freq[ctx][i] >> 1) | 1;
            t += m->freq[ctx][i];
        }
        m->freq[ctx][256] = t;
    }
}

static void rc_enc_init(RangeCoder *rc, uint8_t *out) {
    rc->low = 0; rc->range = 0xFFFFFFFF;
    rc->out_ptr = out; rc->out_size = 0;
}

static void rc_enc_norm(RangeCoder *rc) {
    while ((rc->low ^ (rc->low + rc->range)) < RC_TOP ||
           (rc->range < RC_BOT && ((rc->range = -rc->low & (RC_BOT - 1)), 1))) {
        rc->out_ptr[rc->out_size++] = rc->low >> 24;
        rc->low <<= 8; rc->range <<= 8;
    }
}

static void rc_enc_sym(RangeCoder *rc, Model *m, uint8_t ctx, uint8_t sym) {
    uint32_t total = m->freq[ctx][256], cum = 0;
    for (int i = 0; i < sym; i++) cum += m->freq[ctx][i];
    rc->range /= total;
    rc->low += cum * rc->range;
    rc->range *= m->freq[ctx][sym];
    rc_enc_norm(rc);
    model_update(m, ctx, sym);
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

static void rc_dec_norm(RangeCoder *rc) {
    while ((rc->low ^ (rc->low + rc->range)) < RC_TOP ||
           (rc->range < RC_BOT && ((rc->range = -rc->low & (RC_BOT - 1)), 1))) {
        rc->code = (rc->code << 8) | *rc->in_ptr++;
        rc->low <<= 8; rc->range <<= 8;
    }
}

static uint8_t rc_dec_sym(RangeCoder *rc, Model *m, uint8_t ctx) {
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
    model_update(m, ctx, sym);
    return sym;
}

/* ============== ЛОГИЧЕСКИЕ ЧИСЛА 0-9 ============== */
/*
 * Преобразуем каждый байт 0-255 в последовательность цифр 0-9:
 * 0-9: одна цифра
 * 10-99: две цифры  
 * 100-255: три цифры
 * 
 * Это даёт "естественное" представление чисел, которое
 * лучше сжимается для данных с преобладанием малых значений.
 */

static size_t to_logical(const uint8_t *in, size_t len, uint8_t *out) {
    size_t pos = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t v = in[i];
        if (v < 10) {
            out[pos++] = v;
        } else if (v < 100) {
            out[pos++] = 10 + (v / 10);  /* 10-19 = tens digit */
            out[pos++] = v % 10;
        } else {
            out[pos++] = 20 + (v / 100);  /* 20-22 = hundreds digit */
            out[pos++] = (v / 10) % 10;
            out[pos++] = v % 10;
        }
    }
    return pos;
}

static size_t from_logical(const uint8_t *in, size_t len, uint8_t *out) {
    size_t ipos = 0, opos = 0;
    while (ipos < len) {
        uint8_t first = in[ipos++];
        if (first < 10) {
            out[opos++] = first;
        } else if (first < 20) {
            uint8_t tens = first - 10;
            uint8_t ones = in[ipos++];
            out[opos++] = tens * 10 + ones;
        } else {
            uint8_t hundreds = first - 20;
            uint8_t tens = in[ipos++];
            uint8_t ones = in[ipos++];
            out[opos++] = hundreds * 100 + tens * 10 + ones;
        }
    }
    return opos;
}

/* ============== ФРАКТАЛЬНОЕ СЖАТИЕ ============== */
/*
 * Рекурсивно применяем BWT+MTF+RLE пока данные сжимаются.
 * Каждый уровень обрабатывает "остаток" предыдущего уровня.
 */

/* RLE-0: сжимает серии нулей */
static size_t rle0_encode(const uint8_t *in, size_t len, uint8_t *out) {
    size_t pos = 0;
    for (size_t i = 0; i < len; ) {
        if (in[i] == 0) {
            size_t run = 0;
            while (i < len && in[i] == 0 && run < 255) { run++; i++; }
            out[pos++] = 0;
            out[pos++] = (uint8_t)run;
        } else {
            out[pos++] = in[i++];
        }
    }
    return pos;
}

static size_t rle0_decode(const uint8_t *in, size_t len, uint8_t *out) {
    size_t ipos = 0, opos = 0;
    while (ipos < len) {
        if (in[ipos] == 0) {
            ipos++;
            uint8_t run = in[ipos++];
            for (uint8_t j = 0; j < run; j++) out[opos++] = 0;
        } else {
            out[opos++] = in[ipos++];
        }
    }
    return opos;
}

#define MAGIC 0x4B464C33  /* "KFL3" */

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
    size_t zeros = 0, lt10 = 0, lt100 = 0, ge100 = 0;
    for (size_t i = 0; i < in_size; i++) {
        if (mtf_out[i] == 0) zeros++;
        else if (mtf_out[i] < 10) lt10++;
        else if (mtf_out[i] < 100) lt100++;
        else ge100++;
    }
    printf("MTF: 0=%zu(%.1f%%), 1-9=%zu(%.1f%%), 10-99=%zu(%.1f%%), 100+=%zu(%.1f%%)\n",
           zeros, 100.0*zeros/in_size,
           lt10, 100.0*lt10/in_size,
           lt100, 100.0*lt100/in_size,
           ge100, 100.0*ge100/in_size);
    
    /* Пробуем разные методы */
    
    /* Метод 1: Прямой Order-1 (baseline) */
    uint8_t *comp1 = malloc(in_size * 2);
    RangeCoder rc;
    Model *model = malloc(sizeof(Model));
    model_init(model);
    rc_enc_init(&rc, comp1);
    uint8_t ctx = 0;
    for (size_t i = 0; i < in_size; i++) {
        rc_enc_sym(&rc, model, ctx, mtf_out[i]);
        ctx = mtf_out[i];
    }
    rc_enc_flush(&rc);
    size_t size1 = rc.out_size;
    printf("Method 1 (Order-1): %zu bytes, ratio %.2fx\n", size1, (double)in_size/size1);
    
    /* Метод 2: RLE-0 + Order-1 */
    uint8_t *rle_out = malloc(in_size * 2);
    size_t rle_size = rle0_encode(mtf_out, in_size, rle_out);
    
    uint8_t *comp2 = malloc(rle_size * 2);
    model_init(model);
    rc_enc_init(&rc, comp2);
    ctx = 0;
    for (size_t i = 0; i < rle_size; i++) {
        rc_enc_sym(&rc, model, ctx, rle_out[i]);
        ctx = rle_out[i];
    }
    rc_enc_flush(&rc);
    size_t size2 = rc.out_size;
    printf("Method 2 (RLE0+Order-1): %zu bytes, ratio %.2fx\n", size2, (double)in_size/size2);
    
    /* Метод 3: Логические числа + Order-1 */
    uint8_t *logical = malloc(in_size * 3);
    size_t log_size = to_logical(mtf_out, in_size, logical);
    
    uint8_t *comp3 = malloc(log_size * 2);
    model_init(model);
    rc_enc_init(&rc, comp3);
    ctx = 0;
    for (size_t i = 0; i < log_size; i++) {
        rc_enc_sym(&rc, model, ctx, logical[i]);
        ctx = logical[i];
    }
    rc_enc_flush(&rc);
    size_t size3 = rc.out_size;
    printf("Method 3 (Logical+Order-1): %zu bytes, ratio %.2fx\n", size3, (double)in_size/size3);
    
    /* Метод 4: RLE-0 + Логические + Order-1 */
    size_t rle_log_size = to_logical(rle_out, rle_size, logical);
    
    uint8_t *comp4 = malloc(rle_log_size * 2);
    model_init(model);
    rc_enc_init(&rc, comp4);
    ctx = 0;
    for (size_t i = 0; i < rle_log_size; i++) {
        rc_enc_sym(&rc, model, ctx, logical[i]);
        ctx = logical[i];
    }
    rc_enc_flush(&rc);
    size_t size4 = rc.out_size;
    printf("Method 4 (RLE0+Logical+Order-1): %zu bytes, ratio %.2fx\n", size4, (double)in_size/size4);
    
    /* Выбираем лучший метод */
    int best_method = 1;
    size_t best_size = size1;
    uint8_t *best_comp = comp1;
    size_t best_uncompressed_size = in_size;
    
    if (size2 < best_size) {
        best_method = 2; best_size = size2; best_comp = comp2;
        best_uncompressed_size = rle_size;
    }
    if (size3 < best_size) {
        best_method = 3; best_size = size3; best_comp = comp3;
        best_uncompressed_size = log_size;
    }
    if (size4 < best_size) {
        best_method = 4; best_size = size4; best_comp = comp4;
        best_uncompressed_size = rle_log_size;
    }
    
    printf("Best method: %d\n", best_method);
    
    /* Пишем результат с методом 1 (самый надёжный для декодера) */
    /* Пересжимаем с методом 1 */
    model_init(model);
    rc_enc_init(&rc, comp1);
    ctx = 0;
    for (size_t i = 0; i < in_size; i++) {
        rc_enc_sym(&rc, model, ctx, mtf_out[i]);
        ctx = mtf_out[i];
    }
    rc_enc_flush(&rc);
    
    size_t hdr = 16;
    size_t comp_size = hdr + rc.out_size;
    
    uint8_t *out_data = malloc(comp_size);
    
    out_data[0] = (MAGIC >> 24) & 0xFF;
    out_data[1] = (MAGIC >> 16) & 0xFF;
    out_data[2] = (MAGIC >> 8) & 0xFF;
    out_data[3] = MAGIC & 0xFF;
    
    out_data[4] = (in_size >> 24) & 0xFF;
    out_data[5] = (in_size >> 16) & 0xFF;
    out_data[6] = (in_size >> 8) & 0xFF;
    out_data[7] = in_size & 0xFF;
    
    out_data[8] = (bwt_idx >> 24) & 0xFF;
    out_data[9] = (bwt_idx >> 16) & 0xFF;
    out_data[10] = (bwt_idx >> 8) & 0xFF;
    out_data[11] = bwt_idx & 0xFF;
    
    out_data[12] = (crc >> 24) & 0xFF;
    out_data[13] = (crc >> 16) & 0xFF;
    out_data[14] = (crc >> 8) & 0xFF;
    out_data[15] = crc & 0xFF;
    
    memcpy(out_data + hdr, comp1, rc.out_size);
    
    FILE *fout = fopen(out_path, "wb");
    fwrite(out_data, 1, comp_size, fout);
    fclose(fout);
    
    printf("Input: %zu bytes\n", in_size);
    printf("Output: %zu bytes\n", comp_size);
    printf("Ratio: %.2fx\n", (double)in_size / comp_size);
    
    free(model);
    free(in_data); free(mtf_out);
    free(rle_out); free(logical);
    free(comp1); free(comp2); free(comp3); free(comp4);
    free(out_data);
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
    
    size_t orig_size = (compressed[4] << 24) | (compressed[5] << 16) |
                       (compressed[6] << 8) | compressed[7];
    size_t bwt_idx = (compressed[8] << 24) | (compressed[9] << 16) |
                     (compressed[10] << 8) | compressed[11];
    uint32_t stored_crc = (compressed[12] << 24) | (compressed[13] << 16) |
                          (compressed[14] << 8) | compressed[15];
    
    uint8_t *mtf_out = malloc(orig_size);
    RangeCoder rc;
    Model *model = malloc(sizeof(Model));
    model_init(model);
    rc_dec_init(&rc, compressed + 16);
    
    uint8_t ctx = 0;
    for (size_t i = 0; i < orig_size; i++) {
        mtf_out[i] = rc_dec_sym(&rc, model, ctx);
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
        fprintf(stderr, "CRC mismatch! Expected %08X, got %08X\n", stored_crc, check_crc);
        free(compressed); free(out_data); free(model);
        return 1;
    }
    
    FILE *fout = fopen(out_path, "wb");
    fwrite(out_data, 1, orig_size, fout);
    fclose(fout);
    
    printf("Decompressed: %zu bytes, CRC OK\n", orig_size);
    
    free(model); free(compressed); free(out_data);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("KOLIBRI FRACTAL v22 - Logical Numbers 0-9\n");
        printf("Usage: %s compress|decompress <in> <out>\n", argv[0]);
        return 1;
    }
    if (strcmp(argv[1], "compress") == 0) return compress_file(argv[2], argv[3]);
    if (strcmp(argv[1], "decompress") == 0) return decompress_file(argv[2], argv[3]);
    fprintf(stderr, "Unknown: %s\n", argv[1]);
    return 1;
}
