#include "kolibri/formula.h"
#include "kolibri/roy.h"

#include <arpa/inet.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const unsigned char TEST_KEY[] = "kolibri-test-key";

static void zapolnit_formulu(KolibriFormula *formula) {
    formula->gene.length = 3U;
    formula->gene.digits[0] = 1U;
    formula->gene.digits[1] = 2U;
    formula->gene.digits[2] = 3U;
    formula->fitness = 0.75;
    formula->feedback = 0.0;
}

static void zapolnit_dlinnuyu_formulu(KolibriFormula *formula) {
    memset(formula, 0, sizeof(*formula));
    formula->gene.length = sizeof(formula->gene.digits);
    for (size_t i = 0; i < formula->gene.length; ++i) {
        formula->gene.digits[i] = (uint8_t)(i % 10U);
    }
    formula->fitness = 0.95;
    formula->feedback = 0.0;
}

static void zapolnit_association(KolibriAssociation *association) {
    memset(association, 0, sizeof(*association));
    strncpy(association->question, "что такое рой", sizeof(association->question) - 1U);
    strncpy(association->answer,
            "Рой Колибри — это обмен знаниями между узлами.",
            sizeof(association->answer) - 1U);
    strncpy(association->source, "test", sizeof(association->source) - 1U);
    association->timestamp = 123456789ULL;
}

void test_roy(void) {
    KolibriRoy *pervyj = (KolibriRoy *)calloc(1U, sizeof(KolibriRoy));
    KolibriRoy *vtoroj = (KolibriRoy *)calloc(1U, sizeof(KolibriRoy));
    assert(pervyj);
    assert(vtoroj);
    assert(kolibri_roy_zapustit(pervyj, 1001U, 51200U, TEST_KEY,
                                sizeof(TEST_KEY) - 1U) == 0);
    assert(kolibri_roy_zapustit(vtoroj, 2002U, 51201U, TEST_KEY,
                                sizeof(TEST_KEY) - 1U) == 0);

    struct sockaddr_in adres;
    memset(&adres, 0, sizeof(adres));
    adres.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &adres.sin_addr);
    adres.sin_port = htons(51201U);
    assert(kolibri_roy_dobavit_soseda(pervyj, &adres, 2002U) == 0);
    adres.sin_port = htons(51200U);
    assert(kolibri_roy_dobavit_soseda(vtoroj, &adres, 1001U) == 0);

    usleep(200000);

    KolibriRoySosed perechen[4];
    size_t chislo = kolibri_roy_spisok_sosedey(
        pervyj, perechen, sizeof(perechen) / sizeof(perechen[0]));
    assert(chislo > 0U);

    KolibriFormula *formula = (KolibriFormula *)calloc(1U, sizeof(KolibriFormula));
    KolibriFormula *dlinnaya = (KolibriFormula *)calloc(1U, sizeof(KolibriFormula));
    KolibriAssociation association;
    assert(formula);
    assert(dlinnaya);
    zapolnit_formulu(formula);
    assert(kolibri_roy_otpravit_vsem(pervyj, formula) == 0);

    zapolnit_dlinnuyu_formulu(dlinnaya);
    assert(kolibri_roy_otpravit_vsem(pervyj, dlinnaya) == 0);
    zapolnit_association(&association);
    assert(kolibri_roy_otpravit_association_vsem(pervyj, &association) == 0);

    usleep(200000);

    int nashli_formulu = 0;
    int nashli_dlinnuyu_formulu = 0;
    int nashli_association = 0;
    KolibriRoySobytie *sobytie =
        (KolibriRoySobytie *)calloc(1U, sizeof(KolibriRoySobytie));
    assert(sobytie);
    while (kolibri_roy_poluchit_sobytie(vtoroj, sobytie) > 0) {
        if (sobytie->tip == KOLIBRI_ROY_SOBYTIE_FORMULA) {
            if (sobytie->formula.gene.length == 3U) {
                assert(sobytie->formula.gene.digits[0] == 1U);
                assert(sobytie->formula.gene.digits[1] == 2U);
                assert(sobytie->formula.gene.digits[2] == 3U);
                nashli_formulu = 1;
            } else if (sobytie->formula.gene.length == sizeof(dlinnaya->gene.digits)) {
                for (size_t i = 0; i < sobytie->formula.gene.length; ++i) {
                    assert(sobytie->formula.gene.digits[i] == (uint8_t)(i % 10U));
                }
                nashli_dlinnuyu_formulu = 1;
            }
        } else if (sobytie->tip == KOLIBRI_ROY_SOBYTIE_ASSOCIATION) {
            assert(strcmp(sobytie->association.question, "что такое рой") == 0);
            assert(strstr(sobytie->association.answer, "обмен знаниями") != NULL);
            nashli_association = 1;
        }
    }
    free(sobytie);
    free(formula);
    free(dlinnaya);
    assert(nashli_formulu);
    assert(nashli_dlinnuyu_formulu);
    assert(nashli_association);

    kolibri_roy_ostanovit(pervyj);
    kolibri_roy_ostanovit(vtoroj);
    free(pervyj);
    free(vtoroj);
}
