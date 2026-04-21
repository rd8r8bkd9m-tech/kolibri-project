#include "logical_memory.h"
#include <stdio.h>
#include <string.h>

void kolibri_load_initial_knowledge() {
    /* Инъекция базовых фактов о проекте */
    kolibri_mem_store("что такое колибри", "Kolibri AI — это высокопроизводительный цифровой разум на базе C-core и Number-Thinking.", 1.0);
    kolibri_mem_store("сколько файлов", "В моей базе знаний проиндексировано 1455 файлов из 12 локальных проектов.", 1.0);
    kolibri_mem_store("автор", "Система разработана в рамках концепции Kolibri Hive-Mind для управления сложными IT-инфраструктурами.", 1.0);
    kolibri_mem_store("ядро", "Мое ядро написано на языке C (стандарт C23) и скомпилировано в WASM для работы в браузере.", 1.0);

    printf("[MEM] Knowledge injected: Initial project facts loaded.\n");
}
