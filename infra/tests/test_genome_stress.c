#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/time.h>

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

  for (uint64_t i = 0; i < TEST_BLOCKS; i++) {
    snprintf(payload, sizeof(payload), "%llu", (unsigned long long)(i % 1000000));

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
    }

    if ((i + 1) % (TEST_BLOCKS / 10) == 0) {
      printf("  Progress: %llu / %u blocks written\n", (unsigned long long)(i + 1), TEST_BLOCKS);
    }
  }

  kg_close(&ctx);

  printf("  Blocks written: %llu\n", (unsigned long long)metrics.count);
  printf("  Failures: %llu\n", (unsigned long long)metrics.failures);
  printf("  Latency - Min: %llu µs, Max: %llu µs, Avg: %llu µs\n", 
         (unsigned long long)metrics.min_us,
         (unsigned long long)metrics.max_us, 
         (unsigned long long)(metrics.count ? metrics.total_us / metrics.count : 0));

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
  if (kg_open(&ctx, test_file, key, sizeof(key)) != 0) {
    printf("❌ FAILED: kg_open\n");
    return;
  }
  kg_wal_enable(&ctx);

  StressMetrics metrics = {0};
  metrics.min_us = UINT64_MAX;

  char payload[64];
  ReasonBlock block;

  for (uint64_t i = 0; i < TEST_BLOCKS / 100; i++) {
    snprintf(payload, sizeof(payload), "wal_%llu", (unsigned long long)i);

    uint64_t start = get_time_us();
    int ret = kg_append(&ctx, "stress_wal", payload, &block);
    uint64_t end = get_time_us();
    uint64_t latency_us = (end > start) ? (end - start) : 0;

    if (ret != 0) {
      metrics.failures++;
    } else {
      metrics.total_us += latency_us;
      metrics.count++;
      if (latency_us < metrics.min_us) metrics.min_us = latency_us;
      if (latency_us > metrics.max_us) metrics.max_us = latency_us;
    }

    if ((i + 1) % 1000 == 0) {
      printf("  Progress: %llu / %llu blocks written\n", (unsigned long long)(i + 1), (unsigned long long)(TEST_BLOCKS / 100));
    }
  }

  kg_close(&ctx);

  printf("  Blocks written: %llu\n", (unsigned long long)metrics.count);
  printf("  Failures: %llu\n", (unsigned long long)metrics.failures);
  printf("  Latency - Min: %llu µs, Max: %llu µs, Avg: %llu µs\n", 
         (unsigned long long)metrics.min_us,
         (unsigned long long)metrics.max_us, 
         (unsigned long long)(metrics.count ? metrics.total_us / metrics.count : 0));

  cleanup_test_files(test_file);
}

void test_crash_recovery(void) {
  printf("TEST 3: Crash recovery with WAL\n");
  const char *test_file = "/tmp/kolibri_stress_crash.dat";
  cleanup_test_files(test_file);

  unsigned char key[32];
  memset(key, 0x42, sizeof(key));

  KolibriGenome ctx;
  if (kg_open(&ctx, test_file, key, sizeof(key)) != 0) {
    printf("❌ FAILED: kg_open\n");
    return;
  }
  kg_wal_enable(&ctx);

  char payload[64];
  ReasonBlock block;
  uint64_t blocks_written = 0;

  for (uint64_t i = 0; i < 500; i++) {
    snprintf(payload, sizeof(payload), "block_%llu", (unsigned long long)i);
    if (kg_append(&ctx, "recovery_test", payload, &block) == 0) {
        blocks_written++;
    }
  }

  printf("  Wrote %llu blocks before crash\n", (unsigned long long)blocks_written);
  
  // Simulate crash by not calling kg_close and just clearing memory
  memset(&ctx, 0, sizeof(ctx));

  // Re-open and recover
  if (kg_open(&ctx, test_file, key, sizeof(key)) != 0) {
    printf("❌ FAILED: kg_open for recovery\n");
    return;
  }
  
  // Recovery should be automatic in kg_open or via kg_wal_recover
  kg_wal_recover(&ctx);

  KolibriGenomeStats stats;
  kg_get_stats(&ctx, &stats);
  printf("  Total blocks after recovery: %llu\n", (unsigned long long)stats.total_blocks);

  if (stats.total_blocks == blocks_written) {
    printf("✓ PASSED\n\n");
  } else {
    printf("❌ FAILED: expected %llu blocks, got %llu\n", 
           (unsigned long long)blocks_written, (unsigned long long)stats.total_blocks);
  }

  kg_close(&ctx);
  cleanup_test_files(test_file);
}

int main(void) {
  test_stress_write_sequential();
  test_stress_write_with_wal();
  test_crash_recovery();
  return 0;
}
