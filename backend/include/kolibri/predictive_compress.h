/*
 * Kolibri OS — Предиктивное сжатие с формульным предсказателем
 *
 * Нейросетевой предсказатель (3-слойный MLP) моделирует распределение
 * следующего байта по контексту. Арифметическое кодирование сжимает
 * поток, используя эти вероятности. Эволюция весов (мутации «формул»)
 * адаптирует модель к данным без обратного распространения.
 */

#ifndef KOLIBRI_PREDICTIVE_COMPRESS_H
#define KOLIBRI_PREDICTIVE_COMPRESS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Параметры архитектуры --- */
#define KPC_CONTEXT_SIZE   8     /* байтов контекста для предсказания      */
#define KPC_HIDDEN_SIZE    64    /* нейронов в скрытом слое                */
#define KPC_VOCAB_SIZE     256   /* количество уникальных байтов           */
#define KPC_POPULATION     16    /* формул в популяции                     */
#define KPC_EVOLVE_ROUNDS  10    /* раундов эволюции при адаптации         */

/* Магические числа формата */
#define KPC_MAGIC          0x4B504300  /* "KPC\0" */
#define KPC_VERSION        1

/* --- Формула-предсказатель (один «геном») --- */
typedef struct {
    /* Слой 1: context(KPC_CONTEXT_SIZE) -> hidden(KPC_HIDDEN_SIZE) */
    float w1[KPC_CONTEXT_SIZE][KPC_HIDDEN_SIZE];
    float b1[KPC_HIDDEN_SIZE];

    /* Слой 2: hidden -> vocab(KPC_VOCAB_SIZE) logits */
    float w2[KPC_HIDDEN_SIZE][KPC_VOCAB_SIZE];
    float b2[KPC_VOCAB_SIZE];

    float fitness;  /* лучше = ниже cross-entropy */
} KPCFormula;

/* --- Контекст предиктивного компрессора --- */
typedef struct {
    KPCFormula population[KPC_POPULATION];
    int        best_idx;       /* индекс лучшей формулы        */
    uint64_t   bytes_seen;     /* статистика                    */
    uint64_t   seed;           /* PRNG состояние                */
} KPCContext;

/* --- Заголовок сжатых данных --- */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t original_size;
    uint32_t compressed_size;
    uint32_t checksum;
    uint8_t  reserved[12];
} KPCHeader;

/* --- Основной API --- */

/**
 * Создать контекст предиктивного компрессора
 * @return контекст или NULL
 */
KPCContext *kpc_create(void);

/**
 * Освободить контекст
 */
void kpc_destroy(KPCContext *ctx);

/**
 * Адаптировать модель к данным (эволюция формул)
 * @param ctx   Контекст
 * @param data  Обучающие данные
 * @param size  Размер данных
 * @param rounds Количество раундов эволюции (0 = KPC_EVOLVE_ROUNDS)
 */
void kpc_train(KPCContext *ctx,
               const uint8_t *data, size_t size,
               int rounds);

/**
 * Сжать данные с предсказательным кодированием
 * @param ctx         Контекст (должен быть адаптирован)
 * @param input       Входные данные
 * @param input_size  Размер входных данных
 * @param output      Указатель на выходной буфер (выделяется внутри)
 * @param output_size Размер выходных данных
 * @return 0 — успех, <0 — ошибка
 */
int kpc_compress(KPCContext *ctx,
                 const uint8_t *input, size_t input_size,
                 uint8_t **output, size_t *output_size);

/**
 * Распаковать данные
 * @param input       Сжатые данные (с заголовком KPCHeader)
 * @param input_size  Размер сжатых данных
 * @param output      Указатель на выходной буфер (выделяется внутри)
 * @param output_size Размер выходных данных
 * @return 0 — успех, <0 — ошибка
 */
int kpc_decompress(const uint8_t *input, size_t input_size,
                   uint8_t **output, size_t *output_size);

/**
 * Получить коэффициент сжатия последней операции
 */
double kpc_get_ratio(const KPCContext *ctx);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_PREDICTIVE_COMPRESS_H */
