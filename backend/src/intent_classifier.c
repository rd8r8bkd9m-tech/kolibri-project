/*
 * intent_classifier.c
 *
 * Классификатор намерений для Kolibri
 * Определяет тип запроса и выбирает стратегию обработки
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/intent_classifier.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
 * ============================================================================ */

static double ic_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e6 + (double)ts.tv_nsec / 1e3;
}

static void to_lowercase(char *dest, const char *src, size_t max_len) {
    size_t src_len = strlen(src);
    size_t si = 0, di = 0;

    while (si < src_len && di < max_len - 1) {
        unsigned char c = (unsigned char)src[si];

        /* Latin uppercase A-Z */
        if (c >= 0x41 && c <= 0x5A) {
            dest[di++] = c + 0x20;
            si++;
        }
        /* Cyrillic UTF-8 D0 xx */
        else if (c == 0xD0 && si + 1 < src_len) {
            unsigned char c2 = (unsigned char)src[si + 1];
            if (c2 == 0x81) {
                /* Ё (D0 81) → ё (D1 91) */
                if (di + 2 < max_len) {
                    dest[di++] = 0xD1;
                    dest[di++] = 0x91;
                    si += 2;
                } else
                    break;
            } else if (c2 >= 0x90 && c2 <= 0x9F) {
                /* А-П (D0 90..9F) → а-п (D0 B0..BF) */
                if (di + 2 < max_len) {
                    dest[di++] = 0xD0;
                    dest[di++] = c2 + 0x20;
                    si += 2;
                } else
                    break;
            } else if (c2 >= 0xA0 && c2 <= 0xAF) {
                /* Р-Я (D0 A0..AF) → р-я (D1 80..8F) */
                if (di + 2 < max_len) {
                    dest[di++] = 0xD1;
                    dest[di++] = c2 - 0x20;
                    si += 2;
                } else
                    break;
            } else {
                dest[di++] = c;
                si++;
            }
        }
        /* Already lowercase or other UTF-8 */
        else {
            dest[di++] = c;
            si++;
        }
    }
    dest[di] = '\0';
}

static int contains_keyword(const char *text, const char *keyword) { return strstr(text, keyword) != NULL; }

static int count_keyword_matches(const char *text, const char **keywords, int num_keywords) {
    int matches = 0;
    char lower_text[1024];
    to_lowercase(lower_text, text, sizeof(lower_text));

    for (int i = 0; i < num_keywords; i++) {
        if (contains_keyword(lower_text, keywords[i])) {
            matches++;
        }
    }
    return matches;
}

/* ============================================================================
 * ПАТТЕРНЫ ПО УМОЛЧАНИЮ
 * ============================================================================ */

static const char *fact_keywords[] = {"сколько", "когда", "где", "кто", "что", "какой", "какая", "какие"};
static const char *def_keywords[] = {"что такое", "что значит", "определение", "это", "является"};
static const char *compare_keywords[] = {"сравни", "отличие", "разница", "чем отличается", "vs", "versus"};
static const char *cause_keywords[] = {"почему", "зачем", "причина", "по причине", "из-за"};
static const char *process_keywords[] = {"как работает", "как функционирует", "принцип", "механизм"};
static const char *logic_keywords[] = {"задача", "реши", "реши задачу", "логическая", "докажи"};
static const char *math_keywords[] = {"посчитай", "вычисли", "реши", "уравнение", "формула", "сколько будет"};
static const char *explain_keywords[] = {"объясни", "расскажи", "как", "почему", "разъясни"};
static const char *example_keywords[] = {"пример", "покажи", "приведи пример", "например"};
static const char *whatif_keywords[] = {"что если", "что будет если", "представь что", "допустим"};
static const char *teach_keywords[] = {"научи", "запомни", "выучи", "добавь знание", "запиши"};
static const char *greet_keywords[] = {"привет", "здравствуй", "добрый", "hello", "hi", "hey"};
static const char *farewell_keywords[] = {"пока", "до свидания", "прощай", "bye", "goodbye"};
static const char *thanks_keywords[] = {"спасибо", "благодарю", "thanks", "thank you"};

/* ============================================================================
 * ИНИЦИАЛИЗАЦИЯ
 * ============================================================================ */

int kolibri_ic_init(KolibriIntentClassifier *classifier) {
    if (!classifier)
        return -1;

    memset(classifier, 0, sizeof(*classifier));

    /* Добавляем паттерны */
    kolibri_ic_add_pattern(classifier, KIC_INTENT_QUERY_FACT, fact_keywords,
                           sizeof(fact_keywords) / sizeof(fact_keywords[0]), 0.7, "general");

    kolibri_ic_add_pattern(classifier, KIC_INTENT_QUERY_DEFINITION, def_keywords,
                           sizeof(def_keywords) / sizeof(def_keywords[0]), 0.8, "general");

    kolibri_ic_add_pattern(classifier, KIC_INTENT_QUERY_COMPARISON, compare_keywords,
                           sizeof(compare_keywords) / sizeof(compare_keywords[0]), 0.85, "general");

    kolibri_ic_add_pattern(classifier, KIC_INTENT_QUERY_CAUSE, cause_keywords,
                           sizeof(cause_keywords) / sizeof(cause_keywords[0]), 0.8, "general");

    kolibri_ic_add_pattern(classifier, KIC_INTENT_QUERY_PROCESS, process_keywords,
                           sizeof(process_keywords) / sizeof(process_keywords[0]), 0.8, "science");

    kolibri_ic_add_pattern(classifier, KIC_INTENT_LOGIC_PUZZLE, logic_keywords,
                           sizeof(logic_keywords) / sizeof(logic_keywords[0]), 0.75, "logic");

    kolibri_ic_add_pattern(classifier, KIC_INTENT_MATH_PROBLEM, math_keywords,
                           sizeof(math_keywords) / sizeof(math_keywords[0]), 0.8, "math");

    kolibri_ic_add_pattern(classifier, KIC_INTENT_EXPLAIN, explain_keywords,
                           sizeof(explain_keywords) / sizeof(explain_keywords[0]), 0.7, "general");

    kolibri_ic_add_pattern(classifier, KIC_INTENT_EXAMPLE, example_keywords,
                           sizeof(example_keywords) / sizeof(example_keywords[0]), 0.75, "general");

    kolibri_ic_add_pattern(classifier, KIC_INTENT_COUNTERFACTUAL, whatif_keywords,
                           sizeof(whatif_keywords) / sizeof(whatif_keywords[0]), 0.9, "counterfactual");

    kolibri_ic_add_pattern(classifier, KIC_INTENT_TEACH, teach_keywords,
                           sizeof(teach_keywords) / sizeof(teach_keywords[0]), 0.85, "learning");

    kolibri_ic_add_pattern(classifier, KIC_INTENT_GREETING, greet_keywords,
                           sizeof(greet_keywords) / sizeof(greet_keywords[0]), 0.9, "conversation");

    kolibri_ic_add_pattern(classifier, KIC_INTENT_FAREWELL, farewell_keywords,
                           sizeof(farewell_keywords) / sizeof(farewell_keywords[0]), 0.9, "conversation");

    kolibri_ic_add_pattern(classifier, KIC_INTENT_THANKS, thanks_keywords,
                           sizeof(thanks_keywords) / sizeof(thanks_keywords[0]), 0.9, "conversation");

    return 0;
}

void kolibri_ic_destroy(KolibriIntentClassifier *classifier) {
    if (!classifier)
        return;
    /* Паттерны используют статические строки, освобождать не нужно */
}

/* ============================================================================
 * КЛАССИФИКАЦИЯ
 * ============================================================================ */

int kolibri_ic_classify(KolibriIntentClassifier *classifier, const char *query, KolibriIntentResult *result) {
    if (!classifier || !query || !result)
        return -1;

    double start = ic_time_us();

    memset(result, 0, sizeof(*result));
    result->primary_intent = KIC_INTENT_UNKNOWN;
    result->confidence = 0.0;
    result->num_hypotheses = 0;

    char lower_query[1024];
    to_lowercase(lower_query, query, sizeof(lower_query));

    /* Проходим по всем паттернам */
    double scores[KIC_MAX_CATEGORIES] = {0};
    int intent_counts[KIC_MAX_CATEGORIES] = {0};

    for (int i = 0; i < classifier->num_patterns; i++) {
        KolibriIntentPattern *pattern = &classifier->patterns[i];
        int matches = count_keyword_matches(lower_query, pattern->keywords, pattern->num_keywords);

        if (matches > 0) {
            double score = (double)matches / pattern->num_keywords * pattern->base_confidence;
            int idx = (int)pattern->intent;
            scores[idx] += score;
            intent_counts[idx]++;
        }
    }

    /* Находим лучший intent */
    int best_idx = -1;
    double best_score = 0.0;

    for (int i = 0; i < KIC_MAX_CATEGORIES; i++) {
        if (scores[i] > best_score) {
            best_score = scores[i];
            best_idx = i;
        }
    }

    if (best_idx >= 0 && best_score >= KIC_MIN_CONFIDENCE) {
        result->primary_intent = (KolibriIntent)best_idx;
        result->confidence = best_score > 1.0 ? 1.0 : best_score;

        /* Заполняем топ-5 */
        for (int attempt = 0; attempt < 5 && result->num_hypotheses < 5; attempt++) {
            int max_idx = -1;
            double max_score = 0.0;

            for (int i = 0; i < KIC_MAX_CATEGORIES; i++) {
                /* Проверяем, не добавлен ли уже */
                int already_added = 0;
                for (int j = 0; j < result->num_hypotheses; j++) {
                    if (result->top_intents[j] == (KolibriIntent)i) {
                        already_added = 1;
                        break;
                    }
                }

                if (!already_added && scores[i] > max_score) {
                    max_score = scores[i];
                    max_idx = i;
                }
            }

            if (max_idx >= 0) {
                result->top_intents[result->num_hypotheses] = (KolibriIntent)max_idx;
                result->top_confidences[result->num_hypotheses] = max_score;
                result->num_hypotheses++;
            }
        }

        /* Определяем метаданные */
        result->requires_reasoning = kolibri_ic_requires_reasoning(result->primary_intent);
        result->requires_knowledge =
            (result->primary_intent == KIC_INTENT_QUERY_FACT || result->primary_intent == KIC_INTENT_QUERY_DEFINITION ||
             result->primary_intent == KIC_INTENT_QUERY_COMPARISON);

        /* Рекомендуемый метод */
        if (result->primary_intent == KIC_INTENT_MATH_PROBLEM) {
            strcpy(result->recommended_method, "math_solver");
        } else if (result->primary_intent == KIC_INTENT_LOGIC_PUZZLE) {
            strcpy(result->recommended_method, "logic_solver");
        } else if (result->requires_reasoning) {
            strcpy(result->recommended_method, "reasoning");
        } else if (result->requires_knowledge) {
            strcpy(result->recommended_method, "knowledge_base");
        } else {
            strcpy(result->recommended_method, "chat");
        }
    }

    /* Оценка сложности */
    int word_count = 1;
    for (const char *p = query; *p; p++) {
        if (*p == ' ')
            word_count++;
    }
    result->query_complexity = word_count > 20 ? 1.0 : word_count / 20.0;

    double elapsed = ic_time_us() - start;
    classifier->total_queries++;
    classifier->classification_times_us += (uint64_t)elapsed;

    return 0;
}

KolibriIntent kolibri_ic_classify_fast(KolibriIntentClassifier *classifier, const char *query) {
    KolibriIntentResult result;
    if (kolibri_ic_classify(classifier, query, &result) == 0) {
        return result.primary_intent;
    }
    return KIC_INTENT_UNKNOWN;
}

/* ============================================================================
 * ДОБАВЛЕНИЕ ПАТТЕРНОВ
 * ============================================================================ */

int kolibri_ic_add_pattern(KolibriIntentClassifier *classifier, KolibriIntent intent, const char **keywords,
                           int num_keywords, double base_confidence, const char *domain) {
    if (!classifier || classifier->num_patterns >= KIC_MAX_PATTERNS)
        return -1;

    KolibriIntentPattern *pattern = &classifier->patterns[classifier->num_patterns];
    pattern->intent = intent;
    pattern->keywords = keywords;
    pattern->num_keywords = num_keywords;
    pattern->base_confidence = base_confidence;
    pattern->domain = domain ? domain : "general";
    pattern->requires_reasoning = kolibri_ic_requires_reasoning(intent);
    pattern->regex_patterns = NULL;
    pattern->num_regex_patterns = 0;

    classifier->num_patterns++;
    return 0;
}

/* ============================================================================
 * ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
 * ============================================================================ */

const char *kolibri_ic_intent_name(KolibriIntent intent) {
    static const char *names[] = {"QUERY_FACT",    "QUERY_DEFINITION", "QUERY_COMPARISON", "QUERY_CAUSE",
                                  "QUERY_PROCESS", "LOGIC_PUZZLE",     "MATH_PROBLEM",     "DEDUCTION",
                                  "EXPLAIN",       "EXAMPLE",          "ANALOGY",          "COUNTERFACTUAL",
                                  "TEACH",         "CORRECT",          "DELETE",           "LIST",
                                  "GREETING",      "FAREWELL",         "THANKS",           "CHAT",
                                  "UNKNOWN"};

    if (intent < 0 || intent > KIC_INTENT_UNKNOWN)
        return "INVALID";
    return names[intent];
}

const char *kolibri_ic_intent_desc(KolibriIntent intent) {
    static const char *descs[] = {"Запрос факта (сколько, когда, где)",
                                  "Запрос определения (что такое...)",
                                  "Запрос сравнения",
                                  "Запрос причины (почему...)",
                                  "Запрос процесса (как работает...)",
                                  "Логическая задача",
                                  "Математическая задача",
                                  "Дедуктивный вывод",
                                  "Объяснение",
                                  "Пример",
                                  "Аналогия",
                                  "Counterfactual (что если...)",
                                  "Обучение (научи, запомни)",
                                  "Исправление",
                                  "Удаление",
                                  "Список",
                                  "Приветствие",
                                  "Прощание",
                                  "Благодарность",
                                  "Болтовня",
                                  "Неизвестный intent"};

    if (intent < 0 || intent > KIC_INTENT_UNKNOWN)
        return "Неверный intent";
    return descs[intent];
}

void kolibri_ic_print_result(const KolibriIntentResult *result) {
    if (!result)
        return;

    printf("\n=== Intent Classification Result ===\n");
    printf("Primary Intent: %s (%.2f)\n", kolibri_ic_intent_name(result->primary_intent), result->confidence);
    printf("Description: %s\n", kolibri_ic_intent_desc(result->primary_intent));
    printf("Domain: %s\n", result->domain);
    printf("Complexity: %.2f\n", result->query_complexity);
    printf("Requires Reasoning: %s\n", result->requires_reasoning ? "Yes" : "No");
    printf("Requires Knowledge: %s\n", result->requires_knowledge ? "Yes" : "No");
    printf("Recommended Method: %s\n", result->recommended_method);

    if (result->num_hypotheses > 1) {
        printf("\nTop Hypotheses:\n");
        for (int i = 0; i < result->num_hypotheses; i++) {
            printf("  %d. %s (%.2f)\n", i + 1, kolibri_ic_intent_name(result->top_intents[i]),
                   result->top_confidences[i]);
        }
    }

    if (result->num_entities > 0) {
        printf("\nEntities (%d):\n", result->num_entities);
        for (int i = 0; i < result->num_entities; i++) {
            printf("  %d. %s\n", i + 1, result->entities[i]);
        }
    }
}

int kolibri_ic_requires_reasoning(KolibriIntent intent) {
    return (intent == KIC_INTENT_LOGIC_PUZZLE || intent == KIC_INTENT_MATH_PROBLEM || intent == KIC_INTENT_DEDUCTION ||
            intent == KIC_INTENT_COUNTERFACTUAL || intent == KIC_INTENT_QUERY_CAUSE ||
            intent == KIC_INTENT_QUERY_PROCESS);
}
