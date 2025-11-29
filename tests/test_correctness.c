/*
 * Kolibri Correctness Tests
 * Verify encoding/decoding accuracy for all byte values and edge cases
 * 
 * Copyright (c) 2025 Kolibri Project
 * Licensed under MIT License
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "kolibri/decimal.h"

/* Test counters */
static int tests_passed = 0;
static int tests_failed = 0;

/* Test macros */
#define TEST_START(name) printf("  Testing %s...", name)
#define TEST_PASS() do { printf(" ✓\n"); tests_passed++; } while(0)
#define TEST_FAIL(msg) do { printf(" ✗ %s\n", msg); tests_failed++; } while(0)

/* ============================================== */
/*           ALL 256 BYTE VALUES TEST            */
/* ============================================== */

static int test_all_byte_values(void) {
    TEST_START("all 256 byte values");
    
    unsigned char input[256];
    for (int i = 0; i < 256; i++) {
        input[i] = (unsigned char)i;
    }
    
    /* Encode */
    size_t encoded_size = 256 * 3;
    uint8_t *encoded = malloc(encoded_size);
    if (!encoded) {
        TEST_FAIL("memory allocation failed");
        return 0;
    }
    
    k_digit_stream stream;
    k_digit_stream_init(&stream, encoded, encoded_size);
    
    if (k_transduce_utf8(&stream, input, 256) != 0) {
        TEST_FAIL("encoding failed");
        free(encoded);
        return 0;
    }
    
    if (stream.length != 768) {
        TEST_FAIL("unexpected encoded length");
        free(encoded);
        return 0;
    }
    
    /* Decode */
    unsigned char decoded[256];
    size_t written = 0;
    
    if (k_emit_utf8(&stream, decoded, 256, &written) != 0) {
        TEST_FAIL("decoding failed");
        free(encoded);
        return 0;
    }
    
    /* Verify */
    if (written != 256) {
        TEST_FAIL("incorrect decoded length");
        free(encoded);
        return 0;
    }
    
    for (int i = 0; i < 256; i++) {
        if (decoded[i] != input[i]) {
            char msg[64];
            snprintf(msg, sizeof(msg), "byte %d: expected %d, got %d", i, input[i], decoded[i]);
            TEST_FAIL(msg);
            free(encoded);
            return 0;
        }
    }
    
    TEST_PASS();
    free(encoded);
    return 1;
}

/* ============================================== */
/*           ROUNDTRIP TEST                      */
/* ============================================== */

static int test_roundtrip_simple(void) {
    TEST_START("simple roundtrip");
    
    const char *test_strings[] = {
        "Hello, World!",
        "Kolibri",
        "0123456789",
        "abcdefghijklmnopqrstuvwxyz",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
        "!@#$%^&*()_+-=[]{}|;':\",./<>?",
        NULL
    };
    
    for (int i = 0; test_strings[i] != NULL; i++) {
        const char *input = test_strings[i];
        size_t len = strlen(input);
        
        /* Encode */
        size_t encoded_size = len * 3;
        uint8_t *encoded = malloc(encoded_size);
        if (!encoded) continue;
        
        k_digit_stream stream;
        k_digit_stream_init(&stream, encoded, encoded_size);
        
        if (k_transduce_utf8(&stream, (const unsigned char *)input, len) != 0) {
            TEST_FAIL("encoding failed");
            free(encoded);
            return 0;
        }
        
        /* Decode */
        char *decoded = malloc(len + 1);
        if (!decoded) {
            free(encoded);
            continue;
        }
        
        size_t written = 0;
        if (k_emit_utf8(&stream, (unsigned char *)decoded, len, &written) != 0) {
            TEST_FAIL("decoding failed");
            free(encoded);
            free(decoded);
            return 0;
        }
        decoded[written] = '\0';
        
        /* Verify */
        if (written != len || strcmp(input, decoded) != 0) {
            char msg[64];
            snprintf(msg, sizeof(msg), "string %d mismatch", i);
            TEST_FAIL(msg);
            free(encoded);
            free(decoded);
            return 0;
        }
        
        free(encoded);
        free(decoded);
    }
    
    TEST_PASS();
    return 1;
}

/* ============================================== */
/*           EDGE CASES TEST                     */
/* ============================================== */

static int test_edge_cases(void) {
    TEST_START("edge cases (0x00, 0xFF, etc.)");
    
    unsigned char edge_cases[] = {
        0x00,  /* NULL byte */
        0x01,  /* Start of heading */
        0x7F,  /* DEL */
        0x80,  /* First extended ASCII */
        0xFE,  /* Second to last */
        0xFF,  /* Last byte value */
    };
    int num_cases = sizeof(edge_cases) / sizeof(edge_cases[0]);
    
    for (int i = 0; i < num_cases; i++) {
        unsigned char byte = edge_cases[i];
        
        /* Encode single byte */
        uint8_t encoded[3];
        k_digit_stream stream;
        k_digit_stream_init(&stream, encoded, 3);
        
        if (k_transduce_utf8(&stream, &byte, 1) != 0) {
            char msg[64];
            snprintf(msg, sizeof(msg), "encoding 0x%02X failed", byte);
            TEST_FAIL(msg);
            return 0;
        }
        
        /* Verify encoded value */
        unsigned int expected_val = byte;
        unsigned int encoded_val = encoded[0] * 100 + encoded[1] * 10 + encoded[2];
        
        if (encoded_val != expected_val) {
            char msg[64];
            snprintf(msg, sizeof(msg), "0x%02X encoded as %03u (expected %03u)", 
                     byte, encoded_val, expected_val);
            TEST_FAIL(msg);
            return 0;
        }
        
        /* Decode and verify */
        unsigned char decoded;
        size_t written = 0;
        
        if (k_emit_utf8(&stream, &decoded, 1, &written) != 0) {
            char msg[64];
            snprintf(msg, sizeof(msg), "decoding 0x%02X failed", byte);
            TEST_FAIL(msg);
            return 0;
        }
        
        if (decoded != byte) {
            char msg[64];
            snprintf(msg, sizeof(msg), "0x%02X decoded as 0x%02X", byte, decoded);
            TEST_FAIL(msg);
            return 0;
        }
    }
    
    TEST_PASS();
    return 1;
}

/* ============================================== */
/*           UTF-8 COMPATIBILITY TEST            */
/* ============================================== */

static int test_utf8_compatibility(void) {
    TEST_START("UTF-8 compatibility");
    
    /* Test multi-byte UTF-8 sequences */
    const unsigned char utf8_test[] = {
        /* "Привет" in UTF-8 */
        0xD0, 0x9F, 0xD1, 0x80, 0xD0, 0xB8, 0xD0, 0xB2, 
        0xD0, 0xB5, 0xD1, 0x82,
        /* "日本語" in UTF-8 */
        0xE6, 0x97, 0xA5, 0xE6, 0x9C, 0xAC, 0xE8, 0xAA, 0x9E,
        /* "🚀" emoji in UTF-8 */
        0xF0, 0x9F, 0x9A, 0x80,
    };
    size_t len = sizeof(utf8_test);
    
    /* Encode */
    size_t encoded_size = len * 3;
    uint8_t *encoded = malloc(encoded_size);
    if (!encoded) {
        TEST_FAIL("memory allocation failed");
        return 0;
    }
    
    k_digit_stream stream;
    k_digit_stream_init(&stream, encoded, encoded_size);
    
    if (k_transduce_utf8(&stream, utf8_test, len) != 0) {
        TEST_FAIL("encoding failed");
        free(encoded);
        return 0;
    }
    
    /* Decode */
    unsigned char *decoded = malloc(len);
    if (!decoded) {
        TEST_FAIL("memory allocation failed");
        free(encoded);
        return 0;
    }
    
    size_t written = 0;
    if (k_emit_utf8(&stream, decoded, len, &written) != 0) {
        TEST_FAIL("decoding failed");
        free(encoded);
        free(decoded);
        return 0;
    }
    
    /* Verify */
    if (written != len || memcmp(utf8_test, decoded, len) != 0) {
        TEST_FAIL("roundtrip mismatch");
        free(encoded);
        free(decoded);
        return 0;
    }
    
    TEST_PASS();
    free(encoded);
    free(decoded);
    return 1;
}

/* ============================================== */
/*           LARGE FILE TEST                     */
/* ============================================== */

static int test_large_file(void) {
    TEST_START("large file (1MB)");
    
    size_t size = 1024 * 1024;  /* 1 MB */
    
    /* Generate test data */
    unsigned char *input = malloc(size);
    if (!input) {
        TEST_FAIL("memory allocation failed");
        return 0;
    }
    
    for (size_t i = 0; i < size; i++) {
        input[i] = (unsigned char)((i * 73 + 17) % 256);
    }
    
    /* Encode */
    size_t encoded_size = size * 3;
    uint8_t *encoded = malloc(encoded_size);
    if (!encoded) {
        TEST_FAIL("memory allocation failed");
        free(input);
        return 0;
    }
    
    k_digit_stream stream;
    k_digit_stream_init(&stream, encoded, encoded_size);
    
    if (k_transduce_utf8(&stream, input, size) != 0) {
        TEST_FAIL("encoding failed");
        free(input);
        free(encoded);
        return 0;
    }
    
    /* Decode */
    unsigned char *decoded = malloc(size);
    if (!decoded) {
        TEST_FAIL("memory allocation failed");
        free(input);
        free(encoded);
        return 0;
    }
    
    size_t written = 0;
    if (k_emit_utf8(&stream, decoded, size, &written) != 0) {
        TEST_FAIL("decoding failed");
        free(input);
        free(encoded);
        free(decoded);
        return 0;
    }
    
    /* Verify */
    if (written != size) {
        TEST_FAIL("incorrect decoded length");
        free(input);
        free(encoded);
        free(decoded);
        return 0;
    }
    
    if (memcmp(input, decoded, size) != 0) {
        /* Find first mismatch */
        for (size_t i = 0; i < size; i++) {
            if (input[i] != decoded[i]) {
                char msg[64];
                snprintf(msg, sizeof(msg), "mismatch at offset %zu", i);
                TEST_FAIL(msg);
                break;
            }
        }
        free(input);
        free(encoded);
        free(decoded);
        return 0;
    }
    
    TEST_PASS();
    free(input);
    free(encoded);
    free(decoded);
    return 1;
}

/* ============================================== */
/*           DIGIT STREAM API TEST               */
/* ============================================== */

static int test_digit_stream_api(void) {
    TEST_START("digit stream API");
    
    uint8_t buffer[100];
    k_digit_stream stream;
    
    /* Test init */
    k_digit_stream_init(&stream, buffer, 100);
    if (stream.length != 0 || stream.cursor != 0 || stream.capacity != 100) {
        TEST_FAIL("init failed");
        return 0;
    }
    
    /* Test push */
    for (uint8_t i = 0; i < 10; i++) {
        if (k_digit_stream_push(&stream, i) != 0) {
            TEST_FAIL("push failed");
            return 0;
        }
    }
    
    if (stream.length != 10) {
        TEST_FAIL("incorrect length after push");
        return 0;
    }
    
    /* Test read */
    for (uint8_t i = 0; i < 10; i++) {
        uint8_t digit;
        if (k_digit_stream_read(&stream, &digit) != 0) {
            TEST_FAIL("read failed");
            return 0;
        }
        if (digit != i) {
            TEST_FAIL("incorrect digit read");
            return 0;
        }
    }
    
    /* Test remaining */
    if (k_digit_stream_remaining(&stream) != 0) {
        TEST_FAIL("remaining should be 0");
        return 0;
    }
    
    /* Test rewind */
    k_digit_stream_rewind(&stream);
    if (stream.cursor != 0 || k_digit_stream_remaining(&stream) != 10) {
        TEST_FAIL("rewind failed");
        return 0;
    }
    
    /* Test reset */
    k_digit_stream_reset(&stream);
    if (stream.length != 0 || stream.cursor != 0) {
        TEST_FAIL("reset failed");
        return 0;
    }
    
    /* Test invalid digit */
    if (k_digit_stream_push(&stream, 10) != -1) {
        TEST_FAIL("should reject digit > 9");
        return 0;
    }
    
    TEST_PASS();
    return 1;
}

/* ============================================== */
/*           TEXT API TEST                       */
/* ============================================== */

static int test_text_api(void) {
    TEST_START("text API");
    
    const char *test_input = "Hello";
    
    /* Test encode length */
    size_t encode_len = k_encode_text_length(strlen(test_input));
    if (encode_len != 16) {  /* 5 chars * 3 + 1 null */
        TEST_FAIL("incorrect encode length");
        return 0;
    }
    
    /* Test encode */
    char encoded[20];
    if (k_encode_text(test_input, encoded, sizeof(encoded)) != 0) {
        TEST_FAIL("encode failed");
        return 0;
    }
    
    /* Verify encoded format */
    /* H=72 -> "072", e=101 -> "101", l=108 -> "108", o=111 -> "111" */
    if (strcmp(encoded, "072101108108111") != 0) {
        char msg[64];
        snprintf(msg, sizeof(msg), "unexpected encoding: %s", encoded);
        TEST_FAIL(msg);
        return 0;
    }
    
    /* Test decode length */
    size_t decode_len = k_decode_text_length(strlen(encoded));
    if (decode_len != 6) {  /* 15 / 3 + 1 null */
        TEST_FAIL("incorrect decode length");
        return 0;
    }
    
    /* Test decode */
    char decoded[10];
    if (k_decode_text(encoded, decoded, sizeof(decoded)) != 0) {
        TEST_FAIL("decode failed");
        return 0;
    }
    
    if (strcmp(test_input, decoded) != 0) {
        char msg[64];
        snprintf(msg, sizeof(msg), "mismatch: expected '%s', got '%s'", test_input, decoded);
        TEST_FAIL(msg);
        return 0;
    }
    
    TEST_PASS();
    return 1;
}

/* ============================================== */
/*           ERROR HANDLING TEST                 */
/* ============================================== */

static int test_error_handling(void) {
    TEST_START("error handling");
    
    uint8_t buffer[10];
    k_digit_stream stream;
    k_digit_stream_init(&stream, buffer, 10);
    
    /* Test NULL input */
    if (k_transduce_utf8(NULL, (const unsigned char *)"test", 4) != -1) {
        TEST_FAIL("should fail with NULL stream");
        return 0;
    }
    
    if (k_transduce_utf8(&stream, NULL, 4) != -1) {
        TEST_FAIL("should fail with NULL bytes");
        return 0;
    }
    
    /* Test buffer overflow */
    k_digit_stream_init(&stream, buffer, 3);  /* Only room for 1 byte encoded */
    unsigned char test_data[] = {0x00, 0x01};  /* 2 bytes need 6 digits */
    
    if (k_transduce_utf8(&stream, test_data, 2) != -1) {
        TEST_FAIL("should fail with buffer overflow");
        return 0;
    }
    
    /* Test invalid decode length */
    k_digit_stream_init(&stream, buffer, 10);
    stream.length = 4;  /* Not divisible by 3 */
    
    unsigned char decoded[10];
    size_t written;
    if (k_emit_utf8(&stream, decoded, 10, &written) != -1) {
        TEST_FAIL("should fail with invalid length");
        return 0;
    }
    
    TEST_PASS();
    return 1;
}

/* ============================================== */
/*           MAIN                                */
/* ============================================== */

int main(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║     KOLIBRI CORRECTNESS TEST SUITE                           ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");
    
    /* Run tests */
    test_all_byte_values();
    test_roundtrip_simple();
    test_edge_cases();
    test_utf8_compatibility();
    test_large_file();
    test_digit_stream_api();
    test_text_api();
    test_error_handling();
    
    /* Print summary */
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  RESULTS: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("═══════════════════════════════════════════════════════════════\n\n");
    
    return tests_failed > 0 ? 1 : 0;
}
