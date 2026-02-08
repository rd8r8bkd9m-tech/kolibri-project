#include "kolibri/genome.h"

#include <assert.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint64_t read_u64_be(const unsigned char *data) {
  uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    value = (value << 8) | (uint64_t)data[i];
  }
  return value;
}

static void build_hmac_message(const unsigned char *block_bytes,
                               unsigned char *out_message) {
  memcpy(out_message, block_bytes, 16); /* index + timestamp */
  memcpy(out_message + 16, block_bytes + 16, KOLIBRI_HASH_SIZE);
  memcpy(out_message + 16 + KOLIBRI_HASH_SIZE,
         block_bytes + 16 + KOLIBRI_HASH_SIZE * 2,
         KOLIBRI_EVENT_TYPE_SIZE);
  memcpy(out_message + 16 + KOLIBRI_HASH_SIZE + KOLIBRI_EVENT_TYPE_SIZE,
         block_bytes + 16 + KOLIBRI_HASH_SIZE * 2 + KOLIBRI_EVENT_TYPE_SIZE,
         KOLIBRI_PAYLOAD_SIZE);
}

static void assert_payload_digits(const char *digits) {
  size_t len = strnlen(digits, KOLIBRI_PAYLOAD_SIZE);
  for (size_t i = 0; i < len; ++i) {
    assert(digits[i] >= '0' && digits[i] <= '9');
  }
}

void test_genome(void) {
  char template[] = "/tmp/kolibri_genomeXXXXXX";
  int fd = mkstemp(template);
  assert(fd != -1);
  close(fd);

  KolibriGenome genome;
  const unsigned char key[] = "test-key";
  int rc = kg_open(&genome, template, key, sizeof(key) - 1);
  assert(rc == 0);

  char payload1[KOLIBRI_PAYLOAD_SIZE];
  char payload2[KOLIBRI_PAYLOAD_SIZE];
  char payload3[KOLIBRI_PAYLOAD_SIZE];
  assert(kg_encode_payload("payload", payload1, sizeof(payload1)) == 0);
  assert(kg_encode_payload("second", payload2, sizeof(payload2)) == 0);
  assert(kg_encode_payload("third", payload3, sizeof(payload3)) == 0);
  assert_payload_digits(payload1);
  assert_payload_digits(payload2);
  assert_payload_digits(payload3);

  ReasonBlock block1;
  rc = kg_append(&genome, "TEST", payload1, &block1);
  assert(rc == 0);
  assert(block1.index == 0);
  assert(block1.timestamp > 1000000000000ULL);

  ReasonBlock block2;
  rc = kg_append(&genome, "TEST", payload2, &block2);
  assert(rc == 0);
  assert(block2.index == 1);

  rc = kg_append(&genome, "TEST", "notdigits", NULL);
  assert(rc == -1);

  ReasonBlock block3;
  rc = kg_append(&genome, "TEST", payload3, &block3);
  assert(rc == 0);
  assert(block3.index == 2);

  kg_close(&genome);

  FILE *f = fopen(template, "rb");
  assert(f != NULL);
  int seek_rc = fseek(f, 0, SEEK_END);
  assert(seek_rc == 0);
  long size = ftell(f);
  assert(size == 3L * (long)KOLIBRI_BLOCK_SIZE);
  seek_rc = fseek(f, 0, SEEK_SET);
  assert(seek_rc == 0);

  unsigned char *buffer = (unsigned char *)calloc(3U, KOLIBRI_BLOCK_SIZE);
  assert(buffer != NULL);
  size_t read = fread(buffer, 1, 3U * KOLIBRI_BLOCK_SIZE, f);
  assert(read == 3U * KOLIBRI_BLOCK_SIZE);
  fclose(f);

  const unsigned char *raw1 = buffer;
  const unsigned char *raw2 = buffer + KOLIBRI_BLOCK_SIZE;
  const unsigned char *raw3 = buffer + KOLIBRI_BLOCK_SIZE * 2U;

  assert(read_u64_be(raw1) == 0U);
  assert(read_u64_be(raw2) == 1U);
  assert(read_u64_be(raw3) == 2U);

  for (size_t i = 0; i < KOLIBRI_HASH_SIZE; ++i) {
    assert(raw1[16 + i] == 0U);
  }

  assert(strcmp((const char *)(raw1 + 16 + KOLIBRI_HASH_SIZE * 2), "TEST") == 0);
  assert(strcmp((const char *)(raw2 + 16 + KOLIBRI_HASH_SIZE * 2), "TEST") == 0);
  assert(strcmp((const char *)(raw3 + 16 + KOLIBRI_HASH_SIZE * 2), "TEST") == 0);
  assert(strcmp((const char *)(raw1 + 16 + KOLIBRI_HASH_SIZE * 2 +
                               KOLIBRI_EVENT_TYPE_SIZE),
                 payload1) == 0);
  assert(strcmp((const char *)(raw2 + 16 + KOLIBRI_HASH_SIZE * 2 +
                               KOLIBRI_EVENT_TYPE_SIZE),
                 payload2) == 0);
  assert(strcmp((const char *)(raw3 + 16 + KOLIBRI_HASH_SIZE * 2 +
                               KOLIBRI_EVENT_TYPE_SIZE),
                 payload3) == 0);

  unsigned char hash1[KOLIBRI_HASH_SIZE];
  unsigned char hash2[KOLIBRI_HASH_SIZE];
  assert(SHA256(raw1, KOLIBRI_BLOCK_SIZE, hash1) != NULL);
  assert(SHA256(raw2, KOLIBRI_BLOCK_SIZE, hash2) != NULL);
  assert(memcmp(raw2 + 16, hash1, KOLIBRI_HASH_SIZE) == 0);
  assert(memcmp(raw3 + 16, hash2, KOLIBRI_HASH_SIZE) == 0);

  unsigned char message[KOLIBRI_BLOCK_SIZE - KOLIBRI_HASH_SIZE];
  unsigned char computed[KOLIBRI_HASH_SIZE];
  unsigned int hmac_len = 0;
  build_hmac_message(raw3, message);
  unsigned char *hmac_result = HMAC(EVP_sha256(), key, sizeof(key) - 1, message,
                                    sizeof(message), computed, &hmac_len);
  assert(hmac_result != NULL);
  assert(hmac_len == KOLIBRI_HASH_SIZE);
  assert(memcmp(raw3 + 16 + KOLIBRI_HASH_SIZE, computed, KOLIBRI_HASH_SIZE) == 0);

  free(buffer);

  rc = kg_verify_file(template, key, sizeof(key) - 1);
  assert(rc == 0);

  f = fopen(template, "r+b");
  assert(f != NULL);
  seek_rc = fseek(f, (long)KOLIBRI_BLOCK_SIZE + 120L, SEEK_SET);
  assert(seek_rc == 0);
  int byte = fgetc(f);
  assert(byte != EOF);
  seek_rc = fseek(f, (long)KOLIBRI_BLOCK_SIZE + 120L, SEEK_SET);
  assert(seek_rc == 0);
  fputc((byte == 0x30) ? 0x31 : 0x30, f);
  fclose(f);

  rc = kg_verify_file(template, key, sizeof(key) - 1);
  assert(rc == -1);

  remove(template);

  rc = kg_verify_file(template, key, sizeof(key) - 1);
  assert(rc == 1);
}

/* ============================================================================
 * Тесты WAL (Write-Ahead Logging)
 * ============================================================================ */

void test_wal_enable_disable(void) {
  char template[] = "/tmp/kolibri_wal_testXXXXXX";
  int fd = mkstemp(template);
  assert(fd != -1);
  close(fd);

  KolibriGenome genome;
  const unsigned char key[] = "wal-test-key";
  int rc = kg_open(&genome, template, key, sizeof(key) - 1);
  assert(rc == 0);

  /* Включаем WAL */
  rc = kg_wal_enable(&genome);
  assert(rc == 0);
  assert(genome.wal_enabled == 1);

  /* Повторное включение не должно вызывать ошибку */
  rc = kg_wal_enable(&genome);
  assert(rc == 0);

  /* Отключаем WAL */
  rc = kg_wal_disable(&genome);
  assert(rc == 0);
  assert(genome.wal_enabled == 0);

  kg_close(&genome);
  remove(template);
}

void test_stream_append(void) {
  char template[] = "/tmp/kolibri_stream_testXXXXXX";
  int fd = mkstemp(template);
  assert(fd != -1);
  close(fd);

  KolibriGenome genome;
  const unsigned char key[] = "stream-key";
  int rc = kg_open(&genome, template, key, sizeof(key) - 1);
  assert(rc == 0);

  /* Включаем WAL */
  rc = kg_wal_enable(&genome);
  assert(rc == 0);

  /* Кодируем payload */
  char payload[KOLIBRI_PAYLOAD_SIZE];
  rc = kg_encode_payload("stream_test", payload, sizeof(payload));
  assert(rc == 0);

  /* Записываем через потоковый API */
  ReasonBlock block;
  rc = kg_stream_append(&genome, "STREAM", payload, &block);
  assert(rc == 0);
  assert(block.index == 0);

  /* Записываем ещё несколько блоков */
  for (int i = 0; i < 5; i++) {
    char buf[64];
    snprintf(buf, sizeof(buf), "block_%d", i);
    rc = kg_encode_payload(buf, payload, sizeof(payload));
    assert(rc == 0);
    rc = kg_stream_append(&genome, "BATCH", payload, &block);
    assert(rc == 0);
    assert(block.index == (uint64_t)(i + 1));
  }

  kg_wal_disable(&genome);
  kg_close(&genome);

  /* Проверяем целостность */
  rc = kg_verify_file(template, key, sizeof(key) - 1);
  assert(rc == 0);

  remove(template);
}

void test_genome_stats(void) {
  char template[] = "/tmp/kolibri_stats_testXXXXXX";
  int fd = mkstemp(template);
  assert(fd != -1);
  close(fd);

  KolibriGenome genome;
  const unsigned char key[] = "stats-key";
  int rc = kg_open(&genome, template, key, sizeof(key) - 1);
  assert(rc == 0);

  /* Записываем несколько блоков */
  char payload[KOLIBRI_PAYLOAD_SIZE];
  for (int i = 0; i < 10; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "stat_block_%d", i);
    rc = kg_encode_payload(buf, payload, sizeof(payload));
    assert(rc == 0);
    rc = kg_append(&genome, "STATS", payload, NULL);
    assert(rc == 0);
  }

  /* Проверяем статистику */
  KolibriGenomeStats stats;
  rc = kg_get_stats(&genome, &stats);
  assert(rc == 0);
  assert(stats.total_blocks == 10);
  assert(stats.file_size_bytes == 10 * KOLIBRI_BLOCK_SIZE);
  assert(stats.integrity_valid == 1);
  assert(stats.first_timestamp > 0);
  assert(stats.last_timestamp >= stats.first_timestamp);

  kg_close(&genome);
  remove(template);
}

void test_read_block(void) {
  char template[] = "/tmp/kolibri_read_testXXXXXX";
  int fd = mkstemp(template);
  assert(fd != -1);
  close(fd);

  KolibriGenome genome;
  const unsigned char key[] = "read-key";
  int rc = kg_open(&genome, template, key, sizeof(key) - 1);
  assert(rc == 0);

  /* Записываем блоки */
  char payload[KOLIBRI_PAYLOAD_SIZE];
  ReasonBlock written_blocks[5];
  for (int i = 0; i < 5; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "read_test_%d", i);
    rc = kg_encode_payload(buf, payload, sizeof(payload));
    assert(rc == 0);
    rc = kg_append(&genome, "READ_TEST", payload, &written_blocks[i]);
    assert(rc == 0);
  }

  /* Читаем и проверяем блоки */
  for (int i = 0; i < 5; i++) {
    ReasonBlock read_block;
    rc = kg_read_block(&genome, (uint64_t)i, &read_block);
    assert(rc == 0);
    assert(read_block.index == written_blocks[i].index);
    assert(read_block.timestamp == written_blocks[i].timestamp);
    assert(memcmp(read_block.hmac, written_blocks[i].hmac, KOLIBRI_HASH_SIZE) == 0);
  }

  /* Попытка чтения несуществующего блока */
  ReasonBlock bad_block;
  rc = kg_read_block(&genome, 100, &bad_block);
  assert(rc == -1);

  kg_close(&genome);
  remove(template);
}

static int iterate_counter = 0;
static int iterate_callback(const ReasonBlock *block, void *user_data) {
  (void)block;
  int *count = (int *)user_data;
  (*count)++;
  iterate_counter++;
  return 0;
}

void test_iterate_blocks(void) {
  char template[] = "/tmp/kolibri_iter_testXXXXXX";
  int fd = mkstemp(template);
  assert(fd != -1);
  close(fd);

  KolibriGenome genome;
  const unsigned char key[] = "iter-key";
  int rc = kg_open(&genome, template, key, sizeof(key) - 1);
  assert(rc == 0);

  /* Записываем блоки */
  char payload[KOLIBRI_PAYLOAD_SIZE];
  for (int i = 0; i < 7; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "iter_%d", i);
    rc = kg_encode_payload(buf, payload, sizeof(payload));
    assert(rc == 0);
    rc = kg_append(&genome, "ITER", payload, NULL);
    assert(rc == 0);
  }

  /* Итерируем */
  int counter = 0;
  iterate_counter = 0;
  int result = kg_iterate_blocks(&genome, iterate_callback, &counter);
  assert(result == 7);
  assert(counter == 7);
  assert(iterate_counter == 7);

  kg_close(&genome);
  remove(template);
}

void test_wal_checkpoint(void) {
  char template[] = "/tmp/kolibri_chk_testXXXXXX";
  int fd = mkstemp(template);
  assert(fd != -1);
  close(fd);

  KolibriGenome genome;
  const unsigned char key[] = "checkpoint-key";
  int rc = kg_open(&genome, template, key, sizeof(key) - 1);
  assert(rc == 0);

  rc = kg_wal_enable(&genome);
  assert(rc == 0);

  /* Записываем через потоковый API */
  char payload[KOLIBRI_PAYLOAD_SIZE];
  for (int i = 0; i < 10; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "checkpoint_%d", i);
    rc = kg_encode_payload(buf, payload, sizeof(payload));
    assert(rc == 0);
    rc = kg_stream_append(&genome, "CHECKPOINT", payload, NULL);
    assert(rc == 0);
  }

  /* Принудительный checkpoint */
  rc = kg_wal_checkpoint(&genome);
  assert(rc >= 0);

  kg_wal_disable(&genome);
  kg_close(&genome);

  /* Проверяем целостность */
  rc = kg_verify_file(template, key, sizeof(key) - 1);
  assert(rc == 0);

  remove(template);
}
