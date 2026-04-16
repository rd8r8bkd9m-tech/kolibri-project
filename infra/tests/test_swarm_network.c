/*
 * Тест: Swarm Network — передача полных формул (4000 байт)
 *
 * Проверяет что формула передаётся полностью через TCP протокол,
 * без обрезки до 32 байт (прежний баг).
 */

#include "kolibri/formula.h"
#include "kolibri/net.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_BEGIN(name) printf("\n  [TEST] %s ... ", name)
#define TEST_PASS() printf("✓\n")

static void test_formula_encode_full(void) {
    TEST_BEGIN("formula encode full 4000 bytes");

    /* Создаём формулу с заполненными данными */
    KolibriFormula formula;
    memset(&formula, 0, sizeof(formula));
    formula.fitness = 4.5678;
    formula.gene.length = 4000;

    /* Заполняем все 4000 байт */
    for (int i = 0; i < 4000; i++) {
        formula.gene.digits[i] = (uint8_t)(i % 10);
    }

    /* Кодируем в сетевой буфер */
    uint8_t buffer[8192];
    size_t encoded_len = kn_message_encode_formula(buffer, sizeof(buffer), 12345, &formula);

    assert(encoded_len > 0);
    assert(encoded_len > 4000); /* Должно быть больше 4000 байт */

    /* Проверяем что заголовок правильный */
    assert(buffer[0] == KOLIBRI_MSG_MIGRATE_RULE);

    printf("(encoded=%zu bytes) ", encoded_len);
    TEST_PASS();
}

static void test_formula_no_truncation(void) {
    TEST_BEGIN("formula not truncated — all 4000 bytes preserved");

    KolibriFormula formula;
    memset(&formula, 0, sizeof(formula));
    formula.fitness = 3.14159;
    formula.gene.length = 4000;

    /* Уникальный паттерн для проверки */
    for (int i = 0; i < 4000; i++) {
        formula.gene.digits[i] = (uint8_t)((i * 7 + 3) % 10);
    }

    uint8_t buffer[8192];
    size_t encoded_len = kn_message_encode_formula(buffer, sizeof(buffer), 777, &formula);

    assert(encoded_len > 0);

    /* Проверяем что payload содержит все 4000 цифр */
    /* Структура payload: [4B node_id][2B digit_len][N bytes digits][8B fitness] */
    size_t offset = 3; /* header */

    uint32_t decoded_node_id;
    memcpy(&decoded_node_id, buffer + offset, sizeof(decoded_node_id));
    decoded_node_id = ntohl(decoded_node_id);
    offset += sizeof(decoded_node_id);

    uint16_t digit_len_be;
    memcpy(&digit_len_be, buffer + offset, sizeof(digit_len_be));
    size_t digit_len = ntohs(digit_len_be);
    offset += sizeof(digit_len_be);

    /* digit_len должен быть 4000, не 32! */
    assert(digit_len == 4000);

    /* Проверяем что все 4000 байт совпадают с оригиналом */
    int mismatches = 0;
    for (int i = 0; i < 4000; i++) {
        if (formula.gene.digits[i] != buffer[offset + i]) {
            mismatches++;
        }
    }

    assert(mismatches == 0);

    printf("(digit_len=%zu, mismatches=%d) ", digit_len, mismatches);
    TEST_PASS();
}

static void test_hello_message(void) {
    TEST_BEGIN("HELLO message encode");

    uint8_t buffer[256];
    size_t len = kn_message_encode_hello(buffer, sizeof(buffer), 42);

    assert(len > 0);
    assert(buffer[0] == KOLIBRI_MSG_HELLO);

    printf("(encoded=%zu bytes) ", len);
    TEST_PASS();
}

static void test_ack_message(void) {
    TEST_BEGIN("ACK message encode");

    uint8_t buffer[64];
    size_t len = kn_message_encode_ack(buffer, sizeof(buffer), 1);

    assert(len > 0);
    assert(buffer[0] == KOLIBRI_MSG_ACK);

    printf("(encoded=%zu bytes) ", len);
    TEST_PASS();
}

static void test_knowledge_message(void) {
    TEST_BEGIN("knowledge message encode");

    uint8_t buffer[512];
    size_t len = kn_message_encode_knowledge(buffer, sizeof(buffer), "Столица Франции?", "Париж");

    assert(len > 0);
    assert(buffer[0] == KOLIBRI_MSG_SWARM_KNOWLEDGE);

    printf("(encoded=%zu bytes) ", len);
    TEST_PASS();
}

int main(void) {
    printf("\n=== Kolibri Swarm Network Tests ===\n");

    test_hello_message();
    test_ack_message();
    test_knowledge_message();
    test_formula_encode_full();
    test_formula_no_truncation();

    printf("\n===========================================\n");
    printf("All swarm network tests passed! ✓\n");
    printf("===========================================\n");

    return 0;
}
