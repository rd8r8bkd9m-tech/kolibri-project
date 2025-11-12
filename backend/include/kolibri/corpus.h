/*
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 * 
 * Corpus Learning Module
 * Модуль обучения на текстовых корпусах
 */

#ifndef KOLIBRI_CORPUS_H
#define KOLIBRI_CORPUS_H

#include "kolibri/context.h"
#include "kolibri/semantic.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Максимальный размер батча для обучения */
#define KOLIBRI_CORPUS_BATCH_SIZE 1000

/* Максимальная длина текста для обработки */
#define KOLIBRI_CORPUS_MAX_TEXT_SIZE (1024 * 1024) /* 1MB */

/**
 * Статистика обучения на корпусе
 */
typedef struct {
    size_t total_documents;       /* Всего документов обработано */
    size_t total_tokens;          /* Всего токенов обработано */
    size_t unique_patterns;       /* Уникальных паттернов изучено */
    size_t failed_patterns;       /* Паттернов с ошибками */
    double avg_fitness;           /* Средний fitness паттернов */
    double learning_time_sec;     /* Время обучения в секундах */
} KolibriCorpusStats;

/**
 * Хранилище изученных паттернов
 */
typedef struct {
    KolibriSemanticPattern *patterns;  /* Массив паттернов */
    char **words;                      /* Соответствующие слова */
    size_t count;                      /* Количество паттернов */
    size_t capacity;                   /* Вместимость массива */
} KolibriPatternStore;

/**
 * Контекст обучения на корпусе
 */
typedef struct {
    KolibriPatternStore store;    /* Хранилище паттернов */
    KolibriCorpusStats stats;     /* Статистика */
    size_t batch_size;            /* Размер батча */
    size_t context_window_size;   /* Размер контекстного окна */
    int verbose;                  /* Уровень логирования */
} KolibriCorpusContext;

/**
 * Инициализация контекста обучения на корпусе
 * 
 * @param ctx Контекст корпуса
 * @param batch_size Размер батча (0 = по умолчанию)
 * @param context_size Размер контекстного окна (0 = по умолчанию)
 * @return 0 в случае успеха, -1 при ошибке
 */
int k_corpus_init(KolibriCorpusContext *ctx,
                  size_t batch_size,
                  size_t context_size);

/**
 * Освобождение ресурсов контекста корпуса
 * 
 * @param ctx Контекст корпуса
 */
void k_corpus_free(KolibriCorpusContext *ctx);

/**
 * Обучение на текстовом документе
 * 
 * @param ctx Контекст корпуса
 * @param text Текст документа
 * @param text_len Длина текста
 * @return 0 в случае успеха, -1 при ошибке
 */
int k_corpus_learn_document(KolibriCorpusContext *ctx,
                            const char *text,
                            size_t text_len);

/**
 * Обучение на текстовом файле
 * 
 * @param ctx Контекст корпуса
 * @param filepath Путь к файлу
 * @return 0 в случае успеха, -1 при ошибке
 */
int k_corpus_learn_file(KolibriCorpusContext *ctx,
                        const char *filepath);

/**
 * Обучение на директории с файлами
 * 
 * @param ctx Контекст корпуса
 * @param dirpath Путь к директории
 * @param recursive Рекурсивный обход
 * @return Количество обработанных файлов или -1 при ошибке
 */
int k_corpus_learn_directory(KolibriCorpusContext *ctx,
                             const char *dirpath,
                             int recursive);

/**
 * Поиск паттерна для слова в хранилище
 * 
 * @param ctx Контекст корпуса
 * @param word Слово для поиска
 * @return Указатель на паттерн или NULL если не найден
 */
const KolibriSemanticPattern *k_corpus_find_pattern(const KolibriCorpusContext *ctx,
                                                   const char *word);

/**
 * Добавление или обновление паттерна в хранилище
 * 
 * @param ctx Контекст корпуса
 * @param word Слово
 * @param pattern Паттерн
 * @return 0 в случае успеха, -1 при ошибке
 */
int k_corpus_store_pattern(KolibriCorpusContext *ctx,
                           const char *word,
                           const KolibriSemanticPattern *pattern);

/**
 * Слияние паттерна с существующим (для инкрементального обучения)
 * 
 * @param ctx Контекст корпуса
 * @param word Слово
 * @param new_pattern Новый паттерн для слияния
 * @return 0 в случае успеха, -1 при ошибке
 */
int k_corpus_merge_pattern(KolibriCorpusContext *ctx,
                           const char *word,
                           const KolibriSemanticPattern *new_pattern);

/**
 * Сохранение изученных паттернов в файл
 * 
 * @param ctx Контекст корпуса
 * @param filepath Путь к файлу
 * @return 0 в случае успеха, -1 при ошибке
 */
int k_corpus_save_patterns(const KolibriCorpusContext *ctx,
                           const char *filepath);

/**
 * Загрузка изученных паттернов из файла
 * 
 * @param ctx Контекст корпуса
 * @param filepath Путь к файлу
 * @return 0 в случае успеха, -1 при ошибке
 */
int k_corpus_load_patterns(KolibriCorpusContext *ctx,
                           const char *filepath);

/**
 * Получение статистики обучения
 * 
 * @param ctx Контекст корпуса
 * @return Указатель на статистику
 */
const KolibriCorpusStats *k_corpus_get_stats(const KolibriCorpusContext *ctx);

/**
 * Вывод статистики обучения
 * 
 * @param stats Статистика
 */
void k_corpus_print_stats(const KolibriCorpusStats *stats);

/**
 * Токенизация текста на слова
 * Внутренняя утилита для разбиения текста
 * 
 * @param text Текст
 * @param text_len Длина текста
 * @param tokens Массив для токенов (выделяется внутри)
 * @param token_count Количество токенов (выход)
 * @return 0 в случае успеха, -1 при ошибке
 */
int k_corpus_tokenize(const char *text,
                     size_t text_len,
                     char ***tokens,
                     size_t *token_count);

/**
 * Освобождение токенов
 * 
 * @param tokens Массив токенов
 * @param token_count Количество токенов
 */
void k_corpus_free_tokens(char **tokens, size_t token_count);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_CORPUS_H */
