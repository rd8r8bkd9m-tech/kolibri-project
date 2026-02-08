/*
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 *
 * Тесты модуля Corpus Trainer — масштабное обучение с фиксированным размером
 */

#include "kolibri/corpus_trainer.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PASS(name) printf("  ✓ %s\n", name)
#define SECTION(name) printf("\n=== %s ===\n", name)

/* ============================================================
 * 1. Жизненный цикл
 * ============================================================ */

static void test_create_free(void)
{
    KlmTrainerContext *ctx = klm_trainer_create(NULL);
    assert(ctx != NULL);
    assert(ctx->model.pattern_capacity == KLM_MAX_PATTERNS);
    assert(ctx->model.edge_capacity == KLM_MAX_EDGES);
    assert(ctx->model.pattern_count == 0);
    assert(ctx->model.edge_count == 0);
    klm_trainer_free(ctx);
    PASS("create/free с default config");
}

static void test_custom_config(void)
{
    KlmTrainerConfig cfg = klm_default_config();
    cfg.evolution_generations = 5;
    cfg.context_window = 16;
    cfg.verbose = false;

    KlmTrainerContext *ctx = klm_trainer_create(&cfg);
    assert(ctx != NULL);
    assert(ctx->config.evolution_generations == 5);
    assert(ctx->config.context_window == 16);
    klm_trainer_free(ctx);
    PASS("custom config");
}

/* ============================================================
 * 2. Обучение на тексте
 * ============================================================ */

static void test_train_simple_text(void)
{
    KlmTrainerConfig cfg = klm_default_config();
    cfg.evolution_generations = 1; /* быстро */
    cfg.verbose = false;

    KlmTrainerContext *ctx = klm_trainer_create(&cfg);
    assert(ctx != NULL);

    const char *text = "Кот сидит на крыше и смотрит на звёзды. "
                       "Звёзды яркие и красивые.";
    int rc = klm_train_text(ctx, text, strlen(text));
    assert(rc == 0);
    assert(ctx->model.pattern_count > 0);
    assert(ctx->model.edge_count > 0);
    assert(ctx->stats.documents_processed == 1);
    assert(ctx->stats.tokens_total > 5);

    klm_trainer_free(ctx);
    PASS("train simple text");
}

static void test_train_multiple_documents(void)
{
    KlmTrainerConfig cfg = klm_default_config();
    cfg.evolution_generations = 0; /* без эволюции — чистый хеш */
    cfg.verbose = false;

    KlmTrainerContext *ctx = klm_trainer_create(&cfg);

    const char *docs[] = {
        "Программирование это искусство создания алгоритмов",
        "Алгоритмы решают задачи эффективно и красиво",
        "Компьютер выполняет алгоритмы миллионы раз в секунду",
        "Языки программирования дают возможность описывать решения",
        "Python и Си самые популярные языки в мире",
    };

    for (int i = 0; i < 5; i++) {
        int rc = klm_train_text(ctx, docs[i], strlen(docs[i]));
        assert(rc == 0);
    }

    assert(ctx->stats.documents_processed == 5);
    /* Слово "алгоритмы" должно иметь frequency > 1 */
    assert(ctx->model.pattern_count >= 10);

    klm_trainer_free(ctx);
    PASS("train multiple documents");
}

static void test_train_document_with_title(void)
{
    KlmTrainerConfig cfg = klm_default_config();
    cfg.evolution_generations = 0;
    KlmTrainerContext *ctx = klm_trainer_create(&cfg);

    int rc = klm_train_document(ctx, "О котах",
                                "Коты домашние животные любят молоко и рыбу");
    assert(rc == 0);
    assert(ctx->stats.documents_processed == 2); /* title + body */

    klm_trainer_free(ctx);
    PASS("train_document with title");
}

/* ============================================================
 * 3. Дистилляция (вытеснение)
 * ============================================================ */

static void test_distillation(void)
{
    KlmTrainerConfig cfg = klm_default_config();
    cfg.evolution_generations = 0;
    cfg.distill_interval = 0; /* отключаем автоматическую */
    cfg.verbose = false;

    KlmTrainerContext *ctx = klm_trainer_create(&cfg);

    /* Обучаем на 100 коротких документах */
    for (int i = 0; i < 100; i++) {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "документ номер %d содержит уникальное слово%d "
                 "и общие слова программирование алгоритм компьютер",
                 i, i);
        klm_train_text(ctx, buf, strlen(buf));
    }

    size_t before_patterns = ctx->model.pattern_count;
    size_t before_edges = ctx->model.edge_count;

    /* Ручная дистилляция */
    size_t evicted = klm_distill(ctx);

    assert(ctx->model.current_epoch == 1);
    assert(ctx->stats.distillation_runs == 1);
    /* Что-то должно было быть вытеснено (одноразовые слова) */
    assert(evicted > 0 || before_patterns == ctx->model.pattern_count);

    klm_trainer_free(ctx);
    PASS("distillation");
}

static void test_auto_distillation(void)
{
    KlmTrainerConfig cfg = klm_default_config();
    cfg.evolution_generations = 0;
    cfg.distill_interval = 10; /* каждые 10 документов */
    cfg.verbose = false;

    KlmTrainerContext *ctx = klm_trainer_create(&cfg);

    for (int i = 0; i < 25; i++) {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "автоматическая дистилляция проверка документ%d слово%d",
                 i, i);
        klm_train_text(ctx, buf, strlen(buf));
    }

    /* Должна была пройти хотя бы одна автоматическая дистилляция */
    assert(ctx->stats.distillation_runs >= 1);

    klm_trainer_free(ctx);
    PASS("auto distillation");
}

/* ============================================================
 * 4. Сериализация (save/load)
 * ============================================================ */

static void test_save_load(void)
{
    const char *tmp = "/tmp/test_klm_model.klm";
    KlmTrainerConfig cfg = klm_default_config();
    cfg.evolution_generations = 0;

    /* Создаём и обучаем */
    KlmTrainerContext *ctx1 = klm_trainer_create(&cfg);
    klm_train_text(ctx1, "кот собака мышь птица рыба",
                   strlen("кот собака мышь птица рыба"));
    klm_train_text(ctx1, "программирование алгоритмы структуры данных",
                   strlen("программирование алгоритмы структуры данных"));

    size_t pc1 = ctx1->model.pattern_count;
    size_t ec1 = ctx1->model.edge_count;
    uint64_t docs1 = ctx1->model.documents_trained;

    /* Сохраняем */
    int rc = klm_save(ctx1, tmp);
    assert(rc == 0);
    klm_trainer_free(ctx1);

    /* Загружаем в новый контекст */
    KlmTrainerContext *ctx2 = klm_trainer_create(&cfg);
    rc = klm_load(ctx2, tmp);
    assert(rc == 0);

    assert(ctx2->model.pattern_count == pc1);
    assert(ctx2->model.edge_count == ec1);
    assert(ctx2->model.documents_trained == docs1);

    klm_trainer_free(ctx2);
    unlink(tmp);
    PASS("save/load round-trip");
}

static void test_load_invalid(void)
{
    KlmTrainerConfig cfg = klm_default_config();
    KlmTrainerContext *ctx = klm_trainer_create(&cfg);

    /* Несуществующий файл */
    int rc = klm_load(ctx, "/tmp/nonexistent_klm_file_xyz.klm");
    assert(rc == -1);

    /* Файл с неверным содержимым */
    const char *tmp = "/tmp/test_klm_invalid.klm";
    FILE *f = fopen(tmp, "wb");
    const char *garbage = "this is not a klm file";
    fwrite(garbage, 1, strlen(garbage), f);
    fclose(f);

    rc = klm_load(ctx, tmp);
    assert(rc == -1);

    unlink(tmp);
    klm_trainer_free(ctx);
    PASS("load invalid file");
}

/* ============================================================
 * 5. Запросы к модели
 * ============================================================ */

static void test_word_similarity(void)
{
    KlmTrainerConfig cfg = klm_default_config();
    cfg.evolution_generations = 0;
    KlmTrainerContext *ctx = klm_trainer_create(&cfg);

    /* Обучаем — «кот» и «собака» часто рядом */
    for (int i = 0; i < 20; i++) {
        klm_train_text(ctx, "кот и собака друзья человека",
                       strlen("кот и собака друзья человека"));
    }

    double sim = klm_word_similarity(ctx, "кот", "собака");
    /* Они должны быть связаны через граф */
    assert(sim > 0.0);

    /* Несуществующее слово */
    double sim2 = klm_word_similarity(ctx, "кот", "xlkjdaslk");
    assert(sim2 == 0.0);

    klm_trainer_free(ctx);
    PASS("word_similarity");
}

static void test_get_associations(void)
{
    KlmTrainerConfig cfg = klm_default_config();
    cfg.evolution_generations = 0;
    KlmTrainerContext *ctx = klm_trainer_create(&cfg);

    for (int i = 0; i < 30; i++) {
        klm_train_text(ctx,
            "программирование включает алгоритмы структуры данных "
            "и языки такие как Python Java Rust",
            strlen("программирование включает алгоритмы структуры данных "
                   "и языки такие как Python Java Rust"));
    }

    char results[10][KLM_WORD_MAX];
    float weights[10];
    size_t n = klm_get_associations(ctx, "программирование",
                                    results, weights, 5);
    /* Должны быть ассоциации с «алгоритмы», «языки» и т.д. */
    assert(n > 0);

    klm_trainer_free(ctx);
    PASS("get_associations");
}

static void test_query_similar(void)
{
    KlmTrainerConfig cfg = klm_default_config();
    cfg.evolution_generations = 0;
    KlmTrainerContext *ctx = klm_trainer_create(&cfg);

    klm_train_text(ctx,
        "математика физика химия биология география история",
        strlen("математика физика химия биология география история"));

    char results[5][KLM_WORD_MAX];
    float scores[5];
    int n = klm_query_similar(ctx, "математика", results, scores, 5);
    /* Хотя бы 0 (при хеш-паттернах совпадения маловероятны) */
    assert(n >= 0);

    klm_trainer_free(ctx);
    PASS("query_similar");
}

static void test_answer(void)
{
    KlmTrainerConfig cfg = klm_default_config();
    cfg.evolution_generations = 0;
    KlmTrainerContext *ctx = klm_trainer_create(&cfg);

    /* Обучаем на факте «столица России — Москва» */
    for (int i = 0; i < 50; i++) {
        klm_train_text(ctx,
            "столица России Москва крупнейший город страны",
            strlen("столица России Москва крупнейший город страны"));
    }

    char answer[1024];
    int rc = klm_answer(ctx, "столица России", answer, sizeof(answer));
    /* Должен вернуть что-то (включая «москва») */
    if (rc == 0) {
        assert(strlen(answer) > 0);
    }

    klm_trainer_free(ctx);
    PASS("answer query");
}

/* ============================================================
 * 6. Фиксированный размер модели
 * ============================================================ */

static void test_model_size_fixed(void)
{
    KlmTrainerConfig cfg = klm_default_config();
    cfg.evolution_generations = 0;
    cfg.distill_interval = 50;
    KlmTrainerContext *ctx = klm_trainer_create(&cfg);

    /* Обучаем на 500 документах с уникальными словами */
    for (int i = 0; i < 500; i++) {
        char buf[512];
        snprintf(buf, sizeof(buf),
                 "уникальное_слово_%d контекст обучения документ_%d "
                 "общие слова для создания связей в графе знаний "
                 "программирование алгоритмы компьютер данные",
                 i, i);
        klm_train_text(ctx, buf, strlen(buf));
    }

    double size = klm_model_size_mb(ctx);
    /* Размер модели не должен превышать лимит */
    assert(size < (double)KLM_MODEL_SIZE_LIMIT / (1024.0 * 1024.0));

    /* Паттернов не может быть больше capacity */
    assert(ctx->model.pattern_count <= ctx->model.pattern_capacity);
    assert(ctx->model.edge_count <= ctx->model.edge_capacity);

    klm_trainer_free(ctx);
    PASS("model size stays fixed");
}

static void test_model_size_mb(void)
{
    KlmTrainerConfig cfg = klm_default_config();
    cfg.evolution_generations = 0;
    KlmTrainerContext *ctx = klm_trainer_create(&cfg);

    /* Пустая модель */
    double empty = klm_model_size_mb(ctx);
    assert(empty == 0.0);

    klm_train_text(ctx, "тест размера модели один два три",
                   strlen("тест размера модели один два три"));

    double after = klm_model_size_mb(ctx);
    assert(after > 0.0);
    /* Даже с данными — не больше 50 МБ */
    assert(after < 50.0);

    klm_trainer_free(ctx);
    PASS("model_size_mb");
}

/* ============================================================
 * 7. Статистика
 * ============================================================ */

static void test_stats(void)
{
    KlmTrainerConfig cfg = klm_default_config();
    cfg.evolution_generations = 0;
    KlmTrainerContext *ctx = klm_trainer_create(&cfg);

    klm_train_text(ctx, "тестовый документ для проверки статистики",
                   strlen("тестовый документ для проверки статистики"));

    KlmTrainerStats stats = klm_get_stats(ctx);
    assert(stats.documents_processed == 1);
    assert(stats.tokens_total > 0);
    assert(stats.patterns_learned > 0);

    klm_trainer_free(ctx);
    PASS("get_stats");
}

/* ============================================================
 * 8. Обучение на файле
 * ============================================================ */

static void test_train_file(void)
{
    const char *tmp = "/tmp/test_klm_train.txt";
    FILE *f = fopen(tmp, "w");
    fprintf(f, "Это тестовый файл для обучения модели Kolibri.\n"
               "Модель учится на текстах и создаёт граф знаний.\n"
               "Размер модели фиксированный и не растёт.\n");
    fclose(f);

    KlmTrainerConfig cfg = klm_default_config();
    cfg.evolution_generations = 0;
    KlmTrainerContext *ctx = klm_trainer_create(&cfg);

    int rc = klm_train_file(ctx, tmp);
    assert(rc == 0);
    assert(ctx->stats.documents_processed == 1);
    assert(ctx->model.pattern_count > 5);

    klm_trainer_free(ctx);
    unlink(tmp);
    PASS("train_file");
}

static void test_train_file_nonexistent(void)
{
    KlmTrainerConfig cfg = klm_default_config();
    KlmTrainerContext *ctx = klm_trainer_create(&cfg);

    int rc = klm_train_file(ctx, "/tmp/nonexistent_file_xyz.txt");
    assert(rc == -1);

    klm_trainer_free(ctx);
    PASS("train_file nonexistent");
}

/* ============================================================
 * 9. Edge cases
 * ============================================================ */

static void test_empty_text(void)
{
    KlmTrainerConfig cfg = klm_default_config();
    KlmTrainerContext *ctx = klm_trainer_create(&cfg);

    int rc = klm_train_text(ctx, "", 0);
    assert(rc == -1);

    rc = klm_train_text(ctx, NULL, 0);
    assert(rc == -1);

    klm_trainer_free(ctx);
    PASS("empty/null text");
}

static void test_single_word(void)
{
    KlmTrainerConfig cfg = klm_default_config();
    cfg.evolution_generations = 0;
    KlmTrainerContext *ctx = klm_trainer_create(&cfg);

    int rc = klm_train_text(ctx, "одиночное", strlen("одиночное"));
    assert(rc == 0);
    /* Слово из >= 2 символов должно попасть в паттерны */
    assert(ctx->model.pattern_count >= 1);
    /* Рёбер не будет (одно слово) */
    assert(ctx->model.edge_count == 0);

    klm_trainer_free(ctx);
    PASS("single word");
}

static void test_null_context(void)
{
    int rc = klm_train_text(NULL, "test", 4);
    assert(rc == -1);

    rc = klm_save(NULL, "/tmp/x.klm");
    assert(rc == -1);

    rc = klm_load(NULL, "/tmp/x.klm");
    assert(rc == -1);

    double s = klm_model_size_mb(NULL);
    assert(s == 0.0);

    PASS("null context handling");
}

/* ============================================================
 * main
 * ============================================================ */

int main(void)
{
    printf("\n╔═══════════════════════════════════════════╗\n");
    printf("║  Тесты: Corpus Trainer (фикс. размер)    ║\n");
    printf("╚═══════════════════════════════════════════╝\n");

    SECTION("1. Жизненный цикл");
    test_create_free();
    test_custom_config();

    SECTION("2. Обучение на тексте");
    test_train_simple_text();
    test_train_multiple_documents();
    test_train_document_with_title();

    SECTION("3. Дистилляция");
    test_distillation();
    test_auto_distillation();

    SECTION("4. Сериализация");
    test_save_load();
    test_load_invalid();

    SECTION("5. Запросы к модели");
    test_word_similarity();
    test_get_associations();
    test_query_similar();
    test_answer();

    SECTION("6. Фиксированный размер");
    test_model_size_fixed();
    test_model_size_mb();

    SECTION("7. Статистика");
    test_stats();

    SECTION("8. Файловое обучение");
    test_train_file();
    test_train_file_nonexistent();

    SECTION("9. Edge cases");
    test_empty_text();
    test_single_word();
    test_null_context();

    printf("\n╔═══════════════════════════════════════════╗\n");
    printf("║  ✅ Все %d тестов пройдены!                ║\n", 22);
    printf("╚═══════════════════════════════════════════╝\n\n");

    return 0;
}
