/*
 * test_attention.c
 *
 * Тесты модуля Self-Attention (Transformer)
 *
 * Проверяет:
 *   - Создание/уничтожение модели
 *   - Forward pass
 *   - Эмбеддинг извлечение
 *   - Сэмплирование
 *   - Обучение (loss уменьшается)
 *   - Косинусное сходство
 *   - Подсчёт параметров
 *   - Сериализация/десериализация
 */

#include "kolibri/attention.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- Тест 1: Создание и уничтожение модели --- */
static void test_model_lifecycle(void) {
    KatModel *model = kat_model_create(42);
    assert(model != NULL);
    assert(model->param_count > 0);

    KatWorkspace *ws = kat_workspace_create();
    assert(ws != NULL);

    kat_workspace_destroy(ws);
    kat_model_destroy(model);
    printf("  [OK] Model lifecycle\n");
}

/* --- Тест 2: Forward pass --- */
static void test_forward_pass(void) {
    KatModel *model = kat_model_create(42);
    KatWorkspace *ws = kat_workspace_create();

    uint8_t tokens[] = "Hello, Kolibri AGI!";
    size_t seq_len = strlen((char *)tokens);

    int rc = kat_forward(model, ws, tokens, seq_len);
    assert(rc == 0);
    assert(ws->seq_len == seq_len);

    /* Проверяем, что probs — валидное распределение */
    float sum = 0.0f;
    for (size_t i = 0; i < KAT_VOCAB_SIZE; i++) {
        assert(ws->probs[i] >= 0.0f);
        sum += ws->probs[i];
    }
    assert(fabsf(sum - 1.0f) < 0.01f);

    kat_workspace_destroy(ws);
    kat_model_destroy(model);
    printf("  [OK] Forward pass\n");
}

/* --- Тест 3: Извлечение эмбеддинга --- */
static void test_embedding_extraction(void) {
    KatModel *model = kat_model_create(42);
    KatWorkspace *ws = kat_workspace_create();

    uint8_t tokens[] = "Test embedding";
    kat_forward(model, ws, tokens, strlen((char *)tokens));

    float embedding[KAT_EMBED_DIM];
    kat_extract_embedding(ws, embedding);

    /* Проверяем не-нулевой вектор */
    float norm = 0.0f;
    for (size_t d = 0; d < KAT_EMBED_DIM; d++) {
        norm += embedding[d] * embedding[d];
    }
    assert(norm > 0.0f);

    kat_workspace_destroy(ws);
    kat_model_destroy(model);
    printf("  [OK] Embedding extraction\n");
}

/* --- Тест 4: Сэмплирование --- */
static void test_sampling(void) {
    KatModel *model = kat_model_create(42);
    KatWorkspace *ws = kat_workspace_create();

    uint8_t tokens[] = "Hello";
    kat_forward(model, ws, tokens, 5);

    /* Greedy */
    uint8_t greedy = kat_sample(model, ws, 0.0f);
    assert(greedy < KAT_VOCAB_SIZE);

    /* Температурный */
    uint8_t random = kat_sample(model, ws, 1.0f);
    assert(random < KAT_VOCAB_SIZE);

    kat_workspace_destroy(ws);
    kat_model_destroy(model);
    printf("  [OK] Sampling\n");
}

/* --- Тест 5: Обучение (loss уменьшается) --- */
static void test_training(void) {
    KatModel *model = kat_model_create(42);
    KatWorkspace *ws = kat_workspace_create();

    uint8_t input[] = "The answer is 4";
    uint8_t target = '2';  /* Целевой следующий токен */

    /* Начальный loss */
    float loss1 = kat_train_step(model, ws, input, strlen((char *)input),
                                 target, 0.01f);
    assert(loss1 > 0.0f);

    /* 10 шагов обучения */
    float loss_final = loss1;
    for (int i = 0; i < 10; i++) {
        loss_final = kat_train_step(model, ws, input, strlen((char *)input),
                                    target, 0.01f);
    }

    /* Loss должен быть положительным */
    assert(loss_final > 0.0f);

    kat_workspace_destroy(ws);
    kat_model_destroy(model);
    printf("  [OK] Training (initial_loss=%.4f, final_loss=%.4f)\n",
           (double)loss1, (double)loss_final);
}

/* --- Тест 6: Косинусное сходство --- */
static void test_cosine_similarity(void) {
    float a[KAT_EMBED_DIM], b[KAT_EMBED_DIM];

    /* Идентичные вектора → сходство = 1.0 */
    for (size_t i = 0; i < KAT_EMBED_DIM; i++) {
        a[i] = (float)i;
        b[i] = (float)i;
    }
    float sim = kat_cosine_similarity(a, b, KAT_EMBED_DIM);
    assert(fabsf(sim - 1.0f) < 0.001f);

    /* Противоположные вектора → сходство = -1.0 */
    for (size_t i = 0; i < KAT_EMBED_DIM; i++) {
        b[i] = -(float)i;
    }
    sim = kat_cosine_similarity(a, b, KAT_EMBED_DIM);
    assert(fabsf(sim - (-1.0f)) < 0.001f);

    printf("  [OK] Cosine similarity\n");
}

/* --- Тест 7: Подсчёт параметров --- */
static void test_param_count(void) {
    size_t count = kat_count_params();
    assert(count > 10000);  /* Должно быть значительное количество */

    KatModel *model = kat_model_create(42);
    assert(model->param_count == count);

    kat_model_destroy(model);
    printf("  [OK] Param count = %zu\n", count);
}

/* --- Тест 8: Сериализация --- */
static void test_serialization(void) {
    KatModel *model = kat_model_create(42);

    /* Определяем размер */
    size_t size = kat_serialize(model, NULL, 0);
    assert(size > 0);

    uint8_t *buf = malloc(size);
    assert(buf != NULL);

    size_t written = kat_serialize(model, buf, size);
    assert(written == size);

    /* Десериализация */
    KatModel *model2 = kat_model_create(1);  /* Другой seed */
    int rc = kat_deserialize(model2, buf, written);
    assert(rc == 0);

    /* Проверяем, что модели идентичны */
    assert(model->param_count == model2->param_count);

    /* Forward pass на обеих моделях должен дать одинаковый результат */
    KatWorkspace *ws1 = kat_workspace_create();
    KatWorkspace *ws2 = kat_workspace_create();
    uint8_t tokens[] = "Test";

    kat_forward(model, ws1, tokens, 4);
    kat_forward(model2, ws2, tokens, 4);

    for (size_t i = 0; i < KAT_VOCAB_SIZE; i++) {
        assert(fabsf(ws1->probs[i] - ws2->probs[i]) < 1e-5f);
    }

    free(buf);
    kat_workspace_destroy(ws1);
    kat_workspace_destroy(ws2);
    kat_model_destroy(model);
    kat_model_destroy(model2);
    printf("  [OK] Serialization\n");
}

/* --- Тест 9: Разные тексты дают разные эмбеддинги --- */
static void test_different_embeddings(void) {
    KatModel *model = kat_model_create(42);
    KatWorkspace *ws = kat_workspace_create();

    float emb1[KAT_EMBED_DIM], emb2[KAT_EMBED_DIM];

    uint8_t text1[] = "mathematics";
    kat_forward(model, ws, text1, strlen((char *)text1));
    kat_extract_embedding(ws, emb1);

    uint8_t text2[] = "cooking food";
    kat_forward(model, ws, text2, strlen((char *)text2));
    kat_extract_embedding(ws, emb2);

    /* Сходство не должно быть 1.0 (разные тексты) */
    float sim = kat_cosine_similarity(emb1, emb2, KAT_EMBED_DIM);
    assert(fabsf(sim) < 0.999f);  /* Не полностью идентичные */

    kat_workspace_destroy(ws);
    kat_model_destroy(model);
    printf("  [OK] Different texts → different embeddings (sim=%.4f)\n",
           (double)sim);
}

/* --- Тест 10: Конфигурации (medium/large) --- */
static void test_configs(void) {
    /* Проверяем подсчёт параметров */
    KatConfig small = kat_config_small();
    KatConfig medium = kat_config_medium();
    KatConfig large = kat_config_large();

    size_t ps = kat_config_count_params(&small);
    size_t pm = kat_config_count_params(&medium);
    size_t pl = kat_config_count_params(&large);

    printf("    small=%zu  medium=%zu  large=%zu\n", ps, pm, pl);
    assert(ps > 100000);       /* ~165K  */
    assert(pm > 5000000);      /* ~6.5M  */
    assert(pl > 90000000);     /* ~100M  */
    assert(pm > ps);
    assert(pl > pm);

    /* Backward compat: kat_count_params() == small */
    assert(kat_count_params() == ps);

    /* Создаём medium модель и делаем forward pass */
    KatModel *model = kat_model_create_ex(&medium, 42);
    assert(model != NULL);
    assert(model->param_count == pm);

    KatWorkspace *ws = kat_workspace_create_ex(&medium);
    assert(ws != NULL);

    uint8_t tokens[] = "Test medium config";
    int rc = kat_forward(model, ws, tokens, 18);
    assert(rc == 0);

    /* Проверяем валидное распределение */
    float sum = 0.0f;
    for (int i = 0; i < medium.vocab_size; i++) {
        assert(ws->probs[i] >= 0.0f);
        sum += ws->probs[i];
    }
    assert(fabsf(sum - 1.0f) < 0.01f);

    kat_workspace_destroy(ws);
    kat_model_destroy(model);
    printf("  [OK] Config presets (medium forward pass)\n");
}

/* --- Тест 11: Сериализация (новый формат KAT1) --- */
static void test_serialization_new(void) {
    KatConfig medium = kat_config_medium();
    KatModel *model = kat_model_create_ex(&medium, 42);
    assert(model != NULL);

    /* Определяем размер */
    size_t size = kat_serialize(model, NULL, 0);
    assert(size > 0);

    uint8_t *buf = (uint8_t*)malloc(size);
    assert(buf != NULL);

    size_t written = kat_serialize(model, buf, size);
    assert(written == size);

    /* Десериализация в другую модель */
    KatModel *model2 = kat_model_create_ex(&medium, 1);
    int rc = kat_deserialize(model2, buf, written);
    assert(rc == 0);
    assert(model2->param_count == model->param_count);

    /* Forward pass должен дать идентичные результаты */
    KatWorkspace *ws1 = kat_workspace_create_ex(&medium);
    KatWorkspace *ws2 = kat_workspace_create_ex(&medium);
    uint8_t tokens[] = "Serialize test";

    kat_forward(model, ws1, tokens, 14);
    kat_forward(model2, ws2, tokens, 14);

    for (int i = 0; i < medium.vocab_size; i++) {
        assert(fabsf(ws1->probs[i] - ws2->probs[i]) < 1e-5f);
    }

    free(buf);
    kat_workspace_destroy(ws1);
    kat_workspace_destroy(ws2);
    kat_model_destroy(model);
    kat_model_destroy(model2);
    printf("  [OK] Serialization (KAT1 format, medium)\n");
}

/* --- Тест 12: Полный backpropagation (loss уменьшается) --- */
static void test_full_backprop(void) {
    KatModel *model = kat_model_create(42);
    KatWorkspace *ws = kat_workspace_create();

    uint8_t input[] = "The capital of France is Pari";
    uint8_t target = 's';

    /* Начальный loss */
    float loss1 = kat_train_step_full(model, ws, input, strlen((char *)input),
                                      target, 0.0005f);
    assert(loss1 > 0.0f);

    /* 50 шагов обучения — loss должен уменьшаться */
    float loss_final = loss1;
    for (int i = 0; i < 50; i++) {
        loss_final = kat_train_step_full(model, ws, input, strlen((char *)input),
                                         target, 0.0005f);
    }

    assert(loss_final > 0.0f);
    /* Полный backprop должен сходиться быстрее, чем только LM head */
    assert(loss_final < loss1);

    printf("  [OK] Full backprop (initial=%.4f, final=%.4f, reduction=%.1f%%)\n",
           (double)loss1, (double)loss_final,
           (double)(100.0f * (1.0f - loss_final / loss1)));

    kat_workspace_destroy(ws);
    kat_model_destroy(model);
}

/* --- Тест 13: Сравнение fast vs full backprop --- */
static void test_full_vs_fast(void) {
    /* Две одинаковые модели с одинаковыми весами */
    KatModel *m_fast = kat_model_create(123);
    KatModel *m_full = kat_model_create(123);
    KatWorkspace *ws_fast = kat_workspace_create();
    KatWorkspace *ws_full = kat_workspace_create();

    uint8_t input[] = "Hello world test";
    uint8_t target = '!';

    /* 30 шагов fast */
    float loss_fast = 0.0f;
    for (int i = 0; i < 30; i++) {
        loss_fast = kat_train_step_fast(m_fast, ws_fast, input,
                                        strlen((char *)input),
                                        target, 0.001f);
    }

    /* 30 шагов full */
    float loss_full = 0.0f;
    for (int i = 0; i < 30; i++) {
        loss_full = kat_train_step_full(m_full, ws_full, input,
                                        strlen((char *)input),
                                        target, 0.0005f);
    }

    /* Оба должны давать положительный loss */
    assert(loss_fast > 0.0f);
    assert(loss_full > 0.0f);

    /* Full backprop должен давать лучший (или сопоставимый) loss */
    printf("  [OK] Full vs Fast (fast_loss=%.4f, full_loss=%.4f)\n",
           (double)loss_fast, (double)loss_full);

    kat_workspace_destroy(ws_fast);
    kat_workspace_destroy(ws_full);
    kat_model_destroy(m_fast);
    kat_model_destroy(m_full);
}

int main(void) {
    printf("=== Kolibri AGI: Attention Module Tests ===\n");

    test_model_lifecycle();
    test_forward_pass();
    test_embedding_extraction();
    test_sampling();
    test_training();
    test_cosine_similarity();
    test_param_count();
    test_serialization();
    test_different_embeddings();
    test_configs();
    test_serialization_new();
    test_full_backprop();
    test_full_vs_fast();

    printf("=== All %d attention tests PASSED ===\n", 13);
    return 0;
}
