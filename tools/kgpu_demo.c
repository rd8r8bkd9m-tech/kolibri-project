#include "engine/gpu_encoder/kolibri_gpu_encoder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_file(const char *path, unsigned char **buffer, size_t *size) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        perror("fopen");
        return -1;
    }
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (len <= 0) {
        fclose(fp);
        return -1;
    }
    unsigned char *data = (unsigned char *)malloc((size_t)len);
    if (!data) {
        fclose(fp);
        return -1;
    }
    if (fread(data, 1, (size_t)len, fp) != (size_t)len) {
        perror("fread");
        free(data);
        fclose(fp);
        return -1;
    }
    fclose(fp);
    *buffer = data;
    *size = (size_t)len;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file> [dims]\n", argv[0]);
        return 1;
    }

    unsigned char *payload = NULL;
    size_t payload_len = 0;
    if (read_file(argv[1], &payload, &payload_len) != 0) {
        fprintf(stderr, "Failed to read %s\n", argv[1]);
        return 1;
    }

    size_t dims = 8;
    if (argc >= 3) {
        dims = (size_t)strtoul(argv[2], NULL, 10);
        if (dims == 0) {
            dims = 8;
        }
    }

    kolibri_gpu_config_t cfg = {
#ifdef __APPLE__
        .backend = KOLIBRI_GPU_BACKEND_METAL,
#else
        .backend = KOLIBRI_GPU_BACKEND_NONE,
#endif
        .device_index = 0,
        .max_batch = 64,
    };

    if (kolibri_gpu_encoder_init(&cfg) != 0) {
        fprintf(stderr, "Failed to initialize Kolibri GPU backend\n");
        free(payload);
        return 1;
    }

    kolibri_gpu_reason_batch_t batch = {
        .payload = payload,
        .payload_stride = payload_len,
        .payload_len = payload_len,
        .count = 1,
    };

    float *embedding = (float *)calloc(dims, sizeof(float));
    if (!embedding) {
        free(payload);
        kolibri_gpu_encoder_shutdown();
        return 1;
    }

    kolibri_gpu_embedding_batch_t out = {
        .data = embedding,
        .dims = dims,
        .stride = dims * sizeof(float),
        .count = 1,
    };

    if (kolibri_gpu_encode_reason_blocks(&batch, &out) != 0) {
        fprintf(stderr, "Embedding failed\n");
        free(embedding);
        free(payload);
        kolibri_gpu_encoder_shutdown();
        return 1;
    }

    printf("Embedding for %s (len=%zu):\n", argv[1], payload_len);
    for (size_t i = 0; i < dims; ++i) {
        printf("  dim[%zu] = %.6f\n", i, embedding[i]);
    }

    free(embedding);
    free(payload);
    kolibri_gpu_encoder_shutdown();
    return 0;
}
