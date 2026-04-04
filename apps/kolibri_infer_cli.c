#include "kolibri/inference.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *prog) {
    fprintf(stderr,
            "Kolibri C inference CLI\n"
            "\nUsage: %s --query TEXT [options]\n"
            "\nOptions:\n"
            "  --query TEXT         Query to run through C inference\n"
            "  --strategy NAME      direct|formula|logical|chain|hybrid (default: formula)\n"
            "  --temperature VALUE  Set inference temperature\n"
            "  --help               Show this help\n",
            prog);
}

static int parse_strategy(const char *raw, KolibriInferenceStrategy *out) {
    if (!raw || !out) {
        return -1;
    }
    if (strcmp(raw, "direct") == 0) {
        *out = KOLIBRI_INF_DIRECT;
        return 0;
    }
    if (strcmp(raw, "formula") == 0) {
        *out = KOLIBRI_INF_FORMULA;
        return 0;
    }
    if (strcmp(raw, "logical") == 0) {
        *out = KOLIBRI_INF_LOGICAL;
        return 0;
    }
    if (strcmp(raw, "chain") == 0) {
        *out = KOLIBRI_INF_CHAIN;
        return 0;
    }
    if (strcmp(raw, "hybrid") == 0) {
        *out = KOLIBRI_INF_HYBRID;
        return 0;
    }
    return -1;
}

static void print_digit_votes(const KolibriNumericVoteSummary *summary) {
    if (!summary) {
        printf("DIGIT_VOTES=\n");
        return;
    }
    printf("DIGIT_VOTES=");
    for (size_t i = 0; i < KOLIBRI_INF_DIGIT_VOTERS; ++i) {
        if (i > 0U) {
            printf(",");
        }
        printf("%zu:%.6f", i, summary->channels[i]);
    }
    printf("\n");
}

int main(int argc, char **argv) {
    const char *query = NULL;
    const char *strategy_raw = "formula";
    double temperature = 0.35;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--query") == 0 && i + 1 < argc) {
            query = argv[++i];
        } else if (strcmp(argv[i], "--strategy") == 0 && i + 1 < argc) {
            strategy_raw = argv[++i];
        } else if (strcmp(argv[i], "--temperature") == 0 && i + 1 < argc) {
            temperature = atof(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "[kolibri-infer] unknown arg: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!query || query[0] == '\0') {
        fprintf(stderr, "[kolibri-infer] --query is required\n");
        print_usage(argv[0]);
        return 1;
    }

    KolibriInferenceContext *ctx = kolibri_inference_create();
    if (!ctx) {
        fprintf(stderr, "[kolibri-infer] failed to create inference context\n");
        return 2;
    }

    KolibriInferenceStrategy strategy = KOLIBRI_INF_FORMULA;
    if (parse_strategy(strategy_raw, &strategy) != 0) {
        fprintf(stderr, "[kolibri-infer] invalid strategy: %s\n", strategy_raw);
        kolibri_inference_destroy(ctx);
        return 1;
    }

    (void)kolibri_inference_set_strategy(ctx, strategy);
    (void)kolibri_inference_set_temperature(ctx, temperature);

    KolibriInferenceResult result;
    if (kolibri_inference_run(ctx, query, &result) != 0) {
        fprintf(stderr, "[kolibri-infer] inference failed\n");
        kolibri_inference_destroy(ctx);
        return 3;
    }

    printf("STATUS=ok\n");
    printf("STRATEGY=%s\n", strategy_raw);
    printf("CONFIDENCE=%.6f\n", result.total_confidence);
    printf("KNOWLEDGE_HITS=%zu\n", result.knowledge_hits);
    printf("FORMULAS_APPLIED=%zu\n", result.formulas_applied);
    printf("LOGIC_RULES=%zu\n", result.logic_rules_fired);
    printf("QUERY_KIND=%s\n", result.query_semantics.query_kind);
    printf("CANONICAL_TOPIC=%s\n", result.query_semantics.canonical_topic);
    printf("DEFINITION_ENTITY=%s\n", result.query_semantics.definition_entity);
    printf("TOPIC_TOKEN_COUNT=%zu\n", result.query_semantics.topic_token_count);
    printf("DIGIT_WINNER=%u\n", (unsigned)result.numeric_vote.winner_digit);
    printf("DIGIT_WINNER_SCORE=%.6f\n", result.numeric_vote.winner_score);
    printf("DIGIT_RUNNER_UP_SCORE=%.6f\n", result.numeric_vote.runner_up_score);
    printf("DIGIT_CONSENSUS=%.6f\n", result.numeric_vote.consensus);
    print_digit_votes(&result.numeric_vote);
    printf("DURATION_MS=%.3f\n", result.total_duration_ms);
    printf("RESPONSE_BEGIN\n");
    printf("%s\n", result.response);
    printf("RESPONSE_END\n");

    kolibri_inference_destroy(ctx);
    return 0;
}
