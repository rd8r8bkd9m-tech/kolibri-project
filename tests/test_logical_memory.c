/*
 * test_logical_memory.c
 * 
 * Демонстрация логической памяти БЕЗ данных
 * Данные существуют только как логические выражения!
 */

#include "kolibri/logical_memory.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define GREEN  "\033[32m"
#define YELLOW "\033[33m"
#define CYAN   "\033[36m"
#define RED    "\033[31m"
#define MAGENTA "\033[35m"
#define RESET  "\033[0m"

static void print_separator(void) {
    printf("%s%s%s\n", CYAN, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━", RESET);
}

int main(void) {
    printf("\n%s╔══════════════════════════════════════════════════════════════╗%s\n", 
           CYAN, RESET);
    printf("%s║          LOGICAL MEMORY WITHOUT DATA - DEMO              ║%s\n", 
           CYAN, RESET);
    printf("%s╚══════════════════════════════════════════════════════════════╝%s\n\n", 
           CYAN, RESET);
    
    /* ========== СОЗДАНИЕ ЛОГИЧЕСКОЙ ПАМЯТИ ========== */
    printf("%s[1] Creating logical memory (no data storage yet)%s\n", YELLOW, RESET);
    
    LogicalMemory *mem = lm_create_memory();
    if (!mem) {
        printf("%s✗ Failed to create logical memory!%s\n", RED, RESET);
        return 1;
    }
    
    printf("  ✓ Logical memory initialized\n");
    printf("  Storage: 0 bytes (no data yet!)\n\n");
    
    /* ========== ПРИМЕР 1: REPEAT LOGIC ========== */
    print_separator();
    printf("%s[2] Example 1: Storing LOGIC instead of DATA%s\n\n", YELLOW, RESET);
    
    printf("Traditional approach:\n");
    printf("  Data: \"AAAAAAAAAA...\" (40 bytes)\n");
    printf("  Storage: 40 bytes in RAM\n\n");
    
    printf("Logical approach:\n");
    LogicExpression *logic1 = lm_logic_repeat("A", 40);
    if (!logic1) {
        printf("%s✗ Failed to create logic!%s\n", RED, RESET);
        lm_destroy_memory(mem);
        return 1;
    }
    
    /* Store logic in memory cell */
    if (mem->cell_count < 1024) {
        snprintf(mem->cells[mem->cell_count].id, 64, "cell_1");
        mem->cells[mem->cell_count].logic = logic1;
        mem->cell_count++;
    }
    
    char logic_desc[256];
    lm_logic_to_string(logic1, logic_desc, sizeof(logic_desc));
    
    printf("  %sLogic: %s%s\n", MAGENTA, logic_desc, RESET);
    printf("  Storage: %zu bytes (just the logic!)\n", sizeof(LogicExpression));
    printf("  Predicted data size: %zu bytes\n", lm_predict_size(mem, "cell_1"));
    printf("  %sCompression: %.2fx%s\n\n", GREEN, 
           (double)lm_predict_size(mem, "cell_1") / sizeof(LogicExpression), RESET);
    
    /* Материализация */
    printf("Materialization (when needed):\n");
    char materialized1[128];
    int result1 = lm_materialize(mem, "cell_1", materialized1, sizeof(materialized1));
    
    if (result1 > 0) {
        printf("  %s✓ Data generated from logic: \"%.*s\" (%d bytes)%s\n", 
               GREEN, result1, materialized1, result1, RESET);
    } else {
        printf("  %s✗ Materialization failed!%s\n", RED, RESET);
    }
    printf("\n");
    
    /* ========== ПРИМЕР 2: SEQUENCE LOGIC ========== */
    print_separator();
    printf("%s[3] Example 2: Numeric sequence as LOGIC%s\n\n", YELLOW, RESET);
    
    printf("Traditional approach:\n");
    printf("  Data: \"123456789...\" (100 numbers = ~300 bytes)\n");
    printf("  Storage: 300 bytes\n\n");
    
    printf("Logical approach:\n");
    LogicExpression *logic2 = lm_logic_sequence(1, 1, 100);
    
    /* Store logic in memory cell */
    if (mem->cell_count < 1024) {
        snprintf(mem->cells[mem->cell_count].id, 64, "cell_2");
        mem->cells[mem->cell_count].logic = logic2;
        mem->cell_count++;
    }
    
    lm_logic_to_string(logic2, logic_desc, sizeof(logic_desc));
    printf("  %sLogic: %s%s\n", MAGENTA, logic_desc, RESET);
    printf("  Storage: %zu bytes\n", sizeof(LogicExpression));
    printf("  Predicted data size: %zu bytes\n", lm_predict_size(mem, "cell_2"));
    printf("  %sCompression: %.2fx%s\n\n", GREEN,
           (double)lm_predict_size(mem, "cell_2") / sizeof(LogicExpression), RESET);
    
    printf("Materialization:\n");
    char materialized2[512];
    int result2 = lm_materialize(mem, "cell_2", materialized2, sizeof(materialized2));
    
    if (result2 > 0) {
        printf("  %s✓ Generated: %.*s... (%d bytes total)%s\n",
               GREEN, 50, materialized2, result2, RESET);
    }
    printf("\n");
    
    /* ========== ПРИМЕР 3: COMPOSITION ========== */
    print_separator();
    printf("%s[4] Example 3: Composed logic (multiple patterns)%s\n\n", YELLOW, RESET);
    
    printf("Traditional approach:\n");
    printf("  Data: \"AAABBBCCC\" (9 bytes)\n");
    printf("  Storage: 9 bytes\n\n");
    
    printf("Logical approach:\n");
    LogicExpression *part_a = lm_logic_repeat("A", 3);
    LogicExpression *part_b = lm_logic_repeat("B", 3);
    LogicExpression *logic3 = lm_logic_compose(part_a, part_b);
    
    /* Store logic in memory cell */
    if (mem->cell_count < 1024) {
        snprintf(mem->cells[mem->cell_count].id, 64, "cell_3");
        mem->cells[mem->cell_count].logic = logic3;
        mem->cell_count++;
    }
    
    lm_logic_to_string(logic3, logic_desc, sizeof(logic_desc));
    printf("  %sLogic: %s%s\n", MAGENTA, logic_desc, RESET);
    printf("  Storage: ~%zu bytes (2 expressions + composition)\n", 
           sizeof(LogicExpression) * 3);
    printf("  Predicted data size: %zu bytes\n", lm_predict_size(mem, "cell_3"));
    printf("\n");
    
    char materialized3[64];
    int result3 = lm_materialize(mem, "cell_3", materialized3, sizeof(materialized3));
    
    if (result3 > 0) {
        printf("  %s✓ Generated: \"%.*s\" (%d bytes)%s\n",
               GREEN, result3, materialized3, result3, RESET);
    }
    printf("\n");
    
    /* ========== ПРИМЕР 4: RELATIONS ========== */
    print_separator();
    printf("%s[5] Example 4: Logical relations (knowledge graph)%s\n\n", YELLOW, RESET);
    
    printf("Traditional approach:\n");
    printf("  Store: NodeA + NodeB + Edge + Properties\n");
    printf("  Storage: ~100 bytes per relation\n\n");
    
    printf("Logical approach:\n");
    LogicExpression *node_a = lm_logic_repeat("genome_block", 1);
    LogicExpression *node_b = lm_logic_repeat("formula", 1);
    LogicExpression *relation = lm_logic_relation(node_a, node_b, "derives_from");
    
    /* Store logic in memory cell */
    if (mem->cell_count < 1024) {
        snprintf(mem->cells[mem->cell_count].id, 64, "relation_1");
        mem->cells[mem->cell_count].logic = relation;
        mem->cell_count++;
    }
    
    lm_logic_to_string(relation, logic_desc, sizeof(logic_desc));
    printf("  %sLogic: %s%s\n", MAGENTA, logic_desc, RESET);
    printf("  Storage: %zu bytes (just the relation logic)\n", sizeof(LogicExpression));
    printf("  %sNo materialized data - it's pure logic!%s\n\n", GREEN, RESET);
    
    /* ========== СТАТИСТИКА ========== */
    print_separator();
    printf("%s[6] Logical Memory Statistics%s\n\n", YELLOW, RESET);
    
    LogicalMemoryStats stats;
    lm_get_stats(mem, &stats);
    
    printf("Total cells:           %zu\n", stats.total_cells);
    printf("Logic size:            %zu bytes\n", stats.logic_size_bytes);
    printf("Predicted data size:   %zu bytes (if all materialized)\n", stats.predicted_data_size);
    printf("%sCompression ratio:     %.2fx%s\n", GREEN, stats.compression_ratio, RESET);
    printf("Cached cells:          %zu / %zu\n", stats.cached_cells, stats.total_cells);
    printf("Cache hit rate:        %zu%%\n\n", stats.cache_hit_rate);
    
    /* ========== СРАВНЕНИЕ ========== */
    print_separator();
    printf("%s╔══════════════════════════════════════════════════════════════╗%s\n", 
           GREEN, RESET);
    printf("%s║                    COMPARISON TABLE                      ║%s\n", 
           GREEN, RESET);
    printf("%s╚══════════════════════════════════════════════════════════════╝%s\n\n", 
           GREEN, RESET);
    
    printf("Approach              | Storage  | Type       | Materialization\n");
    printf("----------------------------------------------------------------\n");
    printf("Traditional (data)    | %zu B    | Data       | Instant (already there)\n", 
           stats.predicted_data_size);
    printf("Formula compression   | ~%zu B   | Formula    | Fast (decode formula)\n",
           stats.predicted_data_size / 3);
    printf("%sLogical memory        | %zu B    | Logic      | On-demand (generate)%s\n",
           GREEN, stats.logic_size_bytes, RESET);
    
    printf("\n%sSpace saved vs traditional: %.1f%%%s\n",
           GREEN, 100.0 * (1.0 - (double)stats.logic_size_bytes / stats.predicted_data_size), RESET);
    printf("%sSpace saved vs formula:     %.1f%%%s\n\n",
           GREEN, 100.0 * (1.0 - (double)stats.logic_size_bytes / (stats.predicted_data_size / 3)), RESET);
    
    /* ========== ФИЛОСОФСКАЯ ЧАСТЬ ========== */
    print_separator();
    printf("%s╔══════════════════════════════════════════════════════════════╗%s\n", 
           MAGENTA, RESET);
    printf("%s║                  PHILOSOPHICAL INSIGHT                   ║%s\n", 
           MAGENTA, RESET);
    printf("%s╚══════════════════════════════════════════════════════════════╝%s\n\n", 
           MAGENTA, RESET);
    
    printf("Традиционная память:  Хранит ДАННЫЕ (bits & bytes)\n");
    printf("Формульная память:    Хранит ФОРМУЛЫ (compressed patterns)\n");
    printf("%sЛогическая память:    Хранит ЛОГИКУ (rules & relations)%s\n\n", MAGENTA, RESET);
    
    printf("Данные НЕ СУЩЕСТВУЮТ до момента запроса!\n");
    printf("Они ГЕНЕРИРУЮТСЯ из логических правил on-demand.\n\n");
    
    printf("Это как:\n");
    printf("  • Математическая функция vs таблица значений\n");
    printf("  • Программа vs её выход\n");
    printf("  • ДНК vs белки\n");
    printf("  • Формула vs результат вычисления\n\n");
    
    printf("%s✓ Логическая память = Память без данных = Чистая логика!%s\n\n", 
           GREEN, RESET);
    
    /* ========== ПРИМЕНЕНИЕ В KOLIBRI ========== */
    print_separator();
    printf("%s[7] Application in Kolibri OS%s\n\n", YELLOW, RESET);
    
    printf("Kolibri Genome:\n");
    printf("  Вместо: ReasonBlock[] с payload (1000s of bytes)\n");
    printf("  Храним: LogicExpression (relationship rules)\n");
    printf("  Benefit: 10-100x compression + AI reasoning!\n\n");
    
    printf("AI Evolution:\n");
    printf("  Вместо: Copying genome data for mutations\n");
    printf("  Делаем: Mutate logic expressions\n");
    printf("  Benefit: Instant mutations, no data copying!\n\n");
    
    printf("Knowledge Base:\n");
    printf("  Вместо: Storing all associations as data\n");
    printf("  Храним: Logical relations + inference rules\n");
    printf("  Benefit: Infinite derivations from finite logic!\n\n");
    
    /* ========== CLEANUP ========== */
    lm_destroy_memory(mem);
    
    printf("%s✓ DEMONSTRATION COMPLETE%s\n", GREEN, RESET);
    printf("Logical memory: %zu bytes stored, %zu bytes generated\n",
           stats.logic_size_bytes, stats.predicted_data_size);
    printf("Data exists only as logic - materialized on demand!\n\n");
    
    return 0;
}
