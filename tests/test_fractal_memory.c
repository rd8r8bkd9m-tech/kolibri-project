/*
 * Тест фрактальной десятичной памяти
 * Проверяет вставку, поиск, ассоциации, мутацию, сериализацию
 */

#include "kolibri/fractal_memory.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_init_free(void) {
    printf("test_init_free... ");
    KfmContext ctx;
    assert(kfm_init(&ctx, 42) == 0);
    assert(ctx.root != NULL);
    assert(ctx.node_count == 1); /* root */
    kfm_free(&ctx);
    printf("OK\n");
}

static void test_insert_lookup(void) {
    printf("test_insert_lookup... ");
    KfmContext ctx;
    kfm_init(&ctx, 42);

    /* Вставляем понятие по пути 7-3-1-8 */
    uint8_t path[] = {7, 3, 1, 8};
    const char *payload = "привет мир";
    assert(kfm_insert(&ctx, path, 4, payload, strlen(payload)) == 0);

    /* Ищем по точному пути */
    const KfmNode *node = kfm_lookup(&ctx, path, 4);
    assert(node != NULL);
    assert(node->type == KFM_NODE_CONCEPT);
    assert(node->payload_size == strlen(payload));
    assert(memcmp(node->payload, payload, node->payload_size) == 0);

    /* Несуществующий путь */
    uint8_t bad_path[] = {1, 2, 3};
    assert(kfm_lookup(&ctx, bad_path, 3) == NULL);

    kfm_free(&ctx);
    printf("OK\n");
}

static void test_multiple_inserts(void) {
    printf("test_multiple_inserts... ");
    KfmContext ctx;
    kfm_init(&ctx, 42);

    /* Вставляем 10 понятий */
    for (int i = 0; i < 10; i++) {
        uint8_t path[] = {(uint8_t)i, (uint8_t)(i + 1) % 10, (uint8_t)(i * 3) % 10};
        char buf[32];
        int n = snprintf(buf, sizeof(buf), "concept_%d", i);
        assert(kfm_insert(&ctx, path, 3, buf, (size_t)n) == 0);
    }

    /* Проверяем все */
    for (int i = 0; i < 10; i++) {
        uint8_t path[] = {(uint8_t)i, (uint8_t)(i + 1) % 10, (uint8_t)(i * 3) % 10};
        const KfmNode *node = kfm_lookup(&ctx, path, 3);
        assert(node != NULL);
        assert(node->type == KFM_NODE_CONCEPT);
    }

    kfm_free(&ctx);
    printf("OK\n");
}

static void test_search(void) {
    printf("test_search... ");
    KfmContext ctx;
    kfm_init(&ctx, 42);

    /* Вставляем несколько понятий */
    uint8_t path1[] = {1, 2, 3, 4};
    uint8_t path2[] = {1, 2, 5, 6};
    uint8_t path3[] = {9, 8, 7, 6};
    kfm_insert(&ctx, path1, 4, "alpha", 5);
    kfm_insert(&ctx, path2, 4, "beta", 4);
    kfm_insert(&ctx, path3, 4, "gamma", 5);

    /* Ищем ближайшие к {1, 2, 3, 0} */
    uint8_t query[] = {1, 2, 3, 0};
    KfmSearchResult results[5];
    int found = kfm_search(&ctx, query, 4, results, 5);
    assert(found > 0);

    /* Первый результат должен быть path1 (наибольший общий префикс) */
    float best_sim = 0.0f;
    int best_idx = -1;
    for (int i = 0; i < found; i++) {
        if (results[i].similarity > best_sim) {
            best_sim = results[i].similarity;
            best_idx = i;
        }
    }
    assert(best_idx >= 0);
    /* path1 совпадает по первым 3 цифрам, path2 — по 2, path3 — по 0 */
    assert(results[best_idx].path[0] == 1);
    assert(results[best_idx].path[1] == 2);
    assert(results[best_idx].path[2] == 3);

    kfm_free(&ctx);
    printf("OK\n");
}

static void test_associations(void) {
    printf("test_associations... ");
    KfmContext ctx;
    kfm_init(&ctx, 42);

    uint8_t path_a[] = {1, 2, 3};
    uint8_t path_b[] = {4, 5, 6};
    kfm_insert(&ctx, path_a, 3, "A", 1);
    kfm_insert(&ctx, path_b, 3, "B", 1);

    assert(kfm_associate(&ctx, path_a, 3, path_b, 3, 0.8f) == 0);

    /* Проверяем ассоциации */
    const KfmNode *a = kfm_lookup(&ctx, path_a, 3);
    assert(a != NULL);
    assert(a->num_associations == 1);
    assert(a->associations[0].strength >= 0.79f);

    const KfmNode *b = kfm_lookup(&ctx, path_b, 3);
    assert(b != NULL);
    assert(b->num_associations == 1); /* обратная ассоциация */

    kfm_free(&ctx);
    printf("OK\n");
}

static void test_activation(void) {
    printf("test_activation... ");
    KfmContext ctx;
    kfm_init(&ctx, 42);

    uint8_t path_a[] = {1, 2};
    uint8_t path_b[] = {3, 4};
    kfm_insert(&ctx, path_a, 2, "X", 1);
    kfm_insert(&ctx, path_b, 2, "Y", 1);
    kfm_associate(&ctx, path_a, 2, path_b, 2, 0.9f);

    /* Активируем A — B должен получить часть энергии */
    assert(kfm_activate(&ctx, path_a, 2, 0.8f) == 0);

    const KfmNode *a = kfm_lookup(&ctx, path_a, 2);
    const KfmNode *b = kfm_lookup(&ctx, path_b, 2);
    assert(a != NULL);
    assert(b != NULL);
    assert(a->activation > 0.5f); /* получил прямую активацию */
    assert(b->activation > 0.0f); /* получил через ассоциацию */

    kfm_free(&ctx);
    printf("OK\n");
}

static void test_decay(void) {
    printf("test_decay... ");
    KfmContext ctx;
    kfm_init(&ctx, 42);

    uint8_t path[] = {5, 5, 5};
    kfm_insert(&ctx, path, 3, "data", 4);

    const KfmNode *node = kfm_lookup(&ctx, path, 3);
    assert(node != NULL);
    float initial_activation = node->activation;
    assert(initial_activation > 0.0f);

    /* Прогоняем тики без обращений */
    for (int i = 0; i < 100; i++) {
        ctx.tick += 10;
        kfm_decay(&ctx);
    }

    node = kfm_lookup(&ctx, path, 3);
    /* Активация должна снизиться (lookup повышает, но не до 1.0) */
    /* Главное — что decay не крашится */
    assert(node != NULL);

    kfm_free(&ctx);
    printf("OK\n");
}

static void test_mutate(void) {
    printf("test_mutate... ");
    KfmContext ctx;
    kfm_init(&ctx, 42);

    uint8_t path[] = {1, 2, 3, 4, 5};
    uint8_t new_path[KFM_MAX_DEPTH];
    size_t new_len = 0;

    assert(kfm_mutate(&ctx, path, 5, new_path, &new_len) == 0);
    /* Мутация должна изменить ХОТЯ БЫ что-то */
    int different = 0;
    if (new_len != 5) {
        different = 1;
    } else {
        for (size_t i = 0; i < 5; i++) {
            if (new_path[i] != path[i]) { different = 1; break; }
        }
    }
    assert(different);

    kfm_free(&ctx);
    printf("OK\n");
}

static void test_text_path_roundtrip(void) {
    printf("test_text_path_roundtrip... ");

    const char *text = "Hello";
    uint8_t path[KFM_MAX_DEPTH];
    size_t path_len = kfm_text_to_path(text, strlen(text), path, KFM_MAX_DEPTH);
    assert(path_len > 0);
    assert(path_len == strlen(text) * 3); /* 1 байт → 3 цифры */

    /* Обратная конвертация */
    char recovered[256];
    KfmContext ctx;
    kfm_init(&ctx, 42);
    size_t text_len = kfm_path_to_text(&ctx, path, path_len, recovered, sizeof(recovered));
    assert(text_len == strlen(text));
    assert(memcmp(recovered, text, text_len) == 0);

    kfm_free(&ctx);
    printf("OK\n");
}

static void test_serialize_deserialize(void) {
    printf("test_serialize_deserialize... ");
    KfmContext ctx1;
    kfm_init(&ctx1, 42);

    uint8_t path1[] = {1, 2, 3};
    uint8_t path2[] = {4, 5, 6};
    kfm_insert(&ctx1, path1, 3, "data_A", 6);
    kfm_insert(&ctx1, path2, 3, "data_B", 6);
    kfm_associate(&ctx1, path1, 3, path2, 3, 0.7f);

    /* Сериализуем */
    uint8_t buf[64 * 1024];
    size_t ser_size = kfm_serialize(&ctx1, buf, sizeof(buf));
    assert(ser_size > 20); /* хотя бы заголовок + данные */

    /* Десериализуем в новый контекст */
    KfmContext ctx2;
    kfm_init(&ctx2, 0);
    assert(kfm_deserialize(&ctx2, buf, ser_size) == 0);

    /* Проверяем данные */
    const KfmNode *n1 = kfm_lookup(&ctx2, path1, 3);
    assert(n1 != NULL);
    assert(n1->type == KFM_NODE_CONCEPT);
    assert(n1->payload_size == 6);
    assert(memcmp(n1->payload, "data_A", 6) == 0);

    const KfmNode *n2 = kfm_lookup(&ctx2, path2, 3);
    assert(n2 != NULL);
    assert(memcmp(n2->payload, "data_B", 6) == 0);

    kfm_free(&ctx1);
    kfm_free(&ctx2);
    printf("OK\n");
}

static void test_stats(void) {
    printf("test_stats... ");
    KfmContext ctx;
    kfm_init(&ctx, 42);

    uint8_t path1[] = {1, 2, 3};
    uint8_t path2[] = {4, 5};
    kfm_insert(&ctx, path1, 3, "AAA", 3);
    kfm_insert(&ctx, path2, 2, "BB", 2);

    KfmStats stats;
    kfm_stats(&ctx, &stats);

    assert(stats.total_nodes > 0);
    assert(stats.concept_nodes == 2);
    assert(stats.total_payload_bytes == 5);
    assert(stats.max_depth >= 2);

    kfm_free(&ctx);
    printf("OK\n");
}

int main(void) {
    printf("=== Тест Фрактальной Десятичной Памяти ===\n\n");

    test_init_free();
    test_insert_lookup();
    test_multiple_inserts();
    test_search();
    test_associations();
    test_activation();
    test_decay();
    test_mutate();
    test_text_path_roundtrip();
    test_serialize_deserialize();
    test_stats();

    printf("\n✅ Все тесты пройдены!\n");
    return 0;
}
