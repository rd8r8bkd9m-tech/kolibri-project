/*
 * test_formula_logic.c
 *
 * Полноценные unit-тесты для модуля мета-формул (formula_logic)
 * Каждый тест использует assert для верификации корректности
 */

#include "kolibri/formula_logic.h"
#include "kolibri/logical_memory.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===== Вспомогательные макросы ===== */

#define TEST_BEGIN(name) \
    do { printf("  [TEST] %-50s", name); fflush(stdout); } while(0)
#define TEST_PASS() \
    do { printf(" ✓\n"); tests_passed++; } while(0)

static int tests_run = 0;
static int tests_passed = 0;

/* ===== ТЕСТЫ ЖИЗНЕННОГО ЦИКЛА ===== */

static void test_create_destroy_store(void) {
    TEST_BEGIN("create/destroy store"); tests_run++;

    MetaFormulaStore *store = mf_create_store();
    assert(store != NULL);
    assert(store->count == 0);
    assert(store->cache_count == 0);

    mf_destroy_store(store);
    TEST_PASS();
}

static void test_create_destroy_meta_formula(void) {
    TEST_BEGIN("create/destroy meta formula"); tests_run++;

    MetaFormula *mf = mf_create_meta_formula();
    assert(mf != NULL);
    mf_destroy_meta_formula(mf);
    mf_destroy_meta_formula(NULL);  /* NULL-безопасность */

    TEST_PASS();
}

/* ===== ТЕСТЫ КОНСТРУКТОРОВ ===== */

static void test_create_repeat_generator(void) {
    TEST_BEGIN("create repeat generator"); tests_run++;

    MetaFormula *mf = mf_create_repeat_generator("ABC", "10");
    assert(mf != NULL);
    assert(mf->operation == META_GENERATE_REPEAT);
    assert(strcmp(mf->params.gen_repeat.pattern_formula, "ABC") == 0);
    assert(strcmp(mf->params.gen_repeat.count_formula, "10") == 0);
    assert(mf->generation == 0);
    assert(mf->complexity_score > 0.0);

    free(mf);
    TEST_PASS();
}

static void test_create_sequence_generator(void) {
    TEST_BEGIN("create sequence generator"); tests_run++;

    MetaFormula *mf = mf_create_sequence_generator("0", "2", "5");
    assert(mf != NULL);
    assert(mf->operation == META_GENERATE_SEQUENCE);
    assert(strcmp(mf->params.gen_sequence.start_formula, "0") == 0);
    assert(strcmp(mf->params.gen_sequence.step_formula, "2") == 0);
    assert(strcmp(mf->params.gen_sequence.count_formula, "5") == 0);

    free(mf);
    TEST_PASS();
}

static void test_create_transformer(void) {
    TEST_BEGIN("create transformer"); tests_run++;

    MetaFormula *mf = mf_create_transformer("cell_a", "double_count");
    assert(mf != NULL);
    assert(mf->operation == META_TRANSFORM_LOGIC);
    assert(strcmp(mf->params.transform.input_logic_id, "cell_a") == 0);
    assert(strcmp(mf->params.transform.transform_rule, "double_count") == 0);

    free(mf);
    TEST_PASS();
}

static void test_create_relation_deriver(void) {
    TEST_BEGIN("create relation deriver"); tests_run++;

    MetaFormula *mf = mf_create_relation_deriver("left", "right", "transitive");
    assert(mf != NULL);
    assert(mf->operation == META_DERIVE_RELATION);
    assert(strcmp(mf->params.derive.inference_rule, "transitive") == 0);

    free(mf);
    TEST_PASS();
}

static void test_create_pattern_evolver(void) {
    TEST_BEGIN("create pattern evolver"); tests_run++;

    MetaFormula *mf = mf_create_pattern_evolver("src", 0.5, 10);
    assert(mf != NULL);
    assert(mf->operation == META_EVOLVE_PATTERN);
    assert(strcmp(mf->params.evolve.source_pattern_id, "src") == 0);
    assert(mf->params.evolve.mutation_rate == 0.5);
    assert(mf->params.evolve.generations == 10);

    free(mf);
    TEST_PASS();
}

static void test_create_logic_compressor(void) {
    TEST_BEGIN("create logic compressor"); tests_run++;

    MetaFormula *mf = mf_create_logic_compressor("target", "merge_repeats");
    assert(mf != NULL);
    assert(mf->operation == META_COMPRESS_LOGIC);
    assert(strcmp(mf->params.compress.target_logic_id, "target") == 0);
    assert(strcmp(mf->params.compress.compression_strategy, "merge_repeats") == 0);

    free(mf);
    TEST_PASS();
}

/* ===== ТЕСТЫ ВЫПОЛНЕНИЯ (mf_execute) ===== */

static void test_execute_constant(void) {
    TEST_BEGIN("execute GENERATE_CONSTANT"); tests_run++;

    MetaFormulaStore *store = mf_create_store();
    LogicalMemory *mem = lm_create_memory();

    MetaFormula *mf = mf_create_meta_formula();
    mf->operation = META_GENERATE_CONSTANT;
    mf->params.generate_constant.value = strdup("Hello");

    LogicExpression *result = mf_execute(store, mf, mem);
    assert(result != NULL);
    assert(result->type == LOGIC_CONSTANT);
    assert(strcmp(result->data.constant.value, "Hello") == 0);
    assert(store->cache_count == 1);

    mf_destroy_meta_formula(mf);
    mf_destroy_store(store);
    lm_destroy_memory(mem);
    TEST_PASS();
}

static void test_execute_repeat(void) {
    TEST_BEGIN("execute GENERATE_REPEAT"); tests_run++;

    MetaFormulaStore *store = mf_create_store();
    LogicalMemory *mem = lm_create_memory();

    MetaFormula *mf = mf_create_repeat_generator("XY", "5");
    LogicExpression *result = mf_execute(store, mf, mem);
    assert(result != NULL);
    assert(result->type == LOGIC_REPEAT);
    assert(result->data.repeat.count == 5);

    char *text = lm_materialize_logic(result);
    assert(text != NULL);
    assert(strcmp(text, "XYXYXYXYXY") == 0);
    free(text);

    free(mf);
    mf_destroy_store(store);
    lm_destroy_memory(mem);
    TEST_PASS();
}

static void test_execute_repeat_expression(void) {
    TEST_BEGIN("execute repeat with expr 3*4=12"); tests_run++;

    MetaFormulaStore *store = mf_create_store();
    LogicalMemory *mem = lm_create_memory();

    MetaFormula *mf = mf_create_repeat_generator("Z", "3*4");
    LogicExpression *result = mf_execute(store, mf, mem);
    assert(result != NULL);
    assert(result->data.repeat.count == 12);

    free(mf);
    mf_destroy_store(store);
    lm_destroy_memory(mem);
    TEST_PASS();
}

static void test_execute_sequence(void) {
    TEST_BEGIN("execute GENERATE_SEQUENCE"); tests_run++;

    MetaFormulaStore *store = mf_create_store();
    LogicalMemory *mem = lm_create_memory();

    MetaFormula *mf = mf_create_sequence_generator("1", "1", "4");
    LogicExpression *result = mf_execute(store, mf, mem);
    assert(result != NULL);
    assert(result->type == LOGIC_SEQUENCE);

    char *text = lm_materialize_logic(result);
    assert(text != NULL);
    assert(strcmp(text, "1234") == 0);
    free(text);

    free(mf);
    mf_destroy_store(store);
    lm_destroy_memory(mem);
    TEST_PASS();
}

static void test_execute_compose(void) {
    TEST_BEGIN("execute GENERATE_COMPOSE"); tests_run++;

    MetaFormulaStore *store = mf_create_store();
    LogicalMemory *mem = lm_create_memory();

    /* Наполняем кэш */
    MetaFormula *mf1 = mf_create_repeat_generator("A", "3");
    MetaFormula *mf2 = mf_create_repeat_generator("B", "2");
    assert(mf_execute(store, mf1, mem) != NULL);
    assert(mf_execute(store, mf2, mem) != NULL);
    assert(store->cache_count == 2);

    MetaFormula compose_mf;
    memset(&compose_mf, 0, sizeof(compose_mf));
    compose_mf.operation = META_GENERATE_COMPOSE;

    LogicExpression *composed = mf_execute(store, &compose_mf, mem);
    assert(composed != NULL);
    assert(composed->type == LOGIC_COMPOSITION);

    free(mf1);
    free(mf2);
    mf_destroy_store(store);
    lm_destroy_memory(mem);
    TEST_PASS();
}

static void test_execute_transform_double(void) {
    TEST_BEGIN("execute TRANSFORM double_count"); tests_run++;

    MetaFormulaStore *store = mf_create_store();
    LogicalMemory *mem = lm_create_memory();

    lm_store_logic(mem, "src", lm_logic_repeat("X", 5));

    MetaFormula *mf = mf_create_transformer("src", "double_count");
    LogicExpression *result = mf_execute(store, mf, mem);
    assert(result != NULL);
    assert(result->type == LOGIC_REPEAT);
    assert(result->data.repeat.count == 10);

    char *text = lm_materialize_logic(result);
    assert(text != NULL);
    assert(strcmp(text, "XXXXXXXXXX") == 0);
    free(text);

    free(mf);
    mf_destroy_store(store);
    lm_destroy_memory(mem);
    TEST_PASS();
}

static void test_execute_transform_half(void) {
    TEST_BEGIN("execute TRANSFORM half_count"); tests_run++;

    MetaFormulaStore *store = mf_create_store();
    LogicalMemory *mem = lm_create_memory();

    lm_store_logic(mem, "big", lm_logic_repeat("AB", 20));

    MetaFormula *mf = mf_create_transformer("big", "half_count");
    LogicExpression *result = mf_execute(store, mf, mem);
    assert(result != NULL);
    assert(result->data.repeat.count == 10);

    free(mf);
    mf_destroy_store(store);
    lm_destroy_memory(mem);
    TEST_PASS();
}

static void test_execute_derive_relation(void) {
    TEST_BEGIN("execute DERIVE_RELATION transitive"); tests_run++;

    MetaFormulaStore *store = mf_create_store();
    LogicalMemory *mem = lm_create_memory();

    MetaFormula *mf = mf_create_relation_deriver("A", "C", "transitive");
    LogicExpression *result = mf_execute(store, mf, mem);
    assert(result != NULL);
    assert(result->type == LOGIC_RELATION);
    assert(strcmp(result->data.relation.relation_type, "derives_from") == 0);

    free(mf);
    mf_destroy_store(store);
    lm_destroy_memory(mem);
    TEST_PASS();
}

static void test_execute_derive_equivalence(void) {
    TEST_BEGIN("execute DERIVE_RELATION equivalence"); tests_run++;

    MetaFormulaStore *store = mf_create_store();
    LogicalMemory *mem = lm_create_memory();

    MetaFormula *mf = mf_create_relation_deriver("X", "Y", "equivalence");
    LogicExpression *result = mf_execute(store, mf, mem);
    assert(result != NULL);
    assert(strcmp(result->data.relation.relation_type, "equivalent") == 0);

    free(mf);
    mf_destroy_store(store);
    lm_destroy_memory(mem);
    TEST_PASS();
}

static void test_execute_evolve_pattern(void) {
    TEST_BEGIN("execute EVOLVE_PATTERN"); tests_run++;

    MetaFormulaStore *store = mf_create_store();
    LogicalMemory *mem = lm_create_memory();

    lm_store_logic(mem, "pat", lm_logic_repeat("ABC", 10));

    MetaFormula *mf = mf_create_pattern_evolver("pat", 0.8, 5);
    LogicExpression *result = mf_execute(store, mf, mem);
    assert(result != NULL);
    assert(result->type == LOGIC_REPEAT);

    free(mf);
    mf_destroy_store(store);
    lm_destroy_memory(mem);
    TEST_PASS();
}

static void test_execute_compress_merge(void) {
    TEST_BEGIN("execute COMPRESS merge_repeats"); tests_run++;

    MetaFormulaStore *store = mf_create_store();
    LogicalMemory *mem = lm_create_memory();

    LogicExpression *r1 = lm_logic_repeat("X", 3);
    LogicExpression *r2 = lm_logic_repeat("X", 5);
    lm_store_logic(mem, "comp1", lm_logic_compose(r1, r2));

    MetaFormula *mf = mf_create_logic_compressor("comp1", "merge_repeats");
    LogicExpression *result = mf_execute(store, mf, mem);
    assert(result != NULL);
    assert(result->type == LOGIC_REPEAT);
    assert(result->data.repeat.count == 8);

    char *text = lm_materialize_logic(result);
    assert(text != NULL);
    assert(strcmp(text, "XXXXXXXX") == 0);
    free(text);

    free(mf);
    mf_destroy_store(store);
    lm_destroy_memory(mem);
    TEST_PASS();
}

/* ===== ТЕСТЫ ХРАНЕНИЯ И ЗАГРУЗКИ ===== */

static void test_store_and_load(void) {
    TEST_BEGIN("store and load by ID"); tests_run++;

    MetaFormulaStore *store = mf_create_store();

    MetaFormula *mf1 = mf_create_repeat_generator("A", "10");
    MetaFormula *mf2 = mf_create_sequence_generator("0", "1", "5");

    assert(mf_store_meta(store, mf1, "repeat_a") == 0);
    assert(mf_store_meta(store, mf2, "seq_01") == 0);
    assert(store->count == 2);

    MetaFormula *loaded = mf_load_meta(store, "seq_01");
    assert(loaded != NULL);
    assert(loaded->operation == META_GENERATE_SEQUENCE);

    assert(mf_load_meta(store, "no_such") == NULL);

    free(mf1);
    free(mf2);
    mf_destroy_store(store);
    TEST_PASS();
}

/* ===== ТЕСТЫ ОПТИМИЗАЦИИ ===== */

static void test_optimize_meta(void) {
    TEST_BEGIN("optimize fold expression"); tests_run++;

    MetaFormula *mf = mf_create_repeat_generator("Z", "3+7");
    double orig = mf->complexity_score;

    MetaFormula *opt = mf_optimize_meta(mf);
    assert(opt != NULL);
    assert(opt->complexity_score < orig);
    assert(strcmp(opt->params.gen_repeat.count_formula, "10") == 0);

    free(mf);
    free(opt);
    TEST_PASS();
}

static void test_evolve_meta(void) {
    TEST_BEGIN("evolve meta generation++"); tests_run++;

    MetaFormula *mf = mf_create_repeat_generator("Z", "10");
    MetaFormula *ev = mf_evolve_meta(mf, 1.0);
    assert(ev != NULL);
    assert(ev->generation == 1);

    free(mf);
    free(ev);
    TEST_PASS();
}

static void test_compose_same_type(void) {
    TEST_BEGIN("compose repeat+repeat → sum"); tests_run++;

    MetaFormula *a = mf_create_repeat_generator("A", "3");
    MetaFormula *b = mf_create_repeat_generator("A", "7");

    MetaFormula *c = mf_compose_meta(a, b);
    assert(c != NULL);
    assert(c->operation == META_GENERATE_REPEAT);
    assert(strcmp(c->params.gen_repeat.count_formula, "10") == 0);

    free(a); free(b); free(c);
    TEST_PASS();
}

/* ===== ТЕСТЫ СТАТИСТИКИ ===== */

static void test_get_stats(void) {
    TEST_BEGIN("get stats"); tests_run++;

    MetaFormulaStore *store = mf_create_store();
    LogicalMemory *mem = lm_create_memory();

    MetaFormula *mf = mf_create_repeat_generator("Q", "5");
    mf_store_meta(store, mf, "q5");
    mf_execute(store, mf, mem);

    MetaFormulaStats stats;
    assert(mf_get_stats(store, &stats) == 0);
    assert(stats.total_meta_formulas == 1);
    assert(stats.generated_logic_count == 1);
    assert(stats.meta_size_bytes > 0);
    assert(stats.logic_size_bytes > 0);

    free(mf);
    mf_destroy_store(store);
    lm_destroy_memory(mem);
    TEST_PASS();
}

/* ===== ТЕСТЫ СЕРИАЛИЗАЦИИ ===== */

static void test_to_string_all(void) {
    TEST_BEGIN("mf_to_string all types"); tests_run++;
    char buf[256];

    MetaFormula *m;

    m = mf_create_repeat_generator("X", "10");
    assert(mf_to_string(m, buf, sizeof(buf)) > 0);
    assert(strstr(buf, "meta_repeat") != NULL);
    free(m);

    m = mf_create_sequence_generator("0", "1", "5");
    assert(mf_to_string(m, buf, sizeof(buf)) > 0);
    assert(strstr(buf, "meta_sequence") != NULL);
    free(m);

    m = mf_create_transformer("i1", "rule");
    assert(mf_to_string(m, buf, sizeof(buf)) > 0);
    assert(strstr(buf, "meta_transform") != NULL);
    free(m);

    m = mf_create_relation_deriver("a", "b", "tr");
    assert(mf_to_string(m, buf, sizeof(buf)) > 0);
    assert(strstr(buf, "meta_derive") != NULL);
    free(m);

    m = mf_create_pattern_evolver("p1", 0.5, 10);
    assert(mf_to_string(m, buf, sizeof(buf)) > 0);
    assert(strstr(buf, "meta_evolve") != NULL);
    free(m);

    m = mf_create_logic_compressor("t1", "simplify");
    assert(mf_to_string(m, buf, sizeof(buf)) > 0);
    assert(strstr(buf, "meta_compress") != NULL);
    free(m);

    TEST_PASS();
}

/* ===== ТЕСТЫ ПРОДВИНУТЫХ ОПЕРАЦИЙ ===== */

static void test_auto_discover(void) {
    TEST_BEGIN("auto discover patterns"); tests_run++;

    MetaFormulaStore *store = mf_create_store();
    LogicalMemory *mem = lm_create_memory();

    lm_store_logic(mem, "r1", lm_logic_repeat("A", 5));
    lm_store_logic(mem, "r2", lm_logic_repeat("B", 3));
    lm_store_logic(mem, "s1", lm_logic_sequence(0, 2, 10));

    int found = mf_auto_discover_patterns(mem, store);
    assert(found > 0);
    assert(store->count > 0);

    mf_destroy_store(store);
    lm_destroy_memory(mem);
    TEST_PASS();
}

static void test_batch_execute(void) {
    TEST_BEGIN("batch execute 3 cells"); tests_run++;

    MetaFormulaStore *store = mf_create_store();
    LogicalMemory *mem = lm_create_memory();

    MetaFormula *mf = mf_create_repeat_generator("Z", "2");
    const char *ids[] = {"b0", "b1", "b2"};

    int ok = mf_batch_execute(store, mf, mem, ids, 3);
    assert(ok == 3);
    assert(mem->cell_count == 3);

    char buf[64];
    for (int i = 0; i < 3; i++) {
        int len = lm_materialize(mem, ids[i], buf, sizeof(buf));
        assert(len > 0);
        assert(strcmp(buf, "ZZ") == 0);
    }

    free(mf);
    mf_destroy_store(store);
    lm_destroy_memory(mem);
    TEST_PASS();
}

static void test_infer_combine(void) {
    TEST_BEGIN("infer combine → sum"); tests_run++;

    MetaFormulaStore *store = mf_create_store();

    MetaFormula *a = mf_create_repeat_generator("A", "3");
    MetaFormula *b = mf_create_repeat_generator("A", "7");
    const MetaFormula *in[] = {a, b};

    MetaFormula *result = mf_infer_meta(store, "combine", in, 2);
    assert(result != NULL);
    assert(strcmp(result->params.gen_repeat.count_formula, "10") == 0);

    free(result); free(a); free(b);
    mf_destroy_store(store);
    TEST_PASS();
}

static void test_infer_generalize(void) {
    TEST_BEGIN("infer generalize → average"); tests_run++;

    MetaFormulaStore *store = mf_create_store();

    MetaFormula *a = mf_create_repeat_generator("X", "4");
    MetaFormula *b = mf_create_repeat_generator("X", "6");
    MetaFormula *c = mf_create_repeat_generator("X", "8");
    const MetaFormula *in[] = {a, b, c};

    MetaFormula *gen = mf_infer_meta(store, "generalize", in, 3);
    assert(gen != NULL);
    assert(gen->operation == META_GENERATE_REPEAT);
    assert(strcmp(gen->params.gen_repeat.count_formula, "6") == 0);

    free(gen); free(a); free(b); free(c);
    mf_destroy_store(store);
    TEST_PASS();
}

/* ===== ТЕСТЫ ГРАНИЧНЫХ УСЛОВИЙ ===== */

static void test_null_safety(void) {
    TEST_BEGIN("NULL safety"); tests_run++;

    assert(mf_execute(NULL, NULL, NULL) == NULL);
    assert(mf_load_meta(NULL, NULL) == NULL);
    assert(mf_optimize_meta(NULL) == NULL);
    assert(mf_evolve_meta(NULL, 0.5) == NULL);
    assert(mf_compose_meta(NULL, NULL) == NULL);
    assert(mf_infer_meta(NULL, NULL, NULL, 0) == NULL);
    assert(mf_get_stats(NULL, NULL) == -1);
    assert(mf_to_string(NULL, NULL, 0) == -1);
    assert(mf_auto_discover_patterns(NULL, NULL) == -1);
    assert(mf_batch_execute(NULL, NULL, NULL, NULL, 0) == -1);

    TEST_PASS();
}

/* ===== MAIN ===== */

int main(void) {
    printf("\n=== Тесты модуля мета-формул (formula_logic) ===\n\n");

    test_create_destroy_store();
    test_create_destroy_meta_formula();
    test_create_repeat_generator();
    test_create_sequence_generator();
    test_create_transformer();
    test_create_relation_deriver();
    test_create_pattern_evolver();
    test_create_logic_compressor();

    test_execute_constant();
    test_execute_repeat();
    test_execute_repeat_expression();
    test_execute_sequence();
    test_execute_compose();
    test_execute_transform_double();
    test_execute_transform_half();
    test_execute_derive_relation();
    test_execute_derive_equivalence();
    test_execute_evolve_pattern();
    test_execute_compress_merge();

    test_store_and_load();
    test_optimize_meta();
    test_evolve_meta();
    test_compose_same_type();
    test_get_stats();
    test_to_string_all();

    test_auto_discover();
    test_batch_execute();
    test_infer_combine();
    test_infer_generalize();
    test_null_safety();

    printf("\n=== Результат: %d/%d тестов пройдено ===\n\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
