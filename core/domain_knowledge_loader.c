/*
 * domain_knowledge_loader.c
 *
 * Загрузка доменных знаний в Kolibri reasoning engine
 *
 * Домены:
 *   1. Физика: законы Ньютона, кинематика, энергия
 *   2. Химия: реакции, соединения, стехиометрия
 *   3. Программирование: алгоритмы, структуры данных, паттерны
 *   4. Юриспруденция: законы, права, обязанности
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/domain_knowledge_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kolibri/reasoning_engine.h"

/* ============================================================================
 * ФИЗИКА
 * ============================================================================ */

static int load_physics_facts(KolibriREConfig *config) {
    int count = 0;

    /* Законы Ньютона */
    kolibri_re_add_rule(config, "Тело массой m ускоряется силой F", "F = m * a (Второй закон Ньютона)", KRE_OP_IMPLIES,
                        0.99, "physics");
    count++;

    kolibri_re_add_rule(config, "Действие вызывает равное противодействие",
                        "Третьий закон Ньютона: F_действие = -F_противодействие", KRE_OP_IMPLIES, 0.99, "physics");
    count++;

    kolibri_re_add_rule(config, "Тело сохраняет состояние покоя или движения", "Первый закон Ньютона (инерция)",
                        KRE_OP_IMPLIES, 0.99, "physics");
    count++;

    /* Кинематика */
    kolibri_re_add_rule(config, "Скорость v, время t, расстояние s", "s = v * t (равномерное движение)", KRE_OP_IMPLIES,
                        0.98, "physics");
    count++;

    kolibri_re_add_rule(config, "Ускорение a, начальная скорость v0, время t",
                        "v = v0 + a * t (равноускоренное движение)", KRE_OP_IMPLIES, 0.98, "physics");
    count++;

    kolibri_re_add_rule(config, "Начальная скорость v0, ускорение a, время t",
                        "s = v0*t + a*t²/2 (путь при равноускоренном движении)", KRE_OP_IMPLIES, 0.97, "physics");
    count++;

    /* Энергия */
    kolibri_re_add_rule(config, "Масса m, скорость света c", "E = m*c² (эквивалентность массы и энергии)",
                        KRE_OP_IMPLIES, 0.99, "physics");
    count++;

    kolibri_re_add_rule(config, "Масса m, высота h, ускорение свободного падения g",
                        "Потенциальная энергия: Ep = m*g*h", KRE_OP_IMPLIES, 0.98, "physics");
    count++;

    kolibri_re_add_rule(config, "Масса m, скорость v", "Кинетическая энергия: Ek = m*v²/2", KRE_OP_IMPLIES, 0.98,
                        "physics");
    count++;

    /* Электричество */
    kolibri_re_add_rule(config, "Напряжение U, ток I, сопротивление R", "U = I * R (Закон Ома)", KRE_OP_IMPLIES, 0.99,
                        "physics");
    count++;

    kolibri_re_add_rule(config, "Ток I, напряжение U", "Мощность: P = U * I", KRE_OP_IMPLIES, 0.97, "physics");
    count++;

    /* Quantum Computing */
    kolibri_re_add_rule(config, "Квантовый бит (кубит)", "Кубит может находиться в суперпозиции 0 и 1 одновременно", KRE_OP_IMPLIES, 0.99, "quantum");
    count++;

    kolibri_re_add_rule(config, "Квантовая запутанность", "Состояние одной частицы мгновенно влияет на другую независимо от расстояния", KRE_OP_IMPLIES, 0.98, "quantum");
    count++;

    kolibri_re_add_rule(config, "Алгоритм Шора", "Позволяет факторизовать числа за полиномиальное время на квантовом компьютере", KRE_OP_IMPLIES, 0.97, "quantum");
    count++;

    kolibri_re_add_fact(config, "Земля вращается вокруг своей оси", 0.99, "physics");
    count++;

    kolibri_re_add_rule(config, "Земля вращается", "Происходит смена дня и ночи", KRE_OP_IMPLIES, 0.99, "physics");
    count++;

    kolibri_re_add_rule(config, "Вращение планеты", "Возникает центробежная сила и сила Кориолиса", KRE_OP_IMPLIES, 0.98, "physics");
    count++;

    kolibri_re_add_rule(config, "Земля вращается вокруг Солнца", "Происходит смена времён года", KRE_OP_IMPLIES, 0.99, "physics");
    count++;

    kolibri_re_add_fact(config, "Ускорение свободного падения g ≈ 9.8 м/с²", 0.99, "physics");
    count++;

    kolibri_re_add_fact(config, "Скорость света c ≈ 299792458 м/с", 0.99, "physics");
    count++;

    return count;
}

/* ============================================================================
 * БИОЛОГИЯ И НЕЙРОБИОЛОГИЯ
 * ============================================================================ */

static int load_biology_facts(KolibriREConfig *config) {
    int count = 0;

    kolibri_re_add_rule(config, "Нейрон", "Клетка нервной системы, передающая информацию через синапсы", KRE_OP_IMPLIES, 0.99, "neuro");
    count++;

    kolibri_re_add_rule(config, "Нейропластичность", "Способность мозга изменять структуру и связи в ответ на опыт", KRE_OP_IMPLIES, 0.98, "neuro");
    count++;

    kolibri_re_add_rule(config, "Дофамин", "Нейромедиатор, отвечающий за систему вознаграждения и мотивацию", KRE_OP_IMPLIES, 0.97, "neuro");
    count++;

    return count;
}

/* ============================================================================
 * ХИМИЯ
 * ============================================================================ */

static int load_chemistry_facts(KolibriREConfig *config) {
    int count = 0;

    /* Основные реакции */
    kolibri_re_add_rule(config, "Водород H2 горит в кислороде O2", "2H2 + O2 → 2H2O (реакция горения водорода)",
                        KRE_OP_IMPLIES, 0.99, "chemistry");
    count++;

    kolibri_re_add_rule(config, "Метан CH4 горит в кислороде", "CH4 + 2O2 → CO2 + 2H2O (горение метана)",
                        KRE_OP_IMPLIES, 0.98, "chemistry");
    count++;

    kolibri_re_add_rule(config, "Железо Fe реагирует с серой S", "Fe + S → FeS (сульфид железа)", KRE_OP_IMPLIES, 0.97,
                        "chemistry");
    count++;

    kolibri_re_add_rule(config, "Натрий Na реагирует с водой H2O", "2Na + 2H2O → 2NaOH + H2 (бурная реакция)",
                        KRE_OP_IMPLIES, 0.98, "chemistry");
    count++;

    kolibri_re_add_rule(config, "Соляная кислота HCl реагирует с гидроксидом натрия NaOH",
                        "HCl + NaOH → NaCl + H2O (реакция нейтрализации)", KRE_OP_IMPLIES, 0.99, "chemistry");
    count++;

    /* Стехиометрия */
    kolibri_re_add_rule(config, "Молярная масса вещества M, количество молей n",
                        "Масса = n * M (стехиометрический расчёт)", KRE_OP_IMPLIES, 0.98, "chemistry");
    count++;

    kolibri_re_add_rule(config, "Объём газа V при н.у., молярный объём Vm=22.4 л/моль",
                        "n = V / Vm (количество молей газа)", KRE_OP_IMPLIES, 0.97, "chemistry");
    count++;

    /* pH */
    kolibri_re_add_rule(config, "Концентрация ионов водорода [H+]", "pH = -log10([H+]) (кислотность раствора)",
                        KRE_OP_IMPLIES, 0.98, "chemistry");
    count++;

    kolibri_re_add_fact(config, "Число Авогадро NA ≈ 6.022×10²³ моль⁻¹", 0.99, "chemistry");
    count++;

    kolibri_re_add_fact(config, "Вода H2O: 2 атома водорода + 1 атом кислорода", 0.99, "chemistry");
    count++;

    /* Общие химические понятия */
    kolibri_re_add_fact(config, "Химическая реакция — процесс превращения веществ с образованием новых соединений",
                        0.98, "chemistry");
    count++;

    kolibri_re_add_fact(config, "Химические элементы — вещества из атомов с одинаковым зарядом ядра", 0.97,
                        "chemistry");
    count++;

    kolibri_re_add_fact(config, "Химическое соединение — вещество из двух или более различных элементов", 0.97,
                        "chemistry");
    count++;

    return count;
}

/* ============================================================================
 * ПРОГРАММИРОВАНИЕ
 * ============================================================================ */

static int load_programming_facts(KolibriREConfig *config) {
    int count = 0;

    /* Алгоритмы */
    kolibri_re_add_rule(config, "Массив отсортирован, элемент ищется", "Бинарный поиск: O(log n) сравнений",
                        KRE_OP_IMPLIES, 0.98, "programming");
    count++;

    kolibri_re_add_rule(config, "Массив НЕ отсортирован, элемент ищется", "Линейный поиск: O(n) сравнений",
                        KRE_OP_IMPLIES, 0.98, "programming");
    count++;

    kolibri_re_add_rule(config, "Нужна быстрая сортировка массива", "QuickSort: O(n log n) среднее, O(n²) худшее",
                        KRE_OP_IMPLIES, 0.97, "programming");
    count++;

    kolibri_re_add_rule(config, "Нужна стабильная сортировка", "MergeSort: O(n log n), стабильная", KRE_OP_IMPLIES,
                        0.97, "programming");
    count++;

    /* Структуры данных */
    kolibri_re_add_rule(config, "Нужен FIFO доступ (первый вошёл — первый вышел)",
                        "Queue (очередь): enqueue O(1), dequeue O(1)", KRE_OP_IMPLIES, 0.98, "programming");
    count++;

    kolibri_re_add_rule(config, "Нужен LIFO доступ (последний вошёл — первый вышел)",
                        "Stack (стек): push O(1), pop O(1)", KRE_OP_IMPLIES, 0.98, "programming");
    count++;

    kolibri_re_add_rule(config, "Нужен быстрый поиск по ключу", "HashMap: поиск O(1) среднее, вставка O(1)",
                        KRE_OP_IMPLIES, 0.97, "programming");
    count++;

    kolibri_re_add_rule(config, "Нужен упорядоченный словарь", "Binary Search Tree: поиск O(log n), вставка O(log n)",
                        KRE_OP_IMPLIES, 0.97, "programming");
    count++;

    /* Паттерны проектирования */
    kolibri_re_add_rule(config, "Нужен единственный экземпляр объекта",
                        "Singleton pattern: один объект на всё приложение", KRE_OP_IMPLIES, 0.95, "programming");
    count++;

    kolibri_re_add_rule(config, "Нужно развязать интерфейс от реализации",
                        "Bridge pattern: абстракция + реализация отдельно", KRE_OP_IMPLIES, 0.95, "programming");
    count++;

    kolibri_re_add_rule(config, "Нужно наблюдать за изменениями объекта", "Observer pattern: подписка на события",
                        KRE_OP_IMPLIES, 0.95, "programming");
    count++;

    /* Сложность */
    kolibri_re_add_rule(config, "Алгоритм делит задачу пополам на каждом шаге", "Сложность O(log n) — логарифмическая",
                        KRE_OP_IMPLIES, 0.98, "programming");
    count++;

    kolibri_re_add_fact(config, "O(1) — константное время, лучший случай", 0.99, "programming");
    count++;

    kolibri_re_add_fact(config, "O(n²) — квадратичное время, вложенные циклы", 0.99, "programming");
    count++;

    return count;
}

/* ============================================================================
 * ЮРИСПРУДЕНЦИЯ
 * ============================================================================ */

static int load_law_facts(KolibriREConfig *config) {
    int count = 0;

    /* Основные правовые принципы */
    kolibri_re_add_rule(config, "Человек совершил правонарушение", "Применяется принцип ответственности за деяние",
                        KRE_OP_IMPLIES, 0.95, "law");
    count++;

    kolibri_re_add_rule(config, "Закон не имеет обратной силы",
                        "Принцип: закон применяется к отношениям после вступления в силу", KRE_OP_IMPLIES, 0.99, "law");
    count++;

    kolibri_re_add_rule(config, "Невиновность не доказана", "Презумпция невиновности: лицо считается невиновным",
                        KRE_OP_IMPLIES, 0.99, "law");
    count++;

    kolibri_re_add_rule(config, "Нарушены права гражданина", "Право на судебную защиту и восстановление прав",
                        KRE_OP_IMPLIES, 0.98, "law");
    count++;

    kolibri_re_add_rule(config, "Договор подписан обеими сторонами", "Договор вступает в силу (если не указано иное)",
                        KRE_OP_IMPLIES, 0.97, "law");
    count++;

    kolibri_re_add_rule(config, "Срок исковой давности истёк", "Иск не подлежит удовлетворению (общий срок: 3 года)",
                        KRE_OP_IMPLIES, 0.96, "law");
    count++;

    kolibri_re_add_rule(config, "Необходима юридическая помощь",
                        "Право на получение квалифицированной юридической помощи", KRE_OP_IMPLIES, 0.98, "law");
    count++;

    kolibri_re_add_rule(config, "Решение суда вступило в законную силу",
                        "Решение обязательно для исполнения всеми лицами", KRE_OP_IMPLIES, 0.99, "law");
    count++;

    kolibri_re_add_fact(config, "Конституция — основной закон государства", 0.99, "law");
    count++;

    kolibri_re_add_fact(config, "Законы подразделяются на: федеральные и региональные", 0.97, "law");
    count++;

    kolibri_re_add_fact(config, "Право на обжалование судебного решения — фундаментальное право", 0.98, "law");
    count++;

    return count;
}

/* ============================================================================
 * ПУБЛИЧНЫЙ API
 * ============================================================================ */

/* Forward declarations */
int kolibri_domain_load_physics(KolibriREConfig *config);
int kolibri_domain_load_chemistry(KolibriREConfig *config);
int kolibri_domain_load_programming(KolibriREConfig *config);
int kolibri_domain_load_law(KolibriREConfig *config);

int kolibri_domain_load_all(KolibriREConfig *config) {
    if (!config)
        return -1;

    int total = 0;

    total += kolibri_domain_load_physics(config);
    total += kolibri_domain_load_chemistry(config);
    total += kolibri_domain_load_programming(config);
    total += kolibri_domain_load_law(config);

    return total;
}

int kolibri_domain_load_physics(KolibriREConfig *config) {
    if (!config)
        return -1;
    int count = load_physics_facts(config);
    config->physics_count = count;
    config->total_rules_count += count;                           /* physics has only rules */
    config->total_facts_count += (count - config->physics_count); /* will be corrected */
    return count;
}

int kolibri_domain_load_chemistry(KolibriREConfig *config) {
    if (!config)
        return -1;
    int count = load_chemistry_facts(config);
    config->chemistry_count = count;
    config->total_rules_count += count;
    return count;
}

int kolibri_domain_load_programming(KolibriREConfig *config) {
    if (!config)
        return -1;
    int count = load_programming_facts(config);
    config->programming_count = count;
    config->total_rules_count += count;
    return count;
}

int kolibri_domain_load_law(KolibriREConfig *config) {
    if (!config)
        return -1;
    int count = load_law_facts(config);
    config->law_count = count;
    config->total_rules_count += count;
    return count;
}

int kolibri_domain_print_stats(KolibriREConfig *config) {
    if (!config)
        return -1;

    int total = config->physics_count + config->chemistry_count + config->programming_count + config->law_count;

    printf("\n=== Domain Knowledge Stats ===\n");
    printf("  Physics:     %d facts/rules\n", config->physics_count);
    printf("  Chemistry:   %d facts/rules\n", config->chemistry_count);
    printf("  Programming: %d facts/rules\n", config->programming_count);
    printf("  Law:         %d facts/rules\n", config->law_count);
    printf("  TOTAL:       %d facts/rules\n", total);

    return 0;
}
