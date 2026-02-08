/*
 * Kolibri Ingest - Knowledge Regression Engine
 * Реализует принцип "True AI": Сжатие данных через поиск порождающей формулы (Регрессия).
 * Update: Supports Token-based regression (Embeddings) and Hyper-Evolution.
 */

#include "kolibri/formula.h"
#include "kolibri/genome.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>

#define BUFFER_SIZE 512 * 1024 
#define MAX_TOKENS 2048

static const unsigned char INGEST_KEY[] = "kolibri-secret-key";

// Simple tokenizer: Splits text into integer hashes (Vector Approximation)
int tokenize(char *text, int *tokens, size_t max_tokens) {
    size_t count = 0;
    char *ptr = text;
    while (*ptr && count < max_tokens) {
        // Skip whitespace
        while (*ptr && isspace((unsigned char)*ptr)) ptr++;
        if (!*ptr) break;
        
        // Find word end
        char *start = ptr;
        while (*ptr && !isspace((unsigned char)*ptr)) ptr++;
        
        // Hash the word (Simple DJB2/FNV mix)
        unsigned long hash = 5381;
        for (char *c = start; c < ptr; c++) {
            hash = ((hash << 5) + hash) + *c; /* hash * 33 + c */
        }
        
        // Map to 0-1000 range for regression stability
        tokens[count++] = (int)(hash % 1000); // Token ID
    }
    return count;
}

void process_block_regression(char *url, char *data) {
    size_t data_len = strlen(data);
    int tokens[MAX_TOKENS];
    int token_count = tokenize(data, tokens, MAX_TOKENS);
    
    // Safety clamp
    if (token_count == 0 || token_count > 64) token_count = (token_count > 64) ? 64 : token_count;
    
    printf("\n[Ingest] === Запуск Регрессии (Neural Embeddings) для: %s ===\n", url);
    printf("[Ingest] Вход: %zu байт -> %d токенов (Vectors). Цель: 1,000,000 поколений.\n", data_len, token_count);

    KolibriFormulaPool *pool = malloc(sizeof(KolibriFormulaPool));
    if (!pool) {
        printf("[Error] Out of memory.\n");
        return;
    }
    kf_pool_init(pool, 0xDEADC0DE); 

    // Load Token Embeddings as X->Y examples
    // f(index) -> token_id
    for (int i = 0; i < token_count; i++) {
        kf_pool_add_example(pool, i, tokens[i]);
    }

    printf("[DeepThink] Запуск гипер-эволюции (Adaptive epochs)...\n");
    
    // Hyper-Evolution Loop
    // We run based on env var, allowing fast/deep modes
    const char* gens_env = getenv("KOLIBRI_GENS");
    int MAX_GENS = gens_env ? atoi(gens_env) : 1000000;
    if (MAX_GENS <= 0) MAX_GENS = 5000;
    
    printf("[Ingest] Configured Max Generations: %d\n", MAX_GENS);

    const int BATCH_SIZE = 10;
    
    for(int g=0; g<MAX_GENS; g+=BATCH_SIZE) {
        kf_pool_tick(pool, BATCH_SIZE);
        const KolibriFormula *b = kf_pool_best(pool);
        
        if (g % 100 == 0 || g == 0) {
           printf("   Epoch %d/%d: Loss %.6f [Primitive signatures: %d]\r", 
                  g, MAX_GENS, (b ? 1.0 - b->fitness : 1.0), (b && b->gene.length > 0 ? b->gene.digits[0] : 0));
           fflush(stdout);
        }
        
        // Early stopping for speed on mass scale
        if (b && b->fitness > 0.99) {
            printf("   [Converged] Loss < 0.01 at epoch %d. Stopping.\n", g);
            break;
        }
    }

    const KolibriFormula *best = kf_pool_best(pool);

    const char *min_fit_env = getenv("KOLIBRI_MIN_FITNESS");
    double min_fitness = min_fit_env ? atof(min_fit_env) : 0.1;
    if (min_fitness < 0.0) min_fitness = 0.0;
    if (min_fitness > 0.99) min_fitness = 0.99;
    
    if (best && best->fitness > min_fitness) {
        size_t gene_size = best->gene.length > 0 ? best->gene.length : 32; 
        
        printf("[Success] Семантическая формула найдена!\n");
        printf("   Knowledge Gene Size: %zu bytes\n", gene_size);

        // Verify matches (Prediction vs Reality)
        printf("[Verify] Предсказанные векторы (Tokens):\n   [");
        int matches = 0;
        for(int i=0; i<token_count && i < 20; i++) {
            int val;
            kf_formula_apply(best, i, &val);
            printf("%d", val);
            if (val == tokens[i]) matches++;
            if (i < token_count - 1) printf(" ");
        }
        printf("...]\n");
        printf("   [Accuracy]: %.1f%% on semantic tokens\n", (double)matches*100.0/token_count);
        
        // --- СОХРАНЕНИЕ В ГЕНОМ ---
        // Support Sharding for Swarm Mode
        const char *genome_path = getenv("KOLIBRI_GENOME_PATH");
        if (!genome_path) genome_path = "genome.dat";
        
        KolibriGenome ctx;
        if (kg_open(&ctx, genome_path, INGEST_KEY, strlen((char*)INGEST_KEY)) == 0) {
            
            char raw_payload[256];
            int written = snprintf(raw_payload, sizeof(raw_payload), "U:%s|G:", url);
            
            size_t max_gene_chars = 40; 
            for (size_t i=0; i<gene_size && i<max_gene_chars; i++) {
                if (written >= sizeof(raw_payload)-1) break;
                written += snprintf(raw_payload+written, sizeof(raw_payload)-written, "%d", best->gene.digits[i]);
            }
            
            char numeric_payload[KOLIBRI_PAYLOAD_SIZE];
            if (kg_encode_payload(raw_payload, numeric_payload, sizeof(numeric_payload)) == 0) {
                 if (kg_append(&ctx, "DEEP_L", numeric_payload, NULL) == 0) {
                     printf("[Persist] Знание (векторы) сохранено.\n");
                 }
            }
            kg_close(&ctx);
        }
    } else {
        printf("[Failure] Формула не сошлась.\n");
    }
    
    free(pool);
}

int main(int argc, char **argv) {
    char line[BUFFER_SIZE];
    char current_url[1024];
    char *content_buffer = malloc(BUFFER_SIZE); 
    if (!content_buffer) return 1;
    content_buffer[0] = '\0';
    
    setvbuf(stdout, NULL, _IONBF, 0);
    
    printf("Kolibri Ingest: Online. Mode: Hyper-Regression (Tokens).\n");

    while (fgets(line, sizeof(line), stdin)) {
        size_t ln = strlen(line);
        if (ln > 0 && line[ln-1] == '\n') line[ln-1] = '\0';

        if (strncmp(line, "URL:", 4) == 0) {
            strncpy(current_url, line + 4, sizeof(current_url)-1);
            current_url[sizeof(current_url)-1] = '\0';
            content_buffer[0] = '\0'; 
        } else if (strncmp(line, "DATA:", 5) == 0) {
            strncat(content_buffer, line + 5, BUFFER_SIZE - strlen(content_buffer) - 1);
        } else if (strncmp(line, "END_DATA", 8) == 0) {
            if (strlen(content_buffer) > 0) {
                process_block_regression(current_url, content_buffer);
                content_buffer[0] = '\0';
            }
        } else {
             if (strlen(content_buffer) + ln < BUFFER_SIZE - 1) {
                strncat(content_buffer, " ", BUFFER_SIZE - strlen(content_buffer) - 1); // Replace newlines with space for tokenization
                strncat(content_buffer, line, BUFFER_SIZE - strlen(content_buffer) - 1);
            }
        }
    }

    free(content_buffer);
    return 0;
}
