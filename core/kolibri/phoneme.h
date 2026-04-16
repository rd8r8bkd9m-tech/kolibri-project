/*
 * Copyright (c) 2026 Кочуров Владислав Евгеньевич
 * 
 * Phoneme Module - Phase 2 AGI
 * Фонетическое представление слов для "мышления в числах"
 */

#ifndef KOLIBRI_PHONEME_H
#define KOLIBRI_PHONEME_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Коды фонем (Phoneme Codes)
 * В духе Kolibri OS, каждая фонема - это двузначное число.
 * Это позволяет нейросети "слышать" структуру слова через числовые паттерны.
 */
typedef enum {
    K_PHONEME_NONE = 0,
    
    /* Гласные (Vowels) - Группа 10-19 */
    K_PHONEME_A  = 11, /* А */
    K_PHONEME_O  = 12, /* О */
    K_PHONEME_U  = 13, /* У */
    K_PHONEME_I  = 14, /* И */
    K_PHONEME_Y  = 15, /* Ы */
    K_PHONEME_E  = 16, /* Э */
    K_PHONEME_YA = 17, /* Я (Й+А) */
    K_PHONEME_YO = 18, /* Ё (Й+О) */
    K_PHONEME_YU = 19, /* Ю (Й+У) */
    K_PHONEME_YE = 20, /* Е (Й+Э) */
    
    /* Согласные (Consonants) - Группа 21-49 */
    K_PHONEME_B  = 21,
    K_PHONEME_V  = 22,
    K_PHONEME_G  = 23,
    K_PHONEME_D  = 24,
    K_PHONEME_ZH = 25,
    K_PHONEME_Z  = 26,
    K_PHONEME_J  = 27, /* Й */
    K_PHONEME_K  = 28,
    K_PHONEME_L  = 29,
    K_PHONEME_M  = 30,
    K_PHONEME_N  = 31,
    K_PHONEME_P  = 32,
    K_PHONEME_R  = 33,
    K_PHONEME_S  = 34,
    K_PHONEME_T  = 35,
    K_PHONEME_F  = 36,
    K_PHONEME_X  = 37,
    K_PHONEME_TS = 38,
    K_PHONEME_CH = 39,
    K_PHONEME_SH = 40,
    K_PHONEME_SHCH= 41,

    /* Модификаторы - Группа 50-59 */
    K_PHONEME_SOFT = 50, /* Ь */
    K_PHONEME_HARD = 51, /* Ъ */
    
    K_PHONEME_MAX = 99
} KolibriPhoneme;

/* Максимальное количество фонем в одном слове */
#define KOLIBRI_PHONETIC_MAX 64

/**
 * Фонетическая сигнатура слова
 */
typedef struct {
    KolibriPhoneme phonemes[KOLIBRI_PHONETIC_MAX];
    size_t count;
} KolibriPhoneticSignature;

/**
 * Преобразование UTF-8 текста в фонетическую последовательность.
 * Выполняет базовую транскрипцию (без учета сложных ударений, но с учетом йотированных).
 * 
 * @param text Исходный текст (слово)
 * @param sig Выходная структура сигнатуры
 * @return 0 при успехе, -1 при ошибке
 */
int k_phoneme_encode(const char *text, KolibriPhoneticSignature *sig);

/**
 * Преобразование сигнатуры в поток цифр для генома.
 * @param sig Сигнатура
 * @param buffer Буфер для цифр (каждая фонема -> 2 цифры)
 * @param buffer_size Размер буфера
 * @return Количество записанных цифр или -1
 */
int k_phoneme_to_digits(const KolibriPhoneticSignature *sig, 
                        uint8_t *buffer, 
                        size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_PHONEME_H */
