/*
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 *
 * Асинхронный исполнитель правил для Kolibri OS.
 * Реализует разделение вычислений и коммуникации:
 * - Реактор правил работает в отдельной петле событий
 * - Сетевой слой буферизует входящие стимулы до подтверждения записи в геном
 */

#ifndef KOLIBRI_ASYNC_EXECUTOR_H
#define KOLIBRI_ASYNC_EXECUTOR_H

#include "kolibri/genome.h"
#include "kolibri/script.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Константы модуля --- */
#define KE_MAX_STIMULUS_QUEUE    256U   /* Максимальный размер очереди стимулов */
#define KE_MAX_STIMULUS_PAYLOAD  1024U  /* Максимальный размер данных стимула */
#define KE_MAX_RULES             128U   /* Максимальное число правил в реакторе */
#define KE_COMMIT_TIMEOUT_MS     5000U  /* Таймаут ожидания коммита (мс) */

/* --- Типы приоритетов стимулов --- */
typedef enum {
    KE_PRIORITY_LOW    = 0,
    KE_PRIORITY_NORMAL = 1,
    KE_PRIORITY_HIGH   = 2,
    KE_PRIORITY_URGENT = 3
} KeStimulusPriority;

/* --- Состояние стимула --- */
typedef enum {
    KE_STIMULUS_PENDING   = 0,  /* Ожидает обработки */
    KE_STIMULUS_PROCESSING,     /* В процессе обработки */
    KE_STIMULUS_COMMITTED,      /* Записан в геном */
    KE_STIMULUS_FAILED          /* Ошибка обработки */
} KeStimulusState;

/* --- Входящий стимул с буферизацией --- */
typedef struct {
    uint64_t id;                            /* Уникальный идентификатор */
    uint64_t timestamp;                     /* Время создания (мс) */
    KeStimulusPriority priority;            /* Приоритет обработки */
    KeStimulusState state;                  /* Текущее состояние */
    char source[64];                        /* Источник стимула (IP, модуль) */
    char event_type[32];                    /* Тип события */
    char payload[KE_MAX_STIMULUS_PAYLOAD];  /* Данные стимула */
    size_t payload_len;                     /* Длина данных */
    uint64_t genome_index;                  /* Индекс блока в геноме (после коммита) */
} KolibriStimulus;

/* --- Тип функции-обработчика правила --- */
typedef int (*KeRuleHandler)(const KolibriStimulus *stimulus,
                             KolibriGenome *genome,
                             void *user_data);

/* --- Правило реактора --- */
typedef struct {
    char name[64];              /* Имя правила */
    char event_pattern[64];     /* Паттерн событий (glob или regex) */
    KeRuleHandler handler;      /* Функция-обработчик */
    void *user_data;            /* Пользовательские данные */
    bool active;                /* Активно ли правило */
    uint64_t invocations;       /* Счётчик вызовов */
    uint64_t failures;          /* Счётчик ошибок */
} KeRule;

/* --- Состояние петли событий --- */
typedef enum {
    KE_LOOP_STOPPED = 0,
    KE_LOOP_RUNNING,
    KE_LOOP_PAUSED,
    KE_LOOP_STOPPING
} KeLoopState;

/* --- Статистика петли событий --- */
typedef struct {
    uint64_t total_stimuli;         /* Всего обработано стимулов */
    uint64_t committed_stimuli;     /* Успешно записано в геном */
    uint64_t failed_stimuli;        /* Ошибки обработки */
    uint64_t dropped_stimuli;       /* Отброшено (переполнение) */
    uint64_t total_ticks;           /* Всего тиков реактора */
    uint64_t avg_tick_time_us;      /* Среднее время тика (мкс) */
    uint64_t max_tick_time_us;      /* Максимальное время тика (мкс) */
    size_t queue_depth;             /* Текущая глубина очереди */
} KeLoopStats;

/* --- Петля событий (Event Loop) --- */
typedef struct {
    /* Очередь стимулов (кольцевой буфер) */
    KolibriStimulus queue[KE_MAX_STIMULUS_QUEUE];
    size_t queue_head;
    size_t queue_tail;
    size_t queue_count;
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_not_empty;
    pthread_cond_t queue_not_full;
    
    /* Состояние */
    atomic_int state;
    atomic_uint_fast64_t next_stimulus_id;
    
    /* Синхронизация */
    pthread_mutex_t state_mutex;
    pthread_cond_t state_changed;
    
    /* Статистика */
    KeLoopStats stats;
    pthread_mutex_t stats_mutex;
} KolibriEventLoop;

/* --- Реактор правил --- */
typedef struct {
    /* Связь с геномом и скриптом */
    KolibriGenome *genome;
    KolibriScript *script;
    
    /* Петля событий */
    KolibriEventLoop event_loop;
    
    /* Правила */
    KeRule rules[KE_MAX_RULES];
    size_t rules_count;
    pthread_mutex_t rules_lock;
    
    /* Рабочий поток */
    pthread_t worker_thread;
    bool worker_started;
    
    /* Ожидание коммитов */
    pthread_mutex_t commit_mutex;
    pthread_cond_t commit_done;
    uint64_t last_committed_id;
    
    /* Конфигурация */
    uint32_t tick_interval_ms;      /* Интервал между тиками (мс) */
    uint32_t commit_timeout_ms;     /* Таймаут ожидания коммита (мс) */
    bool auto_checkpoint;           /* Авто-checkpoint WAL */
} KolibriRuleReactor;

/* --- Управление петлёй событий --- */

/**
 * Инициализирует петлю событий.
 * @param loop Указатель на структуру петли
 * @return 0 при успехе, -1 при ошибке
 */
int ke_loop_init(KolibriEventLoop *loop);

/**
 * Освобождает ресурсы петли событий.
 * @param loop Указатель на структуру петли
 */
void ke_loop_free(KolibriEventLoop *loop);

/**
 * Получает текущую статистику петли.
 * @param loop Указатель на структуру петли
 * @param stats Указатель для записи статистики
 * @return 0 при успехе, -1 при ошибке
 */
int ke_loop_get_stats(KolibriEventLoop *loop, KeLoopStats *stats);

/* --- Буферизация стимулов --- */

/**
 * Добавляет стимул в очередь (потокобезопасно).
 * @param loop Указатель на петлю событий
 * @param event_type Тип события
 * @param payload Данные стимула
 * @param payload_len Длина данных
 * @param priority Приоритет
 * @param source Источник стимула
 * @param out_id Выходной ID стимула (может быть NULL)
 * @return 0 при успехе, -1 при переполнении, -2 при ошибке
 */
int ke_stimulus_queue(KolibriEventLoop *loop,
                      const char *event_type,
                      const char *payload,
                      size_t payload_len,
                      KeStimulusPriority priority,
                      const char *source,
                      uint64_t *out_id);

/**
 * Извлекает стимул из очереди (потокобезопасно).
 * Блокируется, если очередь пуста.
 * @param loop Указатель на петлю событий
 * @param out_stimulus Указатель для записи стимула
 * @param timeout_ms Таймаут ожидания (0 = без ожидания, -1 = бесконечно)
 * @return 0 при успехе, 1 при таймауте, -1 при ошибке
 */
int ke_stimulus_dequeue(KolibriEventLoop *loop,
                        KolibriStimulus *out_stimulus,
                        int timeout_ms);

/**
 * Проверяет, пуста ли очередь.
 * @param loop Указатель на петлю событий
 * @return true если пуста
 */
bool ke_stimulus_queue_empty(KolibriEventLoop *loop);

/**
 * Возвращает текущий размер очереди.
 * @param loop Указатель на петлю событий
 * @return Количество стимулов в очереди
 */
size_t ke_stimulus_queue_size(KolibriEventLoop *loop);

/* --- Управление реактором правил --- */

/**
 * Инициализирует реактор правил.
 * @param reactor Указатель на структуру реактора
 * @param genome Указатель на геном (для записи)
 * @param script Указатель на скрипт (может быть NULL)
 * @return 0 при успехе, -1 при ошибке
 */
int ke_reactor_init(KolibriRuleReactor *reactor,
                    KolibriGenome *genome,
                    KolibriScript *script);

/**
 * Освобождает ресурсы реактора.
 * @param reactor Указатель на структуру реактора
 */
void ke_reactor_free(KolibriRuleReactor *reactor);

/**
 * Добавляет правило в реактор.
 * @param reactor Указатель на реактор
 * @param name Имя правила
 * @param event_pattern Паттерн событий
 * @param handler Функция-обработчик
 * @param user_data Пользовательские данные
 * @return Индекс правила при успехе, -1 при ошибке
 */
int ke_reactor_add_rule(KolibriRuleReactor *reactor,
                        const char *name,
                        const char *event_pattern,
                        KeRuleHandler handler,
                        void *user_data);

/**
 * Удаляет правило по индексу.
 * @param reactor Указатель на реактор
 * @param rule_index Индекс правила
 * @return 0 при успехе, -1 при ошибке
 */
int ke_reactor_remove_rule(KolibriRuleReactor *reactor, size_t rule_index);

/**
 * Активирует/деактивирует правило.
 * @param reactor Указатель на реактор
 * @param rule_index Индекс правила
 * @param active Новое состояние
 * @return 0 при успехе, -1 при ошибке
 */
int ke_reactor_set_rule_active(KolibriRuleReactor *reactor,
                               size_t rule_index,
                               bool active);

/**
 * Выполняет один шаг (тик) реактора.
 * Обрабатывает один стимул из очереди.
 * @param reactor Указатель на реактор
 * @return 1 если обработан стимул, 0 если очередь пуста, -1 при ошибке
 */
int ke_reactor_tick(KolibriRuleReactor *reactor);

/**
 * Запускает реактор в отдельном потоке.
 * @param reactor Указатель на реактор
 * @return 0 при успехе, -1 при ошибке
 */
int ke_reactor_run_async(KolibriRuleReactor *reactor);

/**
 * Останавливает асинхронный реактор.
 * @param reactor Указатель на реактор
 * @return 0 при успехе, -1 при ошибке
 */
int ke_reactor_stop(KolibriRuleReactor *reactor);

/**
 * Приостанавливает обработку стимулов.
 * @param reactor Указатель на реактор
 * @return 0 при успехе, -1 при ошибке
 */
int ke_reactor_pause(KolibriRuleReactor *reactor);

/**
 * Возобновляет обработку стимулов.
 * @param reactor Указатель на реактор
 * @return 0 при успехе, -1 при ошибке
 */
int ke_reactor_resume(KolibriRuleReactor *reactor);

/**
 * Проверяет, запущен ли реактор.
 * @param reactor Указатель на реактор
 * @return true если запущен
 */
bool ke_reactor_is_running(KolibriRuleReactor *reactor);

/* --- Ожидание подтверждения записи в геном --- */

/**
 * Ожидает подтверждения записи стимула в геном.
 * @param reactor Указатель на реактор
 * @param stimulus_id ID стимула для ожидания
 * @param timeout_ms Таймаут ожидания в миллисекундах (0 = бесконечно)
 * @return 0 при успехе, 1 при таймауте, -1 при ошибке
 */
int ke_wait_genome_commit(KolibriRuleReactor *reactor,
                          uint64_t stimulus_id,
                          uint32_t timeout_ms);

/**
 * Ожидает подтверждения записи всех стимулов до указанного ID.
 * @param reactor Указатель на реактор
 * @param up_to_id ID стимула (включительно)
 * @param timeout_ms Таймаут ожидания
 * @return 0 при успехе, 1 при таймауте, -1 при ошибке
 */
int ke_wait_all_commits(KolibriRuleReactor *reactor,
                        uint64_t up_to_id,
                        uint32_t timeout_ms);

/**
 * Получает ID последнего закоммиченного стимула.
 * @param reactor Указатель на реактор
 * @return ID последнего закоммиченного стимула
 */
uint64_t ke_get_last_committed_id(KolibriRuleReactor *reactor);

/* --- Вспомогательные функции --- */

/**
 * Получает текущее время в миллисекундах.
 * @return Время в миллисекундах
 */
uint64_t ke_current_time_ms(void);

/**
 * Проверяет соответствие типа события паттерну.
 * @param event_type Тип события
 * @param pattern Паттерн (поддерживает * и ?)
 * @return true если соответствует
 */
bool ke_match_event_pattern(const char *event_type, const char *pattern);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_ASYNC_EXECUTOR_H */
