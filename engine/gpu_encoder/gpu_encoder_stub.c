#include "kolibri_gpu_encoder.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_stub_initialized = 0;

int kolibri_gpu_stub_init(const kolibri_gpu_config_t *config) {
    (void)config;
    g_stub_initialized = 1;
    fprintf(stderr, "[kolibri-gpu] stub backend active (CPU fallback)\n");
    return 0;
}

void kolibri_gpu_stub_shutdown(void) {
    g_stub_initialized = 0;
}

static int ensure_initialized(void) {
    if (!g_stub_initialized) {
        fprintf(stderr, "[kolibri-gpu] backend not initialized\n");
        return -1;
    }
    return 0;
}

static void compute_embedding_cpu(const uint8_t *payload,
                                  size_t payload_len,
                                  float *dst,
                                  size_t dims) {
    if (!payload_len || !dst || dims == 0) {
        return;
    }

    float sum = 0.0f;
    float energy = 0.0f;
    float maxv = 0.0f;
    float minv = 255.0f;
    float transitions = 0.0f;
    float prev = (float)payload[0];

    for (size_t i = 0; i < payload_len; ++i) {
        float v = (float)payload[i];
        sum += v;
        energy += v * v;
        if (v > maxv) maxv = v;
        if (v < minv) minv = v;
        if (i > 0 && v != prev) {
            transitions += 1.0f;
        }
        prev = v;
    }

    float len = (float)payload_len;
    float mean = sum / len;
    float variance = fmaxf(0.0f, (energy / len) - mean * mean);

    if (dims >= 1) dst[0] = mean / 255.0f;
    if (dims >= 2) dst[1] = variance / (255.0f * 255.0f);
    if (dims >= 3) dst[2] = (maxv - minv) / 255.0f;
    if (dims >= 4) dst[3] = transitions / len;
    for (size_t i = 4; i < dims; ++i) {
        dst[i] = 0.0f;
    }
}

int kolibri_gpu_stub_encode(const kolibri_gpu_reason_batch_t *input,
                            kolibri_gpu_embedding_batch_t *output) {
    if (ensure_initialized() != 0) {
        return -1;
    }
    if (!input || !output || !output->data) {
        return -1;
    }
    if (input->payload_stride != input->payload_len) {
        fprintf(stderr, "[kolibri-gpu] stub expects tightly packed payloads\n");
        return -1;
    }
    if (output->stride != output->dims * sizeof(float)) {
        fprintf(stderr, "[kolibri-gpu] invalid embedding stride\n");
        return -1;
    }

    for (size_t i = 0; i < input->count; ++i) {
        const uint8_t *payload = input->payload + i * input->payload_len;
        float *dst = output->data + i * output->dims;
        compute_embedding_cpu(payload, input->payload_len, dst, output->dims);
    }
    return 0;
}

int kolibri_gpu_stub_decode(const kolibri_gpu_embedding_batch_t *input,
                            kolibri_gpu_reason_batch_t *output) {
    if (ensure_initialized() != 0) {
        return -1;
    }
    if (!input || !output || !output->payload) {
        return -1;
    }
    size_t bytes = input->count * output->payload_len;
    memset((void *)output->payload, 0, bytes);
    (void)input;
    return 0;
}

int kolibri_gpu_stub_embed_tokens(const uint16_t *tokens,
                                  size_t token_count,
                                  kolibri_gpu_embedding_batch_t *output) {
    if (ensure_initialized() != 0) {
        return -1;
    }
    if (!tokens || !output || !output->data) {
        return -1;
    }
    if (output->stride != output->dims * sizeof(float)) {
        return -1;
    }
    float len = (float)token_count;
    for (size_t i = 0; i < token_count; ++i) {
        float *dst = output->data + i * output->dims;
        dst[0] = tokens[i] / 65535.0f;
        for (size_t d = 1; d < output->dims; ++d) {
            dst[d] = 0.0f;
        }
    }
    (void)len;
    return 0;
}
