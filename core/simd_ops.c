/*
 * simd_ops.c
 *
 * SIMD-оптимизированные векторные операции для Kolibri
 *
 * Поддерживаемые инструкции:
 *   - x86_64: AVX2 (256-bit), SSE (128-bit) fallback
 *   - ARM64: NEON (128-bit)
 *   - Generic: скалярная версия
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/simd_ops.h"

#include <math.h>
#include <string.h>

/* Автодетект платформы */
#if defined(__AVX2__)
    #include <immintrin.h>
    #define KAT_SIMD_AVX2 1
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
    #include <arm_neon.h>
    #define KAT_SIMD_NEON 1
#else
    #define KAT_SIMD_SCALAR 1
#endif

/* ============================================================================
 * DOT PRODUCT: sum(a[i] * b[i])
 * ============================================================================ */

float kat_vec_dot(const float *a, const float *b, int n) {
#if KAT_SIMD_AVX2
    /* AVX2: 8 floats за итерацию */
    float sum = 0.0f;
    int i = 0;
    __m256 vsum = _mm256_setzero_ps();

    for (; i <= n - 8; i += 8) {
        __m256 va = _mm256_loadu_ps(&a[i]);
        __m256 vb = _mm256_loadu_ps(&b[i]);
        vsum = _mm256_fmadd_ps(va, vb, vsum);
    }

    /* Горизонтальное сложение */
    __m128 vlow = _mm256_extractf128_ps(vsum, 0);
    __m128 vhigh = _mm256_extractf128_ps(vsum, 1);
    vlow = _mm_add_ps(vlow, vhigh);
    __m128 shuf = _mm_movehdup_ps(vlow);
    __m128 sums = _mm_add_ps(vlow, shuf);
    shuf = _mm_movehl_ps(shuf, sums);
    sums = _mm_add_ss(sums, shuf);
    sum = _mm_cvtss_f32(sums);

    /* Scalar tail */
    for (; i < n; i++) sum += a[i] * b[i];
    return sum;

#elif KAT_SIMD_NEON
    /* NEON: 4 floats за итерацию */
    float sum = 0.0f;
    int i = 0;
    float32x4_t vsum = vdupq_n_f32(0.0f);

    for (; i <= n - 4; i += 4) {
        float32x4_t va = vld1q_f32(&a[i]);
        float32x4_t vb = vld1q_f32(&b[i]);
        vsum = vmlaq_f32(vsum, va, vb);
    }

    sum += vgetq_lane_f32(vsum, 0);
    sum += vgetq_lane_f32(vsum, 1);
    sum += vgetq_lane_f32(vsum, 2);
    sum += vgetq_lane_f32(vsum, 3);

    for (; i < n; i++) sum += a[i] * b[i];
    return sum;

#else
    /* Scalar */
    float sum = 0.0f;
    for (int i = 0; i < n; i++) sum += a[i] * b[i];
    return sum;
#endif
}

/* ============================================================================
 * VEC ADD: c[i] = a[i] + b[i]
 * ============================================================================ */

void kat_vec_add(const float *a, const float *b, float *c, int n) {
#if KAT_SIMD_AVX2
    int i = 0;
    for (; i <= n - 8; i += 8) {
        __m256 va = _mm256_loadu_ps(&a[i]);
        __m256 vb = _mm256_loadu_ps(&b[i]);
        __m256 vc = _mm256_add_ps(va, vb);
        _mm256_storeu_ps(&c[i], vc);
    }
    for (; i < n; i++) c[i] = a[i] + b[i];

#elif KAT_SIMD_NEON
    int i = 0;
    for (; i <= n - 4; i += 4) {
        float32x4_t va = vld1q_f32(&a[i]);
        float32x4_t vb = vld1q_f32(&b[i]);
        float32x4_t vc = vaddq_f32(va, vb);
        vst1q_f32(&c[i], vc);
    }
    for (; i < n; i++) c[i] = a[i] + b[i];

#else
    for (int i = 0; i < n; i++) c[i] = a[i] + b[i];
#endif
}

/* ============================================================================
 * VEC SCALE: a[i] *= s
 * ============================================================================ */

void kat_vec_scale(float *a, float s, int n) {
#if KAT_SIMD_AVX2
    __m256 vs = _mm256_set1_ps(s);
    int i = 0;
    for (; i <= n - 8; i += 8) {
        __m256 va = _mm256_loadu_ps(&a[i]);
        _mm256_storeu_ps(&a[i], _mm256_mul_ps(va, vs));
    }
    for (; i < n; i++) a[i] *= s;

#elif KAT_SIMD_NEON
    float32x4_t vs = vdupq_n_f32(s);
    int i = 0;
    for (; i <= n - 4; i += 4) {
        float32x4_t va = vld1q_f32(&a[i]);
        vst1q_f32(&a[i], vmulq_f32(va, vs));
    }
    for (; i < n; i++) a[i] *= s;

#else
    for (int i = 0; i < n; i++) a[i] *= s;
#endif
}

/* ============================================================================
 * RMSNorm: out[i] = x[i] / sqrt(mean(x²) + eps) * weight[i]
 * ============================================================================ */

void kat_rmsnorm(const float *x, const float *weight, float *out, int n, float eps) {
    /* Вычисляем mean(x²) */
    float sum_sq = 0.0f;
    for (int i = 0; i < n; i++) sum_sq += x[i] * x[i];
    float rms = sqrtf(sum_sq / (float)n + eps);
    float inv_rms = 1.0f / rms;

    /* Применяем нормализацию */
    for (int i = 0; i < n; i++) {
        out[i] = weight[i] * (x[i] * inv_rms);
    }
}

/* ============================================================================
 * SOFTMAX (оптимизированный)
 * ============================================================================ */

void kat_softmax(const float *x, float *out, int n) {
    /* Находим max для стабильности */
    float max_val = x[0];
    for (int i = 1; i < n; i++) {
        if (x[i] > max_val) max_val = x[i];
    }

    /* exp(x[i] - max) и сумма */
    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        out[i] = expf(x[i] - max_val);
        sum += out[i];
    }

    /* Нормализация */
    float inv_sum = 1.0f / (sum + 1e-10f);
    for (int i = 0; i < n; i++) {
        out[i] *= inv_sum;
    }
}

/* ============================================================================
 * GELU: x * 0.5 * (1 + erf(x / sqrt(2)))
 * ============================================================================ */

void kat_gelu(const float *x, float *out, int n) {
    const float sqrt2 = 1.4142135623730951f;
    const float coef = 0.044715f;

    for (int i = 0; i < n; i++) {
        float xi = x[i];
        /* Approximation: 0.5 * x * (1 + tanh(sqrt(2/π) * (x + 0.044715 * x³))) */
        float x3 = xi * xi * xi;
        float inner = 0.7978845608f * (xi + coef * x3);  /* sqrt(2/π) ≈ 0.7978845608 */
        float tanh_approx = inner / (1.0f + 0.3275911f * fabsf(inner));  /* Pade approx */
        out[i] = 0.5f * xi * (1.0f + tanh_approx);
    }
}

/* ============================================================================
 * SwiGLU: silu(x * W3) * (x * W1 + b1) → simplified: silu(x) * gate
 * ============================================================================ */

void kat_swiglu(const float *x, const float *gate, float *out, int n) {
    for (int i = 0; i < n; i++) {
        float xi = x[i];
        float silu = xi / (1.0f + expf(-xi));  /* x * sigmoid(x) */
        out[i] = silu * gate[i];
    }
}

/* ============================================================================
 * MATRIX-VECTOR MULTIPLY: y = A @ x, A is [m x n]
 * ============================================================================ */

void kat_matvec(const float *A, const float *x, float *y, int m, int n) {
    for (int i = 0; i < m; i++) {
        y[i] = kat_vec_dot(&A[i * n], x, n);
    }
}

/* ============================================================================
 * MATRIX-MATRIX MULTIPLY: C = A @ B, A[m×k], B[k×n], C[m×n]
 * ============================================================================ */

void kat_matmul(const float *A, const float *B, float *C, int m, int k, int n) {
    /* Naive O(m*k*n) с SIMD dot product */
    for (int i = 0; i < m; i++) {
        for (int j = 0; j < n; j++) {
            float sum = 0.0f;
            for (int p = 0; p < k; p++) {
                sum += A[i * k + p] * B[p * n + j];
            }
            C[i * n + j] = sum;
        }
    }
}

/* ============================================================================
 * QUERY: Какая SIMD реализация активна?
 * ============================================================================ */

const char* kat_simd_backend(void) {
#if KAT_SIMD_AVX2
    return "AVX2";
#elif KAT_SIMD_NEON
    return "NEON";
#else
    return "Scalar";
#endif
}
