/*
 * Kolibri GPU Benchmark (Metal/macOS)
 * Compare CPU vs GPU encoding performance
 * 
 * Copyright (c) 2025 Kolibri Project
 * Licensed under MIT License
 */

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <stdio.h>
#import <stdlib.h>
#import <string.h>
#import <stdint.h>

/* Benchmark configuration */
#define WARMUP_ITERATIONS 3
#define BENCHMARK_ITERATIONS 10

/* Test sizes */
typedef struct {
    const char *name;
    size_t size;
} TestSize;

static const TestSize TEST_SIZES[] = {
    {"1MB", 1024 * 1024},
    {"10MB", 10 * 1024 * 1024},
    {"100MB", 100 * 1024 * 1024},
    {"1GB", 1024 * 1024 * 1024},
};
#define NUM_TEST_SIZES (sizeof(TEST_SIZES) / sizeof(TEST_SIZES[0]))

/* Metal shader source */
static const char *metal_shader_source = R"(
#include <metal_stdlib>
using namespace metal;

kernel void kolibri_encode(
    device const uint8_t *input [[buffer(0)]],
    device uint8_t *output [[buffer(1)]],
    constant uint32_t &count [[buffer(2)]],
    uint id [[thread_position_in_grid]]
) {
    if (id >= count) return;
    
    uint8_t byte = input[id];
    uint32_t out_idx = id * 3;
    
    // Convert byte to 3 decimal digits
    output[out_idx] = (byte / 100) + '0';
    output[out_idx + 1] = ((byte / 10) % 10) + '0';
    output[out_idx + 2] = (byte % 10) + '0';
}

kernel void kolibri_decode(
    device const uint8_t *input [[buffer(0)]],
    device uint8_t *output [[buffer(1)]],
    constant uint32_t &count [[buffer(2)]],
    uint id [[thread_position_in_grid]]
) {
    if (id >= count) return;
    
    uint32_t in_idx = id * 3;
    uint8_t hundreds = input[in_idx] - '0';
    uint8_t tens = input[in_idx + 1] - '0';
    uint8_t ones = input[in_idx + 2] - '0';
    
    output[id] = hundreds * 100 + tens * 10 + ones;
}
)";

/* Pre-computed lookup table for CPU encoding */
static const uint8_t DIGIT_LUT[256][3] = {
    {0,0,0},{0,0,1},{0,0,2},{0,0,3},{0,0,4},{0,0,5},{0,0,6},{0,0,7},{0,0,8},{0,0,9},
    {0,1,0},{0,1,1},{0,1,2},{0,1,3},{0,1,4},{0,1,5},{0,1,6},{0,1,7},{0,1,8},{0,1,9},
    {0,2,0},{0,2,1},{0,2,2},{0,2,3},{0,2,4},{0,2,5},{0,2,6},{0,2,7},{0,2,8},{0,2,9},
    {0,3,0},{0,3,1},{0,3,2},{0,3,3},{0,3,4},{0,3,5},{0,3,6},{0,3,7},{0,3,8},{0,3,9},
    {0,4,0},{0,4,1},{0,4,2},{0,4,3},{0,4,4},{0,4,5},{0,4,6},{0,4,7},{0,4,8},{0,4,9},
    {0,5,0},{0,5,1},{0,5,2},{0,5,3},{0,5,4},{0,5,5},{0,5,6},{0,5,7},{0,5,8},{0,5,9},
    {0,6,0},{0,6,1},{0,6,2},{0,6,3},{0,6,4},{0,6,5},{0,6,6},{0,6,7},{0,6,8},{0,6,9},
    {0,7,0},{0,7,1},{0,7,2},{0,7,3},{0,7,4},{0,7,5},{0,7,6},{0,7,7},{0,7,8},{0,7,9},
    {0,8,0},{0,8,1},{0,8,2},{0,8,3},{0,8,4},{0,8,5},{0,8,6},{0,8,7},{0,8,8},{0,8,9},
    {0,9,0},{0,9,1},{0,9,2},{0,9,3},{0,9,4},{0,9,5},{0,9,6},{0,9,7},{0,9,8},{0,9,9},
    {1,0,0},{1,0,1},{1,0,2},{1,0,3},{1,0,4},{1,0,5},{1,0,6},{1,0,7},{1,0,8},{1,0,9},
    {1,1,0},{1,1,1},{1,1,2},{1,1,3},{1,1,4},{1,1,5},{1,1,6},{1,1,7},{1,1,8},{1,1,9},
    {1,2,0},{1,2,1},{1,2,2},{1,2,3},{1,2,4},{1,2,5},{1,2,6},{1,2,7},{1,2,8},{1,2,9},
    {1,3,0},{1,3,1},{1,3,2},{1,3,3},{1,3,4},{1,3,5},{1,3,6},{1,3,7},{1,3,8},{1,3,9},
    {1,4,0},{1,4,1},{1,4,2},{1,4,3},{1,4,4},{1,4,5},{1,4,6},{1,4,7},{1,4,8},{1,4,9},
    {1,5,0},{1,5,1},{1,5,2},{1,5,3},{1,5,4},{1,5,5},{1,5,6},{1,5,7},{1,5,8},{1,5,9},
    {1,6,0},{1,6,1},{1,6,2},{1,6,3},{1,6,4},{1,6,5},{1,6,6},{1,6,7},{1,6,8},{1,6,9},
    {1,7,0},{1,7,1},{1,7,2},{1,7,3},{1,7,4},{1,7,5},{1,7,6},{1,7,7},{1,7,8},{1,7,9},
    {1,8,0},{1,8,1},{1,8,2},{1,8,3},{1,8,4},{1,8,5},{1,8,6},{1,8,7},{1,8,8},{1,8,9},
    {1,9,0},{1,9,1},{1,9,2},{1,9,3},{1,9,4},{1,9,5},{1,9,6},{1,9,7},{1,9,8},{1,9,9},
    {2,0,0},{2,0,1},{2,0,2},{2,0,3},{2,0,4},{2,0,5},{2,0,6},{2,0,7},{2,0,8},{2,0,9},
    {2,1,0},{2,1,1},{2,1,2},{2,1,3},{2,1,4},{2,1,5},{2,1,6},{2,1,7},{2,1,8},{2,1,9},
    {2,2,0},{2,2,1},{2,2,2},{2,2,3},{2,2,4},{2,2,5},{2,2,6},{2,2,7},{2,2,8},{2,2,9},
    {2,3,0},{2,3,1},{2,3,2},{2,3,3},{2,3,4},{2,3,5},{2,3,6},{2,3,7},{2,3,8},{2,3,9},
    {2,4,0},{2,4,1},{2,4,2},{2,4,3},{2,4,4},{2,4,5},{2,4,6},{2,4,7},{2,4,8},{2,4,9},
    {2,5,0},{2,5,1},{2,5,2},{2,5,3},{2,5,4},{2,5,5},
};

/* CPU encode */
static void cpu_encode(uint8_t *out, const uint8_t *in, size_t len) {
    for (size_t i = 0; i < len; i++) {
        const uint8_t *d = DIGIT_LUT[in[i]];
        out[i * 3] = d[0] + '0';
        out[i * 3 + 1] = d[1] + '0';
        out[i * 3 + 2] = d[2] + '0';
    }
}

/* Result structure */
typedef struct {
    const char *test_name;
    size_t data_size;
    double cpu_gbps;
    double gpu_gbps;
    double speedup;
} BenchmarkResult;

static BenchmarkResult results[NUM_TEST_SIZES];
static int num_results = 0;

/* GPU Benchmark class */
@interface KolibriGPUBenchmark : NSObject
@property (nonatomic, strong) id<MTLDevice> device;
@property (nonatomic, strong) id<MTLCommandQueue> commandQueue;
@property (nonatomic, strong) id<MTLComputePipelineState> encodePipeline;
@property (nonatomic, strong) id<MTLComputePipelineState> decodePipeline;

- (BOOL)setupMetal;
- (double)benchmarkGPUEncode:(const uint8_t *)input size:(size_t)size threadgroupSize:(NSUInteger)tgSize;
- (double)benchmarkCPUEncode:(const uint8_t *)input size:(size_t)size;
@end

@implementation KolibriGPUBenchmark

- (BOOL)setupMetal {
    self.device = MTLCreateSystemDefaultDevice();
    if (!self.device) {
        NSLog(@"Metal is not supported on this device");
        return NO;
    }
    
    NSLog(@"Using Metal device: %@", self.device.name);
    
    self.commandQueue = [self.device newCommandQueue];
    if (!self.commandQueue) {
        NSLog(@"Failed to create command queue");
        return NO;
    }
    
    // Compile shader
    NSError *error = nil;
    NSString *shaderSource = [NSString stringWithUTF8String:metal_shader_source];
    id<MTLLibrary> library = [self.device newLibraryWithSource:shaderSource
                                                       options:nil
                                                         error:&error];
    if (!library) {
        NSLog(@"Failed to compile shader: %@", error);
        return NO;
    }
    
    // Create encode pipeline
    id<MTLFunction> encodeFunction = [library newFunctionWithName:@"kolibri_encode"];
    self.encodePipeline = [self.device newComputePipelineStateWithFunction:encodeFunction
                                                                     error:&error];
    if (!self.encodePipeline) {
        NSLog(@"Failed to create encode pipeline: %@", error);
        return NO;
    }
    
    // Create decode pipeline
    id<MTLFunction> decodeFunction = [library newFunctionWithName:@"kolibri_decode"];
    self.decodePipeline = [self.device newComputePipelineStateWithFunction:decodeFunction
                                                                     error:&error];
    if (!self.decodePipeline) {
        NSLog(@"Failed to create decode pipeline: %@", error);
        return NO;
    }
    
    return YES;
}

- (double)benchmarkGPUEncode:(const uint8_t *)input size:(size_t)size threadgroupSize:(NSUInteger)tgSize {
    size_t outputSize = size * 3;
    
    // Create buffers
    id<MTLBuffer> inputBuffer = [self.device newBufferWithBytes:input
                                                         length:size
                                                        options:MTLResourceStorageModeShared];
    id<MTLBuffer> outputBuffer = [self.device newBufferWithLength:outputSize
                                                          options:MTLResourceStorageModeShared];
    
    uint32_t count = (uint32_t)size;
    
    // Warmup
    for (int w = 0; w < WARMUP_ITERATIONS; w++) {
        @autoreleasepool {
            id<MTLCommandBuffer> commandBuffer = [self.commandQueue commandBuffer];
            id<MTLComputeCommandEncoder> encoder = [commandBuffer computeCommandEncoder];
            
            [encoder setComputePipelineState:self.encodePipeline];
            [encoder setBuffer:inputBuffer offset:0 atIndex:0];
            [encoder setBuffer:outputBuffer offset:0 atIndex:1];
            [encoder setBytes:&count length:sizeof(count) atIndex:2];
            
            MTLSize threadgroupSize = MTLSizeMake(tgSize, 1, 1);
            MTLSize gridSize = MTLSizeMake(size, 1, 1);
            
            [encoder dispatchThreads:gridSize threadsPerThreadgroup:threadgroupSize];
            [encoder endEncoding];
            
            [commandBuffer commit];
            [commandBuffer waitUntilCompleted];
        }
    }
    
    // Benchmark
    double totalTime = 0;
    for (int iter = 0; iter < BENCHMARK_ITERATIONS; iter++) {
        @autoreleasepool {
            id<MTLCommandBuffer> commandBuffer = [self.commandQueue commandBuffer];
            id<MTLComputeCommandEncoder> encoder = [commandBuffer computeCommandEncoder];
            
            [encoder setComputePipelineState:self.encodePipeline];
            [encoder setBuffer:inputBuffer offset:0 atIndex:0];
            [encoder setBuffer:outputBuffer offset:0 atIndex:1];
            [encoder setBytes:&count length:sizeof(count) atIndex:2];
            
            MTLSize threadgroupSize = MTLSizeMake(tgSize, 1, 1);
            MTLSize gridSize = MTLSizeMake(size, 1, 1);
            
            [encoder dispatchThreads:gridSize threadsPerThreadgroup:threadgroupSize];
            [encoder endEncoding];
            
            CFAbsoluteTime start = CFAbsoluteTimeGetCurrent();
            [commandBuffer commit];
            [commandBuffer waitUntilCompleted];
            CFAbsoluteTime end = CFAbsoluteTimeGetCurrent();
            
            totalTime += (end - start);
        }
    }
    
    double avgTime = totalTime / BENCHMARK_ITERATIONS;
    return (size / (1024.0 * 1024.0 * 1024.0)) / avgTime;
}

- (double)benchmarkCPUEncode:(const uint8_t *)input size:(size_t)size {
    size_t outputSize = size * 3;
    uint8_t *output = (uint8_t *)malloc(outputSize);
    if (!output) return 0;
    
    // Warmup
    for (int w = 0; w < WARMUP_ITERATIONS; w++) {
        cpu_encode(output, input, size);
    }
    
    // Benchmark
    double totalTime = 0;
    for (int iter = 0; iter < BENCHMARK_ITERATIONS; iter++) {
        CFAbsoluteTime start = CFAbsoluteTimeGetCurrent();
        cpu_encode(output, input, size);
        CFAbsoluteTime end = CFAbsoluteTimeGetCurrent();
        totalTime += (end - start);
    }
    
    free(output);
    
    double avgTime = totalTime / BENCHMARK_ITERATIONS;
    return (size / (1024.0 * 1024.0 * 1024.0)) / avgTime;
}

@end

/* Output results */
static void printResults(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════════════╗\n");
    printf("║                   CPU vs GPU COMPARISON TABLE                             ║\n");
    printf("╠═══════════════════════════════════════════════════════════════════════════╣\n");
    printf("║ %-10s │ %12s │ %12s │ %12s │ %10s ║\n",
           "Test", "CPU (GB/s)", "GPU (GB/s)", "Chars/sec", "Speedup");
    printf("╠════════════╪══════════════╪══════════════╪══════════════╪════════════╣\n");
    
    for (int i = 0; i < num_results; i++) {
        BenchmarkResult *r = &results[i];
        double gpu_chars = r->gpu_gbps * 1e9;
        printf("║ %-10s │ %10.2f   │ %10.2f   │ %10.2e   │ %8.1fx   ║\n",
               r->test_name, r->cpu_gbps, r->gpu_gbps, gpu_chars, r->speedup);
    }
    
    printf("╚════════════╧══════════════╧══════════════╧══════════════╧════════════╝\n");
}

static void outputJSON(const char *filename) {
    FILE *f = fopen(filename, "w");
    if (!f) {
        fprintf(stderr, "Failed to open %s\n", filename);
        return;
    }
    
    fprintf(f, "{\n");
    fprintf(f, "  \"benchmark\": \"Kolibri GPU Benchmark\",\n");
    fprintf(f, "  \"results\": [\n");
    
    for (int i = 0; i < num_results; i++) {
        BenchmarkResult *r = &results[i];
        fprintf(f, "    {\n");
        fprintf(f, "      \"test\": \"%s\",\n", r->test_name);
        fprintf(f, "      \"data_size\": %zu,\n", r->data_size);
        fprintf(f, "      \"cpu_gbps\": %.4f,\n", r->cpu_gbps);
        fprintf(f, "      \"gpu_gbps\": %.4f,\n", r->gpu_gbps);
        fprintf(f, "      \"speedup\": %.2f\n", r->speedup);
        fprintf(f, "    }%s\n", i < num_results - 1 ? "," : "");
    }
    
    fprintf(f, "  ]\n");
    fprintf(f, "}\n");
    fclose(f);
    printf("\nJSON results written to: %s\n", filename);
}

int main(int argc, const char * argv[]) {
    @autoreleasepool {
        const char *json_file = NULL;
        int quick_mode = 0;
        
        // Parse arguments
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--quick") == 0) {
                quick_mode = 1;
            } else if (strncmp(argv[i], "--json=", 7) == 0) {
                json_file = argv[i] + 7;
            }
        }
        
        printf("\n");
        printf("╔═══════════════════════════════════════════════════════════════════════════╗\n");
        printf("║     KOLIBRI GPU BENCHMARK (Metal/macOS)                                   ║\n");
        printf("║     Comparing CPU vs GPU encoding performance                             ║\n");
        printf("╚═══════════════════════════════════════════════════════════════════════════╝\n\n");
        
        KolibriGPUBenchmark *benchmark = [[KolibriGPUBenchmark alloc] init];
        
        if (![benchmark setupMetal]) {
            printf("\n  ❌ Metal setup failed. GPU benchmark not available.\n");
            printf("     Running CPU-only benchmark instead.\n\n");
            // Fall through to CPU-only benchmark
        }
        
        int max_tests = quick_mode ? 2 : (int)NUM_TEST_SIZES;
        
        for (int t = 0; t < max_tests; t++) {
            const TestSize *ts = &TEST_SIZES[t];
            
            printf("\n  Testing %s...\n", ts->name);
            
            // Generate test data
            uint8_t *input = (uint8_t *)malloc(ts->size);
            if (!input) {
                fprintf(stderr, "  Memory allocation failed for %s\n", ts->name);
                continue;
            }
            
            for (size_t i = 0; i < ts->size; i++) {
                input[i] = (uint8_t)((i * 73 + 17) % 256);
            }
            
            // CPU benchmark
            double cpu_gbps = [benchmark benchmarkCPUEncode:input size:ts->size];
            printf("    CPU: %.2f GB/s\n", cpu_gbps);
            
            // GPU benchmark (test different threadgroup sizes)
            double gpu_gbps = 0;
            if (benchmark.device) {
                NSUInteger threadgroupSizes[] = {64, 128, 256, 512, 1024};
                int numSizes = sizeof(threadgroupSizes) / sizeof(threadgroupSizes[0]);
                
                for (int s = 0; s < numSizes; s++) {
                    double gbps = [benchmark benchmarkGPUEncode:input 
                                                          size:ts->size 
                                               threadgroupSize:threadgroupSizes[s]];
                    printf("    GPU (tg=%lu): %.2f GB/s\n", 
                           (unsigned long)threadgroupSizes[s], gbps);
                    if (gbps > gpu_gbps) {
                        gpu_gbps = gbps;
                    }
                }
            }
            
            // Store results
            BenchmarkResult *r = &results[num_results++];
            r->test_name = ts->name;
            r->data_size = ts->size;
            r->cpu_gbps = cpu_gbps;
            r->gpu_gbps = gpu_gbps;
            r->speedup = gpu_gbps > 0 ? gpu_gbps / cpu_gbps : 0;
            
            free(input);
        }
        
        // Print results
        printResults();
        
        // Output JSON if requested
        if (json_file) {
            outputJSON(json_file);
        }
        
        printf("\n");
        printf("═══════════════════════════════════════════════════════════════════════════════\n");
        printf("  BENCHMARK COMPLETE\n");
        printf("═══════════════════════════════════════════════════════════════════════════════\n\n");
    }
    return 0;
}
