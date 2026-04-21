/*
 * Kolibri OS — Фрактальная десятичная память
 *
 * Десятичное дерево понятий: каждый узел имеет 10 детей (цифры 0-9).
 * Путь в дереве = числовая последовательность = «мысль».
 * Каждая цифра раскрывается во вложенные 10 цифр, образуя фрактал.
 *
 *   7 → 7.3 → 7.3.1 → 7.3.1.8 → ...
 *   Глубина = точность «мысли»
 *
 * Основные операции:
 *   - Вставка понятия (путь в дереве + payload)
 *   - Ассоциативный поиск (ближайший путь)
 *   - Эволюция: мутация путей с сохранением смысла
 *   - Материализация: путь → данные
 */

#ifndef KOLIBRI_FRACTAL_MEMORY_H
#define KOLIBRI_FRACTAL_MEMORY_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Максимальные размеры --- */
#define KFM_MAX_DEPTH       64      /* макс. глубина дерева (= длина пути) */
#define KFM_MAX_PAYLOAD     128     /* макс. размер данных в узле */
#define KFM_MAX_ASSOCIATIONS 16     /* макс. ассоциаций на узел */
#ifdef EMSCRIPTEN
#define KFM_MAX_NODES       4096
#else
#define KFM_MAX_NODES       65536
#endif   /* макс. узлов в дереве */

/* --- Типы узлов --- */
typedef enum {
    KFM_NODE_EMPTY   = 0,   /* пустой узел (транзитный) */
    KFM_NODE_CONCEPT = 1,   /* понятие (с payload) */
    KFM_NODE_LINK    = 2,   /* ссылка на другой путь */
    KFM_NODE_PATTERN = 3    /* паттерн (сжатое представление) */
} KfmNodeType;

/* --- Ассоциация (связь между путями) --- */
typedef struct {
    uint8_t  target_path[KFM_MAX_DEPTH]; /* путь к целевому понятию */
    uint8_t  target_len;                 /* длина пути */
    float    strength;                   /* сила ассоциации (0.0-1.0) */
    uint32_t access_count;               /* кол-во обращений */
} KfmAssociation;

/* --- Узел фрактального дерева --- */
typedef struct KfmNode {
    struct KfmNode *children[10];        /* 10 потомков (цифры 0-9) */
    KfmNodeType     type;                /* тип узла */
    uint8_t         payload[KFM_MAX_PAYLOAD]; /* данные */
    size_t          payload_size;        /* размер данных */
    uint32_t        hash;                /* хеш содержимого */
    uint32_t        access_count;        /* кол-во обращений */
    uint64_t        created_at;          /* время создания */
    uint64_t        last_access;         /* последний доступ */
    float           activation;          /* уровень активации (0.0-1.0) */
    KfmAssociation  associations[KFM_MAX_ASSOCIATIONS]; /* ассоциации */
    uint8_t         num_associations;    /* кол-во ассоциаций */
    uint8_t         depth;               /* глубина в дереве */
} KfmNode;

/* --- Результат поиска --- */
typedef struct {
    const KfmNode *node;                 /* найденный узел */
    uint8_t  path[KFM_MAX_DEPTH];       /* путь к узлу */
    uint8_t  path_len;                   /* длина пути */
    float    similarity;                 /* схожесть (0.0-1.0) */
    int      exact;                      /* точное совпадение? */
} KfmSearchResult;

/* --- Статистика памяти --- */
typedef struct {
    size_t   total_nodes;                /* всего узлов */
    size_t   concept_nodes;              /* узлов с данными */
    size_t   total_associations;         /* всего ассоциаций */
    size_t   total_payload_bytes;        /* общий размер данных */
    float    avg_depth;                  /* средняя глубина */
    float    avg_activation;             /* средняя активация */
    size_t   max_depth;                  /* макс. глубина */
} KfmStats;

/* --- Контекст фрактальной памяти --- */
typedef struct {
    struct KfmNode *root;                /* корень дерева */
    size_t   node_count;                 /* счётчик узлов */
    uint64_t tick;                       /* глобальный тик (время) */
    float    decay_rate;                 /* скорость затухания (0.01-0.1) */
    uint32_t seed;                       /* RNG seed */
    struct KfmNode *node_pool;           /* Пул узлов */
} KfmContext;

/* === API === */

/* Создание/уничтожение */
int  kfm_init(KfmContext *ctx, uint32_t seed);
void kfm_free(KfmContext *ctx);

/* Вставка понятия по десятичному пути */
int  kfm_insert(KfmContext *ctx,
                const uint8_t *path, size_t path_len,
                const void *payload, size_t payload_size);

/* Поиск по точному пути */
const KfmNode *kfm_lookup(KfmContext *ctx,
                           const uint8_t *path, size_t path_len);

/* Ассоциативный поиск (ближайший путь) */
int  kfm_search(KfmContext *ctx,
                const uint8_t *query, size_t query_len,
                KfmSearchResult *results, size_t max_results);

/* Создание ассоциации между путями */
int  kfm_associate(KfmContext *ctx,
                    const uint8_t *path_a, size_t len_a,
                    const uint8_t *path_b, size_t len_b,
                    float strength);

/* Активация пути (волна активации по ассоциациям) */
int  kfm_activate(KfmContext *ctx,
                   const uint8_t *path, size_t path_len,
                   float energy);

/* Затухание — ослабляет неиспользуемые пути */
void kfm_decay(KfmContext *ctx);

/* Мутация — случайное изменение пути с сохранением семантики */
int  kfm_mutate(KfmContext *ctx,
                const uint8_t *path, size_t path_len,
                uint8_t *new_path, size_t *new_len);

/* Кодирование текста → десятичный путь */
size_t kfm_text_to_path(const char *text, size_t text_len,
                         uint8_t *path, size_t max_path);

/* Декодирование десятичного пути → текст (материализация) */
size_t kfm_path_to_text(KfmContext *ctx,
                         const uint8_t *path, size_t path_len,
                         char *text, size_t max_text);

/* Сериализация/десериализация */
size_t kfm_serialize(KfmContext *ctx, uint8_t *buf, size_t buf_size);
int    kfm_deserialize(KfmContext *ctx, const uint8_t *buf, size_t buf_size);

/* Статистика */
void   kfm_stats(const KfmContext *ctx, KfmStats *stats);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_FRACTAL_MEMORY_H */
