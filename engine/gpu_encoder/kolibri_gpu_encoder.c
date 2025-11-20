#include "kolibri_gpu_encoder.h"

#include <stdio.h>

static kolibri_gpu_config_t g_active_cfg = {
    .backend = KOLIBRI_GPU_BACKEND_NONE,
    .device_index = -1,
    .max_batch = 0,
};

struct kolibri_gpu_backend_ops {
    const char *name;
    int (*init)(const kolibri_gpu_config_t *config);
    void (*shutdown)(void);
    int (*encode)(const kolibri_gpu_reason_batch_t *input,
                  kolibri_gpu_embedding_batch_t *output);
    int (*decode)(const kolibri_gpu_embedding_batch_t *input,
                  kolibri_gpu_reason_batch_t *output);
    int (*embed_tokens)(const uint16_t *tokens,
                        size_t token_count,
                        kolibri_gpu_embedding_batch_t *output);
};

/* Stub backend */
int kolibri_gpu_stub_init(const kolibri_gpu_config_t *config);
void kolibri_gpu_stub_shutdown(void);
int kolibri_gpu_stub_encode(const kolibri_gpu_reason_batch_t *input,
                            kolibri_gpu_embedding_batch_t *output);
int kolibri_gpu_stub_decode(const kolibri_gpu_embedding_batch_t *input,
                            kolibri_gpu_reason_batch_t *output);
int kolibri_gpu_stub_embed_tokens(const uint16_t *tokens,
                                  size_t token_count,
                                  kolibri_gpu_embedding_batch_t *output);

static const struct kolibri_gpu_backend_ops k_stub_ops = {
    .name = "stub",
    .init = kolibri_gpu_stub_init,
    .shutdown = kolibri_gpu_stub_shutdown,
    .encode = kolibri_gpu_stub_encode,
    .decode = kolibri_gpu_stub_decode,
    .embed_tokens = kolibri_gpu_stub_embed_tokens,
};

/* CUDA backend (not yet implemented) */
int kolibri_gpu_cuda_init(const kolibri_gpu_config_t *config);
void kolibri_gpu_cuda_shutdown(void);
int kolibri_gpu_cuda_encode(const kolibri_gpu_reason_batch_t *input,
                            kolibri_gpu_embedding_batch_t *output);
int kolibri_gpu_cuda_decode(const kolibri_gpu_embedding_batch_t *input,
                            kolibri_gpu_reason_batch_t *output);
int kolibri_gpu_cuda_embed_tokens(const uint16_t *tokens,
                                  size_t token_count,
                                  kolibri_gpu_embedding_batch_t *output);

static const struct kolibri_gpu_backend_ops k_cuda_ops = {
    .name = "cuda",
    .init = kolibri_gpu_cuda_init,
    .shutdown = kolibri_gpu_cuda_shutdown,
    .encode = kolibri_gpu_cuda_encode,
    .decode = kolibri_gpu_cuda_decode,
    .embed_tokens = kolibri_gpu_cuda_embed_tokens,
};

#ifdef __APPLE__
int kolibri_gpu_metal_init(const kolibri_gpu_config_t *config);
void kolibri_gpu_metal_shutdown(void);
int kolibri_gpu_metal_encode(const kolibri_gpu_reason_batch_t *input,
                             kolibri_gpu_embedding_batch_t *output);
int kolibri_gpu_metal_decode(const kolibri_gpu_embedding_batch_t *input,
                             kolibri_gpu_reason_batch_t *output);
int kolibri_gpu_metal_embed_tokens(const uint16_t *tokens,
                                   size_t token_count,
                                   kolibri_gpu_embedding_batch_t *output);

static const struct kolibri_gpu_backend_ops k_metal_ops = {
    .name = "metal",
    .init = kolibri_gpu_metal_init,
    .shutdown = kolibri_gpu_metal_shutdown,
    .encode = kolibri_gpu_metal_encode,
    .decode = kolibri_gpu_metal_decode,
    .embed_tokens = kolibri_gpu_metal_embed_tokens,
};
#endif

static const struct kolibri_gpu_backend_ops *g_ops = NULL;

static void select_backend(const struct kolibri_gpu_backend_ops *ops) {
    g_ops = ops;
}

int kolibri_gpu_encoder_init(const kolibri_gpu_config_t *config) {
    if (!config) {
        return -1;
    }
    g_active_cfg = *config;

    const struct kolibri_gpu_backend_ops *target = &k_stub_ops;
    switch (config->backend) {
        case KOLIBRI_GPU_BACKEND_CUDA:
            target = &k_cuda_ops;
            break;
        case KOLIBRI_GPU_BACKEND_METAL:
#ifdef __APPLE__
            target = &k_metal_ops;
#else
            fprintf(stderr, "[kolibri-gpu] Metal backend requested but unavailable on this platform\n");
#endif
            break;
        case KOLIBRI_GPU_BACKEND_NONE:
        default:
            target = &k_stub_ops;
            break;
    }

    if (target->init && target->init(config) == 0) {
        select_backend(target);
        return 0;
    }

    fprintf(stderr, "[kolibri-gpu] Failed to initialize backend '%s', falling back to stub\n",
            target->name);
    k_stub_ops.init(config);
    select_backend(&k_stub_ops);
    return 0;
}

void kolibri_gpu_encoder_shutdown(void) {
    if (g_ops && g_ops->shutdown) {
        g_ops->shutdown();
    }
    g_ops = NULL;
    g_active_cfg.backend = KOLIBRI_GPU_BACKEND_NONE;
}

static int ensure_ops(void) {
    if (!g_ops) {
        fprintf(stderr, "[kolibri-gpu] backend not initialized\n");
        return -1;
    }
    return 0;
}

int kolibri_gpu_encode_reason_blocks(const kolibri_gpu_reason_batch_t *input,
                                     kolibri_gpu_embedding_batch_t *output) {
    if (ensure_ops() != 0) {
        return -1;
    }
    return g_ops->encode(input, output);
}

int kolibri_gpu_decode_responses(const kolibri_gpu_embedding_batch_t *input,
                                 kolibri_gpu_reason_batch_t *output) {
    if (ensure_ops() != 0) {
        return -1;
    }
    return g_ops->decode(input, output);
}

int kolibri_gpu_embed_tokens(const uint16_t *tokens,
                             size_t token_count,
                             kolibri_gpu_embedding_batch_t *output) {
    if (ensure_ops() != 0) {
        return -1;
    }
    return g_ops->embed_tokens(tokens, token_count, output);
}
