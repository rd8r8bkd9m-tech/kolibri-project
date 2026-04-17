#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "kolibri/answer_composer.h"
#include "kolibri/formula.h"

int main() {
    printf("--- Запуск теста AnswerComposer ---\n");

    KolibriFormulaPool pool;
    kf_pool_init(&pool, 12345);

    // Добавляем тестовые ассоциации
    KolibriSymbolTable symbols;
    kf_pool_add_association(&pool, &symbols, "Что такое Kolibri OS?", "Kolibri OS — уникальная система ИИ с числовым мышлением.", "test", 0);
    kf_pool_add_association(&pool, &symbols, "Как Kolibri сжимает данные?", "Предиктивное сжатие использует популяцию формул для предсказания байтов.", "test", 0);

    // Инициализируем композитор
    KolibriAnswerComposer composer;
    kac_init(&composer);

    // Добавляем ассоциации в композитор
    kac_add_fragment(&composer, &pool.associations[0], 0.9);
    kac_add_fragment(&composer, &pool.associations[1], 0.9);

    // Композуем
    int res = kac_compose(&composer, "Kolibri OS сжатие");
    assert(res == 0);

    const char* answer = kac_get_answer(&composer);
    printf("Скомпонованный ответ: %s\n", answer);

    assert(strstr(answer, "числовым мышлением") != NULL);
    assert(strstr(answer, "Предиктивное сжатие") != NULL);

    printf("--- Тест успешно пройден! ---\n");

    kf_pool_free(&pool);
    return 0;
}
