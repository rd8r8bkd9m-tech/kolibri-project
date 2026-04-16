/*
 * test_reinforcement_learning.c
 *
 * Unit тесты для reinforcement learning module
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/intent_classifier.h"
#include "kolibri/reinforcement_learning.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition, message, ...)                                                                           \
    do {                                                                                                               \
        tests_run++;                                                                                                   \
        if (condition) {                                                                                               \
            tests_passed++;                                                                                            \
            printf("  ✓ " message "\n", ##__VA_ARGS__);                                                                \
        } else {                                                                                                       \
            tests_failed++;                                                                                            \
            printf("  ✗ FAILED: " message "\n", ##__VA_ARGS__);                                                        \
        }                                                                                                              \
    } while (0)

#define TEST_START(name) printf("\n=== TEST: %s ===\n", name)

/* ============================================================================
 * TEST: Initialization
 * ============================================================================ */

static void test_initialization(void) {
    TEST_START("Initialization");

    KolibriRLContext ctx;
    int result = kolibri_rl_init(&ctx);

    TEST_ASSERT(result == 0, "RL context initialized");
    TEST_ASSERT(ctx.num_states == 0, "No states initially");
    TEST_ASSERT(ctx.alpha > 0.0, "Alpha is set");
    TEST_ASSERT(ctx.gamma > 0.0 && ctx.gamma < 1.0, "Gamma is set correctly");
    TEST_ASSERT(ctx.epsilon > 0.0 && ctx.epsilon <= 1.0, "Epsilon is set");

    kolibri_rl_destroy(&ctx);
}

/* ============================================================================
 * TEST: Action Selection
 * ============================================================================ */

static void test_action_selection(void) {
    TEST_START("Action Selection");

    KolibriRLContext ctx;
    kolibri_rl_init(&ctx);

    KolibriRLState state;
    memset(&state, 0, sizeof(state));
    state.intent = KIC_INTENT_MATH_PROBLEM;
    state.complexity = 0.7;
    state.requires_reasoning = 1;
    state.hash = kolibri_rl_hash_state(&state);

    KolibriRLAction action;
    int result = kolibri_rl_select_action(&ctx, &state, &action);

    TEST_ASSERT(result == 0, "Action selection works");
    TEST_ASSERT(action >= 0 && action < KRL_ACTION_COUNT, "Action is valid");

    printf("  Selected action: %s\n", kolibri_rl_action_name(action));

    kolibri_rl_destroy(&ctx);
}

/* ============================================================================
 * TEST: Greedy Action Selection
 * ============================================================================ */

static void test_greedy_action(void) {
    TEST_START("Greedy Action Selection");

    KolibriRLContext ctx;
    kolibri_rl_init(&ctx);

    KolibriRLState state;
    memset(&state, 0, sizeof(state));
    state.intent = KIC_INTENT_QUERY_FACT;
    state.requires_knowledge = 1;
    state.hash = kolibri_rl_hash_state(&state);

    KolibriRLAction action = kolibri_rl_select_action_greedy(&ctx, &state);

    TEST_ASSERT(action >= 0 && action < KRL_ACTION_COUNT, "Greedy action is valid");

    printf("  Greedy action: %s\n", kolibri_rl_action_name(action));

    kolibri_rl_destroy(&ctx);
}

/* ============================================================================
 * TEST: Q-Table Update
 * ============================================================================ */

static void test_q_update(void) {
    TEST_START("Q-Table Update");

    KolibriRLContext ctx;
    kolibri_rl_init(&ctx);

    KolibriRLState state;
    memset(&state, 0, sizeof(state));
    state.intent = KIC_INTENT_MATH_PROBLEM;
    state.complexity = 0.8;
    state.requires_reasoning = 1;
    state.hash = kolibri_rl_hash_state(&state);

    KolibriRLState next_state;
    memset(&next_state, 0, sizeof(next_state));
    next_state.intent = KIC_INTENT_MATH_PROBLEM;
    next_state.complexity = 0.6;
    next_state.hash = kolibri_rl_hash_state(&next_state);

    double initial_q = 0.0;

    /* Perform update */
    int result = kolibri_rl_update(&ctx, &state, KRL_ACTION_USE_MATH_SOLVER, 0.9, &next_state);

    TEST_ASSERT(result == 0, "Q-update succeeded");
    TEST_ASSERT(ctx.num_states == 1, "State added to Q-table");
    TEST_ASSERT(ctx.stats.total_updates == 1, "Update count incremented");
    TEST_ASSERT(ctx.stats.total_reward == 0.9, "Reward recorded");

    /* Second update with different reward */
    result = kolibri_rl_update(&ctx, &state, KRL_ACTION_USE_MATH_SOLVER, 0.7, &next_state);

    TEST_ASSERT(result == 0, "Second update succeeded");
    TEST_ASSERT(ctx.stats.total_updates == 2, "Two updates total");
    TEST_ASSERT(ctx.stats.average_reward > 0.0, "Average reward calculated");

    printf("  Total reward: %.2f\n", ctx.stats.total_reward);
    printf("  Average reward: %.2f\n", ctx.stats.average_reward);

    kolibri_rl_destroy(&ctx);
}

/* ============================================================================
 * TEST: Experience Replay
 * ============================================================================ */

static void test_experience_replay(void) {
    TEST_START("Experience Replay");

    KolibriRLContext ctx;
    kolibri_rl_init(&ctx);

    /* Store multiple experiences */
    for (int i = 0; i < 10; i++) {
        KolibriExperience exp;
        memset(&exp, 0, sizeof(exp));
        exp.state.intent = KIC_INTENT_QUERY_FACT;
        exp.state.hash = 1000 + i;
        exp.action = KRL_ACTION_USE_KNOWLEDGE_BASE;
        exp.reward = 0.5 + (i * 0.05);
        exp.next_state.hash = 2000 + i;
        exp.done = 1;

        int result = kolibri_rl_store_experience(&ctx, &exp);
        TEST_ASSERT(result == 0, "Experience %d stored", i + 1);
    }

    TEST_ASSERT(ctx.replay_count == 10, "10 experiences in buffer");

    /* Replay mini-batch */
    int result = kolibri_rl_replay(&ctx, 5);
    TEST_ASSERT(result == 0, "Replay works");
    TEST_ASSERT(ctx.stats.total_updates >= 5, "Updates from replay");

    kolibri_rl_destroy(&ctx);
}

/* ============================================================================
 * TEST: Reward Computation
 * ============================================================================ */

static void test_reward_computation(void) {
    TEST_START("Reward Computation");

    /* High confidence, fast response, positive feedback */
    double reward1 = kolibri_rl_compute_reward(0.95, 50.0, 1.0, 1);
    TEST_ASSERT(reward1 > 0.8 && reward1 <= 1.0, "High quality gets high reward (%.2f)", reward1);

    /* Medium quality */
    double reward2 = kolibri_rl_compute_reward(0.7, 500.0, 0.0, 1);
    TEST_ASSERT(reward2 > 0.4 && reward2 < 0.8, "Medium quality gets medium reward (%.2f)", reward2);

    /* Low quality */
    double reward3 = kolibri_rl_compute_reward(0.2, 2000.0, -1.0, 0);
    TEST_ASSERT(reward3 < 0.4, "Low quality gets low reward (%.2f)", reward3);

    printf("  High quality reward: %.2f\n", reward1);
    printf("  Medium quality reward: %.2f\n", reward2);
    printf("  Low quality reward: %.2f\n", reward3);
}

/* ============================================================================
 * TEST: Epsilon Decay
 * ============================================================================ */

static void test_epsilon_decay(void) {
    TEST_START("Epsilon Decay");

    KolibriRLContext ctx;
    kolibri_rl_init(&ctx);

    double initial_epsilon = ctx.epsilon;

    KolibriRLState state;
    memset(&state, 0, sizeof(state));
    state.hash = kolibri_rl_hash_state(&state);
    KolibriRLState next_state = state;

    /* Perform multiple updates */
    for (int i = 0; i < 100; i++) {
        kolibri_rl_update(&ctx, &state, KRL_ACTION_USE_REASONING, 0.8, &next_state);
    }

    TEST_ASSERT(ctx.epsilon < initial_epsilon, "Epsilon decreased (%.3f -> %.3f)", initial_epsilon, ctx.epsilon);
    TEST_ASSERT(ctx.epsilon >= KRL_MIN_EPSILON, "Epsilon above minimum (%.3f >= %.3f)", ctx.epsilon, KRL_MIN_EPSILON);

    printf("  Initial epsilon: %.3f\n", initial_epsilon);
    printf("  Final epsilon: %.3f\n", ctx.epsilon);

    kolibri_rl_destroy(&ctx);
}

/* ============================================================================
 * TEST: State Hashing
 * ============================================================================ */

static void test_state_hashing(void) {
    TEST_START("State Hashing");

    KolibriRLState state1;
    memset(&state1, 0, sizeof(state1));
    state1.intent = KIC_INTENT_MATH_PROBLEM;
    state1.complexity = 0.7;
    state1.requires_reasoning = 1;
    strcpy(state1.domain, "math");

    KolibriRLState state2;
    memcpy(&state2, &state1, sizeof(state2));

    uint32_t hash1 = kolibri_rl_hash_state(&state1);
    uint32_t hash2 = kolibri_rl_hash_state(&state2);

    TEST_ASSERT(hash1 == hash2, "Identical states have same hash");

    /* Different state */
    KolibriRLState state3;
    memset(&state3, 0, sizeof(state3));
    state3.intent = KIC_INTENT_QUERY_FACT;
    state3.hash = kolibri_rl_hash_state(&state3);

    TEST_ASSERT(hash1 != state3.hash, "Different states have different hash");

    printf("  State 1 hash: 0x%08x\n", hash1);
    printf("  State 3 hash: 0x%08x\n", state3.hash);
}

/* ============================================================================
 * TEST: Save/Load Q-Table
 * ============================================================================ */

static void test_save_load_qtable(void) {
    TEST_START("Save/Load Q-Table");

    const char *test_file = "/tmp/test_qtable.bin";

    KolibriRLContext ctx1;
    kolibri_rl_init(&ctx1);

    /* Add some data */
    KolibriRLState state;
    memset(&state, 0, sizeof(state));
    state.intent = KIC_INTENT_MATH_PROBLEM;
    state.hash = kolibri_rl_hash_state(&state);

    kolibri_rl_update(&ctx1, &state, KRL_ACTION_USE_MATH_SOLVER, 0.9, &state);
    kolibri_rl_update(&ctx1, &state, KRL_ACTION_USE_REASONING, 0.7, &state);

    /* Save */
    int result = kolibri_rl_save_qtable(&ctx1, test_file);
    TEST_ASSERT(result == 0, "Q-table saved");

    /* Load into new context */
    KolibriRLContext ctx2;
    kolibri_rl_init(&ctx2);

    result = kolibri_rl_load_qtable(&ctx2, test_file);
    TEST_ASSERT(result == 0, "Q-table loaded");
    TEST_ASSERT(ctx2.num_states == ctx1.num_states, "State count matches after load");

    printf("  Saved %d states\n", ctx1.num_states);
    printf("  Loaded %d states\n", ctx2.num_states);

    kolibri_rl_destroy(&ctx1);
    kolibri_rl_destroy(&ctx2);

    /* Cleanup */
    remove(test_file);
}

/* ============================================================================
 * TEST: Statistics
 * ============================================================================ */

static void test_statistics(void) {
    TEST_START("Statistics");

    KolibriRLContext ctx;
    kolibri_rl_init(&ctx);

    KolibriRLState state;
    memset(&state, 0, sizeof(state));
    state.hash = kolibri_rl_hash_state(&state);
    KolibriRLState next_state = state;

    /* Multiple updates with different actions */
    kolibri_rl_update(&ctx, &state, KRL_ACTION_USE_KNOWLEDGE_BASE, 0.8, &next_state);
    kolibri_rl_update(&ctx, &state, KRL_ACTION_USE_REASONING, 0.9, &next_state);
    kolibri_rl_update(&ctx, &state, KRL_ACTION_USE_MATH_SOLVER, 0.7, &next_state);

    KolibriRLStats stats;
    kolibri_rl_get_stats(&ctx, &stats);

    TEST_ASSERT(stats.total_updates == 3, "Update count correct");
    TEST_ASSERT(stats.total_reward > 0.0, "Total reward tracked");
    TEST_ASSERT(stats.action_counts[KRL_ACTION_USE_KNOWLEDGE_BASE] == 1, "Action counts tracked");
    /* exploration/exploitation counts are only updated via select_action, not direct update */
    TEST_ASSERT(stats.total_reward > 2.0, "Reward sum is correct (%.2f)", stats.total_reward);

    kolibri_rl_print_stats(&ctx);

    kolibri_rl_destroy(&ctx);
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(void) {
    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║  Kolibri Reinforcement Learning - Unit Test Suite        ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n");

    test_initialization();
    test_action_selection();
    test_greedy_action();
    test_q_update();
    test_experience_replay();
    test_reward_computation();
    test_epsilon_decay();
    test_state_hashing();
    test_save_load_qtable();
    test_statistics();

    printf("\n╔═══════════════════════════════════════════════════════════╗\n");
    printf("║                    TEST SUMMARY                           ║\n");
    printf("╠═══════════════════════════════════════════════════════════╣\n");
    printf("║  Total:  %4d                                           ║\n", tests_run);
    printf("║  Passed: %4d                                           ║\n", tests_passed);
    printf("║  Failed: %4d                                           ║\n", tests_failed);
    printf("╚═══════════════════════════════════════════════════════════╝\n");

    return tests_failed > 0 ? 1 : 0;
}
