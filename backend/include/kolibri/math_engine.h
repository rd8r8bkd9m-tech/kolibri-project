/*
 * math_engine.h — Mathematical Engine for Kolibri AI
 *
 * Symbolic mathematics: equations, derivatives, integrals, matrices, trig
 *
 * Copyright (c) 2026 Кочуров Владислав Евгеньевич
 */

#ifndef KOLIBRI_MATH_ENGINE_H
#define KOLIBRI_MATH_ENGINE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ME_MAX_ROOTS 10
#define ME_MAX_MATRIX 10
#define ME_MAX_EXPR 1024
#define ME_MAX_STEPS 20

/* ========== RESULT TYPES ========== */

typedef struct {
    double roots[ME_MAX_ROOTS];
    int num_roots;
    char method[64];
    char steps[ME_MAX_STEPS][256];
    int num_steps;
} MeEquationResult;

typedef struct {
    double matrix[ME_MAX_MATRIX][ME_MAX_MATRIX];
    int rows;
    int cols;
} MeMatrix;

typedef struct {
    char result[512];
    char method[64];
    char steps[ME_MAX_STEPS][256];
    int num_steps;
    double numeric_result;
    int is_symbolic;
} MeMathResult;

/* ========== EQUATION SOLVERS ========== */

/* Solve quadratic: ax^2 + bx + c = 0 */
int me_solve_quadratic(double a, double b, double c, MeEquationResult *result);

/* Solve biquadratic: ax^4 + bx^2 + c = 0 */
int me_solve_biquadratic(double a, double b, double c, MeEquationResult *result);

/* Solve linear: ax + b = 0 */
int me_solve_linear(double a, double b, MeEquationResult *result);

/* Solve system of 2 linear equations:
   a1*x + b1*y = c1
   a2*x + b2*y = c2 */
int me_solve_system_2x2(double a1, double b1, double c1,
                        double a2, double b2, double c2,
                        MeEquationResult *result);

/* ========== DERIVATIVES ========== */

/* Compute derivative of polynomial given by coefficients */
int me_derivative_polynomial(const double *coeffs, int degree,
                             double *deriv_coeffs, int *deriv_degree);

/* Symbolic derivative rules */
int me_derivative_rule_power(double n, char *result);
int me_derivative_rule_product(const char *f, const char *g, char *result);
int me_derivative_rule_chain(const char *outer, const char *inner, char *result);

/* ========== INTEGRALS ========== */

/* Compute integral of polynomial */
int me_integrate_polynomial(const double *coeffs, int degree,
                            double *int_coeffs, int *int_degree);

/* ========== MATRICES ========== */

/* Initialize matrix */
void me_matrix_init(MeMatrix *m, int rows, int cols);

/* Set matrix element */
void me_matrix_set(MeMatrix *m, int row, int col, double value);

/* Get matrix element */
double me_matrix_get(const MeMatrix *m, int row, int col);

/* Compute determinant (up to 3x3) */
double me_matrix_det(const MeMatrix *m);

/* Multiply two matrices */
int me_matrix_mul(const MeMatrix *a, const MeMatrix *b, MeMatrix *result);

/* ========== TRIGONOMETRY ========== */

/* Solve basic trig equations */
int me_solve_trig_sin(double value, MeMathResult *result);
int me_solve_trig_cos(double value, MeMathResult *result);
int me_solve_trig_tan(double value, MeMathResult *result);

/* ========== EXPONENTIAL & LOGARITHMIC ========== */

/* Solve exponential equation: a^x = b */
int me_solve_exp(double a, double b, MeMathResult *result);

/* Solve logarithmic equation: log_a(x) = b */
int me_solve_log(double a, double b, MeMathResult *result);

/* ========== PROBABILITY ========== */

/* Compute combinations C(n,k) */
double me_combinations(int n, int k);

/* Compute permutations P(n) */
double me_permutations(int n);

/* Compute probability of event */
double me_probability(int favorable, int total);

/* ========== NUMBER THEORY ========== */

/* Compute GCD */
int me_gcd(int a, int b);

/* Compute LCM */
int me_lcm(int a, int b);

/* Modular exponentiation: (base^exp) % mod */
long long me_modpow(long long base, long long exp, long long mod);

/* ========== EXPRESSION PARSER ========== */

/* Parse and evaluate simple math expression */
int me_eval_expression(const char *expr, double *result);

/* Parse math query and determine type */
typedef enum {
    ME_QUERY_UNKNOWN = 0,
    ME_QUERY_LINEAR_EQ,
    ME_QUERY_QUADRATIC_EQ,
    ME_QUERY_BIQUADRATIC_EQ,
    ME_QUERY_SYSTEM_2X2,
    ME_QUERY_EXPONENTIAL_EQ,
    ME_QUERY_LOGARITHMIC_EQ,
    ME_QUERY_TRIG_EQ,
    ME_QUERY_DERIVATIVE,
    ME_QUERY_INTEGRAL,
    ME_QUERY_MATRIX_DET,
    ME_QUERY_MATRIX_MUL,
    ME_QUERY_COMBINATIONS,
    ME_QUERY_PERMUTATIONS,
    ME_QUERY_PROBABILITY,
    ME_QUERY_GCD,
    ME_QUERY_LCM,
    ME_QUERY_MODPOW,
    ME_QUERY_EVAL
} MeQueryType;

typedef struct {
    MeQueryType type;
    double params[20];
    int num_params;
    char text_params[10][256];
    int num_text_params;
} MeParsedQuery;

/* Parse a natural language math query */
int me_parse_query(const char *query, MeParsedQuery *parsed);

/* Execute parsed query and return result */
int me_execute_query(const MeParsedQuery *parsed, MeMathResult *result);

/* High-level: solve math problem from text */
int me_solve(const char *problem, MeMathResult *result);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_MATH_ENGINE_H */
