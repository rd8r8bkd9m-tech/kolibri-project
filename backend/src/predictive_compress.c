/*
 * Kolibri OS — Предиктивное сжатие: реализация
 *
 * Алгоритм:
 * 1. Популяция MLP-формул предсказывает P(byte | context).
 * 2. Эволюция (мутация + скрещивание) минимизирует cross-entropy на данных.
 * 3. Лучшая формула используется для арифметического кодирования потока:
 *    - Сжатие: предсказываем → кодируем реальный байт в узком интервале.
 *    - Распаковка: предсказываем → декодируем из потока.
 *
 * Формат выходных данных:
 *   [KPCHeader][сериализованная лучшая формула][арифм. битовый поток]
 */

#include "kolibri/predictive_compress.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

/* --- PRNG (xorshift64*) --- */
static uint64_t kpc_rand(uint64_t *state) {
    uint64_t x = *state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *state = x;
    return x * 0x2545F4914F6CDD1DULL;
}

static float kpc_randf(uint64_t *state) {
    return (float)(kpc_rand(state) >> 40) / (float)(1ULL << 24);
}

/* --- Активация GELU (приближение) --- */
static float gelu(float x) {
    /* tanh-приближение: 0.5 * x * (1 + tanh(sqrt(2/π) * (x + 0.044715 * x³))) */
    float x3 = x * x * x;
    float inner = 0.7978845608f * (x + 0.044715f * x3);
    /* tanh через формулу */
    float e2 = expf(2.0f * inner);
    float th = (e2 - 1.0f) / (e2 + 1.0f);
    return 0.5f * x * (1.0f + th);
}

/* --- Softmax (in-place, stable) --- */
static void softmax(float *logits, int n) {
    float max_val = -FLT_MAX;
    for (int i = 0; i < n; i++) {
        if (logits[i] > max_val) max_val = logits[i];
    }
    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        logits[i] = expf(logits[i] - max_val);
        sum += logits[i];
    }
    if (sum > 0.0f) {
        for (int i = 0; i < n; i++) {
            logits[i] /= sum;
        }
    } else {
        /* Равномерное распределение при ошибке */
        float uniform = 1.0f / (float)n;
        for (int i = 0; i < n; i++) logits[i] = uniform;
    }
}

/* --- Forward pass одной формулы: контекст → P(byte) --- */
static void formula_forward(const KPCFormula *f,
                            const uint8_t *context,
                            float *probs) {
    float hidden[KPC_HIDDEN_SIZE];

    /* Нормализованный контекст [0..1] */
    float input[KPC_CONTEXT_SIZE];
    for (int i = 0; i < KPC_CONTEXT_SIZE; i++) {
        input[i] = (float)context[i] / 255.0f;
    }

    /* Слой 1: input -> hidden (GELU) */
    for (int h = 0; h < KPC_HIDDEN_SIZE; h++) {
        float s = f->b1[h];
        for (int c = 0; c < KPC_CONTEXT_SIZE; c++) {
            s += input[c] * f->w1[c][h];
        }
        hidden[h] = gelu(s);
    }

    /* Слой 2: hidden -> logits → softmax → probs */
    for (int v = 0; v < KPC_VOCAB_SIZE; v++) {
        float s = f->b2[v];
        for (int h = 0; h < KPC_HIDDEN_SIZE; h++) {
            s += hidden[h] * f->w2[h][v];
        }
        probs[v] = s;
    }

    softmax(probs, KPC_VOCAB_SIZE);
}

/* --- Инициализация формулы случайными весами (Xavier) --- */
static void formula_init(KPCFormula *f, uint64_t *seed) {
    float scale1 = sqrtf(2.0f / (float)(KPC_CONTEXT_SIZE + KPC_HIDDEN_SIZE));
    for (int i = 0; i < KPC_CONTEXT_SIZE; i++) {
        for (int j = 0; j < KPC_HIDDEN_SIZE; j++) {
            f->w1[i][j] = (kpc_randf(seed) * 2.0f - 1.0f) * scale1;
        }
    }
    for (int j = 0; j < KPC_HIDDEN_SIZE; j++) {
        f->b1[j] = 0.0f;
    }

    float scale2 = sqrtf(2.0f / (float)(KPC_HIDDEN_SIZE + KPC_VOCAB_SIZE));
    for (int i = 0; i < KPC_HIDDEN_SIZE; i++) {
        for (int j = 0; j < KPC_VOCAB_SIZE; j++) {
            f->w2[i][j] = (kpc_randf(seed) * 2.0f - 1.0f) * scale2;
        }
    }
    for (int j = 0; j < KPC_VOCAB_SIZE; j++) {
        f->b2[j] = 0.0f;
    }
    f->fitness = -1e9f;
}

/* --- Мутация формулы --- */
static void formula_mutate(KPCFormula *child, const KPCFormula *parent,
                           uint64_t *seed, float rate) {
    memcpy(child, parent, sizeof(KPCFormula));

    /* Мутируем веса слоя 1 */
    for (int i = 0; i < KPC_CONTEXT_SIZE; i++) {
        for (int j = 0; j < KPC_HIDDEN_SIZE; j++) {
            if (kpc_randf(seed) < rate) {
                child->w1[i][j] += (kpc_randf(seed) * 2.0f - 1.0f) * 0.1f;
            }
        }
    }
    for (int j = 0; j < KPC_HIDDEN_SIZE; j++) {
        if (kpc_randf(seed) < rate) {
            child->b1[j] += (kpc_randf(seed) * 2.0f - 1.0f) * 0.05f;
        }
    }

    /* Мутируем веса слоя 2 */
    for (int i = 0; i < KPC_HIDDEN_SIZE; i++) {
        for (int j = 0; j < KPC_VOCAB_SIZE; j++) {
            if (kpc_randf(seed) < rate) {
                child->w2[i][j] += (kpc_randf(seed) * 2.0f - 1.0f) * 0.1f;
            }
        }
    }
    for (int j = 0; j < KPC_VOCAB_SIZE; j++) {
        if (kpc_randf(seed) < rate) {
            child->b2[j] += (kpc_randf(seed) * 2.0f - 1.0f) * 0.05f;
        }
    }

    child->fitness = -1e9f;
}

/* --- Оценка формулы: средняя -log P(real byte) --- */
static float formula_evaluate(const KPCFormula *f,
                              const uint8_t *data, size_t size) {
    if (size <= KPC_CONTEXT_SIZE) return -1e9f;

    float total_log_prob = 0.0f;
    size_t count = 0;
    float probs[KPC_VOCAB_SIZE];

    /* Оцениваем на подвыборке для скорости (макс. 2000 позиций) */
    size_t step = 1;
    size_t positions = size - KPC_CONTEXT_SIZE;
    if (positions > 2000) {
        step = positions / 2000;
    }

    for (size_t i = KPC_CONTEXT_SIZE; i < size; i += step) {
        formula_forward(f, &data[i - KPC_CONTEXT_SIZE], probs);
        uint8_t actual = data[i];
        float p = probs[actual];
        if (p < 1e-10f) p = 1e-10f;
        total_log_prob += logf(p);
        count++;
    }

    return (count > 0) ? (total_log_prob / (float)count) : -1e9f;
}

/* =================================================================== */
/*                         Публичный API                               */
/* =================================================================== */

KPCContext *kpc_create(void) {
    KPCContext *ctx = (KPCContext *)calloc(1, sizeof(KPCContext));
    if (!ctx) return NULL;

    ctx->seed = 42;
    ctx->best_idx = 0;

    /* Инициализируем популяцию */
    for (int i = 0; i < KPC_POPULATION; i++) {
        formula_init(&ctx->population[i], &ctx->seed);
    }

    return ctx;
}

void kpc_destroy(KPCContext *ctx) {
    free(ctx);
}

void kpc_train(KPCContext *ctx,
               const uint8_t *data, size_t size,
               int rounds) {
    if (!ctx || !data || size <= KPC_CONTEXT_SIZE) return;
    if (rounds <= 0) rounds = KPC_EVOLVE_ROUNDS;

    /* Оценка начальной популяции */
    float best_fitness = -1e9f;
    for (int i = 0; i < KPC_POPULATION; i++) {
        ctx->population[i].fitness = formula_evaluate(&ctx->population[i], data, size);
        if (ctx->population[i].fitness > best_fitness) {
            best_fitness = ctx->population[i].fitness;
            ctx->best_idx = i;
        }
    }

    /* Эволюционный цикл */
    for (int r = 0; r < rounds; r++) {
        /* Адаптивная скорость мутации: высокая вначале, снижается */
        float mutation_rate = 0.3f * (1.0f - (float)r / (float)rounds) + 0.05f;

        /* Турнирная селекция + мутация: заменяем худшую половину */
        for (int i = 0; i < KPC_POPULATION; i++) {
            if (i == ctx->best_idx) continue; /* Элитизм: лучшая не мутирует */

            /* Турнир: 2 случайных, лучший — родитель */
            int a = (int)(kpc_rand(&ctx->seed) % KPC_POPULATION);
            int b = (int)(kpc_rand(&ctx->seed) % KPC_POPULATION);
            int parent = (ctx->population[a].fitness >= ctx->population[b].fitness) ? a : b;

            formula_mutate(&ctx->population[i], &ctx->population[parent],
                           &ctx->seed, mutation_rate);
        }

        /* Переоценка */
        for (int i = 0; i < KPC_POPULATION; i++) {
            if (i == ctx->best_idx && ctx->population[i].fitness > -1e8f) continue;
            ctx->population[i].fitness = formula_evaluate(&ctx->population[i], data, size);
            if (ctx->population[i].fitness > best_fitness) {
                best_fitness = ctx->population[i].fitness;
                ctx->best_idx = i;
            }
        }
    }

    ctx->bytes_seen += size;
}

/* --- Байтовое арифметическое кодирование --- */

/* Упрощённое арифметическое кодирование: вероятности квантованы в 16 бит */
#define AC_PRECISION  16
#define AC_FULL       (1U << AC_PRECISION)
#define AC_HALF       (AC_FULL >> 1)
#define AC_QUARTER    (AC_FULL >> 2)

typedef struct {
    uint32_t low;
    uint32_t high;
    int      pending;
    uint8_t *buf;
    size_t   buf_size;
    size_t   buf_pos;
    int      bit_pos;  /* для побитовой записи */
} ACEncoder;

typedef struct {
    uint32_t low;
    uint32_t high;
    uint32_t value;
    const uint8_t *buf;
    size_t   buf_size;
    size_t   buf_pos;
    int      bit_pos;
} ACDecoder;

/* --- Энкодер --- */
static void ac_enc_init(ACEncoder *e, uint8_t *buf, size_t buf_size) {
    e->low = 0;
    e->high = AC_FULL - 1;
    e->pending = 0;
    e->buf = buf;
    e->buf_size = buf_size;
    e->buf_pos = 0;
    e->bit_pos = 0;
}

static void ac_enc_put_bit(ACEncoder *e, int bit) {
    if (e->buf_pos >= e->buf_size) return;
    if (bit) {
        e->buf[e->buf_pos] |= (1 << (7 - e->bit_pos));
    }
    e->bit_pos++;
    if (e->bit_pos == 8) {
        e->bit_pos = 0;
        e->buf_pos++;
        if (e->buf_pos < e->buf_size) {
            e->buf[e->buf_pos] = 0;
        }
    }
}

static void ac_enc_put_bit_plus_pending(ACEncoder *e, int bit) {
    ac_enc_put_bit(e, bit);
    for (int i = 0; i < e->pending; i++) {
        ac_enc_put_bit(e, !bit);
    }
    e->pending = 0;
}

static void ac_enc_encode(ACEncoder *e, const float *cdf, int symbol) {
    /* cdf[i] = sum of probs[0..i-1], cdf[256] = 1.0 */
    uint32_t range = e->high - e->low + 1;
    uint32_t cum_low  = (uint32_t)(cdf[symbol] * AC_FULL);
    uint32_t cum_high = (uint32_t)(cdf[symbol + 1] * AC_FULL);
    if (cum_high == cum_low) cum_high = cum_low + 1; /* мин. интервал */
    if (cum_high > AC_FULL) cum_high = AC_FULL;

    e->high = e->low + (range * cum_high / AC_FULL) - 1;
    e->low  = e->low + (range * cum_low  / AC_FULL);

    for (;;) {
        if (e->high < AC_HALF) {
            ac_enc_put_bit_plus_pending(e, 0);
        } else if (e->low >= AC_HALF) {
            ac_enc_put_bit_plus_pending(e, 1);
            e->low -= AC_HALF;
            e->high -= AC_HALF;
        } else if (e->low >= AC_QUARTER && e->high < 3 * AC_QUARTER) {
            e->pending++;
            e->low -= AC_QUARTER;
            e->high -= AC_QUARTER;
        } else {
            break;
        }
        e->low <<= 1;
        e->high = (e->high << 1) | 1;
    }
}

static size_t ac_enc_finish(ACEncoder *e) {
    e->pending++;
    if (e->low < AC_QUARTER) {
        ac_enc_put_bit_plus_pending(e, 0);
    } else {
        ac_enc_put_bit_plus_pending(e, 1);
    }
    /* Дописываем последний неполный байт */
    if (e->bit_pos > 0) {
        e->buf_pos++;
    }
    return e->buf_pos;
}

/* --- Декодер --- */
static void ac_dec_init(ACDecoder *d, const uint8_t *buf, size_t buf_size) {
    d->low = 0;
    d->high = AC_FULL - 1;
    d->buf = buf;
    d->buf_size = buf_size;
    d->buf_pos = 0;
    d->bit_pos = 0;
    d->value = 0;

    /* Прочитать начальные биты */
    for (int i = 0; i < AC_PRECISION; i++) {
        d->value <<= 1;
        if (d->buf_pos < d->buf_size) {
            int bit = (d->buf[d->buf_pos] >> (7 - d->bit_pos)) & 1;
            d->value |= bit;
            d->bit_pos++;
            if (d->bit_pos == 8) {
                d->bit_pos = 0;
                d->buf_pos++;
            }
        }
    }
}

static int ac_dec_get_bit(ACDecoder *d) {
    if (d->buf_pos >= d->buf_size) return 0;
    int bit = (d->buf[d->buf_pos] >> (7 - d->bit_pos)) & 1;
    d->bit_pos++;
    if (d->bit_pos == 8) {
        d->bit_pos = 0;
        d->buf_pos++;
    }
    return bit;
}

static int ac_dec_decode(ACDecoder *d, const float *cdf) {
    uint32_t range = d->high - d->low + 1;
    uint32_t scaled = ((d->value - d->low + 1) * AC_FULL - 1) / range;

    /* Бинарный поиск символа */
    int symbol = 0;
    for (int i = 0; i < KPC_VOCAB_SIZE; i++) {
        uint32_t cum = (uint32_t)(cdf[i + 1] * AC_FULL);
        if (scaled < cum) {
            symbol = i;
            break;
        }
    }

    /* Обновляем интервал */
    uint32_t cum_low  = (uint32_t)(cdf[symbol] * AC_FULL);
    uint32_t cum_high = (uint32_t)(cdf[symbol + 1] * AC_FULL);
    if (cum_high == cum_low) cum_high = cum_low + 1;
    if (cum_high > AC_FULL) cum_high = AC_FULL;

    d->high = d->low + (range * cum_high / AC_FULL) - 1;
    d->low  = d->low + (range * cum_low  / AC_FULL);

    for (;;) {
        if (d->high < AC_HALF) {
            /* ничего */
        } else if (d->low >= AC_HALF) {
            d->low -= AC_HALF;
            d->high -= AC_HALF;
            d->value -= AC_HALF;
        } else if (d->low >= AC_QUARTER && d->high < 3 * AC_QUARTER) {
            d->low -= AC_QUARTER;
            d->high -= AC_QUARTER;
            d->value -= AC_QUARTER;
        } else {
            break;
        }
        d->low <<= 1;
        d->high = (d->high << 1) | 1;
        d->value = (d->value << 1) | ac_dec_get_bit(d);
    }

    return symbol;
}

/* --- Сериализация формулы --- */
static size_t formula_serialized_size(void) {
    return sizeof(float) * (KPC_CONTEXT_SIZE * KPC_HIDDEN_SIZE + KPC_HIDDEN_SIZE +
                            KPC_HIDDEN_SIZE * KPC_VOCAB_SIZE + KPC_VOCAB_SIZE);
}

static void formula_serialize(const KPCFormula *f, uint8_t *buf) {
    size_t off = 0;
    memcpy(buf + off, f->w1, sizeof(f->w1)); off += sizeof(f->w1);
    memcpy(buf + off, f->b1, sizeof(f->b1)); off += sizeof(f->b1);
    memcpy(buf + off, f->w2, sizeof(f->w2)); off += sizeof(f->w2);
    memcpy(buf + off, f->b2, sizeof(f->b2)); off += sizeof(f->b2);
}

static void formula_deserialize(KPCFormula *f, const uint8_t *buf) {
    size_t off = 0;
    memcpy(f->w1, buf + off, sizeof(f->w1)); off += sizeof(f->w1);
    memcpy(f->b1, buf + off, sizeof(f->b1)); off += sizeof(f->b1);
    memcpy(f->w2, buf + off, sizeof(f->w2)); off += sizeof(f->w2);
    memcpy(f->b2, buf + off, sizeof(f->b2)); off += sizeof(f->b2);
    f->fitness = 0.0f;
}

/* --- Простой CRC32 для заголовка --- */
static uint32_t kpc_crc32(const uint8_t *data, size_t size) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return crc ^ 0xFFFFFFFF;
}

/* --- CDF из вероятностей --- */
static void probs_to_cdf(const float *probs, float *cdf) {
    cdf[0] = 0.0f;
    for (int i = 0; i < KPC_VOCAB_SIZE; i++) {
        float p = probs[i];
        if (p < 1e-7f) p = 1e-7f; /* минимальная вероятность */
        cdf[i + 1] = cdf[i] + p;
    }
    /* Нормализуем до 1.0 */
    float total = cdf[KPC_VOCAB_SIZE];
    if (total > 0.0f) {
        for (int i = 1; i <= KPC_VOCAB_SIZE; i++) {
            cdf[i] /= total;
        }
    }
    cdf[KPC_VOCAB_SIZE] = 1.0f; /* гарантия */
}

/* =================================================================== */
/*                   COMPRESS / DECOMPRESS                             */
/* =================================================================== */

int kpc_compress(KPCContext *ctx,
                 const uint8_t *input, size_t input_size,
                 uint8_t **output, size_t *output_size) {
    if (!ctx || !input || !input_size || !output || !output_size) return -1;

    const KPCFormula *best = &ctx->population[ctx->best_idx];
    size_t formula_sz = formula_serialized_size();

    /* Максимальный размер выхода: заголовок + формула + данные * 2 (worst case) */
    size_t max_out = sizeof(KPCHeader) + formula_sz + input_size * 2 + 256;
    uint8_t *buf = (uint8_t *)calloc(1, max_out);
    if (!buf) return -2;

    /* Сериализуем формулу после заголовка */
    formula_serialize(best, buf + sizeof(KPCHeader));

    /* Арифметическое кодирование с предсказаниями */
    ACEncoder enc;
    ac_enc_init(&enc, buf + sizeof(KPCHeader) + formula_sz,
                max_out - sizeof(KPCHeader) - formula_sz);

    /* Контекстный буфер (с нулевым заполнением для начала) */
    uint8_t ctx_buf[KPC_CONTEXT_SIZE];
    memset(ctx_buf, 0, KPC_CONTEXT_SIZE);

    float probs[KPC_VOCAB_SIZE];
    float cdf[KPC_VOCAB_SIZE + 1];

    for (size_t i = 0; i < input_size; i++) {
        formula_forward(best, ctx_buf, probs);
        probs_to_cdf(probs, cdf);
        ac_enc_encode(&enc, cdf, input[i]);

        /* Сдвигаем контекст */
        memmove(ctx_buf, ctx_buf + 1, KPC_CONTEXT_SIZE - 1);
        ctx_buf[KPC_CONTEXT_SIZE - 1] = input[i];
    }

    size_t ac_size = ac_enc_finish(&enc);

    /* Заполняем заголовок */
    KPCHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = KPC_MAGIC;
    hdr.version = KPC_VERSION;
    hdr.original_size = (uint32_t)input_size;
    hdr.compressed_size = (uint32_t)(sizeof(KPCHeader) + formula_sz + ac_size);
    hdr.checksum = kpc_crc32(input, input_size);
    memcpy(buf, &hdr, sizeof(KPCHeader));

    *output = buf;
    *output_size = hdr.compressed_size;
    return 0;
}

int kpc_decompress(const uint8_t *input, size_t input_size,
                   uint8_t **output, size_t *output_size) {
    if (!input || !output || !output_size) return -1;
    if (input_size < sizeof(KPCHeader)) return -3;

    /* Читаем заголовок */
    KPCHeader hdr;
    memcpy(&hdr, input, sizeof(KPCHeader));

    if (hdr.magic != KPC_MAGIC) return -4;
    if (hdr.version != KPC_VERSION) return -5;

    size_t formula_sz = formula_serialized_size();
    if (input_size < sizeof(KPCHeader) + formula_sz) return -6;

    /* Десериализуем формулу */
    KPCFormula formula;
    formula_deserialize(&formula, input + sizeof(KPCHeader));

    /* Выделяем выход */
    uint8_t *out = (uint8_t *)malloc(hdr.original_size);
    if (!out) return -2;

    /* Арифметическое декодирование */
    ACDecoder dec;
    ac_dec_init(&dec,
                input + sizeof(KPCHeader) + formula_sz,
                input_size - sizeof(KPCHeader) - formula_sz);

    uint8_t ctx_buf[KPC_CONTEXT_SIZE];
    memset(ctx_buf, 0, KPC_CONTEXT_SIZE);
    float probs[KPC_VOCAB_SIZE];
    float cdf[KPC_VOCAB_SIZE + 1];

    for (uint32_t i = 0; i < hdr.original_size; i++) {
        formula_forward(&formula, ctx_buf, probs);
        probs_to_cdf(probs, cdf);
        int sym = ac_dec_decode(&dec, cdf);
        out[i] = (uint8_t)sym;

        memmove(ctx_buf, ctx_buf + 1, KPC_CONTEXT_SIZE - 1);
        ctx_buf[KPC_CONTEXT_SIZE - 1] = (uint8_t)sym;
    }

    /* Проверяем контрольную сумму */
    uint32_t check = kpc_crc32(out, hdr.original_size);
    if (check != hdr.checksum) {
        free(out);
        return -7; /* повреждённые данные */
    }

    *output = out;
    *output_size = hdr.original_size;
    return 0;
}

double kpc_get_ratio(const KPCContext *ctx) {
    if (!ctx || ctx->best_idx < 0) return 0.0;
    const KPCFormula *best = &ctx->population[ctx->best_idx];
    return (double)best->fitness;
}
