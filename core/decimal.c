/*
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/decimal.h"

#include <ctype.h>
#include <stdint.h>
#include <string.h>

static int ensure_space(k_digit_stream *stream) {
    if (!stream || !stream->digits) {
        return -1;
    }
    if (stream->length >= stream->capacity) {
        return -1;
    }
    return 0;
}

void k_digit_stream_init(k_digit_stream *stream, uint8_t *buffer, size_t capacity) {
    if (!stream) {
        return;
    }
    stream->digits = buffer;
    stream->capacity = capacity;
    stream->length = 0;
    stream->cursor = 0;
    if (stream->digits && stream->capacity > 0) {
        memset(stream->digits, 0, stream->capacity);
    }
}

void k_digit_stream_reset(k_digit_stream *stream) {
    if (!stream || !stream->digits) {
        return;
    }
    memset(stream->digits, 0, stream->capacity);
    stream->length = 0;
    stream->cursor = 0;
}

void k_digit_stream_rewind(k_digit_stream *stream) {
    if (!stream) {
        return;
    }
    stream->cursor = 0;
}

int k_digit_stream_push(k_digit_stream *stream, uint8_t digit) {
    if (digit > 9) {
        return -1;
    }
    if (ensure_space(stream) != 0) {
        return -1;
    }
    stream->digits[stream->length++] = digit;
    return 0;
}

int k_digit_stream_read(k_digit_stream *stream, uint8_t *digit) {
    if (!stream || !digit) {
        return -1;
    }
    if (stream->cursor >= stream->length) {
        return 1;
    }
    *digit = stream->digits[stream->cursor++];
    return 0;
}

size_t k_digit_stream_remaining(const k_digit_stream *stream) {
    if (!stream) {
        return 0;
    }
    if (stream->cursor >= stream->length) {
        return 0;
    }
    return stream->length - stream->cursor;
}

static int encode_byte(k_digit_stream *stream, unsigned char value) {
    uint8_t hundreds = (uint8_t)(value / 100U);
    uint8_t tens = (uint8_t)((value / 10U) % 10U);
    uint8_t ones = (uint8_t)(value % 10U);
    if (k_digit_stream_push(stream, hundreds) != 0 ||
        k_digit_stream_push(stream, tens) != 0 ||
        k_digit_stream_push(stream, ones) != 0) {
        return -1;
    }
    return 0;
}

int k_transduce_utf8(k_digit_stream *stream, const unsigned char *bytes, size_t len) {
    if (!stream || !bytes) {
        return -1;
    }
    for (size_t i = 0; i < len; ++i) {
        if (encode_byte(stream, bytes[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

int k_emit_utf8(const k_digit_stream *stream, unsigned char *out, size_t out_len,
                size_t *written) {
    if (!stream || !out) {
        return -1;
    }
    if (stream->length % 3 != 0) {
        return -1;
    }
    size_t expected = stream->length / 3;
    if (out_len < expected) {
        return -1;
    }
    for (size_t i = 0; i < expected; ++i) {
        size_t offset = i * 3U;
        unsigned int value = (unsigned int)(stream->digits[offset] * 100U +
                                            stream->digits[offset + 1] * 10U +
                                            stream->digits[offset + 2]);
        out[i] = (unsigned char)value;
    }
    if (written) {
        *written = expected;
    }
    return 0;
}

size_t k_encode_text_length(size_t input_len) {
    return input_len * 3 + 1;
}

size_t k_decode_text_length(size_t digits_len) {
    if (digits_len % 3 != 0) {
        return 0;
    }
    return digits_len / 3 + 1;
}

int k_encode_text(const char *input, char *out, size_t out_len) {
    if (!input || !out) {
        return -1;
    }
    size_t len = strlen(input);
    size_t need = len * 3U + 1U;
    if (out_len < need) {
        return -1;
    }
    uint8_t buffer[512];
    if (len * 3U > sizeof(buffer)) {
        return -1;
    }
    k_digit_stream stream;
    k_digit_stream_init(&stream, buffer, sizeof(buffer));
    if (k_transduce_utf8(&stream, (const unsigned char *)input, len) != 0) {
        return -1;
    }
    for (size_t i = 0; i < stream.length; ++i) {
        out[i] = (char)('0' + stream.digits[i]);
    }
    out[stream.length] = '\0';
    return 0;
}

int k_decode_text(const char *digits, char *out, size_t out_len) {
    if (!digits || !out) {
        return -1;
    }
    size_t len = strlen(digits);
    if (len % 3U != 0U) {
        return -1;
    }
    size_t expected = len / 3U;
    if (out_len < expected + 1U) {
        return -1;
    }
    uint8_t buffer[512];
    if (len > sizeof(buffer)) {
        return -1;
    }
    k_digit_stream stream;
    k_digit_stream_init(&stream, buffer, len);
    for (size_t i = 0; i < len; ++i) {
        if (digits[i] < '0' || digits[i] > '9') {
            return -1;
        }
        stream.digits[i] = (uint8_t)(digits[i] - '0');
    }
    stream.length = len;
    unsigned char decoded[256];
    size_t produced = 0;
    if (k_emit_utf8(&stream, decoded, sizeof(decoded), &produced) != 0) {
        return -1;
    }
    for (size_t i = 0; i < produced; ++i) {
        out[i] = (char)decoded[i];
    }
    out[produced] = '\0';
    return 0;
}

/* ========================================================================== */
/* --- Числовые преобразования --- */
/* ========================================================================== */

int k_encode_uint64(k_digit_stream *stream, uint64_t value) {
    if (!stream) {
        return -1;
    }
    
    /* Максимум 20 цифр для uint64_t */
    uint8_t temp[20];
    int count = 0;
    
    if (value == 0) {
        return k_digit_stream_push(stream, 0);
    }
    
    while (value > 0 && count < 20) {
        temp[count++] = (uint8_t)(value % 10U);
        value /= 10U;
    }
    
    /* Записываем в обратном порядке */
    for (int i = count - 1; i >= 0; --i) {
        if (k_digit_stream_push(stream, temp[i]) != 0) {
            return -1;
        }
    }
    
    return 0;
}

int k_decode_uint64(const k_digit_stream *stream, uint64_t *value) {
    if (!stream || !value || !stream->digits) {
        return -1;
    }
    
    *value = 0;
    for (size_t i = 0; i < stream->length; ++i) {
        if (stream->digits[i] > 9) {
            return -1;
        }
        /* Проверка переполнения */
        if (*value > (UINT64_MAX - stream->digits[i]) / 10U) {
            return -1;
        }
        *value = *value * 10U + stream->digits[i];
    }
    
    return 0;
}

int k_encode_double(k_digit_stream *stream, double value) {
    if (!stream) {
        return -1;
    }
    
    /* Обрабатываем отрицательные числа: 9 = знак минус */
    if (value < 0) {
        if (k_digit_stream_push(stream, 9) != 0) {
            return -1;
        }
        value = -value;
    }
    
    /* Целая часть */
    uint64_t integer_part = (uint64_t)value;
    if (k_encode_uint64(stream, integer_part) != 0) {
        return -1;
    }
    
    /* Разделитель (8 = точка) */
    if (k_digit_stream_push(stream, 8) != 0) {
        return -1;
    }
    
    /* Дробная часть (6 знаков) */
    double frac = value - (double)integer_part;
    for (int i = 0; i < 6; ++i) {
        frac *= 10.0;
        uint8_t digit = (uint8_t)frac;
        if (digit > 9) digit = 9;
        if (k_digit_stream_push(stream, digit) != 0) {
            return -1;
        }
        frac -= (double)digit;
    }
    
    return 0;
}

/* ========================================================================== */
/* --- Сравнение через цифры --- */
/* ========================================================================== */

int k_strcmp(const char *a, const char *b) {
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    
    char buf_a[1536];  /* 512 * 3 */
    char buf_b[1536];
    
    if (k_encode_text(a, buf_a, sizeof(buf_a)) != 0) {
        return -2;  /* Ошибка кодирования */
    }
    if (k_encode_text(b, buf_b, sizeof(buf_b)) != 0) {
        return -2;
    }
    
    /* Сравниваем посимвольно */
    size_t i = 0;
    while (buf_a[i] && buf_b[i]) {
        if (buf_a[i] < buf_b[i]) return -1;
        if (buf_a[i] > buf_b[i]) return 1;
        ++i;
    }
    
    if (buf_a[i]) return 1;
    if (buf_b[i]) return -1;
    return 0;
}

int k_strcmp_digits(const char *str, const char *digits) {
    if (!str || !digits) {
        return -2;
    }
    
    char encoded[1536];
    if (k_encode_text(str, encoded, sizeof(encoded)) != 0) {
        return -2;
    }
    
    size_t i = 0;
    while (encoded[i] && digits[i]) {
        if (encoded[i] < digits[i]) return -1;
        if (encoded[i] > digits[i]) return 1;
        ++i;
    }
    
    if (encoded[i]) return 1;
    if (digits[i]) return -1;
    return 0;
}

/* ========================================================================== */
/* --- Валидация --- */
/* ========================================================================== */

int k_is_pure_decimal(const char *str, size_t len) {
    if (!str) {
        return 0;
    }
    
    for (size_t i = 0; i < len; ++i) {
        if (str[i] < '0' || str[i] > '9') {
            return 0;
        }
    }
    
    return 1;
}

int k_validate_genome(const char *genome) {
    if (!genome) {
        return 0;
    }
    
    size_t len = strlen(genome);
    if (len != 64) {
        return 0;
    }
    
    return k_is_pure_decimal(genome, 64);
}

int k_normalize_input(const char *input, char *out, size_t out_len, size_t *written) {
    if (!input || !out || out_len == 0) {
        return -1;
    }
    
    size_t j = 0;
    for (size_t i = 0; input[i] && j < out_len - 1; ++i) {
        char c = input[i];
        
        /* Пропускаем пробельные символы */
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            continue;
        }
        
        /* Только цифры */
        if (c >= '0' && c <= '9') {
            out[j++] = c;
        }
    }
    
    out[j] = '\0';
    if (written) {
        *written = j;
    }
    
    return 0;
}

/* ========================================================================== */
/* --- Хеширование в цифры --- */
/* ========================================================================== */

int k_digit_hash(const char *input, size_t len, char *out64) {
    if (!input || !out64) {
        return -1;
    }
    
    /* Простой хеш на основе FNV-1a, конвертированный в цифры */
    uint64_t hash1 = 14695981039346656037ULL;
    uint64_t hash2 = 14695981039346656037ULL;
    
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)input[i];
        hash1 ^= c;
        hash1 *= 1099511628211ULL;
        hash2 ^= (c + i) & 0xFF;
        hash2 *= 1099511628211ULL;
    }
    
    /* Генерируем 64 цифры из двух хешей */
    for (int i = 0; i < 32; ++i) {
        out64[i] = '0' + (char)(hash1 % 10);
        hash1 /= 10;
        if (hash1 == 0) hash1 = hash2 + (uint64_t)i;
    }
    for (int i = 32; i < 64; ++i) {
        out64[i] = '0' + (char)(hash2 % 10);
        hash2 /= 10;
        if (hash2 == 0) hash2 = hash1 + (uint64_t)i;
    }
    
    out64[64] = '\0';
    return 0;
}

/* ========================================================================== */
/* --- Phase 1.2: Triplet-Detection & Packing --- */
/* ========================================================================== */

int k_triplet_pack(k_digit_stream *stream) {
    if (!stream || stream->length < 3) return 0;

    /* Ищем последовательности из 3 одинаковых цифр (триплеты) */
    /* Пример: 555 -> упаковываем как спец-код + цифра */
    /* В данной реализации мы просто помечаем их для 'hardware-compression' */
    
    size_t packed_count = 0;
    for (size_t i = 0; i <= stream->length - 3; ) {
        if (stream->digits[i] == stream->digits[i+1] && 
            stream->digits[i] == stream->digits[i+2]) {
            /* Найден триплет! */
            /* В реальной системе здесь будет вызов SIMD инструкции для сжатия блока */
            packed_count++;
            i += 3;
        } else {
            i++;
        }
    }
    
    return (int)packed_count;
}

