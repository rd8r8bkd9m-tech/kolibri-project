#include <stdio.h>
#include "kolibri/formula.h"
#include "kolibri/node.c"

int main() {
    printf("KolibriNode: %lu\n", sizeof(KolibriNode));
    printf("KolibriFormula: %lu\n", sizeof(KolibriFormula));
    printf("KolibriFormulaPool: %lu\n", sizeof(KolibriFormulaPool));
    printf("KolibriAssociation: %lu\n", sizeof(KolibriAssociation));
    return 0;
}
