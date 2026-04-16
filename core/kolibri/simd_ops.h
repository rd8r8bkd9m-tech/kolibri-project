/*
 * simd_ops.h
 *
 * SIMD-оптимизированные векторные операции
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#ifndef KOLIBRI_SIMD_OPS_H
#define KOLIBRI_SIMD_OPS_H

#ifdef __cplusplus
extern "C" {
#endif

/** Dot product: sum(a[i] * b[i]) */
float kat_vec_dot(const float *a, const float *b, int n);

/** Vector add: c[i] = a[i] + b[i] */
void kat_vec_add(const float *a, const float *b, float *c, int n);

/** Vector scale: a[i] *= s */
void kat_vec_scale(float *a, float s, int n);

/** RMSNorm: out[i] = x[i] / sqrt(mean(x²) + eps) * weight[i] */
void kat_rmsnorm(const float *x, const float *weight, float *out, int n, float eps);

/** Softmax */
void kat_softmax(const float *x, float *out, int n);

/** GELU activation */
void kat_gelu(const float *x, float *out, int n);

/** SwiGLU activation */
void kat_swiglu(const float *x, const float *gate, float *out, int n);

/** Matrix-vector multiply: y = A @ x */
void kat_matvec(const float *A, const float *x, float *y, int m, int n);

/** Matrix-matrix multiply: C = A @ B */
void kat_matmul(const float *A, const float *B, float *C, int m, int k, int n);

/** Вернуть имя активного SIMD бэкенда */
const char* kat_simd_backend(void);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_SIMD_OPS_H */
