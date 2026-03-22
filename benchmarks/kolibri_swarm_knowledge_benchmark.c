#include "kolibri/formula.h"
#include "kolibri/roy.h"
#include "kolibri/symbol_table.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define KSK_NODE_COUNT 10U
#define KSK_BASE_PORT 54100U
#define KSK_MAX_DOCS 128U
#define KSK_MAX_DOMAINS 32U
#define KSK_ROUNDS 5U
#define KSK_SINGLE_DOCS_PER_ROUND 1U
#define KSK_SWARM_DOCS_PER_NODE_PER_ROUND 1U

typedef struct {
    char title[256];
    char question[256];
    char answer[512];
    char source[128];
    int input_hash;
    int output_hash;
} KskDocument;

typedef struct {
    double hit_ratio;
    double best_hit_ratio;
    size_t imported_total;
} KskClusterScore;

typedef struct {
    char domain[64];
    size_t documents;
    double single_hit_ratio;
    double isolated_hit_ratio;
    double swarm_hit_ratio;
    double swarm_vs_single_delta;
    double swarm_vs_isolated_delta;
} KskDomainScore;

typedef struct {
    KolibriFormulaPool pool;
    KolibriSymbolTable symbols;
    KolibriRoy roy;
    uint32_t id;
    uint16_t port;
    size_t imported_total;
} KskNode;

typedef struct {
    double single_hit_ratio;
    KskClusterScore isolated_rounds[KSK_ROUNDS];
    KskClusterScore swarm_rounds[KSK_ROUNDS];
    KskClusterScore isolated_final;
    KskClusterScore swarm_final;
    double swarm_vs_single_delta;
    double swarm_vs_isolated_delta;
    size_t total_documents;
    KskDomainScore domain_scores[KSK_MAX_DOMAINS];
    size_t domain_score_count;
    time_t finished_at;
} KskReport;

typedef struct {
    const char *json_out;
    const char *docs_root;
    int loop_forever;
    unsigned int interval_sec;
} KskOptions;

static const unsigned char KSK_SWARM_KEY[] =
    "kolibri-swarm-knowledge-benchmark-key";

static void ksk_trim_inplace(char *text) {
    size_t len = 0U;
    size_t start = 0U;
    size_t end = 0U;
    if (!text) {
        return;
    }
    len = strlen(text);
    while (start < len &&
           (text[start] == ' ' || text[start] == '\n' || text[start] == '\r' ||
            text[start] == '\t')) {
        ++start;
    }
    end = len;
    while (end > start &&
           (text[end - 1U] == ' ' || text[end - 1U] == '\n' ||
            text[end - 1U] == '\r' || text[end - 1U] == '\t')) {
        --end;
    }
    if (start > 0U) {
        memmove(text, text + start, end - start);
    }
    text[end - start] = '\0';
}

static int ksk_is_directory(const char *path) {
    struct stat st;
    if (!path || stat(path, &st) != 0) {
        return 0;
    }
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

static char *ksk_read_file(const char *path, size_t max_bytes) {
    FILE *file = NULL;
    char *buffer = NULL;
    size_t read_bytes = 0U;
    if (!path || max_bytes == 0U) {
        return NULL;
    }
    file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    buffer = (char *)calloc(max_bytes + 1U, 1U);
    if (!buffer) {
        fclose(file);
        return NULL;
    }
    read_bytes = fread(buffer, 1U, max_bytes, file);
    buffer[read_bytes] = '\0';
    fclose(file);
    return buffer;
}

static int ksk_extract_document(const char *root, const char *path,
                                KskDocument *doc) {
    char *content = NULL;
    char *cursor = NULL;
    char *line_end = NULL;
    char *paragraph = NULL;
    if (!path || !doc) {
        return -1;
    }
    content = ksk_read_file(path, 4096U);
    if (!content) {
        return -1;
    }
    memset(doc, 0, sizeof(*doc));

    cursor = content;
    while (*cursor == '\n' || *cursor == '\r') {
        ++cursor;
    }
    line_end = strchr(cursor, '\n');
    if (!line_end) {
        free(content);
        return -1;
    }
    *line_end = '\0';
    if (strncmp(cursor, "# ", 2) == 0) {
        snprintf(doc->title, sizeof(doc->title), "%s", cursor + 2);
    } else {
        snprintf(doc->title, sizeof(doc->title), "%s", cursor);
    }
    ksk_trim_inplace(doc->title);

    paragraph = line_end + 1U;
    while (*paragraph == '\n' || *paragraph == '\r') {
        ++paragraph;
    }
    line_end = strstr(paragraph, "\n1 ");
    if (!line_end) {
        line_end = strstr(paragraph, "\n2 ");
    }
    if (line_end) {
        *line_end = '\0';
    }
    snprintf(doc->answer, sizeof(doc->answer), "%s", paragraph);
    ksk_trim_inplace(doc->answer);
    if (doc->title[0] == '\0' || doc->answer[0] == '\0') {
        free(content);
        return -1;
    }
    snprintf(doc->question, sizeof(doc->question), "что такое %s", doc->title);
    if (root && strstr(path, root) == path) {
        const char *sub = path + strlen(root);
        if (*sub == '/') {
            ++sub;
        }
        snprintf(doc->source, sizeof(doc->source), "%s", sub);
    } else {
        snprintf(doc->source, sizeof(doc->source), "%s", path);
    }
    doc->input_hash = kf_hash_from_text(doc->question);
    doc->output_hash = kf_hash_from_text(doc->answer);
    free(content);
    return 0;
}

static void ksk_extract_domain_from_source(const char *source, char *out,
                                           size_t out_size) {
    const char *slash = NULL;
    size_t len = 0U;
    if (!out || out_size == 0U) {
        return;
    }
    out[0] = '\0';
    if (!source || source[0] == '\0') {
        snprintf(out, out_size, "%s", "root");
        return;
    }
    slash = strchr(source, '/');
    len = slash ? (size_t)(slash - source) : strlen(source);
    if (len == 0U) {
        snprintf(out, out_size, "%s", "root");
        return;
    }
    if (len >= out_size) {
        len = out_size - 1U;
    }
    memcpy(out, source, len);
    out[len] = '\0';
}

static void ksk_collect_documents(const char *root, const char *path,
                                  KskDocument *docs, size_t *count,
                                  size_t capacity) {
    DIR *dir = NULL;
    struct dirent *entry = NULL;
    char child[1024];
    if (!root || !path || !docs || !count || *count >= capacity) {
        return;
    }
    if (!ksk_is_directory(path)) {
        const char *ext = strrchr(path, '.');
        if (ext && (strcmp(ext, ".txt") == 0 || strcmp(ext, ".md") == 0) &&
            *count < capacity &&
            ksk_extract_document(root, path, &docs[*count]) == 0) {
            (*count)++;
        }
        return;
    }
    dir = opendir(path);
    if (!dir) {
        return;
    }
    while ((entry = readdir(dir)) != NULL && *count < capacity) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        ksk_collect_documents(root, child, docs, count, capacity);
    }
    closedir(dir);
}

static void ksk_init_pool(KskNode *node, uint64_t seed) {
    kf_pool_init(&node->pool, seed);
    kolibri_symbol_table_init(&node->symbols, NULL);
    kolibri_symbol_table_seed_defaults(&node->symbols);
}

static void ksk_free_node_pool(KskNode *node) {
    if (!node) {
        return;
    }
    kf_pool_free(&node->pool);
}

static void ksk_ingest_document(KskNode *node, const KskDocument *doc) {
    if (!node || !doc) {
        return;
    }
    (void)kf_pool_add_association(&node->pool, &node->symbols, doc->question,
                                  doc->answer, doc->source,
                                  (uint64_t)time(NULL));
    (void)kf_pool_tick(&node->pool, 2U);
}

static double ksk_hit_ratio(const KskNode *node, const KskDocument *docs,
                            size_t doc_count) {
    const KolibriFormula *best = NULL;
    size_t hits = 0U;
    char answer[768];
    if (!node || !docs || doc_count == 0U) {
        return 0.0;
    }
    best = kf_pool_best(&node->pool);
    if (!best) {
        return 0.0;
    }
    for (size_t i = 0; i < doc_count; ++i) {
        memset(answer, 0, sizeof(answer));
        if (kf_formula_lookup_answer(best, docs[i].input_hash, answer,
                                     sizeof(answer)) == 0 &&
            kf_hash_from_text(answer) == docs[i].output_hash) {
            hits++;
        }
    }
    return (double)hits / (double)doc_count;
}

static double ksk_hit_ratio_for_domain(const KskNode *node,
                                       const KskDocument *docs,
                                       size_t doc_count,
                                       const char *domain) {
    const KolibriFormula *best = NULL;
    size_t hits = 0U;
    size_t total = 0U;
    char answer[768];
    char doc_domain[64];
    if (!node || !docs || doc_count == 0U || !domain || domain[0] == '\0') {
        return 0.0;
    }
    best = kf_pool_best(&node->pool);
    if (!best) {
        return 0.0;
    }
    for (size_t i = 0; i < doc_count; ++i) {
        ksk_extract_domain_from_source(docs[i].source, doc_domain,
                                       sizeof(doc_domain));
        if (strcmp(doc_domain, domain) != 0) {
            continue;
        }
        total++;
        memset(answer, 0, sizeof(answer));
        if (kf_formula_lookup_answer(best, docs[i].input_hash, answer,
                                     sizeof(answer)) == 0 &&
            kf_hash_from_text(answer) == docs[i].output_hash) {
            hits++;
        }
    }
    if (total == 0U) {
        return 0.0;
    }
    return (double)hits / (double)total;
}

static KskClusterScore ksk_capture_cluster_score(const KskNode *nodes,
                                                 size_t node_count,
                                                 const KskDocument *docs,
                                                 size_t doc_count) {
    KskClusterScore score;
    double total = 0.0;
    memset(&score, 0, sizeof(score));
    for (size_t i = 0; i < node_count; ++i) {
        double hit_ratio = ksk_hit_ratio(&nodes[i], docs, doc_count);
        total += hit_ratio;
        if (hit_ratio > score.best_hit_ratio) {
            score.best_hit_ratio = hit_ratio;
        }
        score.imported_total += nodes[i].imported_total;
    }
    score.hit_ratio = total / (double)node_count;
    return score;
}

static int ksk_init_swarm_nodes(KskNode *nodes, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        memset(&nodes[i], 0, sizeof(nodes[i]));
        nodes[i].id = 2000U + (uint32_t)i;
        nodes[i].port = (uint16_t)(KSK_BASE_PORT + i);
        ksk_init_pool(&nodes[i], 0xBEEF0000ULL + (uint64_t)i * 31ULL);
        if (kolibri_roy_zapustit(&nodes[i].roy, nodes[i].id, nodes[i].port,
                                 KSK_SWARM_KEY,
                                 sizeof(KSK_SWARM_KEY) - 1U) != 0) {
            return -1;
        }
    }
    for (size_t i = 0; i < count; ++i) {
        for (size_t j = 0; j < count; ++j) {
            struct sockaddr_in addr;
            if (i == j) {
                continue;
            }
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(nodes[j].port);
            inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
            (void)kolibri_roy_dobavit_soseda(&nodes[i].roy, &addr, nodes[j].id);
        }
    }
    usleep(250000);
    return 0;
}

static void ksk_stop_swarm_nodes(KskNode *nodes, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        kolibri_roy_ostanovit(&nodes[i].roy);
        ksk_free_node_pool(&nodes[i]);
    }
}

static void ksk_init_plain_nodes(KskNode *nodes, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        memset(&nodes[i], 0, sizeof(nodes[i]));
        ksk_init_pool(&nodes[i], 0xCAFE0000ULL + (uint64_t)i * 17ULL);
    }
}

static void ksk_free_plain_nodes(KskNode *nodes, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        ksk_free_node_pool(&nodes[i]);
    }
}

static void ksk_swarm_receive(KskNode *nodes, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        KolibriRoySobytie *event =
            (KolibriRoySobytie *)calloc(1U, sizeof(KolibriRoySobytie));
        if (!event) {
            continue;
        }
        while (kolibri_roy_poluchit_sobytie(&nodes[i].roy, event) > 0) {
            if (event->tip != KOLIBRI_ROY_SOBYTIE_ASSOCIATION) {
                continue;
            }
            if (kf_pool_import_association(&nodes[i].pool, &nodes[i].symbols,
                                           &event->association) == 0) {
                nodes[i].imported_total++;
            }
        }
        free(event);
        (void)kf_pool_tick(&nodes[i].pool, 1U);
    }
}

static void ksk_run_single(const KskDocument *docs, size_t doc_count,
                           KskReport *report) {
    KskNode *node = NULL;
    size_t next_doc = 0U;
    node = (KskNode *)calloc(1U, sizeof(KskNode));
    if (!node) {
        return;
    }
    ksk_init_pool(node, 0xABCDEFULL);
    for (size_t round = 0; round < KSK_ROUNDS; ++round) {
        for (size_t i = 0; i < KSK_SINGLE_DOCS_PER_ROUND && next_doc < doc_count;
             ++i, ++next_doc) {
            ksk_ingest_document(node, &docs[next_doc]);
        }
    }
    report->single_hit_ratio = ksk_hit_ratio(node, docs, doc_count);
    ksk_free_node_pool(node);
    free(node);
}

static void ksk_run_isolated(KskNode *nodes, const KskDocument *docs,
                             size_t doc_count, KskReport *report) {
    for (size_t round = 0; round < KSK_ROUNDS; ++round) {
        for (size_t i = 0; i < KSK_NODE_COUNT; ++i) {
            for (size_t j = 0; j < KSK_SWARM_DOCS_PER_NODE_PER_ROUND; ++j) {
                size_t doc_index =
                    round * KSK_NODE_COUNT * KSK_SWARM_DOCS_PER_NODE_PER_ROUND +
                    i * KSK_SWARM_DOCS_PER_NODE_PER_ROUND + j;
                if (doc_index >= doc_count) {
                    continue;
                }
                ksk_ingest_document(&nodes[i], &docs[doc_index]);
            }
        }
        report->isolated_rounds[round] =
            ksk_capture_cluster_score(nodes, KSK_NODE_COUNT, docs, doc_count);
        printf("[knowledge-isolated] round=%zu avg_hit=%.3f best_hit=%.3f\n",
               round + 1U, report->isolated_rounds[round].hit_ratio,
               report->isolated_rounds[round].best_hit_ratio);
    }
}

static int ksk_run_swarm(KskNode *nodes, const KskDocument *docs,
                         size_t doc_count, KskReport *report) {
    if (ksk_init_swarm_nodes(nodes, KSK_NODE_COUNT) != 0) {
        return -1;
    }
    for (size_t round = 0; round < KSK_ROUNDS; ++round) {
        for (size_t i = 0; i < KSK_NODE_COUNT; ++i) {
            size_t before = nodes[i].pool.association_count;
            for (size_t j = 0; j < KSK_SWARM_DOCS_PER_NODE_PER_ROUND; ++j) {
                size_t doc_index =
                    round * KSK_NODE_COUNT * KSK_SWARM_DOCS_PER_NODE_PER_ROUND +
                    i * KSK_SWARM_DOCS_PER_NODE_PER_ROUND + j;
                if (doc_index >= doc_count) {
                    continue;
                }
                ksk_ingest_document(&nodes[i], &docs[doc_index]);
            }
            for (size_t idx = before; idx < nodes[i].pool.association_count; ++idx) {
                (void)kolibri_roy_otpravit_association_vsem(
                    &nodes[i].roy, &nodes[i].pool.associations[idx]);
            }
        }
        usleep(250000);
        ksk_swarm_receive(nodes, KSK_NODE_COUNT);
        report->swarm_rounds[round] =
            ksk_capture_cluster_score(nodes, KSK_NODE_COUNT, docs, doc_count);
        printf("[knowledge-swarm] round=%zu avg_hit=%.3f best_hit=%.3f imports=%zu\n",
               round + 1U, report->swarm_rounds[round].hit_ratio,
               report->swarm_rounds[round].best_hit_ratio,
               report->swarm_rounds[round].imported_total);
    }
    return 0;
}

static void ksk_write_cluster_json(FILE *out, const char *key,
                                   KskClusterScore score) {
    fprintf(out,
            "\"%s\":{\"hit_ratio\":%.6f,\"best_hit_ratio\":%.6f,"
            "\"imported_total\":%zu}",
            key, score.hit_ratio, score.best_hit_ratio, score.imported_total);
}

static void ksk_write_rounds_json(FILE *out, const char *key,
                                  const KskClusterScore *scores, size_t count) {
    fprintf(out, "\"%s\":[", key);
    for (size_t i = 0; i < count; ++i) {
        fprintf(out,
                "%s{\"round\":%zu,\"hit_ratio\":%.6f,\"best_hit_ratio\":%.6f,"
                "\"imported_total\":%zu}",
                i == 0U ? "" : ",", i + 1U, scores[i].hit_ratio,
                scores[i].best_hit_ratio, scores[i].imported_total);
    }
    fprintf(out, "]");
}

static void ksk_capture_domain_scores(KskReport *report,
                                      const KskNode *single_node,
                                      const KskNode *isolated_nodes,
                                      const KskNode *swarm_nodes,
                                      const KskDocument *docs,
                                      size_t doc_count) {
    char domains[KSK_MAX_DOMAINS][64];
    size_t domain_docs[KSK_MAX_DOMAINS];
    size_t domain_count = 0U;
    char doc_domain[64];
    if (!report || !single_node || !isolated_nodes || !swarm_nodes || !docs ||
        doc_count == 0U) {
        return;
    }
    memset(domains, 0, sizeof(domains));
    memset(domain_docs, 0, sizeof(domain_docs));
    memset(report->domain_scores, 0, sizeof(report->domain_scores));
    report->domain_score_count = 0U;

    for (size_t i = 0; i < doc_count; ++i) {
        size_t idx = 0U;
        int found = 0;
        ksk_extract_domain_from_source(docs[i].source, doc_domain,
                                       sizeof(doc_domain));
        for (idx = 0U; idx < domain_count; ++idx) {
            if (strcmp(domains[idx], doc_domain) == 0) {
                domain_docs[idx]++;
                found = 1;
                break;
            }
        }
        if (!found && domain_count < KSK_MAX_DOMAINS) {
            snprintf(domains[domain_count], sizeof(domains[domain_count]), "%s",
                     doc_domain);
            domain_docs[domain_count] = 1U;
            domain_count++;
        }
    }

    for (size_t i = 0; i < domain_count; ++i) {
        double isolated_total = 0.0;
        double swarm_total = 0.0;
        KskDomainScore *score = &report->domain_scores[report->domain_score_count];
        snprintf(score->domain, sizeof(score->domain), "%s", domains[i]);
        score->documents = domain_docs[i];
        score->single_hit_ratio =
            ksk_hit_ratio_for_domain(single_node, docs, doc_count, score->domain);
        for (size_t j = 0U; j < KSK_NODE_COUNT; ++j) {
            isolated_total +=
                ksk_hit_ratio_for_domain(&isolated_nodes[j], docs, doc_count, score->domain);
            swarm_total +=
                ksk_hit_ratio_for_domain(&swarm_nodes[j], docs, doc_count, score->domain);
        }
        score->isolated_hit_ratio = isolated_total / (double)KSK_NODE_COUNT;
        score->swarm_hit_ratio = swarm_total / (double)KSK_NODE_COUNT;
        score->swarm_vs_single_delta =
            score->swarm_hit_ratio - score->single_hit_ratio;
        score->swarm_vs_isolated_delta =
            score->swarm_hit_ratio - score->isolated_hit_ratio;
        report->domain_score_count++;
    }
}

static void ksk_write_domain_scores_json(FILE *out, const KskReport *report) {
    fprintf(out, "\"domain_scores\":[");
    for (size_t i = 0; i < report->domain_score_count; ++i) {
        const KskDomainScore *score = &report->domain_scores[i];
        fprintf(out,
                "%s{\"domain\":\"%s\",\"documents\":%zu,"
                "\"single_hit_ratio\":%.6f,\"isolated_hit_ratio\":%.6f,"
                "\"swarm_hit_ratio\":%.6f,\"swarm_vs_single_delta\":%.6f,"
                "\"swarm_vs_isolated_delta\":%.6f}",
                i == 0U ? "" : ",", score->domain, score->documents,
                score->single_hit_ratio, score->isolated_hit_ratio,
                score->swarm_hit_ratio, score->swarm_vs_single_delta,
                score->swarm_vs_isolated_delta);
    }
    fprintf(out, "]");
}

static int ksk_write_report_json(const char *path, const KskReport *report) {
    FILE *out = NULL;
    if (!path || !report) {
        return -1;
    }
    out = fopen(path, "w");
    if (!out) {
        return -1;
    }
    fprintf(out, "{");
    fprintf(out, "\"timestamp\":%lld,", (long long)report->finished_at);
    fprintf(out, "\"node_count\":%u,", KSK_NODE_COUNT);
    fprintf(out, "\"rounds\":%u,", KSK_ROUNDS);
    fprintf(out, "\"total_documents\":%zu,", report->total_documents);
    fprintf(out, "\"single\":{\"hit_ratio\":%.6f},", report->single_hit_ratio);
    ksk_write_cluster_json(out, "isolated_final", report->isolated_final);
    fprintf(out, ",");
    ksk_write_cluster_json(out, "swarm_final", report->swarm_final);
    fprintf(out,
            ",\"comparison\":{\"swarm_vs_single_delta\":%.6f,"
            "\"swarm_vs_isolated_delta\":%.6f},",
            report->swarm_vs_single_delta, report->swarm_vs_isolated_delta);
    ksk_write_domain_scores_json(out, report);
    fprintf(out, ",");
    ksk_write_rounds_json(out, "isolated_rounds", report->isolated_rounds,
                          KSK_ROUNDS);
    fprintf(out, ",");
    ksk_write_rounds_json(out, "swarm_rounds", report->swarm_rounds,
                          KSK_ROUNDS);
    fprintf(out, "}\n");
    fclose(out);
    return 0;
}

static void ksk_options_default(KskOptions *options) {
    if (!options) {
        return;
    }
    memset(options, 0, sizeof(*options));
    options->docs_root = "data/formula_domains";
    options->interval_sec = 300U;
}

static int ksk_parse_options(int argc, char **argv, KskOptions *options) {
    ksk_options_default(options);
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--json-out") == 0) {
            if (i + 1 >= argc) {
                return -1;
            }
            options->json_out = argv[++i];
        } else if (strcmp(argv[i], "--docs-root") == 0) {
            if (i + 1 >= argc) {
                return -1;
            }
            options->docs_root = argv[++i];
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

static int ksk_run_once(const KskOptions *options, KskReport *report) {
    KskDocument docs[KSK_MAX_DOCS];
    KskNode *single_node = NULL;
    KskNode *isolated = NULL;
    KskNode *swarm = NULL;
    size_t doc_count = 0U;
    if (!options || !report) {
        return 1;
    }
    memset(report, 0, sizeof(*report));
    memset(docs, 0, sizeof(docs));
    ksk_collect_documents(options->docs_root, options->docs_root, docs, &doc_count,
                          KSK_MAX_DOCS);
    if (doc_count == 0U) {
        fprintf(stderr, "no knowledge documents found in %s\n", options->docs_root);
        return 1;
    }
    isolated = (KskNode *)calloc(KSK_NODE_COUNT, sizeof(KskNode));
    swarm = (KskNode *)calloc(KSK_NODE_COUNT, sizeof(KskNode));
    single_node = (KskNode *)calloc(1U, sizeof(KskNode));
    if (!isolated || !swarm || !single_node) {
        free(single_node);
        free(isolated);
        free(swarm);
        return 1;
    }

    printf("Kolibri Swarm Knowledge Benchmark\n");
    printf("documents=%zu nodes=%u rounds=%u\n", doc_count, KSK_NODE_COUNT,
           KSK_ROUNDS);

    ksk_init_pool(single_node, 0xABCDEFULL);
    for (size_t round = 0U, next_doc = 0U; round < KSK_ROUNDS; ++round) {
        for (size_t i = 0; i < KSK_SINGLE_DOCS_PER_ROUND && next_doc < doc_count;
             ++i, ++next_doc) {
            ksk_ingest_document(single_node, &docs[next_doc]);
        }
    }
    report->single_hit_ratio = ksk_hit_ratio(single_node, docs, doc_count);
    printf("[knowledge-single] hit=%.3f\n", report->single_hit_ratio);

    ksk_init_plain_nodes(isolated, KSK_NODE_COUNT);
    ksk_run_isolated(isolated, docs, doc_count, report);
    report->isolated_final =
        ksk_capture_cluster_score(isolated, KSK_NODE_COUNT, docs, doc_count);
    printf("[knowledge-isolated-final] avg_hit=%.3f best_hit=%.3f\n",
           report->isolated_final.hit_ratio,
           report->isolated_final.best_hit_ratio);

    if (ksk_run_swarm(swarm, docs, doc_count, report) != 0) {
        ksk_free_node_pool(single_node);
        free(single_node);
        ksk_free_plain_nodes(isolated, KSK_NODE_COUNT);
        free(isolated);
        free(swarm);
        return 1;
    }
    report->swarm_final =
        ksk_capture_cluster_score(swarm, KSK_NODE_COUNT, docs, doc_count);
    report->swarm_vs_single_delta =
        report->swarm_final.hit_ratio - report->single_hit_ratio;
    report->swarm_vs_isolated_delta =
        report->swarm_final.hit_ratio - report->isolated_final.hit_ratio;
    report->total_documents = doc_count;
    ksk_capture_domain_scores(report, single_node, isolated, swarm, docs,
                              doc_count);
    report->finished_at = time(NULL);
    printf("[knowledge-swarm-final] avg_hit=%.3f best_hit=%.3f imports=%zu\n",
           report->swarm_final.hit_ratio, report->swarm_final.best_hit_ratio,
           report->swarm_final.imported_total);
    printf("[knowledge-comparison] swarm_vs_single=%.3f swarm_vs_isolated=%.3f\n",
           report->swarm_vs_single_delta, report->swarm_vs_isolated_delta);

    ksk_stop_swarm_nodes(swarm, KSK_NODE_COUNT);
    ksk_free_plain_nodes(isolated, KSK_NODE_COUNT);
    ksk_free_node_pool(single_node);
    free(single_node);
    free(isolated);
    free(swarm);

    if (options->json_out) {
        (void)ksk_write_report_json(options->json_out, report);
    }
    return 0;
}

int main(int argc, char **argv) {
    KskOptions options;
    KskReport report;
    setvbuf(stdout, NULL, _IONBF, 0);
    if (ksk_parse_options(argc, argv, &options) != 0) {
        fprintf(stderr,
                "usage: %s [--json-out PATH] [--docs-root PATH] [--loop] [--interval-sec SEC]\n",
                argv[0]);
        return 1;
    }
    do {
        if (ksk_run_once(&options, &report) != 0) {
            return 1;
        }
        if (!options.loop_forever) {
            break;
        }
        sleep(options.interval_sec);
    } while (1);
    return 0;
}
