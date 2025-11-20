#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <stdio.h>
#include <string.h>
#include "kolibri_gpu_encoder.h"

extern "C" {
int kolibri_gpu_metal_init(const kolibri_gpu_config_t *config);
void kolibri_gpu_metal_shutdown(void);
int kolibri_gpu_metal_encode(const kolibri_gpu_reason_batch_t *input,
                             kolibri_gpu_embedding_batch_t *output);
int kolibri_gpu_metal_decode(const kolibri_gpu_embedding_batch_t *input,
                             kolibri_gpu_reason_batch_t *output);
int kolibri_gpu_metal_embed_tokens(const uint16_t *tokens,
                                   size_t token_count,
                                   kolibri_gpu_embedding_batch_t *output);
}

typedef struct {
    uint32_t payload_len;
    uint32_t embedding_dims;
    uint32_t count;
} KolibriMetalUniforms;

static id<MTLDevice> g_kolibri_device = nil;
static id<MTLCommandQueue> g_kolibri_queue = nil;
static id<MTLComputePipelineState> g_kolibri_pipeline = nil;

static const char *kKolibriMetalSource = R"METAL(
#include <metal_stdlib>
using namespace metal;

struct KolibriMetalUniforms {
    uint payload_len;
    uint embedding_dims;
    uint count;
};

kernel void kolibri_reasonblock_embed(const device uchar *payloads        [[buffer(0)]],
                                      device float *embeddings            [[buffer(1)]],
                                      constant KolibriMetalUniforms &uni  [[buffer(2)]],
                                      uint gid                            [[thread_position_in_grid]]) {
    if (gid >= uni.count) {
        return;
    }

    const device uchar *payload = payloads + gid * uni.payload_len;
    device float *embedding = embeddings + gid * uni.embedding_dims;

    float sum = 0.0f;
    float energy = 0.0f;
    float maxv = 0.0f;
    float minv = 255.0f;
    float transitions = 0.0f;
    float prev = payload[0];

    for (uint i = 0; i < uni.payload_len; ++i) {
        float value = payload[i];
        sum += value;
        energy += value * value;
        maxv = max(maxv, value);
        minv = min(minv, value);
        if (i > 0 && value != prev) {
            transitions += 1.0f;
        }
        prev = value;
    }

    float flen = max(1.0f, (float)uni.payload_len);
    float mean = sum / flen;
    float variance = max(0.0f, (energy / flen) - mean * mean);

    if (uni.embedding_dims >= 1) embedding[0] = mean / 255.0f;
    if (uni.embedding_dims >= 2) embedding[1] = variance / (255.0f * 255.0f);
    if (uni.embedding_dims >= 3) embedding[2] = (maxv - minv) / 255.0f;
    if (uni.embedding_dims >= 4) embedding[3] = transitions / flen;

    for (uint d = 4; d < uni.embedding_dims; ++d) {
        embedding[d] = 0.0f;
    }
}
)METAL";

static int ensure_context(void) {
    if (!g_kolibri_device || !g_kolibri_queue || !g_kolibri_pipeline) {
        fprintf(stderr, "[kolibri-gpu] Metal backend not initialized\n");
        return -1;
    }
    return 0;
}

extern "C" int kolibri_gpu_metal_init(const kolibri_gpu_config_t *config) {
    (void)config;

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            fprintf(stderr, "[kolibri-gpu] Metal device unavailable\n");
            return -1;
        }

        NSError *error = nil;
        NSString *source = [NSString stringWithUTF8String:kKolibriMetalSource];
        id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
        if (!library) {
            fprintf(stderr, "[kolibri-gpu] Failed to compile Metal library: %s\n",
                    error ? error.localizedDescription.UTF8String : "unknown error");
#if !__has_feature(objc_arc)
            [device release];
#endif
            return -1;
        }

        id<MTLFunction> function = [library newFunctionWithName:@"kolibri_reasonblock_embed"];
        if (!function) {
            fprintf(stderr, "[kolibri-gpu] Missing Metal function\n");
#if !__has_feature(objc_arc)
            [library release];
            [device release];
#endif
            return -1;
        }

        id<MTLComputePipelineState> pipeline = [device newComputePipelineStateWithFunction:function error:&error];
        if (!pipeline) {
            fprintf(stderr, "[kolibri-gpu] Unable to create pipeline: %s\n",
                    error ? error.localizedDescription.UTF8String : "unknown error");
#if !__has_feature(objc_arc)
            [function release];
            [library release];
            [device release];
#endif
            return -1;
        }

        id<MTLCommandQueue> queue = [device newCommandQueue];
        if (!queue) {
            fprintf(stderr, "[kolibri-gpu] Failed to create Metal command queue\n");
#if !__has_feature(objc_arc)
            [pipeline release];
            [function release];
            [library release];
            [device release];
#endif
            return -1;
        }

        g_kolibri_device = device;
        g_kolibri_queue = queue;
        g_kolibri_pipeline = pipeline;

#if !__has_feature(objc_arc)
        [function release];
        [library release];
#endif
    }

    fprintf(stderr, "[kolibri-gpu] Metal backend initialized\n");
    return 0;
}

extern "C" void kolibri_gpu_metal_shutdown(void) {
#if !__has_feature(objc_arc)
    [g_kolibri_pipeline release];
    [g_kolibri_queue release];
    [g_kolibri_device release];
#endif
    g_kolibri_pipeline = nil;
    g_kolibri_queue = nil;
    g_kolibri_device = nil;
}

static id<MTLBuffer> make_payload_buffer(const kolibri_gpu_reason_batch_t *input) {
    size_t payload_len = input->payload_len;
    size_t count = input->count;
    size_t total = payload_len * count;
    id<MTLBuffer> buffer = [g_kolibri_device newBufferWithLength:total options:MTLResourceStorageModeShared];
    uint8_t *dst = (uint8_t *)buffer.contents;
    for (size_t i = 0; i < count; ++i) {
        const uint8_t *src = input->payload + i * input->payload_stride;
        memcpy(dst + i * payload_len, src, payload_len);
    }
    return buffer;
}

extern "C" int kolibri_gpu_metal_encode(const kolibri_gpu_reason_batch_t *input,
                                         kolibri_gpu_embedding_batch_t *output) {
    if (ensure_context() != 0) {
        return -1;
    }
    if (!input || !output || !input->payload || !output->data) {
        return -1;
    }
    if (input->count == 0) {
        return 0;
    }
    if (input->payload_stride != input->payload_len) {
        fprintf(stderr, "[kolibri-gpu] Metal backend expects tightly packed payloads\n");
        return -1;
    }
    if (output->dims < 4 || output->stride != output->dims * sizeof(float)) {
        fprintf(stderr, "[kolibri-gpu] Invalid embedding layout\n");
        return -1;
    }
    if (output->count != input->count) {
        fprintf(stderr, "[kolibri-gpu] Output batch size mismatch\n");
        return -1;
    }

    @autoreleasepool {
        id<MTLBuffer> payload_buffer = make_payload_buffer(input);
        size_t embedding_bytes = output->count * output->dims * sizeof(float);
        id<MTLBuffer> embedding_buffer = [g_kolibri_device newBufferWithLength:embedding_bytes options:MTLResourceStorageModeShared];

        KolibriMetalUniforms uniforms = {
            .payload_len = (uint32_t)input->payload_len,
            .embedding_dims = (uint32_t)output->dims,
            .count = (uint32_t)input->count,
        };
        id<MTLBuffer> uniform_buffer = [g_kolibri_device newBufferWithBytes:&uniforms
                                                                       length:sizeof(uniforms)
                                                                      options:MTLResourceStorageModeShared];

        id<MTLCommandBuffer> cmd = [g_kolibri_queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder = [cmd computeCommandEncoder];
        [encoder setComputePipelineState:g_kolibri_pipeline];
        [encoder setBuffer:payload_buffer offset:0 atIndex:0];
        [encoder setBuffer:embedding_buffer offset:0 atIndex:1];
        [encoder setBuffer:uniform_buffer offset:0 atIndex:2];

        NSUInteger threadsPerGroup = MIN((NSUInteger)g_kolibri_pipeline.maxTotalThreadsPerThreadgroup, (NSUInteger)256);
        if (threadsPerGroup == 0) {
            threadsPerGroup = 64;
        }
        MTLSize tg = MTLSizeMake(threadsPerGroup, 1, 1);
        MTLSize grid = MTLSizeMake(output->count, 1, 1);
        [encoder dispatchThreads:grid threadsPerThreadgroup:tg];
        [encoder endEncoding];

        [cmd commit];
        [cmd waitUntilCompleted];

        memcpy(output->data, [embedding_buffer contents], embedding_bytes);

#if !__has_feature(objc_arc)
        [payload_buffer release];
        [embedding_buffer release];
        [uniform_buffer release];
#endif
    }

    return 0;
}

extern "C" int kolibri_gpu_metal_decode(const kolibri_gpu_embedding_batch_t *input,
                                         kolibri_gpu_reason_batch_t *output) {
    (void)input;
    (void)output;
    fprintf(stderr, "[kolibri-gpu] Metal decode not implemented yet\n");
    return -1;
}

extern "C" int kolibri_gpu_metal_embed_tokens(const uint16_t *tokens,
                                               size_t token_count,
                                               kolibri_gpu_embedding_batch_t *output) {
    (void)tokens;
    (void)token_count;
    (void)output;
    fprintf(stderr, "[kolibri-gpu] Metal token embedding not implemented yet\n");
    return -1;
}
