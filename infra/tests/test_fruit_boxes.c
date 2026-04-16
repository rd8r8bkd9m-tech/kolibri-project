/*
 * test_fruit_boxes.c
 *
 * Тест: Kolibri решает задачку про три коробки
 */

#include "kolibri/reasoning_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    const char *problem =
        "Три коробки стоят на столе. "
        "На одной написано 'Только яблоки', на второй - 'Только груши', "
        "на третьей - 'Яблоки и груши'. "
        "Все надписи неправильные. "
        "Ты можешь достать только один фрукт только из одной коробки. "
        "Как после этого точно определить, что лежит в каждой коробке?";

    printf("===========================================\n");
    printf("Kolibri: Задача про три коробки\n");
    printf("===========================================\n\n");
    printf("Задача:\n%s\n\n", problem);

    KolibriREConfig config = {0};
    kolibri_re_init(&config);

    KolibriReasoningResult result;
    int ret = kolibri_re_solve_logic_puzzle(problem, &config, &result);

    if (ret != 0) {
        printf("ERROR: reasoning failed (ret=%d)\n", ret);
        return 1;
    }

    kolibri_re_print_result(&result);

    printf("===========================================\n");
    printf("Ответ Kolibri:\n");
    printf("%s\n", result.answer);
    printf("===========================================\n\n");

    /* Печатаем шаги рассуждения */
    printf("Ход рассуждений:\n");
    for (int i = 0; i < result.chain.num_steps; i++) {
        printf("  Шаг %d: %s\n",
               result.chain.steps[i].step_num,
               result.chain.steps[i].description);
    }

    printf("\nУверенность: %.0f%%\n", result.confidence * 100);

    return 0;
}
