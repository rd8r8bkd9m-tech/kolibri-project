#ifndef KOLIBRI_DECIMAL_H
#define KOLIBRI_DECIMAL_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* --- Цифровой поток (Digit Stream) --- */

typedef struct {
    uint8_t *digits;
    size_t capacity;
    size_t length;
    size_t cursor;
} k_digit_stream;

void k_digit_stream_init(k_digit_stream *stream, uint8_t *buffer, size_t capacity);
void k_digit_stream_reset(k_digit_stream *stream);
void k_digit_stream_rewind(k_digit_stream *stream);
int k_digit_stream_push(k_digit_stream *stream, uint8_t digit);
int k_digit_stream_read(k_digit_stream *stream, uint8_t *digit);
size_t k_digit_stream_remaining(const k_digit_stream *stream);

/* --- UTF-8 Преобразования --- */

int k_transduce_utf8(k_digit_stream *stream, const unsigned char *bytes, size_t len);
int k_emit_utf8(const k_digit_stream *stream, unsigned char *out, size_t out_len, size_t *written);

/* --- Текстовые преобразования (строка ↔ цифры) --- */

size_t k_encode_text_length(size_t input_len);
size_t k_decode_text_length(size_t digits_len);
int k_encode_text(const char *input, char *out, size_t out_len);
int k_decode_text(const char *digits, char *out, size_t out_len);

/* --- Числовые преобразования --- */

/** Кодирует 64-битное число в цифровой поток */
int k_encode_uint64(k_digit_stream *stream, uint64_t value);

/** Декодирует цифровой поток в 64-битное число */
int k_decode_uint64(const k_digit_stream *stream, uint64_t *value);

/** Кодирует число с плавающей точкой (6 знаков точности) */
int k_encode_double(k_digit_stream *stream, double value);

/* --- Сравнение через цифры (замена strcmp) --- */

/** Сравнивает две строки через десятичное представление
 *  Возвращает: <0 если a<b, 0 если a==b, >0 если a>b */
int k_strcmp(const char *a, const char *b);

/** Сравнивает строку с десятичной последовательностью */
int k_strcmp_digits(const char *str, const char *digits);

/* --- Валидация --- */

/** Проверяет, что строка содержит только цифры 0-9 */
int k_is_pure_decimal(const char *str, size_t len);

/** Проверяет корректность 64-цифрового генома */
int k_validate_genome(const char *genome);

/** Нормализует входные данные в чистые цифры (удаляет пробелы, переносы) */
int k_normalize_input(const char *input, char *out, size_t out_len, size_t *written);

/* --- Хеширование в цифры --- */

/** Вычисляет цифровой хеш строки (64 цифры) */
int k_digit_hash(const char *input, size_t len, char *out64);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_DECIMAL_H */
