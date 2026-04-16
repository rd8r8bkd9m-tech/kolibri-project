#include "kolibri/decimal.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static void test_transducer_roundtrip(void) {
  const unsigned char payload[] = {0u, 1u, 2u, 10u, 99u, 128u, 255u};
  uint8_t buffer[64];
  k_digit_stream stream;
  k_digit_stream_init(&stream, buffer, sizeof(buffer));
  int rc = k_transduce_utf8(&stream, payload, sizeof(payload));
  assert(rc == 0);
  unsigned char restored[16];
  size_t written = 0;
  rc = k_emit_utf8(&stream, restored, sizeof(restored), &written);
  assert(rc == 0);
  assert(written == sizeof(payload));
  assert(memcmp(payload, restored, sizeof(payload)) == 0);
}

static void test_digit_stream_bounds(void) {
  uint8_t buffer[3];
  k_digit_stream stream;
  k_digit_stream_init(&stream, buffer, sizeof(buffer));
  assert(k_digit_stream_push(&stream, 1) == 0);
  assert(k_digit_stream_push(&stream, 9) == 0);
  assert(k_digit_stream_push(&stream, 5) == 0);
  assert(k_digit_stream_push(&stream, 2) != 0);
  k_digit_stream_rewind(&stream);
  uint8_t digit = 0;
  assert(k_digit_stream_read(&stream, &digit) == 0 && digit == 1);
  assert(k_digit_stream_read(&stream, &digit) == 0 && digit == 9);
  assert(k_digit_stream_read(&stream, &digit) == 0 && digit == 5);
  assert(k_digit_stream_read(&stream, &digit) == 1);
}

static void test_text_roundtrip(void) {
  const char *text = "Kolibri";
  char encoded[64];
  char decoded[32];
  assert(k_encode_text(text, encoded, sizeof(encoded)) == 0);
  assert(k_decode_text(encoded, decoded, sizeof(decoded)) == 0);
  assert(strcmp(text, decoded) == 0);
}

/* --- Тесты для новых функций --- */

static void test_encode_uint64(void) {
  uint8_t buffer[32];
  k_digit_stream stream;
  
  /* Тест нуля */
  k_digit_stream_init(&stream, buffer, sizeof(buffer));
  assert(k_encode_uint64(&stream, 0) == 0);
  assert(stream.length == 1);
  assert(stream.digits[0] == 0);
  
  /* Тест числа 12345 */
  k_digit_stream_init(&stream, buffer, sizeof(buffer));
  assert(k_encode_uint64(&stream, 12345) == 0);
  assert(stream.length == 5);
  assert(stream.digits[0] == 1);
  assert(stream.digits[1] == 2);
  assert(stream.digits[2] == 3);
  assert(stream.digits[3] == 4);
  assert(stream.digits[4] == 5);
  
  /* Тест декодирования */
  uint64_t decoded = 0;
  assert(k_decode_uint64(&stream, &decoded) == 0);
  assert(decoded == 12345);
}

static void test_encode_double(void) {
  uint8_t buffer[32];
  k_digit_stream stream;
  
  k_digit_stream_init(&stream, buffer, sizeof(buffer));
  assert(k_encode_double(&stream, 3.14159) == 0);
  /* Должно быть: 3.141590 = 3 8 1 4 1 5 9 0 */
  assert(stream.length >= 8);
  assert(stream.digits[0] == 3);
  assert(stream.digits[1] == 8); /* разделитель */
}

static void test_k_strcmp(void) {
  /* Равные строки */
  assert(k_strcmp("hello", "hello") == 0);
  
  /* Разные строки */
  assert(k_strcmp("abc", "abd") < 0);
  assert(k_strcmp("abd", "abc") > 0);
  
  /* Разная длина */
  assert(k_strcmp("abc", "abcd") < 0);
  assert(k_strcmp("abcd", "abc") > 0);
  
  /* NULL обработка */
  assert(k_strcmp(NULL, NULL) == 0);
  assert(k_strcmp("a", NULL) > 0);
  assert(k_strcmp(NULL, "a") < 0);
}

static void test_is_pure_decimal(void) {
  assert(k_is_pure_decimal("12345", 5) == 1);
  assert(k_is_pure_decimal("0000000000", 10) == 1);
  assert(k_is_pure_decimal("9876543210", 10) == 1);
  assert(k_is_pure_decimal("123a45", 6) == 0);
  assert(k_is_pure_decimal("12 45", 5) == 0);
  assert(k_is_pure_decimal("", 0) == 1);
}

static void test_validate_genome(void) {
  /* Валидный 64-цифровой геном */
  const char *valid = "1234567890123456789012345678901234567890123456789012345678901234";
  assert(k_validate_genome(valid) == 1);
  
  /* Неверная длина */
  const char *short_genome = "123456789012345678901234567890";
  assert(k_validate_genome(short_genome) == 0);
  
  /* Недесятичные символы */
  const char *invalid = "123456789012345678901234567890123456789012345678901234567890123a";
  assert(k_validate_genome(invalid) == 0);
  
  assert(k_validate_genome(NULL) == 0);
}

static void test_normalize_input(void) {
  char out[64];
  size_t written = 0;
  
  /* Удаление пробелов */
  assert(k_normalize_input("1 2 3 4 5", out, sizeof(out), &written) == 0);
  assert(strcmp(out, "12345") == 0);
  assert(written == 5);
  
  /* Удаление переносов */
  assert(k_normalize_input("123\n456\r789", out, sizeof(out), &written) == 0);
  assert(strcmp(out, "123456789") == 0);
  
  /* Фильтрация нецифровых */
  assert(k_normalize_input("a1b2c3", out, sizeof(out), &written) == 0);
  assert(strcmp(out, "123") == 0);
}

static void test_digit_hash(void) {
  char hash1[65];
  char hash2[65];
  
  /* Хеш генерируется */
  assert(k_digit_hash("test", 4, hash1) == 0);
  assert(strlen(hash1) == 64);
  assert(k_is_pure_decimal(hash1, 64) == 1);
  
  /* Разные входы - разные хеши */
  assert(k_digit_hash("test2", 5, hash2) == 0);
  assert(strcmp(hash1, hash2) != 0);
  
  /* Одинаковые входы - одинаковые хеши */
  assert(k_digit_hash("test", 4, hash2) == 0);
  assert(strcmp(hash1, hash2) == 0);
}

void test_decimal(void) {
  test_transducer_roundtrip();
  test_digit_stream_bounds();
  test_text_roundtrip();
  
  /* Новые тесты */
  test_encode_uint64();
  test_encode_double();
  test_k_strcmp();
  test_is_pure_decimal();
  test_validate_genome();
  test_normalize_input();
  test_digit_hash();
}
