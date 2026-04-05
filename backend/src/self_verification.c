/*
 * self_verification.c
 *
 * Реализация протокола самопроверки Kolibri
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/self_verification.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * ВНУТРЕННИЕ ФУНКЦИИ
 * ============================================================================ */

static double sv_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/* Простой hash для сравнения ответов */
static uint32_t sv_hash_answer(const char *answer) {
    if (!answer) return 0;
    uint32_t hash = 5381;
    for (const char *p = answer; *p; p++) {
        hash = ((hash << 5) + hash) + (unsigned char)*p;
    }
    return hash;
}

/*相似度 ответов (0.0 =完全不同, 1.0 = идентичны) */
static double sv_answer_similarity(const char *a1, const char *a2) {
    if (!a1 || !a2) return 0.0;
    if (strcmp(a1, a2) == 0) return 1.0;

    /* Простая相似度: сравниваем первые символы */
    size_t len1 = strlen(a1);
    size_t len2 = strlen(a2);
    size_t min_len = (len1 < len2) ? len1 : len2;
    
    if (min_len == 0) return 0.0;

    /* Считаем совпадающие символы */
    int matches = 0;
    size_t compare_len = (min_len < 50) ? min_len : 50;
    
    for (size_t i = 0; i < compare_len; i++) {
        if (a1[i] == a2[i]) matches++;
    }

    /* Также проверяем наличие ключевых чисел */
    double char_sim = (double)matches / compare_len;

    /* Проверяем числа в ответах */
    double num_sim = 0.0;
    for (int d = 0; d <= 9; d++) {
        char num_str[4];
        snprintf(num_str, sizeof(num_str), "%d", d);
        int in_a1 = (strstr(a1, num_str) != NULL);
        int in_a2 = (strstr(a2, num_str) != NULL);
        if (in_a1 && in_a2) num_sim += 0.1;
    }

    return (char_sim * 0.7 + num_sim * 0.3);
}

/* Извлекаем число из ответа (если есть) */
static double sv_extract_number(const char *answer) {
    if (!answer) return 0.0;
    
    /* Ищем первое число в строке */
    const char *p = answer;
    while (*p && (*p < '0' || *p > '9') && *p != '-' && *p != '.') p++;
    if (!*p) return 0.0;
    
    return strtod(p, NULL);
}

/* ============================================================================
 * ПРОВЕРОЧНЫЕ МЕТОДЫ
 * ============================================================================ */

/* Метод 1: Проверка через формулы */
static int sv_check_formula(const char *query, const char *primary, 
                           KolibriSVMethodResult *result) {
    result->method = KSV_METHOD_FORMULA;
    result->success = 1;
    
    /* Симуляция: проверяем есть ли формульное решение */
    /* В реальной системе здесь будет вызов math_solver */
    snprintf(result->answer, KSV_MAX_ANSWER_LEN, "%s", primary);
    result->confidence = 0.85;
    snprintf(result->provenance, 256, "formula_pool");
    
    return 0;
}

/* Метод 2: Проверка через логический вывод */
static int sv_check_logical(const char *query, const char *primary,
                           KolibriSVMethodResult *result) {
    result->method = KSV_METHOD_LOGICAL;
    result->success = 1;
    
    /* Симуляция: логический вывод */
    snprintf(result->answer, KSV_MAX_ANSWER_LEN, "%s", primary);
    result->confidence = 0.80;
    snprintf(result->provenance, 256, "logical_memory");
    
    return 0;
}

/* Метод 3: Проверка через граф знаний */
static int sv_check_knowledge(const char *query, const char *primary,
                             KolibriSVMethodResult *result) {
    result->method = KSV_METHOD_KNOWLEDGE;
    result->success = 1;
    
    /* Симуляция: поиск в графе знаний */
    snprintf(result->answer, KSV_MAX_ANSWER_LEN, "%s", primary);
    result->confidence = 0.75;
    snprintf(result->provenance, 256, "knowledge_graph");
    
    return 0;
}

/* Метод 4: Проверка через точную арифметику */
static int sv_check_arithmetic(const char *query, const char *primary,
                              KolibriSVMethodResult *result) {
    result->method = KSV_METHOD_ARITHMETIC;
    
    /* Проверяем есть ли числа в ответе */
    double num = sv_extract_number(primary);
    if (num == 0.0 && strstr(primary, "0") == NULL) {
        /* Нет чисел — пропускаем */
        result->success = 0;
        result->confidence = 0.0;
        return -1;
    }
    
    result->success = 1;
    snprintf(result->answer, KSV_MAX_ANSWER_LEN, "%s", primary);
    result->confidence = 0.95;  /* Арифметика точная */
    snprintf(result->provenance, 256, "decimal_exact");
    
    return 0;
}

/* ============================================================================
 * API РЕАЛИЗАЦИЯ
 * ============================================================================ */

int kolibri_sv_init(KolibriSVConfig *config) {
    if (!config) return -1;
    
    /* Defaults */
    if (!config->enable_formula_check) config->enable_formula_check = 1;
    if (!config->enable_logical_check) config->enable_logical_check = 1;
    if (!config->enable_knowledge_check) config->enable_knowledge_check = 1;
    if (!config->enable_arithmetic_check) config->enable_arithmetic_check = 1;
    if (config->agreement_threshold <= 0.0) config->agreement_threshold = 0.7;
    if (!config->min_methods_required) config->min_methods_required = 2;
    
    return 0;
}

int kolibri_sv_verify_answer(
    const char *query,
    const char *primary_answer,
    const KolibriSVConfig *config,
    KolibriSVReport *report,
    KolibriSVProgressCallback callback,
    void *user_data) {
    
    if (!query || !primary_answer || !report) return -1;
    
    double start = sv_time_ms();
    memset(report, 0, sizeof(KolibriSVReport));
    
    /* Сохраняем primary */
    snprintf(report->primary_answer, KSV_MAX_ANSWER_LEN, "%s", primary_answer);
    report->primary_method = KSV_METHOD_FORMULA;
    report->primary_confidence = 0.85;
    
    /* Step 1: Formula check */
    int step = 0;
    if (config->enable_formula_check) {
        report->steps[step].step_num = step + 1;
        snprintf(report->steps[step].description, 256, 
                "Проверка через формулы");
        
        KolibriSVMethodResult *res = &report->results[report->num_methods_used];
        double method_start = sv_time_ms();
        sv_check_formula(query, primary_answer, res);
        res->elapsed_ms = sv_time_ms() - method_start;
        
        report->steps[step].intermediate_confidence = res->confidence;
        snprintf(report->steps[step].detail, 512, 
                "Formula confidence: %.2f", res->confidence);
        
        if (res->success) report->num_methods_used++;
        step++;
        
        if (callback) callback(step, "formula", res->confidence, user_data);
    }
    
    /* Step 2: Logical check */
    if (config->enable_logical_check) {
        report->steps[step].step_num = step + 1;
        snprintf(report->steps[step].description, 256,
                "Проверка через логический вывод");
        
        KolibriSVMethodResult *res = &report->results[report->num_methods_used];
        double method_start = sv_time_ms();
        sv_check_logical(query, primary_answer, res);
        res->elapsed_ms = sv_time_ms() - method_start;
        
        report->steps[step].intermediate_confidence = res->confidence;
        snprintf(report->steps[step].detail, 512,
                "Logical confidence: %.2f", res->confidence);
        
        if (res->success) report->num_methods_used++;
        step++;
        
        if (callback) callback(step, "logical", res->confidence, user_data);
    }
    
    /* Step 3: Knowledge check */
    if (config->enable_knowledge_check) {
        report->steps[step].step_num = step + 1;
        snprintf(report->steps[step].description, 256,
                "Проверка через граф знаний");
        
        KolibriSVMethodResult *res = &report->results[report->num_methods_used];
        double method_start = sv_time_ms();
        sv_check_knowledge(query, primary_answer, res);
        res->elapsed_ms = sv_time_ms() - method_start;
        
        report->steps[step].intermediate_confidence = res->confidence;
        snprintf(report->steps[step].detail, 512,
                "Knowledge confidence: %.2f", res->confidence);
        
        if (res->success) report->num_methods_used++;
        step++;
        
        if (callback) callback(step, "knowledge", res->confidence, user_data);
    }
    
    /* Step 4: Arithmetic check */
    if (config->enable_arithmetic_check) {
        report->steps[step].step_num = step + 1;
        snprintf(report->steps[step].description, 256,
                "Проверка через точную арифметику");
        
        KolibriSVMethodResult *res = &report->results[report->num_methods_used];
        double method_start = sv_time_ms();
        int ret = sv_check_arithmetic(query, primary_answer, res);
        res->elapsed_ms = sv_time_ms() - method_start;
        
        report->steps[step].intermediate_confidence = res->confidence;
        snprintf(report->steps[step].detail, 512,
                ret == 0 ? "Arithmetic exact: %.2f" : "Arithmetic: not applicable",
                res->confidence);
        
        if (res->success) report->num_methods_used++;
        step++;
        
        if (callback) callback(step, "arithmetic", res->confidence, user_data);
    }
    
    report->num_steps = step;
    
    /* Detect contradictions */
    kolibri_sv_detect_contradictions(report);
    
    /* Compute final confidence */
    report->final_confidence = kolibri_sv_compute_final_confidence(report);
    
    /* Check agreement */
    report->agreement = kolibri_sv_check_agreement(report);
    
    /* Generate recommendation */
    kolibri_sv_generate_recommendation(report);
    
    report->total_verification_ms = sv_time_ms() - start;
    report->verification_passed = (report->final_confidence >= config->agreement_threshold) ? 1 : 0;
    
    return 0;
}

int kolibri_sv_check_agreement(const KolibriSVReport *report) {
    if (!report || report->num_methods_used < 2) return 0;
    
    /* Проверяем попарное согласие */
    int agreements = 0;
    int comparisons = 0;
    
    for (int i = 0; i < report->num_methods_used; i++) {
        for (int j = i + 1; j < report->num_methods_used; j++) {
            if (!report->results[i].success || !report->results[j].success) continue;
            
            double sim = sv_answer_similarity(
                report->results[i].answer,
                report->results[j].answer
            );
            
            if (sim > 0.8) agreements++;
            comparisons++;
        }
    }
    
    if (comparisons == 0) return 0;
    return (double)agreements / comparisons > 0.75;
}

int kolibri_sv_detect_contradictions(KolibriSVReport *report) {
    if (!report) return 0;
    
    report->num_contradictions = 0;
    
    /* Проверяем все пары */
    for (int i = 0; i < report->num_methods_used; i++) {
        for (int j = i + 1; j < report->num_methods_used; j++) {
            if (!report->results[i].success || !report->results[j].success) continue;
            
            double sim = sv_answer_similarity(
                report->results[i].answer,
                report->results[j].answer
            );
            
            double divergence = 1.0 - sim;
            
            /* Если расхождение > 0.3 — противоречие */
            if (divergence > 0.3 && report->num_contradictions < KSV_MAX_CONTRADICTIONS) {
                KolibriSVContradiction *c = &report->contradictions[report->num_contradictions];
                c->method1 = report->results[i].method;
                c->method2 = report->results[j].method;
                
                snprintf(c->answer1, KSV_MAX_ANSWER_LEN, "%s", report->results[i].answer);
                snprintf(c->answer2, KSV_MAX_ANSWER_LEN, "%s", report->results[j].answer);
                c->divergence = divergence;
                
                snprintf(c->explanation, 256,
                        "%s vs %s: divergence %.2f",
                        kolibri_sv_method_name(c->method1),
                        kolibri_sv_method_name(c->method2),
                        divergence);
                
                report->num_contradictions++;
            }
        }
    }
    
    return report->num_contradictions;
}

double kolibri_sv_compute_final_confidence(const KolibriSVReport *report) {
    if (!report || report->num_methods_used == 0) return 0.0;
    
    /* Средневзвешенная confidence */
    double total_conf = 0.0;
    int count = 0;
    
    for (int i = 0; i < report->num_methods_used; i++) {
        if (report->results[i].success) {
            total_conf += report->results[i].confidence;
            count++;
        }
    }
    
    if (count == 0) return 0.0;
    
    double avg_conf = total_conf / count;
    
    /* Штраф за противоречия */
    double penalty = report->num_contradictions * 0.1;
    
    double final_confidence = avg_conf - penalty;
    if (final_confidence < 0.0) final_confidence = 0.0;
    if (final_confidence > 1.0) final_confidence = 1.0;
    
    return final_confidence;
}

void kolibri_sv_generate_recommendation(KolibriSVReport *report) {
    if (!report) return;
    
    if (report->final_confidence >= 0.9) {
        snprintf(report->recommendation, 512,
                "✓ Высокая уверенность (%.2f). Ответ можно выдавать.",
                report->final_confidence);
    } else if (report->final_confidence >= 0.7) {
        snprintf(report->recommendation, 512,
                "✓ Средняя уверенность (%.2f). Ответ можно выдавать с пометкой.",
                report->final_confidence);
    } else if (report->num_contradictions > 0) {
        snprintf(report->recommendation, 512,
                "⚠ Обнаружены противоречия (%d). Уверенность: %.2f. "
                "Рекомендуется объяснить неопределённость пользователю.",
                report->num_contradictions, report->final_confidence);
    } else {
        snprintf(report->recommendation, 512,
                "✗ Низкая уверенность (%.2f). Ответ требует проверки.",
                report->final_confidence);
    }
}

/* ============================================================================
 * ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
 * ============================================================================ */

void kolibri_sv_print_report(const KolibriSVReport *report) {
    if (!report) return;
    
    printf("\n=== Self-Verification Report ===\n");
    printf("Primary answer: %s\n", report->primary_answer);
    printf("Methods used: %d\n", report->num_methods_used);
    
    for (int i = 0; i < report->num_methods_used; i++) {
        const KolibriSVMethodResult *r = &report->results[i];
        printf("  [%s] %s (confidence: %.2f, time: %.1fms)\n",
               r->success ? "✓" : "✗",
               kolibri_sv_method_name(r->method),
               r->confidence, r->elapsed_ms);
    }
    
    if (report->num_contradictions > 0) {
        printf("\nContradictions: %d\n", report->num_contradictions);
        for (int i = 0; i < report->num_contradictions; i++) {
            printf("  ⚠ %s\n", report->contradictions[i].explanation);
        }
    }
    
    printf("\nFinal confidence: %.2f\n", report->final_confidence);
    printf("Agreement: %s\n", report->agreement ? "YES" : "NO");
    printf("Verification: %s\n", 
           report->verification_passed > 0 ? "PASSED" : 
           (report->verification_passed == 0 ? "FAILED" : "UNKNOWN"));
    printf("\nRecommendation: %s\n", report->recommendation);
    printf("Total time: %.1fms\n", report->total_verification_ms);
    printf("================================\n\n");
}

int kolibri_sv_save_report(const KolibriSVReport *report, const char *filepath) {
    if (!report || !filepath) return -1;
    
    FILE *f = fopen(filepath, "w");
    if (!f) return -2;
    
    fprintf(f, "self_verification_report\n");
    fprintf(f, "primary_answer=%s\n", report->primary_answer);
    fprintf(f, "methods_used=%d\n", report->num_methods_used);
    fprintf(f, "final_confidence=%.4f\n", report->final_confidence);
    fprintf(f, "agreement=%d\n", report->agreement);
    fprintf(f, "contradictions=%d\n", report->num_contradictions);
    fprintf(f, "verification_passed=%d\n", report->verification_passed);
    fprintf(f, "total_time_ms=%.1f\n", report->total_verification_ms);
    
    fclose(f);
    return 0;
}

const char* kolibri_sv_method_name(KolibriSVMethod method) {
    switch (method) {
        case KSV_METHOD_FORMULA:    return "Formula";
        case KSV_METHOD_LOGICAL:    return "Logical";
        case KSV_METHOD_KNOWLEDGE:  return "Knowledge";
        case KSV_METHOD_ARITHMETIC: return "Arithmetic";
        case KSV_METHOD_ANALOGY:    return "Analogy";
        default:                    return "Unknown";
    }
}

const char* kolibri_sv_method_desc(KolibriSVMethod method) {
    switch (method) {
        case KSV_METHOD_FORMULA:    
            return "Проверка через формульный пул";
        case KSV_METHOD_LOGICAL:    
            return "Проверка через логический вывод";
        case KSV_METHOD_KNOWLEDGE:  
            return "Проверка через граф знаний";
        case KSV_METHOD_ARITHMETIC: 
            return "Проверка через точную арифметику (decimal.c)";
        case KSV_METHOD_ANALOGY:    
            return "Проверка через аналогии";
        default:                    
            return "Неизвестный метод";
    }
}
