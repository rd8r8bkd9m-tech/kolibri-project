/*
 * test_simd_ops.c
 *
 * Тесты SIMD оптимизированных операций
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/simd_ops.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <time.h>

#define EPS 1e-4f

/* ============================================================================
 * SIMD BACKEND
 * ============================================================================ */

void test_simd_backend(void) {
    printf("Testing SIMD backend detection...\n");

    const char *backend = kat_simd_backend();
    assert(backend != NULL);
    assert(strlen(backend) > 0);

    printf("  Active backend: %s\n", backend);
    printf("✓ SIMD backend test passed\n\n");
}

/* ============================================================================
 * DOT PRODUCT
 * ============================================================================ */

void test_dot_product(void) {
    printf("Testing dot product...\n");

    /* Скалярный тест */
    float a[] = {1.0f, 2.0f, 3.0f, 4.0f};
    float b[] = {5.0f, 6.0f, 7.0f, 8.0f};

    float result = kat_vec_dot(a, b, 4);
    float expected = 1*5 + 2*6 + 3*7 + 4*8;  /* 70 */

    assert(fabsf(result - expected) < EPS);
    printf("  dot([1,2,3,4], [5,6,7,8]) = %.0f (expected 70)\n", result);

    /* Большой вектор (для AVX2/NEON) */
    int n = 1024;
    float *x = (float*)malloc(n * sizeof(float));
    float *y = (float*)malloc(n * sizeof(float));

    for (int i = 0; i < n; i++) {
        x[i] = (float)i / n;
        y[i] = (float)(n - i) / n;
    }

    result = kat_vec_dot(x, y, n);
    /* sum(i/n * (n-i)/n) = sum(i*(n-i)/n²) */
    float expected2 = 0;
    for (int i = 0; i < n; i++) expected2 += x[i] * y[i];
    assert(fabsf(result - expected2) < EPS);

    printf("  Large dot product (n=%d): %.2f\n", n, result);

    free(x);
    free(y);
    printf("✓ Dot product test passed\n\n");
}

/* ============================================================================
 * VEC ADD
 * ============================================================================ */

void test_vec_add(void) {
    printf("Testing vector add...\n");

    float a[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    float b[] = {10.0f, 20.0f, 30.0f, 40.0f, 50.0f, 60.0f, 70.0f, 80.0f};
    float c[8];

    kat_vec_add(a, b, c, 8);

    for (int i = 0; i < 8; i++) {
        assert(fabsf(c[i] - (a[i] + b[i])) < EPS);
    }
    printf("  [1,2,3,4,5,6,7,8] + [10,20,30,40,50,60,70,80] = c[0]=%.0f\n", c[0]);

    /* Большой вектор */
    int n = 1024;
    float *x = (float*)malloc(n * sizeof(float));
    float *y = (float*)malloc(n * sizeof(float));
    float *z = (float*)malloc(n * sizeof(float));

    for (int i = 0; i < n; i++) {
        x[i] = 1.0f;
        y[i] = 2.0f;
    }

    kat_vec_add(x, y, z, n);
    for (int i = 0; i < n; i++) {
        assert(fabsf(z[i] - 3.0f) < EPS);
    }

    free(x); free(y); free(z);
    printf("✓ Vector add test passed\n\n");
}

/* ============================================================================
 * VEC SCALE
 * ============================================================================ */

void test_vec_scale(void) {
    printf("Testing vector scale...\n");

    float a[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};
    kat_vec_scale(a, 2.0f, 10);

    for (int i = 0; i < 10; i++) {
        assert(fabsf(a[i] - (float)(i + 1) * 2.0f) < EPS);
    }
    printf("  [1..10] * 2 = [%.0f, %.0f, ...]\n", a[0], a[9]);

    printf("✓ Vector scale test passed\n\n");
}

/* ============================================================================
 * RMSNorm
 * ============================================================================ */

void test_rmsnorm(void) {
    printf("Testing RMSNorm...\n");

    float x[] = {1.0f, 2.0f, 3.0f, 4.0f};
    float w[] = {1.0f, 1.0f, 1.0f, 1.0f};
    float out[4];

    kat_rmsnorm(x, w, out, 4, 1e-5f);

    /* RMS = sqrt(mean(x²)) = sqrt((1+4+9+16)/4) = sqrt(7.5) */
    float rms = sqrtf(7.5f);

    for (int i = 0; i < 4; i++) {
        float expected = x[i] / rms;
        assert(fabsf(out[i] - expected) < EPS);
    }
    printf("  RMSNorm([1,2,3,4]) with unit weights: out[0]=%.4f\n", out[0]);

    printf("✓ RMSNorm test passed\n\n");
}

/* ============================================================================
 * SOFTMAX
 * ============================================================================ */

void test_softmax(void) {
    printf("Testing softmax...\n");

    float x[] = {1.0f, 2.0f, 3.0f};
    float out[3];

    kat_softmax(x, out, 3);

    /* Сумма должна быть 1 */
    float sum = out[0] + out[1] + out[2];
    assert(fabsf(sum - 1.0f) < EPS);

    /* Все значения > 0 */
    assert(out[0] > 0 && out[1] > 0 && out[2] > 0);

    /* out[2] > out[1] > out[0] (т.к. x[2] > x[1] > x[0]) */
    assert(out[2] > out[1] && out[1] > out[0]);

    printf("  softmax([1,2,3]) = [%.4f, %.4f, %.4f]\n", out[0], out[1], out[2]);

    /* Большой вектор */
    int n = 256;
    float *big = (float*)malloc(n * sizeof(float));
    float *big_out = (float*)malloc(n * sizeof(float));

    for (int i = 0; i < n; i++) big[i] = (float)i;
    kat_softmax(big, big_out, n);

    sum = 0;
    for (int i = 0; i < n; i++) sum += big_out[i];
    assert(fabsf(sum - 1.0f) < EPS);

    free(big); free(big_out);
    printf("✓ Softmax test passed\n\n");
}

/* ============================================================================
 * GELU
 * ============================================================================ */

void test_gelu(void) {
    printf("Testing GELU activation...\n");

    float x[] = {-2.0f, -1.0f, 0.0f, 1.0f, 2.0f};
    float out[5];

    kat_gelu(x, out, 5);

    /* GELU(0) = 0 */
    assert(fabsf(out[2]) < EPS);

    /* GELU(positive) > 0 */
    assert(out[3] > 0 && out[4] > 0);

    /* GELU(negative) ≈ 0 (очень маленькое) */
    assert(out[0] < 0 && fabsf(out[0]) < 0.05f);

    printf("  GELU([-2,-1,0,1,2]) = [%.4f, %.4f, %.4f, %.4f, %.4f]\n",
           out[0], out[1], out[2], out[3], out[4]);

    printf("✓ GELU test passed\n\n");
}

/* ============================================================================
 * SwiGLU
 * ============================================================================ */

void test_swiglu(void) {
    printf("Testing SwiGLU activation...\n");

    float x[] = {1.0f, 2.0f, 3.0f};
    float gate[] = {0.5f, 1.0f, 1.5f};
    float out[3];

    kat_swiglu(x, gate, out, 3);

    /* silu(x) = x * sigmoid(x), swiglu = silu(x) * gate */
    /* Для x=1: silu(1) ≈ 0.731, swiglu = 0.731 * 0.5 ≈ 0.366 */
    assert(out[0] > 0 && out[1] > 0 && out[2] > 0);

    printf("  SwiGLU([1,2,3], [0.5,1,1.5]) = [%.4f, %.4f, %.4f]\n",
           out[0], out[1], out[2]);

    printf("✓ SwiGLU test passed\n\n");
}

/* ============================================================================
 * MATRIX-VECTOR
 * ============================================================================ */

void test_matvec(void) {
    printf("Testing matrix-vector multiply...\n");

    /* 2x3 matrix */
    float A[] = {
        1.0f, 2.0f, 3.0f,
        4.0f, 5.0f, 6.0f
    };
    float x[] = {1.0f, 2.0f, 3.0f};
    float y[2];

    kat_matvec(A, x, y, 2, 3);

    /* y[0] = 1*1 + 2*2 + 3*3 = 14 */
    /* y[1] = 4*1 + 5*2 + 6*3 = 32 */
    assert(fabsf(y[0] - 14.0f) < EPS);
    assert(fabsf(y[1] - 32.0f) < EPS);

    printf("  [[1,2,3],[4,5,6]] @ [1,2,3] = [%.0f, %.0f]\n", y[0], y[1]);

    printf("✓ Matrix-vector test passed\n\n");
}

/* ============================================================================
 * BENCHMARK: SIMD vs Scalar
 * ============================================================================ */

void test_simd_benchmark(void) {
    printf("Benchmarking SIMD operations...\n");

    int n = 1000000;
    float *a = (float*)malloc(n * sizeof(float));
    float *b = (float*)malloc(n * sizeof(float));
    float *c = (float*)malloc(n * sizeof(float));

    for (int i = 0; i < n; i++) {
        a[i] = (float)rand() / RAND_MAX;
        b[i] = (float)rand() / RAND_MAX;
    }

    /* Dot product */
    struct timespec start, end;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int iter = 0; iter < 100; iter++) {
        kat_vec_dot(a, b, n);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double dot_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("  Dot product (n=%d, 100 iters): %.3fs\n", n, dot_time);

    /* Vector add */
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int iter = 0; iter < 100; iter++) {
        kat_vec_add(a, b, c, n);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double add_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("  Vector add (n=%d, 100 iters): %.3fs\n", n, add_time);
    printf("  SIMD backend: %s\n", kat_simd_backend());

    free(a); free(b); free(c);
    printf("✓ SIMD benchmark passed\n\n");
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(void) {
    printf("===========================================\n");
    printf("Kolibri SIMD Operations Tests\n");
    printf("===========================================\n\n");

    test_simd_backend();
    test_dot_product();
    test_vec_add();
    test_vec_scale();
    test_rmsnorm();
    test_softmax();
    test_gelu();
    test_swiglu();
    test_matvec();
    test_simd_benchmark();

    printf("===========================================\n");
    printf("All tests passed! ✓\n");
    printf("===========================================\n");

    return 0;
}
