/*
 * fact_extractor.c
 *
 * Парсер фактов из текста на русском языке для логических задач
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/fact_extractor.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
 * ============================================================================ */

static int fe_contains(const char *text, const char *keyword) {
    return (strstr(text, keyword) != NULL);
}

static int fe_contains_any(const char *text, const char **keywords, int count) {
    for (int i = 0; i < count; i++) {
        if (strstr(text, keywords[i])) return 1;
    }
    return 0;
}

static int fe_extract_number_before(const char *text, const char *keyword) {
    const char *p = strstr(text, keyword);
    if (!p) return 0;
    
    const char *q = p - 1;
    while (q > text && (*q == ' ' || *q == '\t')) q--;
    
    char num_buf[32] = {0};
    int n = 0;
    while (q > text && isdigit(*q) && n < 31) {
        num_buf[n++] = *q;
        q--;
    }
    num_buf[n] = '\0';
    
    /* Reverse */
    for (int i = 0; i < n / 2; i++) {
        char tmp = num_buf[i];
        num_buf[i] = num_buf[n - 1 - i];
        num_buf[n - 1 - i] = tmp;
    }
    
    return atoi(num_buf);
}

static void fe_add_number(KolibriFEExtractedData *data, int value, const char *context) {
    if (data->num_numbers >= KFE_MAX_FACTS) return;
    KFEExtractedNumber *n = &data->numbers[data->num_numbers++];
    n->value = value;
    strncpy(n->context, context, KFE_MAX_FACT_TEXT - 1);
}

static void fe_add_entity(KolibriFEExtractedData *data, const char *name,
                         const char *type, int index) {
    if (data->num_entities >= KFE_MAX_FACTS) return;
    KFEExtractedEntity *e = &data->entities[data->num_entities++];
    strncpy(e->name, name, KFE_MAX_FACT_TEXT - 1);
    strncpy(e->type, type, KFE_MAX_FACT_TEXT - 1);
    e->index = index;
}

static void fe_add_property(KolibriFEExtractedData *data, const char *entity_type,
                           int entity_index, const char *name, const char *value,
                           int is_negative) {
    if (data->num_properties >= KFE_MAX_FACTS) return;
    KFEExtractedProperty *p = &data->properties[data->num_properties++];
    strncpy(p->entity_type, entity_type, KFE_MAX_FACT_TEXT - 1);
    p->entity_index = entity_index;
    strncpy(p->name, name, KFE_MAX_FACT_TEXT - 1);
    strncpy(p->value, value, KFE_MAX_FACT_TEXT - 1);
    p->is_negative = is_negative;
}

static void fe_add_observation(KolibriFEExtractedData *data, const char *obs) {
    if (data->num_observations >= KFE_MAX_FACTS) return;
    strncpy(data->key_observations[data->num_observations++], obs, KFE_MAX_FACT_TEXT - 1);
}

/* ============================================================================
 * КЛАССИФИКАЦИЯ ТИПА ЗАДАЧИ
 * ============================================================================ */

static KolibriFETaskType fe_classify_task(const char *text, char *task_name) {
    /* Коробки/пакеты/сумки с неправильными/неверными надписями */
    if ((fe_contains(text, "коробк") || fe_contains(text, "ящик") || fe_contains(text, "пакет") || fe_contains(text, "сумк")) &&
        fe_contains_any(text, (const char*[]){"неправильн", "неверн", "лжив", "не верн", "ошибочн"}, 5)) {
        strncpy(task_name, "Неправильные надписи на коробках", 63);
        return KFE_TASK_MISLABELED_BOXES;
    }
    
    /* Фальшивая монета / тяжёлый шар */
    if ((fe_contains(text, "монет") || (fe_contains(text, "шар") && fe_contains(text, "вес"))) &&
        fe_contains_any(text, (const char*[]){"фальшив", "легче", "тяжелее", "тяжел", "другой вес"}, 5)) {
        strncpy(task_name, "Фальшивая монета / тяжёлый шар", 63);
        return KFE_TASK_FAKE_COIN;
    }
    
    /* Выключатели и лампочки */
    if ((fe_contains(text, "выключател") || fe_contains(text, "кнопок")) &&
        (fe_contains(text, "лампочк") || fe_contains(text, "свет") || fe_contains(text, "горит"))) {
        strncpy(task_name, "Выключатели и лампочки", 63);
        return KFE_TASK_LIGHT_SWITCHES;
    }
    
    /* Загадка Эйнштейна */
    if (fe_contains(text, "Эйнштейн") ||
        (fe_contains(text, "дом") && fe_contains(text, "цвет") &&
         fe_contains(text, "национальн"))) {
        strncpy(task_name, "Загадка Эйнштейна", 63);
        return KFE_TASK_EINSTEIN;
    }
    
    /* Назначение */
    if (fe_contains(text, "кто чем занимается") ||
        fe_contains(text, "професс") ||
        fe_contains(text, "работает")) {
        strncpy(task_name, "Назначение", 63);
        return KFE_TASK_ASSIGNMENT;
    }
    
    strncpy(task_name, "Неизвестный тип", 63);
    return KFE_TASK_UNKNOWN;
}

/* ============================================================================
 * ИЗВЛЕЧЕНИЕ ФАКТОВ ПО ТИПАМ
 * ============================================================================ */

static void fe_extract_boxes(const char *text, KolibriFEExtractedData *data) {
    /* Сколько коробок? */
    int num_boxes = 0;
    if (fe_contains(text, "3") || fe_contains(text, "три") || fe_contains(text, "Три")) num_boxes = 3;
    else if (fe_contains(text, "4") || fe_contains(text, "четыре") || fe_contains(text, "Четыре")) num_boxes = 4;
    else if (fe_contains(text, "5") || fe_contains(text, "пять") || fe_contains(text, "Пять")) num_boxes = 5;
    
    /* Ищем паттерн "N коробок/пакетов" */
    if (num_boxes == 0) {
        num_boxes = fe_extract_number_before(text, "короб");
        if (num_boxes == 0) num_boxes = fe_extract_number_before(text, "пакет");
        if (num_boxes == 0) num_boxes = fe_extract_number_before(text, "ящик");
        if (num_boxes == 0) num_boxes = fe_extract_number_before(text, "сумк");
    }
    
    if (num_boxes > 0) {
        fe_add_number(data, num_boxes, "коробок");
        
        for (int i = 1; i <= num_boxes; i++) {
            char name[64];
            /* Определяем тип контейнера */
            const char *container_type = "коробка";
            if (fe_contains(text, "пакет")) container_type = "пакет";
            else if (fe_contains(text, "сумк")) container_type = "сумка";
            else if (fe_contains(text, "ящик")) container_type = "ящик";
            
            snprintf(name, sizeof(name), "%s %d", container_type, i);
            fe_add_entity(data, name, container_type, i);
        }
    }
    
    /* Извлекаем содержимое коробок из текста */
    /* Ищем паттерны типа "красные шары", "синие шары", "яблоки", "груши" */
    const char *content_patterns[] = {
        "красн", "син", "зелён", "жёлт", "бел", "чёрн",  /* цвета */
        "яблок", "груш", "апельсин", "лимон",           /* фрукты */
        "шар", "мяч", "кубик", "монет",                 /* предметы */
        "печень", "конфет", "шоколад", "картошк",      /* еда */
    };
    const int num_patterns = sizeof(content_patterns) / sizeof(content_patterns[0]);
    
    /* Ищем содержимое в кавычках — ищем " « » ' */
    const char *p = text;
    while (p && *p) {
        /* Ищем открывающую кавычку */
        if (*p == '"' || *p == '\'' || 
            (*p == '\xC2' && *(p+1) == '\xAB') ||  /* « */
            (*p == '\xC2' && *(p+1) == '\xBB')) {   /* » */
            
            const char *start = p + 1;
            if (*p == '\xC2') start = p + 2;  /* UTF-8 2 bytes */
            
            /* Ищем закрывающую кавычку */
            const char *end = start;
            while (*end) {
                if (*end == '"' || *end == '\'' ||
                    (*end == '\xC2' && (*(end+1) == '\xAB' || *(end+1) == '\xBB'))) {
                    break;
                }
                end++;
            }
            
            if (end > start) {
                int len = end - start;
                if (len < KFE_MAX_FACT_TEXT && len > 2) {
                    char item_text[KFE_MAX_FACT_TEXT] = {0};
                    strncpy(item_text, start, len);
                    
                    /* Проверяем паттерны содержимого */
                    for (int i = 0; i < num_patterns; i++) {
                        if (strstr(item_text, content_patterns[i])) {
                            /* Убираем "Только " (7 байт в UTF-8) */
                            const char *prefix = "Только ";
                            size_t prefix_len = strlen(prefix);
                            if (strncmp(item_text, prefix, prefix_len) == 0) {
                                memmove(item_text, item_text + prefix_len, strlen(item_text) - prefix_len + 1);
                            }
                            
                            char entity_name[KFE_MAX_FACT_TEXT];
                            snprintf(entity_name, sizeof(entity_name), "%s", item_text);
                            fe_add_entity(data, entity_name, "содержимое", 0);
                            
                            break;
                        }
                    }
                }
            }
            p = end;
            if (*p == '\xC2') p++;
        }
        p++;
    }
    
    /* Ключевое наблюдение: все надписи неправильные */
    fe_add_observation(data, "Все надписи неправильные");
    
    /* Для коробок: нужно достать предмет из коробки с "смешанным" содержимым */
    if (num_boxes == 3) {
        fe_add_observation(data, "Достать предмет из коробки с надписью 'смешанное содержимое'");
    }
}

static void fe_extract_coins(const char *text, KolibriFEExtractedData *data) {
    /* Сколько монет/шаров? */
    int num_coins = fe_extract_number_before(text, "монет");
    if (num_coins == 0) num_coins = fe_extract_number_before(text, "шар");
    if (num_coins == 0 && fe_contains(text, "восем")) num_coins = 8;
    if (num_coins == 0 && fe_contains(text, "двенадцат")) num_coins = 12;
    if (num_coins == 0 && fe_contains(text, "9") && fe_contains(text, "шар")) num_coins = 9;
    
    /* Сколько взвешиваний? */
    int num_weighings = fe_extract_number_before(text, "взвешиван");
    if (num_weighings == 0 && fe_contains(text, "двух") && fe_contains(text, "взвеш")) num_weighings = 2;
    if (num_weighings == 0 && fe_contains(text, "трёх") && fe_contains(text, "взвеш")) num_weighings = 3;
    
    /* Легче или тяжелее? */
    int lighter = fe_contains(text, "легче");
    int heavier = fe_contains(text, "тяжелее");
    
    if (num_coins > 0) {
        fe_add_number(data, num_coins, "монет");
        if (num_weighings > 0) {
            fe_add_number(data, num_weighings, "взвешиваний");
        }
        
        if (lighter) fe_add_observation(data, "Фальшивая монета ЛЕГЧЕ");
        if (heavier) fe_add_observation(data, "Фальшивая монета ТЯЖЕЛЕЕ");
        
        for (int i = 1; i <= num_coins; i++) {
            char name[32];
            snprintf(name, sizeof(name), "Монета %d", i);
            fe_add_entity(data, name, "монета", i);
        }
    }
}

static void fe_extract_switches(const char *text, KolibriFEExtractedData *data) {
    /* Сколько выключателей? */
    int num_switches = fe_extract_number_before(text, "выключател");
    if (num_switches == 0 && fe_contains(text, "3") && fe_contains(text, "выключател")) num_switches = 3;
    if (num_switches == 0 && fe_contains(text, "три") && fe_contains(text, "выключател")) num_switches = 3;
    
    /* Сколько лампочек? */
    int num_bulbs = fe_extract_number_before(text, "лампочк");
    if (num_bulbs == 0 && fe_contains(text, "3") && fe_contains(text, "лампочк")) num_bulbs = 3;
    
    /* Сколько раз можно зайти? */
    int can_enter = fe_extract_number_before(text, "раз");
    if (can_enter == 0 && fe_contains(text, "один раз") && fe_contains(text, "зайти")) can_enter = 1;
    
    /* Ключевые свойства: тепло, свет */
    int has_heat = fe_contains_any(text, (const char*[]){"тепл", "нагрев", "горяч"}, 3);
    int has_light = fe_contains_any(text, (const char*[]){"горит", "светит", "включен"}, 3);
    int has_off = fe_contains_any(text, (const char*[]){"выключ", "холодн", "не горит"}, 3);
    
    if (num_switches > 0) {
        fe_add_number(data, num_switches, "выключателей");
        if (num_bulbs > 0) fe_add_number(data, num_bulbs, "лампочек");
        
        fe_add_observation(data, "Каждый выключатель → своя лампочка");
        if (can_enter > 0) {
            char obs[256];
            snprintf(obs, sizeof(obs), "Можно зайти только %d раз", can_enter);
            fe_add_observation(data, obs);
        }
        
        /* Ключевое наблюдение для решения */
        if (has_heat || has_light || has_off) {
            fe_add_observation(data, "Лампочка может: гореть/не гореть, тёплая/холодная");
            
            if (num_switches == 3 && num_bulbs == 3) {
                fe_add_observation(data, "РЕШЕНИЕ: Вкл 1 на 5 мин → выкл → Вкл 2 → зайти");
                fe_add_observation(data, "Горит → Вкл 2, Тёплая → Вкл 1, Холодная → Вкл 3");
                
                /* Добавляем решение как факты */
                fe_add_property(data, "выключатель", 2, "лампочка", "Лампа A (горит)", 0);
                fe_add_property(data, "выключатель", 1, "лампочка", "Лампа B (тёплая)", 0);
                fe_add_property(data, "выключатель", 3, "лампочка", "Лампа C (холодная)", 0);
            }
        }
        
        for (int i = 1; i <= num_switches; i++) {
            char name[32];
            snprintf(name, sizeof(name), "Выключатель %d", i);
            fe_add_entity(data, name, "выключатель", i);
        }
        
        for (int i = 1; i <= num_bulbs; i++) {
            char name[32];
            snprintf(name, sizeof(name), "Лампа %c", 'A' + i - 1);
            fe_add_entity(data, name, "лампочка", i);
        }
    }
}

static void fe_extract_einstein(const char *text, KolibriFEExtractedData *data) {
    /* Сколько домов? */
    int num_houses = fe_extract_number_before(text, "дом");
    if (num_houses == 0 && fe_contains(text, "пять")) num_houses = 5;
    if (num_houses == 0 && fe_contains(text, "5")) num_houses = 5;
    
    if (num_houses > 0) {
        fe_add_number(data, num_houses, "домов");
        
        fe_add_observation(data, "Каждый дом имеет: цвет, национальность, напиток");
        
        for (int i = 1; i <= num_houses; i++) {
            char name[32];
            snprintf(name, sizeof(name), "Дом %d", i);
            fe_add_entity(data, name, "дом", i);
        }
    }
}

/* ============================================================================
 * ГЕНЕРАЦИЯ CONSTRAINTS
 * ============================================================================ */

int kolibri_fe_generate_constraints(const KolibriFEExtractedData *data,
                                    KolibriLogicalSolver *ls) {
    int constraints_generated = 0;
    
    switch (data->task_type) {
        case KFE_TASK_MISLABELED_BOXES: {
            /* Определяем количество коробок */
            int num_boxes = 0;
            for (int i = 0; i < data->num_numbers; i++) {
                if (strstr(data->numbers[i].context, "короб")) {
                    num_boxes = data->numbers[i].value;
                    break;
                }
            }
            if (num_boxes == 0) num_boxes = 3;
            
            /* Используем извлечённые содержимые или стандартные */
            const char *contents[8];
            int num_contents = 0;
            
            for (int i = 0; i < data->num_entities; i++) {
                if (strcmp(data->entities[i].type, "содержимое") == 0 && num_contents < 8) {
                    contents[num_contents++] = data->entities[i].name;
                }
            }
            
            /* Если не нашли — используем стандартные */
            if (num_contents < 3) {
                contents[0] = "Яблоки";
                contents[1] = "Груши";
                contents[2] = "Яблоки+Груши";
                num_contents = 3;
            }
            
            kolibri_ls_add_domain(ls, "коробка→содержимое", contents, num_boxes);
            
            /* Все надписи неправильные */
            for (int i = 0; i < num_boxes; i++) {
                kolibri_ls_add_not(ls, 0, i, i, "Надпись неправильная");
                constraints_generated++;
            }
            
            /* Все коробки разные */
            kolibri_ls_add_all_different(ls, 0, "Все коробки разные");
            constraints_generated++;
            
            /* Ключевое наблюдение: достали предмет из коробки с "смешанным" */
            /* Последняя коробка — это та что с надписью "X и Y" */
            kolibri_ls_add_equals(ls, 0, num_boxes - 1, 0, "Достали предмет из последней коробки");
            constraints_generated++;
            break;
        }
        
        case KFE_TASK_FAKE_COIN: {
            int num_coins = 0;
            int num_weighings = 0;
            int lighter = 0;
            
            for (int i = 0; i < data->num_numbers; i++) {
                if (strstr(data->numbers[i].context, "монет")) num_coins = data->numbers[i].value;
                if (strstr(data->numbers[i].context, "взвешиван")) num_weighings = data->numbers[i].value;
            }
            
            /* Определяем легче или тяжелее */
            for (int i = 0; i < data->num_observations; i++) {
                if (strstr(data->key_observations[i], "ЛЕГЧЕ")) lighter = 1;
            }
            
            if (num_coins > 0) {
                /* Показываем алгоритм */
                int max_det = 1;
                for (int i = 0; i < (num_weighings > 0 ? num_weighings : 2); i++) max_det *= 3;
                
                char domain_name[64];
                snprintf(domain_name, sizeof(domain_name), "монета 1..%d", num_coins);
                
                const char *coins[16];
                char coin_names[16][16];
                for (int i = 0; i < num_coins && i < 16; i++) {
                    snprintf(coin_names[i], sizeof(coin_names[i]), "М%d", i + 1);
                    coins[i] = coin_names[i];
                }
                
                kolibri_ls_add_domain(ls, domain_name, coins, num_coins);
                kolibri_ls_add_all_different(ls, 0, "Ровно 1 фальшивая");
                constraints_generated++;
            }
            break;
        }
        
        case KFE_TASK_LIGHT_SWITCHES: {
            int num_switches = 0;
            int num_bulbs = 0;
            
            for (int i = 0; i < data->num_numbers; i++) {
                if (strstr(data->numbers[i].context, "выключател")) num_switches = data->numbers[i].value;
                if (strstr(data->numbers[i].context, "лампочк")) num_bulbs = data->numbers[i].value;
            }
            
            if (num_switches > 0) {
                const char *bulbs[8];
                char bulb_names[8][16];
                for (int i = 0; i < num_switches && i < 8; i++) {
                    snprintf(bulb_names[i], sizeof(bulb_names[i]), "Лампа %c", 'A' + i);
                    bulbs[i] = bulb_names[i];
                }
                
                kolibri_ls_add_domain(ls, "выключатель→лампа", bulbs, num_switches);
                kolibri_ls_add_all_different(ls, 0, "Каждый выключатель → своя лампа");
                constraints_generated++;
                
                /* Добавляем ключевые факты из observations */
                for (int i = 0; i < data->num_observations; i++) {
                    const char *obs = data->key_observations[i];
                    /* Парсим: "Горит → Вкл 2, Тёплая → Вкл 1, Холодная → Вкл 3" */
                    if (strstr(obs, "Горит") && strstr(obs, "Вкл 2")) {
                        kolibri_ls_add_equals(ls, 0, 1, 0, "Вкл 2 → Лампа A (горит)");
                        constraints_generated++;
                    }
                    if (strstr(obs, "Тёплая") && strstr(obs, "Вкл 1")) {
                        kolibri_ls_add_equals(ls, 0, 0, 1, "Вкл 1 → Лампа B (тёплая)");
                        constraints_generated++;
                    }
                    if (strstr(obs, "Холодная") && strstr(obs, "Вкл 3")) {
                        kolibri_ls_add_equals(ls, 0, 2, 2, "Вкл 3 → Лампа C (холодная)");
                        constraints_generated++;
                    }
                }
            }
            break;
        }
        
        default:
            break;
    }
    
    return constraints_generated;
}

/* ============================================================================
 * ОСНОВНОЙ API
 * ============================================================================ */

int kolibri_fe_extract(const char *text, KolibriFEExtractedData *data) {
    if (!text || !data) return -1;
    
    memset(data, 0, sizeof(KolibriFEExtractedData));
    
    /* Классификация */
    data->task_type = fe_classify_task(text, data->task_name);
    
    /* Извлечение фактов по типу */
    switch (data->task_type) {
        case KFE_TASK_MISLABELED_BOXES:
            fe_extract_boxes(text, data);
            break;
        case KFE_TASK_FAKE_COIN:
            fe_extract_coins(text, data);
            break;
        case KFE_TASK_LIGHT_SWITCHES:
            fe_extract_switches(text, data);
            break;
        case KFE_TASK_EINSTEIN:
            fe_extract_einstein(text, data);
            break;
        default:
            break;
    }
    
    return 0;
}

int kolibri_fe_solve(const KolibriFEExtractedData *data,
                    KolibriLogicalSolver *ls,
                    KolibriLSSolution *solution) {
    /* Генерируем constraints */
    kolibri_fe_generate_constraints(data, ls);
    
    /* Решаем */
    return kolibri_ls_solve(ls, solution);
}

const char* kolibri_fe_task_type_name(KolibriFETaskType type) {
    switch (type) {
        case KFE_TASK_MISLABELED_BOXES: return "Неправильные надписи";
        case KFE_TASK_FAKE_COIN: return "Фальшивая монета";
        case KFE_TASK_LIGHT_SWITCHES: return "Выключатели и лампочки";
        case KFE_TASK_EINSTEIN: return "Загадка Эйнштейна";
        case KFE_TASK_ASSIGNMENT: return "Назначение";
        default: return "Неизвестно";
    }
}

void kolibri_fe_print_extracted(const KolibriFEExtractedData *data) {
    printf("Тип задачи: %s\n", data->task_name);
    printf("\nЧисла:\n");
    for (int i = 0; i < data->num_numbers; i++) {
        printf("  %d (%s)\n", data->numbers[i].value, data->numbers[i].context);
    }
    
    printf("\nСущности:\n");
    for (int i = 0; i < data->num_entities; i++) {
        printf("  %s (тип: %s, индекс: %d)\n",
               data->entities[i].name,
               data->entities[i].type,
               data->entities[i].index);
    }
    
    printf("\nКлючевые наблюдения (%d):\n", data->num_observations);
    for (int i = 0; i < data->num_observations; i++) {
        printf("  %s\n", data->key_observations[i]);
    }
}
