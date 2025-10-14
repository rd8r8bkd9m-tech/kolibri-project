#ifndef KOLIBRI_DIGIT_TEXT_H
#define KOLIBRI_DIGIT_TEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kolibri/decimal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *digits;
    size_t length;
    size_t capacity;
} KolibriDigitText;

void kolibri_digit_text_init(KolibriDigitText *text);
void kolibri_digit_text_free(KolibriDigitText *text);
int kolibri_digit_text_reserve(KolibriDigitText *text, size_t digits_capacity);
int kolibri_digit_text_assign_utf8(KolibriDigitText *text, const char *utf8);
int kolibri_digit_text_assign_digits(KolibriDigitText *text, const uint8_t *digits, size_t length);
int kolibri_digit_text_clone(const KolibriDigitText *src, KolibriDigitText *dst);
bool kolibri_digit_text_equals(const KolibriDigitText *lhs, const KolibriDigitText *rhs);
bool kolibri_digit_text_equals_utf8(const KolibriDigitText *lhs, const char *utf8);
int kolibri_digit_text_to_utf8(const KolibriDigitText *text, char **out_utf8);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_DIGIT_TEXT_H */
