#include <stdio.h>
#include <string.h>
#include "kolibri/formula.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s <text>\n", argv[0]);
        return 1;
    }
    int hash = kf_hash_from_text(argv[1]);
    printf("%d\n", hash);
    return 0;
}
