/*
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 * 
 * Модуль трассировки "Стеклянный Разум"
 * Glass Mind Tracing Module - Implementation
 * 
 * Реализует инвариант прозрачности: отслеживание каждой цифры
 * от источника до вывода через визуализируемый граф зависимостей.
 */

#include "kolibri/trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * Вспомогательные константы и макросы
 * ============================================================================ */

#define KT_INITIAL_NODE_CAPACITY 256
#define KT_INITIAL_EDGE_CAPACITY 1024
#define KT_JSON_INITIAL_SIZE     8192

/* --- Получение текущего времени в миллисекундах --- */
static uint64_t kt_current_time_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
    }
    return 0;
}

/* --- Безопасное копирование строки --- */
static void kt_safe_strcpy(char *dst, const char *src, size_t dst_size) {
    if (!dst || dst_size == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t len = strlen(src);
    if (len >= dst_size) {
        len = dst_size - 1;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* ============================================================================
 * Названия типов (для отладки и экспорта)
 * ============================================================================ */

static const char *kt_node_type_names[KOLIBRI_TRACE_NODE_COUNT] = {
    "input",
    "digit",
    "formula",
    "mutation",
    "crossover",
    "transform",
    "aggregate",
    "output",
    "intermediate"
};

static const char *kt_edge_type_names[KOLIBRI_TRACE_EDGE_COUNT] = {
    "flow",
    "dependency",
    "mutation",
    "reference",
    "causal"
};

const char *kt_node_type_name(KolibriTraceNodeType type) {
    if (type < 0 || type >= KOLIBRI_TRACE_NODE_COUNT) {
        return "unknown";
    }
    return kt_node_type_names[type];
}

const char *kt_edge_type_name(KolibriTraceEdgeType type) {
    if (type < 0 || type >= KOLIBRI_TRACE_EDGE_COUNT) {
        return "unknown";
    }
    return kt_edge_type_names[type];
}

/* ============================================================================
 * Внутренние функции управления памятью
 * ============================================================================ */

/* --- Расширение массива узлов --- */
static int kt_ensure_node_capacity(KolibriTraceSession *session, size_t needed) {
    if (!session) return -1;
    
    KolibriTraceGraph *graph = &session->graph;
    if (graph->node_count + needed <= graph->node_capacity) {
        return 0;  /* Достаточно места */
    }
    
    size_t new_capacity = graph->node_capacity * 2;
    if (new_capacity < graph->node_count + needed) {
        new_capacity = graph->node_count + needed;
    }
    if (new_capacity > KOLIBRI_TRACE_MAX_NODES) {
        new_capacity = KOLIBRI_TRACE_MAX_NODES;
        if (graph->node_count + needed > new_capacity) {
            return -1;  /* Превышен лимит */
        }
    }
    
    KolibriTraceNode *new_nodes = realloc(graph->nodes, 
                                          new_capacity * sizeof(KolibriTraceNode));
    if (!new_nodes) {
        return -1;
    }
    
    graph->nodes = new_nodes;
    graph->node_capacity = new_capacity;
    return 0;
}

/* --- Расширение массива рёбер --- */
static int kt_ensure_edge_capacity(KolibriTraceSession *session, size_t needed) {
    if (!session) return -1;
    
    KolibriTraceGraph *graph = &session->graph;
    if (graph->edge_count + needed <= graph->edge_capacity) {
        return 0;
    }
    
    size_t new_capacity = graph->edge_capacity * 2;
    if (new_capacity < graph->edge_count + needed) {
        new_capacity = graph->edge_count + needed;
    }
    if (new_capacity > KOLIBRI_TRACE_MAX_EDGES) {
        new_capacity = KOLIBRI_TRACE_MAX_EDGES;
        if (graph->edge_count + needed > new_capacity) {
            return -1;
        }
    }
    
    KolibriTraceEdge *new_edges = realloc(graph->edges,
                                          new_capacity * sizeof(KolibriTraceEdge));
    if (!new_edges) {
        return -1;
    }
    
    graph->edges = new_edges;
    graph->edge_capacity = new_capacity;
    return 0;
}

/* --- Добавление узла в граф --- */
static uint32_t kt_add_node(KolibriTraceSession *session,
                            KolibriTraceNodeType type,
                            const char *label,
                            const char *source) {
    if (!session || !session->active) {
        return 0;
    }
    
    if (kt_ensure_node_capacity(session, 1) != 0) {
        return 0;
    }
    
    KolibriTraceGraph *graph = &session->graph;
    KolibriTraceNode *node = &graph->nodes[graph->node_count];
    
    memset(node, 0, sizeof(KolibriTraceNode));
    node->id = session->next_node_id++;
    node->type = type;
    node->timestamp = kt_current_time_ms();
    node->contribution_weight = 1.0;
    node->depth = (uint32_t)session->stack_depth;
    
    if (label) {
        kt_safe_strcpy(node->label, label, sizeof(node->label));
    }
    if (source) {
        kt_safe_strcpy(node->source, source, sizeof(node->source));
    }
    
    graph->node_count++;
    return node->id;
}

/* --- Добавление ребра в граф --- */
static uint32_t kt_add_edge(KolibriTraceSession *session,
                            uint32_t source_id,
                            uint32_t target_id,
                            KolibriTraceEdgeType type,
                            double weight,
                            const char *operation) {
    if (!session || !session->active) {
        return 0;
    }
    
    if (kt_ensure_edge_capacity(session, 1) != 0) {
        return 0;
    }
    
    KolibriTraceGraph *graph = &session->graph;
    KolibriTraceEdge *edge = &graph->edges[graph->edge_count];
    
    memset(edge, 0, sizeof(KolibriTraceEdge));
    edge->id = session->next_edge_id++;
    edge->source_id = source_id;
    edge->target_id = target_id;
    edge->type = type;
    edge->weight = weight;
    edge->timestamp = kt_current_time_ms();
    
    if (operation) {
        kt_safe_strcpy(edge->operation, operation, sizeof(edge->operation));
    }
    
    graph->edge_count++;
    
    /* Увеличиваем счётчик ссылок целевого узла */
    for (size_t i = 0; i < graph->node_count; i++) {
        if (graph->nodes[i].id == target_id) {
            graph->nodes[i].reference_count++;
            break;
        }
    }
    
    return edge->id;
}

/* --- Поиск узла по ID --- */
static KolibriTraceNode *kt_find_node_mut(KolibriTraceSession *session, uint32_t node_id) {
    if (!session) return NULL;
    
    KolibriTraceGraph *graph = &session->graph;
    for (size_t i = 0; i < graph->node_count; i++) {
        if (graph->nodes[i].id == node_id) {
            return &graph->nodes[i];
        }
    }
    return NULL;
}

/* ============================================================================
 * Функции управления сессией
 * ============================================================================ */

int kt_session_begin(KolibriTraceSession *session) {
    if (!session) {
        return -1;
    }
    
    /* Очищаем структуру */
    memset(session, 0, sizeof(KolibriTraceSession));
    
    /* Выделяем память для узлов */
    session->graph.nodes = malloc(KT_INITIAL_NODE_CAPACITY * sizeof(KolibriTraceNode));
    if (!session->graph.nodes) {
        return -1;
    }
    session->graph.node_capacity = KT_INITIAL_NODE_CAPACITY;
    
    /* Выделяем память для рёбер */
    session->graph.edges = malloc(KT_INITIAL_EDGE_CAPACITY * sizeof(KolibriTraceEdge));
    if (!session->graph.edges) {
        free(session->graph.nodes);
        session->graph.nodes = NULL;
        return -1;
    }
    session->graph.edge_capacity = KT_INITIAL_EDGE_CAPACITY;
    
    /* Выделяем память для индексов входов/выходов */
    session->graph.input_nodes = malloc(KOLIBRI_TRACE_MAX_DIGITS * sizeof(uint32_t));
    session->graph.output_nodes = malloc(KOLIBRI_TRACE_MAX_DIGITS * sizeof(uint32_t));
    if (!session->graph.input_nodes || !session->graph.output_nodes) {
        free(session->graph.nodes);
        free(session->graph.edges);
        free(session->graph.input_nodes);
        free(session->graph.output_nodes);
        return -1;
    }
    
    /* Инициализация счётчиков */
    session->next_node_id = 1;  /* ID начинается с 1, 0 = ошибка */
    session->next_edge_id = 1;
    session->start_time = kt_current_time_ms();
    session->active = 1;
    
    /* Настройки по умолчанию */
    session->trace_digits = 1;
    session->trace_formulas = 1;
    session->trace_mutations = 1;
    session->verbose = 0;
    
    return 0;
}

int kt_session_end(KolibriTraceSession *session) {
    if (!session) {
        return -1;
    }
    
    session->end_time = kt_current_time_ms();
    session->active = 0;
    
    /* Строим граф с вычислением весов */
    return kt_graph_build(session);
}

void kt_session_reset(KolibriTraceSession *session) {
    if (!session) return;
    
    session->graph.node_count = 0;
    session->graph.edge_count = 0;
    session->graph.input_count = 0;
    session->graph.output_count = 0;
    session->graph.max_depth = 0;
    session->graph.total_contribution = 0.0;
    
    session->stack_depth = 0;
    session->next_node_id = 1;
    session->next_edge_id = 1;
    session->start_time = kt_current_time_ms();
    session->end_time = 0;
    session->active = 1;
}

void kt_session_free(KolibriTraceSession *session) {
    if (!session) return;
    
    free(session->graph.nodes);
    free(session->graph.edges);
    free(session->graph.input_nodes);
    free(session->graph.output_nodes);
    free(session->json_buffer);
    
    memset(session, 0, sizeof(KolibriTraceSession));
}

void kt_session_configure(KolibriTraceSession *session,
                          int trace_digits,
                          int trace_formulas,
                          int trace_mutations) {
    if (!session) return;
    
    session->trace_digits = trace_digits;
    session->trace_formulas = trace_formulas;
    session->trace_mutations = trace_mutations;
}

/* ============================================================================
 * Функции трассировки данных
 * ============================================================================ */

uint32_t kt_trace_digit(KolibriTraceSession *session,
                        uint8_t digit,
                        size_t position,
                        const char *source) {
    if (!session || !session->active || !session->trace_digits) {
        return 0;
    }
    
    /* Формируем метку */
    char label[KOLIBRI_TRACE_LABEL_SIZE];
    snprintf(label, sizeof(label), "digit[%zu]=%u", position, digit);
    
    uint32_t node_id = kt_add_node(session, KOLIBRI_TRACE_NODE_DIGIT, label, source);
    if (node_id == 0) {
        return 0;
    }
    
    /* Заполняем специфичные поля */
    KolibriTraceNode *node = kt_find_node_mut(session, node_id);
    if (node) {
        node->digit_value = digit;
        node->digit_position = position;
        
        /* Если это входная цифра (нет родителей), помечаем как вход */
        if (session->stack_depth == 0) {
            node->type = KOLIBRI_TRACE_NODE_INPUT;
            if (session->graph.input_count < KOLIBRI_TRACE_MAX_DIGITS) {
                session->graph.input_nodes[session->graph.input_count++] = node_id;
            }
        }
    }
    
    return node_id;
}

uint32_t kt_trace_formula(KolibriTraceSession *session,
                          uint32_t formula_id,
                          int input_value,
                          int output_value,
                          const uint32_t *input_nodes,
                          size_t input_count) {
    if (!session || !session->active || !session->trace_formulas) {
        return 0;
    }
    
    /* Формируем метку */
    char label[KOLIBRI_TRACE_LABEL_SIZE];
    snprintf(label, sizeof(label), "formula#%u: %d -> %d", 
             formula_id, input_value, output_value);
    
    uint32_t node_id = kt_add_node(session, KOLIBRI_TRACE_NODE_FORMULA, label, NULL);
    if (node_id == 0) {
        return 0;
    }
    
    /* Заполняем специфичные поля */
    KolibriTraceNode *node = kt_find_node_mut(session, node_id);
    if (node) {
        node->formula_id = formula_id;
    }
    
    /* Создаём рёбра от входных узлов */
    if (input_nodes && input_count > 0) {
        for (size_t i = 0; i < input_count; i++) {
            kt_add_edge(session, input_nodes[i], node_id,
                       KOLIBRI_TRACE_EDGE_FLOW, 1.0 / (double)input_count,
                       "formula_input");
        }
    }
    
    return node_id;
}

uint32_t kt_trace_mutation(KolibriTraceSession *session,
                           int mutation_type,
                           size_t position,
                           uint8_t old_value,
                           uint8_t new_value,
                           uint32_t source_node) {
    if (!session || !session->active || !session->trace_mutations) {
        return 0;
    }
    
    /* Формируем метку */
    char label[KOLIBRI_TRACE_LABEL_SIZE];
    snprintf(label, sizeof(label), "mut[%d]@%zu: %u->%u",
             mutation_type, position, old_value, new_value);
    
    uint32_t node_id = kt_add_node(session, KOLIBRI_TRACE_NODE_MUTATION, label, NULL);
    if (node_id == 0) {
        return 0;
    }
    
    /* Заполняем специфичные поля */
    KolibriTraceNode *node = kt_find_node_mut(session, node_id);
    if (node) {
        node->digit_value = new_value;
        node->digit_position = position;
    }
    
    /* Связываем с исходным узлом */
    if (source_node > 0) {
        kt_add_edge(session, source_node, node_id,
                   KOLIBRI_TRACE_EDGE_MUTATION, 1.0, "mutation");
    }
    
    return node_id;
}

uint32_t kt_trace_transform(KolibriTraceSession *session,
                            const char *operation,
                            const uint32_t *input_nodes,
                            size_t input_count) {
    if (!session || !session->active) {
        return 0;
    }
    
    uint32_t node_id = kt_add_node(session, KOLIBRI_TRACE_NODE_TRANSFORM, 
                                   operation, NULL);
    if (node_id == 0) {
        return 0;
    }
    
    /* Создаём рёбра от входных узлов */
    if (input_nodes && input_count > 0) {
        for (size_t i = 0; i < input_count; i++) {
            kt_add_edge(session, input_nodes[i], node_id,
                       KOLIBRI_TRACE_EDGE_FLOW, 1.0 / (double)input_count,
                       operation);
        }
    }
    
    return node_id;
}

int kt_mark_output(KolibriTraceSession *session, uint32_t node_id) {
    if (!session || node_id == 0) {
        return -1;
    }
    
    KolibriTraceNode *node = kt_find_node_mut(session, node_id);
    if (!node) {
        return -1;
    }
    
    node->type = KOLIBRI_TRACE_NODE_OUTPUT;
    
    if (session->graph.output_count < KOLIBRI_TRACE_MAX_DIGITS) {
        session->graph.output_nodes[session->graph.output_count++] = node_id;
    }
    
    return 0;
}

/* ============================================================================
 * Функции построения и анализа графа
 * ============================================================================ */

/* --- Рекурсивное вычисление глубины узла --- */
static uint32_t kt_compute_depth(KolibriTraceSession *session, 
                                 uint32_t node_id, 
                                 uint8_t *visited) {
    if (!session || node_id == 0) return 0;
    
    /* Находим индекс узла */
    size_t node_idx = 0;
    int found = 0;
    for (size_t i = 0; i < session->graph.node_count; i++) {
        if (session->graph.nodes[i].id == node_id) {
            node_idx = i;
            found = 1;
            break;
        }
    }
    if (!found) return 0;
    
    /* Защита от циклов */
    if (visited[node_idx]) {
        return session->graph.nodes[node_idx].depth;
    }
    visited[node_idx] = 1;
    
    /* Ищем максимальную глубину среди предшественников */
    uint32_t max_pred_depth = 0;
    for (size_t e = 0; e < session->graph.edge_count; e++) {
        if (session->graph.edges[e].target_id == node_id) {
            uint32_t pred_depth = kt_compute_depth(session, 
                                                   session->graph.edges[e].source_id,
                                                   visited);
            if (pred_depth + 1 > max_pred_depth) {
                max_pred_depth = pred_depth + 1;
            }
        }
    }
    
    session->graph.nodes[node_idx].depth = max_pred_depth;
    return max_pred_depth;
}

/* --- Рекурсивное вычисление вклада --- */
static double kt_compute_contribution(KolibriTraceSession *session,
                                      uint32_t node_id,
                                      uint8_t *visited) {
    if (!session || node_id == 0) return 0.0;
    
    /* Находим индекс узла */
    size_t node_idx = 0;
    int found = 0;
    for (size_t i = 0; i < session->graph.node_count; i++) {
        if (session->graph.nodes[i].id == node_id) {
            node_idx = i;
            found = 1;
            break;
        }
    }
    if (!found) return 0.0;
    
    /* Защита от циклов */
    if (visited[node_idx]) {
        return session->graph.nodes[node_idx].contribution_weight;
    }
    visited[node_idx] = 1;
    
    KolibriTraceNode *node = &session->graph.nodes[node_idx];
    
    /* Входные узлы имеют базовый вклад 1.0 */
    if (node->type == KOLIBRI_TRACE_NODE_INPUT) {
        node->contribution_weight = 1.0;
        return 1.0;
    }
    
    /* Суммируем взвешенные вклады предшественников */
    double total_contribution = 0.0;
    int has_predecessors = 0;
    
    for (size_t e = 0; e < session->graph.edge_count; e++) {
        if (session->graph.edges[e].target_id == node_id) {
            has_predecessors = 1;
            double pred_contrib = kt_compute_contribution(session,
                                                          session->graph.edges[e].source_id,
                                                          visited);
            total_contribution += pred_contrib * session->graph.edges[e].weight;
        }
    }
    
    /* Если нет предшественников, это также вход */
    if (!has_predecessors) {
        node->contribution_weight = 1.0;
    } else {
        node->contribution_weight = total_contribution;
    }
    
    return node->contribution_weight;
}

int kt_graph_build(KolibriTraceSession *session) {
    if (!session) {
        return -1;
    }
    
    KolibriTraceGraph *graph = &session->graph;
    if (graph->node_count == 0) {
        return 0;  /* Пустой граф */
    }
    
    /* Выделяем массив посещённых узлов */
    uint8_t *visited = calloc(graph->node_count, 1);
    if (!visited) {
        return -1;
    }
    
    /* Вычисляем глубины для всех узлов */
    graph->max_depth = 0;
    for (size_t i = 0; i < graph->node_count; i++) {
        memset(visited, 0, graph->node_count);
        uint32_t depth = kt_compute_depth(session, graph->nodes[i].id, visited);
        if (depth > graph->max_depth) {
            graph->max_depth = depth;
        }
    }
    
    /* Вычисляем вклады для выходных узлов */
    graph->total_contribution = 0.0;
    for (size_t i = 0; i < graph->output_count; i++) {
        memset(visited, 0, graph->node_count);
        double contrib = kt_compute_contribution(session, graph->output_nodes[i], visited);
        graph->total_contribution += contrib;
    }
    
    /* Если нет явных выходов, считаем вклады для всех узлов без потомков */
    if (graph->output_count == 0) {
        for (size_t i = 0; i < graph->node_count; i++) {
            /* Проверяем, есть ли у узла потомки */
            int has_children = 0;
            for (size_t e = 0; e < graph->edge_count; e++) {
                if (graph->edges[e].source_id == graph->nodes[i].id) {
                    has_children = 1;
                    break;
                }
            }
            
            if (!has_children) {
                memset(visited, 0, graph->node_count);
                double contrib = kt_compute_contribution(session, graph->nodes[i].id, visited);
                graph->total_contribution += contrib;
            }
        }
    }
    
    free(visited);
    return 0;
}

int kt_graph_find_path(const KolibriTraceGraph *graph,
                       uint32_t from_id,
                       uint32_t to_id,
                       uint32_t *path,
                       size_t path_capacity,
                       size_t *path_length) {
    if (!graph || !path || !path_length || path_capacity == 0) {
        return -1;
    }
    
    *path_length = 0;
    
    /* Простой BFS для поиска кратчайшего пути */
    if (graph->node_count == 0) {
        return -1;
    }
    
    uint32_t *queue = malloc(graph->node_count * sizeof(uint32_t));
    uint32_t *parent = malloc(graph->node_count * sizeof(uint32_t));
    uint8_t *visited = calloc(graph->node_count, 1);
    
    if (!queue || !parent || !visited) {
        free(queue);
        free(parent);
        free(visited);
        return -1;
    }
    
    /* Инициализация */
    for (size_t i = 0; i < graph->node_count; i++) {
        parent[i] = 0;  /* 0 = нет родителя */
    }
    
    /* Находим индекс начального узла */
    size_t from_idx = graph->node_count;
    size_t to_idx = graph->node_count;
    for (size_t i = 0; i < graph->node_count; i++) {
        if (graph->nodes[i].id == from_id) from_idx = i;
        if (graph->nodes[i].id == to_id) to_idx = i;
    }
    
    if (from_idx >= graph->node_count || to_idx >= graph->node_count) {
        free(queue);
        free(parent);
        free(visited);
        return -1;
    }
    
    /* BFS */
    size_t head = 0, tail = 0;
    queue[tail++] = from_id;
    visited[from_idx] = 1;
    int found = 0;
    
    while (head < tail && !found) {
        uint32_t current = queue[head++];
        
        if (current == to_id) {
            found = 1;
            break;
        }
        
        /* Находим индекс текущего узла */
        size_t curr_idx = 0;
        for (size_t i = 0; i < graph->node_count; i++) {
            if (graph->nodes[i].id == current) {
                curr_idx = i;
                break;
            }
        }
        
        /* Обходим соседей (по рёбрам) */
        for (size_t e = 0; e < graph->edge_count; e++) {
            uint32_t next = 0;
            if (graph->edges[e].source_id == current) {
                next = graph->edges[e].target_id;
            }
            
            if (next == 0) continue;
            
            /* Находим индекс next */
            size_t next_idx = graph->node_count;
            for (size_t i = 0; i < graph->node_count; i++) {
                if (graph->nodes[i].id == next) {
                    next_idx = i;
                    break;
                }
            }
            
            if (next_idx < graph->node_count && !visited[next_idx]) {
                visited[next_idx] = 1;
                parent[next_idx] = current;
                queue[tail++] = next;
                
                if (next == to_id) {
                    found = 1;
                    break;
                }
            }
        }
    }
    
    if (!found) {
        free(queue);
        free(parent);
        free(visited);
        return -1;
    }
    
    /* Восстанавливаем путь */
    size_t temp_len = 0;
    uint32_t current = to_id;
    while (current != 0 && temp_len < path_capacity) {
        path[temp_len++] = current;
        if (current == from_id) break;
        
        /* Находим родителя */
        for (size_t i = 0; i < graph->node_count; i++) {
            if (graph->nodes[i].id == current) {
                current = parent[i];
                break;
            }
        }
    }
    
    /* Разворачиваем путь */
    for (size_t i = 0; i < temp_len / 2; i++) {
        uint32_t tmp = path[i];
        path[i] = path[temp_len - 1 - i];
        path[temp_len - 1 - i] = tmp;
    }
    
    *path_length = temp_len;
    
    free(queue);
    free(parent);
    free(visited);
    return 0;
}

int kt_graph_find_ancestors(const KolibriTraceGraph *graph,
                            uint32_t node_id,
                            uint32_t *ancestors,
                            size_t capacity,
                            size_t *count) {
    if (!graph || !ancestors || !count || capacity == 0) {
        return -1;
    }
    
    *count = 0;
    
    /* DFS для поиска всех предков */
    uint8_t *visited = calloc(graph->node_count, 1);
    uint32_t *stack = malloc(graph->node_count * sizeof(uint32_t));
    
    if (!visited || !stack) {
        free(visited);
        free(stack);
        return -1;
    }
    
    size_t stack_top = 0;
    stack[stack_top++] = node_id;
    
    while (stack_top > 0) {
        uint32_t current = stack[--stack_top];
        
        /* Находим индекс текущего узла */
        size_t curr_idx = graph->node_count;
        for (size_t i = 0; i < graph->node_count; i++) {
            if (graph->nodes[i].id == current) {
                curr_idx = i;
                break;
            }
        }
        
        if (curr_idx >= graph->node_count) continue;
        if (visited[curr_idx]) continue;
        visited[curr_idx] = 1;
        
        /* Добавляем в результат (кроме начального узла) */
        if (current != node_id && *count < capacity) {
            ancestors[(*count)++] = current;
        }
        
        /* Добавляем предшественников в стек */
        for (size_t e = 0; e < graph->edge_count; e++) {
            if (graph->edges[e].target_id == current) {
                stack[stack_top++] = graph->edges[e].source_id;
            }
        }
    }
    
    free(visited);
    free(stack);
    return 0;
}

/* ============================================================================
 * Функции экспорта и визуализации
 * ============================================================================ */

int kt_graph_to_json(const KolibriTraceSession *session,
                     char *buffer,
                     size_t buffer_size,
                     size_t *written) {
    if (!session || !buffer || buffer_size == 0 || !written) {
        return -1;
    }
    
    const KolibriTraceGraph *graph = &session->graph;
    size_t pos = 0;
    
    /* Начало JSON */
    int n = snprintf(buffer + pos, buffer_size - pos,
                     "{\n  \"session\": {\n"
                     "    \"start_time\": %lu,\n"
                     "    \"end_time\": %lu,\n"
                     "    \"node_count\": %zu,\n"
                     "    \"edge_count\": %zu,\n"
                     "    \"max_depth\": %u\n"
                     "  },\n  \"nodes\": [\n",
                     (unsigned long)session->start_time,
                     (unsigned long)session->end_time,
                     graph->node_count,
                     graph->edge_count,
                     graph->max_depth);
    if (n < 0 || (size_t)n >= buffer_size - pos) {
        return -1;
    }
    pos += (size_t)n;
    
    /* Узлы */
    for (size_t i = 0; i < graph->node_count; i++) {
        const KolibriTraceNode *node = &graph->nodes[i];
        n = snprintf(buffer + pos, buffer_size - pos,
                     "    {\n"
                     "      \"id\": %u,\n"
                     "      \"type\": \"%s\",\n"
                     "      \"label\": \"%s\",\n"
                     "      \"digit_value\": %u,\n"
                     "      \"position\": %zu,\n"
                     "      \"depth\": %u,\n"
                     "      \"contribution\": %.6f,\n"
                     "      \"source\": \"%s\"\n"
                     "    }%s\n",
                     node->id,
                     kt_node_type_name(node->type),
                     node->label,
                     node->digit_value,
                     node->digit_position,
                     node->depth,
                     node->contribution_weight,
                     node->source,
                     (i < graph->node_count - 1) ? "," : "");
        if (n < 0 || (size_t)n >= buffer_size - pos) {
            return -1;
        }
        pos += (size_t)n;
    }
    
    /* Переход к рёбрам */
    n = snprintf(buffer + pos, buffer_size - pos, "  ],\n  \"edges\": [\n");
    if (n < 0 || (size_t)n >= buffer_size - pos) {
        return -1;
    }
    pos += (size_t)n;
    
    /* Рёбра */
    for (size_t i = 0; i < graph->edge_count; i++) {
        const KolibriTraceEdge *edge = &graph->edges[i];
        n = snprintf(buffer + pos, buffer_size - pos,
                     "    {\n"
                     "      \"id\": %u,\n"
                     "      \"source\": %u,\n"
                     "      \"target\": %u,\n"
                     "      \"type\": \"%s\",\n"
                     "      \"weight\": %.6f,\n"
                     "      \"operation\": \"%s\"\n"
                     "    }%s\n",
                     edge->id,
                     edge->source_id,
                     edge->target_id,
                     kt_edge_type_name(edge->type),
                     edge->weight,
                     edge->operation,
                     (i < graph->edge_count - 1) ? "," : "");
        if (n < 0 || (size_t)n >= buffer_size - pos) {
            return -1;
        }
        pos += (size_t)n;
    }
    
    /* Закрытие JSON */
    n = snprintf(buffer + pos, buffer_size - pos, "  ]\n}\n");
    if (n < 0 || (size_t)n >= buffer_size - pos) {
        return -1;
    }
    pos += (size_t)n;
    
    *written = pos;
    return 0;
}

int kt_graph_heatmap(const KolibriTraceSession *session,
                     KolibriTraceHeatmap *heatmap) {
    if (!session || !heatmap) {
        return -1;
    }
    
    memset(heatmap, 0, sizeof(KolibriTraceHeatmap));
    
    const KolibriTraceGraph *graph = &session->graph;
    
    /* Собираем все узлы-цифры */
    heatmap->max_weight = 0.0;
    heatmap->min_weight = 1e10;
    double sum_weight = 0.0;
    
    for (size_t i = 0; i < graph->node_count && heatmap->length < KOLIBRI_TRACE_MAX_DIGITS; i++) {
        const KolibriTraceNode *node = &graph->nodes[i];
        
        /* Включаем только узлы типа DIGIT или INPUT */
        if (node->type == KOLIBRI_TRACE_NODE_DIGIT || 
            node->type == KOLIBRI_TRACE_NODE_INPUT) {
            
            heatmap->digits[heatmap->length] = node->digit_value;
            heatmap->weights[heatmap->length] = node->contribution_weight;
            
            if (node->contribution_weight > heatmap->max_weight) {
                heatmap->max_weight = node->contribution_weight;
            }
            if (node->contribution_weight < heatmap->min_weight) {
                heatmap->min_weight = node->contribution_weight;
            }
            sum_weight += node->contribution_weight;
            
            heatmap->length++;
        }
    }
    
    /* Вычисляем среднее */
    if (heatmap->length > 0) {
        heatmap->avg_weight = sum_weight / (double)heatmap->length;
    }
    
    /* Если не было цифр, сбрасываем min */
    if (heatmap->length == 0) {
        heatmap->min_weight = 0.0;
    }
    
    return 0;
}

int kt_export_decimal(const KolibriTraceSession *session,
                      char *buffer,
                      size_t buffer_size,
                      size_t *written) {
    if (!session || !buffer || buffer_size == 0 || !written) {
        return -1;
    }
    
    const KolibriTraceGraph *graph = &session->graph;
    size_t pos = 0;
    
    /* Формат: 
     * Каждый узел: [type:2][id:6][digit:2][position:6][depth:4][weight:6]
     * Каждое ребро: [type:2][source:6][target:6][weight:6]
     */
    
    /* Заголовок: количество узлов и рёбер */
    int n = snprintf(buffer + pos, buffer_size - pos,
                     "%06zu%06zu", graph->node_count, graph->edge_count);
    if (n < 0 || (size_t)n >= buffer_size - pos) {
        return -1;
    }
    pos += (size_t)n;
    
    /* Узлы */
    for (size_t i = 0; i < graph->node_count; i++) {
        const KolibriTraceNode *node = &graph->nodes[i];
        uint32_t weight_int = (uint32_t)(node->contribution_weight * 10000.0);
        if (weight_int > 999999) weight_int = 999999;
        
        n = snprintf(buffer + pos, buffer_size - pos,
                     "%02d%06u%02u%06zu%04u%06u",
                     (int)node->type,
                     node->id,
                     node->digit_value,
                     node->digit_position % 1000000,
                     node->depth % 10000,
                     weight_int);
        if (n < 0 || (size_t)n >= buffer_size - pos) {
            return -1;
        }
        pos += (size_t)n;
    }
    
    /* Рёбра */
    for (size_t i = 0; i < graph->edge_count; i++) {
        const KolibriTraceEdge *edge = &graph->edges[i];
        uint32_t weight_int = (uint32_t)(edge->weight * 10000.0);
        if (weight_int > 999999) weight_int = 999999;
        
        n = snprintf(buffer + pos, buffer_size - pos,
                     "%02d%06u%06u%06u",
                     (int)edge->type,
                     edge->source_id,
                     edge->target_id,
                     weight_int);
        if (n < 0 || (size_t)n >= buffer_size - pos) {
            return -1;
        }
        pos += (size_t)n;
    }
    
    *written = pos;
    return 0;
}

int kt_graph_to_dot(const KolibriTraceSession *session,
                    char *buffer,
                    size_t buffer_size,
                    size_t *written) {
    if (!session || !buffer || buffer_size == 0 || !written) {
        return -1;
    }
    
    const KolibriTraceGraph *graph = &session->graph;
    size_t pos = 0;
    
    /* Заголовок DOT */
    int n = snprintf(buffer + pos, buffer_size - pos,
                     "digraph KolibriTrace {\n"
                     "  rankdir=LR;\n"
                     "  node [shape=box, style=filled];\n\n");
    if (n < 0 || (size_t)n >= buffer_size - pos) {
        return -1;
    }
    pos += (size_t)n;
    
    /* Цвета по типам узлов */
    static const char *node_colors[KOLIBRI_TRACE_NODE_COUNT] = {
        "#90EE90",  /* input - светло-зелёный */
        "#ADD8E6",  /* digit - светло-синий */
        "#FFD700",  /* formula - золотой */
        "#FF6347",  /* mutation - томатный */
        "#DDA0DD",  /* crossover - сливовый */
        "#F0E68C",  /* transform - хаки */
        "#D3D3D3",  /* aggregate - серый */
        "#FFA07A",  /* output - лососевый */
        "#E6E6FA"   /* intermediate - лавандовый */
    };
    
    /* Узлы */
    for (size_t i = 0; i < graph->node_count; i++) {
        const KolibriTraceNode *node = &graph->nodes[i];
        const char *color = (node->type < KOLIBRI_TRACE_NODE_COUNT) 
                           ? node_colors[node->type] : "#FFFFFF";
        
        n = snprintf(buffer + pos, buffer_size - pos,
                     "  n%u [label=\"%s\\nw=%.3f\", fillcolor=\"%s\"];\n",
                     node->id,
                     node->label,
                     node->contribution_weight,
                     color);
        if (n < 0 || (size_t)n >= buffer_size - pos) {
            return -1;
        }
        pos += (size_t)n;
    }
    
    n = snprintf(buffer + pos, buffer_size - pos, "\n");
    if (n < 0 || (size_t)n >= buffer_size - pos) {
        return -1;
    }
    pos += (size_t)n;
    
    /* Рёбра */
    for (size_t i = 0; i < graph->edge_count; i++) {
        const KolibriTraceEdge *edge = &graph->edges[i];
        
        /* Стиль линии по типу ребра */
        const char *style = "solid";
        if (edge->type == KOLIBRI_TRACE_EDGE_MUTATION) {
            style = "dashed";
        } else if (edge->type == KOLIBRI_TRACE_EDGE_REFERENCE) {
            style = "dotted";
        }
        
        n = snprintf(buffer + pos, buffer_size - pos,
                     "  n%u -> n%u [label=\"%.2f\", style=%s];\n",
                     edge->source_id,
                     edge->target_id,
                     edge->weight,
                     style);
        if (n < 0 || (size_t)n >= buffer_size - pos) {
            return -1;
        }
        pos += (size_t)n;
    }
    
    /* Закрытие */
    n = snprintf(buffer + pos, buffer_size - pos, "}\n");
    if (n < 0 || (size_t)n >= buffer_size - pos) {
        return -1;
    }
    pos += (size_t)n;
    
    *written = pos;
    return 0;
}

/* ============================================================================
 * Вспомогательные функции
 * ============================================================================ */

const KolibriTraceNode *kt_get_node(const KolibriTraceSession *session,
                                    uint32_t node_id) {
    if (!session || node_id == 0) {
        return NULL;
    }
    
    const KolibriTraceGraph *graph = &session->graph;
    for (size_t i = 0; i < graph->node_count; i++) {
        if (graph->nodes[i].id == node_id) {
            return &graph->nodes[i];
        }
    }
    return NULL;
}

void kt_get_stats(const KolibriTraceSession *session,
                  size_t *node_count,
                  size_t *edge_count,
                  uint32_t *max_depth) {
    if (!session) {
        if (node_count) *node_count = 0;
        if (edge_count) *edge_count = 0;
        if (max_depth) *max_depth = 0;
        return;
    }
    
    if (node_count) *node_count = session->graph.node_count;
    if (edge_count) *edge_count = session->graph.edge_count;
    if (max_depth) *max_depth = session->graph.max_depth;
}
