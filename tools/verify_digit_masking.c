#include "kolibri/digit_text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int check_sample(const char *utf8) {
    KolibriDigitText text;
    kolibri_digit_text_init(&text);
    int rc = kolibri_digit_text_assign_utf8(&text, utf8);
    if (rc != 0) {
        fprintf(stderr, "kolibri_digit_text_assign_utf8 failed for sample \"%s\"\n", utf8);
        kolibri_digit_text_free(&text);
        return -1;
    }
    for (size_t i = 0; i < text.length; ++i) {
        if (text.digits[i] > 9U) {
            fprintf(stderr, "unmasked digit %u detected in sample \"%s\"\n", (unsigned)text.digits[i], utf8);
            kolibri_digit_text_free(&text);
            return -1;
        }
    }
    char *roundtrip = NULL;
    if (kolibri_digit_text_to_utf8(&text, &roundtrip) != 0) {
        fprintf(stderr, "kolibri_digit_text_to_utf8 failed for sample \"%s\"\n", utf8);
        kolibri_digit_text_free(&text);
        return -1;
    }
    if (strcmp(roundtrip, utf8) != 0) {
        fprintf(stderr, "roundtrip mismatch: \"%s\" != \"%s\"\n", utf8, roundtrip);
        free(roundtrip);
        kolibri_digit_text_free(&text);
        return -1;
    }
    free(roundtrip);
    kolibri_digit_text_free(&text);
    return 0;
}

static int check_invalid_digits(void) {
    KolibriDigitText slot;
    kolibri_digit_text_init(&slot);
    uint8_t invalid_digits[] = { 1U, 10U, 0U };
    if (kolibri_digit_text_assign_digits(&slot, invalid_digits, sizeof(invalid_digits)) == 0) {
        fprintf(stderr, "kolibri_digit_text_assign_digits accepted invalid input\n");
        kolibri_digit_text_free(&slot);
        return -1;
    }
    kolibri_digit_text_free(&slot);
    return 0;
}

int main(void) {
    const char *samples[] = {
        "привет",
        "как дела",
        "Кристаллическое ядро активно",
        "12345",
        ""
    };
    size_t sample_count = sizeof(samples) / sizeof(samples[0]);
    for (size_t i = 0; i < sample_count; ++i) {
        if (check_sample(samples[i]) != 0) {
            return EXIT_FAILURE;
        }
    }
    if (check_invalid_digits() != 0) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
