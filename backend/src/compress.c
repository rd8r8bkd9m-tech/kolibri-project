/*
 * Kolibri OS Archiver - Compression Implementation
 * Multi-layer compression with mathematical analysis
 */

#include "kolibri/compress.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <math.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

/* Magic number for compressed data format */
#define KOLIBRI_COMPRESS_MAGIC 0x4B4C4252 /* "KLBR" */
#define KOLIBRI_COMPRESS_VERSION 40

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

/* Simple LZ77 compression */
#define LZ77_WINDOW_SIZE 4096
#define LZ77_MAX_MATCH 255

static size_t compress_lz77(const uint8_t *input, size_t input_size,
                            uint8_t *output, size_t output_size) {
    if (!input || !output) return 0;

    size_t out_pos = 0;
    size_t in_pos = 0;

    while (in_pos < input_size) {
        uint8_t current = input[in_pos];
        size_t best_match_len = 0;
        size_t best_match_dist = 0;

        /* Search for matches in the sliding window */
        size_t search_start = (in_pos > LZ77_WINDOW_SIZE) ? (in_pos - LZ77_WINDOW_SIZE) : 0;
        
        for (size_t i = search_start; i < in_pos; i++) {
            size_t match_len = 0;
            while (match_len < LZ77_MAX_MATCH && 
                   in_pos + match_len < input_size &&
                   input[i + match_len] == input[in_pos + match_len]) {
                match_len++;
            }

            if (match_len > best_match_len) {
                best_match_len = match_len;
                best_match_dist = in_pos - i;
            }
        }

        if (best_match_len >= 4 && best_match_dist <= 4095) {
            /* Encode as (distance, length) pair - distance is 2 bytes */
            if (out_pos + 4 > output_size) return 0;
            output[out_pos++] = 0xFE; /* LZ77 marker */
            output[out_pos++] = (uint8_t)(best_match_dist >> 8); /* High byte */
            output[out_pos++] = (uint8_t)(best_match_dist & 0xFF); /* Low byte */
            output[out_pos++] = (uint8_t)best_match_len;
            in_pos += best_match_len;
        } else {
            /* Literal byte, escape 0xFE */
            if (current == 0xFE) {
                if (out_pos + 2 > output_size) return 0;
                output[out_pos++] = 0xFE;
                output[out_pos++] = 0x00;
            } else {
                if (out_pos >= output_size) return 0;
                output[out_pos++] = current;
            }
            in_pos++;
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
            if (input[in_pos + 1] == 0x00) {
                /* Escaped literal 0xFE */
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
static size_t compress_mathematical(const uint8_t *input, size_t input_size,
                                   uint8_t *output, size_t output_size) {
    /* Analyze data for mathematical patterns (sequences, progressions) */
    /* For now, we'll implement a simple delta encoding for numeric patterns */
    
    if (input_size < 2) {
        memcpy(output, input, input_size);
        return input_size;
    }

    size_t out_pos = 0;
    output[out_pos++] = input[0]; /* Store first byte as-is */

    for (size_t i = 1; i < input_size && out_pos < output_size; i++) {
        /* Store difference from previous byte */
        int16_t delta = (int16_t)input[i] - (int16_t)input[i-1];
        output[out_pos++] = (uint8_t)(delta & 0xFF);
    }

    return out_pos;
}

/* Mathematical pattern decompression */
static size_t decompress_mathematical(const uint8_t *input, size_t input_size,
                                     uint8_t *output, size_t output_size) {
    if (input_size < 1) return 0;

    output[0] = input[0];
    for (size_t i = 1; i < input_size && i < output_size; i++) {
        int16_t delta = (int8_t)input[i];
        output[i] = (uint8_t)((int16_t)output[i-1] + delta);
    }

    return input_size;
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

    /* Allocate output buffer (worst case: header + input + some overhead) */
    size_t max_output = sizeof(KolibriCompressHeader) + input_size * 2 + 1024;
    uint8_t *out_buf = (uint8_t *)malloc(max_output);
    if (!out_buf) return -1;

    /* Reserve space for header */
    size_t header_size = sizeof(KolibriCompressHeader);
    uint8_t *compressed_data = out_buf + header_size;
    size_t compressed_size = input_size;

    /* Allocate temporary buffers */
    uint8_t *temp1 = (uint8_t *)malloc(input_size * 2);
    uint8_t *temp2 = (uint8_t *)malloc(input_size * 2);
    if (!temp1 || !temp2) {
        free(out_buf);
        free(temp1);
        free(temp2);
        return -1;
    }

    const uint8_t *current_data = input;
    uint32_t methods_used = 0;

    /* Apply mathematical compression first for numeric patterns */
    if (comp->methods & KOLIBRI_COMPRESS_MATH) {
        size_t math_size = compress_mathematical(current_data, compressed_size, temp1, input_size * 2);
        if (math_size > 0) {
            memcpy(temp2, temp1, math_size);
            current_data = temp2;
            compressed_size = math_size;
            methods_used |= KOLIBRI_COMPRESS_MATH;
        }
    }

    /* Apply LZ77 compression */
    if (comp->methods & KOLIBRI_COMPRESS_LZ77) {
        size_t lz77_size = compress_lz77(current_data, compressed_size, temp1, input_size * 2);
        if (lz77_size > 0) {
            memcpy(temp2, temp1, lz77_size);
            current_data = temp2;
            compressed_size = lz77_size;
            methods_used |= KOLIBRI_COMPRESS_LZ77;
        }
    }

    /* Apply RLE compression */
    if (comp->methods & KOLIBRI_COMPRESS_RLE) {
        size_t rle_size = compress_rle(current_data, compressed_size, temp1, input_size * 2);
        if (rle_size > 0) {
            memcpy(temp2, temp1, rle_size);
            current_data = temp2;
            compressed_size = rle_size;
            methods_used |= KOLIBRI_COMPRESS_RLE;
        }
    }

    /* Copy final compressed data */
    memcpy(compressed_data, current_data, compressed_size);

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

    free(temp1);
    free(temp2);

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

    /* Decompress in reverse order of compression */
    
    /* RLE decompression */
    if (header->methods & KOLIBRI_COMPRESS_RLE) {
        size_t rle_size = decompress_rle(current_data, current_size, temp2, original_size * 2);
        if (rle_size == 0) {
            free(out_buf);
            free(temp1);
            free(temp2);
            return -1;
        }
        memcpy(temp1, temp2, rle_size);
        current_size = rle_size;
        current_data = temp1;
    }

    /* LZ77 decompression */
    if (header->methods & KOLIBRI_COMPRESS_LZ77) {
        size_t lz77_size = decompress_lz77(current_data, current_size, temp2, original_size * 2);
        if (lz77_size == 0) {
            free(out_buf);
            free(temp1);
            free(temp2);
            return -1;
        }
        memcpy(temp1, temp2, lz77_size);
        current_size = lz77_size;
        current_data = temp1;
    }

    /* Mathematical decompression */
    if (header->methods & KOLIBRI_COMPRESS_MATH) {
        size_t math_size = decompress_mathematical(current_data, current_size, temp2, original_size * 2);
        if (math_size == 0) {
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
        free(out_buf);
        free(temp1);
        free(temp2);
        return -1; /* Checksum mismatch */
    }

    *output = out_buf;
    *output_size = current_size;

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
#define KOLIBRI_ARCHIVE_VERSION 40
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
