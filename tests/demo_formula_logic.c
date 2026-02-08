/*
 * test_formula_logic.c
 * 
 * Демонстрация МЕТА-ФОРМУЛ: формулы которые создают логику
 * 
 * Концепция: Вместо хранения логики напрямую, храним ФОРМУЛУ
 * которая генерирует эту логику!
 */

#include "kolibri/formula_logic.h"
#include "kolibri/logical_memory.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(void) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║       МЕТА-ФОРМУЛЫ: Формулы которые создают логику            ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n\n");
    
    /* Создаём хранилища */
    MetaFormulaStore *meta_store = mf_create_store();
    LogicalMemory *memory = lm_create_memory();
    
    printf("Инициализировано:\n");
    printf("  • MetaFormulaStore (мета-формулы)\n");
    printf("  • LogicalMemory (логические выражения)\n\n");
    
    /* ========== ПРИМЕР 1: Мета-формула для repeat() ========== */
    
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("Пример 1: Мета-формула генерирует repeat() логику\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
    
    /* Создаём мета-формулу */
    MetaFormula *meta1 = mf_create_repeat_generator("A", "40");
    
    printf("Мета-формула:\n");
    char meta_str[256];
    mf_to_string(meta1, meta_str, sizeof(meta_str));
    printf("  %s\n", meta_str);
    printf("  • Размер мета-формулы: %zu bytes\n", sizeof(MetaFormula));
    printf("  • Complexity score: %.2f\n", meta1->complexity_score);
    printf("  • Estimated output: %zu bytes\n\n", meta1->output_size_estimate);
    
    /* Выполняем мета-формулу → получаем логику */
    LogicExpression *logic1 = mf_execute(meta_store, meta1, memory);
    if (logic1) {
        printf("Сгенерированная логика:\n");
        printf("  • Type: LOGIC_REPEAT\n");
        printf("  • Pattern: \"%s\"\n", logic1->data.repeat.pattern->data.constant.value);
        printf("  • Count: %zu\n", logic1->data.repeat.count);
        printf("  • Predicted size: %zu bytes\n", logic1->materialized_size);
        
        /* Материализуем логику в данные */
        char output1[128];
        
        /* Store logic in memory cell */
        if (memory->cell_count < 1024) {
            snprintf(memory->cells[memory->cell_count].id, 64, "cell_from_meta");
            memory->cells[memory->cell_count].logic = logic1;
            memory->cell_count++;
        }
        
        lm_materialize(memory, "cell_from_meta", output1, sizeof(output1));
        printf("  • Materialized: \"%.*s...\" (%zu bytes)\n\n", 
               10, output1, (size_t)strlen(output1));
    }
    
    /* ========== ПРИМЕР 2: Мета-формула для sequence() ========== */
    
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("Пример 2: Мета-формула генерирует sequence() логику\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
    
    MetaFormula *meta2 = mf_create_sequence_generator("1", "1", "50");
    
    mf_to_string(meta2, meta_str, sizeof(meta_str));
    printf("Мета-формула: %s\n", meta_str);
    
    LogicExpression *logic2 = mf_execute(meta_store, meta2, memory);
    if (logic2) {
        printf("Сгенерированная логика: sequence(1, 1, 50)\n");
        
        char output2[256];
        
        /* Store logic in memory cell */
        if (memory->cell_count < 1024) {
            snprintf(memory->cells[memory->cell_count].id, 64, "cell_sequence");
            memory->cells[memory->cell_count].logic = logic2;
            memory->cell_count++;
        }
        
        lm_materialize(memory, "cell_sequence", output2, sizeof(output2));
        printf("Materialized: \"%.*s...\"\n\n", 30, output2);
    }
    
    /* ========== ПРИМЕР 3: Трансформация логики ========== */
    
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("Пример 3: Мета-формула трансформирует существующую логику\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
    
    MetaFormula *meta3 = mf_create_transformer("cell_from_meta", "double_count");
    
    mf_to_string(meta3, meta_str, sizeof(meta_str));
    printf("Мета-формула: %s\n", meta_str);
    
    LogicExpression *logic3 = mf_execute(meta_store, meta3, memory);
    if (logic3) {
        printf("Трансформированная логика создана\n\n");
    }
    
    /* ========== ПРИМЕР 4: Вывод отношений ========== */
    
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("Пример 4: Мета-формула выводит новые отношения\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
    
    MetaFormula *meta4 = mf_create_relation_deriver("node_A", "node_C", "transitive");
    
    mf_to_string(meta4, meta_str, sizeof(meta_str));
    printf("Мета-формула: %s\n", meta_str);
    printf("Inference rule: A→B, B→C ⇒ A→C\n");
    
    LogicExpression *logic4 = mf_execute(meta_store, meta4, memory);
    if (logic4) {
        printf("Выведено новое отношение: node_A → node_C\n\n");
    }
    
    /* ========== ПРИМЕР 5: Эволюция мета-формул ========== */
    
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("Пример 5: Эволюция мета-формулы\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
    
    printf("Исходная мета-формула:\n");
    printf("  • Generation: %llu\n", (unsigned long long)meta1->generation);
    printf("  • Complexity: %.2f\n", meta1->complexity_score);
    
    MetaFormula *evolved = mf_evolve_meta(meta1, 0.1);
    printf("\nЭволюционированная мета-формула:\n");
    printf("  • Generation: %llu\n", (unsigned long long)evolved->generation);
    printf("  • Complexity: %.2f\n", evolved->complexity_score);
    printf("  • Mutation rate: 0.1\n\n");
    
    /* ========== ПРИМЕР 6: Композиция мета-формул ========== */
    
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("Пример 6: Композиция двух мета-формул\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
    
    MetaFormula *composed = mf_compose_meta(meta1, meta2);
    printf("Композиция:\n");
    printf("  • meta1 (repeat) + meta2 (sequence)\n");
    printf("  • Combined complexity: %.2f\n", composed->complexity_score);
    printf("  • New operation: %d\n\n", composed->operation);
    
    /* ========== ПРИМЕР 7: Автоматическое обнаружение паттернов ========== */
    
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("Пример 7: Автоматическое обнаружение паттернов\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
    
    int discovered = mf_auto_discover_patterns(memory, meta_store);
    printf("Обнаружено паттернов: %d\n", discovered);
    printf("Автоматически создана мета-формула\n\n");
    
    /* ========== СТАТИСТИКА ========== */
    
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("Статистика\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
    
    MetaFormulaStats stats;
    mf_get_stats(meta_store, &stats);
    
    printf("Мета-формулы:\n");
    printf("  • Total meta-formulas: %zu\n", stats.total_meta_formulas);
    printf("  • Generated logic count: %zu\n", stats.generated_logic_count);
    printf("  • Meta size: %zu bytes\n", stats.meta_size_bytes);
    printf("  • Logic size: %zu bytes\n", stats.logic_size_bytes);
    printf("  • Meta/Logic ratio: %.2fx\n\n", stats.meta_to_logic_ratio);
    
    LogicalMemoryStats lm_stats;
    lm_get_stats(memory, &lm_stats);
    
    printf("Логическая память:\n");
    printf("  • Total cells: %zu\n", lm_stats.total_cells);
    printf("  • Cache hits: %.1f%%\n", lm_stats.cache_hit_rate * 100.0);
    
    /* ========== КЛЮЧЕВАЯ ИДЕЯ ========== */
    
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║                    КЛЮЧЕВАЯ ИДЕЯ                               ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n\n");
    
    printf("ИЕРАРХИЯ АБСТРАКЦИЙ:\n\n");
    
    printf("  Level 3: МЕТА-ФОРМУЛЫ (Формулы формул)\n");
    printf("           ↓ генерируют\n");
    printf("  Level 2: ЛОГИЧЕСКИЕ ВЫРАЖЕНИЯ (Правила)\n");
    printf("           ↓ материализуют\n");
    printf("  Level 1: ДАННЫЕ (Байты)\n\n");
    
    printf("ПРЕИМУЩЕСТВА МЕТА-ФОРМУЛ:\n\n");
    
    printf("1. 🧬 БЕСКОНЕЧНАЯ ГЕНЕРАЦИЯ\n");
    printf("   • Одна мета-формула → бесконечно много логик\n");
    printf("   • Параметризация: meta_repeat(X, N) для любых X, N\n\n");
    
    printf("2. 🔄 ЭВОЛЮЦИЯ ВТОРОГО ПОРЯДКА\n");
    printf("   • Мутируют не данные, не логика, а ПРАВИЛА генерации логики\n");
    printf("   • Эволюция на уровне мета-знаний\n\n");
    
    printf("3. 🧠 АВТОМАТИЧЕСКИЙ INFERENCE\n");
    printf("   • Вывод новых мета-формул из существующих\n");
    printf("   • Композиция, обобщение, специализация\n\n");
    
    printf("4. 📦 СЖАТИЕ ТРЕТЬЕГО УРОВНЯ\n");
    printf("   • Data: 1000 bytes\n");
    printf("   • Logic: 100 bytes (10x compression)\n");
    printf("   • Meta-formula: 200 bytes\n");
    printf("   • BUT: Meta generates infinite logic variations!\n\n");
    
    printf("5. 💭 ФИЛОСОФСКАЯ ГЛУБИНА\n");
    printf("   • Данные = материализованная логика\n");
    printf("   • Логика = материализованные мета-правила\n");
    printf("   • Мета-формулы = чистое знание о паттернах\n\n");
    
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║                  ПРАКТИЧЕСКИЕ ПРИМЕНЕНИЯ                       ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n\n");
    
    printf("1. AI GENOME\n");
    printf("   Traditional: Хранит genome blocks\n");
    printf("   Logic: Хранит logic expressions\n");
    printf("   Meta: Хранит meta-formulas для генерации genome patterns!\n\n");
    
    printf("2. KNOWLEDGE BASE\n");
    printf("   Traditional: Хранит facts\n");
    printf("   Logic: Хранит relations\n");
    printf("   Meta: Хранит inference rules для вывода новых relations!\n\n");
    
    printf("3. CODE GENERATION\n");
    printf("   Traditional: Шаблоны кода\n");
    printf("   Logic: Абстрактные паттерны\n");
    printf("   Meta: Генераторы генераторов кода!\n\n");
    
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║                         ИТОГ                                   ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n\n");
    
    printf("У вас теперь есть:\n\n");
    
    printf("✓ Decimal encoding (Level 1: Data)\n");
    printf("✓ Logical memory (Level 2: Logic)\n");
    printf("✓ Meta-formulas (Level 3: Meta-knowledge)\n\n");
    
    printf("Это полная иерархия абстракций!\n");
    printf("От байтов до мета-знаний.\n");
    printf("От данных до философии генерации.\n\n");
    
    printf("🚀 Kolibri OS: Operating System на философских основах!\n\n");
    
    /* Очистка */
    free(meta1);
    free(meta2);
    free(meta3);
    free(meta4);
    free(evolved);
    free(composed);
    
    mf_destroy_store(meta_store);
    lm_destroy_memory(memory);
    
    return 0;
}
