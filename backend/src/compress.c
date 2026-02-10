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

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

/* Magic number for compressed data format */
#define KOLIBRI_COMPRESS_MAGIC 0x4B4C4252 /* "KLBR" */
#define KOLIBRI_COMPRESS_VERSION 52  /* v52: Token-level LZ-lite + Formula */

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

/* RLE compression */
static size_t compress_rle(const uint8_t *input, size_t input_size,
                           uint8_t *output, size_t output_size) {
    if (!input || !output || output_size < input_size * 2) {
        return 0;
    }

    size_t out_pos = 0;
    size_t i = 0;

    while (i < input_size) {
        uint8_t current = input[i];
        size_t run_length = 1;

        /* Count consecutive identical bytes */
        while (i + run_length < input_size && 
               input[i + run_length] == current && 
               run_length < 255) {
            run_length++;
        }

        if (run_length >= 4) {
            /* Use RLE encoding for runs of 4 or more */
            if (out_pos + 3 > output_size) return 0;
            output[out_pos++] = 0xFF; /* RLE marker */
            output[out_pos++] = (uint8_t)run_length;
            output[out_pos++] = current;
            i += run_length;
        } else {
            /* Copy literally, escaping marker bytes */
            for (size_t j = 0; j < run_length; j++) {
                if (current == 0xFF) {
                    /* Escape 0xFF as 0xFF 0x00 */
                    if (out_pos + 2 > output_size) return 0;
                    output[out_pos++] = 0xFF;
                    output[out_pos++] = 0x00;
                } else {
                    if (out_pos >= output_size) return 0;
                    output[out_pos++] = current;
                }
            }
            i += run_length;
        }
    }

    return out_pos;
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

/* =====================================================================
 * LZ77 v50 "Kolibri Titan" — Lazy Matching + Deep Hash Chains
 * =====================================================================
 * Улучшения v50 vs v40:
 * - Окно 64KB (было 32KB) — больше совпадений
 * - Цепочки до 1024 (было 128) — находит длинные совпадения
 * - 4-байтный хеш (был 3-байт) — меньше коллизий
 * - Lazy matching — проверяет позицию +1, выбирает лучшее
 * - Формат вывода совместим с v40
 * ===================================================================== */
#define LZ77_WINDOW_SIZE 65536     /* 64KB (было 32KB) */
#define LZ77_MAX_MATCH 255
#define LZ77_HASH_BITS 16           /* 64K entries (было 32K) */
#define LZ77_HASH_SIZE (1 << LZ77_HASH_BITS)
#define LZ77_HASH_MASK (LZ77_HASH_SIZE - 1)
#define LZ77_MAX_CHAIN 1024        /* Глубокий поиск (было 128) */
#define LZ77_MIN_MATCH 4
#define LZ77_NICE_MATCH 128         /* Стоп если совпадение >= этого */

/* --- LZ77 v50: Helper — \u043f\u043e\u0438\u0441\u043a \u043b\u0443\u0447\u0448\u0435\u0433\u043e \u0441\u043e\u0432\u043f\u0430\u0434\u0435\u043d\u0438\u044f \u0432 \u0445\u0435\u0448-\u0446\u0435\u043f\u043e\u0447\u043a\u0435 --- */
static inline uint32_t lz77_hash4(const uint8_t *p) {
    uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                 ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return (v * 2654435761U) >> (32 - LZ77_HASH_BITS);
}

static size_t lz77_find_best(const uint8_t *input, size_t input_size,
                              const int32_t *head, const int32_t *prev,
                              size_t pos, size_t *out_dist) {
    *out_dist = 0;
    if (pos + 3 >= input_size) return 0;

    uint32_t h = lz77_hash4(input + pos);
    int32_t mp = head[h];
    size_t min_d = (pos > LZ77_WINDOW_SIZE) ? (pos - LZ77_WINDOW_SIZE) : 0;
    size_t best_len = 0;
    int chain = 0;

    while (mp != -1 && (size_t)mp >= min_d && chain++ < LZ77_MAX_CHAIN) {
        /* \u0411\u044b\u0441\u0442\u0440\u0430\u044f \u043f\u0440\u043e\u0432\u0435\u0440\u043a\u0430: \u0441\u043d\u0430\u0447\u0430\u043b\u0430 \u0431\u0430\u0439\u0442 \u043d\u0430 \u043f\u043e\u0437\u0438\u0446\u0438\u0438 best_len+1 */
        if (best_len < input_size - pos &&
            input[(size_t)mp + best_len] == input[pos + best_len]) {
            size_t ml = 0;
            size_t limit = MIN(LZ77_MAX_MATCH, input_size - pos);
            while (ml < limit && input[(size_t)mp + ml] == input[pos + ml]) ml++;

            if (ml > best_len) {
                best_len = ml;
                *out_dist = pos - (size_t)mp;
                if (best_len >= LZ77_NICE_MATCH) break; /* \u0414\u043e\u0441\u0442\u0430\u0442\u043e\u0447\u043d\u043e \u0445\u043e\u0440\u043e\u0448\u0435\u0435 \u0441\u043e\u0432\u043f\u0430\u0434\u0435\u043d\u0438\u0435 */
            }
        }
        int32_t next = prev[(size_t)mp & (LZ77_WINDOW_SIZE - 1)];
        if (next == mp) break;
        mp = next;
    }
    return best_len;
}

/* --- LZ77 v50: \u0441\u0436\u0430\u0442\u0438\u0435 \u0441 Lazy Matching --- */
static size_t compress_lz77(const uint8_t *input, size_t input_size,
                            uint8_t *output, size_t output_size) {
    if (!input || !output) return 0;
    if (input_size < LZ77_MIN_MATCH) {
        if (output_size < input_size) return 0;
        memcpy(output, input, input_size);
        return input_size;
    }

    int32_t *head = (int32_t *)malloc(LZ77_HASH_SIZE * sizeof(int32_t));
    int32_t *prev = (int32_t *)malloc(LZ77_WINDOW_SIZE * sizeof(int32_t));
    if (!head || !prev) { free(head); free(prev); return 0; }
    for (int i = 0; i < LZ77_HASH_SIZE; i++) head[i] = -1;

    size_t out_pos = 0;
    size_t in_pos = 0;

    /* \u0412\u0441\u043f\u043e\u043c\u043e\u0433\u0430\u0442\u0435\u043b\u044c\u043d\u044b\u0439 \u043c\u0430\u043a\u0440\u043e\u0441: \u0434\u043e\u0431\u0430\u0432\u0438\u0442\u044c \u043f\u043e\u0437\u0438\u0446\u0438\u044e \u0432 \u0445\u0435\u0448 */
    #define LZ77_INSERT(p) do { \
        if ((p) + 3 < input_size) { \
            uint32_t _h = lz77_hash4(input + (p)); \
            prev[(p) & (LZ77_WINDOW_SIZE - 1)] = head[_h]; \
            head[_h] = (int32_t)(p); \
        } \
    } while(0)

    #define LZ77_EMIT_LIT(byte) do { \
        if ((byte) == 0xFE) { \
            if (out_pos + 2 > output_size) goto lz77_fail; \
            output[out_pos++] = 0xFE; output[out_pos++] = 0xFF; \
        } else { \
            if (out_pos >= output_size) goto lz77_fail; \
            output[out_pos++] = (byte); \
        } \
    } while(0)

    while (in_pos < input_size) {
        size_t match_dist = 0;
        size_t match_len = lz77_find_best(input, input_size, head, prev,
                                           in_pos, &match_dist);
        LZ77_INSERT(in_pos);

        if (match_len >= LZ77_MIN_MATCH && match_dist <= 65535) {
            /* === Lazy Matching: \u043f\u0440\u043e\u0432\u0435\u0440\u044f\u0435\u043c pos+1 === */
            size_t next_dist = 0;
            size_t next_len = 0;
            if (in_pos + 1 < input_size) {
                next_len = lz77_find_best(input, input_size, head, prev,
                                           in_pos + 1, &next_dist);
            }

            if (next_len > match_len + 1 && next_dist <= 65535) {
                /* \u041f\u043e\u0437\u0438\u0446\u0438\u044f +1 \u043b\u0443\u0447\u0448\u0435 \u2014 \u0432\u044b\u0432\u043e\u0434\u0438\u043c \u043b\u0438\u0442\u0435\u0440\u0430\u043b \u0438 \u0431\u0435\u0440\u0451\u043c \u0441\u043e\u0432\u043f\u0430\u0434\u0435\u043d\u0438\u0435 \u043e\u0442\u0442\u0443\u0434\u0430 */
                LZ77_EMIT_LIT(input[in_pos]);
                in_pos++;
                LZ77_INSERT(in_pos);
                match_len = next_len;
                match_dist = next_dist;
            }

            /* \u0412\u044b\u0432\u043e\u0434\u0438\u043c \u0441\u043e\u0432\u043f\u0430\u0434\u0435\u043d\u0438\u0435 */
            if (out_pos + 4 > output_size) goto lz77_fail;
            output[out_pos++] = 0xFE;
            output[out_pos++] = (uint8_t)(match_dist >> 8);
            output[out_pos++] = (uint8_t)(match_dist & 0xFF);
            output[out_pos++] = (uint8_t)match_len;

            /* \u0414\u043e\u0431\u0430\u0432\u043b\u044f\u0435\u043c \u043f\u0440\u043e\u043f\u0443\u0449\u0435\u043d\u043d\u044b\u0435 \u043f\u043e\u0437\u0438\u0446\u0438\u0438 \u0432 \u0445\u0435\u0448 */
            for (size_t k = 1; k < match_len; k++) {
                LZ77_INSERT(in_pos + k);
            }
            in_pos += match_len;
        } else {
            LZ77_EMIT_LIT(input[in_pos]);
            in_pos++;
        }
    }

    #undef LZ77_INSERT
    #undef LZ77_EMIT_LIT

    free(head); free(prev);
    return out_pos;

lz77_fail:
    free(head); free(prev);
    return 0;
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

/* Глобальные переменные для сортировки ротаций BWT */
static const uint8_t *_bwt_doubled_buf;
static size_t _bwt_block_n;

static int bwt_rotation_cmp(const void *a, const void *b) {
    uint32_t i = *(const uint32_t *)a;
    uint32_t j = *(const uint32_t *)b;
    if (i == j) return 0;
    /* Полное сравнение через удвоенный буфер — гарантирует корректный BWT */
    int r = memcmp(_bwt_doubled_buf + i, _bwt_doubled_buf + j, _bwt_block_n);
    return r ? r : ((int)i - (int)j);
}

/* BWT Forward Transform v50: Radix Sort + qsort \u0432\u043d\u0443\u0442\u0440\u0438 \u0431\u0430\u043a\u0435\u0442\u043e\u0432 */
static int bwt_forward(const uint8_t *input, size_t len, uint8_t *output, uint32_t *primary_index) {
    if (!input || !output || !primary_index || len == 0) return -1;
    
    uint8_t *doubled = (uint8_t *)malloc(len * 2);
    uint32_t *sa = (uint32_t *)malloc(len * sizeof(uint32_t));
    if (!doubled || !sa) { free(doubled); free(sa); return -1; }
    
    /* \u0414\u0443\u0431\u043b\u0438\u0440\u0443\u0435\u043c \u0431\u0443\u0444\u0435\u0440 \u0434\u043b\u044f \u0431\u0435\u0437\u043c\u043e\u0434\u0443\u043b\u044c\u043d\u043e\u0433\u043e \u0441\u0440\u0430\u0432\u043d\u0435\u043d\u0438\u044f */
    memcpy(doubled, input, len);
    memcpy(doubled + len, input, len);
    
    /* === v50: Radix sort \u043f\u043e \u043f\u0435\u0440\u0432\u043e\u043c\u0443 \u0431\u0430\u0439\u0442\u0443, \u0437\u0430\u0442\u0435\u043c qsort \u0432\u043d\u0443\u0442\u0440\u0438 \u0431\u0430\u043a\u0435\u0442\u043e\u0432 ===
     * \u0423\u0441\u043a\u043e\u0440\u0435\u043d\u0438\u0435 ~256x \u043f\u0440\u043e\u0442\u0438\u0432 \u043f\u043e\u043b\u043d\u043e\u0433\u043e qsort, \u043f\u043e\u0437\u0432\u043e\u043b\u044f\u0435\u0442 128KB+ \u0431\u043b\u043e\u043a\u0438. */
    uint32_t counts[256] = {0};
    for (size_t i = 0; i < len; i++) counts[input[i]]++;
    
    uint32_t offsets[256];
    offsets[0] = 0;
    for (int c = 1; c < 256; c++) offsets[c] = offsets[c-1] + counts[c-1];
    
    uint32_t radix_run[256];
    memcpy(radix_run, offsets, sizeof(offsets));
    for (uint32_t i = 0; i < (uint32_t)len; i++) {
        sa[radix_run[input[i]]++] = i;
    }
    
    _bwt_doubled_buf = doubled;
    _bwt_block_n = len;
    
    /* qsort \u0442\u043e\u043b\u044c\u043a\u043e \u0432\u043d\u0443\u0442\u0440\u0438 \u0431\u0430\u043a\u0435\u0442\u043e\u0432 (\u044d\u043b\u0435\u043c\u0435\u043d\u0442\u044b \u0441 \u043e\u0434\u0438\u043d\u0430\u043a\u043e\u0432\u044b\u043c \u043f\u0435\u0440\u0432\u044b\u043c \u0431\u0430\u0439\u0442\u043e\u043c) */
    for (int c = 0; c < 256; c++) {
        if (counts[c] > 1) {
            qsort(sa + offsets[c], counts[c], sizeof(uint32_t), bwt_rotation_cmp);
        }
    }
    
    *primary_index = 0;
    for (size_t i = 0; i < len; i++) {
        if (sa[i] == 0) *primary_index = (uint32_t)i;
        output[i] = input[(sa[i] + len - 1) % len];
    }
    
    free(doubled);
    free(sa);
    return 0;
}

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

/* MTF Encode: Преобразует байты в ранги (маленькие числа для повторяющихся контекстов) */
static void mtf_encode(uint8_t *data, size_t len) {
    uint8_t table[256];
    for (int i = 0; i < 256; i++) table[i] = (uint8_t)i;
    
    for (size_t i = 0; i < len; i++) {
        uint8_t ch = data[i];
        uint8_t rank = 0;
        while (table[rank] != ch) rank++;
        
        data[i] = rank;
        /* Перемещаем символ в начало таблицы */
        memmove(table + 1, table, rank);
        table[0] = ch;
    }
}

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

/* \u0410\u0434\u0430\u043f\u0442\u0438\u0432\u043d\u043e\u0435 \u043f\u0440\u0435\u0434\u0441\u043a\u0430\u0437\u0430\u043d\u0438\u0435 v50: Order-1/2/3 \u0441 \u0441\u043c\u0435\u0448\u0438\u0432\u0430\u043d\u0438\u0435\u043c */
static void context_predict_encode(const uint8_t *input, size_t len, uint8_t *output) {
    /* \u0422\u0430\u0431\u043b\u0438\u0446\u044b \u043f\u0440\u0435\u0434\u0441\u043a\u0430\u0437\u0430\u043d\u0438\u0439 */
    uint8_t *pred1 = (uint8_t *)calloc(256, 1);           /* Order-1 */
    uint8_t *pred2 = (uint8_t *)calloc(CTX_TABLE_SIZE, 1); /* Order-2 */
    uint8_t *pred3 = (uint8_t *)calloc(CTX3_TABLE_SIZE, 1); /* Order-3 (\u0445\u0435\u0448) */
    uint8_t *hit1  = (uint8_t *)calloc(256, 1);           /* \u0421\u0447\u0451\u0442\u0447\u0438\u043a \u043f\u043e\u043f\u0430\u0434\u0430\u043d\u0438\u0439 O1 */
    uint8_t *hit2  = (uint8_t *)calloc(CTX_TABLE_SIZE, 1); /* \u0421\u0447\u0451\u0442\u0447\u0438\u043a \u043f\u043e\u043f\u0430\u0434\u0430\u043d\u0438\u0439 O2 */
    uint8_t *hit3  = (uint8_t *)calloc(CTX3_TABLE_SIZE, 1); /* \u0421\u0447\u0451\u0442\u0447\u0438\u043a \u043f\u043e\u043f\u0430\u0434\u0430\u043d\u0438\u0439 O3 */
    
    if (!pred1 || !pred2 || !pred3 || !hit1 || !hit2 || !hit3) {
        /* Fallback: \u043a\u043e\u043f\u0438\u0440\u043e\u0432\u0430\u043d\u0438\u0435 \u0431\u0435\u0437 \u043f\u0440\u0435\u0434\u0441\u043a\u0430\u0437\u0430\u043d\u0438\u044f */
        memcpy(output, input, len);
        free(pred1); free(pred2); free(pred3);
        free(hit1); free(hit2); free(hit3);
        return;
    }
    
    /* \u041f\u0435\u0440\u0432\u044b\u0435 3 \u0431\u0430\u0439\u0442\u0430 \u0441\u043e\u0445\u0440\u0430\u043d\u044f\u0435\u043c \u043a\u0430\u043a \u0435\u0441\u0442\u044c */
    if (len > 0) output[0] = input[0];
    if (len > 1) output[1] = input[1];
    if (len > 2) output[2] = input[2];
    
    for (size_t i = 3; i < len; i++) {
        /* \u041a\u043e\u043d\u0442\u0435\u043a\u0441\u0442\u044b */
        uint8_t ctx1_key = input[i-1];
        uint16_t ctx2_key = ((uint16_t)input[i-2] << 8) | (uint16_t)input[i-1];
        uint32_t ctx3_key = (((uint32_t)input[i-3] * 65599U) ^
                             ((uint32_t)input[i-2] * 257U) ^
                             (uint32_t)input[i-1]) & CTX3_MASK;
        
        /* \u041f\u0440\u0435\u0434\u0441\u043a\u0430\u0437\u0430\u043d\u0438\u044f \u043e\u0442 \u043a\u0430\u0436\u0434\u043e\u0433\u043e \u043a\u043e\u043d\u0442\u0435\u043a\u0441\u0442\u0430 */
        int p1 = (int)pred1[ctx1_key];
        int p2 = (int)pred2[ctx2_key];
        int p3 = (int)pred3[ctx3_key];
        int h1 = (int)hit1[ctx1_key];
        int h2 = (int)hit2[ctx2_key];
        int h3 = (int)hit3[ctx3_key];
        
        /* \u0412\u0437\u0432\u0435\u0448\u0435\u043d\u043d\u043e\u0435 \u0441\u043c\u0435\u0448\u0438\u0432\u0430\u043d\u0438\u0435: \u0431\u043e\u043b\u044c\u0448\u0435 \u043f\u043e\u043f\u0430\u0434\u0430\u043d\u0438\u0439 \u2192 \u0431\u043e\u043b\u044c\u0448\u0438\u0439 \u0432\u0435\u0441 */
        int w1 = 1 + h1;
        int w2 = 2 + h2 * 2;
        int w3 = 3 + h3 * 3;
        int total_w = w1 + w2 + w3;
        int predicted = (p1 * w1 + p2 * w2 + p3 * w3 + total_w / 2) / total_w;
        predicted = predicted & 0xFF;
        
        /* \u0421\u043e\u0445\u0440\u0430\u043d\u044f\u0435\u043c \u043e\u0448\u0438\u0431\u043a\u0443 (\u0440\u0435\u0437\u0438\u0434\u0443\u0430\u043b) */
        output[i] = input[i] - (uint8_t)predicted;
        
        /* \u041e\u0431\u043d\u043e\u0432\u043b\u044f\u0435\u043c \u0442\u0430\u0431\u043b\u0438\u0446\u044b \u0438 \u0441\u0447\u0451\u0442\u0447\u0438\u043a\u0438 \u043f\u043e\u043f\u0430\u0434\u0430\u043d\u0438\u0439 */
        uint8_t actual = input[i];
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

/* \u041e\u0431\u0440\u0430\u0442\u043d\u043e\u0435 \u043f\u0440\u0435\u0434\u0441\u043a\u0430\u0437\u0430\u043d\u0438\u0435 v50: \u0432\u043e\u0441\u0441\u0442\u0430\u043d\u043e\u0432\u043b\u0435\u043d\u0438\u0435 \u0438\u0437 \u043e\u0448\u0438\u0431\u043e\u043a */
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

/* Оценка энтропии блока (сумма квадратов частот — выше = лучше сжатие) */
static uint64_t estimate_compressibility(const uint8_t *data, size_t len) {
    uint32_t counts[256] = {0};
    for (size_t i = 0; i < len; i++) counts[data[i]]++;
    
    uint64_t sum_sq = 0;
    for (int i = 0; i < 256; i++) {
        sum_sq += (uint64_t)counts[i] * (uint64_t)counts[i];
    }
    return sum_sq; /* Выше = более сжимаемые данные */
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
#define KF_POPULATION     24   /* Популяция (баланс скорости/качества) — v50 */
#define KF_GENERATIONS    40   /* Поколений эволюции — v50 */
#define KF_SAMPLE_SIZE  2048   /* Выборка для оценки fitness — v50 */

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

/* --- Упаковка/распаковка генома: 48 цифр ↔ 24 байта --- */
static void kf_pack_gene(const uint8_t *digits, uint8_t *packed) {
    for (int i = 0; i < KF_GENE_PACKED; i++) {
        packed[i] = (uint8_t)((digits[i * 2] << 4) | (digits[i * 2 + 1] & 0x0F));
    }
}

static void kf_unpack_gene(const uint8_t *packed, uint8_t *digits) {
    for (int i = 0; i < KF_GENE_PACKED; i++) {
        digits[i * 2]     = (packed[i] >> 4) & 0x0F;
        digits[i * 2 + 1] = packed[i] & 0x0F;
    }
}

/* --- Эволюционное обучение формулы на блоке данных ---
 * Формула учится предсказывать data[i] по data[i-1].
 * Fitness = количество точных совпадений (residual=0) — это то, 
 * что максимизирует сжатие через ZRLE.
 */
static void kf_train_formula(KolibriCompactFormula *best,
                             const uint8_t *data, size_t len,
                             uint64_t block_seed) {
    KolibriRng evo_rng;
    k_rng_seed(&evo_rng, block_seed ^ 0xF0AAUL);
    
    KolibriCompactFormula population[KF_POPULATION];
    
    /* Инициализация: одна "нулевая" формула (predict=prev) + случайные */
    for (int p = 0; p < KF_POPULATION; p++) {
        if (p == 0) {
            /* Сид #0: identity (slope≈0, bias≈0 → output ≈ input) */
            for (int d = 0; d < KF_GENE_LEN; d += 8) {
                population[p].digits[d]   = 0; /* op=0: linear */
                population[p].digits[d+1] = 5; /* slope_hi → slope=0 */
                population[p].digits[d+2] = 0; /* slope_lo */
                population[p].digits[d+3] = 5; /* bias_hi → bias=0 */
                population[p].digits[d+4] = 0; /* bias_lo */
                population[p].digits[d+5] = 0;
                population[p].digits[d+6] = 0;
                population[p].digits[d+7] = 0;
            }
        } else if (p == 1) {
            /* Сид #1: "predict zero" (slope=0, bias=-prev → output≈0) */
            for (int d = 0; d < KF_GENE_LEN; d += 8) {
                population[p].digits[d]   = 0;
                population[p].digits[d+1] = 4; /* slope ≈ -10 */
                population[p].digits[d+2] = 0;
                population[p].digits[d+3] = 5;
                population[p].digits[d+4] = 0;
                population[p].digits[d+5] = 0;
                population[p].digits[d+6] = 5;
                population[p].digits[d+7] = 0;
            }
        } else {
            for (int d = 0; d < KF_GENE_LEN; d++) {
                population[p].digits[d] = (uint8_t)(k_rng_next(&evo_rng) % 10);
            }
        }
        population[p].fitness = -1e18;
    }
    
    /* Подготовка выборки: используем максимум данных */
    int num_samples = (int)(len < (size_t)KF_SAMPLE_SIZE ? len : (size_t)KF_SAMPLE_SIZE);
    size_t stride = len / (size_t)num_samples;
    if (stride == 0) stride = 1;
    
    /* === Эволюционный цикл === */
    for (int gen = 0; gen < KF_GENERATIONS; gen++) {
        /* Оценка fitness каждой формулы */
        for (int p = 0; p < KF_POPULATION; p++) {
            int zero_residuals = 0;
            int small_residuals = 0; /* |residual| <= 1 */
            double error_sum = 0;
            
            for (int s = 0; s < num_samples; s++) {
                size_t pos = (size_t)s * stride;
                if (pos >= len) break;
                
                /* Контекст = предыдущий байт (КЛЮЧЕВОЕ ИЗМЕНЕНИЕ!) */
                int prev_byte = (pos > 0) ? (int)data[pos - 1] : 0;
                int predicted = kf_compact_predict(&population[p], prev_byte);
                int actual = (int)data[pos];
                
                int diff = (actual - predicted + 128) & 0xFF;
                if (diff > 128) diff -= 256;
                
                if (diff == 0) zero_residuals++;
                if (diff >= -1 && diff <= 1) small_residuals++;
                error_sum += (double)(diff * diff);
            }
            
            /* Fitness: ZRLE выигрывает от нулевых остатков.
             * zero_residuals × 300 — огромный бонус за точные совпадения
             * small_residuals × 50 — бонус за почти точные
             * -MSE — штраф за большие ошибки */
            population[p].fitness = (double)zero_residuals * 300.0 
                                    + (double)small_residuals * 50.0
                                    - error_sum / (double)num_samples;
        }
        
        /* Сортировка по fitness (убывание) */
        for (int i = 0; i < KF_POPULATION - 1; i++) {
            for (int j = i + 1; j < KF_POPULATION; j++) {
                if (population[j].fitness > population[i].fitness) {
                    KolibriCompactFormula tmp = population[i];
                    population[i] = population[j];
                    population[j] = tmp;
                }
            }
        }
        
        /* Размножение: элита (top-5) выживает, остальные — мутанты */
        int elite = 5;
        for (int p = elite; p < KF_POPULATION; p++) {
            population[p] = population[p % elite]; /* Копия родителя */
            /* Мутация: 2-6 цифр генома */
            int mutations = 2 + (int)(k_rng_next(&evo_rng) % 5);
            for (int m = 0; m < mutations; m++) {
                int idx = (int)(k_rng_next(&evo_rng) % KF_GENE_LEN);
                population[p].digits[idx] = (uint8_t)(k_rng_next(&evo_rng) % 10);
            }
        }
    }
    
    *best = population[0]; /* Лучшая формула */
}

/* --- Секвенциальное кодирование: формула предсказывает byte[i] из byte[i-1] ---
 * Данные = Формула(prev_byte) + Остатки
 * Если формула точна → остатки = 0 → ZRLE сжимает до минимума */
static void kf_formula_encode_block(const KolibriCompactFormula *formula,
                                     const uint8_t *data, size_t len,
                                     uint8_t *residuals) {
    int prev = 0;
    for (size_t i = 0; i < len; i++) {
        int predicted = kf_compact_predict(formula, prev);
        residuals[i] = (uint8_t)((int)data[i] - predicted);
        prev = (int)data[i]; /* Следующий контекст = текущий ОРИГИНАЛЬНЫЙ байт */
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

static size_t zrle_encode(const uint8_t *input, size_t len, uint8_t *output, size_t max_out) {
    size_t in_pos = 0, out_pos = 0;
    while (in_pos < len) {
        if (input[in_pos] == 0) {
            /* Подсчитываем серию нулей (до 65535) */
            size_t run = 0;
            while (in_pos + run < len && input[in_pos + run] == 0 && run < 65535) {
                run++;
            }
            if (run <= 255) {
                /* Короткая серия: 2 байта [0x00][run-1], run-1 ∈ [0..253] 
                 * 0xFF зарезервирован как маркер длинной серии! */
                if (run > 254) run = 254;  /* Отдаём остаток следующей итерации */
                if (out_pos + 2 > max_out) return 0;
                output[out_pos++] = 0;
                output[out_pos++] = (uint8_t)(run - 1);
            } else {
                /* Длинная серия: 4 байта — [0x00][0xFF][hi][lo] */
                if (out_pos + 4 > max_out) return 0;
                output[out_pos++] = 0;
                output[out_pos++] = 0xFF;  /* маркер длинной серии */
                output[out_pos++] = (uint8_t)((run >> 8) & 0xFF);
                output[out_pos++] = (uint8_t)(run & 0xFF);
            }
            in_pos += run;
        } else {
            if (out_pos >= max_out) return 0;
            output[out_pos++] = input[in_pos++];
        }
    }
    return out_pos;
}

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

static size_t compress_mathematical(const uint8_t *input, size_t input_size,
                                   uint8_t *output, size_t output_size) {
    /* If input is too small, just store as raw global block */
    if (input_size < 128) {
        if (output_size < input_size + 1) return 0;
        output[0] = 0; /* RAW */
        memcpy(output + 1, input, input_size);
        return input_size + 1;
    }
    
    size_t in_processed = 0;
    size_t out_pos = 0;

    /* Generator Persistence */
    KolibriRng current_rng;
    int last_was_generator = 0;

    while (in_processed < input_size) {
        size_t block_len = MIN(MATH_BLOCK_SIZE, input_size - in_processed);
        const uint8_t *blk = input + in_processed;
        
        /* Check if we have space for method byte + worst case block + BWT/ZRLE/Formula overhead
         * Method 11: 1(method) + 4(BWT) + 32(gene) + 3(zrle_len) = 40
         * Method 12: 1(method) + 32(gene) + 3(zrle_len) = 36 */
        if (out_pos + block_len + 48 > output_size) return 0;

        uint8_t method = 0;
        uint16_t matched_seed = 0;
        int found_gen = 0;
        
        /* Per-block BWT/Context buffers (initialized to NULL) */
        uint8_t *bwt_result = NULL;
        uint32_t bwt_primary = 0;
        uint8_t *ctx_result = NULL;
        uint8_t *combined_result = NULL;
        uint32_t combined_bwt_primary = 0;
        uint8_t *formula_result = NULL;
        KolibriCompactFormula best_formula;
        memset(&best_formula, 0, sizeof(best_formula));

        /* METHOD 5: GENERATOR CONTINUE */
        if (last_was_generator) {
            /* Check if current block continues the sequence */
            KolibriRng test_rng = current_rng; /* Copy state */
            int mismatch = 0;
            for (size_t k = 0; k < block_len; k++) {
                uint8_t byte = (uint8_t)(k_rng_next(&test_rng) % 256);
                if (byte != blk[k]) { mismatch = 1; break; }
            }
            
            if (!mismatch) {
                method = 5;
                found_gen = 1;
                /* Update state for next block */
                current_rng = test_rng; 
            }
        }

        if (!found_gen) {
            /* Analyze best formula for this block (Methods 0-3) */
            uint64_t sum_raw = 0;
            uint64_t sum_d1 = 0;
            uint64_t sum_d2 = 0;
            uint64_t sum_s4 = 0;

            size_t sample_step = (block_len > 1024) ? 8 : 1;
            size_t check_end = block_len;

            for (size_t i = 4; i < check_end; i += sample_step) {
                int8_t val = (int8_t)blk[i];
                sum_raw += abs(val);
                
                sum_d1 += abs((int8_t)(blk[i] - blk[i-1]));
                sum_d2 += abs((int8_t)(blk[i] - 2*blk[i-1] + blk[i-2]));
                sum_s4 += abs((int8_t)(blk[i] - blk[i-4]));
            }

            uint64_t best_sum = sum_raw;
            /* Reset method to 0 (RAW) */
            method = 0; 

            if (sum_d1 * 10 < best_sum * 8) { best_sum = sum_d1; method = 1; }
            if (sum_d2 * 10 < best_sum * 8) { best_sum = sum_d2; method = 2; }
            if (sum_s4 * 10 < best_sum * 8) { best_sum = sum_s4; method = 3; }

            /* ===== METHOD 8: BWT + MTF (Burrows-Wheeler Transform) ===== */
            /* BWT группирует похожие контексты вместе, MTF превращает их в маленькие числа.
               Это КЛЮЧЕВОЙ метод для текстовых данных — то, что делает bzip2 лучше gzip. */
            uint64_t score_bwt = 0;
            
            if (block_len >= 64) {
                bwt_result = (uint8_t *)malloc(block_len);
                if (bwt_result && bwt_forward(blk, block_len, bwt_result, &bwt_primary) == 0) {
                    mtf_encode(bwt_result, block_len);
                    score_bwt = estimate_compressibility(bwt_result, block_len);
                }
            }

            /* ===== METHOD 9: CONTEXT PREDICTION (Kolibri AI) ===== */
            /* Адаптивная таблица предсказаний: запоминает паттерны и хранит только ошибки.
               Это "формула Kolibri" — логическая цепочка запоминания. */
            ctx_result = (uint8_t *)malloc(block_len);
            uint64_t score_ctx = 0;
            if (ctx_result) {
                context_predict_encode(blk, block_len, ctx_result);
                score_ctx = estimate_compressibility(ctx_result, block_len);
            }

            /* ===== METHOD 10: BWT + MTF + CONTEXT (Full Kolibri Pipeline) ===== */
            uint64_t score_combined = 0;
            
            if (block_len >= 64) {
                combined_result = (uint8_t *)malloc(block_len);
                if (combined_result) {
                    /* Шаг 1: BWT + MTF */
                    if (bwt_forward(blk, block_len, combined_result, &combined_bwt_primary) == 0) {
                        mtf_encode(combined_result, block_len);
                        /* Шаг 2: Context prediction на MTF-выходе */
                        uint8_t *temp_ctx = (uint8_t *)malloc(block_len);
                        if (temp_ctx) {
                            context_predict_encode(combined_result, block_len, temp_ctx);
                            score_combined = estimate_compressibility(temp_ctx, block_len);
                            memcpy(combined_result, temp_ctx, block_len);
                            free(temp_ctx);
                        }
                    }
                }
            }

            /* Оценка RAW для сравнения */
            uint64_t score_raw = estimate_compressibility(blk, block_len);
            /* Оценка лучшего Delta-метода */
            uint8_t *delta_result = (uint8_t *)malloc(block_len);
            uint64_t score_delta = 0;
            if (delta_result && method >= 1 && method <= 3) {
                /* Применяем выбранный delta метод */
                if (method == 1) {
                    delta_result[0] = blk[0];
                    for (size_t i = 1; i < block_len; i++)
                        delta_result[i] = blk[i] - blk[i-1];
                } else if (method == 2) {
                    delta_result[0] = blk[0]; delta_result[1] = blk[1];
                    for (size_t i = 2; i < block_len; i++)
                        delta_result[i] = blk[i] - 2*blk[i-1] + blk[i-2];
                } else if (method == 3) {
                    memcpy(delta_result, blk, 4);
                    for (size_t i = 4; i < block_len; i++)
                        delta_result[i] = blk[i] - blk[i-4];
                }
                score_delta = estimate_compressibility(delta_result, block_len);
            }
            
            /* Выбираем ЛУЧШИЙ метод по score (выше = лучше сжатие) */
            uint64_t best_score = score_raw;
            /* method уже установлен из delta-анализа, переоценим */
            
            if (score_delta > best_score && method >= 1 && method <= 3) {
                best_score = score_delta;
                /* method уже установлен */
            }
            
            if (bwt_result && score_bwt > best_score) {
                best_score = score_bwt;
                method = 8;
            }
            
            if (ctx_result && score_ctx > best_score) {
                best_score = score_ctx;
                method = 9;
            }
            
            if (combined_result && score_combined > best_score) {
                best_score = score_combined;
                method = 10;
            }
            
            /* ===== METHOD 11: BWT + MTF + FORMULA PREDICTION ===== */
            /* «Number-Thinking»: Формула эволюционирует, чтобы предсказать
               данные после BWT+MTF. Хранятся только остатки (residuals).
               Если формула хорошо предсказывает → остатки = почти нули → 
               ZRLE сжимает их до минимума! */
            uint64_t score_formula = 0;
            
            if (bwt_result && block_len >= 256) {
                formula_result = (uint8_t *)malloc(block_len);
                if (formula_result) {
                    /* Обучаем формулу на BWT+MTF данных */
                    kf_train_formula(&best_formula, bwt_result, block_len, 
                                     (uint64_t)in_processed);
                    
                    /* Вычисляем остатки: res = data - prediction */
                    kf_formula_encode_block(&best_formula, bwt_result, block_len,
                                            formula_result);
                    
                    score_formula = estimate_compressibility(formula_result, block_len);
                    
                    if (score_formula > best_score) {
                        best_score = score_formula;
                        method = 11;
                    }
                }
            }
            
            /* ===== METHOD 12: PURE FORMULA (без BWT) ===== */
            /* «Вся информация в формуле»: формула предсказывает байты
               НАПРЯМУЮ из сырых данных. Для файлов где BWT мешает,
               или где байт-к-байту зависимость сильнее чем BWT-контекст.
               Overhead: 32 байта генома + 3 байта ZRLE len = 35 байт
               (нет 4-байтного BWT primary → экономия 4 байта vs Method 11) */
            if (block_len >= 128) {
                uint8_t *raw_formula_result = (uint8_t *)malloc(block_len);
                if (raw_formula_result) {
                    KolibriCompactFormula raw_formula;
                    kf_train_formula(&raw_formula, blk, block_len,
                                     (uint64_t)in_processed ^ 0xDEADUL);
                    kf_formula_encode_block(&raw_formula, blk, block_len,
                                            raw_formula_result);
                    
                    uint64_t score_raw_formula = estimate_compressibility(
                                                    raw_formula_result, block_len);
                    
                    if (score_raw_formula > best_score) {
                        best_score = score_raw_formula;
                        method = 12;
                        /* Сохраняем в formula_result для вывода */
                        if (!formula_result) {
                            formula_result = raw_formula_result;
                            raw_formula_result = NULL;
                        } else {
                            memcpy(formula_result, raw_formula_result, block_len);
                        }
                        best_formula = raw_formula;
                    }
                    free(raw_formula_result);
                }
            }
            
            free(delta_result);

            /* METHOD 4: GENERATOR INIT (Exact Match) — только быстрая проверка нескольких seed'ов */
            if (block_len >= 16 && block_len <= 16384) {
                for (uint32_t s = 0; s < 65536; s++) {
                    KolibriRng rng;
                    k_rng_seed(&rng, s);
                    
                    int mismatch = 0;
                    for (int k=0; k<8; k++) {
                        uint8_t byte = (uint8_t)(k_rng_next(&rng) % 256);
                        if (byte != blk[k]) { mismatch = 1; break; }
                    }
                    
                    if (!mismatch) {
                        k_rng_seed(&rng, s);
                        for (size_t k=0; k<block_len; k++) {
                            uint8_t byte = (uint8_t)(k_rng_next(&rng) % 256);
                            if (byte != blk[k]) { mismatch = 1; break; }
                        }
                        if (!mismatch) {
                            method = 4;
                            matched_seed = (uint16_t)s;
                            current_rng = rng;
                            found_gen = 1;
                            break;
                        }
                    }
                }
            }
        }
        
        /* Update generator tracking */
        if (method == 4 || method == 5 || method == 6) {
            last_was_generator = 1;
        } else {
            last_was_generator = 0;
        }

        output[out_pos++] = method;

        if (method == 0) {
            memcpy(output + out_pos, blk, block_len);
            out_pos += block_len;
        } else if (method == 1) {
            output[out_pos++] = blk[0];
            for (size_t i = 1; i < block_len; i++) {
                output[out_pos++] = blk[i] - blk[i-1];
            }
        } else if (method == 2) {
            output[out_pos++] = blk[0];
            output[out_pos++] = blk[1];
            for (size_t i = 2; i < block_len; i++) {
                output[out_pos++] = blk[i] - 2*blk[i-1] + blk[i-2];
            }
        } else if (method == 3) {
            memcpy(output + out_pos, blk, 4);
            out_pos += 4;
            for (size_t i = 4; i < block_len; i++) {
                output[out_pos++] = blk[i] - blk[i-4];
            }
        } else if (method == 4) {
            /* Store Seed (Little Endian) */
            output[out_pos++] = (uint8_t)(matched_seed & 0xFF);
            output[out_pos++] = (uint8_t)(matched_seed >> 8);
        } else if (method == 5) {
            /* No data needed! Just the method byte (which is already written) */
        } else if (method == 6) {
            /* Method 6: Seed + Delta Stream */
            output[out_pos++] = (uint8_t)(matched_seed & 0xFF);
            output[out_pos++] = (uint8_t)(matched_seed >> 8);
            
            /* Re-run generator to produce deltas */
            KolibriRng rng;
            k_rng_seed(&rng, matched_seed);
            for (size_t i = 0; i < block_len; i++) {
                 uint8_t gen = (uint8_t)(k_rng_next(&rng) % 256);
                 /* Store delta: Real - Gen */
                 output[out_pos++] = blk[i] - gen;
            }
            /* Note: current_rng is already set to end state of this block inside detection */
             KolibriRng tmp; k_rng_seed(&tmp, matched_seed);
             for(size_t k=0; k<block_len; k++) k_rng_next(&tmp);
             current_rng = tmp;
        } else if (method == 8) {
            /* Method 8: BWT + MTF + ZRLE */
            output[out_pos++] = (uint8_t)(bwt_primary & 0xFF);
            output[out_pos++] = (uint8_t)((bwt_primary >> 8) & 0xFF);
            output[out_pos++] = (uint8_t)((bwt_primary >> 16) & 0xFF);
            output[out_pos++] = (uint8_t)((bwt_primary >> 24) & 0xFF);
            
            /* ZRLE кодирование — сжимает серии нулей после MTF */
            uint8_t *zrle_buf = (uint8_t *)malloc(block_len * 2);
            if (zrle_buf) {
                size_t zrle_len = zrle_encode(bwt_result, block_len, zrle_buf, block_len * 2);
                if (zrle_len > 0 && zrle_len < block_len) {
                    /* ZRLE помогло — сохраняем сжатые данные */
                    output[out_pos++] = (uint8_t)(zrle_len & 0xFF);
                    output[out_pos++] = (uint8_t)((zrle_len >> 8) & 0xFF);
                    output[out_pos++] = (uint8_t)((zrle_len >> 16) & 0xFF);
                    memcpy(output + out_pos, zrle_buf, zrle_len);
                    out_pos += zrle_len;
                } else {
                    /* ZRLE не помогло — сохраняем без него (размер = 0 = маркер raw) */
                    output[out_pos++] = 0;
                    output[out_pos++] = 0;
                    output[out_pos++] = 0;
                    memcpy(output + out_pos, bwt_result, block_len);
                    out_pos += block_len;
                }
                free(zrle_buf);
            } else {
                output[out_pos++] = 0; output[out_pos++] = 0; output[out_pos++] = 0;
                memcpy(output + out_pos, bwt_result, block_len);
                out_pos += block_len;
            }
        } else if (method == 9) {
            /* Method 9: Context Prediction + ZRLE */
            uint8_t *zrle_buf = (uint8_t *)malloc(block_len * 2);
            if (zrle_buf) {
                size_t zrle_len = zrle_encode(ctx_result, block_len, zrle_buf, block_len * 2);
                if (zrle_len > 0 && zrle_len < block_len) {
                    output[out_pos++] = (uint8_t)(zrle_len & 0xFF);
                    output[out_pos++] = (uint8_t)((zrle_len >> 8) & 0xFF);
                    output[out_pos++] = (uint8_t)((zrle_len >> 16) & 0xFF);
                    memcpy(output + out_pos, zrle_buf, zrle_len);
                    out_pos += zrle_len;
                } else {
                    output[out_pos++] = 0; output[out_pos++] = 0; output[out_pos++] = 0;
                    memcpy(output + out_pos, ctx_result, block_len);
                    out_pos += block_len;
                }
                free(zrle_buf);
            } else {
                output[out_pos++] = 0; output[out_pos++] = 0; output[out_pos++] = 0;
                memcpy(output + out_pos, ctx_result, block_len);
                out_pos += block_len;
            }
        } else if (method == 10) {
            /* Method 10: BWT + MTF + Context + ZRLE */
            output[out_pos++] = (uint8_t)(combined_bwt_primary & 0xFF);
            output[out_pos++] = (uint8_t)((combined_bwt_primary >> 8) & 0xFF);
            output[out_pos++] = (uint8_t)((combined_bwt_primary >> 16) & 0xFF);
            output[out_pos++] = (uint8_t)((combined_bwt_primary >> 24) & 0xFF);
            
            uint8_t *zrle_buf = (uint8_t *)malloc(block_len * 2);
            if (zrle_buf) {
                size_t zrle_len = zrle_encode(combined_result, block_len, zrle_buf, block_len * 2);
                if (zrle_len > 0 && zrle_len < block_len) {
                    output[out_pos++] = (uint8_t)(zrle_len & 0xFF);
                    output[out_pos++] = (uint8_t)((zrle_len >> 8) & 0xFF);
                    output[out_pos++] = (uint8_t)((zrle_len >> 16) & 0xFF);
                    memcpy(output + out_pos, zrle_buf, zrle_len);
                    out_pos += zrle_len;
                } else {
                    output[out_pos++] = 0; output[out_pos++] = 0; output[out_pos++] = 0;
                    memcpy(output + out_pos, combined_result, block_len);
                    out_pos += block_len;
                }
                free(zrle_buf);
            } else {
                output[out_pos++] = 0; output[out_pos++] = 0; output[out_pos++] = 0;
                memcpy(output + out_pos, combined_result, block_len);
                out_pos += block_len;
            }
        } else if (method == 11) {
            /* Method 11: BWT + MTF + FORMULA PREDICTION + ZRLE
             * Формат: [BWT_primary:4] [gene_packed:32] [zrle_len:3] [zrle_data...]
             * «Number-Thinking»: данные = формула + минимальная коррекция */
            output[out_pos++] = (uint8_t)(bwt_primary & 0xFF);
            output[out_pos++] = (uint8_t)((bwt_primary >> 8) & 0xFF);
            output[out_pos++] = (uint8_t)((bwt_primary >> 16) & 0xFF);
            output[out_pos++] = (uint8_t)((bwt_primary >> 24) & 0xFF);
            
            /* Упаковываем геном формулы: 64 цифры → 32 байта */
            kf_pack_gene(best_formula.digits, output + out_pos);
            out_pos += KF_GENE_PACKED;
            
            /* ZRLE на остатках (residuals) — предсказанные байты = 0, сжимаются отлично */
            uint8_t *zrle_buf = (uint8_t *)malloc(block_len * 2);
            if (zrle_buf) {
                size_t zrle_len = zrle_encode(formula_result, block_len, zrle_buf, block_len * 2);
                if (zrle_len > 0 && zrle_len < block_len) {
                    output[out_pos++] = (uint8_t)(zrle_len & 0xFF);
                    output[out_pos++] = (uint8_t)((zrle_len >> 8) & 0xFF);
                    output[out_pos++] = (uint8_t)((zrle_len >> 16) & 0xFF);
                    memcpy(output + out_pos, zrle_buf, zrle_len);
                    out_pos += zrle_len;
                } else {
                    output[out_pos++] = 0; output[out_pos++] = 0; output[out_pos++] = 0;
                    memcpy(output + out_pos, formula_result, block_len);
                    out_pos += block_len;
                }
                free(zrle_buf);
            } else {
                output[out_pos++] = 0; output[out_pos++] = 0; output[out_pos++] = 0;
                memcpy(output + out_pos, formula_result, block_len);
                out_pos += block_len;
            }
        } else if (method == 12) {
            /* Method 12: PURE FORMULA + ZRLE (без BWT!)
             * Формат: [gene_packed:32] [zrle_len:3] [zrle_data...]
             * «Вся информация в формуле»: нет BWT overhead (экономия 4 байта) */
            kf_pack_gene(best_formula.digits, output + out_pos);
            out_pos += KF_GENE_PACKED;
            
            uint8_t *zrle_buf = (uint8_t *)malloc(block_len * 2);
            if (zrle_buf) {
                size_t zrle_len = zrle_encode(formula_result, block_len, zrle_buf, block_len * 2);
                if (zrle_len > 0 && zrle_len < block_len) {
                    output[out_pos++] = (uint8_t)(zrle_len & 0xFF);
                    output[out_pos++] = (uint8_t)((zrle_len >> 8) & 0xFF);
                    output[out_pos++] = (uint8_t)((zrle_len >> 16) & 0xFF);
                    memcpy(output + out_pos, zrle_buf, zrle_len);
                    out_pos += zrle_len;
                } else {
                    output[out_pos++] = 0; output[out_pos++] = 0; output[out_pos++] = 0;
                    memcpy(output + out_pos, formula_result, block_len);
                    out_pos += block_len;
                }
                free(zrle_buf);
            } else {
                output[out_pos++] = 0; output[out_pos++] = 0; output[out_pos++] = 0;
                memcpy(output + out_pos, formula_result, block_len);
                out_pos += block_len;
            }
        }

        /* Cleanup per-block allocations */
        free(bwt_result);
        free(ctx_result);
        free(combined_result);
        free(formula_result);

        in_processed += block_len;
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

#define LZ_WBITS   16           /* 64 KB окно */
#define LZ_WSIZE   (1 << LZ_WBITS)  /* 65536 */
#define LZ_WMASK   (LZ_WSIZE - 1)
#define LZ_HTBITS  18           /* 256K хеш-таблица */
#define LZ_HTSIZE  (1 << LZ_HTBITS)
#define LZ_HTMASK  (LZ_HTSIZE - 1)
#define LZ_MIN_MATCH  3
#define LZ_MAX_MATCH  257       /* long: len-4 max 253, short: len=3 only */
#define LZ_MAX_CHAIN  512       /* макс. глубина цепочки (v53: увеличена для лучших совпадений) */
#define LZ_ESCAPE     0xFF      /* escape-байт для совпадений */

static inline uint32_t lz_hash4(const uint8_t *p) {
    uint32_t h = ((uint32_t)p[0]) | ((uint32_t)p[1] << 8)
               | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
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

    /* -1 = нет ссылки */
    for (size_t j = 0; j < (size_t)LZ_HTSIZE; j++) head[j] = -1;
    for (size_t j = 0; j < (size_t)LZ_WSIZE; j++) prev[j] = -1;

    size_t op = 0;
    size_t ip = 0;
    size_t limit = input_size - 3; /* нужно 4 байта для хеша */

    while (ip < input_size) {
        int best_len = 0;
        int best_dist = 0;

        if (ip < limit) {
            uint32_t h = lz_hash4(input + ip);
            int32_t cur = head[h];
            int chain = 0;

            /* Поиск по хеш-цепочке */
            while (cur >= 0 && chain < LZ_MAX_CHAIN) {
                size_t candidate = (size_t)cur;
                if (ip > candidate && (ip - candidate) <= LZ_WSIZE) {
                    const uint8_t *a = input + ip;
                    const uint8_t *b = input + candidate;
                    int len = 0;
                    int max_possible = (int)MIN((size_t)LZ_MAX_MATCH, input_size - ip);
                    while (len < max_possible && a[len] == b[len]) len++;

                    if (len >= LZ_MIN_MATCH && len > best_len) {
                        best_len = len;
                        best_dist = (int)(ip - candidate);
                        if (len >= LZ_MAX_MATCH) break;
                    }
                }
                cur = prev[candidate & LZ_WMASK]; if (cur >= (int32_t)ip) break;
                chain++;
            }

            /* Обновляем хеш-цепочку */
            prev[ip & LZ_WMASK] = head[h];
            head[h] = (int32_t)ip;
        }

        if (best_len >= LZ_MIN_MATCH) {
            /* Lazy matching: проверяем ip+1 — может там лучше */
            if (best_len < LZ_MAX_MATCH && (ip + 1) < limit) {
                uint32_t h2_ = lz_hash4(input + ip + 1);
                int32_t cur2 = head[h2_];
                int lazy_len = 0, lazy_dist = 0, chain2 = 0;
                while (cur2 >= 0 && chain2 < LZ_MAX_CHAIN) {
                    size_t cand2 = (size_t)cur2;
                    if ((ip + 1) > cand2 && ((ip + 1) - cand2) <= LZ_WSIZE) {
                        const uint8_t *a2 = input + ip + 1;
                        const uint8_t *b2 = input + cand2;
                        int len2 = 0;
                        int max2 = (int)MIN((size_t)LZ_MAX_MATCH, input_size - ip - 1);
                        while (len2 < max2 && a2[len2] == b2[len2]) len2++;
                        if (len2 > lazy_len) {
                            lazy_len = len2;
                            lazy_dist = (int)((ip + 1) - cand2);
                        }
                    }
                    cur2 = prev[cand2 & LZ_WMASK];
                    if (cur2 >= (int32_t)(ip + 1)) break;
                    chain2++;
                }
                if (lazy_len > best_len + 1) {
                    /* Литерал текущий байт, потом match от ip+1 */
                    if (input[ip] == LZ_ESCAPE) {
                        if (op + 2 > output_max) goto fail;
                        output[op++] = LZ_ESCAPE;
                        output[op++] = LZ_ESCAPE;
                    } else {
                        if (op + 1 > output_max) goto fail;
                        output[op++] = input[ip];
                    }
                    /* Обновляем хеш для ip */
                    prev[ip & LZ_WMASK] = head[lz_hash4(input + ip)];
                    head[lz_hash4(input + ip)] = (int32_t)ip;
                    ip++;
                    best_len = lazy_len;
                    best_dist = lazy_dist;
                }
            }

            /* Match encoding:
             * Short: 0xFF 0xFE dist_lo    (len=3, dist 1-256, 3 bytes)
             * Long:  0xFF len-4 dist_hi dist_lo (len 4-257, 4 bytes) */
            if (best_len == 3 && best_dist <= 256) {
                /* Short match: 3 bytes */
                if (op + 3 > output_max) goto fail;
                output[op++] = LZ_ESCAPE;
                output[op++] = 0xFE;
                output[op++] = (uint8_t)(best_dist - 1);
            } else {
                /* Long match: 4 bytes, len encoded as len-4 (0..253) */
                if (op + 4 > output_max) goto fail;
                output[op++] = LZ_ESCAPE;
                output[op++] = (uint8_t)(best_len - 4);
                output[op++] = (uint8_t)((best_dist >> 8) & 0xFF);
                output[op++] = (uint8_t)(best_dist & 0xFF);
            }

            /* Обновляем хеш для пропущенных позиций */
            for (int k = 1; k < best_len && (ip + k) < limit; k++) {
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
                output[op++] = LZ_ESCAPE;  /* 0xFF 0xFF = литерал 0xFF */
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

/* Декодирование LZ-lite */
static size_t lz_lite_decode(const uint8_t *input, size_t input_size,
                              uint8_t *output, size_t output_max,
                              size_t original_size)
{
    size_t ip = 0, op = 0;

    while (ip < input_size && op < original_size) {
        uint8_t c = input[ip++];
        if (c == LZ_ESCAPE) {
            if (ip >= input_size) return 0;
            uint8_t next = input[ip++];
            if (next == LZ_ESCAPE) {
                /* Экранированный литерал 0xFF */
                if (op >= output_max) return 0;
                output[op++] = LZ_ESCAPE;
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
 *   O5: 256K записей (512KB)
 *   O6: 256K записей (512KB)
 *   O7: 256K записей (512KB)
 *   SSE: 256×8×33    (135KB)
 *   Итого: ~3.2 MB
 */
#define T0_SIZE  65536u     /* Order-0: 256 byte contexts × 256 bit-tree */
#define T1_SIZE  65536u     /* 64K */
#define T2_SIZE  131072u    /* 128K */
#define T3_SIZE  262144u    /* 256K */
#define T4_SIZE  262144u
#define T5_SIZE  1048576u   /* 1M — v53: больше таблицы = меньше коллизий */
#define T6_SIZE  1048576u
#define T7_SIZE  1048576u
#define SSE_Q    33
#define SSE_SZ   (256u * 8u * SSE_Q)

/* Расширенная модель: контексты + match model + word boundary */
typedef struct {
    uint16_t *t0, *t1, *t2, *t3, *t4, *t5, *t6, *t7;
    uint16_t *sse;
    uint16_t *tstate;    /* LZ state-aware: 4_states × 256 × 256 bit-tree */
    uint16_t *tword;     /* word boundary: 64K (контекст начала слова) */
    uint16_t *tsparse;   /* sparse context: (hist[0]^hist[2]) × 256 bit-tree */
    /* v53: адаптивные веса для 11 предикторов (фиксированная точка ×256) */
    int32_t w[11];
    int32_t wsum;
} KF51M;

#define TSTATE_SIZE  65536u  /* compact: state*256 + byte hashed to 64K */
#define TWORD_SIZE   65536u         /* word boundary context */
#define TSPARSE_SIZE 65536u         /* sparse (gap) context */

static uint16_t *kf51_new(size_t n) {
    uint16_t *p = (uint16_t *)malloc(n * sizeof(uint16_t));
    if (p) for (size_t i = 0; i < n; i++) p[i] = 2048;
    return p;
}
static int kf51_init(KF51M *m) {
    m->t0 = kf51_new(T0_SIZE);  m->t1 = kf51_new(T1_SIZE);
    m->t2 = kf51_new(T2_SIZE);  m->t3 = kf51_new(T3_SIZE);
    m->t4 = kf51_new(T4_SIZE);  m->t5 = kf51_new(T5_SIZE);
    m->t6 = kf51_new(T6_SIZE);  m->t7 = kf51_new(T7_SIZE);
    m->sse = kf51_new(SSE_SZ);
    m->tstate = kf51_new(TSTATE_SIZE);
    m->tword = kf51_new(TWORD_SIZE);
    m->tsparse = kf51_new(TSPARSE_SIZE);
    /* v53: адаптивные веса (fixed-point ×256) — стартуем с оригинальных */
    m->w[0] =  1*256; m->w[1] =  1*256; m->w[2] =  2*256; m->w[3] =  4*256;
    m->w[4] =  8*256; m->w[5] = 12*256; m->w[6] = 18*256; m->w[7] = 28*256;
    m->w[8] =  8*256; m->w[9] =  3*256; m->w[10] = 3*256;
    m->wsum = 88*256;
    return (m->t0 && m->t1 && m->t2 && m->t3 &&
            m->t4 && m->t5 && m->t6 && m->t7 && m->sse &&
            m->tstate && m->tword && m->tsparse);
}
static void kf51_destroy(KF51M *m) {
    free(m->t0); free(m->t1); free(m->t2); free(m->t3);
    free(m->t4); free(m->t5); free(m->t6); free(m->t7);
    free(m->sse); free(m->tstate); free(m->tword); free(m->tsparse);
}

/* FNV хеш */
static inline uint32_t kfh(uint32_t h, uint32_t b) {
    return (h ^ b) * 0x01000193u;
}

/* Обновление вероятности с разной скоростью */
static inline void kf_upd(uint16_t *p, int bit, int rate) {
    if (bit) *p += ((4096 - *p) >> rate);
    else     *p -= (*p >> rate);
}

/* Общая inline-логика для одного байта (encoder/decoder)
 *
 * Token-aware state tracking для LZ потока:
 *   state 0: обычный литерал / начало токена
 *   state 1: после 0xFF — ждём 0xFF (литерал) или len (match)
 *   state 2: прочитали len — ждём dist_hi
 *   state 3: прочитали dist_hi — ждём dist_lo
 *
 * Sparse context: hist[0]^hist[2] (пропуск 1 байта для структурных данных)
 */
#define KF51_PROCESS_BYTE(ENCODE)                                           \
do {                                                                        \
    /* Хеши контекстов */                                                   \
    uint32_t h1 = 0xA1B2C3D4u ^ hist[0];                                   \
    uint32_t h2 = kfh(h1, hist[1]);                                         \
    uint32_t h3 = kfh(h2, hist[2]);                                         \
    uint32_t h4 = kfh(h3, hist[3]);                                         \
    uint32_t h5 = kfh(h4, hist[4]);                                         \
    uint32_t h6 = kfh(h5, hist[5]);                                         \
    uint32_t h7 = kfh(h6, hist[6]);                                         \
                                                                            \
    /* Word boundary: пробел/nl/tab → начало нового слова */                \
    int is_wb = (hist[0]==' '||hist[0]=='\n'||hist[0]=='\t'              \
                 ||hist[0]=='\r'||hist[0]==0);                              \
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
                                                                            \
        /* Вероятности */                                                    \
        uint32_t p0 = mm->t0[i0], p1 = mm->t1[i1];                         \
        uint32_t p2 = mm->t2[i2], p3 = mm->t3[i3];                         \
        uint32_t p4 = mm->t4[i4], p5 = mm->t5[i5];                         \
        uint32_t p6 = mm->t6[i6], p7 = mm->t7[i7];                         \
        uint32_t pst = mm->tstate[ist];                                     \
        uint32_t pw = mm->tword[iw];                                        \
        uint32_t psp = mm->tsparse[isp];                                    \
                                                                            \
        /* Смешивание: v53 адаптивные веса (fixed-point ×256)              \
         * Веса обучаются по ошибке — кто точнее, получает больший вес */   \
        int32_t preds[11] = {(int32_t)p0,(int32_t)p1,(int32_t)p2,          \
                             (int32_t)p3,(int32_t)p4,(int32_t)p5,           \
                             (int32_t)p6,(int32_t)p7,(int32_t)pst,          \
                             (int32_t)pw,(int32_t)psp};                     \
        int64_t wmx = 0;                                                     \
        for (int wi = 0; wi < 11; wi++) wmx += (int64_t)mm->w[wi] * preds[wi]; \
        int32_t wdiv = mm->wsum;                                             \
        if (wdiv < 256) wdiv = 256;                                          \
        uint32_t mx = (uint32_t)(wmx / wdiv);                                \
        if (mx > 4095) mx = 4095;          \
                                                                            \
        /* SSE */                                                            \
        int q = (int)(mx >> 7);                                             \
        if (q > 32) q = 32;                                                 \
        int si = ((int)hist[0] * 8 + (7 - b)) * SSE_Q + q;                 \
        uint32_t sp = mm->sse[si];                                          \
        uint32_t fp = (mx * 3 + sp) >> 2;                                   \
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
        kf_upd(&mm->tstate[ist], bit, 3);                                   \
        kf_upd(&mm->tword[iw], bit, 4);                                     \
        kf_upd(&mm->tsparse[isp], bit, 4);                                  \
        kf_upd(&mm->sse[si], bit, 4);                                       \
                                                                            \
        /* v53: обучение весов смешивания (online gradient descent)          \
         * err = bit*4096 - mx; увеличиваем вес точных предикторов */       \
        {                                                                    \
            int32_t target = bit ? 4096 : 0;                                 \
            int32_t err = target - (int32_t)mx;                              \
            for (int wi = 0; wi < 11; wi++) {                                \
                int32_t delta = (err * preds[wi]) >> 18;                     \
                mm->w[wi] += delta;                                          \
                if (mm->w[wi] < 1) mm->w[wi] = 1;                           \
            }                                                                \
            mm->wsum = 0;                                                    \
            for (int wi = 0; wi < 11; wi++) mm->wsum += mm->w[wi];          \
        }                                                                    \
                                                                            \
        cx = (cx << 1) | bit;                                                \
    }                                                                        \
    /* Обновляем LZ state machine */                                         \
    if (lz_state == 0) {                                                     \
        if (byte == 0xFF) lz_state = 1;                                      \
    } else if (lz_state == 1) {                                              \
        if (byte == 0xFF) lz_state = 0; /* escaped literal */                \
        else lz_state = 2; /* len byte read, expect dist_hi */               \
    } else if (lz_state == 2) {                                              \
        lz_state = 3; /* dist_hi read, expect dist_lo */                     \
    } else {                                                                 \
        lz_state = 0; /* dist_lo read, back to normal */                     \
    }                                                                        \
    for (int k = 7; k > 0; k--) hist[k] = hist[k-1];                        \
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

    if (file_type == KOLIBRI_FILE_TEXT) {
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

    /* Этап 1: LZ-lite предпроход */
    size_t lz_size = lz_lite_encode(formula_input, formula_input_size,
                                    lz_buf, formula_input_size + 1024);
    if (lz_size > 0 && lz_size < formula_input_size) {
        formula_input = lz_buf;
        formula_input_size = lz_size;
        lz_used = 1;
    }

    /* Этап 2: Формульное сжатие (работает с LZ-выходом или оригиналом) */
    size_t formula_size = compress_formula_v51(formula_input, formula_input_size,
                                               temp, input_size + 1024);
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
    } else if (lz_used && lz_size < formula_input_size) {
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

        size_t formula_size = decompress_formula_v51(
            formula_data, formula_data_size, temp2, original_size * 2, formula_target);
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
