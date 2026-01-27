#include "kolibri/semantic.h"
#include <stdio.h>
#include <string.h>

int main() {
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║         PHASE 2: PHONETIC & SEMANTIC FUSION              ║\n");
    printf("║   Тестирование влияния фонем на семантическую близость   ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    KolibriSemanticContext ctx;
    k_semantic_context_init(&ctx);
    k_semantic_context_add_word(&ctx, "существо", 1.0);
    
    KolibriSemanticPattern p_kot, p_kit, p_koshka;
    
    printf("Обучение векторов...\n");
    k_semantic_learn("кот", &ctx, 200, &p_kot);
    k_semantic_learn("кит", &ctx, 200, &p_kit);
    k_semantic_learn("кошка", &ctx, 200, &p_koshka);
    
    printf("\nРезультаты анализа:\n");
    
    double sim_kot_kit = k_semantic_similarity(&p_kot, &p_kit);
    double sim_kot_koshka = k_semantic_similarity(&p_kot, &p_koshka);
    
    printf("Сходство Кот <-> Кит (похожее звучание): %.3f\n", sim_kot_kit);
    printf("Сходство Кот <-> Кошка (похожий смысл): %.3f\n", sim_kot_koshka);
    
    printf("\nФонетические сигнатуры:\n");
    printf("Кот: ");
    for(size_t i=0; i<p_kot.phonetics.count; i++) printf("%d ", p_kot.phonetics.phonemes[i]);
    printf("\nКит: ");
    for(size_t i=0; i<p_kit.phonetics.count; i++) printf("%d ", p_kit.phonetics.phonemes[i]);
    printf("\n");
    
    k_semantic_context_free(&ctx);
    
    return 0;
}
