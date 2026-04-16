/*
 * math_engine.c — Mathematical Engine for Kolibri AI
 *
 * Symbolic mathematics: equations, derivatives, integrals, matrices, trig
 *
 * Copyright (c) 2026 Кочуров Владислав Евгеньевич
 */

#include "kolibri/math_engine.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========== EQUATION SOLVERS ========== */

int me_solve_linear(double a, double b, MeEquationResult *result) {
    if (!result)
        return -1;
    memset(result, 0, sizeof(*result));
    strcpy(result->method, "linear");

    if (a == 0) {
        if (b == 0) {
            snprintf(result->steps[0], 256, "0 = 0, infinite solutions");
            result->num_steps = 1;
            return 0;
        }
        snprintf(result->steps[0], 256, "No solution: %.2f ≠ 0", b);
        result->num_steps = 1;
        return -1;
    }

    result->roots[0] = -b / a;
    result->num_roots = 1;

    int s = 0;
    snprintf(result->steps[s++], 256, "Equation: %.2fx + %.2f = 0", a, b);
    snprintf(result->steps[s++], 256, "Move constant: %.2fx = %.2f", a, -b);
    snprintf(result->steps[s++], 256, "Divide by %.2f: x = %.6f", a, result->roots[0]);
    result->num_steps = s;

    return 0;
}

int me_solve_quadratic(double a, double b, double c, MeEquationResult *result) {
    if (!result)
        return -1;
    memset(result, 0, sizeof(*result));
    strcpy(result->method, "quadratic");

    int s = 0;
    snprintf(result->steps[s++], 256, "Equation: %.2fx² + %.2fx + %.2f = 0", a, b, c);

    double D = b * b - 4 * a * c;
    snprintf(result->steps[s++], 256, "Discriminant: D = b²-4ac = %.2f", D);

    if (D < 0) {
        snprintf(result->steps[s++], 256, "D < 0: No real roots");
        result->num_roots = 0;
        result->num_steps = s;
        return 0;
    }

    double sqrtD = sqrt(D);
    snprintf(result->steps[s++], 256, "√D = %.6f", sqrtD);

    if (D == 0) {
        result->roots[0] = -b / (2 * a);
        result->num_roots = 1;
        snprintf(result->steps[s++], 256, "D = 0: x = -b/(2a) = %.6f", result->roots[0]);
    } else {
        result->roots[0] = (-b - sqrtD) / (2 * a);
        result->roots[1] = (-b + sqrtD) / (2 * a);
        result->num_roots = 2;
        snprintf(result->steps[s++], 256, "x₁ = (-b-√D)/(2a) = %.6f", result->roots[0]);
        snprintf(result->steps[s++], 256, "x₂ = (-b+√D)/(2a) = %.6f", result->roots[1]);
    }

    result->num_steps = s;
    return 0;
}

int me_solve_biquadratic(double a, double b, double c, MeEquationResult *result) {
    if (!result)
        return -1;
    memset(result, 0, sizeof(*result));
    strcpy(result->method, "biquadratic");

    int s = 0;
    snprintf(result->steps[s++], 256, "Equation: %.2fx⁴ + %.2fx² + %.2f = 0", a, b, c);
    snprintf(result->steps[s++], 256, "Substitution: t = x², at² + bt + c = 0");

    MeEquationResult quad_res;
    me_solve_quadratic(a, b, c, &quad_res);

    for (int i = 0; i < quad_res.num_steps && s < ME_MAX_STEPS; i++) {
        snprintf(result->steps[s++], 256, "  %s", quad_res.steps[i]);
    }

    result->num_roots = 0;
    for (int i = 0; i < quad_res.num_roots && result->num_roots < ME_MAX_ROOTS; i++) {
        if (quad_res.roots[i] >= 0) {
            double x = sqrt(quad_res.roots[i]);
            result->roots[result->num_roots++] = -x;
            if (x > 0)
                result->roots[result->num_roots++] = x;
            snprintf(result->steps[s++], 256, "t%d = %.6f ≥ 0: x = ±%.6f", i + 1, quad_res.roots[i], x);
        } else {
            snprintf(result->steps[s++], 256, "t%d = %.6f < 0: no real roots", i + 1, quad_res.roots[i]);
        }
    }

    result->num_steps = s;
    return 0;
}

int me_solve_system_2x2(double a1, double b1, double c1, double a2, double b2, double c2, MeEquationResult *result) {
    if (!result)
        return -1;
    memset(result, 0, sizeof(*result));
    strcpy(result->method, "system_2x2");

    int s = 0;
    snprintf(result->steps[s++], 256, "System:");
    snprintf(result->steps[s++], 256, "  %.2fx + %.2fy = %.2f", a1, b1, c1);
    snprintf(result->steps[s++], 256, "  %.2fx + %.2fy = %.2f", a2, b2, c2);

    double D = a1 * b2 - a2 * b1;
    snprintf(result->steps[s++], 256, "Determinant: D = %.2f", D);

    if (D == 0) {
        snprintf(result->steps[s++], 256, "D = 0: No unique solution");
        result->num_roots = 0;
        result->num_steps = s;
        return -1;
    }

    double Dx = c1 * b2 - c2 * b1;
    double Dy = a1 * c2 - a2 * c1;

    result->roots[0] = Dx / D;
    result->roots[1] = Dy / D;
    result->num_roots = 2;

    snprintf(result->steps[s++], 256, "Dx = %.2f, Dy = %.2f", Dx, Dy);
    snprintf(result->steps[s++], 256, "x = Dx/D = %.6f", result->roots[0]);
    snprintf(result->steps[s++], 256, "y = Dy/D = %.6f", result->roots[1]);

    result->num_steps = s;
    return 0;
}

/* ========== DERIVATIVES ========== */

int me_derivative_polynomial(const double *coeffs, int degree, double *deriv_coeffs, int *deriv_degree) {
    if (degree <= 0) {
        *deriv_degree = 0;
        deriv_coeffs[0] = 0;
        return 0;
    }

    for (int i = 0; i < degree; i++) {
        deriv_coeffs[i] = coeffs[i + 1] * (i + 1);
    }
    *deriv_degree = degree - 1;
    return 0;
}

int me_derivative_rule_power(double n, char *result) {
    if (!result)
        return -1;
    if (n == 0) {
        snprintf(result, 256, "d/dx(C) = 0");
    } else if (n == 1) {
        snprintf(result, 256, "d/dx(x) = 1");
    } else if (n == 2) {
        snprintf(result, 256, "d/dx(x²) = 2x");
    } else if (n == 3) {
        snprintf(result, 256, "d/dx(x³) = 3x²");
    } else {
        snprintf(result, 256, "d/dx(x^%.0f) = %.0fx^(%.0f)", n, n, n - 1);
    }
    return 0;
}

int me_derivative_rule_product(const char *f, const char *g, char *result) {
    if (!result || !f || !g)
        return -1;
    snprintf(result, 256, "d/dx(f·g) = f'·g + f·g' = d/dx(%s)·%s + %s·d/dx(%s)", f, g, f, g);
    return 0;
}

int me_derivative_rule_chain(const char *outer, const char *inner, char *result) {
    if (!result || !outer || !inner)
        return -1;
    snprintf(result, 256, "d/dx(%s(%s)) = %s'(%s) · d/dx(%s)", outer, inner, outer, inner, inner);
    return 0;
}

/* ========== INTEGRALS ========== */

int me_integrate_polynomial(const double *coeffs, int degree, double *int_coeffs, int *int_degree) {
    *int_degree = degree + 1;
    int_coeffs[0] = 0; /* Constant of integration C */
    for (int i = 0; i <= degree; i++) {
        int_coeffs[i + 1] = coeffs[i] / (i + 1);
    }
    return 0;
}

/* ========== MATRICES ========== */

void me_matrix_init(MeMatrix *m, int rows, int cols) {
    memset(m, 0, sizeof(*m));
    m->rows = rows;
    m->cols = cols;
}

void me_matrix_set(MeMatrix *m, int row, int col, double value) {
    if (row >= 0 && row < m->rows && col >= 0 && col < m->cols) {
        m->matrix[row][col] = value;
    }
}

double me_matrix_get(const MeMatrix *m, int row, int col) {
    if (row >= 0 && row < m->rows && col >= 0 && col < m->cols) {
        return m->matrix[row][col];
    }
    return 0;
}

double me_matrix_det(const MeMatrix *m) {
    if (m->rows != m->cols)
        return 0;

    if (m->rows == 1)
        return m->matrix[0][0];

    if (m->rows == 2) {
        return m->matrix[0][0] * m->matrix[1][1] - m->matrix[0][1] * m->matrix[1][0];
    }

    if (m->rows == 3) {
        return m->matrix[0][0] * (m->matrix[1][1] * m->matrix[2][2] - m->matrix[1][2] * m->matrix[2][1]) -
               m->matrix[0][1] * (m->matrix[1][0] * m->matrix[2][2] - m->matrix[1][2] * m->matrix[2][0]) +
               m->matrix[0][2] * (m->matrix[1][0] * m->matrix[2][1] - m->matrix[1][1] * m->matrix[2][0]);
    }

    return 0; /* Not implemented for larger matrices */
}

int me_matrix_mul(const MeMatrix *a, const MeMatrix *b, MeMatrix *result) {
    if (a->cols != b->rows)
        return -1;

    me_matrix_init(result, a->rows, b->cols);

    for (int i = 0; i < a->rows; i++) {
        for (int j = 0; j < b->cols; j++) {
            double sum = 0;
            for (int k = 0; k < a->cols; k++) {
                sum += a->matrix[i][k] * b->matrix[k][j];
            }
            result->matrix[i][j] = sum;
        }
    }

    return 0;
}

/* ========== TRIGONOMETRY ========== */

int me_solve_trig_sin(double value, MeMathResult *result) {
    if (!result)
        return -1;
    memset(result, 0, sizeof(*result));
    result->is_symbolic = 1;

    int s = 0;
    snprintf(result->steps[s++], 256, "Equation: sin(x) = %.4f", value);

    if (value < -1 || value > 1) {
        snprintf(result->steps[s++], 256, "|%.4f| > 1: No solution", value);
        snprintf(result->result, 512, "No solution");
        result->num_steps = s;
        return 0;
    }

    double arcsin = asin(value);
    double arcsin_deg = arcsin * 180.0 / M_PI;

    snprintf(result->steps[s++], 256, "arcsin(%.4f) = %.4f rad (%.2f°)", value, arcsin, arcsin_deg);
    snprintf(result->steps[s++], 256, "General solution: x = (-1)^k · %.4f + πk", arcsin);

    if (value == 0.5) {
        snprintf(result->steps[s++], 256, "x = (-1)^k · π/6 + πk, k ∈ ℤ");
        snprintf(result->result, 512, "x = (-1)^k · π/6 + πk, k ∈ ℤ");
    } else if (value == -0.5) {
        snprintf(result->steps[s++], 256, "x = (-1)^(k+1) · π/6 + πk, k ∈ ℤ");
        snprintf(result->result, 512, "x = (-1)^(k+1) · π/6 + πk, k ∈ ℤ");
    } else if (value == 1.0) {
        snprintf(result->steps[s++], 256, "x = π/2 + 2πk, k ∈ ℤ");
        snprintf(result->result, 512, "x = π/2 + 2πk, k ∈ ℤ");
    } else if (value == 0.0) {
        snprintf(result->steps[s++], 256, "x = πk, k ∈ ℤ");
        snprintf(result->result, 512, "x = πk, k ∈ ℤ");
    } else {
        snprintf(result->result, 512, "x = (-1)^k · %.4f + πk, k ∈ ℤ", arcsin);
    }

    result->num_steps = s;
    return 0;
}

int me_solve_trig_cos(double value, MeMathResult *result) {
    if (!result)
        return -1;
    memset(result, 0, sizeof(*result));
    result->is_symbolic = 1;

    int s = 0;
    snprintf(result->steps[s++], 256, "Equation: cos(x) = %.4f", value);

    if (value < -1 || value > 1) {
        snprintf(result->steps[s++], 256, "|%.4f| > 1: No solution", value);
        snprintf(result->result, 512, "No solution");
        result->num_steps = s;
        return 0;
    }

    double arccos = acos(value);
    double arccos_deg = arccos * 180.0 / M_PI;

    snprintf(result->steps[s++], 256, "arccos(%.4f) = %.4f rad (%.2f°)", value, arccos, arccos_deg);
    snprintf(result->steps[s++], 256, "General solution: x = ±%.4f + 2πk", arccos);

    if (value == 0.5) {
        snprintf(result->steps[s++], 256, "x = ±π/3 + 2πk, k ∈ ℤ");
        snprintf(result->result, 512, "x = ±π/3 + 2πk, k ∈ ℤ");
    } else if (value == -0.5) {
        snprintf(result->steps[s++], 256, "x = ±2π/3 + 2πk, k ∈ ℤ");
        snprintf(result->result, 512, "x = ±2π/3 + 2πk, k ∈ ℤ");
    } else if (value == 1.0) {
        snprintf(result->steps[s++], 256, "x = 2πk, k ∈ ℤ");
        snprintf(result->result, 512, "x = 2πk, k ∈ ℤ");
    } else if (value == 0.0) {
        snprintf(result->steps[s++], 256, "x = π/2 + πk, k ∈ ℤ");
        snprintf(result->result, 512, "x = π/2 + πk, k ∈ ℤ");
    } else {
        snprintf(result->result, 512, "x = ±%.4f + 2πk, k ∈ ℤ", arccos);
    }

    result->num_steps = s;
    return 0;
}

int me_solve_trig_tan(double value, MeMathResult *result) {
    if (!result)
        return -1;
    memset(result, 0, sizeof(*result));
    result->is_symbolic = 1;

    int s = 0;
    snprintf(result->steps[s++], 256, "Equation: tan(x) = %.4f", value);

    double arctan = atan(value);
    double arctan_deg = arctan * 180.0 / M_PI;

    snprintf(result->steps[s++], 256, "arctan(%.4f) = %.4f rad (%.2f°)", value, arctan, arctan_deg);
    snprintf(result->steps[s++], 256, "General solution: x = %.4f + πk", arctan);

    if (value == 1.0) {
        snprintf(result->steps[s++], 256, "x = π/4 + πk, k ∈ ℤ");
        snprintf(result->result, 512, "x = π/4 + πk, k ∈ ℤ");
    } else if (value == sqrt(3)) {
        snprintf(result->steps[s++], 256, "x = π/3 + πk, k ∈ ℤ");
        snprintf(result->result, 512, "x = π/3 + πk, k ∈ ℤ");
    } else {
        snprintf(result->result, 512, "x = %.4f + πk, k ∈ ℤ", arctan);
    }

    result->num_steps = s;
    return 0;
}

/* ========== EXPONENTIAL & LOGARITHMIC ========== */

int me_solve_exp(double a, double b, MeMathResult *result) {
    if (!result)
        return -1;
    memset(result, 0, sizeof(*result));
    result->is_symbolic = 1;

    int s = 0;
    snprintf(result->steps[s++], 256, "Equation: %.2f^x = %.2f", a, b);

    if (a <= 0 || a == 1 || b <= 0) {
        snprintf(result->steps[s++], 256, "Invalid parameters");
        snprintf(result->result, 512, "No solution");
        result->num_steps = s;
        return -1;
    }

    result->numeric_result = log(b) / log(a);
    snprintf(result->steps[s++], 256, "x = log_%.2f(%.2f) = ln(%.2f)/ln(%.2f) = %.6f", a, b, b, a,
             result->numeric_result);
    snprintf(result->result, 512, "x = %.6f", result->numeric_result);
    result->num_steps = s;

    return 0;
}

int me_solve_log(double a, double b, MeMathResult *result) {
    if (!result)
        return -1;
    memset(result, 0, sizeof(*result));
    result->is_symbolic = 1;

    int s = 0;
    snprintf(result->steps[s++], 256, "Equation: log_%.2f(x) = %.2f", a, b);

    if (a <= 0 || a == 1) {
        snprintf(result->steps[s++], 256, "Invalid base");
        snprintf(result->result, 512, "No solution");
        result->num_steps = s;
        return -1;
    }

    result->numeric_result = pow(a, b);
    snprintf(result->steps[s++], 256, "x = %.2f^(%.2f) = %.6f", a, b, result->numeric_result);
    snprintf(result->result, 512, "x = %.6f", result->numeric_result);
    result->num_steps = s;

    return 0;
}

/* ========== PROBABILITY & COMBINATORICS ========== */

double me_combinations(int n, int k) {
    if (k < 0 || k > n)
        return 0;
    if (k == 0 || k == n)
        return 1;
    if (k > n / 2)
        k = n - k;

    double result = 1;
    for (int i = 0; i < k; i++) {
        result = result * (n - i) / (i + 1);
    }
    return result;
}

double me_permutations(int n) {
    if (n < 0)
        return 0;
    if (n <= 1)
        return 1;

    double result = 1;
    for (int i = 2; i <= n; i++) {
        result *= i;
    }
    return result;
}

double me_probability(int favorable, int total) {
    if (total <= 0)
        return 0;
    return (double)favorable / total;
}

/* ========== NUMBER THEORY ========== */

int me_gcd(int a, int b) {
    a = abs(a);
    b = abs(b);
    while (b) {
        int t = b;
        b = a % b;
        a = t;
    }
    return a;
}

int me_lcm(int a, int b) {
    if (a == 0 || b == 0)
        return 0;
    return abs(a * b) / me_gcd(a, b);
}

long long me_modpow(long long base, long long exp, long long mod) {
    long long result = 1;
    base = base % mod;
    while (exp > 0) {
        if (exp % 2 == 1)
            result = (result * base) % mod;
        exp = exp >> 1;
        base = (base * base) % mod;
    }
    return result;
}

/* ========== EXPRESSION PARSER ========== */

int me_eval_expression(const char *expr, double *result) {
    if (!expr || !result)
        return -1;

    /* Simple evaluator: handles numbers and basic operations */
    /* This is a simplified version - full parsing would need a proper parser */

    *result = 0;
    return sscanf(expr, "%lf", result) == 1 ? 0 : -1;
}

/* ========== QUERY PARSER ========== */

static int to_lower_str(const char *src, char *dst, size_t max) {
    size_t si = 0, di = 0;
    while (src[si] && di < max - 1) {
        unsigned char c = (unsigned char)src[si];
        if (c >= 'A' && c <= 'Z') {
            dst[di++] = c + 32;
            si++;
        } else if (c == 0xD0 && src[si + 1]) {
            unsigned char c2 = (unsigned char)src[si + 1];
            if (c2 >= 0x90 && c2 <= 0x9F) {
                dst[di++] = 0xD0;
                dst[di++] = c2 + 0x20;
                si += 2;
            } else if (c2 >= 0xA0 && c2 <= 0xAF) {
                dst[di++] = 0xD1;
                dst[di++] = c2 - 0x20;
                si += 2;
            } else {
                dst[di++] = c;
                si++;
            }
        } else {
            dst[di++] = c;
            si++;
        }
    }
    dst[di] = '\0';
    return 0;
}

/* Byte-sequence matcher: searches for a fixed UTF-8 byte pattern in raw input.
   This avoids compiler-dependent handling of wide/Cyrillic string literals. */
static inline int mem_has_bytes(const unsigned char *haystack, size_t hlen, const unsigned char *needle, size_t nlen) {
    if (nlen == 0 || nlen > hlen)
        return 0;
    for (size_t i = 0; i <= hlen - nlen; i++) {
        int match = 1;
        for (size_t j = 0; j < nlen; j++) {
            if (haystack[i + j] != needle[j]) {
                match = 0;
                break;
            }
        }
        if (match)
            return 1;
    }
    return 0;
}

#define BYTES_HAS(bseq)                                                                                                \
    mem_has_bytes((const unsigned char *)query, strlen(query), (const unsigned char *)(bseq), sizeof(bseq) - 1)

/* Pre-computed UTF-8 byte sequences for Cyrillic keywords.
   "выбрать"   = D0 B2 D1 8B D0 B1 D1 80 D0 B0 D1 82 D1 8C
   "сочетан"   = D1 81 D0 BE D1 87 D0 B5 D1 82 D0 B0 D0 BD
   "квадратн"  = D0 BA D0 B2 D0 B0 D0 B4 D1 80 D0 B0 D1 82 D0 BD
   "уравнен"   = D1 83 D1 80 D0 B0 D0 B2 D0 BD D0 B5 D0 BD
   "линейн"    = D0 BB D0 B8 D0 BD D0 B5 D0 B9 D0 BD
   "линей"     = D0 BB D0 B8 D0 BD D0 B5 D0 B9
   "перестанов"= D0 BF D0 B5 D1 80 D0 B5 D1 81 D1 82 D0 B0 D0 BD D0 BE D0 B2
   "вероятност"= D0 B2 D0 B5 D1 80 D0 BE D1 8F D1 82 D0 BD D0 BE D1 81 D1 82
   "факториал" = D1 84 D0 B0 D0 BA D1 82 D0 BE D1 80 D0 B8 D0 B0 D0 BB
   "производн" = D0 BF D1 80 D0 BE D0 B8 D0 B7 D0 B2 D0 BE D0 B4 D0 BD
   "интеграл"  = D0 B8 D0 BD D1 82 D0 B5 D0 B3 D1 80 D0 B0 D0 BB
   "матриц"    = D0 BC D0 B0 D1 82 D1 80 D0 B8 D1 86
   "определител"= D0 BE D0 BF D1 80 D0 B5 D0 B4 D0 B5 D0 BB D0 B8 D1 82 D0 B5 D0 BB
   "биквадрат" = D0 B1 D0 B8 D0 BA D0 B2 D0 B0 D0 B4 D1 80 D0 B0 D1 82
   "остаток"   = D0 BE D1 81 D1 82 D0 B0 D1 82 D0 BE D0 BA
   "расставить"= D1 80 D0 B0 D1 81 D1 81 D1 82 D0 B0 D0 B2 D0 B8 D1 82 D1 8C
   "систем"    = D1 81 D0 B8 D1 81 D1 82 D0 B5 D0 BC
   "умножен"   = D1 83 D0 BC D0 BD D0 BE D0 B6 D0 B5 D0 BD
   "произведен"= D0 BF D1 80 D0 BE D0 B8 D0 B7 D0 B2 D0 B5 D0 B4 D0 B5 D0 BD
   "дифференцирован" = D0 B4 D0 B8 D1 84 D1 84 D0 B5 D1 80 D0 B5 D0 BD D1 86 D0 B8 D1 80 D0 BE D0 B2 D0 B0 D0 BD */

#define CYB_VYBRAT "\xd0\xb2\xd1\x8b\xd0\xb1\xd1\x80\xd0\xb0\xd1\x82\xd1\x8c"
#define CYB_SOCHETAN "\xd1\x81\xd0\xbe\xd1\x87\xd0\xb5\xd1\x82\xd0\xb0\xd0\xbd"
#define CYB_KVADRATN "\xd0\xba\xd0\xb2\xd0\xb0\xd0\xb4\xd1\x80\xd0\xb0\xd1\x82\xd0\xbd"
#define CYB_URAVNEN "\xd1\x83\xd1\x80\xd0\xb0\xd0\xb2\xd0\xbd\xd0\xb5\xd0\xbd"
#define CYB_LINEYN "\xd0\xbb\xd0\xb8\xd0\xbd\xd0\xb5\xd0\xb9\xd0\xbd"
#define CYB_LINEY "\xd0\xbb\xd0\xb8\xd0\xbd\xd0\xb5\xd0\xb9"
#define CYB_PERESTANOV "\xd0\xbf\xd0\xb5\xd1\x80\xd0\xb5\xd1\x81\xd1\x82\xd0\xb0\xd0\xbd\xd0\xbe\xd0\xb2"
#define CYB_VERJATNOST "\xd0\xb2\xd0\xb5\xd1\x80\xd0\xbe\xd1\x8f\xd1\x82\xd0\xbd\xd0\xbe\xd1\x81\xd1\x82"
#define CYB_FAKTORIAL "\xd1\x84\xd0\xb0\xd0\xba\xd1\x82\xd0\xbe\xd1\x80\xd0\xb8\xd0\xb0\xd0\xbb"
#define CYB_PROIZVODN "\xd0\xbf\xd1\x80\xd0\xbe\xd0\xb8\xd0\xb7\xd0\xb2\xd0\xbe\xd0\xb4\xd0\xbd"
#define CYB_INTEGRAL "\xd0\xb8\xd0\xbd\xd1\x82\xd0\xb5\xd0\xb3\xd1\x80\xd0\xb0\xd0\xbb"
#define CYB_MATRIC "\xd0\xbc\xd0\xb0\xd1\x82\xd1\x80\xd0\xb8\xd1\x86"
#define CYB_OPREDELITEL "\xd0\xbe\xd0\xbf\xd1\x80\xd0\xb5\xd0\xb4\xd0\xb5\xd0\xbb\xd0\xb8\xd1\x82\xd0\xb5\xd0\xbb"
#define CYB_BIKVADRAT "\xd0\xb1\xd0\xb8\xd0\xba\xd0\xb2\xd0\xb0\xd0\xb4\xd1\x80\xd0\xb0\xd1\x82"
#define CYB_OSTATOK "\xd0\xbe\xd1\x81\xd1\x82\xd0\xb0\xd1\x82\xd0\xbe\xd0\xba"
#define CYB_RASSTAVIT "\xd1\x80\xd0\xb0\xd1\x81\xd1\x81\xd1\x82\xd0\xb0\xd0\xb2\xd0\xb8\xd1\x82\xd1\x8c"
#define CYB_SISTEM "\xd1\x81\xd0\xb8\xd1\x81\xd1\x82\xd0\xb5\xd0\xbc"
#define CYB_UMNOZHEN "\xd1\x83\xd0\xbc\xd0\xbd\xd0\xbe\xd0\xb6\xd0\xb5\xd0\xbd"
#define CYB_PROIZVEDEN "\xd0\xbf\xd1\x80\xd0\xbe\xd0\xb8\xd0\xb7\xd0\xb2\xd0\xb5\xd0\xb4\xd0\xb5\xd0\xbd"
#define CYB_DIFFERENCI                                                                                                 \
    "\xd0\xb4\xd0\xb8\xd1\x84\xd1\x84\xd0\xb5\xd1\x80\xd0\xb5\xd0\xbd\xd1\x86\xd0\xb8\xd1\x80\xd0\xbe\xd0\xb2\xd0\xb0" \
    "\xd0\xbd"

/* Search helper: checks ASCII via strstr, Cyrillic via byte-level match */
#define HAS(kw) (strstr(query, kw) != NULL || strstr(lower, kw) != NULL || BYTES_HAS(kw))

/* Helper: extract two integers from a string that may have leading non-digit text.
   Uses %*[^0-9] to skip everything until first digit, then reads two ints. */
static inline int sscanf_two_ints(const char *s, int *a, int *b) {
    return sscanf(s, "%*[^0-9]%d%*[^0-9]%d", a, b) == 2;
}

/* Helper: extract one integer from a string that may have leading non-digit text. */
static inline int sscanf_one_int(const char *s, int *a) { return sscanf(s, "%*[^0-9]%d", a) == 1; }


/* Parse quadratic equation: "x^2-5x+6=0" or "2x^2+3x-2=0" or "x^2-4=0" */
static int parse_quadratic(const char *query, double *a, double *b, double *c) {
    const char *x2 = strstr(query, "x^2");
    if (!x2) return 0;
    
    *a = 1; *b = 0; *c = 0;
    
    /* Parse a coefficient */
    if (x2 != query) {
        char buf[32] = {0};
        int len = x2 - query;
        if (len < 31) strncpy(buf, query, len);
        if (len == 1 && buf[0] == '-') *a = -1;
        else if (len == 1 && buf[0] == '+') *a = 1;
        else sscanf(buf, "%lf", a);
    }
    
    /* Parse the rest after x^2 */
    const char *p = x2 + 3;
    if (*p == 'x') p++;
    
    while (*p && *p != '=') {
        while (*p == ' ') p++;
        if (*p == '=' || *p == '\0') break;
        
        double sign = 1;
        if (*p == '+') { sign = 1; p++; }
        else if (*p == '-') { sign = -1; p++; }
        
        while (*p == ' ') p++;
        
        double val = 0;
        if (*p >= '0' && *p <= '9') {
            sscanf(p, "%lf", &val);
            while ((*p >= '0' && *p <= '9') || *p == '.') p++;
        } else {
            val = 1;
        }
        val *= sign;
        
        if (*p == 'x') {
            *b = val;
            p++;
        } else {
            *c = val;
        }
    }
    
    return 1;
}

int me_parse_query(const char *query, MeParsedQuery *parsed) {
    if (!query || !parsed)
        return -1;
    memset(parsed, 0, sizeof(*parsed));

    char lower[2048];
    to_lower_str(query, lower, sizeof(lower));

    size_t qlen = strlen(query);
    (void)qlen; /* used by BYTES_HAS macro */

    /* Quadratic equation */
    if (HAS("x^2") || HAS("x\xc2\xb2") || BYTES_HAS(CYB_KVADRATN) || HAS("x2=")) {
        double a = 0, b = 0, c = 0;
        if (parse_quadratic(query, &a, &b, &c)) {
            parsed->type = ME_QUERY_QUADRATIC_EQ;
            parsed->params[0] = a;
            parsed->params[1] = b;
            parsed->params[2] = c;
            parsed->num_params = 3;
            return 0;
        }
    }

    /* Combinations -- byte-level match for "выбрать" */
    if (HAS("C(")) {
        int cn = 0, ck = 0;
        if (sscanf(query, "C(%d,%d)", &cn, &ck) == 2) {
            parsed->type = ME_QUERY_COMBINATIONS;
            parsed->params[0] = cn;
            parsed->params[1] = ck;
            parsed->num_params = 2;
            return 0;
        }
    }
    if (BYTES_HAS(CYB_SOCHETAN) || BYTES_HAS(CYB_VYBRAT)) {
        int n = 0, k = 0;
        if (sscanf_two_ints(query, &k, &n) || sscanf_two_ints(lower, &k, &n)) {
            parsed->type = ME_QUERY_COMBINATIONS;
            parsed->params[0] = n;
            parsed->params[1] = k;
            parsed->num_params = 2;
            return 0;
        }
    }

    /* GCD */
    if (HAS("\xd0\x9d\xd0\x9e\xd0\x94") || HAS("\xd0\xbd\xd0\xbe\xd0\xb4") || HAS("gcd")) {
        int a = 0, b = 0;
        if (sscanf_two_ints(query, &a, &b) || sscanf_two_ints(lower, &a, &b) || sscanf(query, "%d %d", &a, &b) == 2) {
            parsed->type = ME_QUERY_GCD;
            parsed->params[0] = a;
            parsed->params[1] = b;
            parsed->num_params = 2;
            return 0;
        }
    }

    /* LCM */
    if (HAS("\xd0\x9d\xd0\x9e\xd0\x9a") || HAS("\xd0\xbd\xd0\xbe\xd0\xba") || HAS("lcm")) {
        int a = 0, b = 0;
        if (sscanf_two_ints(query, &a, &b) || sscanf_two_ints(lower, &a, &b)) {
            parsed->type = ME_QUERY_LCM;
            parsed->params[0] = a;
            parsed->params[1] = b;
            parsed->num_params = 2;
            return 0;
        }
    }

    /* Probability */
    if (BYTES_HAS(CYB_VERJATNOST)) {
        int fav = 0, total = 0;
        if (sscanf_two_ints(query, &fav, &total) || sscanf_two_ints(lower, &fav, &total)) {
            parsed->type = ME_QUERY_PROBABILITY;
            parsed->params[0] = fav;
            parsed->params[1] = total;
            parsed->num_params = 2;
            return 0;
        }
    }

    /* Permutations */
    if (BYTES_HAS(CYB_PERESTANOV) || BYTES_HAS(CYB_RASSTAVIT) || BYTES_HAS(CYB_FAKTORIAL)) {
        int n = 0;
        if (sscanf_one_int(query, &n) || sscanf_one_int(lower, &n)) {
            parsed->type = ME_QUERY_PERMUTATIONS;
            parsed->params[0] = n;
            parsed->num_params = 1;
            return 0;
        }
    }

    /* Trigonometry */
    if (HAS("sin") || HAS("cos") || HAS("tan")) {
        double value = 0;
        if (sscanf(query, "sin x=%lf", &value) == 1 || sscanf(query, "cos x=%lf", &value) == 1 ||
            sscanf(query, "tan x=%lf", &value) == 1) {
            parsed->type = ME_QUERY_TRIG_EQ;
            parsed->params[0] = value;
            if (HAS("sin"))
                parsed->params[1] = 0;
            else if (HAS("cos"))
                parsed->params[1] = 1;
            else
                parsed->params[1] = 2;
            parsed->num_params = 2;
            return 0;
        }
    }

    /* Derivative */
    if (BYTES_HAS(CYB_PROIZVODN) || BYTES_HAS(CYB_DIFFERENCI)) {
        parsed->type = ME_QUERY_DERIVATIVE;
        return 0;
    }

    /* Integral */
    if (BYTES_HAS(CYB_INTEGRAL)) {
        parsed->type = ME_QUERY_INTEGRAL;
        return 0;
    }

    /* Matrix */
    if (BYTES_HAS(CYB_MATRIC) || BYTES_HAS(CYB_OPREDELITEL) || HAS("det")) {
        if (BYTES_HAS(CYB_OPREDELITEL) || HAS("det"))
            parsed->type = ME_QUERY_MATRIX_DET;
        else if (BYTES_HAS(CYB_UMNOZHEN) || BYTES_HAS(CYB_PROIZVEDEN))
            parsed->type = ME_QUERY_MATRIX_MUL;
        else
            parsed->type = ME_QUERY_MATRIX_DET;
        return 0;
    }

    /* Linear equation */
    if ((BYTES_HAS(CYB_LINEYN) || BYTES_HAS(CYB_LINEY)) && BYTES_HAS(CYB_URAVNEN)) {
        double a = 0, b = 0;
        if (sscanf(query, "%lfx%lf", &a, &b) == 2) {
            parsed->type = ME_QUERY_LINEAR_EQ;
            parsed->params[0] = a;
            parsed->params[1] = b;
            parsed->num_params = 2;
            return 0;
        }
    }

    /* System */
    if (BYTES_HAS(CYB_SISTEM) || (HAS("x+y") && HAS("x^2+y^2"))) {
        parsed->type = ME_QUERY_SYSTEM_2X2;
        return 0;
    }

    /* Biquadratic */
    if (HAS("x^4") || BYTES_HAS(CYB_BIKVADRAT)) {
        double a = 0, b = 0, c = 0;
        if (sscanf(query, "%lfx^4%lfx^2%lf", &a, &b, &c) == 3) {
            parsed->type = ME_QUERY_BIQUADRATIC_EQ;
            parsed->params[0] = a;
            parsed->params[1] = b;
            parsed->params[2] = c;
            parsed->num_params = 3;
            return 0;
        }
    }

    /* Modulo */
    if (HAS("mod") || BYTES_HAS(CYB_OSTATOK)) {
        long long base = 0, exp = 0, mod = 0;
        if (sscanf(query, "%lld^%lld mod %lld", &base, &exp, &mod) == 3) {
            parsed->type = ME_QUERY_MODPOW;
            parsed->params[0] = base;
            parsed->params[1] = exp;
            parsed->params[2] = mod;
            parsed->num_params = 3;
            return 0;
        }
    }

    parsed->type = ME_QUERY_UNKNOWN;
    return -1;
}

int me_execute_query(const MeParsedQuery *parsed, MeMathResult *result) {
    if (!parsed || !result)
        return -1;
    memset(result, 0, sizeof(*result));

    switch (parsed->type) {
    case ME_QUERY_LINEAR_EQ: {
        MeEquationResult eq;
        me_solve_linear(parsed->params[0], parsed->params[1], &eq);
        result->is_symbolic = 1;
        snprintf(result->result, 512, "x = %.6f", eq.roots[0]);
        for (int i = 0; i < eq.num_steps && i < ME_MAX_STEPS; i++)
            snprintf(result->steps[i], 256, "%s", eq.steps[i]);
        result->num_steps = eq.num_steps;
        break;
    }
    case ME_QUERY_QUADRATIC_EQ: {
        MeEquationResult eq;
        me_solve_quadratic(parsed->params[0], parsed->params[1], parsed->params[2], &eq);
        result->is_symbolic = 1;
        if (eq.num_roots == 0)
            snprintf(result->result, 512, "No real roots");
        else if (eq.num_roots == 1)
            snprintf(result->result, 512, "x = %.6f", eq.roots[0]);
        else
            snprintf(result->result, 512, "x₁ = %.6f, x₂ = %.6f", eq.roots[0], eq.roots[1]);
        for (int i = 0; i < eq.num_steps && i < ME_MAX_STEPS; i++)
            snprintf(result->steps[i], 256, "%s", eq.steps[i]);
        result->num_steps = eq.num_steps;
        break;
    }
    case ME_QUERY_COMBINATIONS: {
        double res = me_combinations((int)parsed->params[0], (int)parsed->params[1]);
        result->is_symbolic = 0;
        result->numeric_result = res;
        snprintf(result->result, 512, "C(%d, %d) = %.0f", (int)parsed->params[0], (int)parsed->params[1], res);
        int s = 0;
        snprintf(result->steps[s++], 256, "Formula: C(n,k) = n! / (k!(n-k)!)");
        snprintf(result->steps[s++], 256, "C(%d, %d) = %d! / (%d!·%d!) = %.0f", (int)parsed->params[0],
                 (int)parsed->params[1], (int)parsed->params[0], (int)parsed->params[1],
                 (int)(parsed->params[0] - parsed->params[1]), res);
        result->num_steps = s;
        break;
    }
    case ME_QUERY_PERMUTATIONS: {
        double res = me_permutations((int)parsed->params[0]);
        result->is_symbolic = 0;
        result->numeric_result = res;
        snprintf(result->result, 512, "P(%d) = %.0f", (int)parsed->params[0], res);
        int s = 0;
        snprintf(result->steps[s++], 256, "Formula: P(n) = n!");
        snprintf(result->steps[s++], 256, "%d! = %.0f", (int)parsed->params[0], res);
        result->num_steps = s;
        break;
    }
    case ME_QUERY_PROBABILITY: {
        double res = me_probability((int)parsed->params[0], (int)parsed->params[1]);
        result->is_symbolic = 0;
        result->numeric_result = res;
        snprintf(result->result, 512, "P = %d/%d = %.4f", (int)parsed->params[0], (int)parsed->params[1], res);
        result->num_steps = 1;
        snprintf(result->steps[0], 256, "P = favorable/total = %d/%d = %.4f", (int)parsed->params[0],
                 (int)parsed->params[1], res);
        break;
    }
    case ME_QUERY_GCD: {
        int res = me_gcd((int)parsed->params[0], (int)parsed->params[1]);
        result->is_symbolic = 0;
        result->numeric_result = res;
        snprintf(result->result, 512, "НОД(%d, %d) = %d", (int)parsed->params[0], (int)parsed->params[1], res);
        result->num_steps = 1;
        snprintf(result->steps[0], 256, "Euclidean algorithm: НОД(%d, %d) = %d", (int)parsed->params[0],
                 (int)parsed->params[1], res);
        break;
    }
    case ME_QUERY_LCM: {
        int res = me_lcm((int)parsed->params[0], (int)parsed->params[1]);
        result->is_symbolic = 0;
        result->numeric_result = res;
        snprintf(result->result, 512, "НОК(%d, %d) = %d", (int)parsed->params[0], (int)parsed->params[1], res);
        result->num_steps = 1;
        snprintf(result->steps[0], 256, "НОК(a,b) = |a·b| / НОД(a,b) = %d", res);
        break;
    }
    case ME_QUERY_MODPOW: {
        long long res =
            me_modpow((long long)parsed->params[0], (long long)parsed->params[1], (long long)parsed->params[2]);
        result->is_symbolic = 0;
        result->numeric_result = res;
        snprintf(result->result, 512, "%lld^%lld mod %lld = %lld", (long long)parsed->params[0],
                 (long long)parsed->params[1], (long long)parsed->params[2], res);
        result->num_steps = 1;
        snprintf(result->steps[0], 256, "Binary exponentiation: result = %lld", res);
        break;
    }
    case ME_QUERY_TRIG_EQ: {
        int s = 0;
        if ((int)parsed->params[1] == 0) {
            me_solve_trig_sin(parsed->params[0], result);
        } else if ((int)parsed->params[1] == 1) {
            me_solve_trig_cos(parsed->params[0], result);
        } else {
            me_solve_trig_tan(parsed->params[0], result);
        }
        break;
    }
    default:
        result->is_symbolic = 1;
        snprintf(result->result, 512, "Unknown query type");
        result->num_steps = 1;
        snprintf(result->steps[0], 256, "Could not parse query");
        return -1;
    }

    return 0;
}

int me_solve(const char *problem, MeMathResult *result) {
    if (!problem || !result)
        return -1;

    MeParsedQuery parsed;
    if (me_parse_query(problem, &parsed) == 0) {
        return me_execute_query(&parsed, result);
    }

    result->is_symbolic = 1;
    snprintf(result->result, 512, "Could not understand math problem");
    result->num_steps = 1;
    snprintf(result->steps[0], 256, "Query: '%s'", problem);
    return -1;
}
