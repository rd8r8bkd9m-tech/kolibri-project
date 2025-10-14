#include "kolibri/digit_text.h"

#include <stdlib.h>
#include <string.h>

static int kolibri_digit_text_validate_digits(const uint8_t *digits, size_t length) {
    if (!digits && length > 0U) {
        return -1;
    }
    for (size_t i = 0; i < length; ++i) {
        if (digits[i] > 9U) {
            return -1;
        }
    }
    return 0;
}

static int kolibri_digit_text_ensure_capacity(KolibriDigitText *text, size_t digits_capacity) {
    if (!text) {
        return -1;
    }
    if (digits_capacity <= text->capacity) {
        return 0;
    }
    uint8_t *new_digits = (uint8_t *)realloc(text->digits, digits_capacity * sizeof(uint8_t));
    if (!new_digits) {
        return -1;
    }
    text->digits = new_digits;
    text->capacity = digits_capacity;
    return 0;
}

void kolibri_digit_text_init(KolibriDigitText *text) {
    if (!text) {
        return;
    }
    text->digits = NULL;
    text->length = 0;
    text->capacity = 0;
}

void kolibri_digit_text_free(KolibriDigitText *text) {
    if (!text) {
        return;
    }
    free(text->digits);
    text->digits = NULL;
    text->length = 0;
    text->capacity = 0;
}

int kolibri_digit_text_reserve(KolibriDigitText *text, size_t digits_capacity) {
    return kolibri_digit_text_ensure_capacity(text, digits_capacity);
}

int kolibri_digit_text_assign_digits(KolibriDigitText *text, const uint8_t *digits, size_t length) {
    if (!text) {
        return -1;
    }
    if (length == 0) {
        text->length = 0;
        return 0;
    }
    if (!digits) {
        return -1;
    }
    if (kolibri_digit_text_validate_digits(digits, length) != 0) {
        return -1;
    }
    if (kolibri_digit_text_ensure_capacity(text, length) != 0) {
        return -1;
    }
    memcpy(text->digits, digits, length * sizeof(uint8_t));
    text->length = length;
    return 0;
}

int kolibri_digit_text_assign_utf8(KolibriDigitText *text, const char *utf8) {
    if (!text) {
        return -1;
    }
    if (!utf8) {
        text->length = 0;
        return 0;
    }
    size_t len = strlen(utf8);
    size_t needed = len * 3U;
    if (kolibri_digit_text_ensure_capacity(text, needed) != 0) {
        return -1;
    }
    k_digit_stream stream;
    k_digit_stream_init(&stream, text->digits, text->capacity);
    k_digit_stream_reset(&stream);
    if (k_transduce_utf8(&stream, (const unsigned char *)utf8, len) != 0) {
        return -1;
    }
    if (kolibri_digit_text_validate_digits(stream.digits, stream.length) != 0) {
        return -1;
    }
    text->length = stream.length;
    return 0;
}

int kolibri_digit_text_clone(const KolibriDigitText *src, KolibriDigitText *dst) {
    if (!src || !dst) {
        return -1;
    }
    if (kolibri_digit_text_assign_digits(dst, src->digits, src->length) != 0) {
        return -1;
    }
    return 0;
}

bool kolibri_digit_text_equals(const KolibriDigitText *lhs, const KolibriDigitText *rhs) {
    if (lhs == rhs) {
        return true;
    }
    if (!lhs || !rhs) {
        return false;
    }
    if (lhs->length != rhs->length) {
        return false;
    }
    if (lhs->length == 0) {
        return true;
    }
    return memcmp(lhs->digits, rhs->digits, lhs->length) == 0;
}

bool kolibri_digit_text_equals_utf8(const KolibriDigitText *lhs, const char *utf8) {
    if (!lhs || !utf8) {
        return lhs && lhs->length == 0;
    }
    size_t len = strlen(utf8);
    if (lhs->length != len * 3U) {
        return false;
    }
    if (len == 0) {
        return lhs->length == 0;
    }
    k_digit_stream stream = {
        .digits = (uint8_t *)lhs->digits,
        .capacity = lhs->length,
        .length = lhs->length,
        .cursor = 0
    };
    unsigned char decoded_stack[256];
    size_t decoded_len = 0;
    if (lhs->length / 3U <= sizeof(decoded_stack)) {
        if (k_emit_utf8(&stream, decoded_stack, sizeof(decoded_stack), &decoded_len) != 0) {
            return false;
        }
        if (decoded_len != len) {
            return false;
        }
        return memcmp(decoded_stack, utf8, len) == 0;
    }
    char *decoded = (char *)malloc(len);
    if (!decoded) {
        return false;
    }
    if (k_emit_utf8(&stream, (unsigned char *)decoded, len, &decoded_len) != 0 || decoded_len != len) {
        free(decoded);
        return false;
    }
    bool equal = memcmp(decoded, utf8, len) == 0;
    free(decoded);
    return equal;
}

int kolibri_digit_text_to_utf8(const KolibriDigitText *text, char **out_utf8) {
    if (!text || !out_utf8) {
        return -1;
    }
    size_t expected = text->length == 0 ? 0 : text->length / 3U;
    char *buffer = (char *)malloc(expected + 1U);
    if (!buffer) {
        return -1;
    }
    if (expected > 0U) {
        k_digit_stream stream = {
            .digits = (uint8_t *)text->digits,
            .capacity = text->length,
            .length = text->length,
            .cursor = 0
        };
        size_t produced = 0;
        if (k_emit_utf8(&stream, (unsigned char *)buffer, expected, &produced) != 0 || produced != expected) {
            free(buffer);
            return -1;
        }
    }
    buffer[expected] = '\0';
    *out_utf8 = buffer;
    return 0;
}
