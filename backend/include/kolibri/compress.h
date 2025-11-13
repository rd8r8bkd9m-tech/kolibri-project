/*
 * Kolibri OS Archiver - Advanced Compression System
 * Implements multi-layer compression with mathematical analysis and formula-based encoding
 */

#ifndef KOLIBRI_COMPRESS_H
#define KOLIBRI_COMPRESS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Compression methods flags */
#define KOLIBRI_COMPRESS_LZ77    0x01
#define KOLIBRI_COMPRESS_RLE     0x02
#define KOLIBRI_COMPRESS_HUFFMAN 0x04
#define KOLIBRI_COMPRESS_FORMULA 0x08
#define KOLIBRI_COMPRESS_MATH    0x10
#define KOLIBRI_COMPRESS_ALL     0x1F

/* File type detection */
typedef enum {
    KOLIBRI_FILE_BINARY,
    KOLIBRI_FILE_TEXT,
    KOLIBRI_FILE_IMAGE,
    KOLIBRI_FILE_UNKNOWN
} KolibriFileType;

/* Compression statistics */
typedef struct {
    size_t original_size;
    size_t compressed_size;
    double compression_ratio;
    uint32_t checksum;
    KolibriFileType file_type;
    uint32_t methods_used;
    double compression_time_ms;
    double decompression_time_ms;
} KolibriCompressStats;

/* Main compression context */
typedef struct KolibriCompressor KolibriCompressor;

/**
 * Create a new compressor instance
 * @param methods Bitfield of compression methods to use
 * @return New compressor instance or NULL on failure
 */
KolibriCompressor *kolibri_compressor_create(uint32_t methods);

/**
 * Destroy a compressor instance
 */
void kolibri_compressor_destroy(KolibriCompressor *comp);

/**
 * Compress data with automatic method selection
 * @param comp Compressor instance
 * @param input Input data
 * @param input_size Size of input data
 * @param output Output buffer (will be allocated)
 * @param output_size Size of output buffer
 * @param stats Optional statistics output
 * @return 0 on success, negative on error
 */
int kolibri_compress(KolibriCompressor *comp,
                     const uint8_t *input,
                     size_t input_size,
                     uint8_t **output,
                     size_t *output_size,
                     KolibriCompressStats *stats);

/**
 * Decompress data
 * @param input Compressed data
 * @param input_size Size of compressed data
 * @param output Output buffer (will be allocated)
 * @param output_size Size of output buffer
 * @param stats Optional statistics output
 * @return 0 on success, negative on error
 */
int kolibri_decompress(const uint8_t *input,
                       size_t input_size,
                       uint8_t **output,
                       size_t *output_size,
                       KolibriCompressStats *stats);

/**
 * Detect file type from data
 */
KolibriFileType kolibri_detect_file_type(const uint8_t *data, size_t size);

/**
 * Calculate checksum for data integrity
 */
uint32_t kolibri_checksum(const uint8_t *data, size_t size);

/* Archive management */
typedef struct KolibriArchive KolibriArchive;

typedef struct {
    char name[256];
    size_t original_size;
    size_t compressed_size;
    uint32_t checksum;
    uint64_t timestamp;
    KolibriFileType type;
} KolibriArchiveEntry;

/**
 * Create a new archive
 */
KolibriArchive *kolibri_archive_create(const char *filename);

/**
 * Open an existing archive
 */
KolibriArchive *kolibri_archive_open(const char *filename);

/**
 * Add file to archive
 */
int kolibri_archive_add_file(KolibriArchive *archive,
                              const char *filename,
                              const uint8_t *data,
                              size_t size);

/**
 * Extract file from archive
 */
int kolibri_archive_extract_file(KolibriArchive *archive,
                                  const char *filename,
                                  uint8_t **data,
                                  size_t *size);

/**
 * List archive contents
 */
int kolibri_archive_list(KolibriArchive *archive,
                         KolibriArchiveEntry **entries,
                         size_t *count);

/**
 * Close and save archive
 */
void kolibri_archive_close(KolibriArchive *archive);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_COMPRESS_H */
