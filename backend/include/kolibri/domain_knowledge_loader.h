/*
 * domain_knowledge_loader.h
 *
 * Загрузчик доменных знаний для Kolibri reasoning engine
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#ifndef KOLIBRI_DOMAIN_KNOWLEDGE_LOADER_H
#define KOLIBRI_DOMAIN_KNOWLEDGE_LOADER_H

#include "kolibri/reasoning_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Загрузить все доменные знания (физика + химия + программирование + право)
 * @return количество загруженных фактов/правил
 */
int kolibri_domain_load_all(KolibriREConfig *config);

/**
 * Загрузить знания по конкретному домену
 */
int kolibri_domain_load_physics(KolibriREConfig *config);
int kolibri_domain_load_chemistry(KolibriREConfig *config);
int kolibri_domain_load_programming(KolibriREConfig *config);
int kolibri_domain_load_law(KolibriREConfig *config);

/**
 * Напечатать статистику загруженных знаний
 */
int kolibri_domain_print_stats(KolibriREConfig *config);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_DOMAIN_KNOWLEDGE_LOADER_H */
