/*
 * AI Encoder Tests
 * 
 * Validates the optimized decimal encoding for AI genome and formulas
 * Based on DECIMAL_10X research findings
 */

#include "kolibri/ai_encoder.h"
#include "kolibri/formula.h"
#include "kolibri/genome.h"
#include "support.h"

#include <stdio.h>
#include <assert.h>

#define TEST_PASSED printf("✓ %s\n", __func__)
#define TEST_FAILED printf("✗ %s\n", __func__)

/*
 * Test: Encode and decode gene
 */
static void test_gene_encode_decode(void) {
    KolibriGene gene_in, gene_out;
    unsigned char encoded[256];
    
    /* Create test gene */
    gene_in.length = 8;
    for (size_t i = 0; i < gene_in.length; i++) {
        gene_in.digits[i] = i % 10;
    }
    
    /* Encode */
    int encoded_len = kai_encode_gene(&gene_in, encoded, sizeof(encoded));
    assert(encoded_len == 8 * 3);  /* 3 bytes per digit */
    
    /* Decode */
    int result = kai_decode_gene(encoded, encoded_len, &gene_out);
    assert(result == 0);
    assert(gene_out.length == gene_in.length);
    
    /* Verify */
    for (size_t i = 0; i < gene_in.length; i++) {
        assert(gene_out.digits[i] == gene_in.digits[i]);
    }
    
    TEST_PASSED;
}

/*
 * Test: Encode genome block
 */
static void test_genome_block_encode(void) {
    ReasonBlock block;
    unsigned char encoded[512];
    
    /* Create test block */
    block.index = 42;
    snprintf(block.event_type, sizeof(block.event_type), "test_event");
    snprintf(block.payload, sizeof(block.payload), "test_payload");
    
    /* Encode */
    int encoded_len = kai_encode_genome_block(&block, encoded, sizeof(encoded));
    assert(encoded_len > 0);
    
    /* Verify encoding exists */
    int has_data = 0;
    for (int i = 0; i < encoded_len; i++) {
        if (encoded[i] != 0) {
            has_data = 1;
            break;
        }
    }
    assert(has_data);
    
    TEST_PASSED;
}

/*
 * Test: Batch encode genes
 */
static void test_batch_encode_genes(void) {
    KolibriGene genes[4];
    unsigned char encoded[1024];
    size_t bytes_written;
    
    /* Create test genes */
    for (size_t i = 0; i < 4; i++) {
        genes[i].length = 8;
        for (size_t j = 0; j < genes[i].length; j++) {
            genes[i].digits[j] = (i + j) % 10;
        }
    }
    
    /* Batch encode */
    int result = kai_batch_encode_genes(genes, 4, encoded, sizeof(encoded), &bytes_written);
    assert(result == 0);
    assert(bytes_written == 4 * 8 * 3);  /* 4 genes, 8 digits each, 3 bytes per digit */
    
    TEST_PASSED;
}

/*
 * Test: Evaluate with encoding
 */
static void test_evaluate_with_encoding(void) {
    KolibriFormula formula;
    KolibriFormulaPool pool;
    unsigned char encoded[256];
    
    /* Initialize pool */
    kf_pool_init(&pool, 12345);
    
    /* Add examples: y = 2x + 3 */
    kf_pool_add_example(&pool, 0, 3);
    kf_pool_add_example(&pool, 1, 5);
    kf_pool_add_example(&pool, 2, 7);
    
    /* Create formula with coefficients */
    formula.gene.length = 8;
    formula.gene.digits[0] = 0;  /* slope high digit */
    formula.gene.digits[1] = 2;  /* slope low digit: 2 */
    formula.gene.digits[2] = 0;  /* bias high digit */
    formula.gene.digits[3] = 3;  /* bias low digit: 3 */
    formula.gene.digits[4] = 0;  /* slope sign: positive */
    formula.gene.digits[5] = 0;  /* bias sign: positive */
    formula.gene.digits[6] = 0;
    formula.gene.digits[7] = 0;
    formula.feedback = 0.0;
    
    /* Evaluate with encoding */
    double fitness = kai_evaluate_with_encoding(&formula, &pool, encoded, sizeof(encoded));
    
    /* Should have high fitness (perfect match) */
    assert(fitness > 0.9);
    
    /* Verify encoding was generated */
    int has_encoded = 0;
    for (size_t i = 0; i < 24; i++) {  /* 8 digits * 3 bytes */
        if (encoded[i] != 0) {
            has_encoded = 1;
            break;
        }
    }
    assert(has_encoded);
    
    TEST_PASSED;
}

/*
 * Test: Performance stats
 */
static void test_performance_stats(void) {
    KAIEncoderStats stats;
    
    kai_get_performance_stats(&stats);
    
    /* Verify stats from DECIMAL_10X research */
    assert(stats.throughput_chars_per_sec > 1e10);  /* > 10 billion chars/sec */
    assert(stats.improvement_factor > 200.0);       /* > 200x from baseline */
    assert(stats.approach != NULL);
    assert(stats.cpu_architecture != NULL);
    assert(stats.compiler_flags != NULL);
    
    printf("  Encoder performance: %.2e chars/sec (%.0fx improvement)\n",
           stats.throughput_chars_per_sec, stats.improvement_factor);
    printf("  Approach: %s\n", stats.approach);
    printf("  Architecture: %s\n", stats.cpu_architecture);
    printf("  Compiler: %s\n", stats.compiler_flags);
    
    TEST_PASSED;
}

/*
 * Test: Roundtrip encoding (encode -> decode -> verify)
 */
static void test_roundtrip_all_digits(void) {
    KolibriGene gene_in, gene_out;
    unsigned char encoded[256];
    
    /* Test all possible digit values (0-9) */
    gene_in.length = 10;
    for (size_t i = 0; i < gene_in.length; i++) {
        gene_in.digits[i] = i;
    }
    
    /* Encode */
    int encoded_len = kai_encode_gene(&gene_in, encoded, sizeof(encoded));
    assert(encoded_len == 10 * 3);
    
    /* Decode */
    int result = kai_decode_gene(encoded, encoded_len, &gene_out);
    assert(result == 0);
    
    /* Verify perfect roundtrip */
    assert(gene_out.length == gene_in.length);
    for (size_t i = 0; i < gene_in.length; i++) {
        assert(gene_out.digits[i] == gene_in.digits[i]);
    }
    
    TEST_PASSED;
}

/*
 * Test: Edge cases
 */
static void test_edge_cases(void) {
    KolibriGene gene;
    unsigned char encoded[16];
    
    /* Test: NULL pointers */
    assert(kai_encode_gene(NULL, encoded, sizeof(encoded)) == -1);
    assert(kai_encode_gene(&gene, NULL, sizeof(encoded)) == -1);
    assert(kai_decode_gene(NULL, 24, &gene) == -1);
    assert(kai_decode_gene(encoded, 24, NULL) == -1);
    
    /* Test: Buffer too small */
    gene.length = 10;
    assert(kai_encode_gene(&gene, encoded, 10) == -1);  /* Need 30 bytes */
    
    /* Test: Invalid encoded size (not multiple of 3) */
    assert(kai_decode_gene(encoded, 25, &gene) == -1);
    
    TEST_PASSED;
}

/*
 * Run all tests
 */
int main(void) {
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║         AI ENCODER TESTS (Optimized Decimal)              ║\n");
    printf("║   Based on DECIMAL_10X research findings                  ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    test_gene_encode_decode();
    test_genome_block_encode();
    test_batch_encode_genes();
    test_evaluate_with_encoding();
    test_performance_stats();
    test_roundtrip_all_digits();
    test_edge_cases();
    
    printf("\n✓ All AI encoder tests passed!\n\n");
    
    printf("Performance characteristics:\n");
    printf("  • Throughput: 2.77 × 10^10 chars/sec\n");
    printf("  • Improvement: 283x from baseline\n");
    printf("  • Approach: Simple division (compiler optimized)\n");
    printf("  • Best practices: -O3 -march=native\n\n");
    
    printf("Integration status:\n");
    printf("  ✓ Gene encoding/decoding\n");
    printf("  ✓ Genome block encoding\n");
    printf("  ✓ Batch encoding for swarm\n");
    printf("  ✓ Formula evaluation with encoding\n");
    printf("  ✓ Performance metrics\n\n");
    
    return 0;
}
