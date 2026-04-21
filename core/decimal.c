/*
 * decimal.c - Слой десятичного мышления (Decimal Cognition)
 * Исправленная инициализация потоков.
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/decimal.h"
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

size_t k_digits_for_bytes(size_t byte_len) {
    return byte_len * 3U;
}

size_t k_bytes_for_digits(size_t digit_len) {
    return digit_len / 3U;
}

void k_digit_stream_init(k_digit_stream *stream, uint8_t *buffer, size_t capacity) {
    if (!stream) return;
    stream->digits = buffer;
    stream->capacity = capacity;
    stream->length = 0;
    stream->cursor = 0;
    /* УБРАНО: memset затирал данные при повторном использовании буфера */
}

void k_digit_stream_reset(k_digit_stream *stream) {
    if (!stream) return;
    stream->length = 0;
    stream->cursor = 0;
}

void k_digit_stream_rewind(k_digit_stream *stream) {
    if (stream) stream->cursor = 0;
}

int k_digit_stream_push(k_digit_stream *stream, uint8_t digit) {
    if (!stream || digit > 9 || stream->length >= stream->capacity) return -1;
    stream->digits[stream->length++] = digit;
    return 0;
}

int k_digit_stream_read(k_digit_stream *stream, uint8_t *digit) {
    if (!stream || !digit) return -1;
    if (stream->cursor >= stream->length) return 1;
    *digit = stream->digits[stream->cursor++];
    return 0;
}

size_t k_digit_stream_remaining(const k_digit_stream *stream) {
    return (stream && stream->cursor < stream->length) ? stream->length - stream->cursor : 0;
}

int k_validate_digit_stream(const k_digit_stream *stream) {
    if (!stream || (!stream->digits && stream->length > 0)) return 0;
    if (stream->length > stream->capacity) return 0;
    for (size_t i = 0; i < stream->length; ++i) {
        if (stream->digits[i] > 9U) return 0;
    }
    return 1;
}

int k_validate_canonical_byte_digits(const k_digit_stream *stream) {
    if (!k_validate_digit_stream(stream)) return 0;
    if (stream->length % 3U != 0) return 0;
    for (size_t i = 0; i < stream->length; i += 3U) {
        unsigned int value = (unsigned int)(stream->digits[i] * 100U +
                                            stream->digits[i + 1U] * 10U +
                                            stream->digits[i + 2U]);
        if (value > 255U) return 0;
    }
    return 1;
}

int k_encode_bytes_to_digits(const uint8_t *input, size_t input_len, k_digit_stream *out) {
    if (!out || (!input && input_len > 0)) return -1;
    if (!out->digits && input_len > 0) return -1;
    if (out->length > out->capacity) return -1;
    if (out->capacity - out->length < k_digits_for_bytes(input_len)) return -1;

    for (size_t i = 0; i < input_len; ++i) {
        uint8_t b = input[i];
        if (k_digit_stream_push(out, (uint8_t)(b / 100U)) != 0) return -1;
        if (k_digit_stream_push(out, (uint8_t)((b / 10U) % 10U)) != 0) return -1;
        if (k_digit_stream_push(out, (uint8_t)(b % 10U)) != 0) return -1;
    }
    return 0;
}

int k_decode_digits_to_bytes(const k_digit_stream *digits, uint8_t *out, size_t out_len, size_t *written) {
    if (written) *written = 0;
    if (!digits || (!out && digits->length > 0)) return -1;
    if (!k_validate_canonical_byte_digits(digits)) return -1;

    size_t expected = k_bytes_for_digits(digits->length);
    if (out_len < expected) return -1;
    for (size_t i = 0; i < expected; ++i) {
        size_t off = i * 3U;
        out[i] = (uint8_t)(digits->digits[off] * 100U +
                           digits->digits[off + 1U] * 10U +
                           digits->digits[off + 2U]);
    }
    if (written) *written = expected;
    return 0;
}

int k_digits_to_ascii(const k_digit_stream *stream, char *out, size_t out_len) {
    if (!stream || !out) return -1;
    if (!k_validate_digit_stream(stream)) return -1;
    if (out_len < stream->length + 1U) return -1;
    for (size_t i = 0; i < stream->length; ++i) {
        out[i] = (char)('0' + stream->digits[i]);
    }
    out[stream->length] = '\0';
    return 0;
}

int k_ascii_to_digits(const char *ascii, k_digit_stream *out) {
    if (!ascii || !out) return -1;
    size_t len = strlen(ascii);
    if (!out->digits && len > 0) return -1;
    if (out->length > out->capacity) return -1;
    if (out->capacity - out->length < len) return -1;
    for (size_t i = 0; i < len; ++i) {
        if (!isdigit((unsigned char)ascii[i])) return -1;
        if (k_digit_stream_push(out, (uint8_t)(ascii[i] - '0')) != 0) return -1;
    }
    return 0;
}

int k_transduce_utf8(k_digit_stream *stream, const unsigned char *bytes, size_t len) {
    return k_encode_bytes_to_digits(bytes, len, stream);
}

int k_emit_utf8(const k_digit_stream *stream, unsigned char *out, size_t out_len, size_t *written) {
    return k_decode_digits_to_bytes(stream, out, out_len, written);
}

size_t k_encode_text_length(size_t input_len) {
    return input_len * 3;
}

size_t k_decode_text_length(size_t digits_len) {
    return digits_len / 3;
}

int k_encode_text(const char *input, char *out, size_t out_len) {
    if (!input || !out) return -1;
    size_t len = strlen(input);
    if (out_len < len * 3 + 1) return -1;
    for (size_t i = 0; i < len; ++i) {
        unsigned char b = (unsigned char)input[i];
        out[i*3]   = (char)((b / 100U) + '0');
        out[i*3+1] = (char)(((b / 10U) % 10U) + '0');
        out[i*3+2] = (char)((b % 10U) + '0');
    }
    out[len*3] = '\0';
    return 0;
}

int k_decode_text(const char *digits, char *out, size_t out_len) {
    if (!digits || !out) return -1;
    size_t len = strlen(digits);
    if (len % 3 != 0 || out_len < len / 3 + 1) return -1;
    for (size_t i = 0; i < len / 3; ++i) {
        size_t off = i * 3;
        if (!isdigit((unsigned char)digits[off]) ||
            !isdigit((unsigned char)digits[off + 1]) ||
            !isdigit((unsigned char)digits[off + 2])) {
            return -1;
        }
        int value = (digits[off]-'0')*100 + (digits[off+1]-'0')*10 + (digits[off+2]-'0');
        if (value > 255) return -1;
        out[i] = (char)value;
    }
    out[len/3] = '\0';
    return 0;
}

int k_encode_uint64(k_digit_stream *stream, uint64_t value) {
    if (!stream) return -1;
    if (value == 0) return k_digit_stream_push(stream, 0);

    uint8_t tmp[20];
    size_t count = 0;
    while (value > 0 && count < sizeof(tmp)) {
        tmp[count++] = (uint8_t)(value % 10U);
        value /= 10U;
    }
    for (size_t i = 0; i < count; ++i) {
        if (k_digit_stream_push(stream, tmp[count - 1U - i]) != 0) return -1;
    }
    return 0;
}

int k_decode_uint64(const k_digit_stream *stream, uint64_t *value) {
    if (!stream || !value || !k_validate_digit_stream(stream)) return -1;
    uint64_t out = 0;
    for (size_t i = 0; i < stream->length; ++i) {
        if (out > (UINT64_MAX - stream->digits[i]) / 10U) return -1;
        out = out * 10U + stream->digits[i];
    }
    *value = out;
    return 0;
}

int k_encode_double(k_digit_stream *stream, double value) {
    if (!stream) return -1;
    char buf[64];
    int written = snprintf(buf, sizeof(buf), "%.6f", value);
    if (written < 0 || (size_t)written >= sizeof(buf)) return -1;
    for (int i = 0; i < written; ++i) {
        unsigned char ch = (unsigned char)buf[i];
        if (isdigit(ch)) {
            if (k_digit_stream_push(stream, (uint8_t)(ch - '0')) != 0) return -1;
        } else if (ch == '.') {
            if (k_digit_stream_push(stream, 8) != 0) return -1;
        } else if (ch == '-') {
            if (k_digit_stream_push(stream, 9) != 0) return -1;
        }
    }
    return 0;
}

int k_strcmp(const char *a, const char *b) {
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    return strcmp(a, b);
}

int k_strcmp_digits(const char *str, const char *digits) {
    if (!str && !digits) return 0;
    if (!str) return -1;
    if (!digits) return 1;
    size_t len = strlen(str);
    size_t needed = k_encode_text_length(len) + 1U;
    char encoded[1024];
    if (needed > sizeof(encoded)) return -1;
    if (k_encode_text(str, encoded, sizeof(encoded)) != 0) return -1;
    return strcmp(encoded, digits);
}

int k_is_pure_decimal(const char *str, size_t len) {
    if (!str && len > 0) return 0;
    for (size_t i = 0; i < len; ++i) {
        if (!isdigit((unsigned char)str[i])) return 0;
    }
    return 1;
}

int k_validate_genome(const char *genome) {
    return genome && strlen(genome) == 64U && k_is_pure_decimal(genome, 64U);
}

int k_normalize_input(const char *input, char *out, size_t out_len, size_t *written) {
    if (written) *written = 0;
    if (!input || !out || out_len == 0) return -1;

    size_t pos = 0;
    for (size_t i = 0; input[i] != '\0'; ++i) {
        if (!isdigit((unsigned char)input[i])) continue;
        if (pos + 1U >= out_len) return -1;
        out[pos++] = input[i];
    }
    out[pos] = '\0';
    if (written) *written = pos;
    return 0;
}

int k_triplet_pack(k_digit_stream *stream) {
    if (!stream || !k_validate_canonical_byte_digits(stream)) return -1;
    return 0;
}

int k_digit_hash(const char *input, size_t len, char *out64) {
    if (!input || !out64) return -1;
    uint64_t h1 = 14695981039346656037ULL, h2 = h1;
    for (size_t i = 0; i < len; ++i) {
        h1 = (h1 ^ (unsigned char)input[i]) * 1099511628211ULL;
        h2 = (h2 ^ ((unsigned char)input[i] + i)) * 1099511628211ULL;
    }
    for (int i = 0; i < 32; ++i) { out64[i] = (char)((h1 % 10) + '0'); h1 /= 10; if (!h1) h1 = h2 + i; }
    for (int i = 32; i < 64; ++i) { out64[i] = (char)((h2 % 10) + '0'); h2 /= 10; if (!h2) h2 = h1 + i; }
    out64[64] = '\0';
    return 0;
}
