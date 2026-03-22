#include "kolibri/formula.h"
#include "kolibri/roy.h"

#include <arpa/inet.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define KSB_NODE_COUNT 10U
#define KSB_BASE_PORT 53100U
#define KSB_TRAIN_EXAMPLES 32
#define KSB_VALID_EXAMPLES 16
#define KSB_ROUNDS 5U
#define KSB_GENS_PER_ROUND 4U

typedef struct {
    double exact_ratio;
    double mae;
    double fitness;
} KsbScore;

typedef struct {
    double avg_exact;
    double avg_mae;
    double best_exact;
    size_t imports_total;
} KsbClusterScore;

typedef struct {
    KolibriFormulaPool pool;
    KolibriRoy roy;
    uint32_t id;
    uint16_t port;
    size_t imported;
} KsbNode;

typedef struct {
    KsbScore single;
    KsbClusterScore isolated_rounds[KSB_ROUNDS];
    KsbClusterScore swarm_rounds[KSB_ROUNDS];
    KsbClusterScore isolated_final;
    KsbClusterScore swarm_final;
    double avg_exact_delta;
    double avg_mae_delta;
    double mae_improvement_x;
    time_t finished_at;
} KsbReport;

typedef struct {
    const char *json_out;
    int loop_forever;
    unsigned int interval_sec;
} KsbOptions;

static const unsigned char KSB_SWARM_KEY[] = "kolibri-swarm-benchmark-key";

static int ksb_target(int input) {
    return input + 1;
}

static void ksb_seed_examples(KolibriFormulaPool *pool) {
    for (int i = 0; i < KSB_TRAIN_EXAMPLES; ++i) {
        (void)kf_pool_add_example(pool, i, ksb_target(i));
    }
}

static void ksb_configure_pool(KolibriFormulaPool *pool, size_t node_index) {
    KolibriEvolutionConfig config;
    kf_config_default(&config);
    config.mutation_rate = 0.10 + (double)(node_index % 4U) * 0.03;
    config.mutation_strength = 1.0 + (double)(node_index % 3U);
    config.mutation_type = (KolibriMutationType)(node_index % KOLIBRI_MUTATION_COUNT);
    config.crossover_type =
        (KolibriCrossoverType)(node_index % KOLIBRI_CROSSOVER_COUNT);
    config.adaptive_mutation = 1;
    config.generations_per_tick = 1U;
    (void)kf_pool_set_config(pool, &config);
}

static void ksb_init_pool(KolibriFormulaPool *pool, uint64_t seed,
                          size_t node_index) {
    kf_pool_init(pool, seed);
    ksb_seed_examples(pool);
    ksb_configure_pool(pool, node_index);
}

static KsbScore ksb_score_formula(const KolibriFormula *formula) {
    KsbScore score;
    memset(&score, 0, sizeof(score));
    if (!formula) {
        score.mae = 1e9;
        return score;
    }

    double total_abs_error = 0.0;
    size_t exact = 0U;
    for (int x = KSB_TRAIN_EXAMPLES;
         x < KSB_TRAIN_EXAMPLES + KSB_VALID_EXAMPLES; ++x) {
        int prediction = 0;
        if (kf_formula_apply(formula, x, &prediction) != 0) {
            total_abs_error += 1000000.0;
            continue;
        }
        int target = ksb_target(x);
        int diff = target - prediction;
        if (diff == 0) {
            ++exact;
        }
        total_abs_error += fabs((double)diff);
    }

    score.exact_ratio = (double)exact / (double)KSB_VALID_EXAMPLES;
    score.mae = total_abs_error / (double)KSB_VALID_EXAMPLES;
    score.fitness = formula->fitness;
    return score;
}

static KsbScore ksb_score_pool(const KolibriFormulaPool *pool) {
    return ksb_score_formula(kf_pool_best(pool));
}

static void ksb_print_score(const char *label, KsbScore score) {
    printf("%s exact=%.3f mae=%.3f fitness=%.6f\n", label, score.exact_ratio,
           score.mae, score.fitness);
}

static double ksb_average_exact(const KsbNode *nodes, size_t count) {
    double sum = 0.0;
    for (size_t i = 0; i < count; ++i) {
        sum += ksb_score_pool(&nodes[i].pool).exact_ratio;
    }
    return sum / (double)count;
}

static double ksb_average_mae(const KsbNode *nodes, size_t count) {
    double sum = 0.0;
    for (size_t i = 0; i < count; ++i) {
        sum += ksb_score_pool(&nodes[i].pool).mae;
    }
    return sum / (double)count;
}

static double ksb_best_exact(const KsbNode *nodes, size_t count) {
    double best = 0.0;
    for (size_t i = 0; i < count; ++i) {
        double exact = ksb_score_pool(&nodes[i].pool).exact_ratio;
        if (exact > best) {
            best = exact;
        }
    }
    return best;
}

static void ksb_train_round(KsbNode *nodes, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        (void)kf_reactor_run(&nodes[i].pool, KSB_GENS_PER_ROUND, 0.999);
    }
}

static size_t ksb_imports_total(const KsbNode *nodes, size_t count) {
    size_t total = 0U;
    for (size_t i = 0; i < count; ++i) {
        total += nodes[i].imported;
    }
    return total;
}

static KsbClusterScore ksb_capture_cluster_score(const KsbNode *nodes,
                                                 size_t count) {
    KsbClusterScore score;
    memset(&score, 0, sizeof(score));
    score.avg_exact = ksb_average_exact(nodes, count);
    score.avg_mae = ksb_average_mae(nodes, count);
    score.best_exact = ksb_best_exact(nodes, count);
    score.imports_total = ksb_imports_total(nodes, count);
    return score;
}

static void ksb_print_cluster_score(const char *label, KsbClusterScore score) {
    printf("%s avg_exact=%.3f avg_mae=%.3f best_exact=%.3f imports_total=%zu\n",
           label, score.avg_exact, score.avg_mae, score.best_exact,
           score.imports_total);
}

static int ksb_init_swarm_nodes(KsbNode *nodes, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        memset(&nodes[i], 0, sizeof(nodes[i]));
        nodes[i].id = 1000U + (uint32_t)i;
        nodes[i].port = (uint16_t)(KSB_BASE_PORT + i);
        ksb_init_pool(&nodes[i].pool, 0xC0FFEEULL + (uint64_t)i * 17ULL, i);
        if (kolibri_roy_zapustit(&nodes[i].roy, nodes[i].id, nodes[i].port,
                                 KSB_SWARM_KEY,
                                 sizeof(KSB_SWARM_KEY) - 1U) != 0) {
            return -1;
        }
    }

    for (size_t i = 0; i < count; ++i) {
        for (size_t j = 0; j < count; ++j) {
            if (i == j) {
                continue;
            }
            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(nodes[j].port);
            inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
            (void)kolibri_roy_dobavit_soseda(&nodes[i].roy, &addr,
                                             nodes[j].id);
        }
    }
    usleep(250000);
    return 0;
}

static void ksb_stop_swarm_nodes(KsbNode *nodes, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        kolibri_roy_ostanovit(&nodes[i].roy);
        kf_pool_free(&nodes[i].pool);
    }
}

static void ksb_swarm_exchange(KsbNode *nodes, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const KolibriFormula *best = kf_pool_best(&nodes[i].pool);
        if (best) {
            (void)kolibri_roy_otpravit_vsem(&nodes[i].roy, best);
        }
    }
    usleep(250000);

    for (size_t i = 0; i < count; ++i) {
        KolibriRoySobytie *event =
            (KolibriRoySobytie *)calloc(1U, sizeof(KolibriRoySobytie));
        if (!event) {
            continue;
        }
        while (kolibri_roy_poluchit_sobytie(&nodes[i].roy, event) > 0) {
            if (event->tip != KOLIBRI_ROY_SOBYTIE_FORMULA) {
                continue;
            }
            if (kf_pool_import_formula(&nodes[i].pool, &event->formula, 1) ==
                0) {
                nodes[i].imported++;
            }
        }
        free(event);
    }
}

static void ksb_run_isolated(KsbNode *nodes, size_t count, KsbReport *report) {
    for (size_t round = 0; round < KSB_ROUNDS; ++round) {
        ksb_train_round(nodes, count);
        KsbClusterScore score = ksb_capture_cluster_score(nodes, count);
        if (report) {
            report->isolated_rounds[round] = score;
        }
        printf("[isolated] round=%zu avg_exact=%.3f avg_mae=%.3f best_exact=%.3f\n",
               round + 1U, score.avg_exact, score.avg_mae, score.best_exact);
    }
}

static int ksb_run_swarm(KsbNode *nodes, size_t count, KsbReport *report) {
    if (ksb_init_swarm_nodes(nodes, count) != 0) {
        return -1;
    }
    for (size_t round = 0; round < KSB_ROUNDS; ++round) {
        ksb_train_round(nodes, count);
        ksb_swarm_exchange(nodes, count);
        KsbClusterScore score = ksb_capture_cluster_score(nodes, count);
        if (report) {
            report->swarm_rounds[round] = score;
        }
        printf("[swarm] round=%zu avg_exact=%.3f avg_mae=%.3f best_exact=%.3f imports=%zu\n",
               round + 1U, score.avg_exact, score.avg_mae, score.best_exact,
               score.imports_total);
    }
    return 0;
}

static void ksb_init_isolated_nodes(KsbNode *nodes, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        memset(&nodes[i], 0, sizeof(nodes[i]));
        ksb_init_pool(&nodes[i].pool, 0xC0FFEEULL + (uint64_t)i * 17ULL, i);
    }
}

static void ksb_free_isolated_nodes(KsbNode *nodes, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        kf_pool_free(&nodes[i].pool);
    }
}

static KsbScore ksb_run_single_node(void) {
    KolibriFormulaPool pool;
    KsbScore score;
    ksb_init_pool(&pool, 0xABCDULL, 0U);
    (void)kf_reactor_run(&pool, KSB_ROUNDS * KSB_GENS_PER_ROUND, 0.999);
    score = ksb_score_pool(&pool);
    ksb_print_score("[single]", score);
    kf_pool_free(&pool);
    return score;
}

static void ksb_write_rounds_json(FILE *out, const char *key,
                                  const KsbClusterScore *scores, size_t count) {
    fprintf(out, "\"%s\":[", key);
    for (size_t i = 0; i < count; ++i) {
        fprintf(out,
                "%s{\"round\":%zu,\"avg_exact\":%.6f,\"avg_mae\":%.6f,"
                "\"best_exact\":%.6f,\"imports_total\":%zu}",
                i == 0U ? "" : ",", i + 1U, scores[i].avg_exact,
                scores[i].avg_mae, scores[i].best_exact,
                scores[i].imports_total);
    }
    fprintf(out, "]");
}

static int ksb_write_report_json(const char *path, const KsbReport *report) {
    FILE *out;
    if (!path || !report) {
        return -1;
    }
    out = fopen(path, "w");
    if (!out) {
        return -1;
    }

    fprintf(out, "{");
    fprintf(out, "\"timestamp\":%lld,", (long long)report->finished_at);
    fprintf(out, "\"node_count\":%u,", KSB_NODE_COUNT);
    fprintf(out, "\"rounds\":%u,", KSB_ROUNDS);
    fprintf(out, "\"gens_per_round\":%u,", KSB_GENS_PER_ROUND);
    fprintf(out,
            "\"single\":{\"exact\":%.6f,\"mae\":%.6f,\"fitness\":%.6f},",
            report->single.exact_ratio, report->single.mae,
            report->single.fitness);
    fprintf(out,
            "\"isolated_final\":{\"avg_exact\":%.6f,\"avg_mae\":%.6f,"
            "\"best_exact\":%.6f,\"imports_total\":%zu},",
            report->isolated_final.avg_exact, report->isolated_final.avg_mae,
            report->isolated_final.best_exact,
            report->isolated_final.imports_total);
    fprintf(out,
            "\"swarm_final\":{\"avg_exact\":%.6f,\"avg_mae\":%.6f,"
            "\"best_exact\":%.6f,\"imports_total\":%zu},",
            report->swarm_final.avg_exact, report->swarm_final.avg_mae,
            report->swarm_final.best_exact, report->swarm_final.imports_total);
    fprintf(out,
            "\"comparison\":{\"avg_exact_delta\":%.6f,\"avg_mae_delta\":%.6f,"
            "\"mae_improvement_x\":%.6f},",
            report->avg_exact_delta, report->avg_mae_delta,
            report->mae_improvement_x);
    ksb_write_rounds_json(out, "isolated_rounds", report->isolated_rounds,
                          KSB_ROUNDS);
    fprintf(out, ",");
    ksb_write_rounds_json(out, "swarm_rounds", report->swarm_rounds,
                          KSB_ROUNDS);
    fprintf(out, "}\n");
    fclose(out);
    return 0;
}

static void ksb_options_default(KsbOptions *options) {
    if (!options) {
        return;
    }
    memset(options, 0, sizeof(*options));
    options->interval_sec = 300U;
}

static int ksb_parse_options(int argc, char **argv, KsbOptions *options) {
    ksb_options_default(options);
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--json-out") == 0) {
            if (i + 1 >= argc) {
                return -1;
            }
            options->json_out = argv[++i];
        } else if (strcmp(argv[i], "--loop") == 0) {
            options->loop_forever = 1;
        } else if (strcmp(argv[i], "--interval-sec") == 0) {
            if (i + 1 >= argc) {
                return -1;
            }
            options->interval_sec = (unsigned int)strtoul(argv[++i], NULL, 10);
            if (options->interval_sec == 0U) {
                options->interval_sec = 300U;
            }
        } else {
            return -1;
        }
    }
    return 0;
}

static int ksb_run_benchmark_once(const KsbOptions *options, KsbReport *report) {
    KsbNode *isolated = (KsbNode *)calloc(KSB_NODE_COUNT, sizeof(KsbNode));
    KsbNode *swarm = (KsbNode *)calloc(KSB_NODE_COUNT, sizeof(KsbNode));
    if (!isolated || !swarm || !report) {
        fprintf(stderr, "allocation failed\n");
        free(isolated);
        free(swarm);
        return 1;
    }
    memset(report, 0, sizeof(*report));

    printf("Kolibri Swarm Benchmark\n");
    printf("nodes=%u rounds=%u gens_per_round=%u train=%u valid=%u\n",
           KSB_NODE_COUNT, KSB_ROUNDS, KSB_GENS_PER_ROUND, KSB_TRAIN_EXAMPLES,
           KSB_VALID_EXAMPLES);

    report->single = ksb_run_single_node();

    ksb_init_isolated_nodes(isolated, KSB_NODE_COUNT);
    ksb_run_isolated(isolated, KSB_NODE_COUNT, report);
    report->isolated_final = ksb_capture_cluster_score(isolated, KSB_NODE_COUNT);
    ksb_print_cluster_score("[isolated-final]", report->isolated_final);

    if (ksb_run_swarm(swarm, KSB_NODE_COUNT, report) != 0) {
        fprintf(stderr, "swarm init failed\n");
        ksb_free_isolated_nodes(isolated, KSB_NODE_COUNT);
        free(isolated);
        free(swarm);
        return 1;
    }
    report->swarm_final = ksb_capture_cluster_score(swarm, KSB_NODE_COUNT);
    ksb_print_cluster_score("[swarm-final]", report->swarm_final);
    report->avg_exact_delta =
        report->swarm_final.avg_exact - report->isolated_final.avg_exact;
    report->avg_mae_delta =
        report->isolated_final.avg_mae - report->swarm_final.avg_mae;
    report->mae_improvement_x =
        report->swarm_final.avg_mae > 0.0
            ? report->isolated_final.avg_mae / report->swarm_final.avg_mae
            : 0.0;
    report->finished_at = time(NULL);
    printf("[comparison] avg_exact_delta=%.3f avg_mae_delta=%.3f mae_improvement_x=%.3f\n",
           report->avg_exact_delta, report->avg_mae_delta,
           report->mae_improvement_x);

    ksb_stop_swarm_nodes(swarm, KSB_NODE_COUNT);
    ksb_free_isolated_nodes(isolated, KSB_NODE_COUNT);
    free(isolated);
    free(swarm);

    if (options && options->json_out) {
        (void)ksb_write_report_json(options->json_out, report);
    }
    return 0;
}

int main(int argc, char **argv) {
    KsbOptions options;
    KsbReport report;
    setvbuf(stdout, NULL, _IONBF, 0);

    if (ksb_parse_options(argc, argv, &options) != 0) {
        fprintf(stderr,
                "usage: %s [--json-out PATH] [--loop] [--interval-sec SEC]\n",
                argv[0]);
        return 1;
    }

    do {
        if (ksb_run_benchmark_once(&options, &report) != 0) {
            return 1;
        }
        if (!options.loop_forever) {
            break;
        }
        sleep(options.interval_sec);
    } while (1);

    return 0;
}
