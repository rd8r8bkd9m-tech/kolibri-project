#include "kolibri_gpu_encoder.h"

extern "C" int kolibri_gpu_cuda_init(const kolibri_gpu_config_t *config) {
    (void)config;
    return -1;
}

extern "C" void kolibri_gpu_cuda_shutdown(void) {}

extern "C" int kolibri_gpu_cuda_encode(const kolibri_gpu_reason_batch_t *input,
                                        kolibri_gpu_embedding_batch_t *output) {
    (void)input;
    (void)output;
    return -1;
}

extern "C" int kolibri_gpu_cuda_decode(const kolibri_gpu_embedding_batch_t *input,
                                        kolibri_gpu_reason_batch_t *output) {
    (void)input;
    (void)output;
    return -1;
}

extern "C" int kolibri_gpu_cuda_embed_tokens(const uint16_t *tokens,
                                              size_t token_count,
                                              kolibri_gpu_embedding_batch_t *output) {
    (void)tokens;
    (void)token_count;
    (void)output;
    return -1;
}
