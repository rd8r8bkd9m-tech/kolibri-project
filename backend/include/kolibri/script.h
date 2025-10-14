/*
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#ifndef KOLIBRI_SCRIPT_H
#define KOLIBRI_SCRIPT_H

#include "kolibri/digit_text.h"
#include "kolibri/formula.h"
#include "kolibri/genome.h"
#include "kolibri/symbol_table.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    KolibriDigitText stimulus;
    KolibriDigitText response;
    KolibriDigitText question;
    KolibriDigitText answer;
    KolibriDigitText expected;
    unsigned char teach_hash[32];
    unsigned char evaluate_hash[32];
    int has_teach;
    int has_evaluate;
} KolibriCrystalCore;

/*
 * Контекст исполнения KolibriScript. Хранит цифровой поток сценария, кристалл
 * генезиса и предоставляет доступ к пулу формул и цифровому геному.
 */
struct KolibriScriptVariable;
struct KolibriScriptAssociation;
struct KolibriScriptFormulaBinding;

typedef struct {
    KolibriFormulaPool *pool;
    KolibriGenome *genome;
    FILE *vyvod;
    KolibriDigitText source_stream;
    KolibriCrystalCore crystal_core;
    KolibriSymbolTable symbol_table;
    char mode[32];

    /* Runtime state */
    struct KolibriScriptVariable *variables;
    size_t variables_count;
    size_t variables_capacity;

    struct KolibriScriptAssociation *associations;
    size_t associations_count;
    size_t associations_capacity;

    struct KolibriScriptFormulaBinding *formulas;
    size_t formulas_count;
    size_t formulas_capacity;
} KolibriScript;

/* Инициализирует интерпретатор и выделяет внутренний цифровой буфер. */
int ks_init(KolibriScript *skript, KolibriFormulaPool *pool,
            KolibriGenome *genome);

/* Освобождает выделенные ресурсы интерпретатора. */
void ks_free(KolibriScript *skript);

/* Переназначает поток вывода интерпретатора (по умолчанию stdout). */
void ks_set_output(KolibriScript *skript, FILE *vyvod);

/* Загружает русскоязычный сценарий из текстовой строки. */
int ks_load_text(KolibriScript *skript, const char *text);

/* Загружает сценарий из файла на диске. */
int ks_load_file(KolibriScript *skript, const char *path);

/* Выполняет сценарий, возвращает 0 при успехе. */
int ks_execute(KolibriScript *skript);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_SCRIPT_H */
