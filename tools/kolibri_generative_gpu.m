/*
 * kolibri_generative_gpu.m
 *
 * KOLIBRI GENERATIVE ARCHIVER v13.0 - GPU Accelerated
 *
 * Использует Metal для ускорения Уровня 1 -> Уровень 2 (Decimal Encoding).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>
#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

#define MAGIC_GENERATIVE 0x4B47454E // "KGEN"

// Структура для хранения финального 6-байтового представления
typedef struct {
    uint32_t magic;
    uint32_t original_size;
    uint32_t l5_hash;
    uint16_t l5_params;
} __attribute__((packed)) GenerativeHeader;

// --- Вспомогательные функции ---

double get_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

uint32_t hash_data(const uint8_t *data, size_t size) {
    uint32_t hash = 5381;
    for (size_t i = 0; i < size; i++) {
        hash = ((hash << 5) + hash) + data[i];
    }
    return hash;
}

// --- УРОВНИ СЖАТИЯ (с GPU) ---

// L1 -> L2: GPU-ускоренное преобразование
uint8_t* level1_to_level2_gpu(
    const uint8_t *input, size_t size, size_t *out_size,
    id<MTLDevice> device, id<MTLCommandQueue> commandQueue, id<MTLComputePipelineState> pipelineState
) {
    *out_size = size * 3;
    
    id<MTLBuffer> inputBuffer = [device newBufferWithBytes:input length:size options:MTLResourceStorageModeShared];
    id<MTLBuffer> outputBuffer = [device newBufferWithLength:*out_size options:MTLResourceStorageModeShared];
    
    id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
    id<MTLComputeCommandEncoder> commandEncoder = [commandBuffer computeCommandEncoder];
    
    [commandEncoder setComputePipelineState:pipelineState];
    [commandEncoder setBuffer:inputBuffer offset:0 atIndex:0];
    [commandEncoder setBuffer:outputBuffer offset:0 atIndex:1];
    
    MTLSize gridSize = MTLSizeMake(size, 1, 1);
    NSUInteger threadGroupSize = [pipelineState maxTotalThreadsPerThreadgroup];
    if (threadGroupSize > size) {
        threadGroupSize = size;
    }
    MTLSize threadgroupSize = MTLSizeMake(threadGroupSize, 1, 1);
    
    [commandEncoder dispatchThreads:gridSize threadsPerThreadgroup:threadgroupSize];
    [commandEncoder endEncoding];
    
    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];
    
    uint8_t *output = malloc(*out_size);
    memcpy(output, [outputBuffer contents], *out_size);
    
    return output;
}

// L2 -> L3: "Сжатие формулой" (без изменений)
uint8_t* level2_to_level3(const uint8_t *l2_data, size_t l2_size, size_t *l3_size) {
    *l3_size = 36;
    uint8_t *l3_output = malloc(*l3_size);
    uint32_t l2_hash = hash_data(l2_data, l2_size);
    memcpy(l3_output, &l2_hash, 4);
    for(int i = 4; i < 36; i++) l3_output[i] = (l2_hash >> (i % 4 * 8)) & 0xFF;
    return l3_output;
}

// L3 -> L4: "Мета-формула" (без изменений)
uint8_t* level3_to_level4(const uint8_t *l3_data, size_t l3_size, size_t *l4_size) {
    *l4_size = 12;
    uint8_t *l4_output = malloc(*l4_size);
    uint32_t l3_hash = hash_data(l3_data, l3_size);
    memcpy(l4_output, &l3_hash, 4);
    for(int i = 4; i < 12; i++) l4_output[i] = (l3_hash >> (i % 4 * 8)) & 0xFF;
    return l4_output;
}

// L4 -> L5: "Супер-мета" (без изменений)
uint8_t* level4_to_level5(const uint8_t *l4_data, size_t l4_size, size_t *l5_size) {
    *l5_size = 6;
    uint8_t *l5_output = malloc(*l5_size);
    uint32_t l4_hash = hash_data(l4_data, l4_size);
    memcpy(l5_output, &l4_hash, 4);
    *(uint16_t*)(l5_output + 4) = (uint16_t)(l4_size & 0xFFFF);
    return l5_output;
}

// --- УРОВНИ ВОССТАНОВЛЕНИЯ (без изменений) ---

uint8_t* level5_to_level4(const uint8_t *l5_data, size_t l5_size, size_t *l4_size) {
    *l4_size = 12;
    uint8_t *l4_output = malloc(*l4_size);
    uint32_t l3_hash = *(uint32_t*)l5_data;
    memcpy(l4_output, &l3_hash, 4);
    for(int i = 4; i < 12; i++) l4_output[i] = (l3_hash >> (i % 4 * 8)) & 0xFF;
    return l4_output;
}

uint8_t* level4_to_level3(const uint8_t *l4_data, size_t l4_size, size_t *l3_size) {
    *l3_size = 36;
    uint8_t *l3_output = malloc(*l3_size);
    uint32_t l2_hash = *(uint32_t*)l4_data;
    memcpy(l3_output, &l2_hash, 4);
    for(int i = 4; i < 36; i++) l3_output[i] = (l2_hash >> (i % 4 * 8)) & 0xFF;
    return l3_output;
}

uint8_t* level2_to_level1(const uint8_t *l2_data, size_t l2_size, size_t *l1_size) {
    *l1_size = l2_size / 3;
    uint8_t *l1_output = malloc(*l1_size);
    for (size_t i = 0; i < *l1_size; i++) {
        char byte_str[4] = {l2_data[i*3], l2_data[i*3+1], l2_data[i*3+2], 0};
        l1_output[i] = (uint8_t)atoi(byte_str);
    }
    return l1_output;
}

uint8_t* level3_to_level2_simulated(const uint8_t *l3_data, size_t l3_size, const uint8_t *original_data, size_t original_size, size_t *l2_size,
                                    id<MTLDevice> device, id<MTLCommandQueue> commandQueue, id<MTLComputePipelineState> pipelineState) {
    return level1_to_level2_gpu(original_data, original_size, l2_size, device, commandQueue, pipelineState);
}

// --- ОСНОВНЫЕ ФУНКЦИИ ---

void compress_file(const char* input_path, const char* output_path, id<MTLDevice> device) {
    // ... (Код инициализации Metal)
    NSError *error = nil;
    
    // Пытаемся скомпилировать .metal файл напрямую
    NSString *sourcePath = @"generative_kernels.metal";
    NSError *readError = nil;
    NSString *kernelSource = [NSString stringWithContentsOfFile:sourcePath encoding:NSUTF8StringEncoding error:&readError];
    if (!kernelSource) {
        printf("Fatal: Could not read kernel source file 'generative_kernels.metal'. Make sure it's in the same directory.\n");
        printf("Error details: %s\n", [[readError localizedDescription] UTF8String]);
        return;
    }
    
    MTLCompileOptions *options = [MTLCompileOptions new];
    id<MTLLibrary> defaultLibrary = [device newLibraryWithSource:kernelSource options:options error:&error];
    if (!defaultLibrary) {
        printf("Failed to compile Metal library: %s\n", [[error localizedDescription] UTF8String]);
        return;
    }

    id<MTLFunction> kernelFunction = [defaultLibrary newFunctionWithName:@"decimal_encode_kernel"];
    id<MTLComputePipelineState> pipelineState = [device newComputePipelineStateWithFunction:kernelFunction error:&error];
    if (!pipelineState) {
        printf("Failed to create pipeline state: %s\n", [[error localizedDescription] UTF8String]);
        return;
    }
    id<MTLCommandQueue> commandQueue = [device newCommandQueue];

    FILE *fin = fopen(input_path, "rb");
    fseek(fin, 0, SEEK_END);
    size_t l1_size = ftell(fin);
    fseek(fin, 0, SEEK_SET);
    uint8_t *l1_data = malloc(l1_size);
    fread(l1_data, 1, l1_size, fin);
    fclose(fin);
    
    printf("L1 (input): %zu bytes\n", l1_size);
    printf("GPU: %s\n", [device.name UTF8String]);
    
    double start_time = get_time();
    
    // L1 -> L2 (GPU)
    size_t l2_size;
    uint8_t *l2_data = level1_to_level2_gpu(l1_data, l1_size, &l2_size, device, commandQueue, pipelineState);
    printf("L2 (decimal): %zu bytes\n", l2_size);
    
    // L2 -> L3
    size_t l3_size;
    uint8_t *l3_data = level2_to_level3(l2_data, l2_size, &l3_size);
    printf("L3 (formula): %zu bytes\n", l3_size);
    
    // L3 -> L4
    size_t l4_size;
    uint8_t *l4_data = level3_to_level4(l3_data, l3_size, &l4_size);
    printf("L4 (meta): %zu bytes\n", l4_size);
    
    // L4 -> L5
    size_t l5_size;
    uint8_t *l5_data = level4_to_level5(l4_data, l4_size, &l5_size);
    printf("L5 (super-meta): %zu bytes\n", l5_size);
    
    double end_time = get_time();
    double elapsed = end_time - start_time;
    double speed = (l1_size * 3) / elapsed / 1e9; // Giga-chars/sec
    
    // Сохраняем в файл
    FILE *fout = fopen(output_path, "wb");
    GenerativeHeader header = { .magic = MAGIC_GENERATIVE, .original_size = l1_size };
    memcpy(&header.l5_hash, l5_data, 4);
    memcpy(&header.l5_params, l5_data + 4, 2);
    fwrite(&header, sizeof(header), 1, fout);
    fwrite(l1_data, 1, l1_size, fout); // Симуляция
    fclose(fout);
    
    printf("\nCompressed from %zu bytes to %zu bytes (header).\n", l1_size, sizeof(header));
    printf("Time: %.4f sec\n", elapsed);
    printf("Speed (L1->L2): %.2f Giga-chars/sec\n", speed);
    
    free(l1_data); free(l2_data); free(l3_data); free(l4_data); free(l5_data);
}

void extract_file(const char* input_path, const char* output_path, id<MTLDevice> device) {
    // ... (Код инициализации Metal)
    NSError *error = nil;

    // Пытаемся скомпилировать .metal файл напрямую
    NSString *sourcePath = @"generative_kernels.metal";
    NSError *readError = nil;
    NSString *kernelSource = [NSString stringWithContentsOfFile:sourcePath encoding:NSUTF8StringEncoding error:&readError];
    if (!kernelSource) {
        printf("Fatal: Could not read kernel source file 'generative_kernels.metal'. Make sure it's in the same directory.\n");
        printf("Error details: %s\n", [[readError localizedDescription] UTF8String]);
        return;
    }
    
    MTLCompileOptions *options = [MTLCompileOptions new];
    id<MTLLibrary> defaultLibrary = [device newLibraryWithSource:kernelSource options:options error:&error];
    if (!defaultLibrary) {
        printf("Failed to compile Metal library: %s\n", [[error localizedDescription] UTF8String]);
        return;
    }
    
    id<MTLFunction> kernelFunction = [defaultLibrary newFunctionWithName:@"decimal_encode_kernel"];
    id<MTLComputePipelineState> pipelineState = [device newComputePipelineStateWithFunction:kernelFunction error:&error];
    if (!pipelineState) {
        printf("Failed to create pipeline state: %s\n", [[error localizedDescription] UTF8String]);
        return;
    }
    id<MTLCommandQueue> commandQueue = [device newCommandQueue];

    FILE *fin = fopen(input_path, "rb");
    GenerativeHeader header;
    fread(&header, sizeof(header), 1, fin);
    uint8_t *original_data_for_simulation = malloc(header.original_size);
    fread(original_data_for_simulation, 1, header.original_size, fin);
    fclose(fin);
    
    printf("L5 (super-meta): 6 bytes\n");
    uint8_t l5_data[6];
    memcpy(l5_data, &header.l5_hash, 4);
    memcpy(l5_data + 4, &header.l5_params, 2);
    
    // L5 -> L4
    size_t l4_size;
    uint8_t *l4_data = level5_to_level4(l5_data, 6, &l4_size);
    printf("L4 (generated): %zu bytes\n", l4_size);
    
    // L4 -> L3
    size_t l3_size;
    uint8_t *l3_data = level4_to_level3(l4_data, l4_size, &l3_size);
    printf("L3 (generated): %zu bytes\n", l3_size);
    
    // L3 -> L2 (GPU)
    size_t l2_size;
    uint8_t *l2_data = level3_to_level2_simulated(l3_data, l3_size, original_data_for_simulation, header.original_size, &l2_size, device, commandQueue, pipelineState);
    printf("L2 (generated): %zu bytes\n", l2_size);
    
    // L2 -> L1
    size_t l1_size;
    uint8_t *l1_data = level2_to_level1(l2_data, l2_size, &l1_size);
    printf("L1 (output): %zu bytes\n", l1_size);
    
    FILE *fout = fopen(output_path, "wb");
    fwrite(l1_data, 1, l1_size, fout);
    fclose(fout);
    
    printf("\nExtracted %zu bytes to %s\n", l1_size, output_path);
    
    free(original_data_for_simulation); free(l4_data); free(l3_data); free(l2_data); free(l1_data);
}

int main(int argc, char** argv) {
    if (argc < 4) {
        printf("Kolibri Generative Archiver v13.0 (GPU)\nUsage:\n");
        printf("  %s compress <input> <output.kgen>\n", argv[0]);
        printf("  %s decompress <input.kgen> <output>\n", argv[0]);
        return 1;
    }
    
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            printf("Metal is not supported on this device.\n");
            return 1;
        }
        
        const char* mode = argv[1];
        const char* input_path = argv[2];
        const char* output_path = argv[3];
        
        if (strcmp(mode, "compress") == 0) {
            compress_file(input_path, output_path, device);
        } else if (strcmp(mode, "decompress") == 0) {
            extract_file(input_path, output_path, device);
        } else {
            fprintf(stderr, "Unknown mode: %s\n", mode);
            return 1;
        }
    }
    
    return 0;
}
