/*
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 *
 * Реализация асинхронного исполнителя правил для Kolibri OS.
 * 
 * Архитектура:
 * 1. Сетевой слой помещает стимулы в очередь через ke_stimulus_queue()
 * 2. Реактор правил работает в отдельном потоке (ke_reactor_run_async)
 * 3. Каждый тик реактора извлекает стимул и применяет правила
 * 4. Результаты записываются в геном с подтверждением
 * 5. Ожидающие потоки уведомляются через ke_wait_genome_commit()
 */

#define _GNU_SOURCE
#include "kolibri/async_executor.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* --- Вспомогательные макросы --- */
#define KE_MIN(a, b) ((a) < (b) ? (a) : (b))
#define KE_ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/* --- Получение времени --- */

uint64_t ke_current_time_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0U;
    }
    return (uint64_t)ts.tv_sec * 1000U + (uint64_t)ts.tv_nsec / 1000000U;
}

static uint64_t ke_current_time_us(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0U;
    }
    return (uint64_t)ts.tv_sec * 1000000U + (uint64_t)ts.tv_nsec / 1000U;
}

/* --- Проверка соответствия паттерну --- */

bool ke_match_event_pattern(const char *event_type, const char *pattern) {
    if (!event_type || !pattern) {
        return false;
    }
    
    const char *e = event_type;
    const char *p = pattern;
    const char *star_p = NULL;
    const char *star_e = NULL;
    
    while (*e) {
        if (*p == '*') {
            /* Запоминаем позицию звёздочки */
            star_p = p++;
            star_e = e;
        } else if (*p == '?' || *p == *e) {
            /* Совпадение символа или ? */
            p++;
            e++;
        } else if (star_p) {
            /* Возврат к последней звёздочке */
            p = star_p + 1;
            e = ++star_e;
        } else {
            return false;
        }
    }
    
    /* Пропускаем оставшиеся звёздочки */
    while (*p == '*') {
        p++;
    }
    
    return *p == '\0';
}

/* --- Инициализация/освобождение петли событий --- */

int ke_loop_init(KolibriEventLoop *loop) {
    if (!loop) {
        return -1;
    }
    
    memset(loop, 0, sizeof(*loop));
    
    /* Инициализация очереди */
    loop->queue_head = 0;
    loop->queue_tail = 0;
    loop->queue_count = 0;
    
    /* Инициализация мьютексов и условных переменных */
    if (pthread_mutex_init(&loop->queue_mutex, NULL) != 0) {
        return -1;
    }
    
    if (pthread_cond_init(&loop->queue_not_empty, NULL) != 0) {
        pthread_mutex_destroy(&loop->queue_mutex);
        return -1;
    }
    
    if (pthread_cond_init(&loop->queue_not_full, NULL) != 0) {
        pthread_cond_destroy(&loop->queue_not_empty);
        pthread_mutex_destroy(&loop->queue_mutex);
        return -1;
    }
    
    if (pthread_mutex_init(&loop->state_mutex, NULL) != 0) {
        pthread_cond_destroy(&loop->queue_not_full);
        pthread_cond_destroy(&loop->queue_not_empty);
        pthread_mutex_destroy(&loop->queue_mutex);
        return -1;
    }
    
    if (pthread_cond_init(&loop->state_changed, NULL) != 0) {
        pthread_mutex_destroy(&loop->state_mutex);
        pthread_cond_destroy(&loop->queue_not_full);
        pthread_cond_destroy(&loop->queue_not_empty);
        pthread_mutex_destroy(&loop->queue_mutex);
        return -1;
    }
    
    if (pthread_mutex_init(&loop->stats_mutex, NULL) != 0) {
        pthread_cond_destroy(&loop->state_changed);
        pthread_mutex_destroy(&loop->state_mutex);
        pthread_cond_destroy(&loop->queue_not_full);
        pthread_cond_destroy(&loop->queue_not_empty);
        pthread_mutex_destroy(&loop->queue_mutex);
        return -1;
    }
    
    /* Начальное состояние */
    atomic_store(&loop->state, KE_LOOP_STOPPED);
    atomic_store(&loop->next_stimulus_id, 1U);
    
    return 0;
}

void ke_loop_free(KolibriEventLoop *loop) {
    if (!loop) {
        return;
    }
    
    /* Уничтожение примитивов синхронизации */
    pthread_mutex_destroy(&loop->stats_mutex);
    pthread_cond_destroy(&loop->state_changed);
    pthread_mutex_destroy(&loop->state_mutex);
    pthread_cond_destroy(&loop->queue_not_full);
    pthread_cond_destroy(&loop->queue_not_empty);
    pthread_mutex_destroy(&loop->queue_mutex);
    
    memset(loop, 0, sizeof(*loop));
}

int ke_loop_get_stats(KolibriEventLoop *loop, KeLoopStats *stats) {
    if (!loop || !stats) {
        return -1;
    }
    
    pthread_mutex_lock(&loop->stats_mutex);
    *stats = loop->stats;
    pthread_mutex_unlock(&loop->stats_mutex);
    
    /* Добавляем текущую глубину очереди */
    pthread_mutex_lock(&loop->queue_mutex);
    stats->queue_depth = loop->queue_count;
    pthread_mutex_unlock(&loop->queue_mutex);
    
    return 0;
}

/* --- Буферизация стимулов --- */

int ke_stimulus_queue(KolibriEventLoop *loop,
                      const char *event_type,
                      const char *payload,
                      size_t payload_len,
                      KeStimulusPriority priority,
                      const char *source,
                      uint64_t *out_id) {
    if (!loop || !event_type) {
        return -2;
    }
    
    pthread_mutex_lock(&loop->queue_mutex);
    
    /* Проверка переполнения */
    if (loop->queue_count >= KE_MAX_STIMULUS_QUEUE) {
        pthread_mutex_unlock(&loop->queue_mutex);
        
        /* Обновляем статистику отброшенных */
        pthread_mutex_lock(&loop->stats_mutex);
        loop->stats.dropped_stimuli++;
        pthread_mutex_unlock(&loop->stats_mutex);
        
        return -1;  /* Переполнение */
    }
    
    /* Получаем уникальный ID */
    uint64_t id = atomic_fetch_add(&loop->next_stimulus_id, 1U);
    
    /* Заполняем стимул */
    KolibriStimulus *stim = &loop->queue[loop->queue_tail];
    memset(stim, 0, sizeof(*stim));
    
    stim->id = id;
    stim->timestamp = ke_current_time_ms();
    stim->priority = priority;
    stim->state = KE_STIMULUS_PENDING;
    
    /* Копируем источник */
    if (source) {
        strncpy(stim->source, source, sizeof(stim->source) - 1);
        stim->source[sizeof(stim->source) - 1] = '\0';
    }
    
    /* Копируем тип события */
    strncpy(stim->event_type, event_type, sizeof(stim->event_type) - 1);
    stim->event_type[sizeof(stim->event_type) - 1] = '\0';
    
    /* Копируем данные */
    if (payload && payload_len > 0) {
        size_t copy_len = KE_MIN(payload_len, sizeof(stim->payload) - 1);
        memcpy(stim->payload, payload, copy_len);
        stim->payload[copy_len] = '\0';
        stim->payload_len = copy_len;
    }
    
    /* Продвигаем хвост очереди */
    loop->queue_tail = (loop->queue_tail + 1) % KE_MAX_STIMULUS_QUEUE;
    loop->queue_count++;
    
    /* Уведомляем ожидающих */
    pthread_cond_signal(&loop->queue_not_empty);
    
    pthread_mutex_unlock(&loop->queue_mutex);
    
    /* Возвращаем ID */
    if (out_id) {
        *out_id = id;
    }
    
    return 0;
}

int ke_stimulus_dequeue(KolibriEventLoop *loop,
                        KolibriStimulus *out_stimulus,
                        int timeout_ms) {
    if (!loop || !out_stimulus) {
        return -1;
    }
    
    pthread_mutex_lock(&loop->queue_mutex);
    
    /* Ожидание с таймаутом */
    while (loop->queue_count == 0) {
        if (timeout_ms == 0) {
            /* Без ожидания */
            pthread_mutex_unlock(&loop->queue_mutex);
            return 1;  /* Таймаут */
        } else if (timeout_ms < 0) {
            /* Бесконечное ожидание */
            pthread_cond_wait(&loop->queue_not_empty, &loop->queue_mutex);
        } else {
            /* Ожидание с таймаутом */
            struct timespec abstime;
            clock_gettime(CLOCK_REALTIME, &abstime);
            abstime.tv_sec += timeout_ms / 1000;
            abstime.tv_nsec += (timeout_ms % 1000) * 1000000L;
            if (abstime.tv_nsec >= 1000000000L) {
                abstime.tv_sec += 1;
                abstime.tv_nsec -= 1000000000L;
            }
            
            int ret = pthread_cond_timedwait(&loop->queue_not_empty,
                                              &loop->queue_mutex, &abstime);
            if (ret == ETIMEDOUT) {
                pthread_mutex_unlock(&loop->queue_mutex);
                return 1;  /* Таймаут */
            }
        }
        
        /* Проверяем состояние петли */
        int state = atomic_load(&loop->state);
        if (state == KE_LOOP_STOPPING || state == KE_LOOP_STOPPED) {
            pthread_mutex_unlock(&loop->queue_mutex);
            return -1;
        }
    }
    
    /* Извлекаем стимул */
    *out_stimulus = loop->queue[loop->queue_head];
    out_stimulus->state = KE_STIMULUS_PROCESSING;
    
    /* Продвигаем голову очереди */
    loop->queue_head = (loop->queue_head + 1) % KE_MAX_STIMULUS_QUEUE;
    loop->queue_count--;
    
    /* Уведомляем ожидающих места в очереди */
    pthread_cond_signal(&loop->queue_not_full);
    
    pthread_mutex_unlock(&loop->queue_mutex);
    
    return 0;
}

bool ke_stimulus_queue_empty(KolibriEventLoop *loop) {
    if (!loop) {
        return true;
    }
    
    pthread_mutex_lock(&loop->queue_mutex);
    bool empty = (loop->queue_count == 0);
    pthread_mutex_unlock(&loop->queue_mutex);
    
    return empty;
}

size_t ke_stimulus_queue_size(KolibriEventLoop *loop) {
    if (!loop) {
        return 0;
    }
    
    pthread_mutex_lock(&loop->queue_mutex);
    size_t size = loop->queue_count;
    pthread_mutex_unlock(&loop->queue_mutex);
    
    return size;
}

/* --- Управление реактором правил --- */

int ke_reactor_init(KolibriRuleReactor *reactor,
                    KolibriGenome *genome,
                    KolibriScript *script) {
    if (!reactor) {
        return -1;
    }
    
    memset(reactor, 0, sizeof(*reactor));
    
    reactor->genome = genome;
    reactor->script = script;
    
    /* Инициализация петли событий */
    if (ke_loop_init(&reactor->event_loop) != 0) {
        return -1;
    }
    
    /* Инициализация блокировки правил */
    if (pthread_mutex_init(&reactor->rules_lock, NULL) != 0) {
        ke_loop_free(&reactor->event_loop);
        return -1;
    }
    
    /* Инициализация синхронизации коммитов */
    if (pthread_mutex_init(&reactor->commit_mutex, NULL) != 0) {
        pthread_mutex_destroy(&reactor->rules_lock);
        ke_loop_free(&reactor->event_loop);
        return -1;
    }
    
    if (pthread_cond_init(&reactor->commit_done, NULL) != 0) {
        pthread_mutex_destroy(&reactor->commit_mutex);
        pthread_mutex_destroy(&reactor->rules_lock);
        ke_loop_free(&reactor->event_loop);
        return -1;
    }
    
    /* Параметры по умолчанию */
    reactor->tick_interval_ms = 10;
    reactor->commit_timeout_ms = KE_COMMIT_TIMEOUT_MS;
    reactor->auto_checkpoint = true;
    reactor->worker_started = false;
    reactor->last_committed_id = 0;
    
    return 0;
}

void ke_reactor_free(KolibriRuleReactor *reactor) {
    if (!reactor) {
        return;
    }
    
    /* Останавливаем реактор, если запущен */
    if (reactor->worker_started) {
        ke_reactor_stop(reactor);
    }
    
    /* Освобождаем ресурсы */
    pthread_cond_destroy(&reactor->commit_done);
    pthread_mutex_destroy(&reactor->commit_mutex);
    pthread_mutex_destroy(&reactor->rules_lock);
    ke_loop_free(&reactor->event_loop);
    
    memset(reactor, 0, sizeof(*reactor));
}

int ke_reactor_add_rule(KolibriRuleReactor *reactor,
                        const char *name,
                        const char *event_pattern,
                        KeRuleHandler handler,
                        void *user_data) {
    if (!reactor || !name || !event_pattern || !handler) {
        return -1;
    }
    
    pthread_mutex_lock(&reactor->rules_lock);
    
    /* Проверяем лимит */
    if (reactor->rules_count >= KE_MAX_RULES) {
        pthread_mutex_unlock(&reactor->rules_lock);
        return -1;
    }
    
    /* Добавляем правило */
    KeRule *rule = &reactor->rules[reactor->rules_count];
    memset(rule, 0, sizeof(*rule));
    
    strncpy(rule->name, name, sizeof(rule->name) - 1);
    rule->name[sizeof(rule->name) - 1] = '\0';
    
    strncpy(rule->event_pattern, event_pattern, sizeof(rule->event_pattern) - 1);
    rule->event_pattern[sizeof(rule->event_pattern) - 1] = '\0';
    
    rule->handler = handler;
    rule->user_data = user_data;
    rule->active = true;
    rule->invocations = 0;
    rule->failures = 0;
    
    int index = (int)reactor->rules_count;
    reactor->rules_count++;
    
    pthread_mutex_unlock(&reactor->rules_lock);
    
    return index;
}

int ke_reactor_remove_rule(KolibriRuleReactor *reactor, size_t rule_index) {
    if (!reactor) {
        return -1;
    }
    
    pthread_mutex_lock(&reactor->rules_lock);
    
    if (rule_index >= reactor->rules_count) {
        pthread_mutex_unlock(&reactor->rules_lock);
        return -1;
    }
    
    /* Сдвигаем правила */
    for (size_t i = rule_index; i < reactor->rules_count - 1; i++) {
        reactor->rules[i] = reactor->rules[i + 1];
    }
    reactor->rules_count--;
    
    pthread_mutex_unlock(&reactor->rules_lock);
    
    return 0;
}

int ke_reactor_set_rule_active(KolibriRuleReactor *reactor,
                               size_t rule_index,
                               bool active) {
    if (!reactor) {
        return -1;
    }
    
    pthread_mutex_lock(&reactor->rules_lock);
    
    if (rule_index >= reactor->rules_count) {
        pthread_mutex_unlock(&reactor->rules_lock);
        return -1;
    }
    
    reactor->rules[rule_index].active = active;
    
    pthread_mutex_unlock(&reactor->rules_lock);
    
    return 0;
}

/* --- Обработка одного тика реактора --- */

int ke_reactor_tick(KolibriRuleReactor *reactor) {
    if (!reactor) {
        return -1;
    }
    
    /* Проверяем состояние */
    int state = atomic_load(&reactor->event_loop.state);
    if (state == KE_LOOP_PAUSED) {
        return 0;
    }
    
    /* Пытаемся извлечь стимул без блокировки */
    KolibriStimulus stimulus;
    int ret = ke_stimulus_dequeue(&reactor->event_loop, &stimulus, 0);
    if (ret != 0) {
        return 0;  /* Очередь пуста */
    }
    
    uint64_t tick_start = ke_current_time_us();
    bool any_matched = false;
    bool commit_success = true;
    
    /* Применяем правила */
    pthread_mutex_lock(&reactor->rules_lock);
    
    for (size_t i = 0; i < reactor->rules_count; i++) {
        KeRule *rule = &reactor->rules[i];
        
        if (!rule->active) {
            continue;
        }
        
        /* Проверяем соответствие паттерну */
        if (!ke_match_event_pattern(stimulus.event_type, rule->event_pattern)) {
            continue;
        }
        
        any_matched = true;
        
        /* Вызываем обработчик */
        int handler_ret = rule->handler(&stimulus, reactor->genome, rule->user_data);
        
        /* Обновляем статистику правила (атомарно не требуется - под read lock) */
        /* ПРИМЕЧАНИЕ: для точной статистики нужен write lock, но это замедлит обработку */
        if (handler_ret == 0) {
            rule->invocations++;
        } else {
            rule->failures++;
            commit_success = false;
        }
    }
    
    pthread_mutex_unlock(&reactor->rules_lock);
    
    /* Записываем в геном, если есть обработчики и геном доступен */
    if (any_matched && reactor->genome && commit_success) {
        ReasonBlock block;
        int append_ret = kg_append(reactor->genome, stimulus.event_type,
                                   stimulus.payload, &block);
        if (append_ret == 0) {
            stimulus.genome_index = block.index;
            stimulus.state = KE_STIMULUS_COMMITTED;
            
            /* Обновляем ID последнего коммита и уведомляем ожидающих */
            pthread_mutex_lock(&reactor->commit_mutex);
            reactor->last_committed_id = stimulus.id;
            pthread_cond_broadcast(&reactor->commit_done);
            pthread_mutex_unlock(&reactor->commit_mutex);
            
            /* Авто-checkpoint если включён */
            if (reactor->auto_checkpoint && reactor->genome->wal_enabled) {
                kg_wal_checkpoint(reactor->genome);
            }
        } else {
            stimulus.state = KE_STIMULUS_FAILED;
            commit_success = false;
        }
    }
    
    /* Обновляем статистику */
    uint64_t tick_time = ke_current_time_us() - tick_start;
    
    pthread_mutex_lock(&reactor->event_loop.stats_mutex);
    KeLoopStats *stats = &reactor->event_loop.stats;
    stats->total_stimuli++;
    stats->total_ticks++;
    
    if (commit_success && stimulus.state == KE_STIMULUS_COMMITTED) {
        stats->committed_stimuli++;
    } else if (stimulus.state == KE_STIMULUS_FAILED) {
        stats->failed_stimuli++;
    }
    
    /* Обновляем среднее и максимальное время тика */
    if (tick_time > stats->max_tick_time_us) {
        stats->max_tick_time_us = tick_time;
    }
    /* Скользящее среднее */
    if (stats->total_ticks == 1) {
        stats->avg_tick_time_us = tick_time;
    } else {
        stats->avg_tick_time_us = (stats->avg_tick_time_us * 7 + tick_time) / 8;
    }
    
    pthread_mutex_unlock(&reactor->event_loop.stats_mutex);
    
    return 1;  /* Обработан один стимул */
}

/* --- Рабочий поток реактора --- */

static void *ke_reactor_worker(void *arg) {
    KolibriRuleReactor *reactor = (KolibriRuleReactor *)arg;
    
    while (1) {
        int state = atomic_load(&reactor->event_loop.state);
        
        if (state == KE_LOOP_STOPPING || state == KE_LOOP_STOPPED) {
            break;
        }
        
        if (state == KE_LOOP_PAUSED) {
            /* Ждём изменения состояния */
            pthread_mutex_lock(&reactor->event_loop.state_mutex);
            while (atomic_load(&reactor->event_loop.state) == KE_LOOP_PAUSED) {
                pthread_cond_wait(&reactor->event_loop.state_changed,
                                  &reactor->event_loop.state_mutex);
            }
            pthread_mutex_unlock(&reactor->event_loop.state_mutex);
            continue;
        }
        
        /* Выполняем тик с блокирующим ожиданием стимула */
        KolibriStimulus stimulus;
        int ret = ke_stimulus_dequeue(&reactor->event_loop, &stimulus, 
                                       reactor->tick_interval_ms);
        
        if (ret == 0) {
            /* Есть стимул - обрабатываем */
            uint64_t tick_start = ke_current_time_us();
            bool any_matched = false;
            bool commit_success = true;
            
            pthread_mutex_lock(&reactor->rules_lock);
            
            for (size_t i = 0; i < reactor->rules_count; i++) {
                KeRule *rule = &reactor->rules[i];
                
                if (!rule->active) {
                    continue;
                }
                
                if (!ke_match_event_pattern(stimulus.event_type, rule->event_pattern)) {
                    continue;
                }
                
                any_matched = true;
                
                int handler_ret = rule->handler(&stimulus, reactor->genome, 
                                                 rule->user_data);
                if (handler_ret != 0) {
                    commit_success = false;
                }
            }
            
            pthread_mutex_unlock(&reactor->rules_lock);
            
            /* Записываем в геном */
            if (any_matched && reactor->genome && commit_success) {
                ReasonBlock block;
                int append_ret = kg_append(reactor->genome, stimulus.event_type,
                                           stimulus.payload, &block);
                if (append_ret == 0) {
                    stimulus.genome_index = block.index;
                    stimulus.state = KE_STIMULUS_COMMITTED;
                    
                    pthread_mutex_lock(&reactor->commit_mutex);
                    reactor->last_committed_id = stimulus.id;
                    pthread_cond_broadcast(&reactor->commit_done);
                    pthread_mutex_unlock(&reactor->commit_mutex);
                    
                    if (reactor->auto_checkpoint && reactor->genome->wal_enabled) {
                        kg_wal_checkpoint(reactor->genome);
                    }
                } else {
                    stimulus.state = KE_STIMULUS_FAILED;
                    commit_success = false;
                }
            }
            
            /* Обновляем статистику */
            uint64_t tick_time = ke_current_time_us() - tick_start;
            
            pthread_mutex_lock(&reactor->event_loop.stats_mutex);
            KeLoopStats *stats = &reactor->event_loop.stats;
            stats->total_stimuli++;
            stats->total_ticks++;
            
            if (commit_success && stimulus.state == KE_STIMULUS_COMMITTED) {
                stats->committed_stimuli++;
            } else if (stimulus.state == KE_STIMULUS_FAILED) {
                stats->failed_stimuli++;
            }
            
            if (tick_time > stats->max_tick_time_us) {
                stats->max_tick_time_us = tick_time;
            }
            if (stats->total_ticks == 1) {
                stats->avg_tick_time_us = tick_time;
            } else {
                stats->avg_tick_time_us = (stats->avg_tick_time_us * 7 + tick_time) / 8;
            }
            
            pthread_mutex_unlock(&reactor->event_loop.stats_mutex);
        }
        /* ret == 1 означает таймаут - просто продолжаем цикл */
    }
    
    return NULL;
}

int ke_reactor_run_async(KolibriRuleReactor *reactor) {
    if (!reactor) {
        return -1;
    }
    
    if (reactor->worker_started) {
        return -1;  /* Уже запущен */
    }
    
    /* Устанавливаем состояние */
    atomic_store(&reactor->event_loop.state, KE_LOOP_RUNNING);
    
    /* Запускаем рабочий поток */
    if (pthread_create(&reactor->worker_thread, NULL, 
                        ke_reactor_worker, reactor) != 0) {
        atomic_store(&reactor->event_loop.state, KE_LOOP_STOPPED);
        return -1;
    }
    
    reactor->worker_started = true;
    return 0;
}

int ke_reactor_stop(KolibriRuleReactor *reactor) {
    if (!reactor) {
        return -1;
    }
    
    if (!reactor->worker_started) {
        return 0;  /* Не запущен */
    }
    
    /* Сигнализируем об остановке */
    atomic_store(&reactor->event_loop.state, KE_LOOP_STOPPING);
    
    /* Будим все ожидающие потоки */
    pthread_mutex_lock(&reactor->event_loop.state_mutex);
    pthread_cond_broadcast(&reactor->event_loop.state_changed);
    pthread_mutex_unlock(&reactor->event_loop.state_mutex);
    
    pthread_mutex_lock(&reactor->event_loop.queue_mutex);
    pthread_cond_broadcast(&reactor->event_loop.queue_not_empty);
    pthread_mutex_unlock(&reactor->event_loop.queue_mutex);
    
    /* Ждём завершения потока */
    pthread_join(reactor->worker_thread, NULL);
    
    atomic_store(&reactor->event_loop.state, KE_LOOP_STOPPED);
    reactor->worker_started = false;
    
    return 0;
}

int ke_reactor_pause(KolibriRuleReactor *reactor) {
    if (!reactor) {
        return -1;
    }
    
    int expected = KE_LOOP_RUNNING;
    if (!atomic_compare_exchange_strong(&reactor->event_loop.state, 
                                         &expected, KE_LOOP_PAUSED)) {
        return -1;
    }
    
    return 0;
}

int ke_reactor_resume(KolibriRuleReactor *reactor) {
    if (!reactor) {
        return -1;
    }
    
    int expected = KE_LOOP_PAUSED;
    if (!atomic_compare_exchange_strong(&reactor->event_loop.state,
                                         &expected, KE_LOOP_RUNNING)) {
        return -1;
    }
    
    /* Будим рабочий поток */
    pthread_mutex_lock(&reactor->event_loop.state_mutex);
    pthread_cond_signal(&reactor->event_loop.state_changed);
    pthread_mutex_unlock(&reactor->event_loop.state_mutex);
    
    return 0;
}

bool ke_reactor_is_running(KolibriRuleReactor *reactor) {
    if (!reactor) {
        return false;
    }
    
    int state = atomic_load(&reactor->event_loop.state);
    return state == KE_LOOP_RUNNING || state == KE_LOOP_PAUSED;
}

/* --- Ожидание подтверждения записи в геном --- */

int ke_wait_genome_commit(KolibriRuleReactor *reactor,
                          uint64_t stimulus_id,
                          uint32_t timeout_ms) {
    if (!reactor) {
        return -1;
    }
    
    pthread_mutex_lock(&reactor->commit_mutex);
    
    /* Проверяем, не закоммичен ли уже */
    if (reactor->last_committed_id >= stimulus_id) {
        pthread_mutex_unlock(&reactor->commit_mutex);
        return 0;
    }
    
    if (timeout_ms == 0) {
        /* Бесконечное ожидание */
        while (reactor->last_committed_id < stimulus_id) {
            pthread_cond_wait(&reactor->commit_done, &reactor->commit_mutex);
            
            /* Проверяем состояние реактора */
            int state = atomic_load(&reactor->event_loop.state);
            if (state == KE_LOOP_STOPPING || state == KE_LOOP_STOPPED) {
                pthread_mutex_unlock(&reactor->commit_mutex);
                return -1;
            }
        }
    } else {
        /* Ожидание с таймаутом */
        struct timespec abstime;
        clock_gettime(CLOCK_REALTIME, &abstime);
        abstime.tv_sec += timeout_ms / 1000;
        abstime.tv_nsec += (timeout_ms % 1000) * 1000000L;
        if (abstime.tv_nsec >= 1000000000L) {
            abstime.tv_sec += 1;
            abstime.tv_nsec -= 1000000000L;
        }
        
        while (reactor->last_committed_id < stimulus_id) {
            int ret = pthread_cond_timedwait(&reactor->commit_done,
                                              &reactor->commit_mutex, &abstime);
            if (ret == ETIMEDOUT) {
                pthread_mutex_unlock(&reactor->commit_mutex);
                return 1;  /* Таймаут */
            }
            
            int state = atomic_load(&reactor->event_loop.state);
            if (state == KE_LOOP_STOPPING || state == KE_LOOP_STOPPED) {
                pthread_mutex_unlock(&reactor->commit_mutex);
                return -1;
            }
        }
    }
    
    pthread_mutex_unlock(&reactor->commit_mutex);
    return 0;
}

int ke_wait_all_commits(KolibriRuleReactor *reactor,
                        uint64_t up_to_id,
                        uint32_t timeout_ms) {
    /* Делегируем к основной функции ожидания */
    return ke_wait_genome_commit(reactor, up_to_id, timeout_ms);
}

uint64_t ke_get_last_committed_id(KolibriRuleReactor *reactor) {
    if (!reactor) {
        return 0;
    }
    
    pthread_mutex_lock(&reactor->commit_mutex);
    uint64_t id = reactor->last_committed_id;
    pthread_mutex_unlock(&reactor->commit_mutex);
    
    return id;
}
