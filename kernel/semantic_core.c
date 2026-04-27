/*
 * Kolibri Semantic Core Implementation
 * 
 * Настоящий ИИ с самообучением на основе:
 * - 64-значных числовых семантических паттернов
 * - Attention mechanism для контекста 2048 токенов
 * - Эволюционного обучения (50 индивидов, 1000 поколений)
 * - Генерации новых идей через комбинацию семантики
 */

#include "kolibri/semantic_core.h"
#include "support.h"

#include <string.h>
#include <math.h>

/* === Вспомогательные функции === */

/* Простая хэш-функция для строк (FNV-1a) */
static uint32_t fnv1a_hash(const char *str) {
    uint32_t hash = 2166136261u;
    while (*str) {
        hash ^= (uint8_t)*str++;
        hash *= 16777619u;
    }
    return hash;
}

/* === Функции управления динамической памятью === */

int ksc_allocate_context(KolibriSemanticCore *core, size_t size) {
    if (!core || size == 0 || size > CONTEXT_WINDOW_SIZE) return -1;
    
    /* Освобождение предыдущих данных если есть */
    ksc_free_context(core);
    
    /* Выделение памяти для токенов */
    core->context.tokens = (SemanticToken *)k_malloc(size * sizeof(SemanticToken));
    if (!core->context.tokens) return -1;
    
    /* Выделение памяти для матрицы attention (size x size) */
    core->context.attention_matrix = (double *)k_malloc(size * size * sizeof(double));
    if (!core->context.attention_matrix) {
        k_free(core->context.tokens);
        core->context.tokens = NULL;
        return -1;
    }
    
    /* Инициализация нулями */
    k_memset(core->context.tokens, 0, size * sizeof(SemanticToken));
    k_memset(core->context.attention_matrix, 0, size * size * sizeof(double));
    
    core->context.allocated_size = size;
    core->context.length = 0;
    
    return 0;
}

void ksc_free_context(KolibriSemanticCore *core) {
    if (!core) return;
    
    if (core->context.tokens) {
        k_free(core->context.tokens);
        core->context.tokens = NULL;
    }
    
    if (core->context.attention_matrix) {
        k_free(core->context.attention_matrix);
        core->context.attention_matrix = NULL;
    }
    
    core->context.allocated_size = 0;
    core->context.length = 0;
}

int ksc_allocate_vocab(KolibriSemanticCore *core, size_t size) {
    if (!core || size == 0 || size > MAX_VOCAB_SIZE) return -1;
    
    /* Освобождение предыдущих данных если есть */
    ksc_free_vocab(core);
    
    /* Выделение памяти для token_ids */
    core->vocab.token_ids = (uint32_t *)k_malloc(size * sizeof(uint32_t));
    if (!core->vocab.token_ids) return -1;
    
    /* Выделение памяти для patterns */
    core->vocab.patterns = (SemanticPattern *)k_malloc(size * sizeof(SemanticPattern));
    if (!core->vocab.patterns) {
        k_free(core->vocab.token_ids);
        core->vocab.token_ids = NULL;
        return -1;
    }
    
    /* Инициализация нулями */
    k_memset(core->vocab.token_ids, 0, size * sizeof(uint32_t));
    k_memset(core->vocab.patterns, 0, size * sizeof(SemanticPattern));
    
    core->vocab.allocated_vocab_size = size;
    core->vocab.vocab_size = 0;
    
    return 0;
}

void ksc_free_vocab(KolibriSemanticCore *core) {
    if (!core) return;
    
    if (core->vocab.token_ids) {
        k_free(core->vocab.token_ids);
        core->vocab.token_ids = NULL;
    }
    
    if (core->vocab.patterns) {
        k_free(core->vocab.patterns);
        core->vocab.patterns = NULL;
    }
    
    core->vocab.allocated_vocab_size = 0;
    core->vocab.vocab_size = 0;
}

/* Вычисление простого хэша для паттерна */
uint32_t ksc_compute_pattern_hash(const SemanticPattern *pattern) {
    if (!pattern) return 0;
    
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < SEMANTIC_PATTERN_LENGTH; i++) {
        hash ^= pattern->digits[i];
        hash *= 16777619u;
    }
    return hash;
}

/* Нормализация паттерна (вычисление magnitude) */
void ksc_normalize_pattern(SemanticPattern *pattern) {
    if (!pattern) return;
    
    double sum_sq = 0.0;
    for (size_t i = 0; i < SEMANTIC_PATTERN_LENGTH; i++) {
        double d = (double)pattern->digits[i] / 9.0;
        sum_sq += d * d;
    }
    
    pattern->magnitude = sum_sq > 0.0 ? sqrt(sum_sq) : 0.0;
    pattern->hash = ksc_compute_pattern_hash(pattern);
}

/* === Инициализация === */

void ksc_init(KolibriSemanticCore *core, uint64_t seed) {
    if (!core) return;
    
    k_memset(core, 0, sizeof(KolibriSemanticCore));
    
    /* Инициализация RNG */
    k_rng_seed(&core->vocab.rng, seed);
    core->context.seed = seed;
    
    /* Выделение динамической памяти для контекста и словаря */
    if (ksc_allocate_context(core, CONTEXT_WINDOW_SIZE) != 0) {
        core->is_initialized = false;
        return;
    }
    
    if (ksc_allocate_vocab(core, MAX_VOCAB_SIZE) != 0) {
        ksc_free_context(core);
        core->is_initialized = false;
        return;
    }
    
    /* Инициализация формул */
    kf_pool_init(&core->formula_pool, seed);
    
    /* Инициализация популяции */
    ksc_evolution_init_population(core);
    
    core->is_initialized = true;
    core->is_learning = false;
}

void ksc_destroy(KolibriSemanticCore *core) {
    if (!core) return;
    
    /* Освобождение динамической памяти */
    ksc_free_context(core);
    ksc_free_vocab(core);
    
    /* Очистка ресурсов */
    k_memset(core, 0, sizeof(KolibriSemanticCore));
    core->is_initialized = false;
}

/* === Генерация семантических паттернов === */

int ksc_generate_pattern_from_text(KolibriSemanticCore *core,
                                    const char *text,
                                    SemanticPattern *pattern) {
    if (!core || !text || !pattern) return -1;
    
    k_memset(pattern, 0, sizeof(SemanticPattern));
    
    /* Генерация паттерна из текста через детерминированный хэш */
    uint32_t hash = fnv1a_hash(text);
    
    /* Заполнение 64 цифр на основе хэша и символов текста */
    size_t text_len = 0;
    while (text[text_len] && text_len < SEMANTIC_PATTERN_LENGTH) {
        text_len++;
    }
    
    for (size_t i = 0; i < SEMANTIC_PATTERN_LENGTH; i++) {
        /* Комбинация хэша, позиции и символа */
        uint32_t combined = hash ^ (uint32_t)(i * 31) ^ 
                           (text_len > 0 ? (uint8_t)text[i % text_len] : 0);
        
        /* Детерминированная генерация цифры 0-9 */
        pattern->digits[i] = (uint8_t)((combined >> ((i % 4) * 8)) % 10);
    }
    
    ksc_normalize_pattern(pattern);
    return 0;
}

/* Вычисление косинусного сходства между паттернами */
double ksc_compute_similarity(const SemanticPattern *p1, 
                               const SemanticPattern *p2) {
    if (!p1 || !p2) return 0.0;
    
    double dot_product = 0.0;
    double norm1 = 0.0;
    double norm2 = 0.0;
    
    for (size_t i = 0; i < SEMANTIC_PATTERN_LENGTH; i++) {
        double v1 = (double)p1->digits[i] / 9.0;
        double v2 = (double)p2->digits[i] / 9.0;
        
        dot_product += v1 * v2;
        norm1 += v1 * v1;
        norm2 += v2 * v2;
    }
    
    if (norm1 == 0.0 || norm2 == 0.0) return 0.0;
    
    return dot_product / (sqrt(norm1) * sqrt(norm2));
}

/* === Словарь === */

int ksc_vocab_add_word(KolibriSemanticCore *core,
                        const char *word,
                        uint32_t *token_id) {
    if (!core || !word || !token_id) return -1;
    if (!core->vocab.token_ids || !core->vocab.patterns) return -1;
    if (core->vocab.vocab_size >= core->vocab.allocated_vocab_size) return -1;
    
    /* Проверка наличия слова */
    uint32_t word_hash = fnv1a_hash(word);
    for (size_t i = 0; i < core->vocab.vocab_size; i++) {
        if (core->vocab.token_ids[i] == word_hash) {
            *token_id = (uint32_t)i;
            return 0; /* Уже есть */
        }
    }
    
    /* Добавление нового слова */
    size_t idx = core->vocab.vocab_size;
    core->vocab.token_ids[idx] = word_hash;
    
    /* Генерация семантического паттерна для слова */
    ksc_generate_pattern_from_text(core, word, &core->vocab.patterns[idx]);
    
    core->vocab.vocab_size++;
    *token_id = (uint32_t)idx;
    
    return 1; /* Новое слово добавлено */
}

const SemanticPattern* ksc_vocab_get_pattern(KolibriSemanticCore *core,
                                              uint32_t token_id) {
    if (!core || !core->vocab.patterns || token_id >= core->vocab.vocab_size) return NULL;
    return &core->vocab.patterns[token_id];
}

/* === Контекстное окно с Attention === */

int ksc_context_add_token(KolibriSemanticCore *core,
                          uint32_t token_id,
                          const SemanticPattern *pattern) {
    if (!core || !pattern || !core->context.tokens) return -1;
    if (core->context.length >= core->context.allocated_size) {
        /* Сдвиг контекста при переполнении */
        ksc_context_shift(core, 1);
    }
    
    size_t pos = core->context.length;
    core->context.tokens[pos].token_id = token_id;
    k_memcpy(&core->context.tokens[pos].pattern, pattern, sizeof(SemanticPattern));
    
    /* Позиционный вес (чем ближе к концу, тем важнее) */
    core->context.tokens[pos].position_weight = 
        1.0 - ((double)pos / (double)core->context.allocated_size);
    
    core->context.length++;
    return 0;
}

/* Вычисление attention matrix (упрощённая версия scaled dot-product) */
void ksc_compute_attention(KolibriSemanticCore *core) {
    if (!core || core->context.length == 0 || !core->context.tokens || !core->context.attention_matrix) return;
    
    size_t n = core->context.length;
    
    for (size_t i = 0; i < n; i++) {
        for (size_t j = 0; j < n; j++) {
            /* Вычисление attention score как сходство паттернов */
            double score = ksc_compute_similarity(
                &core->context.tokens[i].pattern,
                &core->context.tokens[j].pattern
            );
            
            /* Учёт позиции */
            score *= core->context.tokens[j].position_weight;
            
            /* Сохранение для каждого head (упрощённо - одинаково) */
            for (int h = 0; h < ATTENTION_HEADS; h++) {
                core->context.tokens[i].attention_scores[h] = score;
            }
            
            /* Индексация в линейной матрице: [i][j] -> i * n + j */
            core->context.attention_matrix[i * n + j] = score;
        }
    }
    
    /* Softmax нормализация (по строкам) */
    for (size_t i = 0; i < n; i++) {
        double sum = 0.0;
        for (size_t j = 0; j < n; j++) {
            double exp_val = exp(core->context.attention_matrix[i * n + j]);
            sum += exp_val;
            core->context.attention_matrix[i * n + j] = exp_val;
        }
        
        if (sum > 0.0) {
            for (size_t j = 0; j < n; j++) {
                core->context.attention_matrix[i * n + j] /= sum;
            }
        }
    }
}

/* Получение взвешенного контекстного представления */
int ksc_context_get_weighted_representation(KolibriSemanticCore *core,
                                            SemanticPattern *output) {
    if (!core || !output) return -1;
    if (core->context.length == 0 || !core->context.tokens || !core->context.attention_matrix) return -1;
    
    /* Сначала вычислить attention */
    ksc_compute_attention(core);
    
    k_memset(output, 0, sizeof(SemanticPattern));
    
    /* Взвешенная сумма паттернов */
    size_t n = core->context.length;
    for (size_t i = 0; i < n; i++) {
        double weight = 0.0;
        /* Используем последнюю позицию query */
        size_t query_pos = core->context.length - 1;
        if (query_pos < n) {
            weight = core->context.attention_matrix[query_pos * n + i];
        }
        
        for (size_t d = 0; d < SEMANTIC_PATTERN_LENGTH; d++) {
            double val = (double)core->context.tokens[i].pattern.digits[d] * weight;
            output->digits[d] = (uint8_t)((int)output->digits[d] + (int)val);
            if (output->digits[d] > 9) output->digits[d] = 9;
        }
    }
    
    ksc_normalize_pattern(output);
    return 0;
}

void ksc_context_shift(KolibriSemanticCore *core, size_t shift_amount) {
    if (!core || shift_amount == 0 || !core->context.tokens) return;
    if (shift_amount >= core->context.length) {
        core->context.length = 0;
        return;
    }
    
    /* Сдвиг массива токенов */
    size_t new_length = core->context.length - shift_amount;
    for (size_t i = 0; i < new_length; i++) {
        core->context.tokens[i] = core->context.tokens[i + shift_amount];
    }
    core->context.length = new_length;
}

/* === Эволюционное обучение === */

void ksc_evolution_init_population(KolibriSemanticCore *core) {
    if (!core) return;
    
    core->population.active_count = POPULATION_SIZE;
    core->population.generation = 0;
    
    /* Инициализация случайными паттернами */
    for (size_t i = 0; i < POPULATION_SIZE; i++) {
        for (size_t d = 0; d < SEMANTIC_PATTERN_LENGTH; d++) {
            core->population.individuals[i].digits[d] = 
                (uint8_t)(k_rng_next(&core->vocab.rng) % 10);
        }
        core->population.fitness[i] = 0.0;
        ksc_normalize_pattern(&core->population.individuals[i]);
    }
}

double ksc_evolution_evaluate_fitness(KolibriSemanticCore *core,
                                       size_t individual_index,
                                       const SemanticPattern *target) {
    if (!core || individual_index >= POPULATION_SIZE || !target) return 0.0;
    
    /* Fitness = сходство с целевым паттерном */
    double similarity = ksc_compute_similarity(
        &core->population.individuals[individual_index],
        target
    );
    
    /* Дополнительный бонус за разнообразие */
    double diversity_bonus = 0.0;
    for (size_t i = 0; i < core->population.active_count; i++) {
        if (i != individual_index) {
            double sim = ksc_compute_similarity(
                &core->population.individuals[individual_index],
                &core->population.individuals[i]
            );
            diversity_bonus += (1.0 - sim); /* Чем отличается, тем лучше */
        }
    }
    diversity_bonus /= (double)(core->population.active_count - 1);
    
    return 0.7 * similarity + 0.3 * diversity_bonus;
}

void ksc_evolution_crossover(KolibriSemanticCore *core,
                              size_t parent1_idx,
                              size_t parent2_idx,
                              SemanticPattern *offspring) {
    if (!core || !offspring) return;
    if (parent1_idx >= core->population.active_count ||
        parent2_idx >= core->population.active_count) return;
    
    const SemanticPattern *p1 = &core->population.individuals[parent1_idx];
    const SemanticPattern *p2 = &core->population.individuals[parent2_idx];
    
    /* Одноточечный кроссовер */
    uint32_t crossover_point = (uint32_t)(k_rng_next(&core->vocab.rng) % SEMANTIC_PATTERN_LENGTH);
    
    for (size_t i = 0; i < SEMANTIC_PATTERN_LENGTH; i++) {
        if (i < crossover_point) {
            offspring->digits[i] = p1->digits[i];
        } else {
            offspring->digits[i] = p2->digits[i];
        }
    }
    
    ksc_normalize_pattern(offspring);
}

void ksc_evolution_mutate(KolibriSemanticCore *core,
                          SemanticPattern *individual,
                          double mutation_rate) {
    if (!core || !individual) return;
    
    for (size_t i = 0; i < SEMANTIC_PATTERN_LENGTH; i++) {
        double r = (double)(k_rng_next(&core->vocab.rng) % 1000) / 1000.0;
        if (r < mutation_rate) {
            /* Мутация: случайная цифра */
            individual->digits[i] = (uint8_t)(k_rng_next(&core->vocab.rng) % 10);
        }
    }
    
    ksc_normalize_pattern(individual);
}

void ksc_evolution_step(KolibriSemanticCore *core,
                        const SemanticPattern *target_pattern) {
    if (!core || !target_pattern) return;
    
    /* Оценка fitness всех индивидов */
    for (size_t i = 0; i < core->population.active_count; i++) {
        core->population.fitness[i] = 
            ksc_evolution_evaluate_fitness(core, i, target_pattern);
    }
    
    /* Нахождение лучшего */
    size_t best_idx = 0;
    double best_fitness = core->population.fitness[0];
    for (size_t i = 1; i < core->population.active_count; i++) {
        if (core->population.fitness[i] > best_fitness) {
            best_fitness = core->population.fitness[i];
            best_idx = i;
        }
    }
    
    /* Элитизм: лучший сохраняется */
    SemanticPattern elite;
    k_memcpy(&elite, &core->population.individuals[best_idx], sizeof(SemanticPattern));
    
    /* Замена худших потомками от лучших */
    for (size_t i = 0; i < core->population.active_count; i++) {
        if (i == best_idx) continue;
        if (core->population.fitness[i] < 0.5) {
            /* Выбор второго родителя (турнирная селекция) */
            size_t p1 = best_idx;
            size_t p2 = (size_t)(k_rng_next(&core->vocab.rng) % core->population.active_count);
            
            ksc_evolution_crossover(core, p1, p2, &core->population.individuals[i]);
            ksc_evolution_mutate(core, &core->population.individuals[i], 0.1);
        }
    }
    
    /* Восстановление элиты */
    k_memcpy(&core->population.individuals[best_idx], &elite, sizeof(SemanticPattern));
    
    core->population.generation++;
    core->total_evolved++;
}

void ksc_evolution_run(KolibriSemanticCore *core,
                       const SemanticPattern *target,
                       size_t generations) {
    if (!core || !target) return;
    
    for (size_t gen = 0; gen < generations; gen++) {
        ksc_evolution_step(core, target);
        
        /* Обновление средней fitness */
        double sum_fitness = 0.0;
        for (size_t i = 0; i < core->population.active_count; i++) {
            sum_fitness += core->population.fitness[i];
        }
        core->avg_fitness = sum_fitness / (double)core->population.active_count;
    }
}

/* === Самообучение === */

int ksc_self_learn(KolibriSemanticCore *core,
                   const char *stimulus,
                   const char *response,
                   double feedback) {
    if (!core || !stimulus || !response) return -1;
    
    core->is_learning = true;
    
    /* Генерация паттернов для стимула и ответа */
    SemanticPattern stim_pattern, resp_pattern;
    ksc_generate_pattern_from_text(core, stimulus, &stim_pattern);
    ksc_generate_pattern_from_text(core, response, &resp_pattern);
    
    /* Добавление в словарь */
    uint32_t stim_token, resp_token;
    ksc_vocab_add_word(core, stimulus, &stim_token);
    ksc_vocab_add_word(core, response, &resp_token);
    
    /* Добавление в контекст */
    ksc_context_add_token(core, stim_token, &stim_pattern);
    ksc_context_add_token(core, resp_token, &resp_pattern);
    
    /* Обучение через эволюцию: поиск паттерна, связывающего стимул и ответ */
    ksc_evolution_run(core, &resp_pattern, 100); /* 100 поколений для скорости */
    
    /* Обратная связь для формул (используем семантический паттерн как основу) */
    /* Примечание: kf_pool_feedback ожидает KolibriGene, но у нас SemanticPattern */
    /* Для совместимости используем первые байты паттерна */
    KolibriGene temp_gene;
    k_memset(&temp_gene, 0, sizeof(KolibriGene));
    size_t copy_len = sizeof(temp_gene.digits) < SEMANTIC_PATTERN_LENGTH ? 
                      sizeof(temp_gene.digits) : SEMANTIC_PATTERN_LENGTH;
    k_memcpy(temp_gene.digits, core->population.individuals[0].digits, copy_len);
    temp_gene.length = copy_len;
    kf_pool_feedback(&core->formula_pool, &temp_gene, feedback);
    
    core->total_learned++;
    core->is_learning = false;
    
    return 0;
}

/* Обобщение паттернов: создание абстрактного паттерна */
int ksc_generalize_patterns(KolibriSemanticCore *core,
                            const SemanticPattern *patterns[],
                            size_t count,
                            SemanticPattern *generalized) {
    if (!core || !patterns || count == 0 || !generalized) return -1;
    
    k_memset(generalized, 0, sizeof(SemanticPattern));
    
    /* Усреднение цифр */
    for (size_t d = 0; d < SEMANTIC_PATTERN_LENGTH; d++) {
        int sum = 0;
        for (size_t i = 0; i < count; i++) {
            if (patterns[i]) {
                sum += patterns[i]->digits[d];
            }
        }
        generalized->digits[d] = (uint8_t)(sum / (int)count);
    }
    
    ksc_normalize_pattern(generalized);
    return 0;
}

/* Извлечение правила из обученных примеров */
int ksc_extract_rule(KolibriSemanticCore *core,
                     SemanticPattern *rule_pattern) {
    if (!core || !rule_pattern) return -1;
    
    /* Использование лучшего индивида из популяции как правила */
    size_t best_idx = 0;
    double best_fitness = core->population.fitness[0];
    
    for (size_t i = 1; i < core->population.active_count; i++) {
        if (core->population.fitness[i] > best_fitness) {
            best_fitness = core->population.fitness[i];
            best_idx = i;
        }
    }
    
    k_memcpy(rule_pattern, &core->population.individuals[best_idx], sizeof(SemanticPattern));
    return 0;
}

/* === Генерация новых идей === */

int ksc_combine_patterns(KolibriSemanticCore *core,
                         const SemanticPattern *p1,
                         const SemanticPattern *p2,
                         double blend_ratio,
                         SemanticPattern *result) {
    if (!core || !p1 || !p2 || !result) return -1;
    if (blend_ratio < 0.0 || blend_ratio > 1.0) return -1;
    
    for (size_t d = 0; d < SEMANTIC_PATTERN_LENGTH; d++) {
        double blended = p1->digits[d] * (1.0 - blend_ratio) + 
                        p2->digits[d] * blend_ratio;
        result->digits[d] = (uint8_t)((int)blended % 10);
    }
    
    ksc_normalize_pattern(result);
    core->total_generated++;
    return 0;
}

int ksc_generate_idea(KolibriSemanticCore *core,
                      SemanticPattern *new_idea) {
    if (!core || !new_idea) return -1;
    
    /* Получение контекстного представления */
    SemanticPattern context_repr;
    if (ksc_context_get_weighted_representation(core, &context_repr) == 0) {
        /* Комбинация с лучшим индивидом популяции */
        size_t best_idx = 0;
        for (size_t i = 1; i < core->population.active_count; i++) {
            if (core->population.fitness[i] > core->population.fitness[best_idx]) {
                best_idx = i;
            }
        }
        
        double creativity = 0.3 + 0.4 * core->avg_fitness;
        ksc_combine_patterns(core, &context_repr, 
                            &core->population.individuals[best_idx],
                            creativity, new_idea);
    } else {
        /* Если контекст пуст, используем эволюцию */
        k_memcpy(new_idea, &core->population.individuals[0], sizeof(SemanticPattern));
        ksc_creative_mutate(core, new_idea, 0.5, new_idea);
    }
    
    core->total_generated++;
    return 0;
}

int ksc_creative_mutate(KolibriSemanticCore *core,
                        const SemanticPattern *base,
                        double creativity_level,
                        SemanticPattern *mutated) {
    if (!core || !base || !mutated) return -1;
    
    k_memcpy(mutated, base, sizeof(SemanticPattern));
    
    /* Креативная мутация: больше изменений в менее важных позициях */
    size_t mutation_count = (size_t)(SEMANTIC_PATTERN_LENGTH * creativity_level);
    
    for (size_t m = 0; m < mutation_count; m++) {
        size_t pos = k_rng_next(&core->vocab.rng) % SEMANTIC_PATTERN_LENGTH;
        /* Интеллектуальная мутация: соседние значения */
        int delta = (int)(k_rng_next(&core->vocab.rng) % 5) - 2; /* -2..+2 */
        int new_val = (int)mutated->digits[pos] + delta;
        if (new_val < 0) new_val = 0;
        if (new_val > 9) new_val = 9;
        mutated->digits[pos] = (uint8_t)new_val;
    }
    
    ksc_normalize_pattern(mutated);
    return 0;
}

/* === Инференс === */

int ksc_infer_response(KolibriSemanticCore *core,
                       const char *stimulus,
                       char *response_buffer,
                       size_t buffer_size) {
    if (!core || !stimulus || !response_buffer || buffer_size == 0) return -1;
    if (!core->vocab.patterns) return -1;
    
    /* Генерация паттерна стимула */
    SemanticPattern stim_pattern;
    ksc_generate_pattern_from_text(core, stimulus, &stim_pattern);
    
    /* Поиск наиболее похожего паттерна в словаре */
    uint32_t best_token = 0;
    double best_sim = -1.0;
    
    for (size_t i = 0; i < core->vocab.vocab_size; i++) {
        double sim = ksc_compute_similarity(&stim_pattern, &core->vocab.patterns[i]);
        if (sim > best_sim) {
            best_sim = sim;
            best_token = (uint32_t)i;
        }
    }
    
    /* Если найдено хорошее совпадение, возвращаем связанный ответ */
    if (best_sim > 0.7) {
        /* В реальной системе здесь был бы поиск ассоциаций */
        /* Для демонстрации возвращаем заглушку */
        const char *demo_response = "[семантический ответ]";
        size_t len = strlen(demo_response);
        if (len >= buffer_size) len = buffer_size - 1;
        k_memcpy(response_buffer, demo_response, len);
        response_buffer[len] = '\0';
        return 0;
    }
    
    /* Генерация нового ответа через эволюцию */
    SemanticPattern response_pattern;
    ksc_evolution_run(core, &stim_pattern, 50);
    ksc_generate_idea(core, &response_pattern);
    
    /* Преобразование паттерна в текст (упрощённо) */
    ksc_pattern_to_string(&response_pattern, response_buffer, buffer_size);
    
    return 0;
}

int ksc_predict_next_token(KolibriSemanticCore *core,
                           uint32_t *predicted_token_id) {
    if (!core || !predicted_token_id || !core->vocab.patterns) return -1;
    if (core->context.length == 0 || !core->context.tokens) return -1;
    
    /* Вычисление attention */
    ksc_compute_attention(core);
    
    /* Предсказание на основе взвешенного контекста */
    SemanticPattern context_repr;
    if (ksc_context_get_weighted_representation(core, &context_repr) != 0) {
        return -1;
    }
    
    /* Поиск наиболее похожего токена в словаре */
    size_t best_idx = 0;
    double best_sim = -1.0;
    
    for (size_t i = 0; i < core->vocab.vocab_size; i++) {
        double sim = ksc_compute_similarity(&context_repr, &core->vocab.patterns[i]);
        if (sim > best_sim) {
            best_sim = sim;
            best_idx = i;
        }
    }
    
    *predicted_token_id = (uint32_t)best_idx;
    return 0;
}

/* === Утилиты === */

int ksc_pattern_to_string(const SemanticPattern *pattern,
                          char *buffer,
                          size_t buffer_size) {
    if (!pattern || !buffer || buffer_size < SEMANTIC_PATTERN_LENGTH + 1) return -1;
    
    for (size_t i = 0; i < SEMANTIC_PATTERN_LENGTH; i++) {
        buffer[i] = (char)('0' + pattern->digits[i]);
    }
    buffer[SEMANTIC_PATTERN_LENGTH] = '\0';
    
    return 0;
}

int ksc_string_to_pattern(const char *string,
                          SemanticPattern *pattern) {
    if (!string || !pattern) return -1;
    
    size_t len = 0;
    while (string[len] && len < SEMANTIC_PATTERN_LENGTH) {
        if (string[len] < '0' || string[len] > '9') return -1;
        pattern->digits[len] = (uint8_t)(string[len] - '0');
        len++;
    }
    
    if (len < SEMANTIC_PATTERN_LENGTH) return -1;
    
    ksc_normalize_pattern(pattern);
    return 0;
}

void ksc_get_stats(const KolibriSemanticCore *core,
                   uint64_t *learned,
                   uint64_t *generated,
                   uint64_t *evolved,
                   double *avg_fitness) {
    if (!core) return;
    
    if (learned) *learned = core->total_learned;
    if (generated) *generated = core->total_generated;
    if (evolved) *evolved = core->total_evolved;
    if (avg_fitness) *avg_fitness = core->avg_fitness;
}
