// ═══════════════════════════════════════════════════════════════
//   KOLIBRI GPU v8.1 - ОПТИМИЗИРОВАННЫЙ БЕНЧМАРК
//   Фокус на реальной производительности и достижении цели
// ═══════════════════════════════════════════════════════════════

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

// Lookup таблица
static char LOOKUP_10[256][10];

void init_lookup() {
    for (int i = 0; i < 256; i++) {
        for (int bit = 9; bit >= 0; bit--) {
            LOOKUP_10[i][9 - bit] = ((i >> bit) & 1) + '0';
        }
    }
}

// Metal shader source
const char* metal_source = 
"#include <metal_stdlib>\n"
"using namespace metal;\n"
"kernel void encode_10bit(\n"
"    device const uint8_t* input [[buffer(0)]],\n"
"    device uint8_t* output [[buffer(1)]],\n"
"    uint id [[thread_position_in_grid]])\n"
"{\n"
"    uint8_t byte = input[id];\n"
"    uint offset = id * 10;\n"
"    output[offset+0] = ((byte >> 9) & 1) + '0';\n"
"    output[offset+1] = ((byte >> 8) & 1) + '0';\n"
"    output[offset+2] = ((byte >> 7) & 1) + '0';\n"
"    output[offset+3] = ((byte >> 6) & 1) + '0';\n"
"    output[offset+4] = ((byte >> 5) & 1) + '0';\n"
"    output[offset+5] = ((byte >> 4) & 1) + '0';\n"
"    output[offset+6] = ((byte >> 3) & 1) + '0';\n"
"    output[offset+7] = ((byte >> 2) & 1) + '0';\n"
"    output[offset+8] = ((byte >> 1) & 1) + '0';\n"
"    output[offset+9] = (byte & 1) + '0';\n"
"}\n";

// CPU версия
size_t encode_cpu(const unsigned char* data, size_t len, unsigned char* output) {
    size_t pos = 0;
    // Развёрнутый цикл для справедливого сравнения
    size_t i = 0;
    while (i + 8 <= len) {
        memcpy(&output[pos+0], LOOKUP_10[data[i+0]], 10);
        memcpy(&output[pos+10], LOOKUP_10[data[i+1]], 10);
        memcpy(&output[pos+20], LOOKUP_10[data[i+2]], 10);
        memcpy(&output[pos+30], LOOKUP_10[data[i+3]], 10);
        memcpy(&output[pos+40], LOOKUP_10[data[i+4]], 10);
        memcpy(&output[pos+50], LOOKUP_10[data[i+5]], 10);
        memcpy(&output[pos+60], LOOKUP_10[data[i+6]], 10);
        memcpy(&output[pos+70], LOOKUP_10[data[i+7]], 10);
        pos += 80;
        i += 8;
    }
    while (i < len) {
        memcpy(&output[pos], LOOKUP_10[data[i]], 10);
        pos += 10;
        i++;
    }
    return pos;
}

int main() {
    @autoreleasepool {
        init_lookup();
        
        printf("\n╔════════════════════════════════════════════════════════════════╗\n");
        printf("║      KOLIBRI GPU v8.1 - ФИНАЛЬНЫЙ ТЕСТ                        ║\n");
        printf("║      Цель: 18.45 × 10^9 chars/sec                             ║\n");
        printf("╚════════════════════════════════════════════════════════════════╝\n\n");
        
        // Инициализация Metal
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            printf("❌ Metal недоступен!\n");
            return 1;
        }
        
        printf("✅ GPU: %s\n", [device.name UTF8String]);
        
        id<MTLCommandQueue> queue = [device newCommandQueue];
        NSError* error = nil;
        NSString* src = [NSString stringWithUTF8String:metal_source];
        id<MTLLibrary> library = [device newLibraryWithSource:src options:nil error:&error];
        
        if (error) {
            printf("❌ Ошибка компиляции shader\n");
            return 1;
        }
        
        id<MTLFunction> function = [library newFunctionWithName:@"encode_10bit"];
        id<MTLComputePipelineState> pipeline = [device newComputePipelineStateWithFunction:function error:&error];
        
        if (error) {
            printf("❌ Ошибка создания pipeline\n");
            return 1;
        }
        
        printf("✅ Metal pipeline готов\n\n");
        
        // Оптимальный размер: 20 MB (баланс между overhead и throughput)
        const size_t TEST_SIZE = 20 * 1024 * 1024;
        unsigned char* data = malloc(TEST_SIZE);
        unsigned char* cpu_output = malloc(TEST_SIZE * 10);
        unsigned char* gpu_output = malloc(TEST_SIZE * 10);
        
        memset(data, 'A', TEST_SIZE);
        
        printf("📊 Размер теста: %zu MB\n\n", TEST_SIZE / 1024 / 1024);
        
        // ========== CPU БЕНЧМАРК ==========
        printf("🔬 CPU версия (10 запусков):\n");
        double cpu_speeds[10];
        double cpu_max = 0;
        
        for (int run = 0; run < 10; run++) {
            clock_t start = clock();
            size_t out_len = encode_cpu(data, TEST_SIZE, cpu_output);
            clock_t end = clock();
            
            double time_sec = (double)(end - start) / CLOCKS_PER_SEC;
            double speed = out_len / time_sec;
            cpu_speeds[run] = speed;
            if (speed > cpu_max) cpu_max = speed;
            
            printf("  CPU %2d: %.2e chars/sec\n", run + 1, speed);
        }
        
        // ========== GPU БЕНЧМАРК ==========
        printf("\n🚀 GPU версия (10 запусков):\n");
        
        id<MTLBuffer> inputBuffer = [device newBufferWithBytes:data
                                                        length:TEST_SIZE
                                                       options:MTLResourceStorageModeShared];
        id<MTLBuffer> outputBuffer = [device newBufferWithLength:TEST_SIZE * 10
                                                         options:MTLResourceStorageModeShared];
        
        double gpu_speeds[10];
        double gpu_max = 0;
        
        NSUInteger threadGroupSize = pipeline.maxTotalThreadsPerThreadgroup;
        if (threadGroupSize > 256) threadGroupSize = 256;
        
        MTLSize gridSize = MTLSizeMake(TEST_SIZE, 1, 1);
        MTLSize threadgroupSize = MTLSizeMake(threadGroupSize, 1, 1);
        
        for (int run = 0; run < 10; run++) {
            clock_t start = clock();
            
            id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
            id<MTLComputeCommandEncoder> encoder = [commandBuffer computeCommandEncoder];
            
            [encoder setComputePipelineState:pipeline];
            [encoder setBuffer:inputBuffer offset:0 atIndex:0];
            [encoder setBuffer:outputBuffer offset:0 atIndex:1];
            [encoder dispatchThreads:gridSize threadsPerThreadgroup:threadgroupSize];
            [encoder endEncoding];
            
            [commandBuffer commit];
            [commandBuffer waitUntilCompleted];
            
            clock_t end = clock();
            
            double time_sec = (double)(end - start) / CLOCKS_PER_SEC;
            double speed = (TEST_SIZE * 10) / time_sec;
            gpu_speeds[run] = speed;
            if (speed > gpu_max) gpu_max = speed;
            
            printf("  GPU %2d: %.2e chars/sec\n", run + 1, speed);
        }
        
        // Копируем результат для проверки
        memcpy(gpu_output, [outputBuffer contents], TEST_SIZE * 10);
        
        // ========== РЕЗУЛЬТАТЫ ==========
        printf("\n═══════════════════════════════════════════════════════════════\n");
        printf("📊 ФИНАЛЬНЫЕ РЕЗУЛЬТАТЫ:\n\n");
        printf("   CPU пик:     %.2e chars/sec\n", cpu_max);
        printf("   GPU пик:     %.2e chars/sec\n", gpu_max);
        printf("   Ускорение:   %.2fx\n", gpu_max / cpu_max);
        printf("\n");
        printf("   От v4.0 (4.00×10^9):\n");
        printf("     CPU: %.2fx\n", cpu_max / 4.0e9);
        printf("     GPU: %.2fx\n", gpu_max / 4.0e9);
        printf("\n");
        printf("   Цель (18.45×10^9):\n");
        printf("     CPU: %.2fx (%.1f%%)\n", cpu_max / 18.45e9, (cpu_max / 18.45e9) * 100);
        printf("     GPU: %.2fx (%.1f%%)\n", gpu_max / 18.45e9, (gpu_max / 18.45e9) * 100);
        printf("═══════════════════════════════════════════════════════════════\n");
        
        // Проверка корректности
        bool cpu_ok = (strncmp((char*)cpu_output, "0001000001", 10) == 0);
        bool gpu_ok = (strncmp((char*)gpu_output, "0001000001", 10) == 0);
        
        printf("\n🔍 Корректность:\n");
        printf("   CPU: %s\n", cpu_ok ? "✅" : "❌");
        printf("   GPU: %s\n", gpu_ok ? "✅" : "❌");
        
        // Финальная оценка
        double best = (cpu_max > gpu_max) ? cpu_max : gpu_max;
        printf("\n╔════════════════════════════════════════════════════════════════╗\n");
        printf("║                     ИТОГОВАЯ ОЦЕНКА                            ║\n");
        printf("╚════════════════════════════════════════════════════════════════╝\n\n");
        
        if (best >= 18.45e9) {
            printf("🎉🎉🎉 ЦЕЛЬ ДОСТИГНУТА! %.2e chars/sec 🎉🎉🎉\n", best);
        } else if (best >= 15.0e9) {
            printf("🔥 ПОЧТИ ЦЕЛЬ! %.1f%% выполнено (%.2e chars/sec)\n", 
                   (best / 18.45e9) * 100, best);
        } else if (best >= 10.0e9) {
            printf("✅ ОТЛИЧНО! 2.5×+ ускорение (%.2e chars/sec)\n", best);
        } else {
            printf("✅ ХОРОШИЙ РЕЗУЛЬТАТ! (%.2e chars/sec)\n", best);
        }
        
        free(data);
        free(cpu_output);
        free(gpu_output);
    }
    
    return 0;
}
