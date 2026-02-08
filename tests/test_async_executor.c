/*
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 * 
 * Тест асинхронного исполнителя правил для Kolibri OS.
 */

#include "kolibri/async_executor.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* --- Счётчик обработанных стимулов для теста --- */
static int g_processed_count = 0;
static pthread_mutex_t g_count_mutex = PTHREAD_MUTEX_INITIALIZER;

/* --- Тестовый обработчик правил --- */
static int test_rule_handler(const KolibriStimulus *stimulus,
                             KolibriGenome *genome,
                             void *user_data) {
    (void)genome;
    (void)user_data;
    
    printf("  [ОБРАБОТЧИК] Получен стимул: id=%lu, тип='%s', данные='%s'\n",
           (unsigned long)stimulus->id,
           stimulus->event_type,
           stimulus->payload);
    
    pthread_mutex_lock(&g_count_mutex);
    g_processed_count++;
    pthread_mutex_unlock(&g_count_mutex);
    
    return 0;
}

/* --- Тест 1: Проверка паттернов событий --- */
static void test_event_patterns(void) {
    printf("=== Тест 1: Проверка паттернов событий ===\n");
    
    /* Точное совпадение */
    assert(ke_match_event_pattern("user.login", "user.login") == true);
    assert(ke_match_event_pattern("user.login", "user.logout") == false);
    
    /* Звёздочка */
    assert(ke_match_event_pattern("user.login", "user.*") == true);
    assert(ke_match_event_pattern("user.logout", "user.*") == true);
    assert(ke_match_event_pattern("system.start", "user.*") == false);
    assert(ke_match_event_pattern("network.connect", "*") == true);
    assert(ke_match_event_pattern("a.b.c", "a.*.*") == true);
    
    /* Знак вопроса */
    assert(ke_match_event_pattern("user1", "user?") == true);
    assert(ke_match_event_pattern("user12", "user?") == false);
    
    /* Комбинации */
    assert(ke_match_event_pattern("system.network.connect", "system.*") == true);
    assert(ke_match_event_pattern("data", "*") == true);
    
    printf("  [OK] Все паттерны работают корректно\n\n");
}

/* --- Тест 2: Инициализация и освобождение --- */
static void test_init_free(void) {
    printf("=== Тест 2: Инициализация и освобождение ===\n");
    
    KolibriEventLoop loop;
    assert(ke_loop_init(&loop) == 0);
    ke_loop_free(&loop);
    printf("  [OK] Event loop создан и освобождён\n");
    
    KolibriRuleReactor reactor;
    assert(ke_reactor_init(&reactor, NULL, NULL) == 0);
    ke_reactor_free(&reactor);
    printf("  [OK] Реактор создан и освобождён\n\n");
}

/* --- Тест 3: Буферизация стимулов --- */
static void test_stimulus_queue(void) {
    printf("=== Тест 3: Буферизация стимулов ===\n");
    
    KolibriEventLoop loop;
    assert(ke_loop_init(&loop) == 0);
    
    /* Добавляем стимулы */
    uint64_t id1, id2, id3;
    assert(ke_stimulus_queue(&loop, "user.login", "user123", 7,
                             KE_PRIORITY_NORMAL, "test", &id1) == 0);
    assert(ke_stimulus_queue(&loop, "data.update", "{\"key\": 42}", 12,
                             KE_PRIORITY_HIGH, "test", &id2) == 0);
    assert(ke_stimulus_queue(&loop, "system.shutdown", "", 0,
                             KE_PRIORITY_URGENT, "test", &id3) == 0);
    
    printf("  [OK] Добавлены стимулы: id1=%lu, id2=%lu, id3=%lu\n",
           (unsigned long)id1, (unsigned long)id2, (unsigned long)id3);
    
    assert(ke_stimulus_queue_size(&loop) == 3);
    assert(!ke_stimulus_queue_empty(&loop));
    
    /* Извлекаем стимулы */
    KolibriStimulus stim;
    assert(ke_stimulus_dequeue(&loop, &stim, 0) == 0);
    assert(stim.id == id1);
    assert(strcmp(stim.event_type, "user.login") == 0);
    printf("  [OK] Извлечён стимул: id=%lu, тип='%s'\n",
           (unsigned long)stim.id, stim.event_type);
    
    assert(ke_stimulus_dequeue(&loop, &stim, 0) == 0);
    assert(stim.id == id2);
    
    assert(ke_stimulus_dequeue(&loop, &stim, 0) == 0);
    assert(stim.id == id3);
    
    assert(ke_stimulus_queue_empty(&loop));
    
    /* Попытка извлечь из пустой очереди */
    assert(ke_stimulus_dequeue(&loop, &stim, 0) == 1);  /* Таймаут */
    
    ke_loop_free(&loop);
    printf("  [OK] Очередь стимулов работает корректно\n\n");
}

/* --- Тест 4: Добавление и удаление правил --- */
static void test_rules(void) {
    printf("=== Тест 4: Добавление и удаление правил ===\n");
    
    KolibriRuleReactor reactor;
    assert(ke_reactor_init(&reactor, NULL, NULL) == 0);
    
    /* Добавляем правила */
    int idx1 = ke_reactor_add_rule(&reactor, "rule_users", "user.*",
                                   test_rule_handler, NULL);
    int idx2 = ke_reactor_add_rule(&reactor, "rule_all", "*",
                                   test_rule_handler, NULL);
    assert(idx1 == 0);
    assert(idx2 == 1);
    printf("  [OK] Добавлены правила: idx1=%d, idx2=%d\n", idx1, idx2);
    
    /* Деактивация правила */
    assert(ke_reactor_set_rule_active(&reactor, 0, false) == 0);
    printf("  [OK] Правило 0 деактивировано\n");
    
    /* Удаление правила */
    assert(ke_reactor_remove_rule(&reactor, 0) == 0);
    printf("  [OK] Правило 0 удалено\n");
    
    ke_reactor_free(&reactor);
    printf("  [OK] Правила работают корректно\n\n");
}

/* --- Тест 5: Асинхронная обработка --- */
static void test_async_processing(void) {
    printf("=== Тест 5: Асинхронная обработка ===\n");
    
    g_processed_count = 0;
    
    KolibriRuleReactor reactor;
    assert(ke_reactor_init(&reactor, NULL, NULL) == 0);
    
    /* Добавляем правило */
    int idx = ke_reactor_add_rule(&reactor, "catch_all", "*",
                                  test_rule_handler, NULL);
    assert(idx == 0);
    
    /* Запускаем реактор асинхронно */
    assert(ke_reactor_run_async(&reactor) == 0);
    printf("  [OK] Реактор запущен асинхронно\n");
    
    assert(ke_reactor_is_running(&reactor) == true);
    
    /* Добавляем стимулы */
    uint64_t last_id;
    for (int i = 0; i < 5; i++) {
        char payload[32];
        snprintf(payload, sizeof(payload), "data_%d", i);
        assert(ke_stimulus_queue(&reactor.event_loop, "test.event",
                                 payload, strlen(payload),
                                 KE_PRIORITY_NORMAL, "async_test", &last_id) == 0);
    }
    printf("  [OK] Добавлено 5 стимулов\n");
    
    /* Ждём обработки */
    usleep(100000);  /* 100 мс */
    
    /* Останавливаем реактор */
    assert(ke_reactor_stop(&reactor) == 0);
    printf("  [OK] Реактор остановлен\n");
    
    assert(ke_reactor_is_running(&reactor) == false);
    
    pthread_mutex_lock(&g_count_mutex);
    printf("  [OK] Обработано стимулов: %d\n", g_processed_count);
    pthread_mutex_unlock(&g_count_mutex);
    
    ke_reactor_free(&reactor);
    printf("  [OK] Асинхронная обработка завершена\n\n");
}

/* --- Тест 6: Статистика --- */
static void test_statistics(void) {
    printf("=== Тест 6: Статистика ===\n");
    
    KolibriRuleReactor reactor;
    assert(ke_reactor_init(&reactor, NULL, NULL) == 0);
    
    ke_reactor_add_rule(&reactor, "catch_all", "*", test_rule_handler, NULL);
    
    /* Добавляем стимулы */
    for (int i = 0; i < 3; i++) {
        ke_stimulus_queue(&reactor.event_loop, "stat.test", "x", 1,
                          KE_PRIORITY_NORMAL, "stats", NULL);
    }
    
    /* Обрабатываем синхронно */
    while (ke_reactor_tick(&reactor) > 0) {
        /* продолжаем */
    }
    
    KeLoopStats stats;
    assert(ke_loop_get_stats(&reactor.event_loop, &stats) == 0);
    
    printf("  Всего стимулов: %lu\n", (unsigned long)stats.total_stimuli);
    printf("  Всего тиков: %lu\n", (unsigned long)stats.total_ticks);
    printf("  Среднее время тика: %lu мкс\n", (unsigned long)stats.avg_tick_time_us);
    printf("  Максимальное время тика: %lu мкс\n", (unsigned long)stats.max_tick_time_us);
    
    ke_reactor_free(&reactor);
    printf("  [OK] Статистика получена\n\n");
}

/* --- Главная функция --- */
int main(void) {
    printf("\n========================================\n");
    printf("  ТЕСТЫ АСИНХРОННОГО ИСПОЛНИТЕЛЯ ПРАВИЛ\n");
    printf("========================================\n\n");
    
    test_event_patterns();
    test_init_free();
    test_stimulus_queue();
    test_rules();
    test_async_processing();
    test_statistics();
    
    printf("========================================\n");
    printf("  ВСЕ ТЕСТЫ ПРОЙДЕНЫ УСПЕШНО!\n");
    printf("========================================\n\n");
    
    return 0;
}
