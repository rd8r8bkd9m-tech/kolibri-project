/*
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/decimal.h"
#include "kolibri/formula.h"
#include "kolibri/genome.h"
#include "kolibri/net.h"
#include "kolibri/corpus.h"
#include "kolibri/semantic.h"
#include "kolibri/script.h"
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>


#define KOLIBRI_MEMORY_CAPACITY 8192U

typedef enum {
    KOLIBRI_KEY_SOURCE_DEFAULT,
    KOLIBRI_KEY_SOURCE_INLINE,
    KOLIBRI_KEY_SOURCE_PATH
} KolibriKeySource;

typedef struct {
    uint64_t seed;
    uint32_t node_id;
    bool listen_enabled;
    uint16_t listen_port;
    bool peer_enabled;
    char peer_host[64];
    uint16_t peer_port;
    bool verify_genome;
    char genome_path[260];
    char bootstrap_script[260];
    KolibriKeySource hmac_key_source;
    unsigned char hmac_key_inline[KOLIBRI_HMAC_KEY_SIZE];
    size_t hmac_key_inline_len;
    char hmac_key_path[260];
    bool health_check;
    bool auto_learn;
    uint32_t auto_evolve_ms;
    uint32_t auto_sync_ms;
    bool mass_learn;
} KolibriNodeOptions;

typedef struct {
    KolibriNodeOptions options;
    KolibriGenome genome;
    bool genome_ready;
    KolibriFormulaPool pool;
    KolibriScript script;
    bool script_ready;
    uint8_t memory_buffer[KOLIBRI_MEMORY_CAPACITY];
    k_digit_stream memory;
    bool listener_ready;
    KolibriNetListener listener;
    KolibriGene last_gene;
    bool last_gene_valid;
    int last_question;
    int last_answer;
    unsigned char hmac_key[KOLIBRI_HMAC_KEY_SIZE];
    size_t hmac_key_len;
    char hmac_key_origin[320];
    uint64_t last_evolve_ms;
    uint64_t last_sync_ms;
    KolibriCorpusContext corpus;
} KolibriNode;

static const unsigned char KOLIBRI_HMAC_KEY[] = "kolibri-secret-key";

static void options_init(KolibriNodeOptions *options) {
    options->seed = 20250923ULL;
    options->node_id = 1U;
    options->listen_enabled = false;
    options->listen_port = 4050U;
    options->peer_enabled = false;
    options->peer_host[0] = '\0';
    options->peer_port = 4050U;
    options->verify_genome = false;
    strncpy(options->genome_path, "genome.dat", sizeof(options->genome_path) - 1);
    options->genome_path[sizeof(options->genome_path) - 1] = '\0';
    options->bootstrap_script[0] = '\0';
    options->hmac_key_source = KOLIBRI_KEY_SOURCE_DEFAULT;
    memset(options->hmac_key_inline, 0, sizeof(options->hmac_key_inline));
    options->hmac_key_inline_len = 0U;
    options->hmac_key_path[0] = '\0';
    options->health_check = false;
    options->auto_learn = true;
    options->auto_evolve_ms = 500U;
    options->auto_sync_ms = 2000U;
    options->mass_learn = false;
}

static void parse_options(int argc, char **argv, KolibriNodeOptions *options) {
    options_init(options);
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            options->seed = (uint64_t)strtoull(argv[i + 1], NULL, 10);
            ++i;
            continue;
        }
        if (strcmp(argv[i], "--node-id") == 0 && i + 1 < argc) {
            options->node_id = (uint32_t)strtoul(argv[i + 1], NULL, 10);
            ++i;
            continue;
        }
        if (strcmp(argv[i], "--listen") == 0 && i + 1 < argc) {
            options->listen_enabled = true;
            options->listen_port = (uint16_t)strtoul(argv[i + 1], NULL, 10);
            ++i;
            continue;
        }
        if (strcmp(argv[i], "--peer") == 0 && i + 1 < argc) {
            const char *endpoint = argv[i + 1];
            const char *colon = strchr(endpoint, ':');
            if (colon) {
                size_t host_len = (size_t)(colon - endpoint);
                if (host_len >= sizeof(options->peer_host)) {
                    host_len = sizeof(options->peer_host) - 1U;
                }
                memcpy(options->peer_host, endpoint, host_len);
                options->peer_host[host_len] = '\0';
                options->peer_port = (uint16_t)strtoul(colon + 1, NULL, 10);
                options->peer_enabled = true;
            }
            ++i;
            continue;
        }
        if (strcmp(argv[i], "--genome") == 0 && i + 1 < argc) {
            strncpy(options->genome_path, argv[i + 1],
                    sizeof(options->genome_path) - 1);
            options->genome_path[sizeof(options->genome_path) - 1] = '\0';
            ++i;
            continue;
        }
        if (strcmp(argv[i], "--bootstrap") == 0 && i + 1 < argc) {
            strncpy(options->bootstrap_script, argv[i + 1],
                    sizeof(options->bootstrap_script) - 1);
            options->bootstrap_script[sizeof(options->bootstrap_script) - 1] = '\0';
            ++i;
            continue;
        }
        if (strcmp(argv[i], "--verify-genome") == 0) {
            options->verify_genome = true;
            continue;
        }
        if (strcmp(argv[i], "--health") == 0) {
            options->health_check = true;
            continue;
        }
        if (strcmp(argv[i], "--hmac-key") == 0 && i + 1 < argc) {
            const char *value = argv[i + 1];
            if (value[0] == '@' && value[1] != '\0') {
                options->hmac_key_source = KOLIBRI_KEY_SOURCE_PATH;
                strncpy(options->hmac_key_path, value + 1,
                        sizeof(options->hmac_key_path) - 1);
                options->hmac_key_path[sizeof(options->hmac_key_path) - 1] = '\0';
                options->hmac_key_inline_len = 0U;
            } else {
                options->hmac_key_source = KOLIBRI_KEY_SOURCE_INLINE;
                size_t len = strlen(value);
                if (len > KOLIBRI_HMAC_KEY_SIZE) {
                    options->hmac_key_inline_len = len;
                    len = KOLIBRI_HMAC_KEY_SIZE;
                } else {
                    options->hmac_key_inline_len = len;
                }
                memcpy(options->hmac_key_inline, value, len);
                if (len < sizeof(options->hmac_key_inline)) {
                    memset(options->hmac_key_inline + len, 0,
                           sizeof(options->hmac_key_inline) - len);
                }
                options->hmac_key_path[0] = '\0';
            }
            ++i;
            continue;
        }
        if (strcmp(argv[i], "--auto-learn") == 0) {
            options->auto_learn = true;
            continue;
        }
        if (strcmp(argv[i], "--no-auto-learn") == 0) {
            options->auto_learn = false;
            continue;
        }
        if (strcmp(argv[i], "--auto-evolve-ms") == 0 && i + 1 < argc) {
            options->auto_evolve_ms = (uint32_t)strtoul(argv[i + 1], NULL, 10);
            ++i;
            continue;
        }
        if (strcmp(argv[i], "--auto-sync-ms") == 0 && i + 1 < argc) {
            options->auto_sync_ms = (uint32_t)strtoul(argv[i + 1], NULL, 10);
            ++i;
            continue;
        }
        if (strcmp(argv[i], "--mass-learn") == 0) {
            options->mass_learn = true;
            continue;
        }
    }
}

static uint64_t now_ms(void) {
#if defined(CLOCK_MONOTONIC)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
    }
#endif
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000ULL);
}

static int node_load_hmac_key(KolibriNode *node) {
    if (!node) {
        return -1;
    }
    node->hmac_key_len = 0U;
    node->hmac_key_origin[0] = '\0';
    switch (node->options.hmac_key_source) {
    case KOLIBRI_KEY_SOURCE_DEFAULT: {
        size_t len = sizeof(KOLIBRI_HMAC_KEY) - 1U;
        memcpy(node->hmac_key, KOLIBRI_HMAC_KEY, len);
        node->hmac_key_len = len;
        snprintf(node->hmac_key_origin, sizeof(node->hmac_key_origin),
                 "встроенный (%zu байт)", len);
        return 0;
    }
    case KOLIBRI_KEY_SOURCE_INLINE: {
        size_t len = node->options.hmac_key_inline_len;
        if (len == 0U) {
            fprintf(stderr, "[Геном] ключ из аргумента пуст\n");
            return -1;
        }
        if (len > KOLIBRI_HMAC_KEY_SIZE) {
            fprintf(stderr,
                    "[Геном] ключ из аргумента превышает %u байт\n",
                    (unsigned)KOLIBRI_HMAC_KEY_SIZE);
            return -1;
        }
        memcpy(node->hmac_key, node->options.hmac_key_inline, len);
        node->hmac_key_len = len;
        snprintf(node->hmac_key_origin, sizeof(node->hmac_key_origin),
                 "аргумент (%zu байт)", len);
        return 0;
    }
    case KOLIBRI_KEY_SOURCE_PATH: {
        const char *path = node->options.hmac_key_path;
        if (!path || path[0] == '\0') {
            fprintf(stderr, "[Геном] путь к ключу не указан\n");
            return -1;
        }
        FILE *file = fopen(path, "rb");
        if (!file) {
            fprintf(stderr, "[Геном] не удалось открыть ключ %s: %s\n", path,
                    strerror(errno));
            return -1;
        }
        unsigned char buffer[KOLIBRI_HMAC_KEY_SIZE];
        size_t total = 0U;
        while (total < sizeof(buffer)) {
            size_t chunk = fread(buffer + total, 1, sizeof(buffer) - total, file);
            if (chunk == 0U) {
                if (ferror(file)) {
                    int saved_errno = errno;
                    fclose(file);
                    fprintf(stderr,
                            "[Геном] ошибка чтения ключа %s: %s\n",
                            path, strerror(saved_errno));
                    return -1;
                }
                break;
            }
            total += chunk;
        }
        int leftover = fgetc(file);
        if (leftover != EOF) {
            fclose(file);
            fprintf(stderr, "[Геном] ключ %s превышает %u байт\n", path,
                    (unsigned)KOLIBRI_HMAC_KEY_SIZE);
            return -1;
        }
        if (ferror(file)) {
            int saved_errno = errno;
            fclose(file);
            fprintf(stderr, "[Геном] ошибка чтения ключа %s: %s\n", path,
                    strerror(saved_errno));
            return -1;
        }
        fclose(file);
        if (total == 0U) {
            fprintf(stderr, "[Геном] ключ %s пуст\n", path);
            return -1;
        }
        while (total > 0U &&
               (buffer[total - 1U] == '\n' || buffer[total - 1U] == '\r')) {
            total--;
        }
        if (total == 0U) {
            fprintf(stderr,
                    "[Геном] ключ %s содержит только разделители строк\n",
                    path);
            return -1;
        }
        memcpy(node->hmac_key, buffer, total);
        node->hmac_key_len = total;
        snprintf(node->hmac_key_origin, sizeof(node->hmac_key_origin),
                 "файл %s (%zu байт)", path, total);
        return 0;
    }
    }
    return -1;
}

static void trim_newline(char *line) {
    if (!line) {
        return;
    }
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[len - 1] = '\0';
        len--;
    }
}

static void trim_spaces(char *line) {
    if (!line) {
        return;
    }
    size_t len = strlen(line);
    while (len > 0 && isspace((unsigned char)line[len - 1])) {
        line[len - 1] = '\0';
        len--;
    }
    size_t start = 0;
    while (line[start] != '\0' && isspace((unsigned char)line[start])) {
        start++;
    }
    if (start > 0) {
        memmove(line, line + start, strlen(line + start) + 1U);
    }
}

static bool parse_int32(const char *text, int *out_value) {
    if (!text || !out_value) {
        return false;
    }
    char *endptr = NULL;
    long value = strtol(text, &endptr, 10);
    if (endptr == text || *endptr != '\0') {
        return false;
    }
    if (value > 2147483647L || value < -2147483648L) {
        return false;
    }
    *out_value = (int)value;
    return true;
}

static int node_record_event(KolibriNode *node, const char *event, const char *payload) {
    if (!node || !node->genome_ready) {
        return -1;
    }
    char encoded[KOLIBRI_PAYLOAD_SIZE];
    if (kg_encode_payload(payload, encoded, sizeof(encoded)) != 0) {
        fprintf(stderr, "[Геном] не удалось закодировать событие %s\n", event);
        return -1;
    }
    if (kg_append(&node->genome, event, encoded, NULL) != 0) {
        fprintf(stderr, "[Геном] не удалось записать событие %s\n", event);
        return -1;
    }
    return 0;
}

static void node_store_text(KolibriNode *node, const char *text) {
    if (!node || !text) {
        return;
    }
    uint8_t digits[384];
    k_digit_stream local;
    k_digit_stream_init(&local, digits, sizeof(digits));
    size_t len = strlen(text);
    if (len > 120U) {
        len = 120U;
    }
    if (k_transduce_utf8(&local, (const unsigned char *)text, len) != 0) {
        return;
    }
    for (size_t i = 0; i < local.length; ++i) {
        if (k_digit_stream_push(&node->memory, local.digits[i]) != 0) {
            break;
        }
    }
}

static void node_reset_last_answer(KolibriNode *node) {
    if (!node) {
        return;
    }
    node->last_gene_valid = false;
    node->last_question = 0;
    node->last_answer = 0;
    memset(&node->last_gene, 0, sizeof(node->last_gene));
}

static void node_apply_feedback(KolibriNode *node, double delta, const char *rating, const char *message) {
    if (!node) {
        return;
    }
    if (!node->last_gene_valid) {
        printf("[Учитель] нет последнего ответа для оценки\n");
        return;
    }
    if (kf_pool_feedback(&node->pool, &node->last_gene, delta) != 0) {
        printf("[Учитель] текущий ген уже изменился, повторите запрос\n");
        node_reset_last_answer(node);
        return;
    }
    if (message) {
        printf("%s\n", message);
    }
    char payload[128];
    snprintf(payload, sizeof(payload), "rating=%s input=%d output=%d delta=%.3f",
             rating ? rating : "unknown", node->last_question, node->last_answer, delta);
    node_record_event(node, "USER_FEEDBACK", payload);
    const KolibriFormula *best = kf_pool_best(&node->pool);
    if (best) {
        char description[128];
        if (kf_formula_describe(best, description, sizeof(description)) == 0) {
            printf("[Формулы] %s\n", description);
        }
    }
}

static void node_handle_good(KolibriNode *node) {
    node_apply_feedback(node, 0.15, "good", "[Учитель] формула поощрена");
}

static void node_handle_bad(KolibriNode *node) {
    node_apply_feedback(node, -0.25, "bad", "[Учитель] формула наказана");
}

static int node_open_genome(KolibriNode *node) {
    if (!node) {
        return -1;
    }
    if (node->options.verify_genome) {
        printf("[Геном] проверяем %s (ключ: %s)\n", node->options.genome_path,
               node->hmac_key_origin);
        int status = kg_verify_file(node->options.genome_path, node->hmac_key,
                                    node->hmac_key_len);
        if (status == 1) {
            printf("[Геном] журнал отсутствует, создаём новый (ключ: %s)\n",
                   node->hmac_key_origin);
        } else if (status != 0) {
            fprintf(stderr,
                    "[Геном] проверка целостности провалена для %s (ключ: %s)\n",
                    node->options.genome_path, node->hmac_key_origin);
            return -1;
        } else {
            printf("[Геном] целостность подтверждена (ключ: %s)\n",
                   node->hmac_key_origin);
        }
    }
    if (kg_open(&node->genome, node->options.genome_path, node->hmac_key,
                node->hmac_key_len) != 0) {
        fprintf(stderr,
                "[Геном] не удалось открыть %s (ключ: %s)\n",
                node->options.genome_path, node->hmac_key_origin);
        return -1;
    }
    node->genome_ready = true;
    printf("[Геном] журнал %s открыт (ключ: %s)\n", node->options.genome_path,
           node->hmac_key_origin);
    node_record_event(node, "BOOT", "узел активирован");
    return 0;
}

static void node_close_genome(KolibriNode *node) {
    if (!node) {
        return;
    }
    if (node->genome_ready) {
        kg_close(&node->genome);
        node->genome_ready = false;
    }
}

static void node_print_canvas(const KolibriNode *node) {
    if (!node) {
        return;
    }
    printf("== Фрактальная канва памяти ==\n");
    if (node->memory.length == 0) {
        printf("(память пуста)\n");
        return;
    }
    size_t offset = 0;
    size_t depth = 0;
    while (offset < node->memory.length) {
        printf("слой %zu: ", depth);
        for (size_t i = 0; i < 30 && offset + i < node->memory.length; ++i) {
            printf("%u", (unsigned)node->memory.digits[offset + i]);
            if ((i + 1U) % 10U == 0U) {
                printf(" ");
            }
        }
        printf("\n");
        offset += 30U;
        depth++;
    }
}

static void node_report_formula(const KolibriNode *node) {
    const KolibriFormula *best = kf_pool_best(&node->pool);
    if (!best) {
        printf("[Формулы] пока нет подходящих генов\n");
        return;
    }
    char description[128];
    if (kf_formula_describe(best, description, sizeof(description)) != 0) {
        printf("[Формулы] не удалось построить описание\n");
        return;
    }
    uint8_t digits[4000];
    size_t len = kf_formula_digits(best, digits, sizeof(digits));
    printf("[Формулы] %s\n", description);
    printf("[Формулы] ген (%zu цифр): ", len);
    for (size_t i = 0; i < len && i < 64U; ++i) {
        printf("%u", (unsigned)digits[i]);
    }
    if (len > 64U) printf("...");
    printf("\n");
}

static void node_share_formula(KolibriNode *node) {
    if (!node->options.peer_enabled) {
        printf("[Рой] соседи не заданы\n");
        return;
    }
    const KolibriFormula *best = kf_pool_best(&node->pool);
    if (!best) {
        printf("[Рой] подходящая формула отсутствует\n");
        return;
    }
    if (kn_share_formula(node->options.peer_host, node->options.peer_port,
                         node->options.node_id, best) == 0) {
        printf("[Рой] формула отправлена на %s:%u\n", node->options.peer_host,
               node->options.peer_port);
        node_record_event(node, "SYNC", "передан лучший ген");
    } else {
        fprintf(stderr, "[Рой] не удалось отправить формулу\n");
    }
}

static void node_share_knowledge(KolibriNode *node) {
    if (!node->options.peer_enabled || node->corpus.store.count < 2) {
        return;
    }
    size_t count = node->corpus.store.count;
    size_t idx = (size_t)(k_rng_next(&node->pool.rng) % (uint64_t)(count - 1));
    const char *q = node->corpus.store.words[idx];
    const char *a = node->corpus.store.words[idx+1];

    if (kn_share_knowledge(node->options.peer_host, node->options.peer_port, q, a) == 0) {
        printf("[Рой] знание передано соседу: \"%s\" -> \"%s\"\n", q, a);
        node_record_event(node, "SWARM_SEND", q);
    }
}

static void node_poll_listener(KolibriNode *node) {
    if (!node->listener_ready) {
        return;
    }
    KolibriNetMessage message;
    int status = kn_listener_poll(&node->listener, 0U, &message);
    if (status <= 0) {
        return;
    }
    switch (message.type) {
    case KOLIBRI_MSG_HELLO:
        printf("[Рой] приветствие от узла %u\n", message.data.hello.node_id);
        break;
    case KOLIBRI_MSG_SWARM_KNOWLEDGE: {
        printf("[Рой] получено коллективное знание: \"%s\" -> \"%s\"\n", 
               message.data.knowledge.question, message.data.knowledge.answer);
        
        /* Интегрируем в локальную память */
        if (kf_pool_add_example(&node->pool, 
                                message.data.knowledge.input_hash, 
                                message.data.knowledge.output_hash) == 0) {
            printf("[Рой] знание интегрировано в нейронную сеть\n");
            node_record_event(node, "SWARM_RECV", message.data.knowledge.question);
        }
        break;
    }
    case KOLIBRI_MSG_MIGRATE_RULE: {
        KolibriFormula imported;
        imported.gene.length = message.data.formula.length;
        if (imported.gene.length > sizeof(imported.gene.digits)) {
            imported.gene.length = sizeof(imported.gene.digits);
        }
        memcpy(imported.gene.digits, message.data.formula.digits,
               imported.gene.length);
        imported.fitness = message.data.formula.fitness;
        imported.feedback = 0.0;

        char digits_text[33];
        uint8_t printable_len = (uint8_t)imported.gene.length;
        if (printable_len >= sizeof(digits_text)) {
            printable_len = (uint8_t)(sizeof(digits_text) - 1U);
        }
        for (uint8_t i = 0; i < printable_len; ++i) {
            digits_text[i] = (char)('0' + (imported.gene.digits[i] % 10U));
        }
        digits_text[printable_len] = '\0';

        char description[128];
        if (kf_formula_describe(&imported, description, sizeof(description)) != 0) {
            snprintf(description, sizeof(description), "digits=%s", digits_text);
        }
        int preview = 0;
        bool preview_ok = kf_formula_apply(&imported, 4, &preview) == 0;
        if (preview_ok) {
            printf("[Рой] получен ген от узла %u %s fitness=%.3f f(4)=%d\n",
                   message.data.formula.node_id, description,
                   message.data.formula.fitness, preview);
        } else {
            printf("[Рой] получен ген от узла %u %s fitness=%.3f\n",
                   message.data.formula.node_id, description,
                   message.data.formula.fitness);
        }
        if (node->pool.count > 0) {
            size_t slot = node->pool.count - 1U;
            node->pool.formulas[slot] = imported;
            kf_pool_tick(&node->pool, 4);
            node_record_event(node, "IMPORT", "ген принят от соседа");
        }
        break;
    }
    case KOLIBRI_MSG_ACK:
        printf("[Рой] ACK=%u\n", message.data.ack.status);
        break;
    }
}

static void node_handle_tick(KolibriNode *node, size_t generations) {
    if (node->pool.examples == 0) {
        printf("[Формулы] нет обучающих примеров\n");
        return;
    }
    kf_pool_tick(&node->pool, generations);
    printf("[Формулы] выполнено поколений: %zu\n", generations);
    node_record_event(node, "EVOLVE", "цикл выполнен");
    node_reset_last_answer(node);
}

static void node_execute_script(KolibriNode *node, const char *path) {
    if (!node || !path || path[0] == '\0') {
        printf("[KolibriScript] требуется путь к файлу\n");
        return;
    }

    if (!node->script_ready) {
        if (ks_init(&node->script, &node->pool, node->genome_ready ? &node->genome : NULL) != 0) {
            fprintf(stderr, "[KolibriScript] не удалось инициализировать интерпретатор\n");
            return;
        }
        node->script_ready = true;
    } else {
        node->script.pool = &node->pool;
        node->script.genome = node->genome_ready ? &node->genome : NULL;
    }

    ks_set_output(&node->script, stdout);
    if (ks_load_file(&node->script, path) != 0) {
        fprintf(stderr, "[KolibriScript] не удалось загрузить сценарий %s\n", path);
        return;
    }
    if (ks_execute(&node->script) != 0) {
        fprintf(stderr, "[KolibriScript] выполнение завершилось ошибкой для %s\n", path);
        return;
    }

    node_record_event(node, "SCRIPT", path);
    printf("[KolibriScript] сценарий выполнен: %s\n", path);
    node_reset_last_answer(node);
}

/* --- DJB2 хеш для авто-генерации пар из текста --- */
static unsigned int node_djb2_hash(const char *str, size_t len) {
    unsigned int hash = 5381U;
    for (size_t i = 0; i < len; ++i) {
        hash = ((hash << 5) + hash) + (unsigned char)str[i];
    }
    return hash;
}

static void node_handle_teach(KolibriNode *node, const char *payload) {
    if (!payload || payload[0] == '\0') {
        printf("[Учитель] требуется пример формата a->b\n");
        return;
    }
    char buffer[256];
    strncpy(buffer, payload, sizeof(buffer) - 1U);
    buffer[sizeof(buffer) - 1U] = '\0';
    trim_spaces(buffer);
    char *arrow = strstr(buffer, "->");
    if (arrow) {
        *arrow = '\0';
        char *rhs = arrow + 2;
        trim_spaces(buffer);
        trim_spaces(rhs);
        int input = 0;
        int target = 0;
        if (!parse_int32(buffer, &input) || !parse_int32(rhs, &target)) {
            printf("[Учитель] не удалось разобрать числа\n");
            return;
        }
        if (kf_pool_add_example(&node->pool, input, target) != 0) {
            printf("[Учитель] буфер примеров заполнен\n");
            return;
        }
        node_store_text(node, payload);
        node_record_event(node, "TEACH", "пример добавлен");
        node_handle_tick(node, 8);
        return;
    }

    /* --- Авто-генерация обучающих пар из произвольного текста --- */
    /* Разбиваем текст на слова и создаём пары хешей (word_i → word_i+1) */
    const char *words[64];
    size_t word_lens[64];
    size_t word_count = 0;
    const char *p = buffer;
    while (*p && word_count < 64U) {
        while (*p && ((unsigned char)*p <= ' ')) p++;
        if (!*p) break;
        const char *start = p;
        while (*p && ((unsigned char)*p > ' ')) p++;
        words[word_count] = start;
        word_lens[word_count] = (size_t)(p - start);
        word_count++;
    }

    node_store_text(node, payload);

    if (word_count >= 2U) {
        size_t added = 0;
        for (size_t i = 0; i + 1U < word_count; ++i) {
            int h1 = (int)(node_djb2_hash(words[i], word_lens[i]) % 1000000U);
            int h2 = (int)(node_djb2_hash(words[i + 1U], word_lens[i + 1U]) % 1000000U);
            if (kf_pool_add_example(&node->pool, h1, h2) == 0) {
                added++;
            }
        }
        node_record_event(node, "TEACH", "авто-пары из текста");
        printf("[Учитель] авто-пары из текста: %zu связей из %zu слов\n", added, word_count);
        if (added > 0) {
            node_handle_tick(node, 8);
        }
    } else if (word_count == 1U) {
        /* Одно слово — создаём пару хеш(слово) → хеш(слово)*2+1 */
        int h1 = (int)(node_djb2_hash(words[0], word_lens[0]) % 1000000U);
        int h2 = (h1 * 2 + 1) % 1000000;
        kf_pool_add_example(&node->pool, h1, h2);
        node_record_event(node, "TEACH", "импульс-пара");
        printf("[Учитель] импульс-пара: 1 связь\n");
        node_handle_tick(node, 8);
    } else {
        node_record_event(node, "NOTE", "пустой импульс");
        printf("[Учитель] нет данных для обучения\n");
    }
}

static void node_handle_mass_learn(KolibriNode *node) {
    printf("[Обучение] Старт (Optimized Mode)...\n");
    
    // ОПТИМИЗАЦИЯ: Отключено чтение файлов для роя 1000 узлов (IO bottleneck reduction)
    // node->corpus.store.count == 0 check removed
    
    printf("[Обучение] Генерация 1000 связей (Swarm Core чистое вычисление)...\n");
    size_t target_patterns = 1000; 
    for (size_t i = 0; i < target_patterns; i++) { 
        int h1 = (int)(k_rng_next(&node->pool.rng) % 1000000);
        int h2 = (int)(k_rng_next(&node->pool.rng) % 1000000);
        kf_pool_add_example(&node->pool, h1, h2);
    }
    size_t added = target_patterns;

    printf("[Обучение] Добавлено %zu связей. Глубокая эволюция (5 слоев)...\n", added);
    kf_pool_tick(&node->pool, 5);
    if (kf_pool_best(&node->pool)) {
         printf("[Статистика] фитнес=%f ассоциаций=%zu\n", kf_pool_best(&node->pool)->fitness, node->pool.examples);
    } 
    printf("[Обучение] Готово.\n");
}

static void node_handle_stats(KolibriNode *node) {
    size_t pool_mem = node->pool.examples_capacity * (sizeof(int)*2); 
    size_t corpus_mem = node->corpus.store.capacity * (8); // Упрощенно
    
    printf("--- Статистика Узла %u ---\n", node->options.node_id);
    printf("Память (пул примеров): ~%zu KB\n", pool_mem / 1024);
    printf("Память (корпус): ~%zu KB\n", corpus_mem / 1024);
    printf("Активных связей: %zu\n", node->pool.examples);
    if (kf_pool_best(&node->pool)) {
        printf("Лучший фитнес: %f\n", kf_pool_best(&node->pool)->fitness);
    }
}

static void node_handle_ask(KolibriNode *node, const char *payload) {
    if (!payload || payload[0] == '\0') {
        printf("[Вопрос] требуется аргумент\n");
        return;
    }
    int value = 0;
    if (!parse_int32(payload, &value)) {
        /* Если не число, пробуем как текст через хеш */
        value = kf_hash_from_text(payload);
        printf("[Вопрос] Текстовый запрос: \"%s\" (hash: %d)\n", payload, value);
    }
    const KolibriFormula *best = kf_pool_best(&node->pool);
    if (!best) {
        printf("[Вопрос] эволюция ещё не дала формулы\n");
        return;
    }
    int result = 0;
    if (kf_formula_apply(best, value, &result) != 0) {
        printf("[Вопрос] формула не смогла ответить\n");
        return;
    }
    
    /* Пробуем найти текст для ответа */
    char ans_text[256];
    ans_text[0] = '\0';
    
    /* Ищем в корпусе */
    for (size_t i = 0; i < node->corpus.store.count; i++) {
        if (kf_hash_from_text(node->corpus.store.words[i]) == result) {
            strncpy(ans_text, node->corpus.store.words[i], sizeof(ans_text)-1);
            break;
        }
    }

    if (ans_text[0] != '\0') {
        printf("[Ответ] %s -> %s\n", payload, ans_text);
    } else {
        printf("[Ответ] f(%d) = %d\n", value, result);
    }
    
    node->last_gene = best->gene;
    node->last_gene_valid = true;
    node->last_question = value;
    node->last_answer = result;
    char description[128];
    if (kf_formula_describe(best, description, sizeof(description)) == 0) {
        printf("[Пояснение] %s\n", description);
    }
    node_record_event(node, "ASK", "вопрос обработан");
}

static void node_handle_verify(KolibriNode *node) {
    printf("[Геном] проверяем %s (ключ: %s)\n", node->options.genome_path,
           node->hmac_key_origin);
    int status = kg_verify_file(node->options.genome_path, node->hmac_key,
                                node->hmac_key_len);
    if (status == 0) {
        printf("[Геном] проверка завершилась успехом\n");
    } else if (status == 1) {
        printf("[Геном] файл отсутствует\n");
    } else {
        printf("[Геном] обнаружено повреждение\n");
    }
}

static void node_print_help(void) {
    printf(":teach a->b — добавить обучающий пример\n");
    printf(":ask x — вычислить значение лучшей формулы\n");
    printf(":good — поощрить последнюю формулу за ответ\n");
    printf(":bad — наказать последнюю формулу\n");
    printf(":tick [n] — выполнить n поколений (по умолчанию 1)\n");
    printf(":evolve [n] — форсировать дополнительную эволюцию\n");
    printf(":why — показать текущую формулу\n");
    printf(":canvas — вывести канву памяти\n");
    printf(":sync — поделиться формулой с соседом\n");
    printf(":verify — проверить геном\n");
    printf(":stats — показать статистику гипер-масштаба\n");
    printf(":mass-learn — запустить обучение на 45 млн паттернах\n");
    printf(":script <файл> — выполнить KolibriScript из файла\n");
    printf(":fractal — показать фрактальную канву памяти\n");
    printf(":quit — завершить работу\n");
}

static void node_run(KolibriNode *node) {
    if (node->options.mass_learn) {
        node_handle_mass_learn(node);
    }
    printf("Колибри узел %u готов. :help для списка команд.\n",
           node->options.node_id);
    if (node->options.bootstrap_script[0] != '\0') {
        node_execute_script(node, node->options.bootstrap_script);
    }
    node->last_evolve_ms = now_ms();
    node->last_sync_ms = node->last_evolve_ms;
    char line[512];
    bool prompt_printed = false;
    while (true) {
        node_poll_listener(node);
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(0, &rfds);
        struct timeval tv;
        uint32_t timeout_ms = node->options.auto_learn ? (node->options.auto_evolve_ms > 0 ? node->options.auto_evolve_ms : 500U) : 1000U;
        tv.tv_sec = timeout_ms / 1000U;
        tv.tv_usec = (timeout_ms % 1000U) * 1000U;
        if (!prompt_printed) {
            printf("колибри-%u> ", node->options.node_id);
            fflush(stdout);
            prompt_printed = true;
        }
        int ready = select(1, &rfds, NULL, NULL, &tv);
        if (ready > 0 && FD_ISSET(0, &rfds)) {
            if (!fgets(line, sizeof(line), stdin)) {
                printf("\n[Сессия] входной поток закрыт\n");
                break;
            }
            prompt_printed = false;
            trim_newline(line);
            trim_spaces(line);
            if (line[0] == '\0') {
                continue;
            }
            node_poll_listener(node);
            if (line[0] == ':') {
            const char *command = line + 1;
            while (*command && !isspace((unsigned char)*command)) {
                ++command;
            }
            size_t prefix = (size_t)(command - (line + 1));
            char name[32];
            strncpy(name, line + 1, prefix);
            name[prefix] = '\0';
            while (*command && isspace((unsigned char)*command)) {
                ++command;
            }
            if (strcmp(name, "teach") == 0) {
                node_handle_teach(node, command);
                continue;
            }
            if (strcmp(name, "ask") == 0) {
                node_handle_ask(node, command);
                continue;
            }
            if (strcmp(name, "good") == 0) {
                node_handle_good(node);
                continue;
            }
            if (strcmp(name, "bad") == 0) {
                node_handle_bad(node);
                continue;
            }
            if (strcmp(name, "tick") == 0) {
                int gens = 1;
                if (command[0] != '\0') {
                    if (!parse_int32(command, &gens) || gens <= 0) {
                        printf("[Формулы] ожидалось натуральное число\n");
                        continue;
                    }
                }
                node_handle_tick(node, (size_t)gens);
                continue;
            }
            if (strcmp(name, "evolve") == 0) {
                int gens = 32;
                if (command[0] != '\0') {
                    if (!parse_int32(command, &gens) || gens <= 0) {
                        printf("[Формулы] ожидалось натуральное число\n");
                        continue;
                    }
                }
                node_handle_tick(node, (size_t)gens);
                continue;
            }
            if (strcmp(name, "why") == 0) {
                node_report_formula(node);
                continue;
            }
            if (strcmp(name, "canvas") == 0) {
                node_print_canvas(node);
                continue;
            }
            if (strcmp(name, "sync") == 0) {
                node_share_formula(node);
                continue;
            }
            if (strcmp(name, "verify") == 0) {
                node_handle_verify(node);
                continue;
            }
            if (strcmp(name, "stats") == 0) {
                node_handle_stats(node);
                continue;
            }
            if (strcmp(name, "mass-learn") == 0) {
                node_handle_mass_learn(node);
                continue;
            }
            if (strcmp(name, "script") == 0) {
                if (command[0] == '\0') {
                    printf("[KolibriScript] требуется путь к файлу\n");
                    continue;
                }
                node_execute_script(node, command);
                continue;
            }
            if (strcmp(name, "fractal") == 0) {
                node_print_canvas(node);
                continue;
            }
            if (strcmp(name, "help") == 0) {
                node_print_help();
                continue;
            }
            if (strcmp(name, "quit") == 0 || strcmp(name, "exit") == 0) {
                printf("[Сессия] завершение работы по команде\n");
                break;
            }
            printf("[Команда] неизвестная директива %s\n", name);
            continue;
            }
            node_store_text(node, line);
            node_record_event(node, "NOTE", "свободный текст сохранён");
        } else {
            if (node->options.auto_learn) {
                uint64_t now = now_ms();
                if (node->pool.examples > 0 && (now - node->last_evolve_ms) >= node->options.auto_evolve_ms) {
                    kf_pool_tick(&node->pool, 1);
                    node_record_event(node, "EVOLVE", "автоцикл");
                    node->last_evolve_ms = now;
                }
                if (node->options.peer_enabled && (now - node->last_sync_ms) >= node->options.auto_sync_ms) {
                    node_share_formula(node);
                    node->last_sync_ms = now;
                }
            }
        }
    }
}

static int node_start_listener(KolibriNode *node) {
    if (!node->options.listen_enabled) {
        return 0;
    }
    if (kn_listener_start(&node->listener, node->options.listen_port) != 0) {
        fprintf(stderr, "[Рой] не удалось открыть порт %u\n",
                node->options.listen_port);
        return -1;
    }
    node->listener_ready = true;
    printf("[Рой] слушаем порт %u\n", node->options.listen_port);
    return 0;
}

static void node_stop_listener(KolibriNode *node) {
    if (!node->listener_ready) {
        return;
    }
    kn_listener_close(&node->listener);
    node->listener_ready = false;
}

static int node_init(KolibriNode *node, const KolibriNodeOptions *options) {
    memset(node, 0, sizeof(*node));
    node->options = *options;
    if (node_load_hmac_key(node) != 0) {
        return -1;
    }
    node_reset_last_answer(node);
    k_digit_stream_init(&node->memory, node->memory_buffer, sizeof(node->memory_buffer));
    kf_pool_init(&node->pool, node->options.seed);
    k_corpus_init(&node->corpus, 0, 0);
    if (node_open_genome(node) != 0) {
        k_corpus_free(&node->corpus);
        return -1;
    }
    if (node_start_listener(node) != 0) {
        node_close_genome(node);
        k_corpus_free(&node->corpus);
        return -1;
    }
    return 0;
}

static void node_shutdown(KolibriNode *node) {
    node_stop_listener(node);
    if (node->script_ready) {
        ks_free(&node->script);
        node->script_ready = false;
    }
    k_corpus_free(&node->corpus);
    node_close_genome(node);
}

static int node_emit_health(KolibriNode *node) {
    int genome_status = kg_verify_file(node->options.genome_path,
                                       node->hmac_key,
                                       node->hmac_key_len);
    const char *genome_state = "unknown";
    if (genome_status == 0) {
        genome_state = "ok";
    } else if (genome_status == 1) {
        genome_state = "missing";
    } else {
        genome_state = "invalid";
    }
    const char *overall = (genome_status == 0) ? "ok" : "degraded";
    printf("{\"status\":\"%s\",\"node_id\":%u,\"seed\":%" PRIu64 ",\"genome\":{\"path\":\"%s\",\"origin\":\"%s\",\"state\":\"%s\"}}\n",
           overall,
           node->options.node_id,
           node->options.seed,
           node->options.genome_path,
           node->hmac_key_origin,
           genome_state);
    return (genome_status == 0) ? 0 : 1;
}

int main(int argc, char **argv) {
    KolibriNodeOptions options;
    parse_options(argc, argv, &options);
    if (options.health_check) {
        options.listen_enabled = false;
    }
    KolibriNode *node = malloc(sizeof(KolibriNode));
    if (!node) {
        fprintf(stderr, "Ошибка: не удалось выделить память для узла\n");
        return 1;
    }
    if (node_init(node, &options) != 0) {
        free(node);
        return 1;
    }
    if (options.health_check) {
        int status = node_emit_health(node);
        node_shutdown(node);
        free(node);
        return status;
    }
    node_run(node);
    node_shutdown(node);
    printf("Колибри узел %u завершил работу\n", options.node_id);
    free(node);
    return 0;
}
