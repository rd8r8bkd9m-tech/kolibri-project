/*
 * kolibri_brain.c
 *
 * Единый организм Kolibri Brain: интеграция всех модулей в единый цикл восприятия,
 * анализа, памяти и эволюции.
 */

#include "kolibri_core.h"
#include "kolibri/fractal_memory.h"
#include "kolibri/logical_memory.h"
#include "kolibri/spectral.h"
#include "kolibri/swarm.h"
#include "kolibri/generation.h"
#include "kolibri/predictive_compress.h"
#include "kolibri/web_crawler.h"
#include "kolibri/formula.h"

/* Forward declaration for internal MLP forward pass */
extern void formula_forward(const KPCFormula *f, const uint8_t *context, float *probs);

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

#define BRAIN_MAX_CONTEXT 4096
#define BRAIN_FRACTAL_SEED 1337
#define BRAIN_SWARM_AGENTS 8

/* Forward declaration */
typedef struct KolibriBrainInternal KolibriBrainInternal;

/* Конфигурация краулера для мозга */
static KwcConfig _brain_crawler_cfg = {0};

/* Callback для краулера: обучение мозга на каждой странице */
/* Forward declarations */
static int _brain_ingest_text(KolibriBrainInternal *brain, const char *text, size_t len);
static void _brain_crawl_callback(const KwcPage *page, void *userdata) {
    if (!page || !page->text || !userdata) return;
    
    KolibriBrainInternal *brain = (KolibriBrainInternal *)userdata;
    
    /* Ингест чистого текста во фрактальную память и MLP */
    _brain_ingest_text(brain, page->text, page->text_len);
}

/* Внутренний контекст мозга */
struct KolibriBrainInternal {
    /* Память и знания */
    KfmContext fractal_mem;
    LogicalMemory *logical_mem;

    /* Эволюционный рой */
    KolibriSwarm swarm;
    KolibriGenerationContext gen_ctx;
    KolibriCorpusContext corpus_ctx;

    /* Предиктивная модель (MLP) */
    KPCContext *predictor;

    /* Веб-краулер (сенсорика) */
    KwcConfig crawler_cfg;
    bool      crawler_initialized;

    uint64_t total_processed_bytes;
    uint64_t start_time;
};
typedef struct KolibriBrainInternal KolibriBrainInternal;

void* kolibri_brain_create(void) {
    KolibriBrainInternal *brain = (KolibriBrainInternal *)calloc(1, sizeof(KolibriBrainInternal));
    if (!brain) return NULL;

    /* Инициализация фрактальной памяти (десятичное дерево) */
    if (kfm_init(&brain->fractal_mem, BRAIN_FRACTAL_SEED) != 0) {
        free(brain);
        return NULL;
    }

    /* Инициализация логической памяти (символические формулы) */
    brain->logical_mem = lm_create_memory();

    /* Инициализация роя агентов */
    kolibri_swarm_init(&brain->swarm, BRAIN_SWARM_AGENTS, (uint64_t)time(NULL));

    /* Инициализация предиктивного сжатия (MLP) */
    brain->predictor = kpc_create();

    brain->start_time = (uint64_t)time(NULL);
    return (void *)brain;
}

void kolibri_brain_destroy(void *brain_ctx) {
    if (!brain_ctx) return;
    KolibriBrainInternal *brain = (KolibriBrainInternal *)brain_ctx;

    kfm_free(&brain->fractal_mem);
    lm_destroy_memory(brain->logical_mem);
    kolibri_swarm_free(&brain->swarm);
    kpc_destroy(brain->predictor);
    
    free(brain);
}

static int _brain_ingest_text(KolibriBrainInternal *brain, const char *text, size_t len) {
    if (!text || len == 0) return -1;

    /* 1. Спектральный анализ: поиск паттернов */
    double *signal = (double *)malloc(len * sizeof(double));
    for (size_t i = 0; i < len; i++) signal[i] = (double)((unsigned char)text[i]);
    
    size_t period = kolibri_find_dominant_period(signal, len);
    free(signal);

    /* 2. Запись во фрактальную память (ассоциативное десятичное дерево) */
    uint8_t path[256];
    size_t path_len = kfm_text_to_path(text, len, path, sizeof(path));
    
    if (path_len > 0) {
        kfm_insert(&brain->fractal_mem, path, path_len, text, len);
        
        /* Если найден период, создаем ассоциацию */
        if (period > 0 && period < len) {
            /* Можно создать ассоциацию между префиксом и периодом */
        }
    }

    /* 3. Обучение предиктивной модели (MLP) на этом тексте */
    if (brain->predictor) {
        kpc_train(brain->predictor, (const uint8_t *)text, len, 50); /* 50 раундов эволюции */
    }

    brain->total_processed_bytes += len;
    return 0;
}

int kolibri_brain_ingest(void *brain_ctx, const char *data, size_t size) {
    if (!brain_ctx || !data) return -1;
    return _brain_ingest_text((KolibriBrainInternal *)brain_ctx, data, size);
}

int kolibri_brain_generate(void *brain_ctx, const char *prompt, char *output, size_t max_len) {
    if (!brain_ctx || !prompt || !output) return -1;
    
    KolibriBrainInternal *brain = (KolibriBrainInternal *)brain_ctx;
    if (!brain->predictor) return -1;

    const KPCFormula *best = &brain->predictor->population[brain->predictor->best_idx];
    size_t prompt_len = strlen(prompt);
    
    /* Инициализируем контекст из промпта */
    uint8_t ctx_buf[KPC_CONTEXT_SIZE];
    memset(ctx_buf, 0, KPC_CONTEXT_SIZE);
    
    /* Заполняем контекст последними байтами промпта */
    size_t start_offset = prompt_len > KPC_CONTEXT_SIZE ? prompt_len - KPC_CONTEXT_SIZE : 0;
    for (size_t i = 0; i < KPC_CONTEXT_SIZE && (start_offset + i) < prompt_len; i++) {
        ctx_buf[i] = (uint8_t)prompt[start_offset + i];
    }

    float probs[KPC_VOCAB_SIZE];
    size_t out_pos = 0;
    
    /* Генерируем токены, пока не достигнем лимита или конца строки */
    while (out_pos < max_len - 1) {
        formula_forward(best, ctx_buf, probs);
        
        /* Сэмплирование с температурой (argmax для детерминизма) */
        int next_byte = 0;
        float max_prob = -1.0f;
        for (int i = 0; i < KPC_VOCAB_SIZE; i++) {
            if (probs[i] > max_prob) {
                max_prob = probs[i];
                next_byte = i;
            }
        }

        /* Если сгенерировали конец строки или непечатаемый символ (кроме пробела) */
        if (next_byte == '\n' || (next_byte < 32 && next_byte != ' ')) break;

        output[out_pos++] = (char)next_byte;

        /* Сдвигаем контекст */
        memmove(ctx_buf, ctx_buf + 1, KPC_CONTEXT_SIZE - 1);
        ctx_buf[KPC_CONTEXT_SIZE - 1] = (uint8_t)next_byte;
    }
    
    output[out_pos] = '\0';
    return 0;
}

int kolibri_brain_process(void *brain_ctx, KolibriBrainRequest *req, KolibriBrainStats *stats) {
    if (!brain_ctx || !req || !stats) return -1;
    
    KolibriBrainInternal *brain = (KolibriBrainInternal *)brain_ctx;
    memset(stats, 0, sizeof(KolibriBrainStats));

    switch (req->mode) {
        case BRAIN_MODE_INGEST:
            stats->status = kolibri_brain_ingest(brain_ctx, req->input_data, req->input_size);
            break;
            
        case BRAIN_MODE_GENERATE:
            stats->status = kolibri_brain_generate(brain_ctx, req->input_data, req->output_buffer, req->output_max_len);
            if (stats->status == 0) {
                req->output_written = strlen(req->output_buffer);
            }
            break;
            
        case BRAIN_MODE_ANALYZE:
            /* Спектральный анализ входных данных */
            if (req->input_data && req->input_size > 0) {
                double *sig = (double *)malloc(req->input_size * sizeof(double));
                for (size_t i = 0; i < req->input_size; i++) sig[i] = (double)((unsigned char)req->input_data[i]);
                size_t p = kolibri_find_dominant_period(sig, req->input_size);
                free(sig);
                req->fitness_score = (double)p; /* Возвращаем найденный период как результат */
            }
            break;
    }

    /* Сбор статистики организма */
    stats->memory_nodes = brain->fractal_mem.node_count;
    stats->logical_cells = brain->logical_mem->cell_count;
    
    /* Статистика роя */
    const KolibriFormula *best = kolibri_swarm_best(&brain->swarm);
    if (best) stats->global_fitness = best->fitness;
    
    /* Статистика сжатия */
    if (brain->predictor) {
        stats->avg_compression = kpc_get_ratio(brain->predictor);
    }

    return stats->status;
}
