// ═══════════════════════════════════════════════════════════════
//   KOLIBRI ULTRA v5.0 - 5× SPEED BOOST
//   Цель: 18.45 × 10^9 chars/sec (5× от v4.0)
// ═══════════════════════════════════════════════════════════════

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>

// ═══════════════════════════════════════════════════════════════
//         LOOKUP КАК UINT32_T (3 байта упакованы в int)
// ═══════════════════════════════════════════════════════════════

// Оптимизация: храним 3 цифры как один uint32_t для быстрой записи
static uint32_t LOOKUP_PACKED[256];

void init_lookup() {
    for (int i = 0; i < 256; i++) {
        unsigned char d0 = i / 100;
        unsigned char d1 = (i % 100) / 10;
        unsigned char d2 = i % 10;
        
        // Упаковываем 3 байта в uint32_t (little-endian)
        LOOKUP_PACKED[i] = d0 | (d1 << 8) | (d2 << 16);
    }
}

// ═══════════════════════════════════════════════════════════════
//              УЛЬТРА-БЫСТРОЕ КОДИРОВАНИЕ v5.0
//         Прямая запись 32-битных значений + unrolling
// ═══════════════════════════════════════════════════════════════

static inline void ultra_encode_v5(const unsigned char* data, size_t len, unsigned char* out) {
    size_t pos = 0;
    size_t i = 0;
    
    // Обрабатываем блоками по 32 байта с прямой записью
    for (; i + 32 <= len; i += 32) {
        // Развёрнутый цикл для 32 байт (без опасных приведений типов)
        uint32_t p;
        
        p = LOOKUP_PACKED[data[i+0]];  out[pos++] = p; out[pos++] = p>>8; out[pos++] = p>>16;
        p = LOOKUP_PACKED[data[i+1]];  out[pos++] = p; out[pos++] = p>>8; out[pos++] = p>>16;
        p = LOOKUP_PACKED[data[i+2]];  out[pos++] = p; out[pos++] = p>>8; out[pos++] = p>>16;
        p = LOOKUP_PACKED[data[i+3]];  out[pos++] = p; out[pos++] = p>>8; out[pos++] = p>>16;
        p = LOOKUP_PACKED[data[i+4]];  out[pos++] = p; out[pos++] = p>>8; out[pos++] = p>>16;
        p = LOOKUP_PACKED[data[i+5]];  out[pos++] = p; out[pos++] = p>>8; out[pos++] = p>>16;
        p = LOOKUP_PACKED[data[i+6]];  out[pos++] = p; out[pos++] = p>>8; out[pos++] = p>>16;
        p = LOOKUP_PACKED[data[i+7]];  out[pos++] = p; out[pos++] = p>>8; out[pos++] = p>>16;
        p = LOOKUP_PACKED[data[i+8]];  out[pos++] = p; out[pos++] = p>>8; out[pos++] = p>>16;
        p = LOOKUP_PACKED[data[i+9]];  out[pos++] = p; out[pos++] = p>>8; out[pos++] = p>>16;
        p = LOOKUP_PACKED[data[i+10]]; out[pos++] = p; out[pos++] = p>>8; out[pos++] = p>>16;
        p = LOOKUP_PACKED[data[i+11]]; out[pos++] = p; out[pos++] = p>>8; out[pos++] = p>>16;
        p = LOOKUP_PACKED[data[i+12]]; out[pos++] = p; out[pos++] = p>>8; out[pos++] = p>>16;
        p = LOOKUP_PACKED[data[i+13]]; out[pos++] = p; out[pos++] = p>>8; out[pos++] = p>>16;
        p = LOOKUP_PACKED[data[i+14]]; out[pos++] = p; out[pos++] = p>>8; out[pos++] = p>>16;
        p = LOOKUP_PACKED[data[i+15]]; out[pos++] = p; out[pos++] = p>>8; out[pos++] = p>>16;
        p = LOOKUP_PACKED[data[i+16]]; out[pos++] = p; out[pos++] = p>>8; out[pos++] = p>>16;
        p = LOOKUP_PACKED[data[i+17]]; out[pos++] = p; out[pos++] = p>>8; out[pos++] = p>>16;
        p = LOOKUP_PACKED[data[i+18]]; out[pos++] = p; out[pos++] = p>>8; out[pos++] = p>>16;
        p = LOOKUP_PACKED[data[i+19]]; out[pos++] = p; out[pos++] = p>>8; out[pos++] = p>>16;
        p = LOOKUP_PACKED[data[i+20]]; out[pos++] = p; out[pos++] = p>>8; out[pos++] = p>>16;
        p = LOOKUP_PACKED[data[i+21]]; out[pos++] = p; out[pos++] = p>>8; out[pos++] = p>>16;
        p = LOOKUP_PACKED[data[i+22]]; out[pos++] = p; out[pos++] = p>>8; out[pos++] = p>>16;
        p = LOOKUP_PACKED[data[i+23]]; out[pos++] = p; out[pos++] = p>>8; out[pos++] = p>>16;
        p = LOOKUP_PACKED[data[i+24]]; out[pos++] = p; out[pos++] = p>>8; out[pos++] = p>>16;
        p = LOOKUP_PACKED[data[i+25]]; out[pos++] = p; out[pos++] = p>>8; out[pos++] = p>>16;
        p = LOOKUP_PACKED[data[i+26]]; out[pos++] = p; out[pos++] = p>>8; out[pos++] = p>>16;
        p = LOOKUP_PACKED[data[i+27]]; out[pos++] = p; out[pos++] = p>>8; out[pos++] = p>>16;
        p = LOOKUP_PACKED[data[i+28]]; out[pos++] = p; out[pos++] = p>>8; out[pos++] = p>>16;
        p = LOOKUP_PACKED[data[i+29]]; out[pos++] = p; out[pos++] = p>>8; out[pos++] = p>>16;
        p = LOOKUP_PACKED[data[i+30]]; out[pos++] = p; out[pos++] = p>>8; out[pos++] = p>>16;
        p = LOOKUP_PACKED[data[i+31]]; out[pos++] = p; out[pos++] = p>>8; out[pos++] = p>>16;
    }
    
    // Остаток
    for (; i < len; i++) {
        uint32_t packed = LOOKUP_PACKED[data[i]];
        out[pos++] = packed & 0xFF;
        out[pos++] = (packed >> 8) & 0xFF;
        out[pos++] = (packed >> 16) & 0xFF;
    }
}

// ═══════════════════════════════════════════════════════════════
//                    MULTI-THREADING SUPPORT
// ═══════════════════════════════════════════════════════════════

typedef struct {
    const unsigned char* data;
    size_t start;
    size_t end;
    unsigned char* out;
} ThreadTask;

void* encode_thread(void* arg) {
    ThreadTask* task = (ThreadTask*)arg;
    ultra_encode_v5(task->data + task->start, 
                    task->end - task->start,
                    task->out + task->start * 3);
    return NULL;
}

// ═══════════════════════════════════════════════════════════════
//                         БЕНЧМАРК
// ═══════════════════════════════════════════════════════════════

int main() {
    init_lookup();
    
    printf("\n╔════════════════════════════════════════════════════════════════╗\n");
    printf("║      KOLIBRI ULTRA v5.0 - 5× SPEED BOOST                      ║\n");
    printf("║      Цель: 18.45 × 10^9 chars/sec                             ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n\n");
    
    // Тестовые данные 200 MB
    const size_t TEST_SIZE = 200 * 1024 * 1024;
    unsigned char* data = malloc(TEST_SIZE);
    unsigned char* output = malloc(TEST_SIZE * 3);
    
    memset(data, 'A', TEST_SIZE);
    
    printf("📊 Тестовые данные: %zu MB\n", TEST_SIZE / 1024 / 1024);
    
    // ========== ТЕСТ 1: Однопоточный ==========
    printf("\n🔬 ТЕСТ 1: Однопоточное кодирование\n");
    clock_t start = clock();
    ultra_encode_v5(data, TEST_SIZE, output);
    clock_t end = clock();
    
    double time_sec = (double)(end - start) / CLOCKS_PER_SEC;
    uint64_t total_chars = (uint64_t)TEST_SIZE * 3;
    double chars_per_sec = total_chars / time_sec;
    
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("⏱️  Время: %.3f сек\n", time_sec);
    printf("⚡ Скорость: %.2e chars/sec\n", chars_per_sec);
    printf("📈 От v4.0 (3.69×10^9): %.2fx\n", chars_per_sec / 3.69e9);
    printf("═══════════════════════════════════════════════════════════════\n");
    
    // Проверка корректности
    if (strncmp((char*)output, "065065065", 9) == 0) {
        printf("✅ Кодирование корректно!\n");
    } else {
        printf("❌ Ошибка: %.9s (ожидалось 065065065)\n", output);
    }
    
    // ========== ТЕСТ 2: Multi-threading (4 потока) ==========
    printf("\n🔬 ТЕСТ 2: Multi-threading (4 потока)\n");
    
    const int NUM_THREADS = 4;
    pthread_t threads[NUM_THREADS];
    ThreadTask tasks[NUM_THREADS];
    
    size_t chunk_size = TEST_SIZE / NUM_THREADS;
    
    start = clock();
    
    for (int i = 0; i < NUM_THREADS; i++) {
        tasks[i].data = data;
        tasks[i].start = i * chunk_size;
        tasks[i].end = (i == NUM_THREADS - 1) ? TEST_SIZE : (i + 1) * chunk_size;
        tasks[i].out = output;
        pthread_create(&threads[i], NULL, encode_thread, &tasks[i]);
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    end = clock();
    time_sec = (double)(end - start) / CLOCKS_PER_SEC;
    chars_per_sec = total_chars / time_sec;
    
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("⏱️  Время: %.3f сек\n", time_sec);
    printf("⚡ Скорость: %.2e chars/sec\n", chars_per_sec);
    printf("📈 От v4.0 (3.69×10^9): %.2fx\n", chars_per_sec / 3.69e9);
    printf("🎯 Цель (18.45×10^9): %.2fx\n", chars_per_sec / 18.45e9);
    printf("═══════════════════════════════════════════════════════════════\n");
    
    if (chars_per_sec >= 18.45e9) {
        printf("\n✅ ЦЕЛЬ ДОСТИГНУТА! 5× ускорение подтверждено!\n");
    } else {
        printf("\n⚠️  Близко к цели (достигнуто %.1f×)\n", chars_per_sec / 3.69e9);
    }
    
    free(data);
    free(output);
    
    return 0;
}
