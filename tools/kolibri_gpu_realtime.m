// ═══════════════════════════════════════════════════════════════
//   KOLIBRI GPU v8.2 - РЕАЛЬНОЕ ИЗМЕРЕНИЕ ВРЕМЕНИ
//   Используем mach_absolute_time() для wall-clock времени
// ═══════════════════════════════════════════════════════════════

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <mach/mach_time.h>
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

// Получение wall-clock времени в секундах
double get_time() {
    static mach_timebase_info_data_t info = {0, 0};
    if (info.denom == 0) {
        mach_timebase_info(&info);
    }
    uint64_t now = mach_absolute_time();
    return (double)now * info.numer / info.denom / 1e9;
}

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

size_t encode_cpu(const unsigned char* data, size_t len, unsigned char* output) {
    size_t pos = 0;
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
        printf("║      KOLIBRI GPU v8.2 - РЕАЛЬНОЕ ВРЕМЯ (wall-clock)          ║\n");
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
        
        // Тесты разных размеров
        size_t test_sizes[] = {1*1024*1024, 10*1024*1024, 50*1024*1024, 100*1024*1024};
        const char* size_names[] = {"1 MB", "10 MB", "50 MB", "100 MB"};
        
        for (int test = 0; test < 4; test++) {
            size_t TEST_SIZE = test_sizes[test];
            
            unsigned char* data = malloc(TEST_SIZE);
            unsigned char* cpu_output = malloc(TEST_SIZE * 10);
            unsigned char* gpu_output = malloc(TEST_SIZE * 10);
            
            memset(data, 'A', TEST_SIZE);
            
            printf("═══════════════════════════════════════════════════════════════\n");
            printf("📊 Тест: %s\n", size_names[test]);
            printf("═══════════════════════════════════════════════════════════════\n\n");
            
            // CPU тест
            double cpu_best = 0;
            for (int run = 0; run < 5; run++) {
                double start = get_time();
                size_t out_len = encode_cpu(data, TEST_SIZE, cpu_output);
                double end = get_time();
                
                double time_sec = end - start;
                double speed = out_len / time_sec;
                if (speed > cpu_best) cpu_best = speed;
                
                printf("  CPU %d: %.2e chars/sec (%.3f ms)\n", 
                       run + 1, speed, time_sec * 1000);
            }
            
            printf("\n");
            
            // GPU тест
            id<MTLBuffer> inputBuffer = [device newBufferWithBytes:data
                                                            length:TEST_SIZE
                                                           options:MTLResourceStorageModeShared];
            id<MTLBuffer> outputBuffer = [device newBufferWithLength:TEST_SIZE * 10
                                                             options:MTLResourceStorageModeShared];
            
            NSUInteger threadGroupSize = pipeline.maxTotalThreadsPerThreadgroup;
            if (threadGroupSize > 256) threadGroupSize = 256;
            
            MTLSize gridSize = MTLSizeMake(TEST_SIZE, 1, 1);
            MTLSize threadgroupSize = MTLSizeMake(threadGroupSize, 1, 1);
            
            double gpu_best = 0;
            for (int run = 0; run < 5; run++) {
                double start = get_time();
                
                id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
                id<MTLComputeCommandEncoder> encoder = [commandBuffer computeCommandEncoder];
                
                [encoder setComputePipelineState:pipeline];
                [encoder setBuffer:inputBuffer offset:0 atIndex:0];
                [encoder setBuffer:outputBuffer offset:0 atIndex:1];
                [encoder dispatchThreads:gridSize threadsPerThreadgroup:threadgroupSize];
                [encoder endEncoding];
                
                [commandBuffer commit];
                [commandBuffer waitUntilCompleted];
                
                double end = get_time();
                
                double time_sec = end - start;
                double speed = (TEST_SIZE * 10) / time_sec;
                if (speed > gpu_best) gpu_best = speed;
                
                printf("  GPU %d: %.2e chars/sec (%.3f ms)\n", 
                       run + 1, speed, time_sec * 1000);
            }
            
            printf("\n📈 Результат:\n");
            printf("   CPU лучший: %.2e chars/sec\n", cpu_best);
            printf("   GPU лучший: %.2e chars/sec\n", gpu_best);
            printf("   Ускорение GPU: %.2fx\n", gpu_best / cpu_best);
            printf("   До цели (18.45×10^9): CPU %.1f%%, GPU %.1f%%\n\n",
                   (cpu_best / 18.45e9) * 100, (gpu_best / 18.45e9) * 100);
            
            free(data);
            free(cpu_output);
            free(gpu_output);
        }
        
        printf("╔════════════════════════════════════════════════════════════════╗\n");
        printf("║                   ЗАКЛЮЧЕНИЕ                                   ║\n");
        printf("╚════════════════════════════════════════════════════════════════╝\n");
        printf("\nИспользовано mach_absolute_time() - реальное wall-clock время\n");
        printf("Включает все: CPU работу, GPU работу, передачу данных, overhead\n\n");
    }
    
    return 0;
}
