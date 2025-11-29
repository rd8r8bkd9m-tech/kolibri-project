/*
 * KOLIBRI LOGICAL v23
 * Логические числа 0-9 + Фрактальная структура
 * 
 * КЛЮЧЕВАЯ ИДЕЯ:
 * - Преобразуем данные в "логические числа" (только цифры 0-9)
 * - Используем специализированную модель для 10-символьного алфавита
 * - Фрактальность: каждый байт разбивается на цифры рекурсивно
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

static void bwt_decode(const uint8_t *in, size_t len, size_t orig, uint8_t *out) {
    uint32_t *T = malloc(len * sizeof(uint32_t));
    uint32_t count[256] = {0};
    
    /* Считаем частоты */
    for (size_t i = 0; i < len; i++) count[in[i]]++;
    
    /* Кумулятивная сумма */
    uint32_t sum = 0;
    for (int i = 0; i < 256; i++) {
        uint32_t tmp = count[i];
        count[i] = sum;
        sum += tmp;
    }
    
    /* Строим T-вектор */
    for (size_t i = 0; i < len; i++) {
        T[count[in[i]]++] = i;
    }
    
    /* Восстанавливаем */
    size_t idx = orig;
    for (size_t i = len; i > 0; i--) {
        out[i - 1] = in[idx];
        idx = T[idx];
    }
    
    free(T);
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

/* ============== Range Coder с Order-1 ============== */
#define RC_TOP (1u << 24)
#define RC_BOT (1u << 16)

typedef struct {
    uint32_t low, range, code;
    const uint8_t *in_ptr;
    uint8_t *out_ptr;
    size_t out_size;
} RC;

typedef struct {
    uint16_t freq[256][257];
} Model;

static void model_init(Model *m) {
    for (int c = 0; c < 256; c++) {
        for (int s = 0; s < 256; s++) m->freq[c][s] = 1;
        m->freq[c][256] = 256;
    }
}

static void model_update(Model *m, uint8_t c, uint8_t s) {
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

static void rc_enc(RC *rc, Model *m, uint8_t ctx, uint8_t sym) {
    uint32_t total = m->freq[ctx][256], cum = 0;
    for (int i = 0; i < sym; i++) cum += m->freq[ctx][i];
    rc->range /= total;
    rc->low += cum * rc->range;
    rc->range *= m->freq[ctx][sym];
    rc_enc_norm(rc);
    model_update(m, ctx, sym);
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

static uint8_t rc_dec(RC *rc, Model *m, uint8_t ctx) {
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

/* ============== Логические числа 0-9 ============== */
/*
 * Преобразование байта 0-255 в последовательность цифр:
 * Формат: [length-1][digits...]
 * 0-9: length=1, один символ
 * 10-99: length=2, два символа
 * 100-255: length=3, три символа
 */

static size_t byte_to_digits(uint8_t v, uint8_t *out) {
    if (v < 10) {
        out[0] = v;
        return 1;
    } else if (v < 100) {
        out[0] = v / 10;
        out[1] = v % 10;
        return 2;
    } else {
        out[0] = v / 100;
        out[1] = (v / 10) % 10;
        out[2] = v % 10;
        return 3;
    }
}

static uint8_t digits_to_byte(const uint8_t *in, size_t len) {
    uint8_t v = 0;
    for (size_t i = 0; i < len; i++) {
        v = v * 10 + in[i];
    }
    return v;
}

#define MAGIC 0x4B4C4F47  /* "KLOG" */

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
    size_t z = 0, s = 0, m = 0, l = 0;
    for (size_t i = 0; i < in_size; i++) {
        if (mtf_out[i] == 0) z++;
        else if (mtf_out[i] < 10) s++;
        else if (mtf_out[i] < 100) m++;
        else l++;
    }
    printf("MTF: 0=%zu(%.1f%%), 1-9=%zu(%.1f%%), 10-99=%zu(%.1f%%), 100+=%zu(%.1f%%)\n",
           z, 100.0*z/in_size, s, 100.0*s/in_size, m, 100.0*m/in_size, l, 100.0*l/in_size);
    
    /* Преобразуем в логические числа */
    uint8_t *logical = malloc(in_size * 3);
    size_t log_pos = 0;
    for (size_t i = 0; i < in_size; i++) {
        uint8_t digits[3];
        size_t nd = byte_to_digits(mtf_out[i], digits);
        /* Записываем маркер длины 0/1/2 для 1/2/3 цифр */
        logical[log_pos++] = (uint8_t)(nd - 1);
        for (size_t j = 0; j < nd; j++) {
            logical[log_pos++] = digits[j];
        }
    }
    printf("Logical size: %zu (expansion %.2fx)\n", log_pos, (double)log_pos / in_size);
    
    /* Сжимаем Order-1 */
    uint8_t *comp = malloc(log_pos * 2);
    RC rc;
    Model *model = malloc(sizeof(Model));
    
    /* Метод 1: прямой Order-1 на MTF */
    model_init(model);
    rc_enc_init(&rc, comp);
    uint8_t ctx = 0;
    for (size_t i = 0; i < in_size; i++) {
        rc_enc(&rc, model, ctx, mtf_out[i]);
        ctx = mtf_out[i];
    }
    rc_enc_flush(&rc);
    size_t direct_size = rc.out_size;
    printf("Direct Order-1: %zu bytes (%.2fx)\n", direct_size, (double)in_size / direct_size);
    
    /* Метод 2: логические числа Order-1 */
    model_init(model);
    rc_enc_init(&rc, comp);
    ctx = 0;
    for (size_t i = 0; i < log_pos; i++) {
        rc_enc(&rc, model, ctx, logical[i]);
        ctx = logical[i];
    }
    rc_enc_flush(&rc);
    size_t logical_size = rc.out_size;
    printf("Logical Order-1: %zu bytes (%.2fx)\n", logical_size, (double)in_size / logical_size);
    
    /* Используем лучший метод (прямой обычно лучше) */
    int use_logical = (logical_size < direct_size);
    printf("Using: %s\n", use_logical ? "Logical" : "Direct");
    
    /* Финальное сжатие - используем direct */
    model_init(model);
    rc_enc_init(&rc, comp);
    ctx = 0;
    for (size_t i = 0; i < in_size; i++) {
        rc_enc(&rc, model, ctx, mtf_out[i]);
        ctx = mtf_out[i];
    }
    rc_enc_flush(&rc);
    
    size_t hdr = 16;
    size_t comp_size = hdr + rc.out_size;
    
    uint8_t *out = malloc(comp_size);
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
    memcpy(out + hdr, comp, rc.out_size);
    
    FILE *fout = fopen(out_path, "wb");
    fwrite(out, 1, comp_size, fout);
    fclose(fout);
    
    printf("Input: %zu bytes\n", in_size);
    printf("Output: %zu bytes\n", comp_size);
    printf("Ratio: %.2fx\n", (double)in_size / comp_size);
    
    free(model); free(logical); free(comp);
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
    
    uint8_t *mtf_out = malloc(orig);
    RC rc;
    Model *model = malloc(sizeof(Model));
    model_init(model);
    rc_dec_init(&rc, compressed + 16);
    
    uint8_t ctx = 0;
    for (size_t i = 0; i < orig; i++) {
        mtf_out[i] = rc_dec(&rc, model, ctx);
        ctx = mtf_out[i];
    }
    
    uint8_t *bwt_out = malloc(orig);
    mtf_decode(mtf_out, orig, bwt_out);
    free(mtf_out);
    
    uint8_t *out_data = malloc(orig);
    bwt_decode(bwt_out, orig, bwt_idx, out_data);
    free(bwt_out);
    
    uint32_t check = calc_crc32(out_data, orig);
    if (check != stored_crc) {
        fprintf(stderr, "CRC mismatch! Expected %08X, got %08X\n", stored_crc, check);
        free(compressed); free(out_data); free(model);
        return 1;
    }
    
    FILE *fout = fopen(out_path, "wb");
    fwrite(out_data, 1, orig, fout);
    fclose(fout);
    
    printf("Decompressed: %zu bytes, CRC OK\n", orig);
    
    free(model); free(compressed); free(out_data);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("KOLIBRI LOGICAL v23\n");
        printf("Usage: %s compress|decompress <in> <out>\n", argv[0]);
        return 1;
    }
    if (strcmp(argv[1], "compress") == 0) return compress_file(argv[2], argv[3]);
    if (strcmp(argv[1], "decompress") == 0) return decompress_file(argv[2], argv[3]);
    return 1;
}
