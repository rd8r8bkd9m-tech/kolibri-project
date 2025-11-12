/*
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 * 
 * Corpus Learning Implementation
 * Реализация обучения на текстовых корпусах
 */

#include "kolibri/corpus.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

int k_corpus_init(KolibriCorpusContext *ctx,
                  size_t batch_size,
                  size_t context_size) {
    if (!ctx) return -1;
    
    memset(ctx, 0, sizeof(*ctx));
    
    ctx->batch_size = batch_size > 0 ? batch_size : KOLIBRI_CORPUS_BATCH_SIZE;
    ctx->context_window_size = context_size > 0 ? context_size : 16;
    ctx->verbose = 0;
    
    /* Инициализируем хранилище с начальной ёмкостью */
    ctx->store.capacity = 1000;
    ctx->store.patterns = (KolibriSemanticPattern *)malloc(
        ctx->store.capacity * sizeof(KolibriSemanticPattern));
    ctx->store.words = (char **)malloc(
        ctx->store.capacity * sizeof(char *));
    
    if (!ctx->store.patterns || !ctx->store.words) {
        k_corpus_free(ctx);
        return -1;
    }
    
    ctx->store.count = 0;
    
    return 0;
}

void k_corpus_free(KolibriCorpusContext *ctx) {
    if (!ctx) return;
    
    if (ctx->store.patterns) {
        free(ctx->store.patterns);
        ctx->store.patterns = NULL;
    }
    
    if (ctx->store.words) {
        for (size_t i = 0; i < ctx->store.count; i++) {
            if (ctx->store.words[i]) {
                free(ctx->store.words[i]);
            }
        }
        free(ctx->store.words);
        ctx->store.words = NULL;
    }
    
    ctx->store.count = 0;
    ctx->store.capacity = 0;
}

int k_corpus_tokenize(const char *text,
                     size_t text_len,
                     char ***tokens,
                     size_t *token_count) {
    if (!text || !tokens || !token_count) return -1;
    
    /* Выделяем начальный массив токенов */
    size_t capacity = 100;
    char **token_arr = (char **)malloc(capacity * sizeof(char *));
    if (!token_arr) return -1;
    
    size_t count = 0;
    size_t i = 0;
    
    while (i < text_len) {
        /* Пропускаем пробелы и знаки препинания */
        while (i < text_len && (isspace((unsigned char)text[i]) || 
                                ispunct((unsigned char)text[i]))) {
            i++;
        }
        
        if (i >= text_len) break;
        
        /* Находим конец слова */
        size_t start = i;
        while (i < text_len && !isspace((unsigned char)text[i]) && 
               !ispunct((unsigned char)text[i])) {
            i++;
        }
        
        size_t word_len = i - start;
        if (word_len == 0) continue;
        
        /* Расширяем массив если нужно */
        if (count >= capacity) {
            capacity *= 2;
            char **new_arr = (char **)realloc(token_arr, capacity * sizeof(char *));
            if (!new_arr) {
                k_corpus_free_tokens(token_arr, count);
                return -1;
            }
            token_arr = new_arr;
        }
        
        /* Копируем токен */
        token_arr[count] = (char *)malloc(word_len + 1);
        if (!token_arr[count]) {
            k_corpus_free_tokens(token_arr, count);
            return -1;
        }
        
        memcpy(token_arr[count], text + start, word_len);
        token_arr[count][word_len] = '\0';
        count++;
    }
    
    *tokens = token_arr;
    *token_count = count;
    
    return 0;
}

void k_corpus_free_tokens(char **tokens, size_t token_count) {
    if (!tokens) return;
    
    for (size_t i = 0; i < token_count; i++) {
        if (tokens[i]) {
            free(tokens[i]);
        }
    }
    free(tokens);
}

const KolibriSemanticPattern *k_corpus_find_pattern(const KolibriCorpusContext *ctx,
                                                   const char *word) {
    if (!ctx || !word) return NULL;
    
    for (size_t i = 0; i < ctx->store.count; i++) {
        if (ctx->store.words[i] && strcmp(ctx->store.words[i], word) == 0) {
            return &ctx->store.patterns[i];
        }
    }
    
    return NULL;
}

int k_corpus_store_pattern(KolibriCorpusContext *ctx,
                           const char *word,
                           const KolibriSemanticPattern *pattern) {
    if (!ctx || !word || !pattern) return -1;
    
    /* Проверяем, есть ли уже этот паттерн */
    for (size_t i = 0; i < ctx->store.count; i++) {
        if (ctx->store.words[i] && strcmp(ctx->store.words[i], word) == 0) {
            /* Обновляем существующий */
            ctx->store.patterns[i] = *pattern;
            return 0;
        }
    }
    
    /* Расширяем хранилище если нужно */
    if (ctx->store.count >= ctx->store.capacity) {
        size_t new_capacity = ctx->store.capacity * 2;
        
        KolibriSemanticPattern *new_patterns = (KolibriSemanticPattern *)realloc(
            ctx->store.patterns, new_capacity * sizeof(KolibriSemanticPattern));
        char **new_words = (char **)realloc(
            ctx->store.words, new_capacity * sizeof(char *));
        
        if (!new_patterns || !new_words) return -1;
        
        ctx->store.patterns = new_patterns;
        ctx->store.words = new_words;
        ctx->store.capacity = new_capacity;
    }
    
    /* Добавляем новый паттерн */
    ctx->store.patterns[ctx->store.count] = *pattern;
    ctx->store.words[ctx->store.count] = strdup(word);
    
    if (!ctx->store.words[ctx->store.count]) return -1;
    
    ctx->store.count++;
    ctx->stats.unique_patterns++;
    
    return 0;
}

int k_corpus_merge_pattern(KolibriCorpusContext *ctx,
                           const char *word,
                           const KolibriSemanticPattern *new_pattern) {
    if (!ctx || !word || !new_pattern) return -1;
    
    /* Ищем существующий паттерн */
    for (size_t i = 0; i < ctx->store.count; i++) {
        if (ctx->store.words[i] && strcmp(ctx->store.words[i], word) == 0) {
            /* Сливаем с существующим */
            KolibriSemanticPattern merged;
            if (k_semantic_merge_patterns(&ctx->store.patterns[i],
                                         new_pattern,
                                         &merged) == 0) {
                ctx->store.patterns[i] = merged;
                return 0;
            }
            return -1;
        }
    }
    
    /* Если не найден, просто добавляем */
    return k_corpus_store_pattern(ctx, word, new_pattern);
}

int k_corpus_learn_document(KolibriCorpusContext *ctx,
                            const char *text,
                            size_t text_len) {
    if (!ctx || !text || text_len == 0) return -1;
    
    clock_t start = clock();
    
    /* Токенизируем текст */
    char **tokens = NULL;
    size_t token_count = 0;
    
    if (k_corpus_tokenize(text, text_len, &tokens, &token_count) != 0) {
        return -1;
    }
    
    if (ctx->verbose) {
        printf("Tokenized %zu words\n", token_count);
    }
    
    /* Обучаем паттерны с контекстным окном */
    for (size_t i = 0; i < token_count; i++) {
        const char *word = tokens[i];
        
        /* Пропускаем очень короткие слова */
        if (strlen(word) < 2) continue;
        
        /* Создаём контекст из соседних слов */
        KolibriSemanticContext semantic_ctx;
        k_semantic_context_init(&semantic_ctx);
        
        /* Добавляем слова в окне */
        size_t window_start = i > ctx->context_window_size ? 
                             i - ctx->context_window_size : 0;
        size_t window_end = i + ctx->context_window_size < token_count ?
                           i + ctx->context_window_size : token_count;
        
        for (size_t j = window_start; j < window_end; j++) {
            if (j == i) continue; /* Пропускаем само слово */
            
            /* Вычисляем релевантность (убывает с расстоянием) */
            double distance = (double)abs((int)j - (int)i);
            double relevance = 1.0 / (1.0 + distance * 0.1);
            
            k_semantic_context_add_word(&semantic_ctx, tokens[j], relevance);
        }
        
        /* Обучаем паттерн */
        KolibriSemanticPattern pattern;
        if (k_semantic_learn(word, &semantic_ctx, 100, &pattern) == 0) {
            /* Сливаем с существующим или добавляем новый */
            k_corpus_merge_pattern(ctx, word, &pattern);
            
            ctx->stats.avg_fitness += pattern.context_weight;
        } else {
            ctx->stats.failed_patterns++;
        }
        
        k_semantic_context_free(&semantic_ctx);
        ctx->stats.total_tokens++;
    }
    
    /* Нормализуем средний fitness */
    if (ctx->stats.total_tokens > 0) {
        ctx->stats.avg_fitness /= (double)ctx->stats.total_tokens;
    }
    
    k_corpus_free_tokens(tokens, token_count);
    
    ctx->stats.total_documents++;
    ctx->stats.learning_time_sec += (double)(clock() - start) / CLOCKS_PER_SEC;
    
    return 0;
}

int k_corpus_learn_file(KolibriCorpusContext *ctx,
                        const char *filepath) {
    if (!ctx || !filepath) return -1;
    
    /* Открываем файл */
    FILE *f = fopen(filepath, "r");
    if (!f) return -1;
    
    /* Определяем размер файла */
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (file_size <= 0 || file_size > KOLIBRI_CORPUS_MAX_TEXT_SIZE) {
        fclose(f);
        return -1;
    }
    
    /* Читаем файл */
    char *text = (char *)malloc(file_size + 1);
    if (!text) {
        fclose(f);
        return -1;
    }
    
    size_t read_size = fread(text, 1, file_size, f);
    text[read_size] = '\0';
    fclose(f);
    
    if (ctx->verbose) {
        printf("Learning from file: %s (%ld bytes)\n", filepath, file_size);
    }
    
    /* Обучаем на документе */
    int result = k_corpus_learn_document(ctx, text, read_size);
    free(text);
    
    return result;
}

int k_corpus_learn_directory(KolibriCorpusContext *ctx,
                             const char *dirpath,
                             int recursive) {
    if (!ctx || !dirpath) return -1;
    
    DIR *dir = opendir(dirpath);
    if (!dir) return -1;
    
    int files_processed = 0;
    struct dirent *entry;
    
    while ((entry = readdir(dir)) != NULL) {
        /* Пропускаем . и .. */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        /* Строим полный путь */
        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dirpath, entry->d_name);
        
        struct stat st;
        if (stat(fullpath, &st) != 0) continue;
        
        if (S_ISDIR(st.st_mode)) {
            /* Рекурсивно обрабатываем поддиректории */
            if (recursive) {
                int subdir_files = k_corpus_learn_directory(ctx, fullpath, recursive);
                if (subdir_files > 0) {
                    files_processed += subdir_files;
                }
            }
        } else if (S_ISREG(st.st_mode)) {
            /* Обрабатываем текстовый файл */
            if (k_corpus_learn_file(ctx, fullpath) == 0) {
                files_processed++;
            }
        }
    }
    
    closedir(dir);
    return files_processed;
}

int k_corpus_save_patterns(const KolibriCorpusContext *ctx,
                           const char *filepath) {
    if (!ctx || !filepath) return -1;
    
    FILE *f = fopen(filepath, "wb");
    if (!f) return -1;
    
    /* Записываем количество паттернов */
    fwrite(&ctx->store.count, sizeof(size_t), 1, f);
    
    /* Записываем каждый паттерн */
    for (size_t i = 0; i < ctx->store.count; i++) {
        /* Записываем длину слова */
        size_t word_len = strlen(ctx->store.words[i]);
        fwrite(&word_len, sizeof(size_t), 1, f);
        
        /* Записываем слово */
        fwrite(ctx->store.words[i], 1, word_len, f);
        
        /* Записываем паттерн */
        fwrite(&ctx->store.patterns[i], sizeof(KolibriSemanticPattern), 1, f);
    }
    
    fclose(f);
    return 0;
}

int k_corpus_load_patterns(KolibriCorpusContext *ctx,
                           const char *filepath) {
    if (!ctx || !filepath) return -1;
    
    FILE *f = fopen(filepath, "rb");
    if (!f) return -1;
    
    /* Читаем количество паттернов */
    size_t count;
    if (fread(&count, sizeof(size_t), 1, f) != 1) {
        fclose(f);
        return -1;
    }
    
    /* Очищаем существующее хранилище */
    k_corpus_free(ctx);
    k_corpus_init(ctx, 0, 0);
    
    /* Загружаем каждый паттерн */
    for (size_t i = 0; i < count; i++) {
        /* Читаем длину слова */
        size_t word_len;
        if (fread(&word_len, sizeof(size_t), 1, f) != 1) {
            fclose(f);
            return -1;
        }
        
        /* Читаем слово */
        char word[256];
        if (word_len >= sizeof(word)) word_len = sizeof(word) - 1;
        if (fread(word, 1, word_len, f) != word_len) {
            fclose(f);
            return -1;
        }
        word[word_len] = '\0';
        
        /* Читаем паттерн */
        KolibriSemanticPattern pattern;
        if (fread(&pattern, sizeof(KolibriSemanticPattern), 1, f) != 1) {
            fclose(f);
            return -1;
        }
        
        /* Сохраняем в хранилище */
        k_corpus_store_pattern(ctx, word, &pattern);
    }
    
    fclose(f);
    return 0;
}

const KolibriCorpusStats *k_corpus_get_stats(const KolibriCorpusContext *ctx) {
    if (!ctx) return NULL;
    return &ctx->stats;
}

void k_corpus_print_stats(const KolibriCorpusStats *stats) {
    if (!stats) return;
    
    printf("\n╔════════════════════════════════════════════════════════╗\n");
    printf("║           CORPUS LEARNING STATISTICS                  ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n\n");
    
    printf("  Documents processed:  %zu\n", stats->total_documents);
    printf("  Total tokens:         %zu\n", stats->total_tokens);
    printf("  Unique patterns:      %zu\n", stats->unique_patterns);
    printf("  Failed patterns:      %zu\n", stats->failed_patterns);
    printf("  Average fitness:      %.3f\n", stats->avg_fitness);
    printf("  Learning time:        %.2f sec\n", stats->learning_time_sec);
    
    if (stats->total_tokens > 0) {
        double success_rate = 100.0 * (1.0 - (double)stats->failed_patterns / 
                                             (double)stats->total_tokens);
        printf("  Success rate:         %.1f%%\n", success_rate);
    }
    
    if (stats->learning_time_sec > 0) {
        double tokens_per_sec = (double)stats->total_tokens / stats->learning_time_sec;
        printf("  Processing speed:     %.0f tokens/sec\n", tokens_per_sec);
    }
    
    printf("\n");
}
