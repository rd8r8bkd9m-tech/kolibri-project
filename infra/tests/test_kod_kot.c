#include <stdio.h>
#include <string.h>
#include "kolibri/semantic.h"
#include "kolibri/phoneme.h"

int main() {
    KolibriSemanticPattern p_kod, p_kot, p_kit;
    KolibriSemanticContext ctx = {0}; // Пустой контекст для чистого эксперимента
    
    printf("Сравнение (Код vs Кот vs Кит) с учетом фонем Phase 2\n");
    
    k_semantic_learn("Код", &ctx, 10, &p_kod);
    k_semantic_learn("Кот", &ctx, 10, &p_kot);
    k_semantic_learn("Кит", &ctx, 10, &p_kit);
    
    double sim_kod_kot = k_semantic_similarity(&p_kod, &p_kot);
    double sim_kot_kit = k_semantic_similarity(&p_kot, &p_kit);
    
    printf("Сходство 'Код' и 'Кот': %.3f\n", sim_kod_kot);
    printf("Сходство 'Кот' и 'Кит': %.3f\n", sim_kot_kit);
    
    if (sim_kod_kot > 0.5) {
        printf("УСПЕХ: 'Код' и 'Кот' похожи фонетически (Phase 2 работает)\n");
    } else {
        printf("ОШИБКА: Низкое сходство 'Код' и 'Кот'\n");
    }

    return 0;
}
