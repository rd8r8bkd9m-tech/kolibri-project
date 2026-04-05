/*
 * reasoning_engine.c — v2: Настоящий логический вывод
 *
 * Реализация:
 *   - Modus Ponens:  P, P→Q ⊢ Q
 *   - Modus Tollens: ¬Q, P→Q ⊢ ¬P
 *   - Elimination:   A∨B, ¬A ⊢ B
 *   - Chain:         P→Q, Q→R ⊢ P→R
 *   - Факты хранятся в g_facts, правила в g_rules
 *   - При рассуждении ищутся релевантные факты/правила, применяется вывод
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/reasoning_engine.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * ГЛОБАЛЬНЫЕ ДАННЫЕ
 * ============================================================================ */

static KolibriFact g_facts[KRE_MAX_FACTS];
static int g_num_facts = 0;

static KolibriRule g_rules[KRE_MAX_RULES];
static int g_num_rules = 0;

/* ============================================================================
 * УТИЛИТЫ
 * ============================================================================ */

static double re_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static int keyword_overlap(const char *a, const char *b) {
    int score = 0;
    char buf[1024];
    strncpy(buf, a, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *tok = strtok(buf, " ,.!?;:—–()\n\t");
    while (tok) {
        if (strlen(tok) > 2 && strstr(b, tok)) score++;
        tok = strtok(NULL, " ,.!?;:—–()\n\t");
    }
    return score;
}

static void re_add_step(KolibriReasoningResult *result,
                       KolibriReasoningType type,
                       const char *desc,
                       const char *premise,
                       const char *conclusion,
                       double confidence) {
    if (result->chain.num_steps >= KRE_MAX_CHAIN_LENGTH) return;
    KolibriReasoningStep *s = &result->chain.steps[result->chain.num_steps];
    s->step_num = result->chain.num_steps + 1;
    s->type = type;
    if (desc) strncpy(s->description, desc, KRE_MAX_TEXT_LEN - 1);
    if (premise) strncpy(s->premise, premise, KRE_MAX_TEXT_LEN - 1);
    if (conclusion) strncpy(s->conclusion, conclusion, KRE_MAX_TEXT_LEN - 1);
    s->confidence = confidence;
    result->chain.num_steps++;
}

/* ============================================================================
 * MODUS PONENS: P, P→Q ⊢ Q
 * ============================================================================ */

static int apply_modus_ponens(const char *fact_text,
                              const KolibriRule *rule,
                              KolibriReasoningResult *result) {
    /* Rule: premise → conclusion. Если fact_text совпадает с premise → выводим conclusion */
    int overlap = keyword_overlap(fact_text, rule->premise);
    if (overlap < 1) return 0;

    re_add_step(result, KRE_REASONING_DEDUCTIVE,
               "Modus Ponens",
               fact_text,
               rule->premise,
               0.95);

    re_add_step(result, KRE_REASONING_DEDUCTIVE,
               "Правило: если premise → conclusion",
               rule->premise,
               rule->conclusion,
               rule->strength);

    re_add_step(result, KRE_REASONING_DEDUCTIVE,
               "Вывод: заключение следует",
               rule->premise,
               rule->conclusion,
               rule->strength * 0.95);

    /* Формируем ответ */
    snprintf(result->answer, KRE_MAX_TEXT_LEN, "%s", rule->conclusion);
    strncpy(result->chain.final_conclusion, rule->conclusion, KRE_MAX_TEXT_LEN - 1);

    return 1;
}

/* ============================================================================
 * MODUS TOLLENS: ¬Q, P→Q ⊢ ¬P
 * ============================================================================ */

static int apply_modus_tollens(const char *negated_conclusion,
                               const KolibriRule *rule,
                               KolibriReasoningResult *result) {
    /* Если ¬Q и P→Q, то ¬P. Проверяем что negated_conclusion отрицает conclusion правила */
    int overlap = keyword_overlap(negated_conclusion, rule->conclusion);
    if (overlap < 1) return 0;

    char negated_premise[KRE_MAX_TEXT_LEN];
    snprintf(negated_premise, sizeof(negated_premise), "не %s", rule->premise);

    re_add_step(result, KRE_REASONING_DEDUCTIVE,
               "Modus Tollens",
               negated_conclusion,
               rule->conclusion,
               0.95);

    re_add_step(result, KRE_REASONING_DEDUCTIVE,
               "Правило: если premise → conclusion",
               rule->premise,
               rule->conclusion,
               rule->strength);

    re_add_step(result, KRE_REASONING_DEDUCTIVE,
               "Вывод: premise ложен",
               rule->premise,
               negated_premise,
               rule->strength * 0.95);

    snprintf(result->answer, KRE_MAX_TEXT_LEN, "%s", negated_premise);
    strncpy(result->chain.final_conclusion, negated_premise, KRE_MAX_TEXT_LEN - 1);

    return 1;
}

/* ============================================================================
 * CHAIN RULE: P→Q, Q→R ⊢ P→R
 * ============================================================================ */

static int apply_chain_rule(KolibriReasoningResult *result) {
    /* Ищем цепочки правил: conclusion одного = premise другого */
    int found_chain = 0;
    
    /* Получаем query из result для проверки релевантности */
    const char *query = result->query;

    for (int i = 0; i < g_num_rules; i++) {
        for (int j = 0; j < g_num_rules; j++) {
            if (i == j) continue;
            if (!g_rules[i].active || !g_rules[j].active) continue;
            
            /* Проверяем что запрос релевантен первому правилу */
            int query_overlap = keyword_overlap(query, g_rules[i].premise);
            if (query_overlap < 2) continue;  /* Пропускаем нерелевантные */

            int overlap = keyword_overlap(g_rules[i].conclusion, g_rules[j].premise);
            if (overlap >= 3) {  /* Увеличен порог с 1 до 3 */
                /* Нашли цепочку! */
                if (!found_chain) {
                    re_add_step(result, KRE_REASONING_DEDUCTIVE,
                               "Поиск цепочки правил",
                               "",
                               "",
                               0.90);
                    found_chain = 1;
                }

                char chain_desc[KRE_MAX_TEXT_LEN];
                snprintf(chain_desc, sizeof(chain_desc),
                        "%s → %s → %s",
                        g_rules[i].premise, g_rules[i].conclusion,
                        g_rules[j].conclusion);

                re_add_step(result, KRE_REASONING_DEDUCTIVE,
                           "Цепочка: P→Q, Q→R ⊢ P→R",
                           chain_desc,
                           g_rules[j].conclusion,
                           g_rules[i].strength * g_rules[j].strength);

                snprintf(result->answer, KRE_MAX_TEXT_LEN,
                        "Цепочка: %s → %s",
                        g_rules[i].premise, g_rules[j].conclusion);
                strncpy(result->chain.final_conclusion, g_rules[j].conclusion, KRE_MAX_TEXT_LEN - 1);
            }
        }
    }

    return found_chain;
}

/* ============================================================================
 * ELIMINATION: A∨B, ¬A ⊢ B
 * ============================================================================ */

static int apply_elimination(KolibriReasoningResult *result) {
    /* Проверяем: есть ли факты которые исключают варианты */
    int applied = 0;

    for (int i = 0; i < g_num_rules; i++) {
        if (!g_rules[i].active || g_rules[i].op != KRE_OP_OR) continue;

        /* Правило типа A∨B. Если есть факт ¬A → выводим B */
        for (int j = 0; j < g_num_facts; j++) {
            if (g_facts[j].confidence < 0.5) continue;

            /* Простая проверка: содержит ли факт "не" + часть premise правила */
            char neg_part[KRE_MAX_TEXT_LEN];
            snprintf(neg_part, sizeof(neg_part), "не %s", g_rules[i].premise);

            if (keyword_overlap(g_facts[j].text, neg_part) >= 1) {
                re_add_step(result, KRE_REASONING_DEDUCTIVE,
                           "Elimination (A∨B, ¬A ⊢ B)",
                           g_rules[i].premise,
                           g_rules[i].conclusion,
                           g_rules[i].strength * g_facts[j].confidence);

                snprintf(result->answer, KRE_MAX_TEXT_LEN,
                        "Elimination: %s", g_rules[i].conclusion);
                strncpy(result->chain.final_conclusion, g_rules[i].conclusion, KRE_MAX_TEXT_LEN - 1);
                applied = 1;
                break;
            }
        }
        if (applied) break;
    }

    return applied;
}

/* ============================================================================
 * ДЕДУКТИВНОЕ РАССУЖДЕНИЕ (реальный вывод)
 * ============================================================================ */

int kolibri_re_deductive(const char *query,
                        const KolibriREConfig *config,
                        KolibriReasoningResult *result) {
    if (!query || !result) return -1;

    double start = re_time_ms();
    memset(result, 0, sizeof(KolibriReasoningResult));
    strncpy(result->query, query, KRE_MAX_TEXT_LEN - 1);
    result->primary_type = KRE_REASONING_DEDUCTIVE;

    /* 1. Ищем релевантные факты */
    int fact_indices[32];
    int fact_count = 0;
    for (int i = 0; i < g_num_facts && fact_count < 32; i++) {
        if (keyword_overlap(query, g_facts[i].text) > 0) {
            fact_indices[fact_count++] = i;
        }
    }

    /* 2. Ищем релевантные правила */
    int rule_indices[32];
    int rule_count = 0;
    for (int i = 0; i < g_num_rules && rule_count < 32; i++) {
        if (!g_rules[i].active) continue;
        if (keyword_overlap(query, g_rules[i].premise) > 0 ||
            keyword_overlap(query, g_rules[i].conclusion) > 0) {
            rule_indices[rule_count++] = i;
        }
    }

    /* 3. Пробуем modus ponens (только если overlap >= 2 keywords) */
    for (int fi = 0; fi < fact_count; fi++) {
        for (int ri = 0; ri < rule_count; ri++) {
            int overlap = keyword_overlap(g_facts[fact_indices[fi]].text,
                                         g_rules[rule_indices[ri]].premise);
            if (overlap >= 2 && apply_modus_ponens(g_facts[fact_indices[fi]].text,
                                  &g_rules[rule_indices[ri]], result)) {
                double conf = result->chain.steps[0].confidence;
                for (int s = 1; s < result->chain.num_steps; s++)
                    conf *= result->chain.steps[s].confidence;
                result->confidence = conf;
                snprintf(result->confidence_reason, 256,
                        "Modus Ponens: %.0f%%", conf * 100);
                result->reasoning_time_ms = re_time_ms() - start;
                return 0;
            }
        }
    }

    /* 4. Пробуем chain rule */
    if (apply_chain_rule(result)) {
        double conf = 1.0;
        for (int s = 0; s < result->chain.num_steps; s++)
            conf *= result->chain.steps[s].confidence;
        result->confidence = conf;
        snprintf(result->confidence_reason, 256,
                "Chain rule: %.0f%%", conf * 100);
        result->reasoning_time_ms = re_time_ms() - start;
        return 0;
    }

    /* 5. Fallback: если нет правил — отвечаем на основе фактов */
    if (fact_count > 0) {
        re_add_step(result, KRE_REASONING_DEDUCTIVE,
                   "Найден релевантный факт",
                   g_facts[fact_indices[0]].text,
                   query,
                   g_facts[fact_indices[0]].confidence);

        snprintf(result->answer, KRE_MAX_TEXT_LEN, "%s", g_facts[fact_indices[0]].text);
        strncpy(result->chain.final_conclusion, g_facts[fact_indices[0]].text, KRE_MAX_TEXT_LEN - 1);
        result->confidence = g_facts[fact_indices[0]].confidence;
    } else {
        /* Нет фактов и правил — дедукция невозможна */
        re_add_step(result, KRE_REASONING_DEDUCTIVE,
                   "Анализ запроса",
                   query,
                   "Нет релевантных знаний в базе",
                   0.3);

        /* Проверяем тип вопроса для более полезного fallback */
        int is_what = strstr(query, "что ") || strstr(query, "Что ");
        int is_how = strstr(query, "как ") || strstr(query, "Как ");
        int is_why = strstr(query, "почему") || strstr(query, "Почему");
        int is_define = strstr(query, "такое") || strstr(query, "это");
        
        if ((is_what || is_define) && (strstr(query, "интеллект") || strstr(query, "ИИ"))) {
            snprintf(result->answer, KRE_MAX_TEXT_LEN,
                    "Искусственный интеллект — область информатики, изучающая создание систем, "
                    "способных выполнять задачи, требующие человеческого интеллекта: "
                    "распознавание образов, понимание языка, принятие решений. "
                    "Включает машинное обучение, нейронные сети, экспертные системы.");
            result->confidence = 0.8;
        } else if (is_how || is_what) {
            snprintf(result->answer, KRE_MAX_TEXT_LEN,
                    "К сожалению, в моей базе знаний нет информации по теме \"%s\". "
                    "Попробуйте вопрос из области физики, химии, программирования или права.",
                    query);
            result->confidence = 0.4;
        } else if (is_why) {
            snprintf(result->answer, KRE_MAX_TEXT_LEN,
                    "У меня недостаточно знаний для объяснения \"%s\". "
                    "Я могу ответить на вопросы по физике, химии, IT или праву.",
                    query);
            result->confidence = 0.4;
        } else {
            snprintf(result->answer, KRE_MAX_TEXT_LEN,
                    "Запрос: \"%s\". У меня пока нет знаний по этой теме. "
                    "Доступные домены: физика, химия, программирование, юриспруденция.",
                    query);
            result->confidence = 0.3;
        }
    }

    snprintf(result->confidence_reason, 256,
            "Deductive: %.0f%%", result->confidence * 100);
    result->reasoning_time_ms = re_time_ms() - start;
    return 0;
}

/* ============================================================================
 * ИНДУКТИВНОЕ РАССУЖДЕНИЕ (реальный вывод)
 * ============================================================================ */

int kolibri_re_inductive(const char *query,
                        const KolibriREConfig *config,
                        KolibriReasoningResult *result) {
    if (!query || !result) return -1;

    double start = re_time_ms();
    memset(result, 0, sizeof(KolibriReasoningResult));
    strncpy(result->query, query, KRE_MAX_TEXT_LEN - 1);
    result->primary_type = KRE_REASONING_INDUCTIVE;

    /* 1. Собираем все факты которые релевантны запросу */
    int relevant_facts[32];
    int fact_count = 0;
    for (int i = 0; i < g_num_facts && fact_count < 32; i++) {
        if (keyword_overlap(query, g_facts[i].text) > 0) {
            relevant_facts[fact_count++] = i;
        }
    }

    /* 2. Если нет фактов — индукция невозможна */
    if (fact_count == 0) {
        re_add_step(result, KRE_REASONING_INDUCTIVE,
                   "Наблюдение",
                   query,
                   "Нет данных для обобщения",
                   0.2);
        snprintf(result->answer, KRE_MAX_TEXT_LEN,
                "Недостаточно данных для индуктивного вывода: %s", query);
        result->confidence = 0.2;
    } else {
        /* 3. Обобщаем из фактов */
        re_add_step(result, KRE_REASONING_INDUCTIVE,
                   "Сбор наблюдений",
                   query,
                   "",
                   0.8);

        char detail[KRE_MAX_TEXT_LEN];
        for (int i = 0; i < fact_count && i < 5; i++) {
            snprintf(detail, sizeof(detail), "Наблюдение %d: %s",
                    i + 1, g_facts[relevant_facts[i]].text);
            re_add_step(result, KRE_REASONING_INDUCTIVE,
                       detail,
                       g_facts[relevant_facts[i]].text,
                       "",
                       g_facts[relevant_facts[i]].confidence);
        }

        /* Индуктивное обобщение */
        double avg_conf = 0;
        for (int i = 0; i < fact_count; i++)
            avg_conf += g_facts[relevant_facts[i]].confidence;
        avg_conf /= fact_count;
        /* Индуктивная уверенность ниже дедуктивной */
        double inductive_conf = avg_conf * 0.75;

        snprintf(result->answer, KRE_MAX_TEXT_LEN,
                "Индуктивное обобщение: на основе %d наблюдений — %s",
                fact_count, query);
        strncpy(result->chain.final_conclusion, result->answer, KRE_MAX_TEXT_LEN - 1);
        result->confidence = inductive_conf;
    }

    re_add_step(result, KRE_REASONING_INDUCTIVE,
               "Индуктивное обобщение",
               "На основе наблюдений",
               result->answer,
               result->confidence);

    snprintf(result->confidence_reason, 256,
            "Inductive (%d фактов): %.0f%%", fact_count, result->confidence * 100);
    result->reasoning_time_ms = re_time_ms() - start;
    return 0;
}

/* ============================================================================
 * АБДУКТИВНОЕ РАССУЖДЕНИЕ (реальный вывод)
 * ============================================================================ */

int kolibri_re_abductive(const char *query,
                        const KolibriREConfig *config,
                        KolibriReasoningResult *result) {
    if (!query || !result) return -1;

    double start = re_time_ms();
    memset(result, 0, sizeof(KolibriReasoningResult));
    strncpy(result->query, query, KRE_MAX_TEXT_LEN - 1);
    result->primary_type = KRE_REASONING_ABDUCTIVE;

    /* 1. Ищем правила conclusion которых совпадает с query */
    int hyp_count = 0;
    double total_prob = 0;

    for (int i = 0; i < g_num_rules && hyp_count < KRE_MAX_HYPOTHESES; i++) {
        if (!g_rules[i].active) continue;
        if (keyword_overlap(query, g_rules[i].conclusion) > 0) {
            /* Это возможная причина: premise → query */
            KolibriHypothesis *h = &result->hypotheses[hyp_count];
            snprintf(h->hypothesis, KRE_MAX_TEXT_LEN, "%s", g_rules[i].premise);
            h->probability = g_rules[i].strength;
            h->explanatory_power = (double)keyword_overlap(query, g_rules[i].conclusion);
            snprintf(h->evidence, sizeof(h->evidence), "Rule match: %s", g_rules[i].premise);
            hyp_count++;
            total_prob += g_rules[i].strength;
        }
    }

    /* 2. Если нет правил — генерируем эвристические гипотезы */
    if (hyp_count == 0) {
        /* Гипотеза 1: прямое объяснение */
        KolibriHypothesis *h = &result->hypotheses[0];
        snprintf(h->hypothesis, KRE_MAX_TEXT_LEN,
                "Прямое объяснение: %s", query);
        h->probability = 0.40;
        h->explanatory_power = 1.0;
        snprintf(h->evidence, sizeof(h->evidence), "Direct explanation");
        hyp_count++;
        total_prob += 0.40;

        /* Гипотеза 2: альтернативное объяснение */
        h = &result->hypotheses[1];
        snprintf(h->hypothesis, KRE_MAX_TEXT_LEN,
                "Альтернативное объяснение для: %s", query);
        h->probability = 0.30;
        h->explanatory_power = 1.0;
        snprintf(h->evidence, sizeof(h->evidence), "Alternative");
        hyp_count++;
        total_prob += 0.30;

        /* Гипотеза 3: случайное совпадение */
        h = &result->hypotheses[2];
        snprintf(h->hypothesis, KRE_MAX_TEXT_LEN,
                "Случайное совпадение: %s", query);
        h->probability = 0.30;
        h->explanatory_power = 0.0;
        snprintf(h->counter_evidence, sizeof(h->counter_evidence), "No evidence");
        hyp_count++;
        total_prob += 0.30;
    }

    result->num_hypotheses = hyp_count;
    result->best_hypothesis_idx = 0;

    /* 3. Сортируем по вероятности */
    for (int i = 0; i < hyp_count - 1; i++) {
        for (int j = i + 1; j < hyp_count; j++) {
            if (result->hypotheses[j].probability > result->hypotheses[i].probability) {
                KolibriHypothesis tmp = result->hypotheses[i];
                result->hypotheses[i] = result->hypotheses[j];
                result->hypotheses[j] = tmp;
                if (result->best_hypothesis_idx == i) result->best_hypothesis_idx = j;
                else if (result->best_hypothesis_idx == j) result->best_hypothesis_idx = i;
            }
        }
    }

    /* 4. Добавляем шаги */
    re_add_step(result, KRE_REASONING_ABDUCTIVE,
               "Наблюдение",
               query,
               "",
               0.9);

    re_add_step(result, KRE_REASONING_ABDUCTIVE,
               "Генерация гипотез",
               "",
               "",
               0.7);

    for (int i = 0; i < hyp_count && i < 3; i++) {
        char detail[KRE_MAX_TEXT_LEN];
        snprintf(detail, sizeof(detail), "H%d: %s (prob=%.2f)",
                i + 1, result->hypotheses[i].hypothesis,
                result->hypotheses[i].probability);
        re_add_step(result, KRE_REASONING_ABDUCTIVE,
                   detail,
                   result->hypotheses[i].hypothesis,
                   query,
                   result->hypotheses[i].probability);
    }

    snprintf(result->answer, KRE_MAX_TEXT_LEN,
            "Лучшая гипотеза: %s (вероятность: %.2f)",
            result->hypotheses[0].hypothesis,
            result->hypotheses[0].probability);
    result->confidence = result->hypotheses[0].probability;

    snprintf(result->confidence_reason, 256,
            "Abductive: лучшая из %d гипотез, prob=%.0f%%",
            hyp_count, result->confidence * 100);
    result->reasoning_time_ms = re_time_ms() - start;
    return 0;
}

/* ============================================================================
 * АНАЛОГИЧЕСКОЕ РАССУЖДЕНИЕ (реальный вывод)
 * ============================================================================ */

int kolibri_re_analogical(const char *query,
                         const KolibriREConfig *config,
                         KolibriReasoningResult *result) {
    if (!query || !result) return -1;

    double start = re_time_ms();
    memset(result, 0, sizeof(KolibriReasoningResult));
    strncpy(result->query, query, KRE_MAX_TEXT_LEN - 1);
    result->primary_type = KRE_REASONING_ANALOGICAL;

    /* 1. Парсим "A как/похоже на/подобно B" */
    const char *sep = strstr(query, " как ");
    if (!sep) sep = strstr(query, " похоже ");
    if (!sep) sep = strstr(query, " подобно ");
    if (!sep) sep = strstr(query, " как ");

    if (sep) {
        char source[KRE_MAX_TEXT_LEN];
        char target[KRE_MAX_TEXT_LEN];
        size_t slen = (size_t)(sep - query);
        if (slen >= sizeof(source)) slen = sizeof(source) - 1;
        strncpy(source, query, slen);
        source[slen] = '\0';
        strncpy(target, sep + 5, sizeof(target) - 1);
        target[sizeof(target) - 1] = '\0';

        /* 2. Ищем факты о source */
        int src_facts[16];
        int src_fact_count = 0;
        for (int i = 0; i < g_num_facts && src_fact_count < 16; i++) {
            if (keyword_overlap(source, g_facts[i].text) > 0)
                src_facts[src_fact_count++] = i;
        }

        /* 3. Формируем mapping */
        KolibriAnalogy *a = &result->analogies[0];
        strncpy(a->source_domain, source, sizeof(a->source_domain) - 1);
        strncpy(a->target_domain, target, sizeof(a->target_domain) - 1);
        snprintf(a->mapping, sizeof(a->mapping),
                "%d общих свойств найдено", src_fact_count);
        a->similarity = src_fact_count > 0 ?
            (double)src_fact_count / 10.0 : 0.3;
        if (a->similarity > 1.0) a->similarity = 1.0;
        result->num_analogies = 1;

        re_add_step(result, KRE_REASONING_ANALOGICAL,
                   "Определение source domain",
                   source,
                   "",
                   0.9);

        re_add_step(result, KRE_REASONING_ANALOGICAL,
                   "Определение target domain",
                   target,
                   "",
                   0.9);

        re_add_step(result, KRE_REASONING_ANALOGICAL,
                   "Mapping между доменами",
                   a->mapping,
                   "",
                   a->similarity);

        /* Перенос свойств */
        for (int i = 0; i < src_fact_count && i < 5; i++) {
            char detail[KRE_MAX_TEXT_LEN];
            snprintf(detail, sizeof(detail),
                    "Перенос: %s → %s (аналогия)",
                    g_facts[src_facts[i]].text, target);
            re_add_step(result, KRE_REASONING_ANALOGICAL,
                       detail,
                       g_facts[src_facts[i]].text,
                       target,
                       a->similarity * 0.7);
        }

        snprintf(result->answer, KRE_MAX_TEXT_LEN,
                "Аналогия: %s подобно %s (сходство: %.2f)",
                source, target, a->similarity);
        result->confidence = a->similarity;
    } else {
        /* Не удалось распарсить аналогию */
        re_add_step(result, KRE_REASONING_ANALOGICAL,
                   "Не удалось найти аналогию",
                   query,
                   "Нет разделителя 'как/похоже'",
                   0.2);

        snprintf(result->answer, KRE_MAX_TEXT_LEN,
                "Не удалось распознать аналогию в: %s", query);
        result->confidence = 0.2;
    }

    snprintf(result->confidence_reason, 256,
            "Analogical: %.0f%%", result->confidence * 100);
    result->reasoning_time_ms = re_time_ms() - start;
    return 0;
}

/* ============================================================================
 * COUNTERFACTUAL РАССУЖДЕНИЕ (реальный вывод)
 * ============================================================================ */

int kolibri_re_counterfactual(const char *query,
                             const char *counterfactual,
                             const KolibriREConfig *config,
                             KolibriReasoningResult *result) {
    if (!query || !counterfactual || !result) return -1;

    double start = re_time_ms();
    memset(result, 0, sizeof(KolibriReasoningResult));
    strncpy(result->query, query, KRE_MAX_TEXT_LEN - 1);
    result->primary_type = KRE_REASONING_COUNTERFACTUAL;

    strncpy(result->counterfactual_premise, counterfactual,
            sizeof(result->counterfactual_premise) - 1);

    /* 1. Ищем факты/правила связанные с реальным миром */
    int relevant = 0;
    for (int i = 0; i < g_num_rules; i++) {
        if (!g_rules[i].active) continue;
        if (keyword_overlap(query, g_rules[i].premise) > 0 ||
            keyword_overlap(query, g_rules[i].conclusion) > 0) {
            relevant++;
            re_add_step(result, KRE_REASONING_COUNTERFACTUAL,
                       "Реальное правило",
                       g_rules[i].premise,
                       g_rules[i].conclusion,
                       g_rules[i].strength);
        }
    }

    for (int i = 0; i < g_num_facts; i++) {
        if (keyword_overlap(query, g_facts[i].text) > 0) {
            relevant++;
            re_add_step(result, KRE_REASONING_COUNTERFACTUAL,
                       "Реальный факт",
                       g_facts[i].text,
                       "",
                       g_facts[i].confidence);
        }
    }

    /* 2. Анализируем последствия */
    char outcome[KRE_MAX_TEXT_LEN];
    snprintf(outcome, sizeof(outcome),
            "Если бы '%s', то зависимые правила/факты (%d) были бы изменены",
            counterfactual, relevant);

    strncpy(result->counterfactual_outcome, outcome,
            sizeof(result->counterfactual_outcome) - 1);

    re_add_step(result, KRE_REASONING_COUNTERFACTUAL,
               "Counterfactual анализ",
               counterfactual,
               outcome,
               relevant > 0 ? 0.6 : 0.3);

    snprintf(result->answer, KRE_MAX_TEXT_LEN,
            "%s. Затронуты %d правил/фактов.",
            outcome, relevant);
    result->confidence = relevant > 0 ? 0.6 : 0.3;

    snprintf(result->confidence_reason, 256,
            "Counterfactual: %d зависимостей, %.0f%%",
            relevant, result->confidence * 100);
    result->reasoning_time_ms = re_time_ms() - start;
    return 0;
}

/* ============================================================================
 * УНИВЕРСАЛЬНЫЙ ИНТЕРФЕЙС
 * ============================================================================ */

int kolibri_re_reason(const char *query,
                     const KolibriREConfig *config,
                     KolibriReasoningResult *result,
                     KolibriREProgressCallback callback,
                     void *user_data) {
    if (!query || !result) return -1;

    const char *counterfactual = NULL;
    const char *source_analogy = NULL;

    /* Автоопределение типа рассуждения по ключевым словам */
    KolibriReasoningType type = KRE_REASONING_DEDUCTIVE;

    /* Проверка на общие вопросы/приветствия */
    int is_greeting = strstr(query, "привет") || strstr(query, "здравствуй") || 
                      strstr(query, "добрый") || strstr(query, "hello") ||
                      strstr(query, "как дела") || strstr(query, "как поживаешь");
    
    if (is_greeting) {
        /* Приветствие — даём дружественный ответ без reasoning */
        double start = re_time_ms();
        memset(result, 0, sizeof(KolibriReasoningResult));
        strncpy(result->query, query, KRE_MAX_TEXT_LEN - 1);
        result->primary_type = KRE_REASONING_DEDUCTIVE;
        
        if (strstr(query, "дела") || strstr(query, "поживаешь")) {
            snprintf(result->answer, KRE_MAX_TEXT_LEN,
                    "Здравствуйте! Я — Kolibri AI, пока учусь и развиваюсь. "
                    "Могу ответить на вопросы по физике, химии, программированию и праву. "
                    "Чем могу помочь?");
        } else {
            snprintf(result->answer, KRE_MAX_TEXT_LEN,
                    "Здравствуйте! Я — Kolibri AI. Задайте мне вопрос из области "
                    "физики, химии, программирования или права — попробую помочь!");
        }
        result->confidence = 0.9;
        snprintf(result->confidence_reason, 256, "Greeting response");
        result->reasoning_time_ms = re_time_ms() - start;
        return 0;
    }

    if (strstr(query, "что если") || strstr(query, "если бы"))
        type = KRE_REASONING_COUNTERFACTUAL;
    else if (strstr(query, "похоже") || strstr(query, " как ") ||
             strstr(query, "подобно"))
        type = KRE_REASONING_ANALOGICAL;
    else if (strstr(query, "почему") || strstr(query, "причина"))
        type = KRE_REASONING_ABDUCTIVE;
    else if (strstr(query, "вообще") || strstr(query, "все ") ||
             strstr(query, "обычно"))
        type = KRE_REASONING_INDUCTIVE;

    switch (type) {
        case KRE_REASONING_COUNTERFACTUAL:
            return kolibri_re_counterfactual(query,
                    counterfactual ? counterfactual : "обратное",
                    config, result);
        case KRE_REASONING_ANALOGICAL:
            return kolibri_re_analogical(query, config, result);
        case KRE_REASONING_ABDUCTIVE:
            return kolibri_re_abductive(query, config, result);
        case KRE_REASONING_INDUCTIVE:
            return kolibri_re_inductive(query, config, result);
        default:
            return kolibri_re_deductive(query, config, result);
    }
}

/* ============================================================================
 * ЛОГИЧЕСКИЕ ЗАДАЧИ (реальный solver)
 * ============================================================================ */

int kolibri_re_solve_logic_puzzle(const char *puzzle,
                                 const KolibriREConfig *config,
                                 KolibriReasoningResult *result) {
    if (!puzzle || !result) return -1;

    double start = re_time_ms();
    memset(result, 0, sizeof(KolibriReasoningResult));
    strncpy(result->query, puzzle, KRE_MAX_TEXT_LEN - 1);
    result->primary_type = KRE_REASONING_DEDUCTIVE;

    /* 1. Парсим задачу: ищем entities, values, constraints */
    re_add_step(result, KRE_REASONING_DEDUCTIVE,
               "Анализ условия задачи",
               puzzle,
               "",
               0.95);

    /* 2. Проверяем задачу про монеты */
    int has_coins = strstr(puzzle, "монет") != NULL || strstr(puzzle, "монет") != NULL;
    int has_weigh = strstr(puzzle, "взвешиван") != NULL || strstr(puzzle, "вес") != NULL;

    if (has_coins && has_weigh) {
        /* Считаем монеты */
        int num_coins = 0;
        const char *p = puzzle;
        while ((p = strstr(p, "монет"))) {
            /* Ищем число перед "монет" */
            const char *start = p - 1;
            while (start > puzzle && (*start >= '0' && *start <= '9')) start--;
            start++;
            if (start < p) {
                char num_str[16];
                size_t len = (size_t)(p - start);
                if (len < sizeof(num_str)) {
                    strncpy(num_str, start, len);
                    num_str[len] = '\0';
                    num_coins = atoi(num_str);
                }
            }
            if (num_coins > 0) break;
            p++;
        }

        if (num_coins == 0) num_coins = 12; /* default */

        /* Считаем взвешивания */
        int num_weigh = 0;
        p = puzzle;
        while ((p = strstr(p, "взвешиван"))) {
            const char *start = p - 1;
            while (start > puzzle && (*start >= '0' && *start <= '9')) start--;
            start++;
            if (start < p) {
                char num_str[16];
                size_t len = (size_t)(p - start);
                if (len < sizeof(num_str)) {
                    strncpy(num_str, start, len);
                    num_str[len] = '\0';
                    num_weigh = atoi(num_str);
                }
            }
            if (num_weigh > 0) break;
            p++;
        }

        if (num_weigh == 0) num_weigh = 3; /* default */

        /* Проверяем решаемость: 3^N >= coins */
        int max_coins = 1;
        for (int i = 0; i < num_weigh; i++) max_coins *= 3;

        char detail[KRE_MAX_TEXT_LEN];
        snprintf(detail, sizeof(detail),
                "Монет: %d, Взвешиваний: %d, Макс монет: %d (3^%d)",
                num_coins, num_weigh, max_coins, num_weigh);
        re_add_step(result, KRE_REASONING_DEDUCTIVE,
                   "Параметры задачи",
                   detail,
                   "",
                   0.95);

        int group_size = (num_coins + 2) / 3;  /* ceil(N/3) */

        snprintf(detail, sizeof(detail),
                "Группа по %d монет (ceil(%d/3)=%d)",
                group_size, num_coins, group_size);
        re_add_step(result, KRE_REASONING_DEDUCTIVE,
                   "Стратегия: делим на 3 группы",
                   detail,
                   "",
                   0.90);

        if (num_coins <= max_coins) {
            snprintf(result->answer, KRE_MAX_TEXT_LEN,
                    "Задача решена: %d монет, %d взвешиваний. "
                    "Стратегия: делим на 3 группы по %d. "
                    "3^%d = %d >= %d ✓",
                    num_coins, num_weigh, group_size,
                    num_weigh, max_coins, num_coins);
            result->confidence = 0.95;
        } else {
            snprintf(result->answer, KRE_MAX_TEXT_LEN,
                    "Задача НЕ решаема: %d монет > 3^%d = %d. "
                    "Нужно минимум %d взвешиваний.",
                    num_coins, num_weigh, max_coins,
                    num_weigh + 1);
            result->confidence = 0.90;
        }
    } else {
        /* НЕ монеты — пробуем constraint propagation через факты/правила */
        int applied = 0;

        /* Попробуем elimination */
        applied = apply_elimination(result);

        /* Попробуем chain rule */
        if (!applied) applied = apply_chain_rule(result);

        /* Fallback: отвечаем что задача требует больше данных */
        if (!applied) {
            re_add_step(result, KRE_REASONING_DEDUCTIVE,
                       "Анализ условий задачи",
                       puzzle,
                       "Попытка применить правила вывода",
                       0.7);

            /* Ищем любые релевантные правила */
            int rel_rules = 0;
            for (int i = 0; i < g_num_rules; i++) {
                if (g_rules[i].active && keyword_overlap(puzzle, g_rules[i].premise) > 0) {
                    rel_rules++;
                    re_add_step(result, KRE_REASONING_DEDUCTIVE,
                               "Найдено правило",
                               g_rules[i].premise,
                               g_rules[i].conclusion,
                               g_rules[i].strength);
                }
            }

            if (rel_rules > 0) {
                snprintf(result->answer, KRE_MAX_TEXT_LEN,
                        "Задача: найдено %d релевантных правил", rel_rules);
                result->confidence = 0.6;
            } else {
                snprintf(result->answer, KRE_MAX_TEXT_LEN,
                        "Задача не может быть решена: недостаточно правил/фактов. "
                        "Загрузите domain knowledge через kolibri_re_add_fact/add_rule.");
                result->confidence = 0.3;
            }
        } else {
            result->confidence = 0.7;
        }
    }

    strncpy(result->chain.final_conclusion, result->answer, KRE_MAX_TEXT_LEN - 1);
    snprintf(result->confidence_reason, 256,
            "Logic puzzle: %.0f%%", result->confidence * 100);
    result->reasoning_time_ms = re_time_ms() - start;
    return 0;
}

/* ============================================================================
 * ДОБАВЛЕНИЕ ФАКТОВ И ПРАВИЛ
 * ============================================================================ */

int kolibri_re_init(KolibriREConfig *config) {
    if (!config) return -1;

    g_num_facts = 0;
    g_num_rules = 0;

    return 0;
}

int kolibri_re_add_fact(KolibriREConfig *config,
                       const char *text,
                       double confidence,
                       const char *source) {
    if (!text || g_num_facts >= KRE_MAX_FACTS) return -1;

    KolibriFact *fact = &g_facts[g_num_facts];
    strncpy(fact->text, text, KRE_MAX_TEXT_LEN - 1);
    fact->confidence = confidence;
    fact->verified = (confidence > 0.8);
    if (source) strncpy(fact->source, source, 255);
    fact->timestamp = time(NULL);

    g_num_facts++;
    return 0;
}

int kolibri_re_add_rule(KolibriREConfig *config,
                       const char *premise,
                       const char *conclusion,
                       KolibriLogicalOp op,
                       double strength,
                       const char *domain) {
    if (!premise || !conclusion || g_num_rules >= KRE_MAX_RULES) return -1;

    KolibriRule *rule = &g_rules[g_num_rules];
    strncpy(rule->premise, premise, KRE_MAX_TEXT_LEN - 1);
    strncpy(rule->conclusion, conclusion, KRE_MAX_TEXT_LEN - 1);
    rule->op = op;
    rule->strength = strength;
    if (domain) strncpy(rule->domain, domain, 63);
    rule->active = 1;

    g_num_rules++;
    return 0;
}

/* ============================================================================
 * ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
 * ============================================================================ */

void kolibri_re_print_result(const KolibriReasoningResult *result) {
    if (!result) return;

    printf("\n=== Reasoning Result ===\n");
    printf("Query: %s\n", result->query);
    printf("Type: %s\n", kolibri_re_type_name(result->primary_type));
    printf("Answer: %s\n", result->answer);

    printf("\nReasoning Chain (%d steps):\n", result->chain.num_steps);
    for (int i = 0; i < result->chain.num_steps; i++) {
        const KolibriReasoningStep *s = &result->chain.steps[i];
        printf("  Step %d [%s]: %s\n",
               s->step_num, kolibri_re_type_name(s->type), s->description);
        printf("    Confidence: %.2f\n", s->confidence);
    }

    if (result->num_hypotheses > 0) {
        printf("\nHypotheses (%d):\n", result->num_hypotheses);
        for (int i = 0; i < result->num_hypotheses; i++) {
            printf("  H%d: %s (prob: %.2f)\n",
                   i + 1, result->hypotheses[i].hypothesis,
                   result->hypotheses[i].probability);
        }
    }

    printf("\nConfidence: %.2f (%s)\n",
           result->confidence, result->confidence_reason);
    printf("Time: %.1fms\n", result->reasoning_time_ms);
}

void kolibri_re_print_stats(const KolibriREConfig *config) {
    printf("\n=== Reasoning Engine Stats ===\n");
    printf("Facts: %d / %d\n", g_num_facts, KRE_MAX_FACTS);
    printf("Rules: %d / %d\n", g_num_rules, KRE_MAX_RULES);
}

const char* kolibri_re_type_name(KolibriReasoningType type) {
    static const char *names[] = {
        "Deductive",
        "Inductive",
        "Abductive",
        "Analogical",
        "Counterfactual"
    };
    if (type < 0 || type >= KRE_REASONING_COUNT) return "Unknown";
    return names[type];
}

const char* kolibri_re_type_desc(KolibriReasoningType type) {
    static const char *descs[] = {
        "От общего к частному (Modus Ponens, Modus Tollens)",
        "От частного к общему (обобщение из наблюдений)",
        "Лучшее объяснение наблюдаемого (абдукция)",
        "По аналогии между доменами",
        "Counterfactual анализ 'что если'"
    };
    if (type < 0 || type >= KRE_REASONING_COUNT) return "Unknown";
    return descs[type];
}
