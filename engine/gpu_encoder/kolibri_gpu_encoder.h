#ifndef KOLIBRI_GPU_ENCODER_H
#define KOLIBRI_GPU_ENCODER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    KOLIBRI_GPU_BACKEND_NONE = 0,
    KOLIBRI_GPU_BACKEND_CUDA = 1,
    KOLIBRI_GPU_BACKEND_METAL = 2
} kolibri_gpu_backend_t;

/**
 * Global configuration for the GPU encoder. The backend is selected at runtime
 * and configured once during process start. All subsequent encode/decode calls
 * assume the same backend and device.
 */
typedef struct {
    kolibri_gpu_backend_t backend;
    int device_index;
    size_t max_batch;
} kolibri_gpu_config_t;

/**
 * Descriptor of a ReasonBlock batch.
 */
typedef struct {
    const uint8_t *payload;   /* Flattened payload buffer */
    size_t payload_stride;    /* Stride between payloads */
    size_t payload_len;       /* Length of a single payload */
    size_t count;             /* Number of ReasonBlocks in the batch */
} kolibri_gpu_reason_batch_t;

/**
 * Descriptor of an embedding/output batch.
 */
typedef struct {
    float *data;
    size_t dims;
    size_t stride;
    size_t count;
} kolibri_gpu_embedding_batch_t;

/**
 * Initializes the GPU encoder. Returns 0 on success.
 */
int kolibri_gpu_encoder_init(const kolibri_gpu_config_t *config);

/**
 * Releases GPU resources.
 */
void kolibri_gpu_encoder_shutdown(void);

/**
 * Encodes ReasonBlocks into embeddings via the configured GPU backend.
 * Returns 0 on success.
 */
int kolibri_gpu_encode_reason_blocks(const kolibri_gpu_reason_batch_t *input,
                                     kolibri_gpu_embedding_batch_t *output);

/**
 * Reconstructs payloads from embeddings (KRHA residual assist).
 */
int kolibri_gpu_decode_responses(const kolibri_gpu_embedding_batch_t *input,
                                 kolibri_gpu_reason_batch_t *output);

/**
 * Performs in-place embedding of KolibriScript tokens.
 */
int kolibri_gpu_embed_tokens(const uint16_t *tokens,
                             size_t token_count,
                             kolibri_gpu_embedding_batch_t *output);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_GPU_ENCODER_H */
