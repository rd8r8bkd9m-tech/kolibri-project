#include <stdio.h>
#include <string.h>
#include "kolibri/formula.h"

int main() {
    const char *words[] = {"философия", "наука", "бытие", "разум", "сознание", "мир", "знание", "логика", "мудрость", "колибри", "будущее", "эволюция"};
    for (int i = 0; i < 12; i++) {
        printf("%s: %d\n", words[i], kf_hash_from_text(words[i]));
    }
    return 0;
}
