/*
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 * 
 * Модуль трассировки "Стеклянный Разум"
 * Glass Mind Tracing Module
 * 
 * Реализует инвариант прозрачности: отслеживание каждой цифры
 * от источника до вывода через визуализируемый граф зависимостей.
 */

#ifndef KOLIBRI_TRACE_H
#define KOLIBRI_TRACE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Константы модуля трассировки
 * ============================================================================ */

/** Максимальное количество узлов в графе */
#define KOLIBRI_TRACE_MAX_NODES      4096

/** Максимальное количество рёбер в графе */
#define KOLIBRI_TRACE_MAX_EDGES      16384

/** Максимальная длина метки узла */
#define KOLIBRI_TRACE_LABEL_SIZE     64

/** Максимальная длина источника данных */
#define KOLIBRI_TRACE_SOURCE_SIZE    128

/** Максимальная глубина стека вызовов */
#define KOLIBRI_TRACE_STACK_DEPTH    64

/** Максимальное количество цифр для отслеживания */
#define KOLIBRI_TRACE_MAX_DIGITS     1024

/* ============================================================================
 * Типы узлов графа зависимостей
 * ============================================================================ */

typedef enum {
    KOLIBRI_TRACE_NODE_INPUT = 0,      /* Входные данные (источник) */
    KOLIBRI_TRACE_NODE_DIGIT,          /* Одиночная цифра */
    KOLIBRI_TRACE_NODE_FORMULA,        /* Применение формулы */
    KOLIBRI_TRACE_NODE_MUTATION,       /* Мутация гена */
    KOLIBRI_TRACE_NODE_CROSSOVER,      /* Кроссовер */
    KOLIBRI_TRACE_NODE_TRANSFORM,      /* Трансформация */
    KOLIBRI_TRACE_NODE_AGGREGATE,      /* Агрегация (объединение) */
    KOLIBRI_TRACE_NODE_OUTPUT,         /* Выходные данные */
    KOLIBRI_TRACE_NODE_INTERMEDIATE,   /* Промежуточный результат */
    KOLIBRI_TRACE_NODE_COUNT
} KolibriTraceNodeType;

/* ============================================================================
 * Типы рёбер графа зависимостей
 * ============================================================================ */

typedef enum {
    KOLIBRI_TRACE_EDGE_FLOW = 0,       /* Поток данных */
    KOLIBRI_TRACE_EDGE_DEPENDENCY,     /* Зависимость */
    KOLIBRI_TRACE_EDGE_MUTATION,       /* Мутационная связь */
    KOLIBRI_TRACE_EDGE_REFERENCE,      /* Ссылка на источник */
    KOLIBRI_TRACE_EDGE_CAUSAL,         /* Причинно-следственная связь */
    KOLIBRI_TRACE_EDGE_COUNT
} KolibriTraceEdgeType;

/* ============================================================================
 * Структуры данных
 * ============================================================================ */

/**
 * Узел графа зависимостей
 * 
 * Представляет одну точку в цепочке преобразований данных.
 * Каждый узел хранит информацию о своём типе, значении и источнике.
 */
typedef struct {
    uint32_t id;                               /* Уникальный идентификатор узла */
    KolibriTraceNodeType type;                 /* Тип узла */
    
    /* --- Данные узла --- */
    uint8_t digit_value;                       /* Значение цифры (0-11) */
    size_t digit_position;                     /* Позиция в последовательности */
    
    /* --- Метаданные --- */
    char label[KOLIBRI_TRACE_LABEL_SIZE];      /* Человекочитаемая метка */
    char source[KOLIBRI_TRACE_SOURCE_SIZE];    /* Источник данных */
    uint64_t timestamp;                        /* Временная метка создания */
    
    /* --- Статистика вклада --- */
    double contribution_weight;                /* Вес вклада в результат [0.0-1.0] */
    uint32_t depth;                            /* Глубина от корня */
    uint32_t reference_count;                  /* Количество ссылок на узел */
    
    /* --- Связи с формулами --- */
    uint32_t formula_id;                       /* ID применённой формулы (если есть) */
    double fitness_contribution;               /* Вклад в fitness */
} KolibriTraceNode;

/**
 * Ребро графа зависимостей
 * 
 * Представляет связь между двумя узлами, описывая
 * характер зависимости и силу влияния.
 */
typedef struct {
    uint32_t id;                               /* Уникальный идентификатор ребра */
    uint32_t source_id;                        /* ID исходного узла */
    uint32_t target_id;                        /* ID целевого узла */
    KolibriTraceEdgeType type;                 /* Тип связи */
    
    /* --- Характеристики связи --- */
    double weight;                             /* Вес связи [0.0-1.0] */
    char operation[KOLIBRI_TRACE_LABEL_SIZE];  /* Описание операции */
    uint64_t timestamp;                        /* Время создания связи */
} KolibriTraceEdge;

/**
 * Полный граф зависимостей
 * 
 * Хранит все узлы и рёбра, образующие граф потока данных
 * для одной сессии трассировки.
 */
typedef struct {
    /* --- Узлы графа --- */
    KolibriTraceNode *nodes;                   /* Массив узлов */
    size_t node_count;                         /* Текущее количество узлов */
    size_t node_capacity;                      /* Вместимость массива */
    
    /* --- Рёбра графа --- */
    KolibriTraceEdge *edges;                   /* Массив рёбер */
    size_t edge_count;                         /* Текущее количество рёбер */
    size_t edge_capacity;                      /* Вместимость массива */
    
    /* --- Индексация --- */
    uint32_t *input_nodes;                     /* Индексы входных узлов */
    size_t input_count;                        /* Количество входов */
    uint32_t *output_nodes;                    /* Индексы выходных узлов */
    size_t output_count;                       /* Количество выходов */
    
    /* --- Метаданные графа --- */
    uint32_t max_depth;                        /* Максимальная глубина графа */
    double total_contribution;                 /* Суммарный вклад */
} KolibriTraceGraph;

/**
 * Сессия трассировки
 * 
 * Управляет процессом сбора информации о потоке данных.
 * Отслеживает иерархию вызовов и накапливает граф.
 */
typedef struct {
    /* --- Основной граф --- */
    KolibriTraceGraph graph;                   /* Граф зависимостей */
    
    /* --- Стек вызовов --- */
    uint32_t call_stack[KOLIBRI_TRACE_STACK_DEPTH];  /* Стек ID узлов */
    size_t stack_depth;                        /* Текущая глубина стека */
    
    /* --- Состояние сессии --- */
    int active;                                /* Флаг активности (1 = активна) */
    uint64_t start_time;                       /* Время начала сессии */
    uint64_t end_time;                         /* Время окончания */
    
    /* --- Счётчики --- */
    uint32_t next_node_id;                     /* Следующий ID узла */
    uint32_t next_edge_id;                     /* Следующий ID ребра */
    
    /* --- Настройки --- */
    int trace_digits;                          /* Трассировать отдельные цифры? */
    int trace_formulas;                        /* Трассировать формулы? */
    int trace_mutations;                       /* Трассировать мутации? */
    int verbose;                               /* Подробный вывод? */
    
    /* --- Буфер для JSON --- */
    char *json_buffer;                         /* Буфер для JSON-экспорта */
    size_t json_capacity;                      /* Размер буфера */
} KolibriTraceSession;

/**
 * Тепловая карта вклада цифр
 * 
 * Визуализирует относительный вклад каждой цифры
 * в формирование результата.
 */
typedef struct {
    uint8_t digits[KOLIBRI_TRACE_MAX_DIGITS];  /* Значения цифр */
    double weights[KOLIBRI_TRACE_MAX_DIGITS];  /* Веса вклада [0.0-1.0] */
    size_t length;                             /* Количество цифр */
    double max_weight;                         /* Максимальный вес */
    double min_weight;                         /* Минимальный вес */
    double avg_weight;                         /* Средний вес */
} KolibriTraceHeatmap;

/* ============================================================================
 * Функции управления сессией
 * ============================================================================ */

/**
 * Начинает новую сессию трассировки
 * 
 * @param session Указатель на сессию
 * @return 0 при успехе, -1 при ошибке
 */
int kt_session_begin(KolibriTraceSession *session);

/**
 * Завершает сессию трассировки
 * 
 * @param session Указатель на сессию
 * @return 0 при успехе, -1 при ошибке
 */
int kt_session_end(KolibriTraceSession *session);

/**
 * Сбрасывает сессию без освобождения памяти
 * 
 * @param session Указатель на сессию
 */
void kt_session_reset(KolibriTraceSession *session);

/**
 * Освобождает все ресурсы сессии
 * 
 * @param session Указатель на сессию
 */
void kt_session_free(KolibriTraceSession *session);

/**
 * Настраивает параметры трассировки
 * 
 * @param session Указатель на сессию
 * @param trace_digits Трассировать цифры?
 * @param trace_formulas Трассировать формулы?
 * @param trace_mutations Трассировать мутации?
 */
void kt_session_configure(KolibriTraceSession *session,
                          int trace_digits,
                          int trace_formulas,
                          int trace_mutations);

/* ============================================================================
 * Функции трассировки данных
 * ============================================================================ */

/**
 * Отслеживает одну цифру
 * 
 * @param session Активная сессия
 * @param digit Значение цифры (0-11)
 * @param position Позиция в последовательности
 * @param source Источник данных (может быть NULL)
 * @return ID созданного узла или 0 при ошибке
 */
uint32_t kt_trace_digit(KolibriTraceSession *session,
                        uint8_t digit,
                        size_t position,
                        const char *source);

/**
 * Отслеживает применение формулы
 * 
 * @param session Активная сессия
 * @param formula_id ID формулы
 * @param input_value Входное значение
 * @param output_value Выходное значение
 * @param input_nodes ID входных узлов (массив)
 * @param input_count Количество входных узлов
 * @return ID созданного узла или 0 при ошибке
 */
uint32_t kt_trace_formula(KolibriTraceSession *session,
                          uint32_t formula_id,
                          int input_value,
                          int output_value,
                          const uint32_t *input_nodes,
                          size_t input_count);

/**
 * Отслеживает мутацию
 * 
 * @param session Активная сессия
 * @param mutation_type Тип мутации
 * @param position Позиция мутации
 * @param old_value Старое значение
 * @param new_value Новое значение
 * @param source_node ID исходного узла
 * @return ID созданного узла или 0 при ошибке
 */
uint32_t kt_trace_mutation(KolibriTraceSession *session,
                           int mutation_type,
                           size_t position,
                           uint8_t old_value,
                           uint8_t new_value,
                           uint32_t source_node);

/**
 * Отслеживает трансформацию (преобразование данных)
 * 
 * @param session Активная сессия
 * @param operation Название операции
 * @param input_nodes ID входных узлов
 * @param input_count Количество входов
 * @return ID созданного узла или 0 при ошибке
 */
uint32_t kt_trace_transform(KolibriTraceSession *session,
                            const char *operation,
                            const uint32_t *input_nodes,
                            size_t input_count);

/**
 * Помечает узел как выходной
 * 
 * @param session Активная сессия
 * @param node_id ID узла
 * @return 0 при успехе, -1 при ошибке
 */
int kt_mark_output(KolibriTraceSession *session, uint32_t node_id);

/* ============================================================================
 * Функции построения и анализа графа
 * ============================================================================ */

/**
 * Строит граф зависимостей из накопленных данных
 * 
 * Анализирует все узлы и рёбра, вычисляет веса вкладов,
 * определяет глубины и обновляет статистику.
 * 
 * @param session Сессия с накопленными данными
 * @return 0 при успехе, -1 при ошибке
 */
int kt_graph_build(KolibriTraceSession *session);

/**
 * Вычисляет кратчайший путь между узлами
 * 
 * @param graph Граф зависимостей
 * @param from_id ID начального узла
 * @param to_id ID конечного узла
 * @param path Массив для записи пути (ID узлов)
 * @param path_capacity Размер массива
 * @param path_length Фактическая длина пути
 * @return 0 при успехе, -1 если путь не найден
 */
int kt_graph_find_path(const KolibriTraceGraph *graph,
                       uint32_t from_id,
                       uint32_t to_id,
                       uint32_t *path,
                       size_t path_capacity,
                       size_t *path_length);

/**
 * Находит все узлы, влияющие на заданный
 * 
 * @param graph Граф зависимостей
 * @param node_id ID целевого узла
 * @param ancestors Массив для записи предков
 * @param capacity Размер массива
 * @param count Фактическое количество предков
 * @return 0 при успехе, -1 при ошибке
 */
int kt_graph_find_ancestors(const KolibriTraceGraph *graph,
                            uint32_t node_id,
                            uint32_t *ancestors,
                            size_t capacity,
                            size_t *count);

/* ============================================================================
 * Функции экспорта и визуализации
 * ============================================================================ */

/**
 * Экспортирует граф в JSON для веб-визуализации
 * 
 * Формат совместим с D3.js и другими библиотеками визуализации.
 * 
 * @param session Сессия с графом
 * @param buffer Буфер для записи JSON
 * @param buffer_size Размер буфера
 * @param written Количество записанных байт
 * @return 0 при успехе, -1 при ошибке
 */
int kt_graph_to_json(const KolibriTraceSession *session,
                     char *buffer,
                     size_t buffer_size,
                     size_t *written);

/**
 * Генерирует тепловую карту вклада цифр
 * 
 * @param session Сессия с графом
 * @param heatmap Структура для записи тепловой карты
 * @return 0 при успехе, -1 при ошибке
 */
int kt_graph_heatmap(const KolibriTraceSession *session,
                     KolibriTraceHeatmap *heatmap);

/**
 * Экспортирует данные трассировки в десятичном виде
 * 
 * Формат: каждый узел и связь представлены последовательностью цифр.
 * 
 * @param session Сессия с графом
 * @param buffer Буфер для записи
 * @param buffer_size Размер буфера
 * @param written Количество записанных байт
 * @return 0 при успехе, -1 при ошибке
 */
int kt_export_decimal(const KolibriTraceSession *session,
                      char *buffer,
                      size_t buffer_size,
                      size_t *written);

/**
 * Экспортирует граф в формате DOT (Graphviz)
 * 
 * @param session Сессия с графом
 * @param buffer Буфер для записи
 * @param buffer_size Размер буфера
 * @param written Количество записанных байт
 * @return 0 при успехе, -1 при ошибке
 */
int kt_graph_to_dot(const KolibriTraceSession *session,
                    char *buffer,
                    size_t buffer_size,
                    size_t *written);

/* ============================================================================
 * Вспомогательные функции
 * ============================================================================ */

/**
 * Получает узел по ID
 * 
 * @param session Сессия
 * @param node_id ID узла
 * @return Указатель на узел или NULL
 */
const KolibriTraceNode *kt_get_node(const KolibriTraceSession *session,
                                    uint32_t node_id);

/**
 * Получает статистику сессии
 * 
 * @param session Сессия
 * @param node_count Количество узлов (выход)
 * @param edge_count Количество рёбер (выход)
 * @param max_depth Максимальная глубина (выход)
 */
void kt_get_stats(const KolibriTraceSession *session,
                  size_t *node_count,
                  size_t *edge_count,
                  uint32_t *max_depth);

/**
 * Возвращает текстовое описание типа узла
 * 
 * @param type Тип узла
 * @return Строка с названием типа
 */
const char *kt_node_type_name(KolibriTraceNodeType type);

/**
 * Возвращает текстовое описание типа ребра
 * 
 * @param type Тип ребра
 * @return Строка с названием типа
 */
const char *kt_edge_type_name(KolibriTraceEdgeType type);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_TRACE_H */
