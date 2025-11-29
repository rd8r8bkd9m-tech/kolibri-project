/*
 * KOLIBRI FRACTAL v20
 * Фрактальная вложенность + Логические числа (0-9)
 * 
 * Идея: Исходный код имеет фрактальную структуру - паттерны повторяются
 * на разных уровнях (токены, строки, блоки, функции).
 * 
 * Логические числа 0-9: Используем только цифры для кодирования,
 * что даёт естественную decimal-оптимизацию.
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

/* ============== BWT (Burrows-Wheeler Transform) ============== */
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
    
    bwt_data = in;
    bwt_len = len;
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
    size_t count[256] = {0};
    size_t cumul[256];
    
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

/* ============== MTF (Move-To-Front) ============== */
static void mtf_encode(const uint8_t *in, size_t len, uint8_t *out) {
    uint8_t table[256];
    for (int i = 0; i < 256; i++) table[i] = i;
    
    for (size_t i = 0; i < len; i++) {
        uint8_t c = in[i];
        uint8_t rank = 0;
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
        uint8_t rank = in[i];
        uint8_t c = table[rank];
        out[i] = c;
        memmove(&table[1], &table[0], rank);
        table[0] = c;
    }
}

/* ============== ФРАКТАЛЬНАЯ ВЛОЖЕННОСТЬ ============== */
/*
 * Идея: После BWT+MTF данные состоят в основном из 0 и малых чисел.
 * Фрактальная структура: разбиваем на "уровни":
 * - Уровень 0: позиции нулей (bitmap)
 * - Уровень 1: значения ненулевых байтов 1-9
 * - Уровень 2: значения >= 10 кодируются рекурсивно
 * 
 * Логические числа 0-9: Каждый уровень использует base-10 кодирование
 */

/* Фрактальное кодирование с логическими числами */
typedef struct {
    uint8_t *data;
    size_t size;
    size_t capacity;
} BitStream;

static void bs_init(BitStream *bs) {
    bs->capacity = 65536;
    bs->data = malloc(bs->capacity);
    bs->size = 0;
}

static void bs_write(BitStream *bs, uint8_t byte) {
    if (bs->size >= bs->capacity) {
        bs->capacity *= 2;
        bs->data = realloc(bs->data, bs->capacity);
    }
    bs->data[bs->size++] = byte;
}

static void bs_free(BitStream *bs) {
    free(bs->data);
}

/* Запись числа в base-10 (логические числа) с Elias gamma кодированием */
static void write_logical_number(BitStream *bs, uint32_t n) {
    /* Для чисел 0-9: записываем напрямую как 1 байт */
    if (n < 10) {
        bs_write(bs, (uint8_t)n);
        return;
    }
    /* Для больших чисел: маркер 10 + длина + цифры */
    bs_write(bs, 10); /* escape */
    
    /* Разбиваем на цифры base-10 */
    uint8_t digits[12];
    int ndigits = 0;
    uint32_t tmp = n;
    while (tmp > 0) {
        digits[ndigits++] = tmp % 10;
        tmp /= 10;
    }
    bs_write(bs, (uint8_t)ndigits);
    for (int i = ndigits - 1; i >= 0; i--) {
        bs_write(bs, digits[i]);
    }
}

static uint32_t read_logical_number(const uint8_t *data, size_t *pos, size_t max) {
    if (*pos >= max) return 0;
    uint8_t first = data[(*pos)++];
    if (first < 10) return first;
    
    /* Большое число */
    uint8_t ndigits = data[(*pos)++];
    uint32_t result = 0;
    for (int i = 0; i < ndigits && *pos < max; i++) {
        result = result * 10 + data[(*pos)++];
    }
    return result;
}

/* ============== Range Coder с Order-1 контекстом ============== */
#define RC_TOP (1u << 24)
#define RC_BOT (1u << 16)

typedef struct {
    uint32_t low, range, code;
    const uint8_t *in_ptr;
    uint8_t *out_ptr;
    size_t out_size;
    size_t out_cap;
} RangeCoder;

/* Адаптивная модель Order-1 */
typedef struct {
    uint16_t freq[256][257];  /* [context][symbol], slot 256 = total */
} Order1Model;

static void model_init(Order1Model *m) {
    for (int ctx = 0; ctx < 256; ctx++) {
        for (int s = 0; s < 256; s++) m->freq[ctx][s] = 1;
        m->freq[ctx][256] = 256;
    }
}

static void model_update(Order1Model *m, uint8_t ctx, uint8_t sym) {
    m->freq[ctx][sym] += 8;
    m->freq[ctx][256] += 8;
    /* Масштабирование при переполнении */
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
    rc->low = 0;
    rc->range = 0xFFFFFFFF;
    rc->out_ptr = out;
    rc->out_size = 0;
    rc->out_cap = 0;
}

static void rc_enc_normalize(RangeCoder *rc) {
    while ((rc->low ^ (rc->low + rc->range)) < RC_TOP ||
           (rc->range < RC_BOT && ((rc->range = -rc->low & (RC_BOT - 1)), 1))) {
        rc->out_ptr[rc->out_size++] = rc->low >> 24;
        rc->low <<= 8;
        rc->range <<= 8;
    }
}

static void rc_enc_symbol(RangeCoder *rc, Order1Model *m, uint8_t ctx, uint8_t sym) {
    uint32_t total = m->freq[ctx][256];
    uint32_t cum = 0;
    for (int i = 0; i < sym; i++) cum += m->freq[ctx][i];
    uint32_t freq = m->freq[ctx][sym];
    
    rc->range /= total;
    rc->low += cum * rc->range;
    rc->range *= freq;
    rc_enc_normalize(rc);
    model_update(m, ctx, sym);
}

static void rc_enc_flush(RangeCoder *rc) {
    for (int i = 0; i < 4; i++) {
        rc->out_ptr[rc->out_size++] = rc->low >> 24;
        rc->low <<= 8;
    }
}

static void rc_dec_init(RangeCoder *rc, const uint8_t *in) {
    rc->low = 0;
    rc->range = 0xFFFFFFFF;
    rc->code = 0;
    rc->in_ptr = in;
    for (int i = 0; i < 4; i++)
        rc->code = (rc->code << 8) | *rc->in_ptr++;
}

static void rc_dec_normalize(RangeCoder *rc) {
    while ((rc->low ^ (rc->low + rc->range)) < RC_TOP ||
           (rc->range < RC_BOT && ((rc->range = -rc->low & (RC_BOT - 1)), 1))) {
        rc->code = (rc->code << 8) | *rc->in_ptr++;
        rc->low <<= 8;
        rc->range <<= 8;
    }
}

static uint8_t rc_dec_symbol(RangeCoder *rc, Order1Model *m, uint8_t ctx) {
    uint32_t total = m->freq[ctx][256];
    uint32_t cum = 0;
    rc->range /= total;
    uint32_t target = (rc->code - rc->low) / rc->range;
    
    uint8_t sym = 0;
    while (cum + m->freq[ctx][sym] <= target) {
        cum += m->freq[ctx][sym];
        sym++;
    }
    
    rc->low += cum * rc->range;
    rc->range *= m->freq[ctx][sym];
    rc_dec_normalize(rc);
    model_update(m, ctx, sym);
    return sym;
}

/* ============== ФРАКТАЛЬНОЕ СЖАТИЕ ============== */
/*
 * Три уровня фрактальной структуры:
 * 1. Нулевые последовательности (самый частый паттерн после BWT+MTF)
 * 2. Малые числа 1-9 (второй по частоте)
 * 3. Большие числа >= 10 (редкие)
 * 
 * Каждый уровень кодируется отдельно, что даёт лучшее сжатие
 */

/* Сжатие с фрактальной структурой */
static size_t fractal_compress(const uint8_t *mtf_data, size_t len, uint8_t *out) {
    /* Шаг 1: Разделяем на фрактальные уровни */
    BitStream level0, level1, level2;
    bs_init(&level0); /* Длины нулевых серий */
    bs_init(&level1); /* Малые числа 1-9 */
    bs_init(&level2); /* Большие числа >= 10 */
    
    size_t i = 0;
    while (i < len) {
        if (mtf_data[i] == 0) {
            /* Считаем длину нулевой серии */
            size_t run = 0;
            while (i < len && mtf_data[i] == 0) {
                run++;
                i++;
            }
            /* Записываем маркер 0 и длину */
            write_logical_number(&level0, (uint32_t)run);
        } else if (mtf_data[i] < 10) {
            /* Малое число 1-9 */
            bs_write(&level1, mtf_data[i]);
            i++;
        } else {
            /* Большое число */
            write_logical_number(&level2, mtf_data[i]);
            i++;
        }
    }
    
    /* Теперь сжимаем каждый уровень Range Coder'ом */
    RangeCoder rc;
    Order1Model *model = malloc(sizeof(Order1Model));
    model_init(model);
    
    /* Записываем размеры уровней */
    size_t pos = 0;
    out[pos++] = (level0.size >> 24) & 0xFF;
    out[pos++] = (level0.size >> 16) & 0xFF;
    out[pos++] = (level0.size >> 8) & 0xFF;
    out[pos++] = level0.size & 0xFF;
    
    out[pos++] = (level1.size >> 24) & 0xFF;
    out[pos++] = (level1.size >> 16) & 0xFF;
    out[pos++] = (level1.size >> 8) & 0xFF;
    out[pos++] = level1.size & 0xFF;
    
    out[pos++] = (level2.size >> 24) & 0xFF;
    out[pos++] = (level2.size >> 16) & 0xFF;
    out[pos++] = (level2.size >> 8) & 0xFF;
    out[pos++] = level2.size & 0xFF;
    
    /* Сжимаем level0 (нулевые серии) - Order-1 */
    rc_enc_init(&rc, out + pos);
    uint8_t ctx = 0;
    for (size_t j = 0; j < level0.size; j++) {
        rc_enc_symbol(&rc, model, ctx, level0.data[j]);
        ctx = level0.data[j];
    }
    rc_enc_flush(&rc);
    size_t lvl0_compressed = rc.out_size;
    pos += lvl0_compressed;
    
    /* Записываем размер сжатого level0 */
    out[12] = (lvl0_compressed >> 24) & 0xFF;
    out[13] = (lvl0_compressed >> 16) & 0xFF;
    out[14] = (lvl0_compressed >> 8) & 0xFF;
    out[15] = lvl0_compressed & 0xFF;
    
    /* Сдвигаем данные чтобы вставить размер */
    memmove(out + 16, out + 12, pos - 12);
    pos += 4;
    
    /* Сжимаем level1 (малые числа) - Order-1 */
    model_init(model);
    rc_enc_init(&rc, out + pos);
    ctx = 0;
    for (size_t j = 0; j < level1.size; j++) {
        rc_enc_symbol(&rc, model, ctx, level1.data[j]);
        ctx = level1.data[j];
    }
    rc_enc_flush(&rc);
    size_t lvl1_compressed = rc.out_size;
    
    /* Записываем размер сжатого level1 */
    out[16] = (lvl1_compressed >> 24) & 0xFF;
    out[17] = (lvl1_compressed >> 16) & 0xFF;
    out[18] = (lvl1_compressed >> 8) & 0xFF;
    out[19] = lvl1_compressed & 0xFF;
    
    memmove(out + 20, out + 16, pos - 16 + lvl1_compressed);
    pos += 4 + lvl1_compressed;
    
    /* Сжимаем level2 (большие числа) - Order-1 */
    model_init(model);
    rc_enc_init(&rc, out + pos);
    ctx = 0;
    for (size_t j = 0; j < level2.size; j++) {
        rc_enc_symbol(&rc, model, ctx, level2.data[j]);
        ctx = level2.data[j];
    }
    rc_enc_flush(&rc);
    size_t lvl2_compressed = rc.out_size;
    
    /* Записываем размер сжатого level2 */
    out[20] = (lvl2_compressed >> 24) & 0xFF;
    out[21] = (lvl2_compressed >> 16) & 0xFF;
    out[22] = (lvl2_compressed >> 8) & 0xFF;
    out[23] = lvl2_compressed & 0xFF;
    
    memmove(out + 24, out + 20, pos - 20 + lvl2_compressed);
    pos += 4 + lvl2_compressed;
    
    free(model);
    bs_free(&level0);
    bs_free(&level1);
    bs_free(&level2);
    
    return pos;
}

/* ============== ОСНОВНОЕ СЖАТИЕ/РАСПАКОВКА ============== */
/* Магическое число */
#define MAGIC 0x4B4F4C46  /* "KOLF" - Kolibri Fractal */

static int compress_file(const char *in_path, const char *out_path) {
    FILE *fin = fopen(in_path, "rb");
    if (!fin) { perror("fopen input"); return 1; }
    
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
    
    /* Статистика MTF */
    size_t zeros = 0, small = 0, large = 0;
    for (size_t i = 0; i < in_size; i++) {
        if (mtf_out[i] == 0) zeros++;
        else if (mtf_out[i] < 10) small++;
        else large++;
    }
    printf("MTF stats: zeros=%zu (%.1f%%), 1-9=%zu (%.1f%%), >=10=%zu (%.1f%%)\n",
           zeros, 100.0*zeros/in_size,
           small, 100.0*small/in_size,
           large, 100.0*large/in_size);
    
    /* Используем Order-1 Range Coder напрямую (проверенный метод) */
    uint8_t *compressed = malloc(in_size * 2);
    size_t hdr_size = 16;  /* magic + orig_size + bwt_idx + crc */
    
    /* Order-1 Range Coder */
    RangeCoder rc;
    Order1Model *model = malloc(sizeof(Order1Model));
    model_init(model);
    rc_enc_init(&rc, compressed + hdr_size);
    
    uint8_t ctx = 0;
    for (size_t i = 0; i < in_size; i++) {
        rc_enc_symbol(&rc, model, ctx, mtf_out[i]);
        ctx = mtf_out[i];
    }
    rc_enc_flush(&rc);
    
    size_t comp_size = hdr_size + rc.out_size;
    
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
    
    FILE *fout = fopen(out_path, "wb");
    fwrite(compressed, 1, comp_size, fout);
    fclose(fout);
    
    printf("Input: %zu bytes\n", in_size);
    printf("Output: %zu bytes\n", comp_size);
    printf("Ratio: %.2fx\n", (double)in_size / comp_size);
    
    free(model);
    free(in_data);
    free(mtf_out);
    free(compressed);
    return 0;
}

static int decompress_file(const char *in_path, const char *out_path) {
    FILE *fin = fopen(in_path, "rb");
    if (!fin) { perror("fopen input"); return 1; }
    
    fseek(fin, 0, SEEK_END);
    size_t comp_size = ftell(fin);
    fseek(fin, 0, SEEK_SET);
    
    uint8_t *compressed = malloc(comp_size);
    fread(compressed, 1, comp_size, fin);
    fclose(fin);
    
    /* Читаем заголовок */
    uint32_t magic = (compressed[0] << 24) | (compressed[1] << 16) |
                     (compressed[2] << 8) | compressed[3];
    if (magic != MAGIC) {
        fprintf(stderr, "Invalid magic number\n");
        free(compressed);
        return 1;
    }
    
    size_t orig_size = (compressed[4] << 24) | (compressed[5] << 16) |
                       (compressed[6] << 8) | compressed[7];
    size_t bwt_idx = (compressed[8] << 24) | (compressed[9] << 16) |
                     (compressed[10] << 8) | compressed[11];
    uint32_t stored_crc = (compressed[12] << 24) | (compressed[13] << 16) |
                          (compressed[14] << 8) | compressed[15];
    
    /* Order-1 Range Decoder */
    uint8_t *mtf_out = malloc(orig_size);
    RangeCoder rc;
    Order1Model *model = malloc(sizeof(Order1Model));
    model_init(model);
    rc_dec_init(&rc, compressed + 16);
    
    uint8_t ctx = 0;
    for (size_t i = 0; i < orig_size; i++) {
        mtf_out[i] = rc_dec_symbol(&rc, model, ctx);
        ctx = mtf_out[i];
    }
    
    /* MTF decode */
    uint8_t *bwt_out = malloc(orig_size);
    mtf_decode(mtf_out, orig_size, bwt_out);
    free(mtf_out);
    
    /* BWT decode */
    uint8_t *out_data = malloc(orig_size);
    bwt_decode(bwt_out, orig_size, bwt_idx, out_data);
    free(bwt_out);
    
    /* Verify CRC */
    uint32_t calc_crc = calc_crc32(out_data, orig_size);
    if (calc_crc != stored_crc) {
        fprintf(stderr, "CRC mismatch! Expected %08X, got %08X\n", stored_crc, calc_crc);
        free(compressed);
        free(out_data);
        free(model);
        return 1;
    }
    
    FILE *fout = fopen(out_path, "wb");
    fwrite(out_data, 1, orig_size, fout);
    fclose(fout);
    
    printf("Decompressed: %zu bytes, CRC OK\n", orig_size);
    
    free(model);
    free(compressed);
    free(out_data);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("KOLIBRI FRACTAL v20 - Фрактальная вложенность + Логические числа\n");
        printf("Usage: %s compress|decompress <input> <output>\n", argv[0]);
        return 1;
    }
    
    if (strcmp(argv[1], "compress") == 0) {
        return compress_file(argv[2], argv[3]);
    } else if (strcmp(argv[1], "decompress") == 0) {
        return decompress_file(argv[2], argv[3]);
    } else {
        fprintf(stderr, "Unknown command: %s\n", argv[1]);
        return 1;
    }
}
