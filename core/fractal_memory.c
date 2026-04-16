/*
 * Kolibri OS — Фрактальная десятичная память (реализация)
 *
 * Десятичное дерево 10-арное: каждый узел имеет 10 детей (цифры 0-9).
 * Путь = числовая последовательность = «мысль» в числовом мышлении Kolibri.
 *
 * Фрактальность: каждая цифра раскрывается в 10 подцифр, бесконечная глубина.
 * Ассоциации: двунаправленные связи между путями, затухающие со временем.
 * Активация: волна активации распространяется по ассоциациям.
 *
 * Интеграция:
 *   - genome.c  → пути формируются из 64-значных геномов
 *   - formula.c → формулы хранятся как payload узлов
 *   - script.c  → KolibriScript команда CANVAS визуализирует дерево
 */

#include "kolibri/fractal_memory.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* --- Вспомогательные функции --- */

/* Псевдослучайные числа (xorshift32) */
static uint32_t kfm_rand(uint32_t *seed)
{
    *seed ^= *seed << 13;
    *seed ^= *seed >> 17;
    *seed ^= *seed << 5;
    return *seed;
}

/* Быстрый хеш (FNV-1a) */
static uint32_t kfm_hash(const uint8_t *data, size_t len)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

/* Текущее время (монотонно растущий тик) */
static uint64_t kfm_now(KfmContext *ctx)
{
    return ++ctx->tick;
}

/* --- Создание/уничтожение узлов --- */

static KfmNode *kfm_node_alloc(KfmContext *ctx, uint8_t depth)
{
    if (ctx->node_count >= KFM_MAX_NODES) return NULL;

    KfmNode *n = (KfmNode *)calloc(1, sizeof(KfmNode));
    if (!n) return NULL;

    n->depth      = depth;
    n->created_at = kfm_now(ctx);
    n->last_access = n->created_at;
    n->activation  = 0.0f;
    ctx->node_count++;
    return n;
}

/* Итеративная очистка дерева (без рекурсии — защита от stack overflow) */
static void kfm_node_free(KfmNode *node)
{
    if (!node) return;
    /* Используем явный стек вместо рекурсии */
    size_t cap = 256;
    size_t top = 0;
    KfmNode **stack = (KfmNode **)malloc(cap * sizeof(KfmNode *));
    if (!stack) {
        /* Fallback: если malloc не удался, хотя бы освободим корень */
        free(node);
        return;
    }
    stack[top++] = node;
    while (top > 0) {
        KfmNode *cur = stack[--top];
        for (int i = 0; i < 10; i++) {
            if (cur->children[i]) {
                /* Расширяем стек при необходимости */
                if (top >= cap) {
                    size_t new_cap = cap * 2;
                    KfmNode **tmp = (KfmNode **)realloc(stack, new_cap * sizeof(KfmNode *));
                    if (!tmp) {
                        /* Не можем расширить — освобождаем что можем */
                        free(cur);
                        while (top > 0) free(stack[--top]);
                        free(stack);
                        return;
                    }
                    stack = tmp;
                    cap = new_cap;
                }
                stack[top++] = cur->children[i];
            }
        }
        free(cur);
    }
    free(stack);
}

/* --- Навигация по дереву --- */

/* Найти (или создать) узел по пути */
static KfmNode *kfm_navigate(KfmContext *ctx, const uint8_t *path,
                               size_t path_len, int create)
{
    if (!ctx->root) {
        if (!create) return NULL;
        ctx->root = kfm_node_alloc(ctx, 0);
        if (!ctx->root) return NULL;
    }

    KfmNode *cur = ctx->root;
    for (size_t i = 0; i < path_len; i++) {
        uint8_t d = path[i];
        if (d > 9) d = d % 10; /* нормализация */

        if (!cur->children[d]) {
            if (!create) return NULL;
            cur->children[d] = kfm_node_alloc(ctx, (uint8_t)(i + 1));
            if (!cur->children[d]) return NULL;
        }
        cur = cur->children[d];
    }
    return cur;
}

/* === Основной API === */

int kfm_init(KfmContext *ctx, uint32_t seed)
{
    if (!ctx) return -1;

    memset(ctx, 0, sizeof(KfmContext));
    ctx->seed = seed ? seed : 42;
    ctx->decay_rate = 0.05f; /* 5% затухание за тик */
    ctx->root = kfm_node_alloc(ctx, 0);
    return ctx->root ? 0 : -1;
}

void kfm_free(KfmContext *ctx)
{
    if (!ctx) return;
    kfm_node_free(ctx->root);
    ctx->root = NULL;
    ctx->node_count = 0;
}

int kfm_insert(KfmContext *ctx,
               const uint8_t *path, size_t path_len,
               const void *payload, size_t payload_size)
{
    if (!ctx || !path || path_len == 0) return -1;
    if (path_len > KFM_MAX_DEPTH) return -1;
    if (payload_size > KFM_MAX_PAYLOAD) return -1;

    KfmNode *node = kfm_navigate(ctx, path, path_len, 1);
    if (!node) return -1;

    node->type = KFM_NODE_CONCEPT;
    if (payload && payload_size > 0) {
        memcpy(node->payload, payload, payload_size);
        node->payload_size = payload_size;
        node->hash = kfm_hash(node->payload, payload_size);
    }
    node->last_access = kfm_now(ctx);
    node->access_count++;
    node->activation = 1.0f;

    return 0;
}

const KfmNode *kfm_lookup(KfmContext *ctx,
                            const uint8_t *path, size_t path_len)
{
    if (!ctx || !path || path_len == 0) return NULL;

    KfmNode *node = kfm_navigate(ctx, path, path_len, 0);
    if (!node) return NULL;

    /* Обновляем статистику доступа */
    node->last_access = kfm_now(ctx);
    node->access_count++;
    if (node->activation < 1.0f) {
        node->activation += 0.1f;
        if (node->activation > 1.0f) node->activation = 1.0f;
    }

    return node;
}

/* --- Ассоциативный поиск --- */

/* Рекурсивный обход: собираем все concept-узлы */
static void kfm_collect(const KfmNode *node,
                         uint8_t *cur_path, size_t cur_len,
                         const uint8_t *query, size_t query_len,
                         KfmSearchResult *results, size_t max_results,
                         size_t *found)
{
    if (!node || *found >= max_results) return;

    /* Если узел — понятие, подсчитать схожесть */
    if (node->type == KFM_NODE_CONCEPT && node->payload_size > 0) {
        /* Схожесть по общему префиксу */
        size_t common = 0;
        size_t min_len = cur_len < query_len ? cur_len : query_len;
        for (size_t i = 0; i < min_len; i++) {
            if (cur_path[i] == query[i]) common++;
            else break;
        }
        size_t max_len = cur_len > query_len ? cur_len : query_len;
        float sim = max_len > 0 ? (float)common / (float)max_len : 0.0f;

        /* Бонус за активацию */
        sim += node->activation * 0.1f;
        if (sim > 1.0f) sim = 1.0f;

        /* Добавляем в результаты (с вытеснением наименее похожих) */
        int slot = -1;
        if (*found < max_results) {
            slot = (int)(*found);
            (*found)++;
        } else {
            /* Ищем наихудший результат для замены */
            float worst = 2.0f;
            for (size_t i = 0; i < max_results; i++) {
                if (results[i].similarity < worst) {
                    worst = results[i].similarity;
                    slot = (int)i;
                }
            }
            if (slot >= 0 && sim <= results[slot].similarity) {
                slot = -1; /* текущий ещё хуже */
            }
        }

        if (slot >= 0) {
            results[slot].node       = node;
            results[slot].path_len   = (uint8_t)cur_len;
            memcpy(results[slot].path, cur_path, cur_len);
            results[slot].similarity = sim;
            results[slot].exact      = (cur_len == query_len && common == cur_len) ? 1 : 0;
        }
    }

    /* Рекурсивно обходим потомков */
    for (int d = 0; d < 10; d++) {
        if (node->children[d] && cur_len < KFM_MAX_DEPTH) {
            cur_path[cur_len] = (uint8_t)d;
            kfm_collect(node->children[d], cur_path, cur_len + 1,
                        query, query_len, results, max_results, found);
        }
    }
}

int kfm_search(KfmContext *ctx,
               const uint8_t *query, size_t query_len,
               KfmSearchResult *results, size_t max_results)
{
    if (!ctx || !query || !results || max_results == 0) return 0;

    uint8_t cur_path[KFM_MAX_DEPTH];
    size_t found = 0;
    memset(results, 0, max_results * sizeof(KfmSearchResult));

    kfm_collect(ctx->root, cur_path, 0, query, query_len,
                results, max_results, &found);
    return (int)found;
}

/* --- Ассоциации --- */

int kfm_associate(KfmContext *ctx,
                   const uint8_t *path_a, size_t len_a,
                   const uint8_t *path_b, size_t len_b,
                   float strength)
{
    if (!ctx || !path_a || !path_b) return -1;
    if (len_a == 0 || len_a > KFM_MAX_DEPTH) return -1;
    if (len_b == 0 || len_b > KFM_MAX_DEPTH) return -1;
    if (strength < 0.0f || strength > 1.0f) return -1;

    /* Навигация до обоих узлов (создаём при необходимости) */
    KfmNode *a = kfm_navigate(ctx, path_a, len_a, 1);
    KfmNode *b = kfm_navigate(ctx, path_b, len_b, 1);
    if (!a || !b) return -1;

    /* Добавить ассоциацию A → B */
    if (a->num_associations < KFM_MAX_ASSOCIATIONS) {
        KfmAssociation *assoc = &a->associations[a->num_associations];
        memcpy(assoc->target_path, path_b, len_b);
        assoc->target_len   = (uint8_t)len_b;
        assoc->strength     = strength;
        assoc->access_count = 0;
        a->num_associations++;
    }

    /* Обратная ассоциация B → A */
    if (b->num_associations < KFM_MAX_ASSOCIATIONS) {
        KfmAssociation *assoc = &b->associations[b->num_associations];
        memcpy(assoc->target_path, path_a, len_a);
        assoc->target_len   = (uint8_t)len_a;
        assoc->strength     = strength;
        assoc->access_count = 0;
        b->num_associations++;
    }

    return 0;
}

/* --- Активация (волна) --- */

int kfm_activate(KfmContext *ctx,
                  const uint8_t *path, size_t path_len,
                  float energy)
{
    if (!ctx || !path || path_len == 0) return -1;
    if (energy <= 0.0f) return 0;

    KfmNode *node = kfm_navigate(ctx, path, path_len, 0);
    if (!node) return -1;

    /* Активировать текущий узел */
    node->activation += energy;
    if (node->activation > 1.0f) node->activation = 1.0f;
    node->last_access = kfm_now(ctx);

    /* Распространить по ассоциациям (с затуханием) */
    for (int i = 0; i < node->num_associations; i++) {
        KfmAssociation *a = &node->associations[i];
        float spread = energy * a->strength * 0.5f; /* 50% затухание */
        if (spread > 0.01f) {
            a->access_count++;
            /* Рекурсивная активация (с ограниченной глубиной) */
            KfmNode *target = kfm_navigate(ctx, a->target_path,
                                            a->target_len, 0);
            if (target) {
                target->activation += spread;
                if (target->activation > 1.0f) target->activation = 1.0f;
                target->last_access = kfm_now(ctx);
            }
        }
    }

    return 0;
}

/* --- Затухание --- */

static void kfm_decay_node(KfmNode *node, float rate, uint64_t now)
{
    if (!node) return;

    /* Затухание активации */
    if (node->activation > 0.0f) {
        uint64_t age = now - node->last_access;
        float decay = 1.0f - rate * (float)age;
        if (decay < 0.0f) decay = 0.0f;
        node->activation *= decay;
        if (node->activation < 0.001f) node->activation = 0.0f;
    }

    /* Затухание ассоциаций */
    for (int i = 0; i < node->num_associations; i++) {
        node->associations[i].strength *= (1.0f - rate * 0.01f);
        if (node->associations[i].strength < 0.001f) {
            /* Удалить слабую ассоциацию */
            node->num_associations--;
            if (i < node->num_associations) {
                node->associations[i] = node->associations[node->num_associations];
            }
            i--;
        }
    }

    /* Рекурсивно обойти потомков */
    for (int d = 0; d < 10; d++) {
        kfm_decay_node(node->children[d], rate, now);
    }
}

void kfm_decay(KfmContext *ctx)
{
    if (!ctx || !ctx->root) return;
    kfm_decay_node(ctx->root, ctx->decay_rate, ctx->tick);
}

/* --- Мутация --- */

int kfm_mutate(KfmContext *ctx,
               const uint8_t *path, size_t path_len,
               uint8_t *new_path, size_t *new_len)
{
    if (!ctx || !path || !new_path || !new_len) return -1;
    if (path_len == 0 || path_len > KFM_MAX_DEPTH) return -1;

    memcpy(new_path, path, path_len);
    *new_len = path_len;

    /* Выбрать тип мутации */
    uint32_t r = kfm_rand(&ctx->seed);
    int mutation_type = (int)(r % 4);

    switch (mutation_type) {
    case 0: {
        /* Точечная мутация: изменить одну цифру */
        size_t pos = kfm_rand(&ctx->seed) % path_len;
        uint8_t delta = 1 + (uint8_t)(kfm_rand(&ctx->seed) % 3); /* ±1..3 */
        if (kfm_rand(&ctx->seed) & 1) {
            new_path[pos] = (new_path[pos] + delta) % 10;
        } else {
            new_path[pos] = (new_path[pos] + 10 - delta) % 10;
        }
        break;
    }
    case 1: {
        /* Вставка: добавить новую цифру */
        if (*new_len < KFM_MAX_DEPTH) {
            size_t pos = kfm_rand(&ctx->seed) % (*new_len + 1);
            memmove(new_path + pos + 1, new_path + pos, *new_len - pos);
            new_path[pos] = (uint8_t)(kfm_rand(&ctx->seed) % 10);
            (*new_len)++;
        }
        break;
    }
    case 2: {
        /* Удаление: убрать одну цифру */
        if (*new_len > 1) {
            size_t pos = kfm_rand(&ctx->seed) % *new_len;
            memmove(new_path + pos, new_path + pos + 1, *new_len - pos - 1);
            (*new_len)--;
        }
        break;
    }
    case 3: {
        /* Своп: поменять две соседние цифры */
        if (path_len >= 2) {
            size_t pos = kfm_rand(&ctx->seed) % (path_len - 1);
            uint8_t tmp = new_path[pos];
            new_path[pos] = new_path[pos + 1];
            new_path[pos + 1] = tmp;
        }
        break;
    }
    }

    return 0;
}

/* --- Кодирование текста → десятичный путь --- */

size_t kfm_text_to_path(const char *text, size_t text_len,
                         uint8_t *path, size_t max_path)
{
    if (!text || !path || text_len == 0 || max_path == 0) return 0;

    /*
     * Схема кодирования:
     * Каждый байт → два десятичных знака: (byte / 26) и (byte % 26)
     * Но мы используем цифры 0-9, поэтому:
     * Каждый байт → 3 десятичных знака:
     *   d0 = byte / 100
     *   d1 = (byte / 10) % 10
     *   d2 = byte % 10
     *
     * Более компактно: каждые 2 байта → 5 цифр (через base-10)
     * Но для простоты и обратимости: 1 байт → 3 цифры
     */
    size_t out = 0;
    for (size_t i = 0; i < text_len && out + 3 <= max_path; i++) {
        uint8_t b = (uint8_t)text[i];
        path[out++] = b / 100;
        path[out++] = (b / 10) % 10;
        path[out++] = b % 10;
    }
    return out;
}

/* --- Декодирование десятичного пути → текст --- */

size_t kfm_path_to_text(KfmContext *ctx,
                         const uint8_t *path, size_t path_len,
                         char *text, size_t max_text)
{
    (void)ctx; /* может использоваться для контекста в будущем */

    if (!path || !text || path_len == 0 || max_text == 0) return 0;

    /* 3 цифры → 1 байт */
    size_t out = 0;
    size_t i = 0;
    while (i + 3 <= path_len && out < max_text - 1) {
        uint8_t b = (uint8_t)(path[i] * 100 + path[i + 1] * 10 + path[i + 2]);
        text[out++] = (char)b;
        i += 3;
    }
    text[out] = '\0';
    return out;
}

/* --- Сериализация --- */

/*
 * Формат: [magic:4][version:4][node_count:4][tick:8]
 *          [nodes...]
 * Каждый узел: [type:1][depth:1][payload_size:2][payload:N]
 *              [hash:4][access_count:4][activation:4]
 *              [num_associations:1][associations...]
 *              [children_mask:2] (битовая маска 0-9 какие дети есть)
 */

#define KFM_MAGIC   0x4B464D31  /* "KFM1" */
#define KFM_VERSION 1

static size_t kfm_ser_node(const KfmNode *node, uint8_t *buf,
                            size_t buf_size, size_t offset)
{
    if (!node || offset + 32 > buf_size) return offset;

    /* Тип и глубина */
    buf[offset++] = (uint8_t)node->type;
    buf[offset++] = node->depth;

    /* Размер payload (little-endian 16 бит) */
    uint16_t ps = (uint16_t)node->payload_size;
    buf[offset++] = (uint8_t)(ps & 0xFF);
    buf[offset++] = (uint8_t)(ps >> 8);

    /* Payload */
    if (ps > 0 && offset + ps <= buf_size) {
        memcpy(buf + offset, node->payload, ps);
        offset += ps;
    }

    /* Hash (32 бит LE) */
    buf[offset++] = (uint8_t)(node->hash & 0xFF);
    buf[offset++] = (uint8_t)((node->hash >> 8) & 0xFF);
    buf[offset++] = (uint8_t)((node->hash >> 16) & 0xFF);
    buf[offset++] = (uint8_t)((node->hash >> 24) & 0xFF);

    /* Access count (32 бит LE) */
    uint32_t ac = node->access_count;
    buf[offset++] = (uint8_t)(ac & 0xFF);
    buf[offset++] = (uint8_t)((ac >> 8) & 0xFF);
    buf[offset++] = (uint8_t)((ac >> 16) & 0xFF);
    buf[offset++] = (uint8_t)((ac >> 24) & 0xFF);

    /* Activation (float → 32 бит) */
    uint32_t af;
    memcpy(&af, &node->activation, 4);
    buf[offset++] = (uint8_t)(af & 0xFF);
    buf[offset++] = (uint8_t)((af >> 8) & 0xFF);
    buf[offset++] = (uint8_t)((af >> 16) & 0xFF);
    buf[offset++] = (uint8_t)((af >> 24) & 0xFF);

    /* Ассоциации */
    buf[offset++] = node->num_associations;
    for (int i = 0; i < node->num_associations && offset + 72 <= buf_size; i++) {
        const KfmAssociation *a = &node->associations[i];
        buf[offset++] = a->target_len;
        memcpy(buf + offset, a->target_path, a->target_len);
        offset += a->target_len;
        /* padding до KFM_MAX_DEPTH */
        if (a->target_len < KFM_MAX_DEPTH) {
            memset(buf + offset, 0, KFM_MAX_DEPTH - a->target_len);
        }
        offset += KFM_MAX_DEPTH - a->target_len;

        /* Strength (float LE) */
        uint32_t sf;
        memcpy(&sf, &a->strength, 4);
        buf[offset++] = (uint8_t)(sf & 0xFF);
        buf[offset++] = (uint8_t)((sf >> 8) & 0xFF);
        buf[offset++] = (uint8_t)((sf >> 16) & 0xFF);
        buf[offset++] = (uint8_t)((sf >> 24) & 0xFF);

        /* Access count */
        buf[offset++] = (uint8_t)(a->access_count & 0xFF);
        buf[offset++] = (uint8_t)((a->access_count >> 8) & 0xFF);
        buf[offset++] = (uint8_t)((a->access_count >> 16) & 0xFF);
        buf[offset++] = (uint8_t)((a->access_count >> 24) & 0xFF);
    }

    /* Маска потомков (10 бит в 16-битном слове) */
    uint16_t mask = 0;
    for (int d = 0; d < 10; d++) {
        if (node->children[d]) mask |= (1 << d);
    }
    buf[offset++] = (uint8_t)(mask & 0xFF);
    buf[offset++] = (uint8_t)(mask >> 8);

    /* Рекурсивно сериализовать потомков */
    for (int d = 0; d < 10; d++) {
        if (node->children[d]) {
            offset = kfm_ser_node(node->children[d], buf, buf_size, offset);
        }
    }

    return offset;
}

size_t kfm_serialize(KfmContext *ctx, uint8_t *buf, size_t buf_size)
{
    if (!ctx || !buf || buf_size < 20) return 0;

    size_t offset = 0;

    /* Magic */
    uint32_t magic = KFM_MAGIC;
    memcpy(buf + offset, &magic, 4); offset += 4;

    /* Version */
    uint32_t ver = KFM_VERSION;
    memcpy(buf + offset, &ver, 4); offset += 4;

    /* Node count */
    uint32_t nc = (uint32_t)ctx->node_count;
    memcpy(buf + offset, &nc, 4); offset += 4;

    /* Tick */
    memcpy(buf + offset, &ctx->tick, 8); offset += 8;

    /* Дерево */
    offset = kfm_ser_node(ctx->root, buf, buf_size, offset);

    return offset;
}

/* --- Десериализация --- */

static size_t kfm_deser_node(KfmContext *ctx, KfmNode *node,
                               const uint8_t *buf, size_t buf_size,
                               size_t offset)
{
    if (!node || offset + 20 > buf_size) return offset;

    node->type  = (KfmNodeType)buf[offset++];
    node->depth = buf[offset++];

    /* Payload size */
    uint16_t ps = buf[offset] | ((uint16_t)buf[offset + 1] << 8);
    offset += 2;

    if (ps > 0 && ps <= KFM_MAX_PAYLOAD && offset + ps <= buf_size) {
        memcpy(node->payload, buf + offset, ps);
        node->payload_size = ps;
        offset += ps;
    }

    /* Hash */
    node->hash = buf[offset] | ((uint32_t)buf[offset+1] << 8) |
                 ((uint32_t)buf[offset+2] << 16) | ((uint32_t)buf[offset+3] << 24);
    offset += 4;

    /* Access count */
    node->access_count = buf[offset] | ((uint32_t)buf[offset+1] << 8) |
                         ((uint32_t)buf[offset+2] << 16) | ((uint32_t)buf[offset+3] << 24);
    offset += 4;

    /* Activation */
    uint32_t af = buf[offset] | ((uint32_t)buf[offset+1] << 8) |
                  ((uint32_t)buf[offset+2] << 16) | ((uint32_t)buf[offset+3] << 24);
    memcpy(&node->activation, &af, 4);
    offset += 4;

    /* Ассоциации */
    node->num_associations = buf[offset++];
    if (node->num_associations > KFM_MAX_ASSOCIATIONS)
        node->num_associations = KFM_MAX_ASSOCIATIONS;

    for (int i = 0; i < node->num_associations && offset + 72 <= buf_size; i++) {
        KfmAssociation *a = &node->associations[i];
        a->target_len = buf[offset++];
        if (a->target_len > KFM_MAX_DEPTH) a->target_len = KFM_MAX_DEPTH;
        memcpy(a->target_path, buf + offset, a->target_len);
        offset += KFM_MAX_DEPTH; /* включая padding */

        uint32_t sf = buf[offset] | ((uint32_t)buf[offset+1] << 8) |
                      ((uint32_t)buf[offset+2] << 16) | ((uint32_t)buf[offset+3] << 24);
        memcpy(&a->strength, &sf, 4);
        offset += 4;

        a->access_count = buf[offset] | ((uint32_t)buf[offset+1] << 8) |
                          ((uint32_t)buf[offset+2] << 16) | ((uint32_t)buf[offset+3] << 24);
        offset += 4;
    }

    /* Маска потомков */
    uint16_t mask = buf[offset] | ((uint16_t)buf[offset+1] << 8);
    offset += 2;

    for (int d = 0; d < 10; d++) {
        if (mask & (1 << d)) {
            node->children[d] = kfm_node_alloc(ctx, node->depth + 1);
            if (node->children[d]) {
                offset = kfm_deser_node(ctx, node->children[d],
                                         buf, buf_size, offset);
            }
        }
    }

    return offset;
}

int kfm_deserialize(KfmContext *ctx, const uint8_t *buf, size_t buf_size)
{
    if (!ctx || !buf || buf_size < 20) return -1;

    size_t offset = 0;

    /* Magic */
    uint32_t magic;
    memcpy(&magic, buf + offset, 4); offset += 4;
    if (magic != KFM_MAGIC) return -1;

    /* Version */
    uint32_t ver;
    memcpy(&ver, buf + offset, 4); offset += 4;
    if (ver != KFM_VERSION) return -1;

    /* Node count (info only) */
    offset += 4;

    /* Tick */
    memcpy(&ctx->tick, buf + offset, 8); offset += 8;

    /* Очистить старое дерево */
    if (ctx->root) {
        kfm_node_free(ctx->root);
        ctx->node_count = 0;
    }

    /* Дерево */
    ctx->root = kfm_node_alloc(ctx, 0);
    if (!ctx->root) return -1;

    kfm_deser_node(ctx, ctx->root, buf, buf_size, offset);
    return 0;
}

/* --- Статистика --- */

static void kfm_stats_node(const KfmNode *node, KfmStats *stats,
                             size_t depth)
{
    if (!node) return;

    stats->total_nodes++;
    if (node->type == KFM_NODE_CONCEPT) {
        stats->concept_nodes++;
        stats->total_payload_bytes += node->payload_size;
    }
    stats->total_associations += node->num_associations;
    stats->avg_depth += (float)depth;
    stats->avg_activation += node->activation;
    if (depth > stats->max_depth) stats->max_depth = depth;

    for (int d = 0; d < 10; d++) {
        kfm_stats_node(node->children[d], stats, depth + 1);
    }
}

void kfm_stats(const KfmContext *ctx, KfmStats *stats)
{
    if (!ctx || !stats) return;
    memset(stats, 0, sizeof(KfmStats));
    if (!ctx->root) return;

    kfm_stats_node(ctx->root, stats, 0);

    if (stats->total_nodes > 0) {
        stats->avg_depth      /= (float)stats->total_nodes;
        stats->avg_activation /= (float)stats->total_nodes;
    }
}
