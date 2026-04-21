#include <stddef.h>
#include <stdint.h>

void* kwm_create(uint64_t seed) { (void)seed; return NULL; }
void kwm_destroy(void* wm) { (void)wm; }
float kwm_observe_block(void *ctx, const uint8_t *data, size_t len) { (void)ctx; (void)data; (void)len; return 0.0f; }
int kwm_predict(void *ctx, void *pred) { (void)ctx; (void)pred; return 0; }
size_t kwm_generate(void *ctx, uint8_t *output, size_t max_len, float temperature) { (void)ctx; (void)temperature; if(output && max_len > 0) output[0]=0; return 0; }
size_t kwm_extract_concepts(void *ctx, const char *text, size_t len, void *concepts, size_t max_concepts) { (void)ctx; (void)text; (void)len; (void)concepts; (void)max_concepts; return 0; }
