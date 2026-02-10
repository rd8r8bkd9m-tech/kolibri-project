/*
 * Kolibri OS Archiver - Compression Implementation
 * Multi-layer compression with mathematical analysis
 */

#define _GNU_SOURCE  /* CLOCK_MONOTONIC */
#include "kolibri/compress.h"
#include "kolibri/huffman_ans.h"
#include "kolibri/random.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <math.h>

/* v62: SIMD и многопоточность */
#if defined(__SSE2__)
#include <emmintrin.h>
#define KF_USE_SIMD 1
#else
#define KF_USE_SIMD 0
#endif
#ifdef __linux__
#include <pthread.h>
#define KF_USE_THREADS 1
#else
#define KF_USE_THREADS 0
#endif

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

/* Magic number for compressed data format */
#define KOLIBRI_COMPRESS_MAGIC 0x4B4C4252 /* "KLBR" */
#define KOLIBRI_COMPRESS_VERSION 63  /* v63: TURBO LZ default, KOLIBRI_QUALITY=max for CM */

/* Compression header */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t methods;
    uint32_t original_size;
    uint32_t compressed_size;
    uint32_t checksum;
    KolibriFileType file_type;
    uint8_t reserved[12];
} KolibriCompressHeader;

struct KolibriCompressor {
    uint32_t methods;
    uint8_t *temp_buffer;
    size_t temp_buffer_size;
};

/* Internal helper functions */
static double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

/* Checksum implementation (CRC32) */
static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba,
    0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
    0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
    0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
    0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de,
    0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec,
    0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
    0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
    0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
    0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940,
    0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116,
    0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
    0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
    0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a,
    0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818,
    0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
    0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
    0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
    0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c,
    0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2,
    0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
    0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
    0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
    0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086,
    0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4,
    0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
    0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
    0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
    0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8,
    0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe,
    0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
    0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
    0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
    0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252,
    0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60,
    0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
    0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
    0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04,
    0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a,
    0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
    0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
    0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
    0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e,
    0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c,
    0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
    0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
    0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
    0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0,
    0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6,
    0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
    0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
    0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

uint32_t kolibri_checksum(const uint8_t *data, size_t size) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < size; i++) {
        uint8_t index = (crc ^ data[i]) & 0xFF;
        crc = (crc >> 8) ^ crc32_table[index];
    }
    return crc ^ 0xFFFFFFFF;
}

/* File type detection */
KolibriFileType kolibri_detect_file_type(const uint8_t *data, size_t size) {
    if (!data || size < 4) {
        return KOLIBRI_FILE_UNKNOWN;
    }

    /* Check for common image formats */
    if (size >= 2 && data[0] == 0xFF && data[1] == 0xD8) {
        return KOLIBRI_FILE_IMAGE; /* JPEG */
    }
    if (size >= 4 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') {
        return KOLIBRI_FILE_IMAGE; /* PNG */
    }
    if (size >= 3 && data[0] == 'G' && data[1] == 'I' && data[2] == 'F') {
        return KOLIBRI_FILE_IMAGE; /* GIF */
    }

    /* Check if text (UTF-8 compatible) */
    size_t text_chars = 0;
    size_t check_size = MIN(size, 512);
    for (size_t i = 0; i < check_size; i++) {
        uint8_t c = data[i];
        if ((c >= 32 && c <= 126) || c == '\n' || c == '\r' || c == '\t') {
            text_chars++;
        }
    }

    if (text_chars > check_size * 0.9) {
        return KOLIBRI_FILE_TEXT;
    }

    return KOLIBRI_FILE_BINARY;
}

/* RLE decompression */
static size_t decompress_rle(const uint8_t *input, size_t input_size,
                             uint8_t *output, size_t output_size) {
    size_t in_pos = 0;
    size_t out_pos = 0;

    while (in_pos < input_size) {
        if (input[in_pos] == 0xFF && in_pos + 1 < input_size) {
            if (input[in_pos + 1] == 0x00) {
                /* Escaped literal 0xFF */
                if (out_pos >= output_size) return 0;
                output[out_pos++] = 0xFF;
                in_pos += 2;
            } else if (in_pos + 2 < input_size) {
                /* RLE sequence */
                uint8_t count = input[in_pos + 1];
                uint8_t value = input[in_pos + 2];
                
                for (uint8_t i = 0; i < count; i++) {
                    if (out_pos >= output_size) return 0;
                    output[out_pos++] = value;
                }
                in_pos += 3;
            } else {
                /* Incomplete sequence at end */
                return 0;
            }
        } else {
            /* Literal byte */
            if (out_pos >= output_size) return 0;
            output[out_pos++] = input[in_pos++];
        }
    }

    return out_pos;
}


/* LZ77 decompression */
static size_t decompress_lz77(const uint8_t *input, size_t input_size,
                              uint8_t *output, size_t output_size) {
    size_t in_pos = 0;
    size_t out_pos = 0;

    while (in_pos < input_size) {
        if (input[in_pos] == 0xFE && in_pos + 1 < input_size) {
            if (input[in_pos + 1] == 0xFF) {
                /* Escaped literal 0xFE (marker followed by 0xFF) */
                if (out_pos >= output_size) return 0;
                output[out_pos++] = 0xFE;
                in_pos += 2;
            } else if (in_pos + 3 < input_size) {
                /* LZ77 sequence - distance is 2 bytes */
                size_t distance = ((size_t)input[in_pos + 1] << 8) | (size_t)input[in_pos + 2];
                size_t length = input[in_pos + 3];
                
                if (out_pos < distance || distance == 0) return 0;
                
                size_t copy_pos = out_pos - distance;
                for (size_t i = 0; i < length; i++) {
                    if (out_pos >= output_size) return 0;
                    output[out_pos++] = output[copy_pos++];
                }
                in_pos += 4;
            } else {
                /* Incomplete sequence */
                return 0;
            }
        } else {
            /* Literal byte */
            if (out_pos >= output_size) return 0;
            output[out_pos++] = input[in_pos++];
        }
    }

    return out_pos;
}

/* Mathematical pattern analysis for additional compression */
/* Supported Formulas:
 * 0: RAW (No change)
 * 1: DELTA_1 (x[i] - x[i-1]) - Linear simple
 * 2: DELTA_2 (x[i] - 2*x[i-1] + x[i-2]) - Linear progressive
 * 3: STRIDE_4 (x[i] - x[i-4]) - 32-bit integers / structs
 * 4: GENERATOR (Procedural Generation from Seed) - "Genome Formula"
 * 5: GENERATOR_CONTINUE (Continue from previous block)
 * 6: GENERATOR_APPROX (Seed + Delta)
 * 8: BWT+MTF (Burrows-Wheeler Transform + Move-to-Front)
 * 9: CONTEXT_PRED (Order-2 context prediction - Kolibri AI)
 * 10: BWT+MTF+CONTEXT (Combined: BWT + MTF + Context prediction)
 */

/* ===================== BWT (Burrows-Wheeler Transform) ===================== */
/* BWT Inverse Transform: Восстановление исходных данных из BWT */
static int bwt_inverse(const uint8_t *L, size_t len, uint8_t *output, uint32_t primary_index) {
    if (!L || !output || primary_index >= (uint32_t)len) return -1;
    
    /* Подсчёт частот */
    uint32_t count[256] = {0};
    for (size_t i = 0; i < len; i++) count[L[i]]++;
    
    /* Кумулятивные частоты */
    uint32_t cumul[256];
    uint32_t sum = 0;
    for (int c = 0; c < 256; c++) {
        cumul[c] = sum;
        sum += count[c];
    }
    
    /* Строим LF-mapping */
    uint32_t *LF = (uint32_t *)malloc(len * sizeof(uint32_t));
    if (!LF) return -1;
    
    uint32_t running[256];
    memcpy(running, cumul, sizeof(cumul));
    for (size_t i = 0; i < len; i++) {
        LF[i] = running[L[i]]++;
    }
    
    /* Восстанавливаем строку обратным обходом цепочки */
    uint32_t idx = primary_index;
    for (size_t i = len; i > 0; i--) {
        output[i - 1] = L[idx];
        idx = LF[idx];
    }
    
    free(LF);
    return 0;
}

/* ===================== MTF (Move-to-Front Transform) ===================== */

/* MTF Decode: Обратное преобразование рангов в байты */
static void mtf_decode(uint8_t *data, size_t len) {
    uint8_t table[256];
    for (int i = 0; i < 256; i++) table[i] = (uint8_t)i;
    
    for (size_t i = 0; i < len; i++) {
        uint8_t rank = data[i];
        uint8_t ch = table[rank];
        
        data[i] = ch;
        memmove(table + 1, table, rank);
        table[0] = ch;
    }
}

/* ===================== Context Prediction v50 (Kolibri AI) ===================== */
/* 
 * v50: \u0421\u043c\u0435\u0448\u0430\u043d\u043d\u043e\u0435 \u043f\u0440\u0435\u0434\u0441\u043a\u0430\u0437\u0430\u043d\u0438\u0435 Order-1 + Order-2 + \u0445\u0435\u0448\u0438\u0440\u043e\u0432\u0430\u043d\u043d\u044b\u0439 Order-3.
 * \u041a\u0430\u0436\u0434\u044b\u0439 \u043a\u043e\u043d\u0442\u0435\u043a\u0441\u0442 \u0440\u0430\u0431\u043e\u0442\u0430\u0435\u0442 \u043d\u0435\u0437\u0430\u0432\u0438\u0441\u0438\u043c\u043e \u2014 \u043f\u0440\u0435\u0434\u0441\u043a\u0430\u0437\u0430\u043d\u0438\u044f \u0441\u043c\u0435\u0448\u0438\u0432\u0430\u044e\u0442\u0441\u044f \u0441 \u0432\u0435\u0441\u0430\u043c\u0438.
 * Order-3 \u043f\u0440\u0435\u0434\u0441\u043a\u0430\u0437\u044b\u0432\u0430\u0435\u0442 \u043b\u0443\u0447\u0448\u0435 \u043d\u0430 \u0441\u0442\u0440\u0443\u043a\u0442\u0443\u0440\u0438\u0440\u043e\u0432\u0430\u043d\u043d\u044b\u0445 \u0434\u0430\u043d\u043d\u044b\u0445,
 * \u043d\u043e Order-1/2 \u043e\u0441\u0442\u0430\u044e\u0442\u0441\u044f \u043d\u0430\u0434\u0451\u0436\u043d\u044b\u043c fallback.
 */

#define CTX_TABLE_SIZE 65536  /* 256 * 256 = Order-2 context */
#define CTX3_TABLE_SIZE 262144 /* 2^18 = \u0445\u0435\u0448\u0438\u0440\u043e\u0432\u0430\u043d\u043d\u044b\u0439 Order-3 context */
#define CTX3_MASK (CTX3_TABLE_SIZE - 1)

static void context_predict_decode(const uint8_t *input, size_t len, uint8_t *output) {
    uint8_t *pred1 = (uint8_t *)calloc(256, 1);
    uint8_t *pred2 = (uint8_t *)calloc(CTX_TABLE_SIZE, 1);
    uint8_t *pred3 = (uint8_t *)calloc(CTX3_TABLE_SIZE, 1);
    uint8_t *hit1  = (uint8_t *)calloc(256, 1);
    uint8_t *hit2  = (uint8_t *)calloc(CTX_TABLE_SIZE, 1);
    uint8_t *hit3  = (uint8_t *)calloc(CTX3_TABLE_SIZE, 1);
    
    if (!pred1 || !pred2 || !pred3 || !hit1 || !hit2 || !hit3) {
        memcpy(output, input, len);
        free(pred1); free(pred2); free(pred3);
        free(hit1); free(hit2); free(hit3);
        return;
    }
    
    if (len > 0) output[0] = input[0];
    if (len > 1) output[1] = input[1];
    if (len > 2) output[2] = input[2];
    
    for (size_t i = 3; i < len; i++) {
        uint8_t ctx1_key = output[i-1];
        uint16_t ctx2_key = ((uint16_t)output[i-2] << 8) | (uint16_t)output[i-1];
        uint32_t ctx3_key = (((uint32_t)output[i-3] * 65599U) ^
                             ((uint32_t)output[i-2] * 257U) ^
                             (uint32_t)output[i-1]) & CTX3_MASK;
        
        int p1 = (int)pred1[ctx1_key];
        int p2 = (int)pred2[ctx2_key];
        int p3 = (int)pred3[ctx3_key];
        int h1 = (int)hit1[ctx1_key];
        int h2 = (int)hit2[ctx2_key];
        int h3 = (int)hit3[ctx3_key];
        
        int w1 = 1 + h1;
        int w2 = 2 + h2 * 2;
        int w3 = 3 + h3 * 3;
        int total_w = w1 + w2 + w3;
        int predicted = (p1 * w1 + p2 * w2 + p3 * w3 + total_w / 2) / total_w;
        predicted = predicted & 0xFF;
        
        /* \u0412\u043e\u0441\u0441\u0442\u0430\u043d\u0430\u0432\u043b\u0438\u0432\u0430\u0435\u043c: real = predicted + residual */
        output[i] = (uint8_t)predicted + input[i];
        
        uint8_t actual = output[i];
        if (pred1[ctx1_key] == actual && hit1[ctx1_key] < 255) hit1[ctx1_key]++;
        else { hit1[ctx1_key] = 0; }
        if (pred2[ctx2_key] == actual && hit2[ctx2_key] < 255) hit2[ctx2_key]++;
        else { hit2[ctx2_key] = 0; }
        if (pred3[ctx3_key] == actual && hit3[ctx3_key] < 255) hit3[ctx3_key]++;
        else { hit3[ctx3_key] = 0; }
        
        pred1[ctx1_key] = actual;
        pred2[ctx2_key] = actual;
        pred3[ctx3_key] = actual;
    }
    
    free(pred1); free(pred2); free(pred3);
    free(hit1); free(hit2); free(hit3);
}

/* =====================================================================
 * KOLIBRI FORMULA PREDICTOR — «Number-Thinking» Compression
 * =====================================================================
 * Компактная нейроподобная формула (вдохновлённая KolibriFormula/ResNet)
 * предсказывает байты данных из позиционного контекста.
 *
 * Философия: Данные = Формула + Минимальная коррекция
 *
 * Формула (геном): 48 цифр (0-9), кодирующих 6 «ReasonBlock» слоёв.
 * Каждый слой: [op:1][slope_hi:1][slope_lo:1][bias_hi:1][bias_lo:1]
 *              [aux_hi:1][aux_lo:1][skip_weight:1] = 8 цифр
 *
 * Хранение: 24 байта (packed, 4 бита на цифру) — ультракомпактно!
 * Для блока 65KB: 24 байта формулы vs 65536 байт данных.
 * =====================================================================*/
#define KF_GENE_LEN      64   /* Длина генома (цифры 0-9): 8 слоёв × 8 цифр */
#define KF_GENE_PACKED    32   /* Упакованный размер (байты) */
#define KF_LAYERS          8   /* Количество слоёв-ReasonBlock'ов */

typedef struct {
    uint8_t digits[KF_GENE_LEN];
    double fitness;
} KolibriCompactFormula;

/* --- Контекстно-зависимое предсказание: формула получает предыдущий байт ---
 * Философия: после BWT+MTF, каждый байт сильно зависит от предыдущего.
 * Если prev=0 → скорее всего следующий тоже 0 (серия нулей).
 * Если prev=маленький → скорее всего следующий = 0 (возврат в серию).
 * Формула учится этим переходам за 8 слоёв ReasonBlock'ов.
 */
static int kf_compact_predict(const KolibriCompactFormula *f, int prev_byte) {
    long long x = (long long)(prev_byte & 0xFF);
    
    for (int layer = 0; layer < KF_LAYERS; layer++) {
        int off = layer * 8;
        int op    = f->digits[off] % 6;
        int slope = (int)f->digits[off+1] * 10 + (int)f->digits[off+2] - 50;
        int bias  = (int)f->digits[off+3] * 100 + (int)f->digits[off+4] * 10 
                    + (int)f->digits[off+5] - 500;
        int aux   = (int)f->digits[off+6] * 10 + (int)f->digits[off+7];
        
        long long residual = x;
        long long result;
        
        switch (op) {
            case 0: /* Линейная: slope*x/100 + bias */
                result = (long long)slope * x / 100 + bias; 
                break;
            case 1: /* Пороговая: if |x| < aux then bias, else slope*x/100+bias */
                { long long ax = x < 0 ? -x : x;
                  result = (ax < (long long)aux) ? (long long)bias 
                           : ((long long)slope * x / 100 + bias); }
                break;
            case 2: /* Модульная: (slope*x) % (aux+1) + bias */
                { long long d = (long long)(aux > 0 ? aux : 1);
                  result = ((long long)slope * x / 100) % d + bias; }
                break;
            case 3: /* Мягкая квадратичная: slope*x/(100+|x|) + bias */
                { long long ax = x < 0 ? -x : x;
                  result = (long long)slope * x / (100 + ax) + bias; }
                break;
            case 4: /* Shift: (x >> (aux&3)) + bias */
                result = (x >> ((unsigned)aux & 3)) + bias; 
                break;
            case 5: /* Масштабирование: x * aux / 100 + bias */
                result = x * (long long)aux / 100 + bias;
                break;
            default:
                result = x + bias; 
                break;
        }
        
        /* Клиппинг + Residual Connection */
        if (result > 512LL) result = 512LL;
        if (result < -512LL) result = -512LL;
        x = result + residual;
        if (x > 512LL) x = 512LL;
        if (x < -512LL) x = -512LL;
    }
    
    return (int)(((x % 256) + 256) % 256);
}

/* --- Распаковка генома: 32 байта → 64 цифры --- */
static void kf_unpack_gene(const uint8_t *packed, uint8_t *digits) {
    for (int i = 0; i < KF_GENE_PACKED; i++) {
        digits[i * 2]     = (packed[i] >> 4) & 0x0F;
        digits[i * 2 + 1] = packed[i] & 0x0F;
    }
}

/* --- Секвенциальное декодирование: контекст = предыдущий ДЕКОДИРОВАННЫЙ байт --- */
static void kf_formula_decode_block(const KolibriCompactFormula *formula,
                                     const uint8_t *residuals, size_t len,
                                     uint8_t *output) {
    int prev = 0;
    for (size_t i = 0; i < len; i++) {
        int predicted = kf_compact_predict(formula, prev);
        output[i] = (uint8_t)((int)residuals[i] + predicted);
        prev = (int)output[i]; /* Следующий контекст = текущий ДЕКОДИРОВАННЫЙ байт */
    }
}

#define MATH_BLOCK_SIZE_V40 65536
#define MATH_BLOCK_SIZE    900000  /* v50: 900KB блоки (= bzip2) */

/* ===================== ZRLE (Zero-Run-Length Encoding) v50 ===================== */
/* После BWT+MTF данные содержат множество нулей. ZRLE кодирует серии эффективно:
 * - Ненулевой байт: [byte] (1 байт)
 * - Серия 1..255 нулей:   [0x00] [N-1]       (2 байта)
 * - Серия 256..65535 нулей: [0x00] [0xFF] [hi] [lo] (4 байта)
 * v50: поддержка длинных серий для больших BWT-блоков (512KB+).
 */


static size_t zrle_decode(const uint8_t *input, size_t compressed_len, uint8_t *output, size_t max_out) {
    size_t in_pos = 0, out_pos = 0;
    while (in_pos < compressed_len) {
        if (input[in_pos] == 0) {
            if (in_pos + 1 >= compressed_len) return 0;
            size_t run;
            if (input[in_pos + 1] == 0xFF && in_pos + 3 < compressed_len) {
                /* Длинная серия: [0x00][0xFF][hi][lo], run = hi*256+lo */
                run = ((size_t)input[in_pos + 2] << 8) | input[in_pos + 3];
                if (run == 0) return 0;  /* invalid */
                in_pos += 4;
            } else {
                /* Короткая серия */
                run = (size_t)input[in_pos + 1] + 1;
                in_pos += 2;
            }
            if (out_pos + run > max_out) return 0;
            memset(output + out_pos, 0, run);
            out_pos += run;
        } else {
            if (out_pos >= max_out) return 0;
            output[out_pos++] = input[in_pos++];
        }
    }
    return out_pos;
}
/* Mathematical pattern decompression */
static size_t decompress_mathematical(const uint8_t *input, size_t input_size,
                                     uint8_t *output, size_t output_size,
                                     uint32_t version) {
    
    /* v50: \u043f\u043e\u0434\u0434\u0435\u0440\u0436\u043a\u0430 128KB \u0431\u043b\u043e\u043a\u043e\u0432 (v40 \u0438\u0441\u043f\u043e\u043b\u044c\u0437\u043e\u0432\u0430\u043b 64KB) */
    size_t block_size = (version >= 50) ? MATH_BLOCK_SIZE : MATH_BLOCK_SIZE_V40;
    
    size_t in_pos = 0;
    size_t out_processed = 0;

    /* Generator Persistence */
    KolibriRng current_rng; /* State is maintained across blocks if Method 5 is used */

    while (in_pos < input_size && out_processed < output_size) {
        if (in_pos >= input_size) break;
        
        uint8_t method = input[in_pos++];
        size_t remaining_out = output_size - out_processed;
        size_t block_len = MIN(block_size, remaining_out);
        
        /* If this is the last block, it might be smaller than MATH_BLOCK_SIZE. 
           We know exact output size, so we can clamp.
        */
        
        uint8_t *out_blk = output + out_processed;

        if (method == 0) {
            if (in_pos + block_len > input_size) return 0; 
            memcpy(out_blk, input + in_pos, block_len);
            in_pos += block_len;
        } else if (method == 1) {
            if (in_pos + block_len > input_size) return 0;
            const uint8_t *data = input + in_pos;
            out_blk[0] = data[0];
            for (size_t i = 1; i < block_len; i++) {
                out_blk[i] = out_blk[i-1] + data[i];
            }
            in_pos += block_len;
        } else if (method == 2) {
            if (in_pos + block_len > input_size) return 0;
            const uint8_t *data = input + in_pos; 
            out_blk[0] = data[0];
            out_blk[1] = data[1];
            for (size_t i = 2; i < block_len; i++) {
                out_blk[i] = 2*out_blk[i-1] - out_blk[i-2] + data[i];
            }
            in_pos += block_len;
        } else if (method == 3) {
             if (in_pos + block_len > input_size) return 0;
             const uint8_t *data = input + in_pos;
             memcpy(out_blk, data, 4);
             for (size_t i = 4; i < block_len; i++) {
                 out_blk[i] = out_blk[i-4] + data[i];
             }
             in_pos += block_len;
        } else if (method == 4) {
             /* GENERATOR RECOVERY - INIT */
             if (in_pos + 2 > input_size) {
                 /* Not enough bytes for seed */
                 return 0;
             }
             uint16_t seed = (uint16_t)input[in_pos] | ((uint16_t)input[in_pos+1] << 8);
             in_pos += 2;
             
             k_rng_seed(&current_rng, seed);
             for(size_t i=0; i<block_len; i++) {
                 out_blk[i] = (uint8_t)(k_rng_next(&current_rng) % 256);
             }
        } else if (method == 5) {
             /* GENERATOR RECOVERY - CONTINUE */
             /* Uses current_rng state from previous block */
             for(size_t i=0; i<block_len; i++) {
                 out_blk[i] = (uint8_t)(k_rng_next(&current_rng) % 256);
             }
        } else if (method == 6) {
             /* GENERATOR APPROXIMATION - DELTA */
             if (in_pos + 2 + block_len > input_size) {
                 return 0;
             }
             uint16_t seed = (uint16_t)input[in_pos] | ((uint16_t)input[in_pos+1] << 8);
             in_pos += 2;
             
             KolibriRng rng;
             k_rng_seed(&rng, seed);
             const uint8_t *deltas = input + in_pos;
             
             for(size_t i=0; i<block_len; i++) {
                 uint8_t gen = (uint8_t)(k_rng_next(&rng) % 256);
                 /* Restore: Real = Generator + Delta */
                 out_blk[i] = gen + deltas[i];
             }
             in_pos += block_len;
             
             /* Update continuing seed state for potential next block usage */
             KolibriRng tmp; k_rng_seed(&tmp, seed);
             for(size_t k=0; k<block_len; k++) k_rng_next(&tmp);
             current_rng = tmp;
             
        } else if (method == 8) {
             /* BWT + MTF + ZRLE INVERSE */
             if (in_pos + 7 > input_size) return 0;
             
             uint32_t bwt_primary_d = (uint32_t)input[in_pos]
                                  | ((uint32_t)input[in_pos+1] << 8)
                                  | ((uint32_t)input[in_pos+2] << 16)
                                  | ((uint32_t)input[in_pos+3] << 24);
             in_pos += 4;
             
             /* Читаем размер ZRLE данных */
             uint32_t zrle_size = (uint32_t)input[in_pos]
                               | ((uint32_t)input[in_pos+1] << 8)
                               | ((uint32_t)input[in_pos+2] << 16);
             in_pos += 3;
             
             uint8_t *temp = (uint8_t *)malloc(block_len);
             if (!temp) return 0;
             
             if (zrle_size == 0) {
                 /* Raw MTF данные (ZRLE не использовалась) */
                 if (in_pos + block_len > input_size) { free(temp); return 0; }
                 memcpy(temp, input + in_pos, block_len);
                 in_pos += block_len;
             } else {
                 /* ZRLE декодирование */
                 if (in_pos + zrle_size > input_size) {
                     free(temp); return 0;
                 }
                 size_t decoded = zrle_decode(input + in_pos, zrle_size, temp, block_len);
                 if (decoded != block_len) { free(temp); return 0; }
                 in_pos += zrle_size;
             }
             
             mtf_decode(temp, block_len);
             if (bwt_inverse(temp, block_len, out_blk, bwt_primary_d) != 0) {
                 free(temp);
                 return 0;
             }
             free(temp);
             
        } else if (method == 9) {
             /* CONTEXT PREDICTION + ZRLE INVERSE */
             if (in_pos + 3 > input_size) return 0;
             
             uint32_t zrle_size = (uint32_t)input[in_pos]
                               | ((uint32_t)input[in_pos+1] << 8)
                               | ((uint32_t)input[in_pos+2] << 16);
             in_pos += 3;
             
             uint8_t *temp = (uint8_t *)malloc(block_len);
             if (!temp) return 0;
             
             if (zrle_size == 0) {
                 if (in_pos + block_len > input_size) { free(temp); return 0; }
                 context_predict_decode(input + in_pos, block_len, out_blk);
                 in_pos += block_len;
             } else {
                 if (in_pos + zrle_size > input_size) { free(temp); return 0; }
                 size_t decoded = zrle_decode(input + in_pos, zrle_size, temp, block_len);
                 if (decoded != block_len) { free(temp); return 0; }
                 in_pos += zrle_size;
                 context_predict_decode(temp, block_len, out_blk);
             }
             free(temp);
             
        } else if (method == 10) {
             /* BWT + MTF + CONTEXT + ZRLE INVERSE */
             if (in_pos + 7 > input_size) return 0;
             
             uint32_t bwt_primary_d = (uint32_t)input[in_pos]
                                  | ((uint32_t)input[in_pos+1] << 8)
                                  | ((uint32_t)input[in_pos+2] << 16)
                                  | ((uint32_t)input[in_pos+3] << 24);
             in_pos += 4;
             
             uint32_t zrle_size = (uint32_t)input[in_pos]
                               | ((uint32_t)input[in_pos+1] << 8)
                               | ((uint32_t)input[in_pos+2] << 16);
             in_pos += 3;
             
             uint8_t *temp = (uint8_t *)malloc(block_len);
             if (!temp) return 0;
             
             if (zrle_size == 0) {
                 if (in_pos + block_len > input_size) { free(temp); return 0; }
                 memcpy(temp, input + in_pos, block_len);
                 in_pos += block_len;
             } else {
                 if (in_pos + zrle_size > input_size) { free(temp); return 0; }
                 size_t decoded = zrle_decode(input + in_pos, zrle_size, temp, block_len);
                 if (decoded != block_len) { free(temp); return 0; }
                 in_pos += zrle_size;
             }
             
             /* Inverse Context → MTF → BWT */
             uint8_t *temp2_d = (uint8_t *)malloc(block_len);
             if (!temp2_d) { free(temp); return 0; }
             context_predict_decode(temp, block_len, temp2_d);
             mtf_decode(temp2_d, block_len);
             if (bwt_inverse(temp2_d, block_len, out_blk, bwt_primary_d) != 0) {
                 free(temp); free(temp2_d);
                 return 0;
             }
             free(temp);
             free(temp2_d);
             
        } else if (method == 11) {
             /* BWT + MTF + FORMULA PREDICTION + ZRLE INVERSE
              * Формат: [BWT_primary:4] [gene_packed:32] [zrle_len:3] [zrle_data...] */
             if (in_pos + 4 + KF_GENE_PACKED + 3 > input_size) return 0;
             
             uint32_t bwt_primary_d = (uint32_t)input[in_pos]
                                  | ((uint32_t)input[in_pos+1] << 8)
                                  | ((uint32_t)input[in_pos+2] << 16)
                                  | ((uint32_t)input[in_pos+3] << 24);
             in_pos += 4;
             
             /* Распаковываем геном формулы: 32 байта → 64 цифры */
             KolibriCompactFormula formula_d;
             kf_unpack_gene(input + in_pos, formula_d.digits);
             in_pos += KF_GENE_PACKED;
             
             /* ZRLE размер остатков */
             uint32_t zrle_size = (uint32_t)input[in_pos]
                               | ((uint32_t)input[in_pos+1] << 8)
                               | ((uint32_t)input[in_pos+2] << 16);
             in_pos += 3;
             
             /* Декодируем ZRLE → остатки (residuals) */
             uint8_t *residuals = (uint8_t *)malloc(block_len);
             if (!residuals) return 0;
             
             if (zrle_size == 0) {
                 if (in_pos + block_len > input_size) { free(residuals); return 0; }
                 memcpy(residuals, input + in_pos, block_len);
                 in_pos += block_len;
             } else {
                 if (in_pos + zrle_size > input_size) { free(residuals); return 0; }
                 size_t decoded = zrle_decode(input + in_pos, zrle_size, residuals, block_len);
                 if (decoded != block_len) { free(residuals); return 0; }
                 in_pos += zrle_size;
             }
             
             /* Восстанавливаем BWT+MTF данные: data[i] = predict(prev) + residual[i] */
             uint8_t *mtf_data = (uint8_t *)malloc(block_len);
             if (!mtf_data) { free(residuals); return 0; }
             
             kf_formula_decode_block(&formula_d, residuals, block_len, mtf_data);
             free(residuals);
             
             /* Inverse MTF → BWT */
             mtf_decode(mtf_data, block_len);
             if (bwt_inverse(mtf_data, block_len, out_blk, bwt_primary_d) != 0) {
                 free(mtf_data);
                 return 0;
             }
             free(mtf_data);
             
        } else if (method == 12) {
             /* PURE FORMULA + ZRLE INVERSE (без BWT!)
              * Формат: [gene_packed:32] [zrle_len:3] [zrle_data...] */
             if (in_pos + KF_GENE_PACKED + 3 > input_size) return 0;
             
             KolibriCompactFormula formula_d;
             kf_unpack_gene(input + in_pos, formula_d.digits);
             in_pos += KF_GENE_PACKED;
             
             uint32_t zrle_size = (uint32_t)input[in_pos]
                               | ((uint32_t)input[in_pos+1] << 8)
                               | ((uint32_t)input[in_pos+2] << 16);
             in_pos += 3;
             
             uint8_t *residuals = (uint8_t *)malloc(block_len);
             if (!residuals) return 0;
             
             if (zrle_size == 0) {
                 if (in_pos + block_len > input_size) { free(residuals); return 0; }
                 memcpy(residuals, input + in_pos, block_len);
                 in_pos += block_len;
             } else {
                 if (in_pos + zrle_size > input_size) { free(residuals); return 0; }
                 size_t decoded = zrle_decode(input + in_pos, zrle_size, residuals, block_len);
                 if (decoded != block_len) { free(residuals); return 0; }
                 in_pos += zrle_size;
             }
             
             /* Восстанавливаем данные напрямую: data[i] = predict(prev) + residual[i] */
             kf_formula_decode_block(&formula_d, residuals, block_len, out_blk);
             free(residuals);
             
        } else {
            return 0; /* Unknown method */
        }

        
        out_processed += block_len;
    }

    return out_processed;
}

/* Public API implementation */
KolibriCompressor *kolibri_compressor_create(uint32_t methods) {
    KolibriCompressor *comp = (KolibriCompressor *)calloc(1, sizeof(KolibriCompressor));
    if (!comp) return NULL;

    comp->methods = methods ? methods : KOLIBRI_COMPRESS_ALL;
    comp->temp_buffer = NULL;
    comp->temp_buffer_size = 0;

    return comp;
}

void kolibri_compressor_destroy(KolibriCompressor *comp) {
    if (!comp) return;
    free(comp->temp_buffer);
    free(comp);
}


/* ====================================================================
 * TOKEN-LEVEL TEXT STREAM (v52)
 * ====================================================================
 * Простая токенизация текста:
 *   - Слова (A-Za-z0-9_) кодируются как токены по словарю
 *   - Остальные байты идут как есть
 *   - Escape 0xFE используется для меток токенов
 * Формат токенов в потоке:
 *   0xFE 0x00           -> literal 0xFE
 *   0xFE 0x01 <id>      -> token dictionary word
 */
#define TOKEN_ESCAPE 0xFE
#define TOKEN_LITERAL 0x00
#define TOKEN_DICT_MARK 0x01

#define TOKEN_DICT_MAX 256
#define TOKEN_COLLECT_MAX 2048
#define TOKEN_HASH_SIZE 4096
#define TOKEN_MIN_LEN 2
#define TOKEN_MAX_LEN 32

typedef struct {
    uint32_t hash;
    uint16_t len;
    uint32_t count;
    uint32_t str_off;
} TokenEntry;

typedef struct {
    TokenEntry entries[TOKEN_COLLECT_MAX];
    int32_t table[TOKEN_HASH_SIZE];
    uint32_t entry_count;
    uint8_t *blob;
    uint32_t blob_size;
    uint32_t blob_cap;
} TokenCollector;

typedef struct {
    uint16_t count;
    uint16_t len[TOKEN_DICT_MAX];
    uint32_t off[TOKEN_DICT_MAX];
    uint8_t *blob;
    uint32_t blob_size;
} TokenDict;

typedef struct {
    uint32_t idx;
    uint32_t score;
} TokenScore;

static int token_score_cmp(const void *a, const void *b) {
    const TokenScore *x = (const TokenScore *)a;
    const TokenScore *y = (const TokenScore *)b;
    if (x->score < y->score) return 1;
    if (x->score > y->score) return -1;
    return 0;
}

static inline int token_is_word(uint8_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static inline uint32_t token_hash(const uint8_t *s, uint16_t len) {
    uint32_t h = 2166136261u;
    for (uint16_t i = 0; i < len; i++) h = (h ^ s[i]) * 16777619u;
    return h;
}

static void token_collector_init(TokenCollector *tc, size_t input_size) {
    tc->entry_count = 0;
    for (size_t i = 0; i < TOKEN_HASH_SIZE; i++) tc->table[i] = -1;
    tc->blob_cap = (uint32_t)MIN(input_size, (size_t)(1024 * 1024));
    tc->blob_size = 0;
    tc->blob = (uint8_t *)malloc(tc->blob_cap);
}

static void token_collector_free(TokenCollector *tc) {
    free(tc->blob);
    tc->blob = NULL;
}

static void token_collector_add(TokenCollector *tc, const uint8_t *word, uint16_t len) {
    if (len < TOKEN_MIN_LEN || len > TOKEN_MAX_LEN) return;
    if (tc->entry_count >= TOKEN_COLLECT_MAX || !tc->blob) return;
    uint32_t h = token_hash(word, len);
    uint32_t idx = h & (TOKEN_HASH_SIZE - 1);
    for (uint32_t step = 0; step < TOKEN_HASH_SIZE; step++) {
        int32_t slot = tc->table[idx];
        if (slot < 0) {
            if (tc->blob_size + len > tc->blob_cap) return;
            TokenEntry *e = &tc->entries[tc->entry_count];
            e->hash = h;
            e->len = len;
            e->count = 1;
            e->str_off = tc->blob_size;
            memcpy(tc->blob + tc->blob_size, word, len);
            tc->blob_size += len;
            tc->table[idx] = (int32_t)tc->entry_count;
            tc->entry_count++;
            return;
        }
        TokenEntry *e = &tc->entries[(uint32_t)slot];
        if (e->hash == h && e->len == len &&
            memcmp(tc->blob + e->str_off, word, len) == 0) {
            e->count++;
            return;
        }
        idx = (idx + 1) & (TOKEN_HASH_SIZE - 1);
    }
}

static int token_dict_build(TokenDict *dict, const uint8_t *input, size_t input_size) {
    TokenCollector tc;
    token_collector_init(&tc, input_size);
    if (!tc.blob) return 0;

    size_t i = 0;
    while (i < input_size) {
        if (token_is_word(input[i])) {
            size_t start = i;
            size_t len = 0;
            while (i < input_size && token_is_word(input[i]) && len < TOKEN_MAX_LEN) {
                i++;
                len++;
            }
            token_collector_add(&tc, input + start, (uint16_t)len);
        } else {
            i++;
        }
    }

    TokenScore *scores = (TokenScore *)malloc(tc.entry_count * sizeof(TokenScore));
    if (!scores) { token_collector_free(&tc); return 0; }

    uint32_t sc_count = 0;
    for (uint32_t k = 0; k < tc.entry_count; k++) {
        TokenEntry *e = &tc.entries[k];
        if (e->count >= 2 && e->len >= TOKEN_MIN_LEN && e->len <= TOKEN_MAX_LEN) {
            scores[sc_count].idx = k;
            scores[sc_count].score = e->count * e->len;
            sc_count++;
        }
    }

    if (sc_count == 0) {
        free(scores);
        token_collector_free(&tc);
        return 0;
    }

    qsort(scores, sc_count, sizeof(TokenScore), token_score_cmp);

    dict->count = 0;
    dict->blob_size = 0;
    dict->blob = (uint8_t *)malloc(tc.blob_size);
    if (!dict->blob) {
        free(scores);
        token_collector_free(&tc);
        return 0;
    }

    for (uint32_t k = 0; k < sc_count && dict->count < TOKEN_DICT_MAX; k++) {
        TokenEntry *e = &tc.entries[scores[k].idx];
        dict->len[dict->count] = e->len;
        dict->off[dict->count] = dict->blob_size;
        memcpy(dict->blob + dict->blob_size, tc.blob + e->str_off, e->len);
        dict->blob_size += e->len;
        dict->count++;
    }

    free(scores);
    token_collector_free(&tc);
    return dict->count > 0;
}


static void token_dict_free(TokenDict *dict) {
    free(dict->blob);
    dict->blob = NULL;
    dict->blob_size = 0;
    dict->count = 0;
}

static size_t token_dict_write(const TokenDict *dict, uint8_t *output, size_t output_max) {
    size_t need = 2;
    for (uint16_t i = 0; i < dict->count; i++) need += 1 + dict->len[i];
    if (need > output_max) return 0;
    output[0] = (uint8_t)(dict->count & 0xFF);
    output[1] = (uint8_t)((dict->count >> 8) & 0xFF);
    size_t pos = 2;
    for (uint16_t i = 0; i < dict->count; i++) {
        output[pos++] = (uint8_t)dict->len[i];
        memcpy(output + pos, dict->blob + dict->off[i], dict->len[i]);
        pos += dict->len[i];
    }
    return pos;
}

static int token_dict_read(const uint8_t *input, size_t input_size,
                           size_t *consumed, TokenDict *dict) {
    if (input_size < 2) return 0;
    uint16_t count = (uint16_t)input[0] | ((uint16_t)input[1] << 8);
    if (count > TOKEN_DICT_MAX) return 0;
    size_t pos = 2;
    uint32_t total = 0;
    for (uint16_t i = 0; i < count; i++) {
        if (pos >= input_size) return 0;
        uint8_t len = input[pos++];
        if (pos + len > input_size) return 0;
        total += len;
        pos += len;
    }
    dict->blob = (uint8_t *)malloc(total);
    if (!dict->blob) return 0;
    dict->count = count;
    dict->blob_size = total;
    pos = 2;
    uint32_t off = 0;
    for (uint16_t i = 0; i < count; i++) {
        uint8_t len = input[pos++];
        dict->len[i] = len;
        dict->off[i] = off;
        memcpy(dict->blob + off, input + pos, len);
        pos += len;
        off += len;
    }
    *consumed = pos;
    return 1;
}

static int token_dict_find(const TokenDict *dict, const uint8_t *word,
                           uint16_t len, uint8_t *out_id) {
    for (uint16_t i = 0; i < dict->count; i++) {
        if (dict->len[i] == len &&
            memcmp(dict->blob + dict->off[i], word, len) == 0) {
            *out_id = (uint8_t)i;
            return 1;
        }
    }
    return 0;
}

static size_t token_encode_text(const uint8_t *input, size_t input_size,
                                uint8_t *output, size_t output_max,
                                const TokenDict *dict) {
    size_t ip = 0;
    size_t op = 0;
    while (ip < input_size) {
        if (token_is_word(input[ip])) {
            size_t start = ip;
            size_t len = 0;
            while (ip < input_size && token_is_word(input[ip]) && len < 255) {
                ip++;
                len++;
            }
            uint8_t id = 0;
            if (len >= TOKEN_MIN_LEN && token_dict_find(dict, input + start, (uint16_t)len, &id)) {
                if (op + 3 > output_max) return 0;
                output[op++] = TOKEN_ESCAPE;
                output[op++] = TOKEN_DICT_MARK;
                output[op++] = id;
            } else {
                for (size_t k = 0; k < len; k++) {
                    uint8_t c = input[start + k];
                    if (c == TOKEN_ESCAPE) {
                        if (op + 2 > output_max) return 0;
                        output[op++] = TOKEN_ESCAPE;
                        output[op++] = TOKEN_LITERAL;
                    } else {
                        if (op + 1 > output_max) return 0;
                        output[op++] = c;
                    }
                }
            }
        } else {
            uint8_t c = input[ip++];
            if (c == TOKEN_ESCAPE) {
                if (op + 2 > output_max) return 0;
                output[op++] = TOKEN_ESCAPE;
                output[op++] = TOKEN_LITERAL;
            } else {
                if (op + 1 > output_max) return 0;
                output[op++] = c;
            }
        }
    }
    return op;
}

static size_t token_decode_text(const uint8_t *input, size_t input_size,
                                uint8_t *output, size_t output_max,
                                const TokenDict *dict, size_t expected_size) {
    size_t ip = 0;
    size_t op = 0;
    while (ip < input_size && op < expected_size) {
        uint8_t c = input[ip++];
        if (c != TOKEN_ESCAPE) {
            if (op >= output_max) return 0;
            output[op++] = c;
            continue;
        }
        if (ip >= input_size) return 0;
        uint8_t tag = input[ip++];
        if (tag == TOKEN_LITERAL) {
            if (op >= output_max) return 0;
            output[op++] = TOKEN_ESCAPE;
        } else if (tag == TOKEN_DICT_MARK) {
            if (ip >= input_size) return 0;
            uint8_t id = input[ip++];
            if (id >= dict->count) return 0;
            uint16_t len = dict->len[id];
            if (op + len > output_max) return 0;
            memcpy(output + op, dict->blob + dict->off[id], len);
            op += len;
        } else {
            return 0;
        }
    }
    return (op == expected_size) ? op : 0;
}


/* ====================================================================
 * LZ-LITE: быстрый предпроход для удаления длинных повторов
 * ====================================================================
 * Окно 32KB, хеш-цепочки, макс. совпадение 258 байт.
 * Формат: литерал <0xFE, byte> или <len_hi|0x80, len_lo, dist_hi, dist_lo>
 *   literal: если byte != 0xFE → один байт
 *            если byte == 0xFE → <0xFE, 0xFE>
 *   match:   <0xFF, len-3, dist_hi, dist_lo>  (len 3..258, dist 1..32768)
 * ==================================================================== */

#define LZ_WBITS   20           /* v55: 1 MB окно */
#define LZ_WSIZE   (1 << LZ_WBITS)  /* 1048576 */
#define LZ_WMASK   (LZ_WSIZE - 1)
#define LZ_HTBITS  20           /* v55: 1M хеш-таблица */
#define LZ_HTSIZE  (1 << LZ_HTBITS)
#define LZ_HTMASK  (LZ_HTSIZE - 1)
#define LZ_MIN_MATCH  3
#define LZ_MAX_MATCH  259       /* v55: extended len-4 max 255 */
#define LZ_MAX_CHAIN  512       /* макс. глубина цепочки (v53+) */
#define LZ_NICE_MATCH 128       /* v59: ранний выход при хорошем совпадении */
#define LZ_ESCAPE     0xFF      /* escape-байт для совпадений */
#define LZ_EXT_CODE   0xFC      /* v55: extended 24-bit distance match */
#define LZ_REP_CODE   0xFD      /* v54: опкод rep-match (LZMA-style) */
#define LZ_NUM_REPS   4         /* кол-во хранимых последних дистанций */

static inline uint32_t lz_hash4(const uint8_t *p) {
    uint32_t h;
    memcpy(&h, p, 4); /* v58: быстрое чтение (unaligned OK на x86/arm) */
    return (h * 0x9E3779B1u) >> (32 - LZ_HTBITS);
}

/* Кодирование LZ-lite: input→output, возвращает размер или 0 при неудаче */
static size_t lz_lite_encode(const uint8_t *input, size_t input_size,
                              uint8_t *output, size_t output_max)
{
    if (input_size < 4) return 0;

    int32_t *head = (int32_t *)calloc(LZ_HTSIZE, sizeof(int32_t));
    int32_t *prev = (int32_t *)calloc(LZ_WSIZE, sizeof(int32_t));
    if (!head || !prev) { free(head); free(prev); return 0; }

    /* v54: Rep-match — последние 4 дистанции (LZMA-style) */
    int rep[LZ_NUM_REPS] = {0, 0, 0, 0};

    /* -1 = нет ссылки */
    for (size_t j = 0; j < (size_t)LZ_HTSIZE; j++) head[j] = -1;
    for (size_t j = 0; j < (size_t)LZ_WSIZE; j++) prev[j] = -1;

    size_t op = 0;
    size_t ip = 0;
    size_t limit = input_size - 3; /* нужно 4 байта для хеша */

    /* v54: Near-Optimal LZ parsing helper — поиск лучшего матча от позиции p */
    #define LZ_FIND_MATCH(pos_, best_l_, best_d_)                          \
    do {                                                                    \
        best_l_ = 0; best_d_ = 0;                                          \
        if ((pos_) < limit) {                                               \
            uint32_t hh_ = lz_hash4(input + (pos_));                        \
            int32_t cc_ = head[hh_];                                         \
            int ch_ = 0;                                                     \
            while (cc_ >= 0 && ch_ < 256) {  /* v60: near-optimal chain/2 */ \
                size_t ca_ = (size_t)cc_;                                    \
                if ((pos_) > ca_ && ((pos_) - ca_) <= LZ_WSIZE) {           \
                    const uint8_t *aa_ = input + (pos_);                     \
                    const uint8_t *bb_ = input + ca_;                        \
                    int ll_ = 0;                                             \
                    int mx_ = (int)MIN((size_t)LZ_MAX_MATCH, input_size-(pos_)); \
                    /* v61: 8-байтное сравнение */                             \
                    if (mx_ >= 4) {                                          \
                        uint32_t va_, vb_;                                   \
                        memcpy(&va_, aa_, 4); memcpy(&vb_, bb_, 4);          \
                        if (va_ == vb_) {                                    \
                            ll_ = 4;                                         \
                            while (ll_ + 8 <= mx_) {                         \
                                uint64_t va8_,vb8_;                          \
                                memcpy(&va8_, aa_+ll_, 8);                   \
                                memcpy(&vb8_, bb_+ll_, 8);                   \
                                if (va8_ != vb8_) break;                     \
                                ll_ += 8;                                    \
                            }                                                \
                            while (ll_ < mx_ && aa_[ll_] == bb_[ll_]) ll_++; \
                        }                                                    \
                    } else {                                                 \
                        while (ll_ < mx_ && aa_[ll_] == bb_[ll_]) ll_++;     \
                    }                                                        \
                    if (ll_ >= LZ_MIN_MATCH && ll_ > best_l_) {              \
                        best_l_ = ll_; best_d_ = (int)((pos_) - ca_);       \
                        if (ll_ >= LZ_NICE_MATCH) break;                     \
                    }                                                        \
                }                                                            \
                cc_ = prev[ca_ & LZ_WMASK];                                 \
                if (cc_ >= (int32_t)(pos_)) break;                           \
                if (cc_ >= 0) __builtin_prefetch(input+(size_t)cc_, 0, 0);   \
                ch_++;                                                       \
            }                                                                \
        }                                                                    \
    } while(0)

    while (ip < input_size) {
        int best_len = 0;
        int best_dist = 0;
        int best_rep = -1;  /* v54: индекс rep-match (-1 = обычный матч) */

        /* v54: Rep-match поиск — проверяем последние 4 дистанции (LZMA-style)
         * Rep-match кодируется за 3 байта (вместо 3-4 для обычного),
         * поэтому выгоднее при прочих равных условиях */
        if (ip < limit) {
            int max_rep_len = (int)MIN((size_t)LZ_MAX_MATCH, input_size - ip);
            for (int ri = 0; ri < LZ_NUM_REPS; ri++) {
                if (rep[ri] <= 0 || rep[ri] > (int)ip) continue;
                const uint8_t *a = input + ip;
                const uint8_t *b = input + ip - rep[ri];
                int rlen = 0;
                /* v61: fast 4-byte check + 8-byte inner loop */
                if (max_rep_len >= 4) {
                    uint32_t rv, rbv;
                    memcpy(&rv, a, 4); memcpy(&rbv, b, 4);
                    if (rv == rbv) {
                        rlen = 4;
                        while (rlen + 8 <= max_rep_len) {
                            uint64_t rv8, rbv8;
                            memcpy(&rv8, a+rlen, 8); memcpy(&rbv8, b+rlen, 8);
                            if (rv8 != rbv8) break;
                            rlen += 8;
                        }
                        while (rlen < max_rep_len && a[rlen] == b[rlen]) rlen++;
                    }
                } else {
                    while (rlen < max_rep_len && a[rlen] == b[rlen]) rlen++;
                }
                /* Rep-match дешевле: 3B vs 3-4B, бонус к длине */
                if (rlen >= LZ_MIN_MATCH && rlen > best_len) {
                    best_len = rlen;
                    best_dist = rep[ri];
                    best_rep = ri;
                }
            }
        }

        /* Обычный хеш-цепочный поиск (если rep не дал NICE_MATCH) */
        if (ip < limit && best_len < LZ_NICE_MATCH) {
            uint32_t h = lz_hash4(input + ip);
            int32_t cur = head[h];
            int chain = 0;

            while (cur >= 0 && chain < LZ_MAX_CHAIN) {
                size_t candidate = (size_t)cur;
                if (ip > candidate && (ip - candidate) <= LZ_WSIZE) {
                    const uint8_t *a = input + ip;
                    const uint8_t *b = input + candidate;
                    int len = 0;
                    int max_possible = (int)MIN((size_t)LZ_MAX_MATCH, input_size - ip);
                    /* v61: 8-байтное сравнение */
                    if (max_possible >= 4) {
                        uint32_t va, vb;
                        memcpy(&va, a, 4); memcpy(&vb, b, 4);
                        if (va == vb) {
                            len = 4;
                            while (len + 8 <= max_possible) {
                                uint64_t va8, vb8;
                                memcpy(&va8, a+len, 8); memcpy(&vb8, b+len, 8);
                                if (va8 != vb8) break;
                                len += 8;
                            }
                            while (len < max_possible && a[len] == b[len]) len++;
                        }
                    } else {
                        while (len < max_possible && a[len] == b[len]) len++;
                    }

                    /* Обычный матч должен быть длиннее rep-match на 1+,
                     * т.к. rep-match на 1 байт дешевле в кодировании */
                    if (len >= LZ_MIN_MATCH && len > best_len + (best_rep >= 0 ? 1 : 0)) {
                        best_len = len;
                        best_dist = (int)(ip - candidate);
                        best_rep = -1;  /* обычный матч */
                        if (len >= LZ_NICE_MATCH) break;
                    }
                }
                cur = prev[candidate & LZ_WMASK];
                if (cur >= (int32_t)ip) break;
                if (cur >= 0) __builtin_prefetch(input + (size_t)cur, 0, 0);
                chain++;
                if (best_len >= 32 && chain >= 64) break; /* v60: adaptive chain */
            }

            /* Обновляем хеш-цепочку */
            prev[ip & LZ_WMASK] = head[h];
            head[h] = (int32_t)ip;
        } else if (ip < limit) {
            /* Если rep-match уже NICE — всё равно обновляем хеш */
            uint32_t h = lz_hash4(input + ip);
            prev[ip & LZ_WMASK] = head[h];
            head[h] = (int32_t)ip;
        }

        if (best_len >= LZ_MIN_MATCH) {
            /* v54 Near-Optimal: проверяем ip+1 и ip+2 (если не rep-NICE) */
            if (best_len < LZ_NICE_MATCH && best_rep < 0) {
                int len1 = 0, dist1 = 0;
                if ((ip + 1) < limit) {
                    LZ_FIND_MATCH(ip + 1, len1, dist1);
                }
                int len2 = 0, dist2 = 0;
                if ((ip + 2) < limit) {
                    LZ_FIND_MATCH(ip + 2, len2, dist2);
                }

                /* v55: стоимость учитывает 24-bit extended (6B) */
                int mc0 = (best_len==3 && best_dist<=256) ? 3 : (best_dist>65535 ? 6 : 4);
                int mc1 = (len1==3 && dist1<=256) ? 3 : (dist1>65535 ? 6 : 4);
                int mc2 = (len2==3 && dist2<=256) ? 3 : (dist2>65535 ? 6 : 4);
                int cost0 = best_len - mc0;
                int cost1 = len1 - mc1
                            - (input[ip] == LZ_ESCAPE ? 2 : 1);
                int cost2 = len2 - mc2
                            - (input[ip] == LZ_ESCAPE ? 2 : 1)
                            - (input[ip+1] == LZ_ESCAPE ? 2 : 1);

                if (cost2 > cost0 && cost2 > cost1 && len2 >= LZ_MIN_MATCH) {
                    for (int s = 0; s < 2; s++) {
                        if (input[ip] == LZ_ESCAPE) {
                            if (op + 2 > output_max) goto fail;
                            output[op++] = LZ_ESCAPE; output[op++] = LZ_ESCAPE;
                        } else {
                            if (op + 1 > output_max) goto fail;
                            output[op++] = input[ip];
                        }
                        prev[ip & LZ_WMASK] = head[lz_hash4(input + ip)];
                        head[lz_hash4(input + ip)] = (int32_t)ip;
                        ip++;
                    }
                    best_len = len2; best_dist = dist2;
                    best_rep = -1;
                } else if (cost1 > cost0 && len1 >= LZ_MIN_MATCH) {
                    if (input[ip] == LZ_ESCAPE) {
                        if (op + 2 > output_max) goto fail;
                        output[op++] = LZ_ESCAPE; output[op++] = LZ_ESCAPE;
                    } else {
                        if (op + 1 > output_max) goto fail;
                        output[op++] = input[ip];
                    }
                    prev[ip & LZ_WMASK] = head[lz_hash4(input + ip)];
                    head[lz_hash4(input + ip)] = (int32_t)ip;
                    ip++;
                    best_len = len1; best_dist = dist1;
                    best_rep = -1;
                }
            }

            /* v55 Match encoding:
             * Rep:      0xFF 0xFD (rep_idx<<6|len-3)         (3B, len 3-66)
             * Short:    0xFF 0xFE dist_lo                    (3B, len=3, dist≤256)
             * Long:     0xFF (len-4) dist_hi dist_lo         (4B, len 4-255, dist≤65535)
             * Extended: 0xFF 0xFC (len-4) d[23:16] d[15:8] d[7:0]  (6B, 24-bit dist) */
            int rep_max_len = 66;
            /* FIX: если rep-match слишком длинный для rep-формата,
             * кодируем как обычный — сбрасываем best_rep для sync rep MRU */
            if (best_rep >= 0 && best_len > rep_max_len) {
                best_rep = -1;
            }
            if (best_rep >= 0 && best_len <= rep_max_len) {
                int rl = best_len;
                if (rl > rep_max_len) rl = rep_max_len;
                if (op + 3 > output_max) goto fail;
                output[op++] = LZ_ESCAPE;
                output[op++] = LZ_REP_CODE;
                output[op++] = (uint8_t)((best_rep << 6) | (rl - 3));
                best_len = rl;
            } else if (best_len == 3 && best_dist <= 256) {
                if (op + 3 > output_max) goto fail;
                output[op++] = LZ_ESCAPE;
                output[op++] = 0xFE;
                output[op++] = (uint8_t)(best_dist - 1);
            } else if (best_dist > 65535) {
                /* v55: Extended 24-bit distance match */
                int el = best_len;
                if (el > 259) el = 259;
                if (op + 6 > output_max) goto fail;
                output[op++] = LZ_ESCAPE;
                output[op++] = LZ_EXT_CODE;
                output[op++] = (uint8_t)(el - 4);
                output[op++] = (uint8_t)((best_dist >> 16) & 0xFF);
                output[op++] = (uint8_t)((best_dist >> 8) & 0xFF);
                output[op++] = (uint8_t)(best_dist & 0xFF);
                best_len = el;
            } else {
                int rl = best_len;
                if (rl > 255) rl = 255; /* regular long max len-4=0xFB=251 */
                if (op + 4 > output_max) goto fail;
                output[op++] = LZ_ESCAPE;
                output[op++] = (uint8_t)(rl - 4);
                output[op++] = (uint8_t)((best_dist >> 8) & 0xFF);
                output[op++] = (uint8_t)(best_dist & 0xFF);
                best_len = rl;
            }

            /* v54: обновляем rep-дистанции (MRU — последний вперёд) */
            if (best_rep < 0) {
                /* Новая дистанция — сдвигаем всё вправо */
                for (int ri = LZ_NUM_REPS - 1; ri > 0; ri--)
                    rep[ri] = rep[ri - 1];
                rep[0] = best_dist;
            } else if (best_rep > 0) {
                /* rep[1..3] → двигаем на позицию 0 (MRU) */
                int tmp = rep[best_rep];
                for (int ri = best_rep; ri > 0; ri--)
                    rep[ri] = rep[ri - 1];
                rep[0] = tmp;
            }
            /* best_rep == 0: rep[0] уже актуален, ничего не меняем */

            /* v60: sparse hash update — для длинных матчей обновляем каждую 4-ю */
            for (int k = 1; k < best_len && (ip + k) < limit; k++) {
                if (k > 32 && (k & 3)) continue;
                uint32_t hk = lz_hash4(input + ip + k);
                prev[(ip + k) & LZ_WMASK] = head[hk];
                head[hk] = (int32_t)(ip + k);
            }
            ip += best_len;
        } else {
            /* Литерал */
            if (input[ip] == LZ_ESCAPE) {
                if (op + 2 > output_max) goto fail;
                output[op++] = LZ_ESCAPE;
                output[op++] = LZ_ESCAPE;
            } else {
                if (op + 1 > output_max) goto fail;
                output[op++] = input[ip];
            }
            ip++;
        }
    }

    free(head);
    free(prev);

    /* Выгодно только если уменьшили */
    if (op >= input_size) return 0;
    return op;

fail:
    free(head);
    free(prev);
    return 0;
}

/* ====================================================================
 * LZ-TURBO v63: максимальная скорость — 8-step chains + greedy
 * ====================================================================
 * Формат выхода совместим с LZ-lite (lz_lite_decode без изменений).
 * Отличия от LZ-lite:
 *   - Макс. глубина цепочки 8 (vs 512) — ~64× меньше сравнений
 *   - Greedy (vs near-optimal ip+1/ip+2 проверки)
 *   - Без rep-match (экономим bookkeeping)
 *   - Обновление хеша каждые 3 байта внутри матча (vs каждый)
 * Pipeline: input → turbo LZ → output (без CM/RC/Formula)
 * Скорость: ~100-200 MB/s vs ~4 MB/s у v62 = 25-50× быстрее
 * ==================================================================== */
#define TURBO_MAX_CHAIN 8  /* макс. глубина цепочки (vs 512 у lz_lite) */

static size_t lz_turbo_encode(const uint8_t *input, size_t input_size,
                               uint8_t *output, size_t output_max)
{
    if (input_size < 8) return 0;

    int32_t *head = (int32_t *)malloc(LZ_HTSIZE * sizeof(int32_t));
    int32_t *prev = (int32_t *)malloc(LZ_WSIZE * sizeof(int32_t));
    if (!head || !prev) { free(head); free(prev); return 0; }
    for (size_t j = 0; j < (size_t)LZ_HTSIZE; j++) head[j] = -1;
    for (size_t j = 0; j < (size_t)LZ_WSIZE; j++) prev[j] = -1;

    size_t op = 0, ip = 0;
    size_t limit = input_size - 3;

    while (ip < input_size) {
        int best_len = 0, best_dist = 0;

        if (ip < limit) {
            uint32_t h = lz_hash4(input + ip);
            int32_t cur = head[h];
            int chain = 0;

            while (cur >= 0 && chain < TURBO_MAX_CHAIN) {
                size_t cand = (size_t)cur;
                if (ip > cand && (ip - cand) <= LZ_WSIZE) {
                    const uint8_t *a = input + ip;
                    const uint8_t *b = input + cand;
                    int len = 0;
                    int mx = (int)MIN((size_t)LZ_MAX_MATCH, input_size - ip);
                    if (mx >= 4) {
                        uint32_t va, vb;
                        memcpy(&va, a, 4); memcpy(&vb, b, 4);
                        if (va == vb) {
                            len = 4;
                            while (len + 8 <= mx) {
                                uint64_t va8, vb8;
                                memcpy(&va8, a + len, 8);
                                memcpy(&vb8, b + len, 8);
                                if (va8 != vb8) break;
                                len += 8;
                            }
                            while (len < mx && a[len] == b[len]) len++;
                        }
                    } else {
                        while (len < mx && a[len] == b[len]) len++;
                    }
                    if (len >= LZ_MIN_MATCH && len > best_len) {
                        best_len = len;
                        best_dist = (int)(ip - cand);
                        if (len >= 64) break; /* достаточно хороший матч */
                    }
                }
                cur = prev[cand & LZ_WMASK];
                if (cur >= (int32_t)ip) break;
                chain++;
            }

            /* Обновляем цепочку */
            prev[ip & LZ_WMASK] = head[h];
            head[h] = (int32_t)ip;
        }

        if (best_len >= LZ_MIN_MATCH) {
            /* Кодируем матч в формате LZ-lite */
            if (best_len == 3 && best_dist <= 256) {
                if (op + 3 > output_max) goto turbo_fail;
                output[op++] = LZ_ESCAPE;
                output[op++] = 0xFE;
                output[op++] = (uint8_t)(best_dist - 1);
            } else if (best_dist > 65535) {
                if (best_len > 259) best_len = 259;
                if (op + 6 > output_max) goto turbo_fail;
                output[op++] = LZ_ESCAPE;
                output[op++] = LZ_EXT_CODE;
                output[op++] = (uint8_t)(best_len - 4);
                output[op++] = (uint8_t)((best_dist >> 16) & 0xFF);
                output[op++] = (uint8_t)((best_dist >> 8) & 0xFF);
                output[op++] = (uint8_t)(best_dist & 0xFF);
            } else {
                if (best_len > 255) best_len = 255;
                if (op + 4 > output_max) goto turbo_fail;
                output[op++] = LZ_ESCAPE;
                output[op++] = (uint8_t)(best_len - 4);
                output[op++] = (uint8_t)((best_dist >> 8) & 0xFF);
                output[op++] = (uint8_t)(best_dist & 0xFF);
            }
            /* Обновляем хеш для позиций внутри матча (каждые 3-й для скорости) */
            for (int k = 1; k < best_len && (ip + k) < limit; k += 3) {
                uint32_t hk = lz_hash4(input + ip + k);
                prev[(ip + k) & LZ_WMASK] = head[hk];
                head[hk] = (int32_t)(ip + k);
            }
            ip += best_len;
        } else {
            /* Литерал */
            if (input[ip] == LZ_ESCAPE) {
                if (op + 2 > output_max) goto turbo_fail;
                output[op++] = LZ_ESCAPE; output[op++] = LZ_ESCAPE;
            } else {
                if (op + 1 > output_max) goto turbo_fail;
                output[op++] = input[ip];
            }
            ip++;
        }
    }

    free(head); free(prev);
    if (op >= input_size) return 0;
    return op;

turbo_fail:
    free(head); free(prev);
    return 0;
}

/* Декодирование LZ-lite */
static size_t lz_lite_decode(const uint8_t *input, size_t input_size,
                              uint8_t *output, size_t output_max,
                              size_t original_size)
{
    size_t ip = 0, op = 0;

    /* v54: Rep-match — последние 4 дистанции */
    int rep[LZ_NUM_REPS] = {0, 0, 0, 0};

    while (ip < input_size && op < original_size) {
        uint8_t c = input[ip++];
        if (c == LZ_ESCAPE) {
            if (ip >= input_size) return 0;
            uint8_t next = input[ip++];
            if (next == LZ_ESCAPE) {
                /* Экранированный литерал 0xFF */
                if (op >= output_max) return 0;
                output[op++] = LZ_ESCAPE;
            } else if (next == LZ_REP_CODE) {
                /* v54: Rep-match: 0xFF 0xFD (rep_idx<<6 | len-3) */
                if (ip >= input_size) return 0;
                uint8_t rb = input[ip++];
                int rep_idx = (rb >> 6) & 3;
                int len = (rb & 0x3F) + 3;
                int dist = rep[rep_idx];
                if (dist == 0 || dist > (int)op) return 0;
                if (op + len > output_max) return 0;
                for (int k = 0; k < len; k++) {
                    output[op] = output[op - dist];
                    op++;
                }
                /* MRU обновление rep */
                if (rep_idx > 0) {
                    int tmp = rep[rep_idx];
                    for (int ri = rep_idx; ri > 0; ri--)
                        rep[ri] = rep[ri - 1];
                    rep[0] = tmp;
                }
            } else if (next == LZ_EXT_CODE) {
                /* v55: Extended 24-bit distance match */
                if (ip + 3 >= input_size) return 0;
                int len = (int)input[ip] + 4;
                int dist = ((int)input[ip+1] << 16)
                         | ((int)input[ip+2] << 8)
                         | (int)input[ip+3];
                ip += 4;
                if (dist == 0 || dist > (int)op) return 0;
                if (op + len > output_max) return 0;
                for (int k = 0; k < len; k++) {
                    output[op] = output[op - dist];
                    op++;
                }
                for (int ri = LZ_NUM_REPS - 1; ri > 0; ri--)
                    rep[ri] = rep[ri - 1];
                rep[0] = dist;
            } else if (next == 0xFE) {
                /* Short match: len=3, dist=next_byte+1 */
                if (ip >= input_size) return 0;
                int len = 3;
                int dist = (int)input[ip] + 1;
                ip += 1;
                if (dist == 0 || dist > (int)op) return 0;
                if (op + len > output_max) return 0;
                for (int k = 0; k < len; k++) {
                    output[op] = output[op - dist];
                    op++;
                }
                for (int ri = LZ_NUM_REPS - 1; ri > 0; ri--)
                    rep[ri] = rep[ri - 1];
                rep[0] = dist;
            } else {
                /* Long match: len = next+4, dist = 2 bytes */
                if (ip + 1 >= input_size) return 0;
                int len = (int)next + 4;
                int dist = ((int)input[ip] << 8) | (int)input[ip + 1];
                ip += 2;
                if (dist == 0 || dist > (int)op) return 0;
                if (op + len > output_max) return 0;
                for (int k = 0; k < len; k++) {
                    output[op] = output[op - dist];
                    op++;
                }
                for (int ri = LZ_NUM_REPS - 1; ri > 0; ri--)
                    rep[ri] = rep[ri - 1];
                rep[0] = dist;
            }
        } else {
            if (op >= output_max) return 0;
            output[op++] = c;
        }
    }

    return op;
}

/* ====================================================================
 * KOLIBRI FORMULA COMPRESSION v51 — "Kolibri Mind"
 * ====================================================================
 * Чистое формульное сжатие: адаптивное предсказание + range coding.
 *
 * Архитектура:
 *   - Range Coder (fpaq0-style, 32-bit)
 *   - Раздельные таблицы для каждого порядка контекста (Order 0..7)
 *   - SSE (Secondary Symbol Estimation)
 *   - Память: ~4.2 MB  (раздельные таблицы, нет коллизий между порядками)
 *   - Никакого BWT, LZ77, Huffman — чистая формула
 * ==================================================================== */

/* --- Range Coder --- */
typedef struct {
    uint32_t x1, x2, x;
    uint8_t *buf;
    size_t pos, size;
} KolibriRC;

static void krc_enc_init(KolibriRC *c, uint8_t *buf, size_t sz) {
    c->x1 = 0; c->x2 = 0xFFFFFFFFu; c->x = 0;
    c->buf = buf; c->pos = 0; c->size = sz;
}
static inline void krc_enc_bit(KolibriRC *c, int bit, uint32_t prob) {
    uint32_t xmid = c->x1 + ((c->x2 - c->x1) >> 12) * prob;
    if (bit) c->x2 = xmid; else c->x1 = xmid + 1;
    while (((c->x1 ^ c->x2) & 0xFF000000u) == 0) {
        if (c->pos < c->size) c->buf[c->pos++] = (uint8_t)(c->x1 >> 24);
        c->x1 <<= 8; c->x2 = (c->x2 << 8) | 0xFF;
    }
}
static void krc_enc_flush(KolibriRC *c) {
    for (int i = 0; i < 4; i++) {
        if (c->pos < c->size) c->buf[c->pos++] = (uint8_t)(c->x1 >> 24);
        c->x1 <<= 8;
    }
}
static void krc_dec_init(KolibriRC *c, const uint8_t *buf, size_t sz) {
    c->x1 = 0; c->x2 = 0xFFFFFFFFu;
    c->buf = (uint8_t *)buf; c->pos = 0; c->size = sz;
    c->x = 0;
    for (int i = 0; i < 4; i++)
        c->x = (c->x << 8) | (c->pos < c->size ? c->buf[c->pos++] : 0);
}
static inline int krc_dec_bit(KolibriRC *c, uint32_t prob) {
    uint32_t xmid = c->x1 + ((c->x2 - c->x1) >> 12) * prob;
    int bit = (c->x <= xmid) ? 1 : 0;
    if (bit) c->x2 = xmid; else c->x1 = xmid + 1;
    while (((c->x1 ^ c->x2) & 0xFF000000u) == 0) {
        c->x1 <<= 8; c->x2 = (c->x2 << 8) | 0xFF;
        c->x = (c->x << 8) | (c->pos < c->size ? c->buf[c->pos++] : 0);
    }
    return bit;
}

/* --- Контекстная модель с раздельными таблицами ---
 *
 * Размеры таблиц (степени двойки для быстрого маскирования):
 *   O0:   256 × 256 = 65536 (128KB) — полная статистика (без хеша)
 *   O1: 64K записей  (128KB)
 *   O2: 128K записей (256KB)
 *   O3: 256K записей (512KB)
 *   O4: 256K записей (512KB)
 *   O5: 1M записей (2MB)
 *   O6: 1M записей (2MB)
 *   O7: 1M записей (2MB)
 *   O8: 1M записей (2MB) — v55
 *   SSE: 256×8×33    (135KB)
 *   Run: 4K записей (8KB) — v55
 *   Итого: ~11.4 MB
 */
#define T0_SIZE  65536u     /* Order-0: 256 byte contexts × 256 bit-tree */
#define T1_SIZE  65536u     /* 64K */
#define T2_SIZE  131072u    /* 128K */
#define T3_SIZE  262144u    /* 256K */
#define T4_SIZE  262144u
#define T5_SIZE  1048576u   /* 1M — v53: больше таблицы = меньше коллизий */
#define T6_SIZE  1048576u
#define T7_SIZE  1048576u
#define T8_SIZE  1048576u   /* v55: Order-8 — глубокий контекст для исходного кода */
#define SSE_Q    33         /* SSE: 33 линейных бина (компактно) */
#define SSE_SZ   (512u * 8u * SSE_Q)  /* v59: O1 контекст — hist[0]*2+(hist[1]>>7) */
#define TRUN_SIZE  4096u    /* v55: run-length context (16 run_len × 256 cx) */
#define APM_SIZE   32768u   /* v59: state-aware APM — (cx*2+is_lz)*33+q2 */

/* v62: объединённые таблицы + SIMD параметры */
#define T678_SIZE     1048576u  /* v62: merged O6+O7+O8 → одна таблица 1M */
#define KF62_NUM_PREDS 11       /* v62: 11 предикторов (O0-O5,O678,st,w,sp,run) */
#define KF62_PAD       16       /* v62: padding до 16 для SIMD (2 × __m128i) */
#define KF62_BLOCK_SIZE 65536   /* v62: размер блока для многопоточности */

/* --- v54: Логистическое смешивание (stretch/squash) ---
 * stretch(p) = ln(p/(1-p)) — переводит вероятность в logit-пространство
 * squash(x) = 1/(1+exp(-x)) — обратно в вероятность
 * Таблицы предвычислены для 12-бит вероятностей (0..4095) */
static int16_t stretch_table[4096];
static uint16_t squash_table[4096]; /* индекс: x+2048 → вероятность 0..4095 */
static int kf_tables_ready = 0;

static void kf_init_tables(void) {
    if (kf_tables_ready) return;
    /* stretch: p в 0..4095 → logit в ~[-2048..2047] */
    for (int i = 1; i < 4095; i++) {
        double p = (double)i / 4096.0;
        double logit = log(p / (1.0 - p)) * 256.0; /* масштаб ×256 */
        if (logit < -2047) logit = -2047;
        if (logit > 2047) logit = 2047;
        stretch_table[i] = (int16_t)logit;
    }
    stretch_table[0] = -2047;
    stretch_table[4095] = 2047;
    /* squash: x в -2048..2047 → p в 0..4095 */
    for (int i = 0; i < 4096; i++) {
        int x = i - 2048;
        double ex = exp(-(double)x / 256.0);
        double p = 1.0 / (1.0 + ex);
        int q = (int)(p * 4096.0 + 0.5);
        if (q < 1) q = 1; if (q > 4095) q = 4095;
        squash_table[i] = (uint16_t)q;
    }
    kf_tables_ready = 1;
}
static inline int16_t kf_stretch(uint32_t p) {
    if (p > 4095) p = 4095;
    return stretch_table[p];
}
static inline uint32_t kf_squash(int32_t x) {
    int idx = x + 2048;
    if (idx < 0) idx = 0;
    if (idx > 4095) idx = 4095;
    return squash_table[idx];
}

/* v57: Модель: 13 предикторов + APM chain + per-bit weights
 * O0-O8, LZ state, word boundary, sparse, run.
 * APM (2-й SSE) уточняет вероятность по контексту partial byte (cx). */

#define KF_NUM_PREDS 13  /* кол-во предикторов */

typedef struct {
    uint16_t *t0, *t1, *t2, *t3, *t4, *t5, *t6, *t7;
    uint16_t *t8;        /* v55: Order-8 context */
    uint16_t *sse;
    uint16_t *apm;       /* v56: 2nd SSE (APM chain) */
    uint16_t *tstate;    /* LZ state-aware */
    uint16_t *tword;     /* word boundary */
    uint16_t *tsparse;   /* sparse context */
    uint16_t *trun;      /* v55: run-length context */
    /* v60: per-bit weights (8 позиций × 13 предикторов) */
    int32_t w[8][KF_NUM_PREDS];
    int32_t wsum;
} KF51M;

#define TSTATE_SIZE  262144u /* v56: 256K — для 10-state machine */
#define TWORD_SIZE   65536u         /* word boundary context */
#define TSPARSE_SIZE 65536u         /* sparse (gap) context */

static uint16_t *kf51_new(size_t n) {
    uint16_t *p = (uint16_t *)malloc(n * sizeof(uint16_t));
    if (p) for (size_t i = 0; i < n; i++) p[i] = 2048;
    return p;
}
static int kf51_init(KF51M *m) {
    kf_init_tables();
    m->t0 = kf51_new(T0_SIZE);  m->t1 = kf51_new(T1_SIZE);
    m->t2 = kf51_new(T2_SIZE);  m->t3 = kf51_new(T3_SIZE);
    m->t4 = kf51_new(T4_SIZE);  m->t5 = kf51_new(T5_SIZE);
    m->t6 = kf51_new(T6_SIZE);  m->t7 = kf51_new(T7_SIZE);
    m->t8 = kf51_new(T8_SIZE);
    m->sse = kf51_new(SSE_SZ);
    m->apm = kf51_new(APM_SIZE);    /* v56: 2nd SSE */
    m->tstate = kf51_new(TSTATE_SIZE);
    m->tword = kf51_new(TWORD_SIZE);
    m->tsparse = kf51_new(TSPARSE_SIZE);
    m->trun = kf51_new(TRUN_SIZE);
    /* v60: per-bit weights — отдельные веса для каждой битовой позиции */
    for (int bp = 0; bp < 8; bp++) {
        m->w[bp][0]  =  4;  m->w[bp][1]  =  4;  m->w[bp][2]  =  8;
        m->w[bp][3]  = 16;  m->w[bp][4]  = 32;  m->w[bp][5]  = 48;
        m->w[bp][6]  = 72;  m->w[bp][7]  = 112; m->w[bp][8]  = 128;
        m->w[bp][9]  = 32;  m->w[bp][10] = 12;  m->w[bp][11] = 12;
        m->w[bp][12] = 16;
    }
    m->wsum = 0;
    return (m->t0 && m->t1 && m->t2 && m->t3 &&
            m->t4 && m->t5 && m->t6 && m->t7 && m->t8 &&
            m->sse && m->apm && m->tstate && m->tword &&
            m->tsparse && m->trun);
}
static void kf51_destroy(KF51M *m) {
    free(m->t0); free(m->t1); free(m->t2); free(m->t3);
    free(m->t4); free(m->t5); free(m->t6); free(m->t7);
    free(m->t8); free(m->sse); free(m->apm); free(m->tstate);
    free(m->tword); free(m->tsparse); free(m->trun);
}

/* =====================================================================
 * v62 MODEL: merged O678 tables, SIMD-aligned int16 weights
 * Память: ~6.9 MB (вместо 11.4 MB) — влезает в L3 кэш
 * ===================================================================== */
typedef struct {
    uint16_t *t0, *t1, *t2, *t3, *t4, *t5;
    uint16_t *t678;     /* v62: merged O6+O7+O8 */
    uint16_t *sse, *apm;
    uint16_t *tstate, *tword, *tsparse, *trun;
    /* v62: int16 веса, aligned для SSE2 */
    int16_t w[8][KF62_PAD] __attribute__((aligned(16)));
} KF62M;

static int kf62_init(KF62M *m) {
    kf_init_tables();
    m->t0     = kf51_new(T0_SIZE);   m->t1    = kf51_new(T1_SIZE);
    m->t2     = kf51_new(T2_SIZE);   m->t3    = kf51_new(T3_SIZE);
    m->t4     = kf51_new(T4_SIZE);   m->t5    = kf51_new(T5_SIZE);
    m->t678   = kf51_new(T678_SIZE);
    m->sse    = kf51_new(SSE_SZ);
    m->apm    = kf51_new(APM_SIZE);
    m->tstate = kf51_new(TSTATE_SIZE);
    m->tword  = kf51_new(TWORD_SIZE);
    m->tsparse= kf51_new(TSPARSE_SIZE);
    m->trun   = kf51_new(TRUN_SIZE);
    for (int bp = 0; bp < 8; bp++) {
        m->w[bp][0]  =  4;   m->w[bp][1]  =  4;   m->w[bp][2]  =  8;
        m->w[bp][3]  = 16;   m->w[bp][4]  = 32;   m->w[bp][5]  = 48;
        m->w[bp][6]  = 96;   /* merged O678: ~сумма весов O6+O7+O8 */
        m->w[bp][7]  = 32;   /* state */
        m->w[bp][8]  = 12;   /* word */
        m->w[bp][9]  = 12;   /* sparse */
        m->w[bp][10] = 16;   /* run */
        for (int p = 11; p < KF62_PAD; p++) m->w[bp][p] = 0;
    }
    return (m->t0 && m->t1 && m->t2 && m->t3 && m->t4 && m->t5 &&
            m->t678 && m->sse && m->apm && m->tstate &&
            m->tword && m->tsparse && m->trun);
}
static void kf62_destroy(KF62M *m) {
    free(m->t0);  free(m->t1);  free(m->t2);  free(m->t3);
    free(m->t4);  free(m->t5);  free(m->t678);
    free(m->sse); free(m->apm); free(m->tstate);
    free(m->tword); free(m->tsparse); free(m->trun);
}

/* v62: SIMD-оптимизированное смешивание и обновление весов */
#if KF_USE_SIMD
static inline int32_t kf_hsum_epi32(__m128i v) {
    __m128i hi = _mm_shuffle_epi32(v, _MM_SHUFFLE(2, 3, 0, 1));
    v = _mm_add_epi32(v, hi);
    hi = _mm_shuffle_epi32(v, _MM_SHUFFLE(1, 0, 3, 2));
    v = _mm_add_epi32(v, hi);
    return _mm_cvtsi128_si32(v);
}
static inline int32_t kf62_mix(const int16_t *w, const int16_t *s) {
    __m128i w0 = _mm_load_si128((const __m128i*)(w));
    __m128i w1 = _mm_load_si128((const __m128i*)(w + 8));
    __m128i s0 = _mm_load_si128((const __m128i*)(s));
    __m128i s1 = _mm_load_si128((const __m128i*)(s + 8));
    __m128i p0 = _mm_madd_epi16(w0, s0);
    __m128i p1 = _mm_madd_epi16(w1, s1);
    return kf_hsum_epi32(_mm_add_epi32(p0, p1));
}
static inline void kf62_update_weights(int16_t *w, const int16_t *s, int32_t err) {
    __m128i err_v = _mm_set1_epi16((int16_t)(err * 2));
    __m128i min_v = _mm_set1_epi16(-8192);
    __m128i max_v = _mm_set1_epi16(8192);
    /* Блок 0..7 */
    __m128i s0 = _mm_load_si128((const __m128i*)s);
    __m128i d0 = _mm_mulhi_epi16(err_v, s0);  /* (err*2*s) >> 16 ≈ (err*s) >> 15 */
    __m128i w0 = _mm_load_si128((__m128i*)w);
    w0 = _mm_max_epi16(_mm_min_epi16(_mm_add_epi16(w0, d0), max_v), min_v);
    _mm_store_si128((__m128i*)w, w0);
    /* Блок 8..15 */
    __m128i s1 = _mm_load_si128((const __m128i*)(s + 8));
    __m128i d1 = _mm_mulhi_epi16(err_v, s1);
    __m128i w1 = _mm_load_si128((__m128i*)(w + 8));
    w1 = _mm_max_epi16(_mm_min_epi16(_mm_add_epi16(w1, d1), max_v), min_v);
    _mm_store_si128((__m128i*)(w + 8), w1);
}
#else
static inline int32_t kf62_mix(const int16_t *w, const int16_t *s) {
    int32_t sum = 0;
    for (int i = 0; i < KF62_NUM_PREDS; i++)
        sum += (int32_t)w[i] * (int32_t)s[i];
    return sum;
}
static inline void kf62_update_weights(int16_t *w, const int16_t *s, int32_t err) {
    for (int i = 0; i < KF62_NUM_PREDS; i++) {
        int32_t delta = (err * (int32_t)s[i]) >> 15;
        int32_t nw = (int32_t)w[i] + delta;
        if (nw < -8192) nw = -8192;
        if (nw > 8192) nw = 8192;
        w[i] = (int16_t)nw;
    }
}
#endif

/* FNV хеш */
static inline uint32_t kfh(uint32_t h, uint32_t b) {
    return (h ^ b) * 0x01000193u;
}

/* v58: branchless update — убираем условный переход */
static inline void kf_upd(uint16_t *p, int bit, int rate) {
    uint32_t v = *p;
    *p = (uint16_t)(v + ((((uint32_t)bit << 12) - v) >> rate));
}

/* v57: 13 предикторов + APM chain + non-linear quant + per-bit weights.
 * O0-O8, LZ state, word boundary, sparse, run.
 * Нелинейное квантование: stretch-based, 64 бина (больше у 0 и 1).
 * Per-bit weights: отдельные веса для каждой из 8 битовых позиций. */
#define KF51_PROCESS_BYTE(ENCODE)                                           \
do {                                                                        \
    /* Хеши контекстов (Order 1..8) */                                      \
    uint32_t h1 = 0xA1B2C3D4u ^ hist[0];                                   \
    uint32_t h2 = kfh(h1, hist[1]);                                         \
    uint32_t h3 = kfh(h2, hist[2]);                                         \
    uint32_t h4 = kfh(h3, hist[3]);                                         \
    uint32_t h5 = kfh(h4, hist[4]);                                         \
    uint32_t h6 = kfh(h5, hist[5]);                                         \
    uint32_t h7 = kfh(h6, hist[6]);                                         \
    uint32_t h8 = kfh(h7, hist[7]); /* v55: Order-8 */                     \
                                                                            \
    /* Word boundary: пробел/nl/tab → начало нового слова */                \
    int is_wb = (hist[0]==' '||hist[0]=='\n'||hist[0]=='\t'              \
                 ||hist[0]=='\r'||hist[0]==0);                              \
                                                                            \
    /* v55: Run-length tracking — считаем серию одинаковых байт */          \
    int run_len = 0;                                                         \
    while (run_len < 15 && run_len < 7                                       \
           && hist[run_len] == hist[run_len+1]) run_len++;                   \
                                                                            \
    uint32_t cx = 1;                                                        \
    for (int b = 7; b >= 0; b--) {                                          \
        /* Индексы в раздельных таблицах */                                  \
        uint32_t i0 = (hist[0] * 256u + cx) & (T0_SIZE - 1);               \
        uint32_t i1 = (h1 * 256u + cx) & (T1_SIZE - 1);                    \
        uint32_t i2 = (h2 * 256u + cx) & (T2_SIZE - 1);                    \
        uint32_t i3 = (h3 * 256u + cx) & (T3_SIZE - 1);                    \
        uint32_t i4 = (h4 * 256u + cx) & (T4_SIZE - 1);                    \
        uint32_t i5 = (h5 * 256u + cx) & (T5_SIZE - 1);                    \
        uint32_t i6 = (h6 * 256u + cx) & (T6_SIZE - 1);                    \
        uint32_t i7 = (h7 * 256u + cx) & (T7_SIZE - 1);                    \
        uint32_t i8 = (h8 * 256u + cx) & (T8_SIZE - 1); /* v55 */          \
                                                                            \
        /* State-aware: (lz_state * 65536) + byte * 256 + cx */             \
        uint32_t ist = ((uint32_t)lz_state * 65536u                        \
                        + hist[0] * 256u + cx) & (TSTATE_SIZE - 1);         \
        /* Word model */                                                     \
        uint32_t iw = is_wb ?                                               \
            ((hist[1] * 137u + hist[2]) * 256u + cx) & (TWORD_SIZE - 1)    \
            : (hist[0] * 256u + cx) & (TWORD_SIZE - 1);                    \
        /* Sparse: (hist[0]^hist[2]) * 256 + cx */                          \
        uint32_t isp = ((hist[0] ^ hist[2]) * 256u + cx)                   \
                        & (TSPARSE_SIZE - 1);                               \
        /* v55: Run context: run_len * 256 + cx */                          \
        uint32_t irun = ((uint32_t)run_len * 256u + cx)                     \
                         & (TRUN_SIZE - 1);                                 \
                                                                            \
        /* Вероятности (13 предикторов) */                                   \
        uint32_t p0 = mm->t0[i0], p1 = mm->t1[i1];                         \
        uint32_t p2 = mm->t2[i2], p3 = mm->t3[i3];                         \
        uint32_t p4 = mm->t4[i4], p5 = mm->t5[i5];                         \
        uint32_t p6 = mm->t6[i6], p7 = mm->t7[i7];                         \
        uint32_t p8 = mm->t8[i8];                                           \
        uint32_t pst = mm->tstate[ist];                                     \
        uint32_t pw = mm->tword[iw];                                        \
        uint32_t psp = mm->tsparse[isp];                                    \
        uint32_t prun = mm->trun[irun];                                     \
                                                                            \
        /* Логистическое смешивание (13 предикторов) */                    \
        int16_t s[KF_NUM_PREDS];                                            \
        s[0]=kf_stretch(p0);  s[1]=kf_stretch(p1);  s[2]=kf_stretch(p2);   \
        s[3]=kf_stretch(p3);  s[4]=kf_stretch(p4);  s[5]=kf_stretch(p5);   \
        s[6]=kf_stretch(p6);  s[7]=kf_stretch(p7);  s[8]=kf_stretch(p8);   \
        s[9]=kf_stretch(pst); s[10]=kf_stretch(pw);  s[11]=kf_stretch(psp);\
        s[12]=kf_stretch(prun);                                              \
        int32_t logit_mix = 0;                                               \
        const int bp_ = 7 - b;  /* v60: per-bit weight index */              \
        for (int wi = 0; wi < KF_NUM_PREDS; wi++)                           \
            logit_mix += (int32_t)mm->w[bp_][wi] * (int32_t)s[wi];          \
        logit_mix >>= 8;                                                     \
        uint32_t mx = kf_squash(logit_mix);                                  \
                                                                            \
        /* v57: SSE с линейным квантованием (33 бина) */                     \
        int q = (int)(mx >> 7);                                             \
        if (q > 32) q = 32;                                                 \
        int sse_cx = (int)hist[0] * 2 + ((int)hist[1] >> 7);  /* v59: O1 */ \
        int si = (sse_cx * 8 + (7 - b)) * SSE_Q + q;                        \
        uint32_t sp = mm->sse[si];                                          \
        uint32_t fp = (mx * 3 + sp) >> 2;                                   \
        if (fp < 1) fp = 1; if (fp > 4095) fp = 4095;                      \
        /* v57: APM с линейным квантованием */                             \
        int q2 = (int)(fp >> 7); if (q2 > 32) q2 = 32;                     \
        int is_lz = (lz_state > 0) ? 1 : 0;  /* v59: state-aware */         \
        int api = ((int)cx * 2 + is_lz) * 33 + q2; /* v59: max=511*33+32 */ \
        if (api > (int)(APM_SIZE - 1)) api = (int)(APM_SIZE - 1);           \
        uint32_t ap = mm->apm[api];                                         \
        fp = (fp * 7 + ap) >> 3; /* 87.5% SSE + 12.5% APM */               \
        if (fp < 1) fp = 1; if (fp > 4095) fp = 4095;                      \
                                                                            \
        int bit;                                                             \
        if (ENCODE) {                                                        \
            bit = (byte >> b) & 1;                                           \
            krc_enc_bit(&rc, bit, fp);                                       \
        } else {                                                             \
            bit = krc_dec_bit(&rc, fp);                                      \
            if (bit) byte |= (1u << b);                                      \
        }                                                                    \
                                                                            \
        /* Обучаем контекстные таблицы */                                    \
        kf_upd(&mm->t0[i0], bit, 5);                                        \
        kf_upd(&mm->t1[i1], bit, 5);                                        \
        kf_upd(&mm->t2[i2], bit, 4);                                        \
        kf_upd(&mm->t3[i3], bit, 4);                                        \
        kf_upd(&mm->t4[i4], bit, 3);                                        \
        kf_upd(&mm->t5[i5], bit, 3);                                        \
        kf_upd(&mm->t6[i6], bit, 3);                                        \
        kf_upd(&mm->t7[i7], bit, 3);                                        \
        kf_upd(&mm->t8[i8], bit, 3); /* v55 */                             \
        kf_upd(&mm->tstate[ist], bit, 3);                                   \
        kf_upd(&mm->tword[iw], bit, 4);                                     \
        kf_upd(&mm->tsparse[isp], bit, 4);                                  \
        kf_upd(&mm->trun[irun], bit, 4);                                    \
        kf_upd(&mm->sse[si], bit, 3);  /* v57: rate 3 (быстрее адаптация) */\
        kf_upd(&mm->apm[api], bit, 4);  /* v57: rate 4 (быстрее) */     \
                                                                            \
        /* v57: обучение весов (>>15) */                                    \
        {                                                                    \
            int32_t err = (bit ? 4096 : 0) - (int32_t)mx;                   \
            for (int wi = 0; wi < KF_NUM_PREDS; wi++) {                     \
                int32_t delta = (err * (int32_t)s[wi]) >> 15;               \
                mm->w[bp_][wi] += delta;                 /* v60: per-bit */  \
                if (mm->w[bp_][wi] < -8192) mm->w[bp_][wi] = -8192;         \
                if (mm->w[bp_][wi] > 8192) mm->w[bp_][wi] = 8192;          \
            }                                                                \
        }                                                                    \
                                                                            \
        cx = (cx << 1) | bit;                                                \
    }                                                                        \
    /* v55: 10-state LZ token machine */                                     \
    switch (lz_state) {                                                      \
    case 0: if (byte == 0xFF) lz_state = 1; break;                          \
    case 1:                                                                  \
        if (byte == 0xFF) lz_state = 0;      /* escaped literal */           \
        else if (byte == 0xFE) lz_state = 4; /* short match */              \
        else if (byte == 0xFD) lz_state = 5; /* rep match */                \
        else if (byte == 0xFC) lz_state = 6; /* extended match */           \
        else lz_state = 2;                   /* regular long */              \
        break;                                                               \
    case 2: lz_state = 3; break; /* dist_hi → dist_lo */                    \
    case 3: lz_state = 0; break; /* dist_lo → normal */                     \
    case 4: lz_state = 0; break; /* short dist → normal */                  \
    case 5: lz_state = 0; break; /* rep_byte → normal */                    \
    case 6: lz_state = 7; break; /* ext len → dist[23:16] */               \
    case 7: lz_state = 8; break; /* dist[23:16] → dist[15:8] */            \
    case 8: lz_state = 9; break; /* dist[15:8] → dist[7:0] */              \
    case 9: lz_state = 0; break; /* dist[7:0] → normal */                  \
    default: lz_state = 0; break;                                            \
    }                                                                        \
    memmove(hist + 1, hist, 7);  /* v60: быстрый сдвиг истории */             \
    hist[0] = byte;                                                          \
} while(0)

/* =====================================================================
 * v62 PROCESS BYTE: 11 предикторов, merged O678, SIMD mixing/update
 * ===================================================================== */
#define KF62_PROCESS_BYTE(ENCODE)                                           \
do {                                                                        \
    uint32_t h1 = 0xA1B2C3D4u ^ hist[0];                                   \
    uint32_t h2 = kfh(h1, hist[1]);                                         \
    uint32_t h3 = kfh(h2, hist[2]);                                         \
    uint32_t h4 = kfh(h3, hist[3]);                                         \
    uint32_t h5 = kfh(h4, hist[4]);                                         \
    uint32_t h6 = kfh(h5, hist[5]);                                         \
    uint32_t h7 = kfh(h6, hist[6]);                                         \
    uint32_t h8 = kfh(h7, hist[7]);                                         \
    int is_wb = (hist[0]==' '||hist[0]=='\n'||hist[0]=='\t'                  \
                 ||hist[0]=='\r'||hist[0]==0);                               \
    int run_len = 0;                                                         \
    while (run_len < 15 && run_len < 7                                       \
           && hist[run_len] == hist[run_len+1]) run_len++;                   \
    uint32_t cx = 1;                                                        \
    for (int b = 7; b >= 0; b--) {                                          \
        uint32_t i0 = (hist[0] * 256u + cx) & (T0_SIZE - 1);               \
        uint32_t i1 = (h1 * 256u + cx) & (T1_SIZE - 1);                    \
        uint32_t i2 = (h2 * 256u + cx) & (T2_SIZE - 1);                    \
        uint32_t i3 = (h3 * 256u + cx) & (T3_SIZE - 1);                    \
        uint32_t i4 = (h4 * 256u + cx) & (T4_SIZE - 1);                    \
        uint32_t i5 = (h5 * 256u + cx) & (T5_SIZE - 1);                    \
        uint32_t i678 = (h8 * 256u + cx) & (T678_SIZE - 1);                \
        uint32_t ist = ((uint32_t)lz_state * 65536u                        \
                        + hist[0] * 256u + cx) & (TSTATE_SIZE - 1);         \
        uint32_t iw = is_wb ?                                               \
            ((hist[1] * 137u + hist[2]) * 256u + cx) & (TWORD_SIZE - 1)    \
            : (hist[0] * 256u + cx) & (TWORD_SIZE - 1);                    \
        uint32_t isp = ((hist[0] ^ hist[2]) * 256u + cx)                   \
                        & (TSPARSE_SIZE - 1);                               \
        uint32_t irun = ((uint32_t)run_len * 256u + cx)                     \
                         & (TRUN_SIZE - 1);                                 \
        /* v62: 11 stretch-значений, SIMD-aligned */                        \
        int16_t s_[KF62_PAD] __attribute__((aligned(16)));                  \
        s_[0]  = kf_stretch(mm->t0[i0]);                                    \
        s_[1]  = kf_stretch(mm->t1[i1]);                                    \
        s_[2]  = kf_stretch(mm->t2[i2]);                                    \
        s_[3]  = kf_stretch(mm->t3[i3]);                                    \
        s_[4]  = kf_stretch(mm->t4[i4]);                                    \
        s_[5]  = kf_stretch(mm->t5[i5]);                                    \
        s_[6]  = kf_stretch(mm->t678[i678]);                                \
        s_[7]  = kf_stretch(mm->tstate[ist]);                               \
        s_[8]  = kf_stretch(mm->tword[iw]);                                 \
        s_[9]  = kf_stretch(mm->tsparse[isp]);                              \
        s_[10] = kf_stretch(mm->trun[irun]);                                \
        for(int p_=11; p_<KF62_PAD; p_++) s_[p_]=0;                        \
        const int bp_ = 7 - b;                                              \
        /* v62: SIMD dot-product mixing */                                   \
        int32_t logit_mix = kf62_mix(mm->w[bp_], s_) >> 8;                  \
        uint32_t mx = kf_squash(logit_mix);                                  \
        /* SSE (O1 context) */                                               \
        int q = (int)(mx >> 7); if (q > 32) q = 32;                         \
        int sse_cx = (int)hist[0] * 2 + ((int)hist[1] >> 7);               \
        int si = (sse_cx * 8 + (7 - b)) * SSE_Q + q;                        \
        uint32_t sp = mm->sse[si];                                          \
        uint32_t fp = (mx * 3 + sp) >> 2;                                   \
        if (fp < 1) fp = 1; if (fp > 4095) fp = 4095;                      \
        /* APM (state-aware) */                                              \
        int q2 = (int)(fp >> 7); if (q2 > 32) q2 = 32;                     \
        int is_lz = (lz_state > 0) ? 1 : 0;                                 \
        int api = ((int)cx * 2 + is_lz) * 33 + q2;                         \
        if (api > (int)(APM_SIZE - 1)) api = (int)(APM_SIZE - 1);           \
        uint32_t ap = mm->apm[api];                                         \
        fp = (fp * 7 + ap) >> 3;                                             \
        if (fp < 1) fp = 1; if (fp > 4095) fp = 4095;                      \
        int bit;                                                             \
        if (ENCODE) {                                                        \
            bit = (byte >> b) & 1;                                           \
            krc_enc_bit(&rc, bit, fp);                                       \
        } else {                                                             \
            bit = krc_dec_bit(&rc, fp);                                      \
            if (bit) byte |= (1u << b);                                      \
        }                                                                    \
        /* Обучаем 11 таблиц + SSE + APM */                                  \
        kf_upd(&mm->t0[i0], bit, 5);    kf_upd(&mm->t1[i1], bit, 5);      \
        kf_upd(&mm->t2[i2], bit, 4);    kf_upd(&mm->t3[i3], bit, 4);      \
        kf_upd(&mm->t4[i4], bit, 3);    kf_upd(&mm->t5[i5], bit, 3);      \
        kf_upd(&mm->t678[i678], bit, 3);                                    \
        kf_upd(&mm->tstate[ist], bit, 3);                                   \
        kf_upd(&mm->tword[iw], bit, 4); kf_upd(&mm->tsparse[isp], bit, 4);\
        kf_upd(&mm->trun[irun], bit, 4);                                    \
        kf_upd(&mm->sse[si], bit, 3);   kf_upd(&mm->apm[api], bit, 4);    \
        /* v62: SIMD weight update */                                        \
        {                                                                    \
            int32_t err_ = (bit ? 4096 : 0) - (int32_t)mx;                 \
            kf62_update_weights(mm->w[bp_], s_, err_);                      \
        }                                                                    \
        cx = (cx << 1) | bit;                                                \
    }                                                                        \
    switch (lz_state) {                                                      \
    case 0: if (byte == 0xFF) lz_state = 1; break;                          \
    case 1:                                                                  \
        if (byte == 0xFF) lz_state = 0;                                     \
        else if (byte == 0xFE) lz_state = 4;                                \
        else if (byte == 0xFD) lz_state = 5;                                \
        else if (byte == 0xFC) lz_state = 6;                                \
        else lz_state = 2;                                                   \
        break;                                                               \
    case 2: lz_state = 3; break;                                            \
    case 3: lz_state = 0; break;                                            \
    case 4: lz_state = 0; break;                                            \
    case 5: lz_state = 0; break;                                            \
    case 6: lz_state = 7; break;                                            \
    case 7: lz_state = 8; break;                                            \
    case 8: lz_state = 9; break;                                            \
    case 9: lz_state = 0; break;                                            \
    default: lz_state = 0; break;                                            \
    }                                                                        \
    memmove(hist + 1, hist, 7);                                              \
    hist[0] = byte;                                                          \
} while(0)

/* --- Компрессор v51 --- */
static size_t compress_formula_v51(
    const uint8_t *input, size_t input_size,
    uint8_t *output, size_t output_max)
{
    if (input_size == 0 || output_max < 16) return 0;
    KF51M m; if (!kf51_init(&m)) { kf51_destroy(&m); return 0; }
    KF51M *mm = &m;
    KolibriRC rc; krc_enc_init(&rc, output, output_max);
    uint8_t hist[8] = {0};
    int lz_state = 0;

    for (size_t i = 0; i < input_size; i++) {
        uint8_t byte = input[i];
        KF51_PROCESS_BYTE(1);
    }

    krc_enc_flush(&rc);
    kf51_destroy(&m);
    if (rc.pos >= input_size) return 0;
    return rc.pos;
}

/* --- Декомпрессор v51 --- */
static size_t decompress_formula_v51(
    const uint8_t *input, size_t input_size,
    uint8_t *output, size_t output_max,
    size_t original_size)
{
    if (original_size == 0 || original_size > output_max) return 0;
    KF51M m; if (!kf51_init(&m)) { kf51_destroy(&m); return 0; }
    KF51M *mm = &m;
    KolibriRC rc; krc_dec_init(&rc, input, input_size);
    uint8_t hist[8] = {0};
    int lz_state = 0;

    for (size_t i = 0; i < original_size; i++) {
        uint8_t byte = 0;
        KF51_PROCESS_BYTE(0);
        output[i] = byte;
    }

    kf51_destroy(&m);
    return original_size;
}

/* =====================================================================
 * v62 COMPRESS/DECOMPRESS: merged model + SIMD + multi-threading
 * =====================================================================
 * Формат: [num_blocks:2] [block_csizes:4*N] [block_data...]
 * Каждый блок ≤64KB, независимая модель, параллельное сжатие.
 * ===================================================================== */

/* --- Блочный компрессор v62 (один блок, одна модель) --- */
static size_t compress_formula_v62_block(
    const uint8_t *input, size_t input_size,
    uint8_t *output, size_t output_max)
{
    if (input_size == 0 || output_max < 16) return 0;
    KF62M m; if (!kf62_init(&m)) { kf62_destroy(&m); return 0; }
    KF62M *mm = &m;
    KolibriRC rc; krc_enc_init(&rc, output, output_max);
    uint8_t hist[8] = {0};
    int lz_state = 0;

    for (size_t i = 0; i < input_size; i++) {
        uint8_t byte = input[i];
        KF62_PROCESS_BYTE(1);
    }

    krc_enc_flush(&rc);
    kf62_destroy(&m);
    if (rc.pos >= input_size) return 0;
    return rc.pos;
}

/* --- Блочный декомпрессор v62 --- */
static size_t decompress_formula_v62_block(
    const uint8_t *input, size_t input_size,
    uint8_t *output, size_t output_max,
    size_t original_size)
{
    if (original_size == 0 || original_size > output_max) return 0;
    KF62M m; if (!kf62_init(&m)) { kf62_destroy(&m); return 0; }
    KF62M *mm = &m;
    KolibriRC rc; krc_dec_init(&rc, input, input_size);
    uint8_t hist[8] = {0};
    int lz_state = 0;

    for (size_t i = 0; i < original_size; i++) {
        uint8_t byte = 0;
        KF62_PROCESS_BYTE(0);
        output[i] = byte;
    }

    kf62_destroy(&m);
    return original_size;
}

/* --- Многопоточная обёртка v62 --- */
#if KF_USE_THREADS
typedef struct {
    const uint8_t *input;
    size_t input_size;
    uint8_t *output;
    size_t output_max;
    size_t original_size;   /* для декомпрессии */
    size_t result_size;
} KF62ThreadArg;

static void *kf62_compress_worker(void *arg) {
    KF62ThreadArg *a = (KF62ThreadArg *)arg;
    a->result_size = compress_formula_v62_block(
        a->input, a->input_size, a->output, a->output_max);
    return NULL;
}

static void *kf62_decompress_worker(void *arg) {
    KF62ThreadArg *a = (KF62ThreadArg *)arg;
    a->result_size = decompress_formula_v62_block(
        a->input, a->input_size, a->output, a->output_max, a->original_size);
    return NULL;
}
#endif

/* --- Основной компрессор v62 (блочный + потоковый) --- */
static size_t compress_formula_v62(
    const uint8_t *input, size_t input_size,
    uint8_t *output, size_t output_max)
{
    if (input_size == 0 || output_max < 16) return 0;

    size_t nblocks = (input_size + KF62_BLOCK_SIZE - 1) / KF62_BLOCK_SIZE;
    if (nblocks > 65535) nblocks = 65535;

    size_t hdr_size = 2 + nblocks * 4;
    if (hdr_size >= output_max) return 0;

    uint8_t **bbufs = (uint8_t **)calloc(nblocks, sizeof(uint8_t*));
    size_t *bsizes  = (size_t *)calloc(nblocks, sizeof(size_t));
    size_t *bcsizes = (size_t *)calloc(nblocks, sizeof(size_t));
    if (!bbufs || !bsizes || !bcsizes) goto v62c_fail;

    for (size_t i = 0; i < nblocks; i++) {
        size_t off = i * KF62_BLOCK_SIZE;
        bsizes[i] = MIN((size_t)KF62_BLOCK_SIZE, input_size - off);
        bbufs[i] = (uint8_t *)malloc(bsizes[i] + 256);
        if (!bbufs[i]) goto v62c_fail;
    }

#if KF_USE_THREADS
    {
        size_t max_threads = 4;
        KF62ThreadArg *args = (KF62ThreadArg *)calloc(nblocks, sizeof(KF62ThreadArg));
        pthread_t *tids = (pthread_t *)calloc(nblocks, sizeof(pthread_t));
        if (!args || !tids) { free(args); free(tids); goto v62c_fail; }

        for (size_t start = 0; start < nblocks; start += max_threads) {
            size_t batch = MIN(max_threads, nblocks - start);
            for (size_t j = 0; j < batch; j++) {
                size_t idx = start + j;
                args[idx].input      = input + idx * KF62_BLOCK_SIZE;
                args[idx].input_size = bsizes[idx];
                args[idx].output     = bbufs[idx];
                args[idx].output_max = bsizes[idx] + 256;
                pthread_create(&tids[idx], NULL, kf62_compress_worker, &args[idx]);
            }
            for (size_t j = 0; j < batch; j++)
                pthread_join(tids[start + j], NULL);
            for (size_t j = 0; j < batch; j++)
                bcsizes[start + j] = args[start + j].result_size;
        }
        free(args); free(tids);
    }
#else
    for (size_t i = 0; i < nblocks; i++) {
        bcsizes[i] = compress_formula_v62_block(
            input + i * KF62_BLOCK_SIZE, bsizes[i],
            bbufs[i], bsizes[i] + 256);
    }
#endif

    /* Записываем заголовок блоков */
    output[0] = (uint8_t)(nblocks & 0xFF);
    output[1] = (uint8_t)((nblocks >> 8) & 0xFF);
    size_t pos = 2;
    for (size_t i = 0; i < nblocks; i++) {
        uint32_t cs = (uint32_t)bcsizes[i];
        memcpy(output + pos, &cs, 4);
        pos += 4;
    }

    /* Записываем данные блоков */
    for (size_t i = 0; i < nblocks; i++) {
        if (bcsizes[i] == 0) {
            /* Несжимаемый блок — raw */
            if (pos + bsizes[i] > output_max) goto v62c_fail;
            memcpy(output + pos, input + i * KF62_BLOCK_SIZE, bsizes[i]);
            pos += bsizes[i];
        } else {
            if (pos + bcsizes[i] > output_max) goto v62c_fail;
            memcpy(output + pos, bbufs[i], bcsizes[i]);
            pos += bcsizes[i];
        }
    }

    for (size_t i = 0; i < nblocks; i++) free(bbufs[i]);
    free(bbufs); free(bsizes); free(bcsizes);

    if (pos >= input_size) return 0;
    return pos;

v62c_fail:
    if (bbufs) { for (size_t i = 0; i < nblocks; i++) free(bbufs[i]); }
    free(bbufs); free(bsizes); free(bcsizes);
    return 0;
}

/* --- Основной декомпрессор v62 (блочный + потоковый) --- */
static size_t decompress_formula_v62(
    const uint8_t *input, size_t input_size,
    uint8_t *output, size_t output_max,
    size_t original_size)
{
    if (original_size == 0 || original_size > output_max || input_size < 2) return 0;

    uint16_t nblocks = (uint16_t)input[0] | ((uint16_t)input[1] << 8);
    if (nblocks == 0 || input_size < 2 + (size_t)nblocks * 4) return 0;

    size_t hpos = 2;
    uint32_t *csizes = (uint32_t *)malloc(nblocks * sizeof(uint32_t));
    if (!csizes) return 0;
    for (uint16_t i = 0; i < nblocks; i++) {
        memcpy(&csizes[i], input + hpos, 4);
        hpos += 4;
    }

#if KF_USE_THREADS
    {
        /* Предрассчитываем смещения блоков */
        size_t *offsets = (size_t *)malloc((nblocks + 1) * sizeof(size_t));
        size_t *bsizes  = (size_t *)malloc(nblocks * sizeof(size_t));
        if (!offsets || !bsizes) { free(csizes); free(offsets); free(bsizes); return 0; }

        offsets[0] = hpos;
        for (uint16_t i = 0; i < nblocks; i++) {
            bsizes[i] = MIN((size_t)KF62_BLOCK_SIZE, original_size - (size_t)i * KF62_BLOCK_SIZE);
            offsets[i + 1] = offsets[i] + (csizes[i] == 0 ? bsizes[i] : csizes[i]);
        }

        size_t max_threads = 4;
        KF62ThreadArg *args = (KF62ThreadArg *)calloc(nblocks, sizeof(KF62ThreadArg));
        pthread_t *tids = (pthread_t *)calloc(nblocks, sizeof(pthread_t));
        if (!args || !tids) {
            free(csizes); free(offsets); free(bsizes);
            free(args); free(tids);
            return 0;
        }

        for (size_t start = 0; start < nblocks; start += max_threads) {
            size_t batch = MIN(max_threads, (size_t)nblocks - start);
            for (size_t j = 0; j < batch; j++) {
                size_t idx = start + j;
                size_t ooff = idx * KF62_BLOCK_SIZE;
                if (csizes[idx] == 0) {
                    /* Raw block — прямое копирование */
                    memcpy(output + ooff, input + offsets[idx], bsizes[idx]);
                    args[idx].result_size = bsizes[idx];
                } else {
                    args[idx].input         = input + offsets[idx];
                    args[idx].input_size    = csizes[idx];
                    args[idx].output        = output + ooff;
                    args[idx].output_max    = bsizes[idx];
                    args[idx].original_size = bsizes[idx];
                    pthread_create(&tids[idx], NULL, kf62_decompress_worker, &args[idx]);
                }
            }
            for (size_t j = 0; j < batch; j++) {
                size_t idx = start + j;
                if (csizes[idx] != 0)
                    pthread_join(tids[idx], NULL);
            }
            /* Проверяем результат */
            for (size_t j = 0; j < batch; j++) {
                size_t idx = start + j;
                if (csizes[idx] != 0 && args[idx].result_size != bsizes[idx]) {
                    free(csizes); free(offsets); free(bsizes);
                    free(args); free(tids);
                    return 0;
                }
            }
        }

        free(csizes); free(offsets); free(bsizes);
        free(args); free(tids);
        return original_size;
    }
#else
    {
        size_t pos = hpos;
        size_t out_pos = 0;
        for (uint16_t i = 0; i < nblocks; i++) {
            size_t bsize = MIN((size_t)KF62_BLOCK_SIZE, original_size - out_pos);
            if (csizes[i] == 0) {
                if (pos + bsize > input_size) { free(csizes); return 0; }
                memcpy(output + out_pos, input + pos, bsize);
                pos += bsize;
            } else {
                if (pos + csizes[i] > input_size) { free(csizes); return 0; }
                size_t dec = decompress_formula_v62_block(
                    input + pos, csizes[i],
                    output + out_pos, bsize, bsize);
                if (dec != bsize) { free(csizes); return 0; }
                pos += csizes[i];
            }
            out_pos += bsize;
        }
        free(csizes);
        return out_pos;
    }
#endif
}

int kolibri_compress(KolibriCompressor *comp,
                     const uint8_t *input,
                     size_t input_size,
                     uint8_t **output,
                     size_t *output_size,
                     KolibriCompressStats *stats) {
    if (!comp || !input || !output || !output_size) {
        return -1;
    }

    double start_time = get_time_ms();

    /* Detect file type */
    KolibriFileType file_type = kolibri_detect_file_type(input, input_size);

    /* Allocate output buffer */
    size_t max_output = sizeof(KolibriCompressHeader) + input_size + 16384;
    uint8_t *out_buf = (uint8_t *)malloc(max_output);
    if (!out_buf) return -1;

    size_t header_size = sizeof(KolibriCompressHeader);
    uint8_t *compressed_data = out_buf + header_size;

    size_t compressed_size = input_size;
    uint32_t methods_used = 0;

    /* === KOLIBRI v52 HYBRID: Token LZ-lite + Formula ===
     * Этап 1: LZ-lite убирает длинные повторы (дёшево)
     * Этап 2: Formula кодирует остаток (адаптивно) */

    /* Буферы */
    uint8_t *lz_buf = (uint8_t *)malloc(input_size + 1024);
    uint8_t *temp = (uint8_t *)malloc(input_size + 1024);
    if (!lz_buf || !temp) { free(lz_buf); free(temp); free(out_buf); return -1; }

    /* Token-level text stream (v52) */
    TokenDict tdict = {0};
    uint8_t *token_buf = NULL;
    size_t token_size = 0;
    int token_used = 0;
    size_t token_dict_bytes = 0;

    const uint8_t *formula_input = input;
    size_t formula_input_size = input_size;
    int lz_used = 0;

    /* v63: TURBO mode (default) — skip token dict + formula CM.
     * Установите KOLIBRI_QUALITY=max для полного v62 pipeline. */
    int turbo = 1;
    { const char *q = getenv("KOLIBRI_QUALITY");
      if (q && strcmp(q, "max") == 0) turbo = 0; }

    if (file_type == KOLIBRI_FILE_TEXT && !turbo) {
        if (token_dict_build(&tdict, input, input_size)) {
            token_buf = (uint8_t *)malloc(input_size * 2 + 16);
            if (token_buf) {
                token_size = token_encode_text(input, input_size,
                                               token_buf, input_size * 2 + 16,
                                               &tdict);
                if (token_size > 0 && token_size < input_size) {
                    token_used = 1;
                    formula_input = token_buf;
                    formula_input_size = token_size;
                } else {
                    free(token_buf);
                    token_buf = NULL;
                    token_dict_free(&tdict);
                }
            } else {
                token_dict_free(&tdict);
            }
        }
    }

    /* Этап 1: LZ — turbo (greedy, 50-100× быстрее) или lite (цепочки) */
    size_t lz_size = turbo
        ? lz_turbo_encode(formula_input, formula_input_size,
                          lz_buf, formula_input_size + 1024)
        : lz_lite_encode(formula_input, formula_input_size,
                         lz_buf, formula_input_size + 1024);
    if (lz_size > 0 && lz_size < formula_input_size) {
        formula_input = lz_buf;
        formula_input_size = lz_size;
        lz_used = 1;
    }

    /* Этап 2: Формульное сжатие (QUALITY only — в TURBO пропускаем) */
    size_t formula_size = 0;
    if (!turbo) {
        formula_size = compress_formula_v62(formula_input, formula_input_size,
                                           temp, input_size + 1024);
    }
    if (formula_size > 0 && formula_size < formula_input_size) {
        size_t prefix_size = 0;
        uint8_t *payload = compressed_data;
        methods_used = KOLIBRI_COMPRESS_FORMULA;
        if (token_used) {
            token_dict_bytes = token_dict_write(&tdict, payload,
                                                max_output - header_size);
            if (token_dict_bytes == 0) {
                methods_used = 0;
                compressed_size = input_size;
                memcpy(compressed_data, input, input_size);
                goto compress_done;
            } else {
                payload += token_dict_bytes;
                uint32_t token_intermediate = (uint32_t)token_size;
                memcpy(payload, &token_intermediate, 4);
                payload += 4;
                prefix_size = token_dict_bytes + 4;
                methods_used |= KOLIBRI_COMPRESS_TOKEN;
            }
        }
        if (lz_used) {
            methods_used |= KOLIBRI_COMPRESS_LZ77;
            uint32_t lz_intermediate = (uint32_t)formula_input_size;
            memcpy(payload, &lz_intermediate, 4);
            payload += 4;
            prefix_size += 4;
        }
        memcpy(payload, temp, formula_size);
        compressed_size = prefix_size + formula_size;
        if (compressed_size >= input_size) {
            methods_used = 0;
            compressed_size = input_size;
            memcpy(compressed_data, input, input_size);
        }
    } else if (lz_used) {
        size_t prefix_size = 0;
        uint8_t *payload = compressed_data;
        methods_used = KOLIBRI_COMPRESS_LZ77;
        if (token_used) {
            token_dict_bytes = token_dict_write(&tdict, payload,
                                                max_output - header_size);
            if (token_dict_bytes == 0) {
                methods_used = 0;
                compressed_size = input_size;
                memcpy(compressed_data, input, input_size);
                goto compress_done;
            } else {
                payload += token_dict_bytes;
                uint32_t token_intermediate = (uint32_t)token_size;
                memcpy(payload, &token_intermediate, 4);
                payload += 4;
                prefix_size = token_dict_bytes + 4;
                methods_used |= KOLIBRI_COMPRESS_TOKEN;
            }
        }
        memcpy(payload, lz_buf, lz_size);
        compressed_size = prefix_size + lz_size;
        if (compressed_size >= input_size) {
            methods_used = 0;
            compressed_size = input_size;
            memcpy(compressed_data, input, input_size);
        }
    } else {
        /* Данные несжимаемы — сохраняем как есть */
        compressed_size = input_size;
        methods_used = 0;
        memcpy(compressed_data, input, input_size);
    }

compress_done:
    free(lz_buf);
    free(temp);
    free(token_buf);
    token_dict_free(&tdict);

    /* Fill header */
    KolibriCompressHeader *header = (KolibriCompressHeader *)out_buf;
    header->magic = KOLIBRI_COMPRESS_MAGIC;
    header->version = KOLIBRI_COMPRESS_VERSION;
    header->methods = methods_used;
    header->original_size = (uint32_t)input_size;
    header->compressed_size = (uint32_t)compressed_size;
    header->checksum = kolibri_checksum(input, input_size);
    header->file_type = file_type;
    memset(header->reserved, 0, sizeof(header->reserved));

    *output = out_buf;
    *output_size = header_size + compressed_size;

    /* Fill statistics */
    if (stats) {
        stats->original_size = input_size;
        stats->compressed_size = *output_size;
        stats->compression_ratio = (double)input_size / (double)*output_size;
        stats->checksum = header->checksum;
        stats->file_type = file_type;
        stats->methods_used = methods_used;
        stats->compression_time_ms = get_time_ms() - start_time;
        stats->decompression_time_ms = 0;
    }

    return 0;
}

int kolibri_decompress(const uint8_t *input,
                       size_t input_size,
                       uint8_t **output,
                       size_t *output_size,
                       KolibriCompressStats *stats) {
    if (!input || !output || !output_size || input_size < sizeof(KolibriCompressHeader)) {
        return -1;
    }

    double start_time = get_time_ms();

    /* Read and verify header */
    const KolibriCompressHeader *header = (const KolibriCompressHeader *)input;
    if (header->magic != KOLIBRI_COMPRESS_MAGIC) {
        return -1; /* Invalid format */
    }
    /* Support versions 1-40 for backward compatibility */
    if (header->version < 1 || header->version > KOLIBRI_COMPRESS_VERSION) {
        return -1; /* Unsupported version */
    }

    const uint8_t *compressed_data = input + sizeof(KolibriCompressHeader);
    size_t compressed_size = header->compressed_size;
    size_t original_size = header->original_size;

    /* Allocate output buffer */
    uint8_t *out_buf = (uint8_t *)malloc(original_size);
    if (!out_buf) return -1;

    /* Allocate temporary buffers */
    uint8_t *temp1 = (uint8_t *)malloc(original_size * 2);
    uint8_t *temp2 = (uint8_t *)malloc(original_size * 2);
    if (!temp1 || !temp2) {
        free(out_buf);
        free(temp1);
        free(temp2);
        return -1;
    }

    /* Copy compressed data to temp buffer */
    memcpy(temp1, compressed_data, compressed_size);
    size_t current_size = compressed_size;
    const uint8_t *current_data = temp1;

    /* Token dictionary (v52) */
    TokenDict tdict = {0};
    size_t token_size = original_size;
    int token_used = 0;
    if ((header->methods & KOLIBRI_COMPRESS_TOKEN) && header->version >= 52) {
        size_t dict_consumed = 0;
        if (!token_dict_read(current_data, current_size, &dict_consumed, &tdict)) {
            free(out_buf); free(temp1); free(temp2);
            return -1;
        }
        current_data += dict_consumed;
        current_size -= dict_consumed;
        if (current_size < 4) {
            token_dict_free(&tdict);
            free(out_buf); free(temp1); free(temp2);
            return -1;
        }
        uint32_t token_intermediate = 0;
        memcpy(&token_intermediate, current_data, 4);
        token_size = (size_t)token_intermediate;
        current_data += 4;
        current_size -= 4;
        token_used = 1;
    }

    /* Decompress in reverse order of compression */

    /* === v51: Формульная декомпрессия === */
    if (header->methods & KOLIBRI_COMPRESS_FORMULA) {
        /* Если и LZ и Formula — первые 4 байта = размер LZ-промежуточных данных */
        size_t formula_target = token_used ? token_size : original_size;
        const uint8_t *formula_data = current_data;
        size_t formula_data_size = current_size;

        if ((header->methods & KOLIBRI_COMPRESS_LZ77) && header->version >= 51) {
            /* Читаем lz_intermediate_size из префикса */
            if (current_size < 4) {
                token_dict_free(&tdict);
                free(out_buf); free(temp1); free(temp2);
                return -1;
            }
            uint32_t lz_intermediate;
            memcpy(&lz_intermediate, current_data, 4);
            formula_target = (size_t)lz_intermediate;
            formula_data = current_data + 4;
            formula_data_size = current_size - 4;
        }

        size_t formula_size;
        if (header->version >= 62) {
            formula_size = decompress_formula_v62(
                formula_data, formula_data_size, temp2, original_size * 2, formula_target);
        } else {
            formula_size = decompress_formula_v51(
                formula_data, formula_data_size, temp2, original_size * 2, formula_target);
        }
        if (formula_size == 0 || formula_size != formula_target) {
            token_dict_free(&tdict);
            free(out_buf); free(temp1); free(temp2);
            return -1;
        }
        memcpy(temp1, temp2, formula_size);
        current_size = formula_size;
        current_data = temp1;
    }

    /* Huffman/ANS decompression (backward compat) */
    if (header->methods & KOLIBRI_COMPRESS_HUFFMAN) {
        size_t huff_size = kolibri_entropy_decompress(current_data, current_size, temp2, original_size * 2);
        if (huff_size == 0) {
            token_dict_free(&tdict);
            free(out_buf);
            free(temp1);
            free(temp2);
            return -1;
        }
        memcpy(temp1, temp2, huff_size);
        current_size = huff_size;
        current_data = temp1;
    }

    /* RLE decompression */
    if (header->methods & KOLIBRI_COMPRESS_RLE) {
        size_t rle_size = decompress_rle(current_data, current_size, temp2, original_size * 2);
        if (rle_size == 0) {
            token_dict_free(&tdict);
            free(out_buf);
            free(temp1);
            free(temp2);
            return -1;
        }
        memcpy(temp1, temp2, rle_size);
        current_size = rle_size;
        current_data = temp1;
    }

    /* LZ-lite decompression (v51 hybrid) или старый LZ77 (backward compat) */
    if (header->methods & KOLIBRI_COMPRESS_LZ77) {
        size_t lz_size;
        if (header->version >= 51) {
            /* v51+: LZ-lite формат */
            size_t lz_target = token_used ? token_size : original_size;
            lz_size = lz_lite_decode(current_data, current_size,
                                     temp2, original_size * 2, lz_target);
        } else {
            /* v50 и ранее: старый LZ77 */
            lz_size = decompress_lz77(current_data, current_size, temp2, original_size * 2);
        }
        if (lz_size == 0) {
            token_dict_free(&tdict);
            free(out_buf);
            free(temp1);
            free(temp2);
            return -1;
        }
        memcpy(temp1, temp2, lz_size);
        current_size = lz_size;
        current_data = temp1;
    }

    /* Token detokenization (v52) */
    if (token_used) {
        size_t detok_size = token_decode_text(current_data, current_size,
                                              temp2, original_size * 2,
                                              &tdict, original_size);
        if (detok_size == 0 || detok_size != original_size) {
            token_dict_free(&tdict);
            free(out_buf); free(temp1); free(temp2);
            return -1;
        }
        memcpy(temp1, temp2, detok_size);
        current_size = detok_size;
        current_data = temp1;
    }

    /* Mathematical decompression */
    if (header->methods & KOLIBRI_COMPRESS_MATH) {
        /* Pass strict original_size as limit to ensure block logic works correctly */
        size_t math_size = decompress_mathematical(current_data, current_size, temp2, original_size, header->version);
        
        if (math_size == 0 || math_size != original_size) {
            token_dict_free(&tdict);
            free(out_buf);
            free(temp1);
            free(temp2);
            return -1;
        }
        memcpy(temp1, temp2, math_size);
        current_size = math_size;
        current_data = temp1;
    }

    /* Copy final decompressed data */
    memcpy(out_buf, current_data, current_size);

    /* Verify checksum */
    uint32_t checksum = kolibri_checksum(out_buf, current_size);
    if (checksum != header->checksum) {
        token_dict_free(&tdict);
        free(out_buf);
        free(temp1);
        free(temp2);
        return -1; /* Checksum mismatch */
    }

    *output = out_buf;
    *output_size = current_size;

    token_dict_free(&tdict);
    free(temp1);
    free(temp2);

    /* Fill statistics */
    if (stats) {
        stats->original_size = original_size;
        stats->compressed_size = input_size;
        stats->compression_ratio = (double)original_size / (double)input_size;
        stats->checksum = header->checksum;
        stats->file_type = header->file_type;
        stats->methods_used = header->methods;
        stats->compression_time_ms = 0;
        stats->decompression_time_ms = get_time_ms() - start_time;
    }

    return 0;
}

/* Archive management implementation */
#define KOLIBRI_ARCHIVE_MAGIC 0x4B415243 /* "KARC" */
#define KOLIBRI_ARCHIVE_VERSION 50
#define KOLIBRI_ARCHIVE_MAX_ENTRIES 1024

typedef struct {
    KolibriArchiveEntry entry;
    size_t data_offset;
    size_t data_size;
} KolibriArchiveEntryInternal;

struct KolibriArchive {
    char filename[512];
    FILE *file;
    KolibriArchiveEntryInternal entries[KOLIBRI_ARCHIVE_MAX_ENTRIES];
    size_t entry_count;
    int mode; /* 0 = read, 1 = write */
};

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t entry_count;
    uint8_t reserved[52];
} KolibriArchiveHeader;

KolibriArchive *kolibri_archive_create(const char *filename) {
    if (!filename) return NULL;

    KolibriArchive *archive = (KolibriArchive *)calloc(1, sizeof(KolibriArchive));
    if (!archive) return NULL;

    strncpy(archive->filename, filename, sizeof(archive->filename) - 1);
    archive->file = fopen(filename, "wb");
    if (!archive->file) {
        free(archive);
        return NULL;
    }

    archive->mode = 1; /* write mode */
    archive->entry_count = 0;

    /* Write placeholder header */
    KolibriArchiveHeader header = {0};
    header.magic = KOLIBRI_ARCHIVE_MAGIC;
    header.version = KOLIBRI_ARCHIVE_VERSION;
    fwrite(&header, sizeof(header), 1, archive->file);

    /* Reserve space for entries table following the header */
    uint8_t *placeholder = (uint8_t *)calloc(KOLIBRI_ARCHIVE_MAX_ENTRIES, sizeof(KolibriArchiveEntryInternal));
    if (placeholder) {
        fwrite(placeholder, sizeof(KolibriArchiveEntryInternal), KOLIBRI_ARCHIVE_MAX_ENTRIES, archive->file);
        free(placeholder);
    }

    return archive;
}

KolibriArchive *kolibri_archive_open(const char *filename) {
    if (!filename) return NULL;

    FILE *file = fopen(filename, "rb");
    if (!file) return NULL;

    KolibriArchiveHeader header;
    if (fread(&header, sizeof(header), 1, file) != 1 ||
        header.magic != KOLIBRI_ARCHIVE_MAGIC ||
        header.version < 1 || header.version > KOLIBRI_ARCHIVE_VERSION) {
        fclose(file);
        return NULL;
    }

    KolibriArchive *archive = (KolibriArchive *)calloc(1, sizeof(KolibriArchive));
    if (!archive) {
        fclose(file);
        return NULL;
    }

    strncpy(archive->filename, filename, sizeof(archive->filename) - 1);
    archive->file = file;
    archive->mode = 0; /* read mode */
    archive->entry_count = header.entry_count;

    /* Read entry table */
    for (size_t i = 0; i < header.entry_count && i < KOLIBRI_ARCHIVE_MAX_ENTRIES; i++) {
        if (fread(&archive->entries[i], sizeof(KolibriArchiveEntryInternal), 1, file) != 1) {
            fclose(file);
            free(archive);
            return NULL;
        }
    }

    return archive;
}

int kolibri_archive_add_file(KolibriArchive *archive,
                              const char *filename,
                              const uint8_t *data,
                              size_t size) {
    if (!archive || !filename || !data || archive->mode != 1) {
        return -1;
    }

    if (archive->entry_count >= KOLIBRI_ARCHIVE_MAX_ENTRIES) {
        return -1; /* Archive full */
    }

    /* Compress the data */
    KolibriCompressor *comp = kolibri_compressor_create(KOLIBRI_COMPRESS_ALL);
    if (!comp) return -1;

    uint8_t *compressed = NULL;
    size_t compressed_size = 0;
    KolibriCompressStats stats;

    int ret = kolibri_compress(comp, data, size, &compressed, &compressed_size, &stats);
    kolibri_compressor_destroy(comp);

    if (ret != 0) {
        return -1;
    }

    /* Get current file position for data offset */
    long data_offset = ftell(archive->file);
    if (data_offset < 0) {
        free(compressed);
        return -1;
    }

    /* Write compressed data */
    if (fwrite(compressed, 1, compressed_size, archive->file) != compressed_size) {
        free(compressed);
        return -1;
    }

    /* Add entry */
    KolibriArchiveEntryInternal *entry = &archive->entries[archive->entry_count];
    strncpy(entry->entry.name, filename, sizeof(entry->entry.name) - 1);
    entry->entry.original_size = size;
    entry->entry.compressed_size = compressed_size;
    entry->entry.checksum = stats.checksum;
    entry->entry.timestamp = (uint64_t)time(NULL);
    entry->entry.type = stats.file_type;
    entry->data_offset = (size_t)data_offset;
    entry->data_size = compressed_size;

    archive->entry_count++;

    free(compressed);
    return 0;
}

int kolibri_archive_extract_file(KolibriArchive *archive,
                                  const char *filename,
                                  uint8_t **data,
                                  size_t *size) {
    if (!archive || !filename || !data || !size) {
        return -1;
    }

    /* Find entry */
    KolibriArchiveEntryInternal *entry = NULL;
    for (size_t i = 0; i < archive->entry_count; i++) {
        if (strcmp(archive->entries[i].entry.name, filename) == 0) {
            entry = &archive->entries[i];
            break;
        }
    }

    if (!entry) {
        return -1; /* File not found */
    }

    /* Seek to data */
    if (fseek(archive->file, (long)entry->data_offset, SEEK_SET) != 0) {
        return -1;
    }

    /* Read compressed data */
    uint8_t *compressed = (uint8_t *)malloc(entry->data_size);
    if (!compressed) return -1;

    if (fread(compressed, 1, entry->data_size, archive->file) != entry->data_size) {
        free(compressed);
        return -1;
    }

    /* Decompress */
    uint8_t *decompressed = NULL;
    size_t decompressed_size = 0;
    int ret = kolibri_decompress(compressed, entry->data_size, &decompressed, 
                                  &decompressed_size, NULL);
    free(compressed);

    if (ret != 0) {
        return -1;
    }

    *data = decompressed;
    *size = decompressed_size;

    return 0;
}

int kolibri_archive_list(KolibriArchive *archive,
                         KolibriArchiveEntry **entries,
                         size_t *count) {
    if (!archive || !entries || !count) {
        return -1;
    }

    if (archive->entry_count == 0) {
        *entries = NULL;
        *count = 0;
        return 0;
    }

    /* Allocate and copy entries */
    KolibriArchiveEntry *entry_list = (KolibriArchiveEntry *)malloc(
        archive->entry_count * sizeof(KolibriArchiveEntry));
    if (!entry_list) return -1;

    for (size_t i = 0; i < archive->entry_count; i++) {
        entry_list[i] = archive->entries[i].entry;
    }

    *entries = entry_list;
    *count = archive->entry_count;

    return 0;
}

void kolibri_archive_close(KolibriArchive *archive) {
    if (!archive) return;

    if (archive->mode == 1) {
        /* Write mode - update header and entry table */
        fseek(archive->file, 0, SEEK_SET);

        KolibriArchiveHeader header = {0};
        header.magic = KOLIBRI_ARCHIVE_MAGIC;
        header.version = KOLIBRI_ARCHIVE_VERSION;
        header.entry_count = (uint32_t)archive->entry_count;
        fwrite(&header, sizeof(header), 1, archive->file);

        /* Write entry table */
        for (size_t i = 0; i < archive->entry_count; i++) {
            fwrite(&archive->entries[i], sizeof(KolibriArchiveEntryInternal), 1, archive->file);
        }
    }

    if (archive->file) {
        fclose(archive->file);
    }

    free(archive);
}


