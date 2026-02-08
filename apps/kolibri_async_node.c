/*
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 *
 * kolibri_async_node — асинхронный daemon-узел Kolibri OS.
 *
 * Использует модуль async_executor для работы с реактором правил
 * в отдельном потоке. Принимает сетевые стимулы через TCP-порт
 * и автоматически эволюционирует формулы.
 *
 * Использование:
 *   ./kolibri_async_node --genome genome.dat --listen 4050
 */

#include "kolibri/async_executor.h"
#include "kolibri/formula.h"
#include "kolibri/genome.h"
#include "kolibri/net.h"
#include "kolibri/script.h"

#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* --- Конфигурация узла --- */
#define ASYNC_NODE_VERSION "1.0.0"
#define DEFAULT_GENOME_PATH "genome.dat"
#define DEFAULT_PORT 4050
#define DEFAULT_TICK_INTERVAL_MS 200

/* --- Параметры командной строки --- */
typedef struct {
    char genome_path[260];
    uint16_t listen_port;
    char bootstrap_script[260];
    uint32_t tick_interval_ms;
    bool verbose;
} AsyncNodeConfig;

/* --- Глобальное состояние --- */
static volatile sig_atomic_t g_running = 1;

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

/* --- Обработчики правил реактора --- */

/* Правило: обучение — записывает стимул в геном */
static int rule_teach(const KolibriStimulus *stimulus,
                      KolibriGenome *genome,
                      void *user_data) {
    (void)user_data;
    ReasonBlock block;
    int rc = kg_append(genome, stimulus->event_type, stimulus->payload, &block);
    if (rc != 0) {
        fprintf(stderr, "[Правило:teach] ошибка записи в геном\n");
        return -1;
    }
    printf("[Правило:teach] стимул %" PRIu64 " записан → блок %" PRIu64 "\n",
           stimulus->id, block.index);
    return 0;
}

/* Правило: эволюция — запускает тик формул */
static int rule_evolve(const KolibriStimulus *stimulus,
                       KolibriGenome *genome,
                       void *user_data) {
    (void)stimulus;
    (void)genome;
    KolibriFormulaPool *pool = (KolibriFormulaPool *)user_data;
    if (pool && pool->examples > 0) {
        kf_pool_tick(pool, 1);
    }
    return 0;
}

/* --- Обработка входящего P2P-сообщения --- */
static void handle_net_message(const KolibriNetMessage *msg,
                               KolibriRuleReactor *reactor) {
    const char *event = "NET";
    const char *payload = "p2p_message";
    size_t plen = strlen(payload);

    switch (msg->type) {
    case KOLIBRI_MSG_SWARM_KNOWLEDGE:
        event = "TEACH";
        payload = "knowledge_received";
        plen = strlen(payload);
        break;
    case KOLIBRI_MSG_MIGRATE_RULE:
        event = "TEACH";
        payload = "rule_received";
        plen = strlen(payload);
        break;
    default:
        break;
    }

    ke_stimulus_queue(&reactor->event_loop,
                      event, payload, plen,
                      KE_PRIORITY_NORMAL, "p2p", NULL);
}

/* --- Вывод справки --- */
static void print_usage(const char *prog) {
    printf("Kolibri Async Node v%s — асинхронный daemon-узел\n\n", ASYNC_NODE_VERSION);
    printf("Использование: %s [опции]\n\n", prog);
    printf("Опции:\n");
    printf("  --genome <путь>       Путь к файлу генома (по умолч: %s)\n", DEFAULT_GENOME_PATH);
    printf("  --listen <порт>       Порт для входящих стимулов (по умолч: %d)\n", DEFAULT_PORT);
    printf("  --bootstrap <путь>    KolibriScript для выполнения при старте\n");
    printf("  --tick-ms <мс>        Интервал тиков реактора (по умолч: %d)\n", DEFAULT_TICK_INTERVAL_MS);
    printf("  --verbose             Расширенный вывод\n");
    printf("  --help, -h            Показать эту справку\n");
    printf("\nСетевой протокол (TCP):\n");
    printf("  Отправить: EVENT_TYPE payload_text\\n\n");
    printf("  Ответ:     OK <stim_id>\\n\n");
}

/* --- Разбор аргументов --- */
static void parse_config(int argc, char **argv, AsyncNodeConfig *cfg) {
    strncpy(cfg->genome_path, DEFAULT_GENOME_PATH, sizeof(cfg->genome_path) - 1);
    cfg->listen_port = DEFAULT_PORT;
    cfg->bootstrap_script[0] = '\0';
    cfg->tick_interval_ms = DEFAULT_TICK_INTERVAL_MS;
    cfg->verbose = false;

    for (int i = 1; i < argc; ++i) {
        if ((strcmp(argv[i], "--help") == 0) || (strcmp(argv[i], "-h") == 0)) {
            print_usage(argv[0]);
            exit(0);
        }
        if (strcmp(argv[i], "--genome") == 0 && i + 1 < argc) {
            strncpy(cfg->genome_path, argv[++i], sizeof(cfg->genome_path) - 1);
            continue;
        }
        if (strcmp(argv[i], "--listen") == 0 && i + 1 < argc) {
            cfg->listen_port = (uint16_t)strtoul(argv[++i], NULL, 10);
            continue;
        }
        if (strcmp(argv[i], "--bootstrap") == 0 && i + 1 < argc) {
            strncpy(cfg->bootstrap_script, argv[++i], sizeof(cfg->bootstrap_script) - 1);
            continue;
        }
        if (strcmp(argv[i], "--tick-ms") == 0 && i + 1 < argc) {
            cfg->tick_interval_ms = (uint32_t)strtoul(argv[++i], NULL, 10);
            continue;
        }
        if (strcmp(argv[i], "--verbose") == 0) {
            cfg->verbose = true;
            continue;
        }
        fprintf(stderr, "[async_node] неизвестный аргумент: %s\n", argv[i]);
    }
}

/* --- Главная точка входа --- */
int main(int argc, char **argv) {
    AsyncNodeConfig cfg;
    parse_config(argc, argv, &cfg);

    /* Перехват сигналов для грациозной остановки */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("=== Kolibri Async Node v%s ===\n", ASYNC_NODE_VERSION);
    printf("[Конфигурация] геном=%s порт=%u тик=%u мс\n",
           cfg.genome_path, cfg.listen_port, cfg.tick_interval_ms);

    /* --- Инициализация генома --- */
    static const unsigned char hmac_key[] = "kolibri-secret-key";
    KolibriGenome genome;
    if (kg_open(&genome, cfg.genome_path, hmac_key, sizeof(hmac_key) - 1) != 0) {
        fprintf(stderr, "[Геном] не удалось открыть %s\n", cfg.genome_path);
        return 1;
    }
    printf("[Геном] файл %s открыт\n", cfg.genome_path);

    /* --- Инициализация пула формул --- */
    KolibriFormulaPool pool;
    kf_pool_init(&pool, 20250923ULL);

    /* --- Инициализация реактора --- */
    KolibriRuleReactor reactor;
    if (ke_reactor_init(&reactor, &genome, NULL) != 0) {
        fprintf(stderr, "[Реактор] ошибка инициализации\n");
        kg_close(&genome);
        return 1;
    }
    reactor.tick_interval_ms = cfg.tick_interval_ms;

    /* Регистрация правил */
    ke_reactor_add_rule(&reactor, "teach", "TEACH", rule_teach, NULL);
    ke_reactor_add_rule(&reactor, "note", "NOTE", rule_teach, NULL);
    ke_reactor_add_rule(&reactor, "evolve", "EVOLVE", rule_evolve, &pool);
    ke_reactor_add_rule(&reactor, "boot", "BOOT", rule_teach, NULL);
    printf("[Реактор] зарегистрировано %zu правил\n", reactor.rules_count);

    /* --- Запуск реактора в отдельном потоке --- */
    if (ke_reactor_run_async(&reactor) != 0) {
        fprintf(stderr, "[Реактор] не удалось запустить\n");
        ke_reactor_free(&reactor);
        kg_close(&genome);
        return 1;
    }
    printf("[Реактор] запущен в фоновом потоке\n");

    /* Записываем событие загрузки */
    ke_stimulus_queue(&reactor.event_loop, "BOOT",
                      "async_node активирован", 23,
                      KE_PRIORITY_HIGH, "system", NULL);

    /* --- Запуск сетевого слушателя --- */
    KolibriNetListener listener;
    if (kn_listener_start(&listener, cfg.listen_port) != 0) {
        fprintf(stderr, "[Сеть] не удалось открыть порт %u\n", cfg.listen_port);
        ke_reactor_stop(&reactor);
        ke_reactor_free(&reactor);
        kg_close(&genome);
        return 1;
    }
    printf("[Сеть] слушаем порт %u\n", cfg.listen_port);

    /* --- Главный цикл --- */
    printf("[Узел] готов к работе. CTRL+C для остановки.\n");
    uint64_t evolve_counter = 0;

    while (g_running) {
        /* Принимаем входящие P2P-сообщения (неблокирующе) */
        KolibriNetMessage net_msg;
        int poll_rc = kn_listener_poll(&listener, 100, &net_msg);
        if (poll_rc == 0) {
            handle_net_message(&net_msg, &reactor);
        }

        /* Периодическая эволюция формул */
        evolve_counter++;
        if (evolve_counter % 50 == 0 && pool.examples > 0) {
            ke_stimulus_queue(&reactor.event_loop, "EVOLVE",
                              "автоцикл", 14,
                              KE_PRIORITY_LOW, "timer", NULL);
        }

        /* Периодический вывод статистики */
        if (cfg.verbose && evolve_counter % 500 == 0) {
            KeLoopStats stats;
            if (ke_loop_get_stats(&reactor.event_loop, &stats) == 0) {
                printf("[Статистика] обработано=%" PRIu64
                       " коммитов=%" PRIu64
                       " ошибок=%" PRIu64
                       " очередь=%zu\n",
                       stats.total_stimuli,
                       stats.committed_stimuli,
                       stats.failed_stimuli,
                       stats.queue_depth);
            }
        }
    }

    /* --- Грациозная остановка --- */
    printf("\n[Узел] завершение работы...\n");

    kn_listener_close(&listener);
    printf("[Сеть] слушатель закрыт\n");

    ke_reactor_stop(&reactor);
    printf("[Реактор] остановлен\n");

    /* Финальная статистика */
    KeLoopStats final_stats;
    if (ke_loop_get_stats(&reactor.event_loop, &final_stats) == 0) {
        printf("[Итого] стимулов=%" PRIu64 " коммитов=%" PRIu64
               " ошибок=%" PRIu64 " отброшено=%" PRIu64 "\n",
               final_stats.total_stimuli,
               final_stats.committed_stimuli,
               final_stats.failed_stimuli,
               final_stats.dropped_stimuli);
    }

    ke_reactor_free(&reactor);
    kg_close(&genome);
    printf("[Узел] завершён.\n");

    return 0;
}
