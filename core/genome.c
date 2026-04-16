/*
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/genome.h"

#include "kolibri/decimal.h"

#include <openssl/hmac.h>
#include <openssl/sha.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#define KOLIBRI_HMAC_INPUT_SIZE                                                \
  (KOLIBRI_BLOCK_SIZE - KOLIBRI_HASH_SIZE)

static void reset_context(KolibriGenome *ctx) {
  if (!ctx) {
    return;
  }
  ctx->file = NULL;
  memset(ctx->last_hash, 0, sizeof(ctx->last_hash));
  memset(ctx->last_block, 0, sizeof(ctx->last_block));
  memset(ctx->hmac_key, 0, sizeof(ctx->hmac_key));
  ctx->hmac_key_len = 0;
  memset(ctx->path, 0, sizeof(ctx->path));
  ctx->next_index = 0;
  ctx->has_last_block = 0;
  /* WAL fields */
  ctx->wal_file = NULL;
  memset(ctx->wal_path, 0, sizeof(ctx->wal_path));
  ctx->wal_enabled = 0;
  ctx->wal_entries = 0;
}

static void encode_u64_be(uint64_t value, unsigned char *out) {
  for (int i = 0; i < 8; ++i) {
    out[i] = (unsigned char)((value >> (56 - (i * 8))) & 0xFFU);
  }
}

static uint64_t decode_u64_be(const unsigned char *data) {
  uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    value = (value << 8) | (uint64_t)data[i];
  }
  return value;
}

static void serialize_block(const ReasonBlock *block, unsigned char *out) {
  encode_u64_be(block->index, out);
  encode_u64_be(block->timestamp, out + 8);
  memcpy(out + 16, block->prev_hash, KOLIBRI_HASH_SIZE);
  memcpy(out + 16 + KOLIBRI_HASH_SIZE, block->hmac, KOLIBRI_HASH_SIZE);
  memcpy(out + 16 + KOLIBRI_HASH_SIZE * 2, block->event_type,
         KOLIBRI_EVENT_TYPE_SIZE);
  memcpy(out + 16 + KOLIBRI_HASH_SIZE * 2 + KOLIBRI_EVENT_TYPE_SIZE,
         block->payload, KOLIBRI_PAYLOAD_SIZE);
}

static void deserialize_block(const unsigned char *data, ReasonBlock *block) {
  memset(block, 0, sizeof(*block));
  block->index = decode_u64_be(data);
  block->timestamp = decode_u64_be(data + 8);
  memcpy(block->prev_hash, data + 16, KOLIBRI_HASH_SIZE);
  memcpy(block->hmac, data + 16 + KOLIBRI_HASH_SIZE, KOLIBRI_HASH_SIZE);
  memcpy(block->event_type, data + 16 + KOLIBRI_HASH_SIZE * 2,
         KOLIBRI_EVENT_TYPE_SIZE);
  memcpy(block->payload,
         data + 16 + KOLIBRI_HASH_SIZE * 2 + KOLIBRI_EVENT_TYPE_SIZE,
         KOLIBRI_PAYLOAD_SIZE);
}

static void build_hmac_message(const ReasonBlock *block, unsigned char *out) {
  encode_u64_be(block->index, out);
  encode_u64_be(block->timestamp, out + 8);
  memcpy(out + 16, block->prev_hash, KOLIBRI_HASH_SIZE);
  memcpy(out + 16 + KOLIBRI_HASH_SIZE, block->event_type,
         KOLIBRI_EVENT_TYPE_SIZE);
  memcpy(out + 16 + KOLIBRI_HASH_SIZE + KOLIBRI_EVENT_TYPE_SIZE, block->payload,
         KOLIBRI_PAYLOAD_SIZE);
}

/* Вычисляет HMAC для блока */
static int compute_block_hmac(const KolibriGenome *ctx, const ReasonBlock *block,
                              unsigned char *out_hmac) {
  unsigned char message[KOLIBRI_HMAC_INPUT_SIZE];
  build_hmac_message(block, message);

  unsigned int hmac_len = 0;
  unsigned char *result = HMAC(EVP_sha256(), ctx->hmac_key, (int)ctx->hmac_key_len,
                               message, sizeof(message), out_hmac, &hmac_len);
  if (!result || hmac_len != KOLIBRI_HASH_SIZE) {
    return -1;
  }
  return 0;
}

static int payload_is_digits(const char *payload) {
  if (!payload) {
    return 0;
  }
  for (size_t i = 0; i < KOLIBRI_PAYLOAD_SIZE; ++i) {
    char ch = payload[i];
    if (ch == '\0') {
      return 1;
    }
    if (ch < '0' || ch > '9') {
      return 0;
    }
  }
  return 0;
}

static uint64_t current_time_ns(void) {
#if defined(CLOCK_REALTIME)
  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
  }
#endif
  time_t seconds = time(NULL);
  if (seconds < 0) {
    seconds = 0;
  }
  return (uint64_t)seconds * 1000000000ULL;
}

static int parse_and_verify_block(const unsigned char *bytes,
                                  const unsigned char *key, size_t key_len,
                                  uint64_t expected_index,
                                  const unsigned char *expected_prev,
                                  ReasonBlock *out_block,
                                  unsigned char *out_hash) {
  ReasonBlock block;
  deserialize_block(bytes, &block);

  if (!memchr(block.event_type, '\0', KOLIBRI_EVENT_TYPE_SIZE) ||
      !memchr(block.payload, '\0', KOLIBRI_PAYLOAD_SIZE)) {
    return -1;
  }

  if (block.index != expected_index) {
    return -1;
  }

  if (memcmp(block.prev_hash, expected_prev, KOLIBRI_HASH_SIZE) != 0) {
    return -1;
  }

  unsigned char message[KOLIBRI_HMAC_INPUT_SIZE];
  build_hmac_message(&block, message);

  unsigned char computed[KOLIBRI_HASH_SIZE];
  unsigned int hmac_len = 0;
  unsigned char *result =
      HMAC(EVP_sha256(), key, (int)key_len, message, sizeof(message), computed,
           &hmac_len);
  if (!result || hmac_len != KOLIBRI_HASH_SIZE) {
    return -1;
  }

  if (memcmp(computed, block.hmac, KOLIBRI_HASH_SIZE) != 0) {
    return -1;
  }

  if (out_block) {
    *out_block = block;
  }

  if (out_hash) {
    if (!SHA256(bytes, KOLIBRI_BLOCK_SIZE, out_hash)) {
      return -1;
    }
  }

  return 0;
}

int kg_open(KolibriGenome *ctx, const char *path, const unsigned char *key,
            size_t key_len) {
  if (!ctx || !path || !key || key_len == 0 ||
      key_len > sizeof(ctx->hmac_key)) {
    return -1;
  }

  reset_context(ctx);

  FILE *file = fopen(path, "r+b");
  if (!file) {
    file = fopen(path, "w+b");
    if (!file) {
      return -1;
    }
  }

  ctx->file = file;
  strncpy(ctx->path, path, sizeof(ctx->path) - 1);
  memcpy(ctx->hmac_key, key, key_len);
  ctx->hmac_key_len = key_len;

  unsigned char expected_prev[KOLIBRI_HASH_SIZE];
  memset(expected_prev, 0, sizeof(expected_prev));
  uint64_t expected_index = 0;

  unsigned char bytes[KOLIBRI_BLOCK_SIZE];
  size_t read = 0;
  while ((read = fread(bytes, 1, KOLIBRI_BLOCK_SIZE, ctx->file)) ==
         KOLIBRI_BLOCK_SIZE) {
    unsigned char block_hash[KOLIBRI_HASH_SIZE];
    ReasonBlock block;
    if (parse_and_verify_block(bytes, key, key_len, expected_index,
                               expected_prev, &block, block_hash) != 0) {
      kg_close(ctx);
      return -1;
    }

    memcpy(ctx->last_hash, block.hmac, KOLIBRI_HASH_SIZE);
    memcpy(ctx->last_block, bytes, KOLIBRI_BLOCK_SIZE);
    ctx->has_last_block = 1;
    memcpy(expected_prev, block_hash, KOLIBRI_HASH_SIZE);
    expected_index = block.index + 1;
  }

  if (read != 0) {
    kg_close(ctx);
    return -1;
  }

  if (ferror(ctx->file)) {
    kg_close(ctx);
    return -1;
  }

  ctx->next_index = expected_index;

  if (fseek(ctx->file, 0, SEEK_END) != 0) {
    kg_close(ctx);
    return -1;
  }

  return 0;
}

void kg_close(KolibriGenome *ctx) {
  if (!ctx) {
    return;
  }
  if (ctx->file) {
    fclose(ctx->file);
    ctx->file = NULL;
  }
  memset(ctx->last_hash, 0, sizeof(ctx->last_hash));
  memset(ctx->last_block, 0, sizeof(ctx->last_block));
  memset(ctx->hmac_key, 0, sizeof(ctx->hmac_key));
  ctx->hmac_key_len = 0;
  memset(ctx->path, 0, sizeof(ctx->path));
  ctx->next_index = 0;
  ctx->has_last_block = 0;
}

int kg_encode_payload(const char *utf8, char *out, size_t out_len) {
  if (!out || out_len == 0) {
    return -1;
  }
  memset(out, 0, out_len);
  if (!utf8 || utf8[0] == '\0') {
    return 0;
  }
  size_t required = k_encode_text_length(strlen(utf8));
  if (required > out_len) {
    return -1;
  }
  return k_encode_text(utf8, out, out_len);
}

int kg_append(KolibriGenome *ctx, const char *event_type, const char *payload,
              ReasonBlock *out_block) {
  if (!ctx || !ctx->file || !event_type) {
    return -1;
  }

  const char *digits = payload ? payload : "";
  if (!payload_is_digits(digits)) {
    return -1;
  }

  size_t event_len = strnlen(event_type, KOLIBRI_EVENT_TYPE_SIZE);
  if (event_len >= KOLIBRI_EVENT_TYPE_SIZE) {
    return -1;
  }

  ReasonBlock block;
  memset(&block, 0, sizeof(block));

  block.index = ctx->next_index;
  block.timestamp = current_time_ns();

  if (ctx->has_last_block) {
    if (!SHA256(ctx->last_block, KOLIBRI_BLOCK_SIZE, block.prev_hash)) {
      return -1;
    }
  } else {
    memset(block.prev_hash, 0, KOLIBRI_HASH_SIZE);
  }

  memcpy(block.event_type, event_type, event_len);
  size_t payload_len = strnlen(digits, KOLIBRI_PAYLOAD_SIZE);
  if (payload_len >= KOLIBRI_PAYLOAD_SIZE) {
    return -1;
  }
  memcpy(block.payload, digits, payload_len);

  unsigned char message[KOLIBRI_HMAC_INPUT_SIZE];
  build_hmac_message(&block, message);

  unsigned int hmac_len = 0;
  if (!HMAC(EVP_sha256(), ctx->hmac_key, (int)ctx->hmac_key_len, message,
            sizeof(message), block.hmac, &hmac_len) ||
      hmac_len != KOLIBRI_HASH_SIZE) {
    return -1;
  }

  unsigned char bytes[KOLIBRI_BLOCK_SIZE];
  serialize_block(&block, bytes);

  if (fwrite(bytes, 1, KOLIBRI_BLOCK_SIZE, ctx->file) != KOLIBRI_BLOCK_SIZE) {
    return -1;
  }

  if (fflush(ctx->file) != 0) {
    return -1;
  }

  /* #5. Incremental checkpoint: автосохранение каждые 10 блоков */
  if (ctx->next_index > 0 && ctx->next_index % 10 == 0) {
    fsync(fileno(ctx->file));
  }

  memcpy(ctx->last_hash, block.hmac, KOLIBRI_HASH_SIZE);
  memcpy(ctx->last_block, bytes, KOLIBRI_BLOCK_SIZE);
  ctx->has_last_block = 1;
  ctx->next_index = block.index + 1;

  if (out_block) {
    *out_block = block;
  }

  return 0;
}

int kg_verify_file(const char *path, const unsigned char *key,
                   size_t key_len) {
  if (!path || !key || key_len == 0 || key_len > KOLIBRI_HMAC_KEY_SIZE) {
    return -1;
  }

  FILE *file = fopen(path, "rb");
  if (!file) {
    if (errno == ENOENT) {
      return 1;
    }
    return -1;
  }

  unsigned char expected_prev[KOLIBRI_HASH_SIZE];
  memset(expected_prev, 0, sizeof(expected_prev));
  uint64_t expected_index = 0;

  unsigned char bytes[KOLIBRI_BLOCK_SIZE];
  size_t read = 0;
  while ((read = fread(bytes, 1, KOLIBRI_BLOCK_SIZE, file)) ==
         KOLIBRI_BLOCK_SIZE) {
    unsigned char block_hash[KOLIBRI_HASH_SIZE];
    if (parse_and_verify_block(bytes, key, key_len, expected_index,
                               expected_prev, NULL, block_hash) != 0) {
      fclose(file);
      return -1;
    }
    memcpy(expected_prev, block_hash, KOLIBRI_HASH_SIZE);
    expected_index += 1;
  }

  if (read != 0) {
    fclose(file);
    return -1;
  }

  if (ferror(file)) {
    fclose(file);
    return -1;
  }

  fclose(file);
  return 0;
}

/* ============================================================================
 * WAL (Write-Ahead Logging) реализация
 * ============================================================================
 * Обеспечивает атомарность и восстановление после сбоев.
 * WAL-файл: <genome_path>.wal
 * Формат записи: [8B magic][BLOCK_SIZE данные][4B CRC32]
 * ============================================================================ */

static uint32_t crc32_simple(const unsigned char *data, size_t len) {
  /* Простой CRC32 для проверки целостности WAL-записей */
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
    }
  }
  return ~crc;
}

int kg_wal_enable(KolibriGenome *ctx) {
  if (!ctx || !ctx->file) {
    return -1;
  }

  if (ctx->wal_enabled && ctx->wal_file) {
    return 0; /* Уже включён */
  }

  /* Формируем путь к WAL-файлу */
  snprintf(ctx->wal_path, sizeof(ctx->wal_path), "%s.wal", ctx->path);

  /* Открываем WAL для записи и чтения */
  ctx->wal_file = fopen(ctx->wal_path, "a+b");
  if (!ctx->wal_file) {
    return -1;
  }

  /* Подсчитываем существующие записи */
  fseek(ctx->wal_file, 0, SEEK_END);
  long wal_size = ftell(ctx->wal_file);
  ctx->wal_entries = (uint64_t)(wal_size / KOLIBRI_WAL_ENTRY_SIZE);

  ctx->wal_enabled = 1;
  return 0;
}

int kg_wal_disable(KolibriGenome *ctx) {
  if (!ctx) {
    return -1;
  }

  if (ctx->wal_file) {
    fclose(ctx->wal_file);
    ctx->wal_file = NULL;
  }

  /* Удаляем WAL-файл если он пуст или checkpoint выполнен */
  if (ctx->wal_path[0] && ctx->wal_entries == 0) {
    remove(ctx->wal_path);
  }

  ctx->wal_enabled = 0;
  ctx->wal_entries = 0;
  return 0;
}

int kg_wal_checkpoint(KolibriGenome *ctx) {
  if (!ctx || !ctx->wal_file || !ctx->file) {
    return -1;
  }

  if (ctx->wal_entries == 0) {
    return 0; /* Нечего переносить */
  }

  /* В нашей реализации stream_append пишет и в WAL, и в основной файл.
   * Поэтому checkpoint просто очищает WAL - все блоки уже в основном файле.
   * WAL служит для гарантии durability при крэше после записи в основной файл. */

  fflush(ctx->file);

  int entries_cleared = (int)ctx->wal_entries;

  /* Очищаем WAL */
  fclose(ctx->wal_file);
  ctx->wal_file = fopen(ctx->wal_path, "wb");
  if (ctx->wal_file) {
    fclose(ctx->wal_file);
    ctx->wal_file = fopen(ctx->wal_path, "a+b");
  }

  ctx->wal_entries = 0;
  return entries_cleared;
}

int kg_wal_recover(KolibriGenome *ctx) {
  if (!ctx) {
    return -1;
  }

  /* Формируем путь к WAL если не установлен */
  if (ctx->wal_path[0] == '\0') {
    snprintf(ctx->wal_path, sizeof(ctx->wal_path), "%s.wal", ctx->path);
  }

  /* Проверяем существование WAL-файла */
  FILE *wal = fopen(ctx->wal_path, "rb");
  if (!wal) {
    return 0; /* WAL отсутствует - восстановление не требуется */
  }

  /* Получаем размер WAL */
  fseek(wal, 0, SEEK_END);
  long wal_size = ftell(wal);
  if (wal_size == 0) {
    fclose(wal);
    remove(ctx->wal_path);
    return 0;
  }

  fseek(wal, 0, SEEK_SET);

  unsigned char entry_buf[KOLIBRI_WAL_ENTRY_SIZE];
  int recovered = 0;

  while (fread(entry_buf, 1, KOLIBRI_WAL_ENTRY_SIZE, wal) ==
         KOLIBRI_WAL_ENTRY_SIZE) {
    /* Проверяем magic */
    uint64_t magic;
    memcpy(&magic, entry_buf, sizeof(magic));
    if (magic != KOLIBRI_WAL_MAGIC) {
      continue;
    }

    /* Проверяем CRC */
    uint32_t stored_crc;
    memcpy(&stored_crc, entry_buf + 8 + KOLIBRI_BLOCK_SIZE, sizeof(stored_crc));
    uint32_t computed_crc = crc32_simple(entry_buf + 8, KOLIBRI_BLOCK_SIZE);
    if (stored_crc != computed_crc) {
      continue;
    }

    /* Десериализуем блок для проверки индекса */
    ReasonBlock block;
    deserialize_block(entry_buf + 8, &block);

    /* Проверяем, не дубликат ли это */
    if (block.index < ctx->next_index) {
      continue; /* Уже записан в основной файл */
    }

    /* Записываем в основной файл */
    fseek(ctx->file, 0, SEEK_END);
    if (fwrite(entry_buf + 8, 1, KOLIBRI_BLOCK_SIZE, ctx->file) !=
        KOLIBRI_BLOCK_SIZE) {
      fclose(wal);
      return -1;
    }

    /* Обновляем состояние контекста */
    memcpy(ctx->last_hash, block.hmac, KOLIBRI_HASH_SIZE);
    memcpy(ctx->last_block, entry_buf + 8, KOLIBRI_BLOCK_SIZE);
    ctx->next_index = block.index + 1;
    ctx->has_last_block = 1;

    recovered++;
  }

  fclose(wal);
  fflush(ctx->file);

  /* Очищаем WAL после восстановления */
  remove(ctx->wal_path);

  return recovered;
}

int kg_stream_append(KolibriGenome *ctx, const char *event_type,
                     const char *payload, ReasonBlock *out_block) {
  if (!ctx || !ctx->file || !event_type) {
    return -1;
  }

  /* Проверяем, что payload содержит только цифры */
  const char *digits = payload ? payload : "";
  if (!payload_is_digits(digits)) {
    return -1;
  }

  /* Создаём блок */
  ReasonBlock block;
  memset(&block, 0, sizeof(block));

  block.index = ctx->next_index;
  block.timestamp = current_time_ns();

  /* Вычисляем prev_hash из последнего блока */
  if (ctx->has_last_block) {
    if (!SHA256(ctx->last_block, KOLIBRI_BLOCK_SIZE, block.prev_hash)) {
      return -1;
    }
  } else {
    memset(block.prev_hash, 0, KOLIBRI_HASH_SIZE);
  }

  strncpy(block.event_type, event_type, KOLIBRI_EVENT_TYPE_SIZE - 1);
  size_t payload_len = strnlen(digits, KOLIBRI_PAYLOAD_SIZE);
  if (payload_len >= KOLIBRI_PAYLOAD_SIZE) {
    return -1;
  }
  memcpy(block.payload, digits, payload_len);

  /* Вычисляем HMAC */
  if (compute_block_hmac(ctx, &block, block.hmac) != 0) {
    return -1;
  }

  /* Сериализуем блок */
  unsigned char block_data[KOLIBRI_BLOCK_SIZE];
  serialize_block(&block, block_data);

  /* Если WAL включён - записываем сначала в WAL */
  if (ctx->wal_enabled && ctx->wal_file) {
    unsigned char entry_buf[KOLIBRI_WAL_ENTRY_SIZE];

    /* Magic */
    uint64_t magic = KOLIBRI_WAL_MAGIC;
    memcpy(entry_buf, &magic, sizeof(magic));

    /* Block data */
    memcpy(entry_buf + 8, block_data, KOLIBRI_BLOCK_SIZE);

    /* CRC32 */
    uint32_t crc = crc32_simple(block_data, KOLIBRI_BLOCK_SIZE);
    memcpy(entry_buf + 8 + KOLIBRI_BLOCK_SIZE, &crc, sizeof(crc));

    /* Записываем в WAL */
    if (fwrite(entry_buf, 1, KOLIBRI_WAL_ENTRY_SIZE, ctx->wal_file) !=
        KOLIBRI_WAL_ENTRY_SIZE) {
      return -1;
    }
    fflush(ctx->wal_file);
    ctx->wal_entries++;

    /* Автоматический checkpoint каждые 100 записей */
    if (ctx->wal_entries >= 100) {
      kg_wal_checkpoint(ctx);
    }
  }

  /* Записываем в основной файл */
  fseek(ctx->file, 0, SEEK_END);
  if (fwrite(block_data, 1, KOLIBRI_BLOCK_SIZE, ctx->file) !=
      KOLIBRI_BLOCK_SIZE) {
    return -1;
  }
  fflush(ctx->file);

  /* Обновляем состояние */
  memcpy(ctx->last_hash, block.hmac, KOLIBRI_HASH_SIZE);
  memcpy(ctx->last_block, block_data, KOLIBRI_BLOCK_SIZE);
  ctx->next_index++;
  ctx->has_last_block = 1;

  if (out_block) {
    *out_block = block;
  }

  return 0;
}

int kg_get_stats(KolibriGenome *ctx, KolibriGenomeStats *stats) {
  if (!ctx || !ctx->file || !stats) {
    return -1;
  }

  memset(stats, 0, sizeof(*stats));

  /* Получаем размер файла */
  long current_pos = ftell(ctx->file);
  fseek(ctx->file, 0, SEEK_END);
  long file_size = ftell(ctx->file);
  fseek(ctx->file, current_pos, SEEK_SET);

  stats->file_size_bytes = (uint64_t)file_size;
  stats->total_blocks = (uint64_t)(file_size / KOLIBRI_BLOCK_SIZE);

  if (stats->total_blocks == 0) {
    stats->integrity_valid = 1;
    return 0;
  }

  /* Читаем первый блок для first_timestamp */
  unsigned char block_data[KOLIBRI_BLOCK_SIZE];
  ReasonBlock block;

  fseek(ctx->file, 0, SEEK_SET);
  if (fread(block_data, 1, KOLIBRI_BLOCK_SIZE, ctx->file) ==
      KOLIBRI_BLOCK_SIZE) {
    deserialize_block(block_data, &block);
    stats->first_timestamp = block.timestamp;
  }

  /* Читаем последний блок для last_timestamp */
  fseek(ctx->file, -KOLIBRI_BLOCK_SIZE, SEEK_END);
  if (fread(block_data, 1, KOLIBRI_BLOCK_SIZE, ctx->file) ==
      KOLIBRI_BLOCK_SIZE) {
    deserialize_block(block_data, &block);
    stats->last_timestamp = block.timestamp;
  }

  fseek(ctx->file, current_pos, SEEK_SET);

  /* Проверяем целостность через kg_verify_file */
  stats->integrity_valid =
      (kg_verify_file(ctx->path, ctx->hmac_key, ctx->hmac_key_len) == 0) ? 1
                                                                         : 0;

  return 0;
}

int kg_read_block(KolibriGenome *ctx, uint64_t index, ReasonBlock *out_block) {
  if (!ctx || !ctx->file || !out_block) {
    return -1;
  }

  long offset = (long)(index * KOLIBRI_BLOCK_SIZE);
  long current_pos = ftell(ctx->file);

  fseek(ctx->file, offset, SEEK_SET);

  unsigned char block_data[KOLIBRI_BLOCK_SIZE];
  size_t read = fread(block_data, 1, KOLIBRI_BLOCK_SIZE, ctx->file);

  fseek(ctx->file, current_pos, SEEK_SET);

  if (read != KOLIBRI_BLOCK_SIZE) {
    return -1;
  }

  deserialize_block(block_data, out_block);

  if (out_block->index != index) {
    return -1; /* Несоответствие индекса */
  }

  return 0;
}

int kg_iterate_blocks(KolibriGenome *ctx, kg_block_callback callback,
                      void *user_data) {
  if (!ctx || !ctx->file || !callback) {
    return -1;
  }

  long current_pos = ftell(ctx->file);
  fseek(ctx->file, 0, SEEK_SET);

  unsigned char block_data[KOLIBRI_BLOCK_SIZE];
  ReasonBlock block;
  int count = 0;

  while (fread(block_data, 1, KOLIBRI_BLOCK_SIZE, ctx->file) ==
         KOLIBRI_BLOCK_SIZE) {
    deserialize_block(block_data, &block);

    int result = callback(&block, user_data);
    if (result != 0) {
      break; /* Callback запросил остановку */
    }

    count++;
  }

  fseek(ctx->file, current_pos, SEEK_SET);

  return count;
}

/* ===================================================================
 * v65: Фрактальная память — персистентное хранение в блокчейне генома
 *
 * Формат payload (все цифры):
 *   - Первые 2 цифры:  длина пути (01–30)
 *   - Следующие N цифр: десятичный путь фрактальной памяти
 *   - Остальные цифры:  значение, закодированное через kg_encode_payload
 * =================================================================== */

int kg_save_memory_node(KolibriGenome *ctx, const uint8_t *path,
                        size_t path_len, const char *value,
                        size_t value_len) {
  if (!ctx || !path || path_len == 0 || path_len > 30 ||
      !value || value_len == 0) {
    return -1;
  }

  /* Формируем payload из десятичных цифр */
  char payload[KOLIBRI_PAYLOAD_SIZE];
  memset(payload, 0, sizeof(payload));
  int pos = 0;

  /* Длина пути: 2 цифры */
  payload[pos++] = '0' + (char)(path_len / 10);
  payload[pos++] = '0' + (char)(path_len % 10);

  /* Десятичный путь */
  for (size_t i = 0; i < path_len && pos < KOLIBRI_PAYLOAD_SIZE - 1; i++) {
    payload[pos++] = '0' + (char)(path[i] % 10);
  }

  /* Кодируем значение: каждый байт → 3 десятичные цифры */
  for (size_t i = 0; i < value_len && pos + 3 < KOLIBRI_PAYLOAD_SIZE; i++) {
    unsigned char c = (unsigned char)value[i];
    payload[pos++] = '0' + (char)(c / 100);
    payload[pos++] = '0' + (char)((c / 10) % 10);
    payload[pos++] = '0' + (char)(c % 10);
  }
  payload[pos] = '\0';

  return kg_append(ctx, "FMEM", payload, NULL);
}

/* --- Загрузка узлов фрактальной памяти --- */

struct kg_memory_load_ctx {
  kg_memory_callback callback;
  void *user_data;
  int count;
};

static int memory_block_handler(const ReasonBlock *block, void *user_data) {
  struct kg_memory_load_ctx *mctx = (struct kg_memory_load_ctx *)user_data;

  /* Фильтруем по event_type "FMEM" */
  if (strncmp(block->event_type, "FMEM", 4) != 0) {
    return 0; /* пропускаем, продолжаем */
  }

  const char *p = block->payload;
  size_t plen = strnlen(p, KOLIBRI_PAYLOAD_SIZE);

  if (plen < 3) return 0; /* слишком короткий */

  /* Читаем длину пути (2 цифры) */
  size_t path_len = (size_t)(p[0] - '0') * 10 + (size_t)(p[1] - '0');
  if (path_len == 0 || path_len > 30 || 2 + path_len > plen) return 0;

  /* Читаем путь */
  uint8_t path[30];
  for (size_t i = 0; i < path_len; i++) {
    path[i] = (uint8_t)(p[2 + i] - '0');
  }

  /* Декодируем значение: тройки цифр → байты */
  size_t val_start = 2 + path_len;
  size_t val_digits = plen - val_start;
  size_t val_len = val_digits / 3;

  char value[KOLIBRI_PAYLOAD_SIZE];
  for (size_t i = 0; i < val_len && i * 3 + 2 < val_digits; i++) {
    unsigned char c = (unsigned char)(
        (p[val_start + i * 3] - '0') * 100 +
        (p[val_start + i * 3 + 1] - '0') * 10 +
        (p[val_start + i * 3 + 2] - '0'));
    value[i] = (char)c;
  }
  if (val_len < sizeof(value)) {
    value[val_len] = '\0';
  }

  /* Вызываем callback */
  if (mctx->callback) {
    mctx->callback(path, path_len, value, val_len, mctx->user_data);
  }
  mctx->count++;

  return 0;
}

int kg_load_memory_nodes(KolibriGenome *ctx, kg_memory_callback callback,
                         void *user_data) {
  if (!ctx || !callback) return -1;

  struct kg_memory_load_ctx mctx = {
    .callback = callback,
    .user_data = user_data,
    .count = 0
  };

  int result = kg_iterate_blocks(ctx, memory_block_handler, &mctx);
  if (result < 0) return -1;

  return mctx.count;
}
