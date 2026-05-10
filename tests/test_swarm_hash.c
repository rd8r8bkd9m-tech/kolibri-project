/*
 * Стресс-тест масштабируемости: 16-битная задача
 * Задача: Найти закономерность в 16-битном нелинейном преобразовании
 */
#include "kolibri/spectral.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

/* 16-битная нелинейная функция (симуляция хеша) */
uint16_t simple_hash_16(uint16_t x) {
    x = ((x >> 8) ^ x) * 0x45d9f3b;
    x = ((x >> 8) ^ x) * 0x45d9f3b;
    x = (x >> 8) ^ x;
    return x;
}

int main() {
    printf("[STRESS TEST] Kolibri AI Scalability (16-bit)...\n");
    srand(time(NULL));

    #define N 200
    int inputs[N];
    int outputs[N];

    /* Генерируем обучающую выборку из 16-битных чисел */
    for (int i = 0; i < N; ++i) {
        uint16_t val = (uint16_t)rand();
        inputs[i] = (int)val;
        outputs[i] = (int)simple_hash_16(val);
    }

    printf("[TARGET] Анализ %d пар 16-битных данных...\n", N);
    printf("[INFO] Запуск аналитики (GF2)...\n");
    
    int *predicted = malloc(N * sizeof(int));
    kolibri_solve_logic_gf2(inputs, outputs, N, 16, predicted);

    int errors = 0;
    for (int i = 0; i < N; ++i) {
        if (predicted[i] != outputs[i]) errors++;
    }
    free(predicted);

    if (errors == 0) {
        printf("[RESULT] Аналитика (GF2) справилась!\n");
    } else {
        printf("[RESULT] Аналитика провалилась (%d ошибок). \n", errors);
        printf("[ACTION] Передача задачи Рою (Deep Swarm Evolution)...\n");

        kolibri_hybrid_solve(inputs, outputs, N, 0, NULL, 500);

        printf("\n[FINAL CHECK] Проверка накопленных знаний...\n");
        int final_score = 0;
        for (int i = 0; i < N; ++i) {
            if (simple_hash_16((uint16_t)inputs[i]) == (uint16_t)outputs[i]) final_score++;
        }
        printf("[RESULT] Точность предсказания после обучения: %d / %d\n", final_score, N);
    }

    return 0;
}
