#include "kolibri/logical_memory.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main() {
    printf("=== Test Pattern Discovery in Logical Memory ===\n");

    /* 1. Тест повторов */
    LogicExpression *const_expr = lm_logic_constant("ABCABCABCABC");
    printf("Original: ");
    char buf[256];
    lm_logic_to_string(const_expr, buf, sizeof(buf));
    printf("%s\n", buf);

    LogicExpression *opt_expr = lm_optimize_logic(const_expr);
    printf("Optimized: ");
    lm_logic_to_string(opt_expr, buf, sizeof(buf));
    printf("%s\n", buf);

    assert(opt_expr->type == LOGIC_REPEAT);
    assert(opt_expr->data.repeat.count == 4);
    
    char *materialized = lm_materialize_logic(opt_expr);
    printf("Materialized: %s\n", materialized);
    assert(strcmp(materialized, "ABCABCABCABC") == 0);
    free(materialized);

    /* 2. Тест последовательности */
    LogicExpression *seq_const = lm_logic_constant("10 20 30 40 50");
    printf("\nOriginal: ");
    lm_logic_to_string(seq_const, buf, sizeof(buf));
    printf("%s\n", buf);

    LogicExpression *opt_seq = lm_optimize_logic(seq_const);
    printf("Optimized: ");
    lm_logic_to_string(opt_seq, buf, sizeof(buf));
    printf("%s\n", buf);

    assert(opt_seq->type == LOGIC_SEQUENCE);
    assert(opt_seq->data.sequence.start == 10);
    assert(opt_seq->data.sequence.step == 10);
    assert(opt_seq->data.sequence.count == 5);

    /* 3. Тест композиции */
    LogicExpression *comp_const = lm_logic_constant("ABCABCABC123123123");
    printf("\nOriginal: ");
    lm_logic_to_string(comp_const, buf, sizeof(buf));
    printf("%s\n", buf);

    LogicExpression *opt_comp = lm_optimize_logic(comp_const);
    printf("Optimized: ");
    lm_logic_to_string(opt_comp, buf, sizeof(buf));
    printf("%s\n", buf);

    assert(opt_comp->type == LOGIC_COMPOSITION);
    
    char *mat_comp = lm_materialize_logic(opt_comp);
    printf("Materialized: %s\n", mat_comp);
    assert(strcmp(mat_comp, "ABCABCABC123123123") == 0);
    free(mat_comp);

    lm_destroy_logic(opt_expr);
    lm_destroy_logic(opt_seq);
    lm_destroy_logic(opt_comp);

    printf("\n=== SUCCESS: Pattern discovery works! ===\n");
    return 0;
}
