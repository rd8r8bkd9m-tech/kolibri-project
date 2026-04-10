/*
 * Copyright (c) 2026 Кочуров Владислав Евгеньевич
 *
 * Phoneme Implementation
 * Реализация фонетического кодирования
 */

#include "kolibri/phoneme.h"
#include <string.h>

/**
 * Вспомогательная функция для извлечения символа UTF-8 и его кода Unicode.
 */
static int get_utf8_code(const char **text) {
    if (!text || !*text || !**text)
        return 0;

    const unsigned char *p = (const unsigned char *)*text;
    int code = 0;
    int bytes = 0;

    if (*p < 0x80) {
        code = *p;
        bytes = 1;
    } else if ((*p & 0xE0) == 0xC0) {
        code = ((*p & 0x1F) << 6) | (p[1] & 0x3F);
        bytes = 2;
    } else if ((*p & 0xF0) == 0xE0) {
        code = ((*p & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        bytes = 3;
    } else if ((*p & 0xF8) == 0xF0) {
        code = ((*p & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        bytes = 4;
    } else {
        bytes = 1; /* Ошибка кодирования, просто пропускаем байт */
    }

    *text += bytes;
    return code;
}

int k_phoneme_encode(const char *text, KolibriPhoneticSignature *sig) {
    if (!text || !sig)
        return -1;
    sig->count = 0;

    const char *ptr = text;
    while (*ptr && sig->count < KOLIBRI_PHONETIC_MAX) {
        int code = get_utf8_code(&ptr);
        if (code == 0)
            break;

        /* Нормализация регистра для кириллицы (Unicode) */
        if (code >= 0x0410 && code <= 0x042F)
            code += 0x20; /* А-Я -> а-я */
        if (code == 0x0401)
            code = 0x0451; /* Ё -> ё */

        KolibriPhoneme p = K_PHONEME_NONE;

        switch (code) {
        case 0x0430:
            p = K_PHONEME_A;
            break;
        case 0x0431:
            p = K_PHONEME_B;
            break;
        case 0x0432:
            p = K_PHONEME_V;
            break;
        case 0x0433:
            p = K_PHONEME_G;
            break;
        case 0x0434:
            p = K_PHONEME_D;
            break;
        case 0x0435:
            p = K_PHONEME_YE;
            break;
        case 0x0436:
            p = K_PHONEME_ZH;
            break;
        case 0x0437:
            p = K_PHONEME_Z;
            break;
        case 0x0438:
            p = K_PHONEME_I;
            break;
        case 0x0439:
            p = K_PHONEME_J;
            break;
        case 0x043a:
            p = K_PHONEME_K;
            break;
        case 0x043b:
            p = K_PHONEME_L;
            break;
        case 0x043c:
            p = K_PHONEME_M;
            break;
        case 0x043d:
            p = K_PHONEME_N;
            break;
        case 0x043e:
            p = K_PHONEME_O;
            break;
        case 0x043f:
            p = K_PHONEME_P;
            break;
        case 0x0440:
            p = K_PHONEME_R;
            break;
        case 0x0441:
            p = K_PHONEME_S;
            break;
        case 0x0442:
            p = K_PHONEME_T;
            break;
        case 0x0443:
            p = K_PHONEME_U;
            break;
        case 0x0444:
            p = K_PHONEME_F;
            break;
        case 0x0445:
            p = K_PHONEME_X;
            break;
        case 0x0446:
            p = K_PHONEME_TS;
            break;
        case 0x0447:
            p = K_PHONEME_CH;
            break;
        case 0x0448:
            p = K_PHONEME_SH;
            break;
        case 0x0449:
            p = K_PHONEME_SHCH;
            break;
        case 0x044a:
            p = K_PHONEME_HARD;
            break;
        case 0x044b:
            p = K_PHONEME_Y;
            break;
        case 0x044c:
            p = K_PHONEME_SOFT;
            break;
        case 0x044d:
            p = K_PHONEME_E;
            break;
        case 0x044e:
            p = K_PHONEME_YU;
            break;
        case 0x044f:
            p = K_PHONEME_YA;
            break;
        case 0x0451:
            p = K_PHONEME_YO;
            break;

        /* Latin alphabet mapping (ASCII) */
        case 'a':
        case 'A':
            p = K_PHONEME_A;
            break;
        case 'b':
        case 'B':
            p = K_PHONEME_B;
            break;
        case 'v':
        case 'V':
            p = K_PHONEME_V;
            break;
        case 'g':
        case 'G':
            p = K_PHONEME_G;
            break;
        case 'd':
        case 'D':
            p = K_PHONEME_D;
            break;
        case 'e':
        case 'E':
            p = K_PHONEME_E;
            break;
        case 'f':
        case 'F':
            p = K_PHONEME_F;
            break;
        case 'h':
        case 'H':
            p = K_PHONEME_X;
            break;
        case 'i':
        case 'I':
            p = K_PHONEME_I;
            break;
        case 'j':
        case 'J':
            p = K_PHONEME_J;
            break;
        case 'k':
        case 'K':
            p = K_PHONEME_K;
            break;
        case 'l':
        case 'L':
            p = K_PHONEME_L;
            break;
        case 'm':
        case 'M':
            p = K_PHONEME_M;
            break;
        case 'n':
        case 'N':
            p = K_PHONEME_N;
            break;
        case 'o':
        case 'O':
            p = K_PHONEME_O;
            break;
        case 'p':
        case 'P':
            p = K_PHONEME_P;
            break;
        case 'q':
        case 'Q':
            p = K_PHONEME_K;
            break; /* Q -> K sound */
        case 'r':
        case 'R':
            p = K_PHONEME_R;
            break;
        case 's':
        case 'S':
            p = K_PHONEME_S;
            break;
        case 't':
        case 'T':
            p = K_PHONEME_T;
            break;
        case 'u':
        case 'U':
            p = K_PHONEME_U;
            break;
        case 'w':
        case 'W':
            p = K_PHONEME_V;
            break; /* W -> V approximation */
        case 'x':
        case 'X':
            p = K_PHONEME_X;
            break;
        case 'y':
        case 'Y':
            p = K_PHONEME_Y;
            break;
        case 'z':
        case 'Z':
            p = K_PHONEME_Z;
            break;
        case 'c':
        case 'C':
            p = K_PHONEME_TS;
            break; /* C -> TS/S approximation */

        /* Skip spaces, punctuation, digits */
        default:
            continue;
        }

        if (p != K_PHONEME_NONE) {
            sig->phonemes[sig->count++] = p;
        }
    }

    return 0;
}

int k_phoneme_to_digits(const KolibriPhoneticSignature *sig, uint8_t *buffer, size_t buffer_size) {
    if (!sig || !buffer)
        return -1;
    if (buffer_size < sig->count * 2)
        return -1;

    for (size_t i = 0; i < sig->count; i++) {
        uint8_t p = (uint8_t)sig->phonemes[i];
        buffer[i * 2] = p / 10;
        buffer[i * 2 + 1] = p % 10;
    }

    return (int)(sig->count * 2);
}
