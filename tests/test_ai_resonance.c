/*
 * test_ai_resonance.c
 * 
 * Тесты для резонансного ядра рассуждений.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Прямое включение для тестирования */
#include "../kernel/ai_resonance.c"

void test_create_destroy(void) {
    printf("test_create_destroy... ");
    
    ResonanceCore *core = resonance_create();
    assert(core != NULL);
    assert(core->generative != NULL);
    assert(core->residual != NULL);
    assert(core->generative->count == 0);
    
    resonance_destroy(core);
    printf("OK\n");
}

void test_add_formula(void) {
    printf("test_add_formula... ");
    
    ResonanceCore *core = resonance_create();
    
    uint8_t digits1[] = {1, 2, 3, 4, 5};
    uint8_t digits2[] = {9, 8, 7, 6};
    
    int ret = resonance_add_formula(core, digits1, 5, 50.0);
    assert(ret == 0);
    assert(core->generative->count == 1);
    
    ret = resonance_add_formula(core, digits2, 4, 75.0);
    assert(ret == 0);
    assert(core->generative->count == 2);
    
    /* Проверяем данные */
    assert(core->generative->formulas[0].length == 5);
    assert(core->generative->formulas[0].fitness == 50.0);
    assert(core->generative->formulas[1].length == 4);
    assert(core->generative->formulas[1].fitness == 75.0);
    
    resonance_destroy(core);
    printf("OK\n");
}

void test_evolve(void) {
    printf("test_evolve... ");
    
    ResonanceCore *core = resonance_create();
    
    /* Добавляем несколько формул */
    uint8_t d1[] = {1, 2, 3, 4, 5};
    uint8_t d2[] = {5, 4, 3, 2, 1};
    uint8_t d3[] = {0, 0, 0, 0, 0};
    
    resonance_add_formula(core, d1, 5, 50.0);
    resonance_add_formula(core, d2, 5, 60.0);
    resonance_add_formula(core, d3, 5, 40.0);
    
    size_t count_before = core->generative->count;
    uint32_t gen_before = core->current_generation;
    
    /* Эволюция */
    int ret = resonance_evolve(core, 10);
    assert(ret == 0);
    
    /* Поколение должно увеличиться */
    assert(core->current_generation == gen_before + 1);
    
    /* Формул может стать больше (из-за кроссовера) */
    assert(core->generative->count >= count_before);
    
    resonance_destroy(core);
    printf("OK\n");
}

void test_fitness(void) {
    printf("test_fitness... ");
    
    ResonanceCore *core = resonance_create();
    
    uint8_t target[] = {1, 2, 3, 4, 5};
    uint8_t match[] = {1, 2, 3, 4, 5};   /* 100% совпадение */
    uint8_t partial[] = {1, 2, 0, 0, 0}; /* 40% совпадение */
    
    resonance_add_formula(core, match, 5, 0.0);
    resonance_add_formula(core, partial, 5, 0.0);
    
    resonance_update_fitness(core, target, 5);
    
    /* Первая формула должна иметь высокий fitness */
    assert(core->generative->formulas[0].fitness == 100.0);
    
    /* Вторая — частичный */
    assert(core->generative->formulas[1].fitness == 40.0);
    
    /* Лучшая — первая */
    const GenerativeFormula *best = resonance_get_best(core);
    assert(best != NULL);
    assert(best->fitness == 100.0);
    
    resonance_destroy(core);
    printf("OK\n");
}

void test_delta(void) {
    printf("test_delta... ");
    
    ResonanceCore *core = resonance_create();
    
    uint8_t expected[] = {0xAA, 0xBB, 0xCC, 0xDD};
    uint8_t actual[] = {0xAA, 0x00, 0xCC, 0xFF};
    
    int ret = resonance_compute_delta(core, 1, expected, 4, actual, 4);
    assert(ret == 0);
    assert(core->residual->count == 1);
    
    /* Проверяем дельту: XOR */
    ResidualDelta *d = &core->residual->deltas[0];
    assert(d->delta[0] == (0xAA ^ 0xAA));  /* 0x00 */
    assert(d->delta[1] == (0xBB ^ 0x00));  /* 0xBB */
    assert(d->delta[2] == (0xCC ^ 0xCC));  /* 0x00 */
    assert(d->delta[3] == (0xDD ^ 0xFF));  /* 0x22 */
    
    /* Применяем дельту для восстановления */
    uint8_t restored[4];
    int len = resonance_apply_delta(d, actual, 4, restored, 4);
    assert(len == 4);
    
    /* Должны получить expected */
    assert(memcmp(restored, expected, 4) == 0);
    
    resonance_destroy(core);
    printf("OK\n");
}

void test_stats(void) {
    printf("test_stats... ");
    
    ResonanceCore *core = resonance_create();
    
    uint8_t d[] = {1, 2, 3};
    resonance_add_formula(core, d, 3, 50.0);
    
    /* Просто проверяем, что не падает */
    resonance_print_stats(core);
    
    resonance_destroy(core);
    printf("OK\n");
}

int main(void) {
    printf("\n=== Тесты Resonance Core ===\n\n");
    
    test_create_destroy();
    test_add_formula();
    test_evolve();
    test_fitness();
    test_delta();
    test_stats();
    
    printf("\n*** Все тесты пройдены! ***\n\n");
    return 0;
}
