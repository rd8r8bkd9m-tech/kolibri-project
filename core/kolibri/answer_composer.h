#ifndef KOLIBRI_ANSWER_COMPOSER_H
#define KOLIBRI_ANSWER_COMPOSER_H

#include "kolibri/formula.h"
#include <stddef.h>
#include <stdint.h>

/* Максимальное количество фрагментов (ассоциаций), из которых может состоять ответ */
#define KAC_MAX_FRAGMENTS 16

/* Максимальная длина финального скомпонованного ответа */
#define KAC_MAX_ANSWER_LEN 4096

/* Конфигурация для композитора */
typedef struct {
    double relevance_threshold; /* Минимальный порог релевантности фрагмента */
    int allow_duplicates;       /* Разрешить дубликаты фактов? */
    // Другие параметры стилизации и логики в будущем
} KolibriAnswerComposerConfig;

/* Структура для одного фрагмента, передаваемого композитору */
typedef struct {
    const KolibriAssociation *association; /* Ссылка на исходную ассоциацию */
    double relevance_score;                /* Оценка релевантности к основному запросу */
    int used_in_composition;               /* Флаг, использован ли фрагмент */
} KolibriCompositionFragment;

/* Главный объект-композитор */
typedef struct {
    KolibriAnswerComposerConfig config;
    KolibriCompositionFragment fragments[KAC_MAX_FRAGMENTS];
    size_t fragment_count;
    char composed_answer[KAC_MAX_ANSWER_LEN];
} KolibriAnswerComposer;


/* --- Основные функции API --- */

/**
 * @brief Инициализирует композитор с конфигурацией по умолчанию.
 * @param composer Указатель на объект KolibriAnswerComposer.
 */
void kac_init(KolibriAnswerComposer *composer);

/**
 * @brief Устанавливает кастомную конфигурацию для композитора.
 * @param composer Указатель на объект KolibriAnswerComposer.
 * @param config Указатель на объект KolibriAnswerComposerConfig.
 */
void kac_set_config(KolibriAnswerComposer *composer, const KolibriAnswerComposerConfig *config);

/**
 * @brief Добавляет фрагмент (ассоциацию) для последующей композиции.
 * @param composer Указатель на объект KolibriAnswerComposer.
 * @param association Указатель на ассоциацию-факт.
 * @param relevance_score Оценка релевантности этого факта к основному запросу.
 * @return 0 в случае успеха, -1 если массив фрагментов полон.
 */
int kac_add_fragment(KolibriAnswerComposer *composer, const KolibriAssociation *association, double relevance_score);

/**
 * @brief Выполняет основную логику: анализирует фрагменты и собирает из них связный ответ.
 * @param composer Указатель на объект KolibriAnswerComposer.
 * @param main_query Исходный запрос пользователя, который задает тему.
 * @return 0 в случае успеха, -1 в случае ошибки.
 */
int kac_compose(KolibriAnswerComposer *composer, const char *main_query);

/**
 * @brief Возвращает указатель на скомпонованный ответ.
 * @param composer Указатель на объект KolibriAnswerComposer.
 * @return Константная строка с финальным ответом.
 */
const char* kac_get_answer(const KolibriAnswerComposer *composer);

/**
 * @brief Очищает композитор для нового запроса.
 * @param composer Указатель на объект KolibriAnswerComposer.
 */
void kac_reset(KolibriAnswerComposer *composer);

#endif /* KOLIBRI_ANSWER_COMPOSER_H */
