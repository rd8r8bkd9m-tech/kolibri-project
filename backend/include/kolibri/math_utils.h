/* Copyright (c) 2025 Кочуров Владислав Евгеньевич */
/* ============================================================================
 * math_utils.h — Общие математические утилиты Kolibri
 *
 * Единая точка для sigmoid, gelu, softmax, layer_norm.
 * Исключает дублирование между attention.c, evolve_ffi.c, formula.c.
 * ============================================================================ */
#ifndef KOLIBRI_MATH_UTILS_H
#define KOLIBRI_MATH_UTILS_H

#include <math.h>
#include <stddef.h>

/* ---------- Ограничения ---------- */
#define KOLIBRI_MAX_ALLOC_SIZE  ((size_t)(512UL * 1024UL * 1024UL))  /* 512 MB */
#define KOLIBRI_MAX_DESERIAL_COUNT ((size_t)(16UL * 1024UL * 1024UL))  /* 16M записей */

/* ---------- Активации (float) ---------- */

static inline float kolibri_sigmoid_f(float x)
{
    if (x > 15.0f) return 1.0f;
    if (x < -15.0f) return 0.0f;
    return 1.0f / (1.0f + expf(-x));
}

static inline float kolibri_gelu_f(float x)
{
    /* Быстрое приближение GELU (tanh) */
    return 0.5f * x * (1.0f + tanhf(0.7978845608f * (x + 0.044715f * x * x * x)));
}

static inline float kolibri_relu_f(float x)
{
    return x > 0.0f ? x : 0.0f;
}

/* ---------- Активации (double, для FFI/формул) ---------- */

static inline double kolibri_sigmoid_d(double x)
{
    if (x > 15.0) return 1.0;
    if (x < -15.0) return 0.0;
    return 1.0 / (1.0 + exp(-x));
}

/* ---------- Softmax (in-place, float) ---------- */

static inline void kolibri_softmax_f(float *x, size_t n)
{
    if (n == 0) return;
    float max_val = x[0];
    for (size_t i = 1; i < n; i++)
        if (x[i] > max_val) max_val = x[i];
    float sum = 0.0f;
    for (size_t i = 0; i < n; i++) {
        x[i] = expf(x[i] - max_val);
        sum += x[i];
    }
    if (sum > 0.0f)
        for (size_t i = 0; i < n; i++)
            x[i] /= sum;
}

/* ---------- Layer Normalization ---------- */

static inline void kolibri_layer_norm_f(
    const float *input, float *output,
    const float *gamma, const float *beta,
    size_t dim
)
{
    if (dim == 0) return;
    float mean = 0.0f;
    for (size_t i = 0; i < dim; i++) mean += input[i];
    mean /= (float)dim;

    float var = 0.0f;
    for (size_t i = 0; i < dim; i++) {
        float d = input[i] - mean;
        var += d * d;
    }
    var /= (float)dim;
    float inv_std = 1.0f / sqrtf(var + 1e-5f);

    for (size_t i = 0; i < dim; i++)
        output[i] = gamma[i] * (input[i] - mean) * inv_std + beta[i];
}

/* ---------- Безопасные проверки для десериализации ---------- */

/**
 * Проверка размера перед malloc: запрещает > 512МБ и переполнение size+1.
 * Возвращает 1 если безопасно, 0 если нет.
 */
static inline int kolibri_safe_alloc_check(size_t size)
{
    return (size > 0 && size <= KOLIBRI_MAX_ALLOC_SIZE);
}

/**
 * Проверка count при десериализации массивов.
 * Возвращает 1 если безопасно, 0 если нет.
 */
static inline int kolibri_safe_count_check(size_t count, size_t element_size)
{
    if (count == 0 || element_size == 0) return 0;
    if (count > KOLIBRI_MAX_DESERIAL_COUNT) return 0;
    /* Проверка переполнения count * element_size */
    if (count > KOLIBRI_MAX_ALLOC_SIZE / element_size) return 0;
    return 1;
}

#endif /* KOLIBRI_MATH_UTILS_H */
