#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "kolibri/decimal.h"
#include "kolibri/formula.h"
#include "kolibri/genome.h"
#include "kolibri/net.h"
#include "kolibri/corpus.h"
#include "kolibri/semantic.h"
#include "kolibri/script.h"

// Define structs from kolibri_node.c here for sizing
#define KOLIBRI_MEMORY_CAPACITY 8192U

typedef enum {
    KOLIBRI_KEY_SOURCE_DEFAULT,
    KOLIBRI_KEY_SOURCE_INLINE,
    KOLIBRI_KEY_SOURCE_PATH
} KolibriKeySource;

typedef struct {
    uint64_t seed;
    uint32_t node_id;
    bool listen_enabled;
    uint16_t listen_port;
    bool peer_enabled;
    char peer_host[64];
    uint16_t peer_port;
    bool verify_genome;
    char genome_path[260];
    char bootstrap_script[260];
    KolibriKeySource hmac_key_source;
    unsigned char hmac_key_inline[KOLIBRI_HMAC_KEY_SIZE];
    size_t hmac_key_inline_len;
    char hmac_key_path[260];
    bool health_check;
    bool auto_learn;
    uint32_t auto_evolve_ms;
    uint32_t auto_sync_ms;
    bool mass_learn;
} KolibriNodeOptions;

typedef struct {
    KolibriNodeOptions options;
    KolibriGenome genome;
    bool genome_ready;
    KolibriFormulaPool pool;
    KolibriScript script;
    bool script_ready;
    uint8_t memory_buffer[KOLIBRI_MEMORY_CAPACITY];
    k_digit_stream memory;
    bool listener_ready;
    KolibriNetListener listener;
    KolibriGene last_gene;
    bool last_gene_valid;
    int last_question;
    int last_answer;
    unsigned char hmac_key[KOLIBRI_HMAC_KEY_SIZE];
    size_t hmac_key_len;
    char hmac_key_origin[320];
    uint64_t last_evolve_ms;
    uint64_t last_sync_ms;
    KolibriCorpusContext corpus;
} KolibriNode;

int main() {
    printf("KolibriNode: %lu bytes (%.2f MB)\n", sizeof(KolibriNode), sizeof(KolibriNode)/1024.0/1024.0);
    printf("KolibriFormulaPool: %lu bytes (%.2f MB)\n", sizeof(KolibriFormulaPool), sizeof(KolibriFormulaPool)/1024.0/1024.0);
    printf("KolibriFormula: %lu bytes (%.2f KB)\n", sizeof(KolibriFormula), sizeof(KolibriFormula)/1024.0);
    return 0;
}
