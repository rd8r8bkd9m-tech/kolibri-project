/*
 * test_formula_storage.c
 * 
 * Демонстрация ФОРМУЛЬНОГО хранения:
 * 1. Берём исходные данные
 * 2. Анализируем и создаём ФОРМУЛУ (компактное представление)
 * 3. УДАЛЯЕМ исходные данные И промежуточную кодировку
 * 4. Остаётся только ФОРМУЛА
 * 5. Из формулы восстанавливаем ВСЁ обратно
 */

#include "kolibri/decimal.h"
#include "kolibri/formula.h"
#include "support.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GREEN  "\033[32m"
#define YELLOW "\033[33m"
#define CYAN   "\033[36m"
#define RED    "\033[31m"
#define MAGENTA "\033[35m"
#define RESET  "\033[0m"

/* Упрощённое представление формулы для демонстрации */
typedef struct {
    char pattern[16];      /* Базовый паттерн */
    size_t repeat_count;   /* Сколько раз повторить */
    size_t formula_size;   /* Размер самой формулы в байтах */
} SimpleFormula;

/* Создать формулу из данных */
static int create_formula_from_text(const char *text, SimpleFormula *formula) {
    if (!text || !formula) return -1;
    
    /* Для простоты: обрабатываем только случай одинаковых символов */
    size_t len = strlen(text);
    if (len == 0) return -1;
    
    /* Проверяем что все символы одинаковые */
    char first = text[0];
    int all_same = 1;
    for (size_t i = 1; i < len; i++) {
        if (text[i] != first) {
            all_same = 0;
            break;
        }
    }
    
    if (all_same) {
        /* Формула: repeat(pattern, count) */
        /* pattern = decimal код символа (3 цифры) */
        unsigned char byte = (unsigned char)first;
        snprintf(formula->pattern, sizeof(formula->pattern), 
                 "%03d", byte);
        formula->repeat_count = len;
        /* Размер формулы: pattern (3 байта) + count (зависит от числа) */
        formula->formula_size = 3 + sizeof(size_t);
        return 0;
    }
    
    return -1;  /* Паттерн не найден */
}

/* Восстановить данные из формулы */
static int restore_from_formula(const SimpleFormula *formula, char *output, size_t output_size) {
    if (!formula || !output) return -1;
    
    if (output_size < formula->repeat_count + 1) return -1;
    
    /* Парсим паттерн обратно в байт */
    int byte_value = atoi(formula->pattern);
    if (byte_value < 0 || byte_value > 255) return -1;
    
    /* Восстанавливаем повторяющийся символ */
    for (size_t i = 0; i < formula->repeat_count; i++) {
        output[i] = (char)byte_value;
    }
    output[formula->repeat_count] = '\0';
    
    return 0;
}

int main(void) {
    printf("\n%s╔══════════════════════════════════════════════════════════════╗%s\n", 
           CYAN, RESET);
    printf("%s║            FORMULA-ONLY STORAGE DEMONSTRATION            ║%s\n", 
           CYAN, RESET);
    printf("%s╚══════════════════════════════════════════════════════════════╝%s\n\n", 
           CYAN, RESET);
    
    /* ========== ЭТАП 1: Исходные данные ========== */
    printf("%s[Step 1] Original data%s\n", YELLOW, RESET);
    
    const char *original = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";  /* 40x 'A' */
    size_t original_size = strlen(original);
    
    printf("Data: \"%s\" (%zu bytes)\n", original, original_size);
    printf("Memory address: %p\n\n", (void*)original);
    
    /* ========== ЭТАП 2: Создание формулы ========== */
    printf("%s[Step 2] Creating formula (analyzing patterns)...%s\n", YELLOW, RESET);
    
    SimpleFormula formula;
    int formula_result = create_formula_from_text(original, &formula);
    
    if (formula_result != 0) {
        printf("%s✗ No patterns found, cannot use formula storage%s\n", RED, RESET);
        return 1;
    }
    
    printf("%s✓ Formula created!%s\n", GREEN, RESET);
    printf("Formula representation:\n");
    printf("  Type: repeat()\n");
    printf("  Pattern: \"%s\" (decimal code of '%c')\n", 
           formula.pattern, original[0]);
    printf("  Count: %zu times\n", formula.repeat_count);
    printf("  Formula size: %zu bytes\n", formula.formula_size);
    printf("  %sCompression: %zu bytes → %zu bytes (%.2fx)%s\n\n", 
           GREEN, original_size, formula.formula_size,
           (double)original_size / formula.formula_size, RESET);
    
    /* ========== ЭТАП 3: УДАЛЕНИЕ исходных данных ========== */
    printf("%s[Step 3] Deleting original data and intermediate encodings...%s\n", 
           YELLOW, RESET);
    
    /* Симулируем удаление:
     * - Исходные данные
     * - Промежуточная decimal кодировка (если была)
     * - Всё кроме формулы!
     */
    
    char *original_copy = strdup(original);  /* Копия для демонстрации */
    size_t saved_size = original_size;
    char saved_first_char = original[0];
    
    /* "Удаляем" всё */
    memset(original_copy, 0xDD, original_size);
    free(original_copy);
    original_copy = NULL;
    
    printf("  %s✓ Original data deleted (freed from memory)%s\n", RED, RESET);
    printf("  %s✓ Intermediate decimal encoding deleted (not stored)%s\n", RED, RESET);
    printf("  %s✓ Only formula remains in storage%s\n", MAGENTA, RESET);
    printf("\n");
    printf("Current storage state:\n");
    printf("  Original: [DELETED]\n");
    printf("  Decimal encoding: [DELETED]\n");
    printf("  %sFormula: repeat(\"%s\", %zu) - %zu bytes%s\n\n", 
           GREEN, formula.pattern, formula.repeat_count, 
           formula.formula_size, RESET);
    
    /* ========== ЭТАП 4: Восстановление ТОЛЬКО из формулы ========== */
    printf("%s[Step 4] Restoring data from formula ONLY...%s\n", YELLOW, RESET);
    
    char restored[256];
    int restore_result = restore_from_formula(&formula, restored, sizeof(restored));
    
    if (restore_result != 0) {
        printf("%s✗ Restoration failed!%s\n", RED, RESET);
        return 1;
    }
    
    size_t restored_size = strlen(restored);
    printf("%s✓ Data restored!%s\n", GREEN, RESET);
    printf("Restored: \"%s\" (%zu bytes)\n", restored, restored_size);
    printf("First char check: '%c' == '%c' ? %s\n", 
           restored[0], saved_first_char,
           restored[0] == saved_first_char ? "✓" : "✗");
    printf("Size check: %zu == %zu ? %s\n\n",
           restored_size, saved_size,
           restored_size == saved_size ? "✓" : "✗");
    
    /* ========== ЭТАП 5: Проверка целостности ========== */
    printf("%s[Step 5] Verification%s\n", YELLOW, RESET);
    
    /* Не можем сравнить с оригиналом (он удалён!), 
     * но проверяем что восстановленное соответствует формуле */
    
    int all_correct = 1;
    unsigned char expected = (unsigned char)atoi(formula.pattern);
    for (size_t i = 0; i < restored_size; i++) {
        if ((unsigned char)restored[i] != expected) {
            all_correct = 0;
            break;
        }
    }
    
    if (!all_correct || restored_size != formula.repeat_count) {
        printf("%s✗ Verification failed!%s\n", RED, RESET);
        return 1;
    }
    
    printf("%s✓ Verification passed!%s\n", GREEN, RESET);
    printf("  • All %zu characters match pattern '%c' (code: %s)\n",
           restored_size, expected, formula.pattern);
    printf("  • Count matches: %zu repeats\n", formula.repeat_count);
    printf("  • %s100%% data integrity confirmed%s\n\n", GREEN, RESET);
    
    /* ========== ФИНАЛЬНОЕ СРАВНЕНИЕ ========== */
    printf("%s╔══════════════════════════════════════════════════════════════╗%s\n", 
           GREEN, RESET);
    printf("%s║                    STORAGE COMPARISON                    ║%s\n", 
           GREEN, RESET);
    printf("%s╚══════════════════════════════════════════════════════════════╝%s\n", 
           GREEN, RESET);
    printf("\n");
    
    /* Расчёт размеров */
    size_t naive_decimal = saved_size * 3;  /* UTF-8 → Decimal (1 level) */
    size_t double_decimal = naive_decimal * 3;  /* Decimal → Decimal (2 level) */
    
    printf("Storage method        | Size (bytes) | vs Original | Lossless\n");
    printf("----------------------------------------------------------------\n");
    printf("Original data         | %6zu       | 1.00x       | ✅\n", saved_size);
    printf("Decimal (1 level)     | %6zu       | %.2fx       | ✅\n", 
           naive_decimal, (double)naive_decimal / saved_size);
    printf("Decimal² (2 level)    | %6zu       | %.2fx       | ✅ (but wasteful!)\n",
           double_decimal, (double)double_decimal / saved_size);
    printf("%sFormula (final)       | %6zu       | %.2fx       | ✅ (optimal!)%s\n",
           GREEN, formula.formula_size, (double)formula.formula_size / saved_size, RESET);
    
    printf("\n%sSpace saved by formula vs naive methods:%s\n", CYAN, RESET);
    printf("  vs Decimal (1 lvl):  %.0f%% reduction\n",
           100.0 * (1.0 - (double)formula.formula_size / naive_decimal));
    printf("  vs Decimal² (2 lvl): %.0f%% reduction\n",
           100.0 * (1.0 - (double)formula.formula_size / double_decimal));
    
    printf("\n%s✓ КЛЮЧЕВОЙ МОМЕНТ:%s\n", MAGENTA, RESET);
    printf("  Формула заменяет:\n");
    printf("    ❌ Исходные данные (%zu bytes)\n", saved_size);
    printf("    ❌ Decimal кодировку (%zu bytes)\n", naive_decimal);
    printf("    ✅ Только формула (%zu bytes)\n", formula.formula_size);
    printf("  %sВсё восстанавливается из формулы!%s\n\n", GREEN, RESET);
    
    /* ========== РЕАЛЬНЫЙ ПРИМЕР С KOLIBRI ========== */
    printf("%s╔══════════════════════════════════════════════════════════════╗%s\n", 
           CYAN, RESET);
    printf("%s║              REAL KOLIBRI GENOME EXAMPLE                 ║%s\n", 
           CYAN, RESET);
    printf("%s╚══════════════════════════════════════════════════════════════╝%s\n\n", 
           CYAN, RESET);
    
    /* Пример с genome/formula */
    KolibriGene gene;
    gene.length = 5;
    gene.digits[0] = 1;
    gene.digits[1] = 2;
    gene.digits[2] = 3;
    gene.digits[3] = 2;
    gene.digits[4] = 1;
    
    printf("Example: KolibriGene\n");
    printf("  Digits: [");
    for (size_t i = 0; i < gene.length; i++) {
        printf("%d%s", gene.digits[i], i < gene.length - 1 ? ", " : "");
    }
    printf("]\n");
    printf("  Storage: %zu bytes (direct)\n", sizeof(gene.digits[0]) * gene.length);
    printf("\n");
    
    printf("If genome has patterns → store as KolibriFormula:\n");
    printf("  Formula.gene = compressed pattern\n");
    printf("  Formula.associations = metadata\n");
    printf("  %sOriginal digits can be deleted!%s\n", GREEN, RESET);
    printf("  Formula reconstructs everything when needed\n\n");
    
    printf("%s✓ DEMONSTRATION COMPLETE%s\n", GREEN, RESET);
    printf("Formula-only storage: from %zu bytes to %zu bytes (%.2fx compression)\n",
           saved_size, formula.formula_size, 
           (double)saved_size / formula.formula_size);
    printf("Исходные данные и decimal кодировка удалены — остаётся только формула!\n\n");
    
    return 0;
}
