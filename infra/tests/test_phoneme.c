#include "kolibri/phoneme.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

void test_phoneme_encoding() {
    printf("Testing phoneme encoding...\n");
    
    KolibriPhoneticSignature sig;
    
    /* Тест 1: Слово "Мама" */
    k_phoneme_encode("Мама", &sig);
    assert(sig.count == 4);
    assert(sig.phonemes[0] == K_PHONEME_M);
    assert(sig.phonemes[1] == K_PHONEME_A);
    assert(sig.phonemes[2] == K_PHONEME_M);
    assert(sig.phonemes[3] == K_PHONEME_A);
    printf("Test 1 (Мама) passed.\n");
    
    /* Тест 2: Слово "Кот" */
    k_phoneme_encode("Кот", &sig);
    assert(sig.count == 3);
    assert(sig.phonemes[0] == K_PHONEME_K);
    assert(sig.phonemes[1] == K_PHONEME_O);
    assert(sig.phonemes[2] == K_PHONEME_T);
    printf("Test 2 (Кот) passed.\n");
    
    /* Тест 3: Смешанный регистр и Ё */
    k_phoneme_encode("Ёлка", &sig);
    assert(sig.count == 4);
    assert(sig.phonemes[0] == K_PHONEME_YO);
    assert(sig.phonemes[1] == K_PHONEME_L);
    assert(sig.phonemes[2] == K_PHONEME_K);
    assert(sig.phonemes[3] == K_PHONEME_A);
    printf("Test 3 (Ёлка) passed.\n");
    
    /* Тест 4: Преобразование в цифры */
    uint8_t buffer[10];
    int digits = k_phoneme_to_digits(&sig, buffer, 10);
    assert(digits == 8);
    /* YO = 18 -> 1, 8 */
    assert(buffer[0] == 1);
    assert(buffer[1] == 8);
    printf("Test 4 (Digits) passed.\n");
    
    printf("All phoneme tests passed!\n");
}

int main() {
    test_phoneme_encoding();
    return 0;
}
