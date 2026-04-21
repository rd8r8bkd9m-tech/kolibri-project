#ifndef LOGICAL_MEMORY_H
#define LOGICAL_MEMORY_H

void kolibri_mem_init();
void kolibri_mem_set_quiet(int quiet);
void kolibri_mem_store(const char* premise, const char* conclusion, float confidence);
int kolibri_mem_query(const char* query, char* out_buf, int buf_size);

#endif
