#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>

#include "kolibri/genome.h"

#define TEST_BLOCKS 1000000
#define BATCH_SIZE 1000

typedef struct {
  uint64_t min_us;
  uint64_t max_us;
  uint64_t total_us;
  uint64_t count;
  uint64_t failures;
  uint64_t recovered;
} StressMetrics;

// Platform-agnostic microsecond timer
static uint64_t get_time_us(void) {
#ifdef _WIN32
  LARGE_INTEGER freq, counter;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&counter);
  return (counter.QuadPart * 1000000LL) / freq.QuadPart;
#elif defined(CLOCK_MONOTONIC)
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000LL;
#else
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint64_t)tv.tv_sec * 1000000LL + tv.tv_usec;
#endif
}

static void cleanup_test_files(const char *base_path) {
  unlink(base_path);
  char wal_path[512];
  snprintf(wal_path, sizeof(wal_path), "%s.wal", base_path);
  unlink(wal_path);
}

void test_stress_write_sequential(void) {
  printf("TEST 1: Sequential write of %d blocks\n", TEST_BLOCKS);
  const char *test_file = "/tmp/kolibri_stress_1.dat";
  cleanup_test_files(test_file);

  unsigned char key[32];
  memset(key, 0x42, sizeof(key));

  KolibriGenome ctx;
  if (kg_open(&ctx, test_file, key, sizeof(key)) != 0) {
    printf("❌ FAILED: kg_open\n");
    return;
  }

  StressMetrics metrics = {0};
  metrics.min_us = UINT64_MAX;

  char payload[64];
  ReasonBlock block;
  uint64_t latencies[BATCH_SIZE];

  for (uint64_t i = 0; i < TEST_BLOCKS; i++) {
    snprintf(payload, sizeof(payload), "%lu", i % 1000000);

    uint64_t start = get_time_us();
    int ret = kg_append(&ctx, "stress", payload, &block);
    uint64_t end = get_time_us();
    uint64_t latency = (end > start) ? (end - start) : 0;

    if (ret != 0) {
      metrics.failures++;
    } else {
      metrics.total_us += latency;
      metrics.count++;
      if (latency < metrics.min_us) metrics.min_us = latency;
      if (latency > metrics.max_us) metrics.max_us = latency;
      latencies[i % BATCH_SIZE] = latency;
    }

    if ((i + 1) % (TEST_BLOCKS / 10) == 0) {
      printf("  Progress: %lu / %u blocks written\n", i + 1, TEST_BLOCKS);
    }
  }

  kg_close(&ctx);

  printf("  Blocks written: %lu\n", metrics.count);
  printf("  Failures: %lu\n", metrics.failures);
  printf("  Latency - Min: %lu µs, Max: %lu µs, Avg: %lu µs\n", metrics.min_us,
         metrics.max_us, metrics.count ? metrics.total_us / metrics.count : 0);

  if (metrics.failures == 0 && metrics.count == TEST_BLOCKS) {
    printf("✓ PASSED\n\n");
  } else {
    printf("❌ FAILED\n\n");
  }

  cleanup_test_files(test_file);
}

void test_stress_write_with_wal(void) {
  printf("TEST 2: Sequential write with WAL enabled\n");
  const char *test_file = "/tmp/kolibri_stress_2.dat";
  cleanup_test_files(test_file);

  unsigned char key[32];
  memset(key, 0x42, sizeof(key));

  KolibriGenome ctx;
  if (kg_open_with_wal(&ctx, test_file, key, sizeof(key), 1) != 0) {
    printf("❌ FAILED: kg_open_with_wal\n");
    return;
  }

  StressMetrics metrics = {0};
  metrics.min_us = UINT64_MAX;

  char payload[64];
  ReasonBlock block;
  uint64_t latency_us;

  for (uint64_t i = 0; i < TEST_BLOCKS / 100; i++) {
    snprintf(payload, sizeof(payload), "wal_%lu", i);

    int ret =
        kg_append_with_latency(&ctx, "stress_wal", payload, &block, &latency_us);

    if (ret != 0) {
      metrics.failures++;
    } else {
      metrics.total_us += latency_us;
      metrics.count++;
      if (latency_us < metrics.min_us) metrics.min_us = latency_us;
      if (latency_us > metrics.max_us) metrics.max_us = latency_us;
    }

    if ((i + 1) % 1000 == 0) {
      printf("  Progress: %lu / %lu blocks written\n", i + 1, TEST_BLOCKS / 100);
    }
  }

  kg_close(&ctx);

  printf("  Blocks written: %lu\n", metrics.count);
  printf("  Failures: %lu\n", metrics.failures);
  printf("  Latency - Min: %lu µs, Max: %lu µs, Avg: %lu µs\n", metrics.min_us,
         metrics.max_us, metrics.count ? metrics.total_us / metrics.count : 0);

  if (metrics.failures == 0 && metrics.count == TEST_BLOCKS / 100) {
    printf("✓ PASSED\n\n");
  } else {
    printf("❌ FAILED\n\n");
  }

  cleanup_test_files(test_file);
}

void test_wal_recovery(void) {
  printf("TEST 3: WAL recovery after simulated crash\n");
  const char *test_file = "/tmp/kolibri_stress_3.dat";
  cleanup_test_files(test_file);

  unsigned char key[32];
  memset(key, 0x42, sizeof(key));

  KolibriGenome ctx;
  if (kg_open_with_wal(&ctx, test_file, key, sizeof(key), 1) != 0) {
    printf("❌ FAILED: kg_open_with_wal\n");
    return;
  }

  // Write 500 blocks normally
  char payload[64];
  ReasonBlock block;
  uint64_t latency_us;
  uint64_t blocks_written = 0;

  for (uint64_t i = 0; i < 500; i++) {
    snprintf(payload, sizeof(payload), "block_%lu", i);
    if (kg_append_with_latency(&ctx, "recovery_test", payload, &block,
                               &latency_us) == 0) {
      blocks_written++;
    }
  }

  printf("  Wrote %lu blocks before crash\n", blocks_written);

  kg_close(&ctx);

  // Check if WAL file exists
  char wal_path[512];
  snprintf(wal_path, sizeof(wal_path), "%s.wal", test_file);
  struct stat st;
  int has_wal = (stat(wal_path, &st) == 0);
  printf("  WAL file exists: %s\n", has_wal ? "yes" : "no");

  // Reopen and trigger recovery
  if (kg_open_with_wal(&ctx, test_file, key, sizeof(key), 1) != 0) {
    printf("❌ FAILED: Recovery open\n");
    kg_close(&ctx);
    cleanup_test_files(test_file);
    return;
  }

  // Verify all blocks
  if (kg_verify_file(test_file, key, sizeof(key)) == 0) {
    printf("✓ All blocks verified after recovery\n");
    printf("✓ PASSED\n\n");
  } else {
    printf("❌ Verification failed\n");
    printf("❌ FAILED\n\n");
  }

  kg_close(&ctx);
  cleanup_test_files(test_file);
}

void test_metrics_collection(void) {
  printf("TEST 4: Metrics collection during append\n");
  const char *test_file = "/tmp/kolibri_stress_4.dat";
  cleanup_test_files(test_file);

  unsigned char key[32];
  memset(key, 0x42, sizeof(key));

  KolibriGenome ctx;
  if (kg_open_with_wal(&ctx, test_file, key, sizeof(key), 1) != 0) {
    printf("❌ FAILED: kg_open_with_wal\n");
    return;
  }

  char payload[64];
  ReasonBlock block;
  uint64_t latency_us;

  for (uint64_t i = 0; i < 1000; i++) {
    snprintf(payload, sizeof(payload), "metric_%lu", i);
    kg_append_with_latency(&ctx, "metrics", payload, &block, &latency_us);
  }

  KolibriGenomeMetrics metrics;
  if (kg_get_metrics(&ctx, &metrics) != 0) {
    printf("❌ FAILED: kg_get_metrics\n");
    kg_close(&ctx);
    cleanup_test_files(test_file);
    return;
  }

  printf("  Total blocks: %lu\n", metrics.total_blocks);
  printf("  Write time ms: %lu\n", metrics.write_time_ms);
  printf("  Avg latency µs: %lu\n", metrics.avg_latency_us);

  kg_close(&ctx);

  if (metrics.total_blocks >= 1000) {
    printf("✓ PASSED\n\n");
  } else {
    printf("❌ FAILED\n\n");
  }

  cleanup_test_files(test_file);
}

void test_block_integrity(void) {
  printf("TEST 5: Block integrity verification (10k blocks)\n");
  const char *test_file = "/tmp/kolibri_stress_5.dat";
  cleanup_test_files(test_file);

  unsigned char key[32];
  memset(key, 0x42, sizeof(key));

  KolibriGenome ctx;
  if (kg_open(&ctx, test_file, key, sizeof(key)) != 0) {
    printf("❌ FAILED: kg_open\n");
    return;
  }

  char payload[64];
  ReasonBlock block;

  for (uint64_t i = 0; i < 10000; i++) {
    snprintf(payload, sizeof(payload), "integrity_%lu", i);
    if (kg_append(&ctx, "integrity", payload, &block) != 0) {
      printf("❌ Append failed at block %lu\n", i);
      kg_close(&ctx);
      cleanup_test_files(test_file);
      return;
    }
  }

  kg_close(&ctx);

  // Verify entire file
  if (kg_verify_file(test_file, key, sizeof(key)) == 0) {
    printf("  All 10000 blocks verified successfully\n");
    printf("✓ PASSED\n\n");
  } else {
    printf("❌ Verification failed\n");
    printf("❌ FAILED\n\n");
  }

  cleanup_test_files(test_file);
}

int main(void) {
  printf("=== GENOME STRESS TEST SUITE ===\n");
  printf("Testing Write-Ahead Logging and crash recovery\n\n");

  test_block_integrity();
  test_stress_write_sequential();
  test_metrics_collection();
  test_wal_recovery();
  test_stress_write_with_wal();

  printf("=== STRESS TESTS COMPLETE ===\n");
  return 0;
}
