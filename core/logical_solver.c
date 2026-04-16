/*
 * logical_solver.c
 *
 * Реализация настоящего логического solver для Kolibri
 *
 * Алгоритм:
 *   1. Инициализация: все possibilities = 1 (все возможно)
 *   2. Применяем constraints: исключаем невозможное
 *   3. Constraint propagation: если entity имеет 1 possibility → фиксируем,
 *      исключаем это значение из других entities
 *   4. Повторяем пока есть изменения или решено
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/logical_solver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * ВНУТРЕННИЕ ФУНКЦИИ
 * ============================================================================ */

static double ls_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static void ls_add_step(KolibriLSSolution *sol, const char *desc,
                       const char *detail, double conf, int changed) {
    if (sol->steps_count >= KLS_MAX_STEPS) return;
    KolibriLSStep *s = &sol->steps[sol->steps_count];
    s->step_num = sol->steps_count + 1;
    strncpy(s->description, desc, KLS_MAX_TEXT - 1);
    strncpy(s->detail, detail, KLS_MAX_TEXT - 1);
    s->confidence = conf;
    s->entities_changed = changed;
    sol->steps_count++;
}

/* Исключить значение для entity */
static int ls_exclude(KolibriLogicalSolver *ls, int domain, int entity, int value,
                     const char *reason, KolibriLSSolution *sol, int *changed_count) {
    if (ls->possible[domain][entity][value] == 0) return 0;  /* Уже исключено */
    
    ls->possible[domain][entity][value] = 0;
    if (changed_count) (*changed_count)++;
    
    if (sol) {
        char detail[KLS_MAX_TEXT];
        snprintf(detail, sizeof(detail), "%s: [%s][%d] ≠ %s",
                reason, ls->domain_names[domain], entity,
                ls->domain_values[domain][value]);
        ls_add_step(sol, "Исключение", detail, 0.95, 1);
    }
    
    return 1;
}

/* Зафиксировать значение для entity */
static int ls_fix_value(KolibriLogicalSolver *ls, int domain, int entity, int value,
                       const char *reason, KolibriLSSolution *sol, int *changed_count) {
    int domain_size = ls->domain_sizes[domain];
    int changed = 0;
    
    for (int v = 0; v < domain_size; v++) {
        if (v != value) {
            changed += ls_exclude(ls, domain, entity, v, reason, sol, changed_count);
        }
    }
    
    if (sol && changed > 0) {
        char detail[KLS_MAX_TEXT];
        snprintf(detail, sizeof(detail), "%s: [%s][%d] = %s",
                reason, ls->domain_names[domain], entity,
                ls->domain_values[domain][value]);
        ls_add_step(sol, "Фиксация значения", detail, 0.98, changed);
    }
    
    return changed;
}

/* Constraint propagation: если у entity 1 possibility → зафиксировать */
static int ls_propagate_single(KolibriLogicalSolver *ls, KolibriLSSolution *sol,
                              int *changed_count) {
    int total_changed = 0;
    
    for (int d = 0; d < ls->num_domains; d++) {
        int domain_size = ls->domain_sizes[d];
        
        for (int e = 0; e < domain_size; e++) {
            /* Считаем сколько possibilities осталось */
            int count = 0;
            int last_value = -1;
            
            for (int v = 0; v < domain_size; v++) {
                if (ls->possible[d][e][v]) {
                    count++;
                    last_value = v;
                }
            }
            
            /* Если 1 possibility → фиксируем */
            if (count == 1 && last_value >= 0 && ls->values[d][e] == KLS_VAL_UNKNOWN) {
                ls->values[d][e] = last_value;
                
                /* Исключаем это значение из других entities того же домена */
                for (int e2 = 0; e2 < domain_size; e2++) {
                    if (e2 != e) {
                        total_changed += ls_exclude(ls, d, e2, last_value,
                            "Constraint propagation (all-different)", sol, changed_count);
                    }
                }
                
                total_changed++;
            }
        }
    }
    
    return total_changed;
}

/* Propagate: если значение зафиксировано для одного entity, исключить из других */
static int ls_propagate_fixed(KolibriLogicalSolver *ls, KolibriLSSolution *sol,
                             int *changed_count) {
    int total_changed = 0;
    
    for (int d = 0; d < ls->num_domains; d++) {
        int domain_size = ls->domain_sizes[d];
        
        for (int e = 0; e < domain_size; e++) {
            if (ls->values[d][e] != KLS_VAL_UNKNOWN) {
                int val = ls->values[d][e];
                
                for (int e2 = 0; e2 < domain_size; e2++) {
                    if (e2 != e) {
                        total_changed += ls_exclude(ls, d, e2, val,
                            "Constraint propagation (fixed)", sol, changed_count);
                    }
                }
            }
        }
    }
    
    return total_changed;
}

/* Применить all-different constraint */
static int ls_apply_all_different(KolibriLogicalSolver *ls, int domain,
                                  KolibriLSSolution *sol, int *changed_count) {
    int domain_size = ls->domain_sizes[domain];
    int total_changed = 0;
    
    /* Для каждого значения: если оно зафиксировано для одного entity,
       исключить из остальных */
    for (int v = 0; v < domain_size; v++) {
        int fixed_entity = -1;
        
        for (int e = 0; e < domain_size; e++) {
            if (ls->values[domain][e] == v) {
                fixed_entity = e;
                break;
            }
        }
        
        /* Если значение зафиксировано для какого-то entity */
        if (fixed_entity >= 0) {
            for (int e = 0; e < domain_size; e++) {
                if (e != fixed_entity) {
                    total_changed += ls_exclude(ls, domain, e, v,
                        "All-different constraint", sol, changed_count);
                }
            }
        }
        
        /* Если значение возможно только для одного entity → фиксируем */
        int possible_count = 0;
        int possible_entity = -1;
        
        for (int e = 0; e < domain_size; e++) {
            if (ls->possible[domain][e][v] && ls->values[domain][e] == KLS_VAL_UNKNOWN) {
                possible_count++;
                possible_entity = e;
            }
        }
        
        if (possible_count == 1 && possible_entity >= 0) {
            total_changed += ls_fix_value(ls, domain, possible_entity, v,
                "All-different: единственное possible значение",
                sol, changed_count);
        }
    }
    
    return total_changed;
}

/* Проверить решена ли задача */
static int ls_is_solved(const KolibriLogicalSolver *ls) {
    for (int d = 0; d < ls->num_domains; d++) {
        int domain_size = ls->domain_sizes[d];
        
        for (int e = 0; e < domain_size; e++) {
            if (ls->values[d][e] == KLS_VAL_UNKNOWN) {
                /* Проверяем осталась ли 1 possibility */
                int count = 0;
                int last_v = -1;
                for (int v = 0; v < domain_size; v++) {
                    if (ls->possible[d][e][v]) { count++; last_v = v; }
                }
                if (count == 1) {
                    /* Фиксируем! */
                    ((KolibriLogicalSolver *)ls)->values[d][e] = last_v;
                } else if (count != 1) {
                    return 0;  /* Не решено */
                }
            }
        }
    }
    return 1;  /* Решено */
}

/* Проверить противоречие */
static int ls_is_contradiction(const KolibriLogicalSolver *ls) {
    for (int d = 0; d < ls->num_domains; d++) {
        int domain_size = ls->domain_sizes[d];
        
        for (int e = 0; e < domain_size; e++) {
            int count = 0;
            for (int v = 0; v < domain_size; v++) {
                if (ls->possible[d][e][v]) count++;
            }
            if (count == 0) return 1;  /* Нет possibilities → противоречие */
        }
    }
    return 0;
}

/* ============================================================================
 * API РЕАЛИЗАЦИЯ
 * ============================================================================ */

int kolibri_ls_init(KolibriLogicalSolver *ls) {
    if (!ls) return -1;
    
    memset(ls, 0, sizeof(KolibriLogicalSolver));
    ls->num_domains = 0;
    ls->num_constraints = 0;
    
    for (int d = 0; d < KLS_MAX_DOMAINS; d++) {
        for (int e = 0; e < KLS_MAX_DOMAIN_SIZE; e++) {
            ls->values[d][e] = KLS_VAL_UNKNOWN;
        }
    }
    
    memset(&ls->solution, 0, sizeof(KolibriLSSolution));
    
    return 0;
}

int kolibri_ls_add_domain(KolibriLogicalSolver *ls,
                         const char *domain,
                         const char **values,
                         int num_values) {
    if (!ls || !domain || !values || num_values <= 0) return -1;
    if (ls->num_domains >= KLS_MAX_DOMAINS) return -2;
    if (num_values > KLS_MAX_DOMAIN_SIZE) return -3;
    
    int d = ls->num_domains;
    strncpy(ls->domain_names[d], domain, KLS_MAX_TEXT - 1);
    ls->domain_sizes[d] = num_values;
    
    for (int i = 0; i < num_values; i++) {
        strncpy(ls->domain_values[d][i], values[i], KLS_MAX_TEXT - 1);
        ls->values[d][i] = KLS_VAL_UNKNOWN;
    }
    
    /* Все cross-possibilities = 1 по умолчанию (всё возможно) */
    for (int e = 0; e < num_values; e++) {
        for (int v = 0; v < num_values; v++) {
            ls->possible[d][e][v] = 1;
        }
    }
    
    ls->num_domains++;
    return 0;
}

int kolibri_ls_add_not(KolibriLogicalSolver *ls,
                      int domain, int entity_idx, int value_idx,
                      const char *reason) {
    if (!ls || domain < 0 || domain >= ls->num_domains) return -1;
    if (entity_idx < 0 || entity_idx >= ls->domain_sizes[domain]) return -2;
    if (value_idx < 0 || value_idx >= ls->domain_sizes[domain]) return -3;
    
    ls->possible[domain][entity_idx][value_idx] = 0;
    
    if (ls->num_constraints < KLS_MAX_CONSTRAINTS) {
        KolibriLSConstraint *c = &ls->constraints[ls->num_constraints++];
        c->type = KLS_CONSTRAINT_NOT;
        c->domain = domain;
        c->entity_idx = entity_idx;
        c->value_idx = value_idx;
        if (reason) strncpy(c->reason, reason, KLS_MAX_TEXT - 1);
    }
    
    return 0;
}

int kolibri_ls_add_equals(KolibriLogicalSolver *ls,
                         int domain, int entity_idx, int value_idx,
                         const char *reason) {
    if (!ls || domain < 0 || domain >= ls->num_domains) return -1;
    
    int domain_size = ls->domain_sizes[domain];
    
    /* Исключаем все другие значения */
    for (int v = 0; v < domain_size; v++) {
        if (v != value_idx) {
            ls->possible[domain][entity_idx][v] = 0;
        }
    }
    
    /* Исключаем это значение из других entities */
    for (int e = 0; e < domain_size; e++) {
        if (e != entity_idx) {
            ls->possible[domain][e][value_idx] = 0;
        }
    }
    
    ls->values[domain][entity_idx] = value_idx;
    
    if (ls->num_constraints < KLS_MAX_CONSTRAINTS) {
        KolibriLSConstraint *c = &ls->constraints[ls->num_constraints++];
        c->type = KLS_CONSTRAINT_EQUALS;
        c->domain = domain;
        c->entity_idx = entity_idx;
        c->value_idx = value_idx;
        if (reason) strncpy(c->reason, reason, KLS_MAX_TEXT - 1);
    }
    
    return 0;
}

int kolibri_ls_add_all_different(KolibriLogicalSolver *ls,
                                int domain,
                                const char *reason) {
    if (!ls || domain < 0 || domain >= ls->num_domains) return -1;
    
    if (ls->num_constraints < KLS_MAX_CONSTRAINTS) {
        KolibriLSConstraint *c = &ls->constraints[ls->num_constraints++];
        c->type = KLS_CONSTRAINT_ALL_DIFFERENT;
        c->domain = domain;
        if (reason) strncpy(c->reason, reason, KLS_MAX_TEXT - 1);
    }
    
    return 0;
}

int kolibri_ls_add_implies(KolibriLogicalSolver *ls,
                          int domain1, int entity1, int value1,
                          int domain2, int entity2, int value2,
                          const char *reason) {
    if (!ls) return -1;
    
    if (ls->num_constraints < KLS_MAX_CONSTRAINTS) {
        KolibriLSConstraint *c = &ls->constraints[ls->num_constraints++];
        c->type = KLS_CONSTRAINT_IMPLIES;
        c->domain = domain1;
        c->entity_idx = entity1;
        c->value_idx = value1;
        c->entity2_idx = entity2;
        c->value2_idx = value2;
        c->domain2 = domain2;
        if (reason) strncpy(c->reason, reason, KLS_MAX_TEXT - 1);
    }
    
    return 0;
}

int kolibri_ls_solve(KolibriLogicalSolver *ls, KolibriLSSolution *solution) {
    if (!ls || !solution) return -1;
    
    double start = ls_time_ms();
    memset(solution, 0, sizeof(KolibriLSSolution));
    
    int iteration = 0;
    int total_changed = 1;
    
    while (total_changed > 0 && !ls_is_solved(ls) && !ls_is_contradiction(ls)) {
        total_changed = 0;
        iteration++;
        
        if (iteration > 100) {
            /* Защита от бесконечного цикла */
            break;
        }
        
        char iter_desc[64];
        snprintf(iter_desc, sizeof(iter_desc), "Итерация %d", iteration);
        
        /* 1. Propagate single possibilities */
        int changed = 0;
        total_changed += ls_propagate_single(ls, solution, &changed);
        if (changed > 0) {
            ls_add_step(solution, iter_desc,
                       "Propagate: entity с 1 possibility → fixed",
                       0.90, changed);
        }
        
        /* 2. Propagate fixed values */
        changed = 0;
        total_changed += ls_propagate_fixed(ls, solution, &changed);
        if (changed > 0) {
            ls_add_step(solution, iter_desc,
                       "Propagate: fixed value → exclude from others",
                       0.90, changed);
        }
        
        /* 3. Apply all-different constraints */
        for (int d = 0; d < ls->num_domains; d++) {
            changed = 0;
            total_changed += ls_apply_all_different(ls, d, solution, &changed);
            if (changed > 0) {
                char detail[KLS_MAX_TEXT];
                snprintf(detail, sizeof(detail), "All-different для домена '%s'",
                        ls->domain_names[d]);
                ls_add_step(solution, iter_desc, detail, 0.90, changed);
            }
        }
    }
    
    /* 4. Если не решено — пробуем case analysis (proof by cases) */
    if (!ls_is_solved(ls) && !ls_is_contradiction(ls) && iteration <= 100) {
        /* Находим entity с минимальным количеством possibilities (>1) */
        int best_domain = -1, best_entity = -1, best_count = 999;
        
        for (int d = 0; d < ls->num_domains; d++) {
            for (int e = 0; e < ls->domain_sizes[d]; e++) {
                if (ls->values[d][e] == KLS_VAL_UNKNOWN) {
                    int count = 0;
                    for (int v = 0; v < ls->domain_sizes[d]; v++) {
                        if (ls->possible[d][e][v]) count++;
                    }
                    if (count > 1 && count < best_count) {
                        best_count = count;
                        best_domain = d;
                        best_entity = e;
                    }
                }
            }
        }
        
        /* Case analysis: пробуем каждое possible значение */
        if (best_domain >= 0 && best_entity >= 0) {
            ls_add_step(solution, "Case analysis",
                       "Прямой вывод невозможен, пробуем варианты",
                       0.80, 0);
            
            int domain_size = ls->domain_sizes[best_domain];
            
            for (int v = 0; v < domain_size; v++) {
                if (!ls->possible[best_domain][best_entity][v]) continue;
                
                /* Создаём копию solver для этого case */
                KolibriLogicalSolver backup = *ls;
                memcpy(&backup.values, ls->values, sizeof(ls->values));
                memcpy(&backup.possible, ls->possible, sizeof(ls->possible));
                
                /* Фиксируем значение */
                int changed = 0;
                ls_fix_value(&backup, best_domain, best_entity, v,
                           "Case analysis: пробуем вариант",
                           NULL, &changed);
                
                /* Propagate на копии */
                int sub_iter = 0;
                int sub_changed = 1;
                int sub_contradiction = 0;
                
                while (sub_changed > 0 && sub_iter < 50) {
                    sub_changed = 0;
                    sub_iter++;
                    sub_changed += ls_propagate_single(&backup, NULL, NULL);
                    sub_changed += ls_propagate_fixed(&backup, NULL, NULL);
                    for (int d = 0; d < backup.num_domains; d++) {
                        sub_changed += ls_apply_all_different(&backup, d, NULL, NULL);
                    }
                    
                    /* Проверяем противоречие */
                    for (int d = 0; d < backup.num_domains; d++) {
                        for (int e = 0; e < backup.domain_sizes[d]; e++) {
                            int cnt = 0;
                            for (int vv = 0; vv < backup.domain_sizes[d]; vv++) {
                                if (backup.possible[d][e][vv]) cnt++;
                            }
                            if (cnt == 0) sub_contradiction = 1;
                        }
                    }
                    if (sub_contradiction) break;
                }
                
                if (sub_contradiction) {
                    /* Этот вариант ведёт к противоречию → исключаем */
                    ls_exclude(ls, best_domain, best_entity, v,
                             "Case analysis: ведёт к противоречию",
                             solution, NULL);
                    
                    char detail[KLS_MAX_TEXT];
                    snprintf(detail, sizeof(detail),
                            "Вариант [%s][%d]=%s → противоречие через %d шагов",
                            ls->domain_names[best_domain], best_entity,
                            ls->domain_values[best_domain][v], sub_iter);
                    ls_add_step(solution, "Case analysis — исключение",
                               detail, 0.90, 1);
                }
            }
            
            /* После case analysis — ещё одна волна propagation */
            total_changed = 1;
            iteration = 0;
            while (total_changed > 0 && !ls_is_solved(ls) && !ls_is_contradiction(ls)) {
                total_changed = 0;
                iteration++;
                if (iteration > 100) break;
                
                total_changed += ls_propagate_single(ls, solution, NULL);
                total_changed += ls_propagate_fixed(ls, solution, NULL);
                for (int d = 0; d < ls->num_domains; d++) {
                    total_changed += ls_apply_all_different(ls, d, solution, NULL);
                }
            }
        }
    }
    
    /* Формируем ответ */
    int solved = ls_is_solved(ls);
    int contradiction = ls_is_contradiction(ls);
    
    solution->solved = solved;
    solution->confidence = solved ? 1.0 : (contradiction ? 0.0 : 0.5);
    
    if (contradiction) {
        strncpy(solution->answer, "Противоречие в условии задачи!", KLS_MAX_TEXT * 4 - 1);
    } else if (solved) {
        /* Формируем читаемый ответ */
        char *p = solution->answer;
        size_t remaining = KLS_MAX_TEXT * 4;
        int written;
        
        for (int d = 0; d < ls->num_domains; d++) {
            written = snprintf(p, remaining, "Домен '%s':\n", ls->domain_names[d]);
            p += written; remaining -= written;
            
            for (int e = 0; e < ls->domain_sizes[d]; e++) {
                int val = ls->values[d][e];
                if (val >= 0) {
                    written = snprintf(p, remaining, "  %d → %s\n",
                                     e + 1, ls->domain_values[d][val]);
                    p += written; remaining -= written;
                } else {
                    /* Проверяем possibilities */
                    int count = 0;
                    int last_v = -1;
                    for (int v = 0; v < ls->domain_sizes[d]; v++) {
                        if (ls->possible[d][e][v]) { count++; last_v = v; }
                    }
                    if (count == 1 && last_v >= 0) {
                        written = snprintf(p, remaining, "  %d → %s\n",
                                         e + 1, ls->domain_values[d][last_v]);
                        p += written; remaining -= written;
                    } else {
                        written = snprintf(p, remaining, "  %d → ??? (%d вариантов)\n",
                                         e + 1, count);
                        p += written; remaining -= written;
                    }
                }
            }
        }
    } else {
        /* Не полностью решено */
        char *p = solution->answer;
        size_t remaining = KLS_MAX_TEXT * 4;
        int written;
        
        written = snprintf(p, remaining, "Частичное решение:\n");
        p += written; remaining -= written;
        
        for (int d = 0; d < ls->num_domains; d++) {
            for (int e = 0; e < ls->domain_sizes[d]; e++) {
                int val = ls->values[d][e];
                if (val >= 0) {
                    written = snprintf(p, remaining, "  [%s][%d] = %s\n",
                                     ls->domain_names[d], e + 1,
                                     ls->domain_values[d][val]);
                    p += written; remaining -= written;
                }
            }
        }
    }
    
    /* Summary */
    snprintf(solution->reasoning_summary, KLS_MAX_TEXT * 2 - 1,
            "Решено за %d итераций, %d шагов вывода, %d constraints",
            iteration, solution->steps_count, ls->num_constraints);
    
    ls->solve_time_ms = ls_time_ms() - start;
    ls->solution = *solution;
    
    return solved ? 0 : (contradiction ? -1 : 1);
}

void kolibri_ls_print_solution(const KolibriLSSolution *sol) {
    if (!sol) return;
    
    printf("\n=== Решение ===\n");
    printf("Статус: %s\n", sol->solved ? "РЕШЕНО" : "НЕ РЕШЕНО");
    printf("\nОтвет:\n%s\n", sol->answer);
    
    printf("\nШаги вывода (%d):\n", sol->steps_count);
    for (int i = 0; i < sol->steps_count; i++) {
        const KolibriLSStep *s = &sol->steps[i];
        printf("  %d. %s\n", s->step_num, s->description);
        if (strlen(s->detail) > 0 && strcmp(s->detail, s->description) != 0) {
            printf("     %s\n", s->detail);
        }
    }
    
    printf("\nУверенность: %.0f%%\n", sol->confidence * 100);
    printf("%s\n", sol->reasoning_summary);
    printf("==============\n\n");
}

void kolibri_ls_print_grid(const KolibriLogicalSolver *ls) {
    if (!ls) return;
    
    for (int d = 0; d < ls->num_domains; d++) {
        printf("\nДомен: %s\n", ls->domain_names[d]);
        printf("     ");
        for (int v = 0; v < ls->domain_sizes[d]; v++) {
            printf(" %3d", v + 1);
        }
        printf("\n");
        
        for (int e = 0; e < ls->domain_sizes[d]; e++) {
            printf("  %2d:", e + 1);
            for (int v = 0; v < ls->domain_sizes[d]; v++) {
                if (ls->possible[d][e][v]) {
                    printf("   ✓");
                } else {
                    printf("   ✗");
                }
            }
            printf("\n");
        }
    }
}
