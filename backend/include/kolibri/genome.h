#ifndef KOLIBRI_GENOME_H
#define KOLIBRI_GENOME_H

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Константы блока --- */
#define KOLIBRI_HASH_SIZE 32
#define KOLIBRI_EVENT_TYPE_SIZE 32
#define KOLIBRI_PAYLOAD_SIZE 512
#define KOLIBRI_HMAC_KEY_SIZE 64
#define KOLIBRI_BLOCK_SIZE                                                     \
  (sizeof(uint64_t) + sizeof(uint64_t) + KOLIBRI_HASH_SIZE +                   \
   KOLIBRI_HASH_SIZE + KOLIBRI_EVENT_TYPE_SIZE + KOLIBRI_PAYLOAD_SIZE)

/* --- WAL (Write-Ahead Logging) --- */
#define KOLIBRI_WAL_MAGIC 0x4B4F4C57414C0001ULL  /* "KOLWAL" + version */
#define KOLIBRI_WAL_ENTRY_SIZE (8 + KOLIBRI_BLOCK_SIZE + 4)  /* magic + block + crc */

typedef struct {
  uint64_t index;
  uint64_t timestamp;
  unsigned char prev_hash[KOLIBRI_HASH_SIZE];
  unsigned char hmac[KOLIBRI_HASH_SIZE];
  char event_type[KOLIBRI_EVENT_TYPE_SIZE];
  char payload[KOLIBRI_PAYLOAD_SIZE];
} ReasonBlock;

typedef struct {
  FILE *file;
  unsigned char last_hash[KOLIBRI_HASH_SIZE];
  unsigned char last_block[KOLIBRI_BLOCK_SIZE];
  unsigned char hmac_key[KOLIBRI_HMAC_KEY_SIZE];
  size_t hmac_key_len;
  char path[260];
  uint64_t next_index;
  int has_last_block;
  
  /* WAL support */
  FILE *wal_file;
  char wal_path[264];
  int wal_enabled;
  uint64_t wal_entries;
} KolibriGenome;

/* --- Статистика генома --- */
typedef struct {
  uint64_t total_blocks;
  uint64_t file_size_bytes;
  uint64_t first_timestamp;
  uint64_t last_timestamp;
  int integrity_valid;
} KolibriGenomeStats;

/* --- Основные операции --- */
int kg_open(KolibriGenome *ctx, const char *path, const unsigned char *key,
            size_t key_len);
void kg_close(KolibriGenome *ctx);
int kg_append(KolibriGenome *ctx, const char *event_type, const char *payload,
              ReasonBlock *out_block);
int kg_verify_file(const char *path, const unsigned char *key,
                   size_t key_len);
int kg_encode_payload(const char *utf8, char *out, size_t out_len);

/* --- WAL операции --- */

/** Включает WAL для контекста генома */
int kg_wal_enable(KolibriGenome *ctx);

/** Отключает WAL и очищает WAL-файл */
int kg_wal_disable(KolibriGenome *ctx);

/** Восстанавливает геном из WAL после сбоя */
int kg_wal_recover(KolibriGenome *ctx);

/** Принудительный checkpoint - переносит все WAL записи в основной файл */
int kg_wal_checkpoint(KolibriGenome *ctx);

/* --- Потоковые операции --- */

/** Потоковая запись блока с буферизацией в WAL
 *  Возвращает 0 при успехе, блок записывается в WAL немедленно
 *  и переносится в основной файл при checkpoint */
int kg_stream_append(KolibriGenome *ctx, const char *event_type, 
                     const char *payload, ReasonBlock *out_block);

/** Получает статистику генома */
int kg_get_stats(KolibriGenome *ctx, KolibriGenomeStats *stats);

/** Читает блок по индексу */
int kg_read_block(KolibriGenome *ctx, uint64_t index, ReasonBlock *out_block);

/** Итератор по всем блокам */
typedef int (*kg_block_callback)(const ReasonBlock *block, void *user_data);
int kg_iterate_blocks(KolibriGenome *ctx, kg_block_callback callback, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_GENOME_H */
