/*
 * KOLIBRI FRACTAL v34 - Оптимизированный кодер
 * 
 * Улучшения:
 * 1. RLE для нулей перед range coding (нули идут пачками после MTF)
 * 2. Тритовый алфавит для vals1 (1-3) вместо 256-символьной модели
 * 3. Контекст по длинам серий для bits
 * 4. 10 логических ядер с разделением каждого на 10
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* CRC32 */
static uint32_t crc32_table[256];
static void init_crc32(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) c = (c >> 1) ^ ((c & 1) ? 0xEDB88320 : 0);
        crc32_table[i] = c;
    }
}
static uint32_t calc_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFF;
}

/* BWT */
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

/* MTF */
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

/* Адаптивная модель для N символов */
typedef struct {
    uint16_t freq[256];
    uint16_t total;
    int nsym;
} AdaptModel;

static void am_init(AdaptModel *m, int nsym) {
    m->nsym = nsym;
    for (int i = 0; i < nsym; i++) m->freq[i] = 1;
    m->total = nsym;
}
static void am_update(AdaptModel *m, int sym) {
    m->freq[sym] += 16;
    m->total += 16;
    if (m->total > 0x3FFF) {
        m->total = 0;
        for (int i = 0; i < m->nsym; i++) {
            m->freq[i] = (m->freq[i] >> 1) | 1;
            m->total += m->freq[i];
        }
    }
}
static void rc_enc_sym(RC *rc, AdaptModel *m, int sym) {
    uint32_t cum = 0;
    for (int i = 0; i < sym; i++) cum += m->freq[i];
    rc->range /= m->total;
    rc->low += cum * rc->range;
    rc->range *= m->freq[sym];
    rc_enc_norm(rc);
    am_update(m, sym);
}
static int rc_dec_sym(RC *rc, AdaptModel *m) {
    rc->range /= m->total;
    uint32_t target = (rc->code - rc->low) / rc->range;
    uint32_t cum = 0;
    int sym = 0;
    while (sym < m->nsym - 1 && cum + m->freq[sym] <= target) {
        cum += m->freq[sym];
        sym++;
    }
    rc->low += cum * rc->range;
    rc->range *= m->freq[sym];
    rc_dec_norm(rc);
    am_update(m, sym);
    return sym;
}

/* Контекстная модель Order-1 для N символов */
typedef struct {
    AdaptModel ctx[256];
    int nsym;
} CtxModel;

static void cm_init(CtxModel *m, int nsym) {
    m->nsym = nsym;
    for (int i = 0; i < 256; i++) am_init(&m->ctx[i], nsym);
}
static void rc_enc_sym_ctx(RC *rc, CtxModel *m, int ctx, int sym) {
    if (ctx >= 256) ctx = 255;
    rc_enc_sym(rc, &m->ctx[ctx], sym);
}
static int rc_dec_sym_ctx(RC *rc, CtxModel *m, int ctx) {
    if (ctx >= 256) ctx = 255;
    return rc_dec_sym(rc, &m->ctx[ctx]);
}

/* RLE для нулей - кодируем длины серий */
typedef struct {
    uint8_t *data;    // 0 = zero-run, 1+ = value
    uint32_t *runs;   // длины серий нулей
    size_t data_len;
    size_t runs_len;
    size_t orig_len;
} RLEData;

static RLEData* rle_encode_zeros(const uint8_t *in, size_t len) {
    RLEData *rle = malloc(sizeof(RLEData));
    rle->data = malloc(len);
    rle->runs = malloc(len * sizeof(uint32_t));
    rle->data_len = 0;
    rle->runs_len = 0;
    rle->orig_len = len;
    
    size_t i = 0;
    while (i < len) {
        if (in[i] == 0) {
            // Считаем серию нулей
            uint32_t run = 0;
            while (i < len && in[i] == 0) {
                run++;
                i++;
            }
            rle->data[rle->data_len++] = 0;  // маркер
            rle->runs[rle->runs_len++] = run;
        } else {
            rle->data[rle->data_len++] = in[i++];
        }
    }
    return rle;
}

static void rle_decode_zeros(const RLEData *rle, uint8_t *out) {
    size_t out_pos = 0;
    size_t run_idx = 0;
    
    for (size_t i = 0; i < rle->data_len; i++) {
        if (rle->data[i] == 0) {
            uint32_t run = rle->runs[run_idx++];
            for (uint32_t j = 0; j < run; j++) {
                out[out_pos++] = 0;
            }
        } else {
            out[out_pos++] = rle->data[i];
        }
    }
}

static void rle_free(RLEData *rle) {
    free(rle->data);
    free(rle->runs);
    free(rle);
}

/* Кодирование длин серий - Elias gamma */
static void encode_run_length(RC *rc, AdaptModel *m_flag, AdaptModel *m_bits, uint32_t run) {
    // Кодируем run как: unary(length) + binary(bits)
    // run=1: 0
    // run=2-3: 10 + 1bit
    // run=4-7: 110 + 2bits
    // run=8-15: 1110 + 3bits
    // ...
    if (run == 0) run = 1;  // защита
    
    int bits = 0;
    uint32_t temp = run;
    while (temp > 1) { bits++; temp >>= 1; }
    
    // Унарный код длины
    for (int i = 0; i < bits; i++) {
        rc_enc_sym(rc, m_flag, 1);  // 1 = continue
    }
    rc_enc_sym(rc, m_flag, 0);  // 0 = stop
    
    // Бинарные биты (кроме старшего, он всегда 1)
    for (int i = bits - 1; i >= 0; i--) {
        rc_enc_sym(rc, m_bits, (run >> i) & 1);
    }
}

static uint32_t decode_run_length(RC *rc, AdaptModel *m_flag, AdaptModel *m_bits) {
    int bits = 0;
    while (rc_dec_sym(rc, m_flag) == 1) bits++;
    
    uint32_t run = 1;
    for (int i = bits - 1; i >= 0; i--) {
        run = (run << 1) | rc_dec_sym(rc, m_bits);
    }
    return run;
}

#define MAGIC 0x4B463334  /* "KF34" */

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
    
    uint8_t *bwt_out = malloc(in_size);
    size_t bwt_idx = bwt_encode(in_data, in_size, bwt_out);
    
    uint8_t *mtf = malloc(in_size);
    mtf_encode(bwt_out, in_size, mtf);
    free(bwt_out);
    
    /* Анализ распределения */
    size_t cnt[5] = {0};  // 0, 1-3, 4-9, 10-31, 32+
    for (size_t i = 0; i < in_size; i++) {
        uint8_t v = mtf[i];
        if (v == 0) cnt[0]++;
        else if (v <= 3) cnt[1]++;
        else if (v <= 9) cnt[2]++;
        else if (v <= 31) cnt[3]++;
        else cnt[4]++;
    }
    printf("=== РАСПРЕДЕЛЕНИЕ (10 логических ядер / 10) ===\n");
    printf("Ядро 0 (=0):    %zu (%.1f%%)\n", cnt[0], 100.0*cnt[0]/in_size);
    printf("Ядро 1-3:       %zu (%.1f%%)\n", cnt[1], 100.0*cnt[1]/in_size);
    printf("Ядро 4-9:       %zu (%.1f%%)\n", cnt[2], 100.0*cnt[2]/in_size);
    printf("Ядро 10-31:     %zu (%.1f%%)\n", cnt[3], 100.0*cnt[3]/in_size);
    printf("Ядро 32+:       %zu (%.1f%%)\n", cnt[4], 100.0*cnt[4]/in_size);
    
    /* Подсчёт серий нулей */
    size_t zero_runs = 0;
    for (size_t i = 0; i < in_size; i++) {
        if (mtf[i] == 0 && (i == 0 || mtf[i-1] != 0)) zero_runs++;
    }
    printf("Серий нулей: %zu (avg len: %.1f)\n", zero_runs, (double)cnt[0]/zero_runs);
    
    size_t buf = in_size + 1024;
    RC rc;
    
    /* === Метод 1: Baseline Order-1 === */
    uint8_t *comp_base = malloc(buf);
    CtxModel *cm256 = malloc(sizeof(CtxModel));
    cm_init(cm256, 256);
    rc_enc_init(&rc, comp_base, buf);
    int ctx = 0;
    for (size_t i = 0; i < in_size; i++) {
        rc_enc_sym_ctx(&rc, cm256, ctx, mtf[i]);
        ctx = mtf[i];
    }
    rc_enc_flush(&rc);
    size_t size_base = rc.out_size;
    printf("Baseline Order-1: %zu (%.2fx)\n", size_base, (double)in_size/size_base);
    free(cm256);
    
    /* === Метод 2: RLE + Fractal с оптимизированными моделями === */
    
    /* RLE для нулей */
    RLEData *rle = rle_encode_zeros(mtf, in_size);
    printf("После RLE: %zu элементов + %zu серий\n", rle->data_len, rle->runs_len);
    
    /* Кодируем RLE данные (ненулевые значения) */
    uint8_t *comp_vals = malloc(buf);
    
    // Разделяем на группы: 1-3, 4-9, 10-31, 32+
    // Для каждой группы используем оптимальный размер алфавита
    
    // Группа 1-3: тритовый алфавит (3 символа)
    size_t n13 = 0;
    for (size_t i = 0; i < rle->data_len; i++) {
        if (rle->data[i] >= 1 && rle->data[i] <= 3) n13++;
    }
    
    // Группа 4-9: 6 символов
    size_t n49 = 0;
    for (size_t i = 0; i < rle->data_len; i++) {
        if (rle->data[i] >= 4 && rle->data[i] <= 9) n49++;
    }
    
    // Группа 10-31: 22 символа
    size_t n1031 = 0;
    for (size_t i = 0; i < rle->data_len; i++) {
        if (rle->data[i] >= 10 && rle->data[i] <= 31) n1031++;
    }
    
    // Группа 32+: 224 символа
    size_t n32 = 0;
    for (size_t i = 0; i < rle->data_len; i++) {
        if (rle->data[i] >= 32) n32++;
    }
    
    /* Кодируем биты структуры - какой тип значения */
    uint8_t *comp_struct = malloc(buf);
    AdaptModel m_struct;
    am_init(&m_struct, 5);  // 0=zero-run, 1=1-3, 2=4-9, 3=10-31, 4=32+
    rc_enc_init(&rc, comp_struct, buf);
    for (size_t i = 0; i < rle->data_len; i++) {
        uint8_t v = rle->data[i];
        int type;
        if (v == 0) type = 0;
        else if (v <= 3) type = 1;
        else if (v <= 9) type = 2;
        else if (v <= 31) type = 3;
        else type = 4;
        rc_enc_sym(&rc, &m_struct, type);
    }
    rc_enc_flush(&rc);
    size_t size_struct = rc.out_size;
    
    /* Кодируем длины серий нулей */
    uint8_t *comp_runs = malloc(buf);
    AdaptModel m_flag, m_bits;
    am_init(&m_flag, 2);
    am_init(&m_bits, 2);
    rc_enc_init(&rc, comp_runs, buf);
    for (size_t i = 0; i < rle->runs_len; i++) {
        encode_run_length(&rc, &m_flag, &m_bits, rle->runs[i]);
    }
    rc_enc_flush(&rc);
    size_t size_runs = rc.out_size;
    
    /* Кодируем значения 1-3 (тритовый алфавит) */
    uint8_t *comp_v13 = malloc(buf);
    CtxModel *cm3 = malloc(sizeof(CtxModel));
    cm_init(cm3, 3);
    rc_enc_init(&rc, comp_v13, buf);
    ctx = 0;
    for (size_t i = 0; i < rle->data_len; i++) {
        if (rle->data[i] >= 1 && rle->data[i] <= 3) {
            int sym = rle->data[i] - 1;  // 0-2
            rc_enc_sym_ctx(&rc, cm3, ctx, sym);
            ctx = sym;
        }
    }
    rc_enc_flush(&rc);
    size_t size_v13 = rc.out_size;
    free(cm3);
    
    /* Кодируем значения 4-9 (6 символов) */
    uint8_t *comp_v49 = malloc(buf);
    CtxModel *cm6 = malloc(sizeof(CtxModel));
    cm_init(cm6, 6);
    rc_enc_init(&rc, comp_v49, buf);
    ctx = 0;
    for (size_t i = 0; i < rle->data_len; i++) {
        if (rle->data[i] >= 4 && rle->data[i] <= 9) {
            int sym = rle->data[i] - 4;  // 0-5
            rc_enc_sym_ctx(&rc, cm6, ctx, sym);
            ctx = sym;
        }
    }
    rc_enc_flush(&rc);
    size_t size_v49 = rc.out_size;
    free(cm6);
    
    /* Кодируем значения 10-31 (22 символа) */
    uint8_t *comp_v1031 = malloc(buf);
    CtxModel *cm22 = malloc(sizeof(CtxModel));
    cm_init(cm22, 22);
    rc_enc_init(&rc, comp_v1031, buf);
    ctx = 0;
    for (size_t i = 0; i < rle->data_len; i++) {
        if (rle->data[i] >= 10 && rle->data[i] <= 31) {
            int sym = rle->data[i] - 10;  // 0-21
            rc_enc_sym_ctx(&rc, cm22, ctx, sym);
            ctx = sym;
        }
    }
    rc_enc_flush(&rc);
    size_t size_v1031 = rc.out_size;
    free(cm22);
    
    /* Кодируем значения 32+ (224 символа) */
    uint8_t *comp_v32 = malloc(buf);
    CtxModel *cm224 = malloc(sizeof(CtxModel));
    cm_init(cm224, 224);
    rc_enc_init(&rc, comp_v32, buf);
    ctx = 0;
    for (size_t i = 0; i < rle->data_len; i++) {
        if (rle->data[i] >= 32) {
            int sym = rle->data[i] - 32;  // 0-223
            rc_enc_sym_ctx(&rc, cm224, ctx, sym);
            ctx = sym;
        }
    }
    rc_enc_flush(&rc);
    size_t size_v32 = rc.out_size;
    free(cm224);
    
    size_t size_frac = size_struct + size_runs + size_v13 + size_v49 + size_v1031 + size_v32 + 28;
    printf("RLE+Fractal:\n");
    printf("  struct:  %zu\n", size_struct);
    printf("  runs:    %zu\n", size_runs);
    printf("  v1-3:    %zu (%zu vals, %.2f bpv)\n", size_v13, n13, n13 ? 8.0*size_v13/n13 : 0);
    printf("  v4-9:    %zu (%zu vals, %.2f bpv)\n", size_v49, n49, n49 ? 8.0*size_v49/n49 : 0);
    printf("  v10-31:  %zu (%zu vals, %.2f bpv)\n", size_v1031, n1031, n1031 ? 8.0*size_v1031/n1031 : 0);
    printf("  v32+:    %zu (%zu vals, %.2f bpv)\n", size_v32, n32, n32 ? 8.0*size_v32/n32 : 0);
    printf("  ИТОГО: %zu (%.2fx)\n", size_frac, (double)in_size/size_frac);
    
    int method = (size_frac < size_base) ? 2 : 1;
    size_t best = (method == 2) ? size_frac : size_base;
    printf("Выбран: %d\n", method);
    
    /* Запись */
    uint8_t *out = malloc(64 + best);
    size_t pos = 0;
    
    #define W32(x) out[pos++]=(x>>24);out[pos++]=(x>>16);out[pos++]=(x>>8);out[pos++]=x
    W32(MAGIC);
    W32(in_size);
    W32(bwt_idx);
    W32(crc);
    out[pos++] = method;
    out[pos++] = 0; out[pos++] = 0; out[pos++] = 0;
    
    if (method == 2) {
        W32(rle->data_len);
        W32(rle->runs_len);
        W32(size_struct);
        W32(size_runs);
        W32(size_v13);
        W32(size_v49);
        W32(size_v1031);
        memcpy(out+pos, comp_struct, size_struct); pos += size_struct;
        memcpy(out+pos, comp_runs, size_runs); pos += size_runs;
        memcpy(out+pos, comp_v13, size_v13); pos += size_v13;
        memcpy(out+pos, comp_v49, size_v49); pos += size_v49;
        memcpy(out+pos, comp_v1031, size_v1031); pos += size_v1031;
        memcpy(out+pos, comp_v32, size_v32); pos += size_v32;
    } else {
        memcpy(out+pos, comp_base, size_base); pos += size_base;
    }
    #undef W32
    
    FILE *fout = fopen(out_path, "wb");
    fwrite(out, 1, pos, fout);
    fclose(fout);
    
    printf("\n=== РЕЗУЛЬТАТ ===\n");
    printf("Вход: %zu\n", in_size);
    printf("Выход: %zu\n", pos);
    printf("Сжатие: %.2fx\n", (double)in_size/pos);
    
    rle_free(rle);
    free(comp_base); free(comp_struct); free(comp_runs);
    free(comp_v13); free(comp_v49); free(comp_v1031); free(comp_v32); free(comp_vals);
    free(in_data); free(mtf); free(out);
    return 0;
}

static int decompress_file(const char *in_path, const char *out_path) {
    FILE *fin = fopen(in_path, "rb");
    if (!fin) { perror("fopen"); return 1; }
    fseek(fin, 0, SEEK_END);
    size_t in_size = ftell(fin);
    fseek(fin, 0, SEEK_SET);
    uint8_t *in_data = malloc(in_size);
    fread(in_data, 1, in_size, fin);
    fclose(fin);
    
    size_t pos = 0;
    #define R32(x) x=(in_data[pos]<<24)|(in_data[pos+1]<<16)|(in_data[pos+2]<<8)|in_data[pos+3];pos+=4
    
    uint32_t magic; R32(magic);
    if (magic != MAGIC) { fprintf(stderr, "Bad magic\n"); return 1; }
    
    uint32_t orig; R32(orig);
    uint32_t bwt_idx; R32(bwt_idx);
    uint32_t stored_crc; R32(stored_crc);
    uint8_t method = in_data[pos++]; pos += 3;
    
    uint8_t *mtf = malloc(orig);
    RC rc;
    
    if (method == 2) {
        uint32_t data_len, runs_len;
        uint32_t sz_struct, sz_runs, sz_v13, sz_v49, sz_v1031;
        R32(data_len);
        R32(runs_len);
        R32(sz_struct);
        R32(sz_runs);
        R32(sz_v13);
        R32(sz_v49);
        R32(sz_v1031);
        
        /* Декодируем структуру */
        uint8_t *types = malloc(data_len);
        AdaptModel m_struct;
        am_init(&m_struct, 5);
        rc_dec_init(&rc, in_data + pos);
        for (size_t i = 0; i < data_len; i++) {
            types[i] = rc_dec_sym(&rc, &m_struct);
        }
        pos += sz_struct;
        
        /* Декодируем длины серий */
        uint32_t *runs = malloc(runs_len * sizeof(uint32_t));
        AdaptModel m_flag, m_bits;
        am_init(&m_flag, 2);
        am_init(&m_bits, 2);
        rc_dec_init(&rc, in_data + pos);
        for (size_t i = 0; i < runs_len; i++) {
            runs[i] = decode_run_length(&rc, &m_flag, &m_bits);
        }
        pos += sz_runs;
        
        /* Подсчёт значений каждого типа */
        size_t n13 = 0, n49 = 0, n1031 = 0, n32 = 0;
        for (size_t i = 0; i < data_len; i++) {
            if (types[i] == 1) n13++;
            else if (types[i] == 2) n49++;
            else if (types[i] == 3) n1031++;
            else if (types[i] == 4) n32++;
        }
        
        /* Декодируем значения 1-3 */
        uint8_t *v13 = malloc(n13 + 1);
        CtxModel *cm3 = malloc(sizeof(CtxModel));
        cm_init(cm3, 3);
        rc_dec_init(&rc, in_data + pos);
        int ctx = 0;
        for (size_t i = 0; i < n13; i++) {
            v13[i] = rc_dec_sym_ctx(&rc, cm3, ctx);
            ctx = v13[i];
        }
        pos += sz_v13;
        free(cm3);
        
        /* Декодируем значения 4-9 */
        uint8_t *v49 = malloc(n49 + 1);
        CtxModel *cm6 = malloc(sizeof(CtxModel));
        cm_init(cm6, 6);
        rc_dec_init(&rc, in_data + pos);
        ctx = 0;
        for (size_t i = 0; i < n49; i++) {
            v49[i] = rc_dec_sym_ctx(&rc, cm6, ctx);
            ctx = v49[i];
        }
        pos += sz_v49;
        free(cm6);
        
        /* Декодируем значения 10-31 */
        uint8_t *v1031 = malloc(n1031 + 1);
        CtxModel *cm22 = malloc(sizeof(CtxModel));
        cm_init(cm22, 22);
        rc_dec_init(&rc, in_data + pos);
        ctx = 0;
        for (size_t i = 0; i < n1031; i++) {
            v1031[i] = rc_dec_sym_ctx(&rc, cm22, ctx);
            ctx = v1031[i];
        }
        pos += sz_v1031;
        free(cm22);
        
        /* Декодируем значения 32+ */
        uint8_t *v32 = malloc(n32 + 1);
        CtxModel *cm224 = malloc(sizeof(CtxModel));
        cm_init(cm224, 224);
        rc_dec_init(&rc, in_data + pos);
        ctx = 0;
        for (size_t i = 0; i < n32; i++) {
            v32[i] = rc_dec_sym_ctx(&rc, cm224, ctx);
            ctx = v32[i];
        }
        free(cm224);
        
        /* Восстанавливаем RLE данные */
        uint8_t *rle_data = malloc(data_len);
        size_t i13 = 0, i49 = 0, i1031 = 0, i32 = 0;
        for (size_t i = 0; i < data_len; i++) {
            switch (types[i]) {
                case 0: rle_data[i] = 0; break;
                case 1: rle_data[i] = v13[i13++] + 1; break;
                case 2: rle_data[i] = v49[i49++] + 4; break;
                case 3: rle_data[i] = v1031[i1031++] + 10; break;
                case 4: rle_data[i] = v32[i32++] + 32; break;
            }
        }
        
        /* RLE декодирование */
        size_t mtf_pos = 0;
        size_t run_idx = 0;
        for (size_t i = 0; i < data_len; i++) {
            if (rle_data[i] == 0) {
                uint32_t run = runs[run_idx++];
                for (uint32_t j = 0; j < run; j++) {
                    mtf[mtf_pos++] = 0;
                }
            } else {
                mtf[mtf_pos++] = rle_data[i];
            }
        }
        
        free(types); free(runs); free(rle_data);
        free(v13); free(v49); free(v1031); free(v32);
    } else {
        CtxModel *cm256 = malloc(sizeof(CtxModel));
        cm_init(cm256, 256);
        rc_dec_init(&rc, in_data + pos);
        int ctx = 0;
        for (size_t i = 0; i < orig; i++) {
            mtf[i] = rc_dec_sym_ctx(&rc, cm256, ctx);
            ctx = mtf[i];
        }
        free(cm256);
    }
    #undef R32
    
    /* MTF decode */
    uint8_t *bwt_out = malloc(orig);
    mtf_decode(mtf, orig, bwt_out);
    free(mtf);
    
    /* BWT decode */
    uint8_t *out = malloc(orig);
    bwt_decode(bwt_out, orig, bwt_idx, out);
    free(bwt_out);
    
    uint32_t calc_crc = calc_crc32(out, orig);
    printf("CRC: %08X vs %08X %s\n", stored_crc, calc_crc, stored_crc==calc_crc?"OK":"FAIL");
    
    FILE *fout = fopen(out_path, "wb");
    fwrite(out, 1, orig, fout);
    fclose(fout);
    
    printf("Распаковано: %u\n", orig);
    
    free(in_data); free(out);
    return stored_crc == calc_crc ? 0 : 1;
}

int main(int argc, char **argv) {
    init_crc32();
    if (argc < 4) {
        printf("Kolibri v34 - RLE + Fractal + Optimal Alphabets\n");
        printf("Usage: %s compress|decompress in out\n", argv[0]);
        return 1;
    }
    return strcmp(argv[1], "compress") == 0 ? 
           compress_file(argv[2], argv[3]) : 
           decompress_file(argv[2], argv[3]);
}
