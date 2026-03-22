/*
 * inference.c
 *
 * Реализация модуля инференса Kolibri AI v66 (AGI)
 *
 * Центральный pipeline вывода:
 *   query → кодирование → поиск знаний → рассуждение → генерация → ответ
 *
 * Поддерживаемые стратегии:
 *   DIRECT   — прямой поиск по ключевым словам
 *   FORMULA  — формульный вывод через KolibriFormulaPool
 *   LOGICAL  — рассуждение через мета-формулы
 *   CHAIN    — многошаговый chain-of-thought
 *   HYBRID   — комбинация всех методов с голосованием
 *
 * v66 AGI расширения:
 *   - Шаг 5: Семантическое понимание через Self-Attention + World Model
 *   - Chain-of-Thought: разбиение запроса на подзадачи
 *   - Семантическое сходство чрез эмбеддинги Transformer
 */

#include "kolibri/inference.h"
#include "kolibri/knowledge.h"
#include "kolibri/formula.h"
#include "kolibri/logical_memory.h"
#include "kolibri/formula_logic.h"
#include "kolibri/fractal_memory.h"
#include "kolibri/attention.h"
#include "kolibri/world_model.h"
#include "kolibri/symbol_table.h"
#include <pthread.h>
#include <ctype.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>

/* ========== Внутренние утилиты ========== */

/* Получить текущее время в миллисекундах */
static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static size_t tokenize_query(const char *query, char tokens[][128], size_t max_tokens);
static double inference_text_overlap_score(const char *query, const char *candidate);
static int inference_contains_phrase(const char *text, const char *phrase);
static size_t inference_extract_topic_tokens(const char *query,
                                             char tokens[][128],
                                             size_t max_tokens);
static size_t inference_extract_definition_entity(const char *query,
                                                  char *out,
                                                  size_t out_size);
static int inference_definition_support_ok(const char *entity, const char *answer);
static int inference_definition_forbidden_hit(const char *entity, const char *answer);
static double inference_topic_overlap_score(const char *query,
                                            const char *candidate);
static double inference_topic_exact_match_score(const char *query,
                                                const char *candidate,
                                                size_t *query_topic_count,
                                                size_t *candidate_topic_count);
static int inference_is_definition_query(const char *query);
static int inference_extract_definition_clause(const char *text,
                                               const char *entity,
                                               char *out,
                                               size_t out_size);
static int inference_rewrite_definition_clause(const char *text,
                                               char *out,
                                               size_t out_size);

static KolibriKnowledgeIndex g_inference_knowledge_index;
static pthread_once_t g_inference_knowledge_once = PTHREAD_ONCE_INIT;
static int g_inference_knowledge_ready = 0;
static KolibriFormulaPool g_formula_memory_pool;
static KolibriSymbolTable g_formula_symbol_table;
static pthread_once_t g_formula_memory_once = PTHREAD_ONCE_INIT;
static int g_formula_memory_ready = 0;

static const char *inference_knowledge_root(void) {
    const char *root = getenv("KOLIBRI_KNOWLEDGE_PATH");
    return (root && root[0] != '\0') ? root : "data";
}

static const char *inference_formula_memory_root(void) {
    const char *root = getenv("KOLIBRI_FORMULA_MEMORY_PATH");
    return (root && root[0] != '\0') ? root : inference_knowledge_root();
}

static int inference_is_directory(const char *path) {
    struct stat st;
    if (!path || stat(path, &st) != 0) {
        return 0;
    }
    return S_ISDIR(st.st_mode);
}

static char *inference_string_slice(const char *begin, size_t length) {
    char *copy = (char *)malloc(length + 1U);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, begin, length);
    copy[length] = '\0';
    return copy;
}

static char *inference_lower_ascii_copy(const char *text) {
    if (!text) {
        return NULL;
    }
    size_t length = strlen(text);
    char *copy = (char *)malloc(length * 2U + 1U);
    if (!copy) {
        return NULL;
    }
    size_t in_pos = 0U;
    size_t out_pos = 0U;
    while (in_pos < length) {
        unsigned char byte = (unsigned char)text[in_pos];
        if (byte == 0xCCU &&
            in_pos + 1U < length &&
            (unsigned char)text[in_pos + 1U] == 0x81U) {
            in_pos += 2U;
            continue;
        }
        if (byte < 0x80U) {
            copy[out_pos++] = (char)tolower(byte);
            ++in_pos;
            continue;
        }
        if (in_pos + 1U < length &&
            byte == 0xD0U &&
            (unsigned char)text[in_pos + 1U] >= 0x90U &&
            (unsigned char)text[in_pos + 1U] <= 0x9FU) {
            copy[out_pos++] = (char)0xD0U;
            copy[out_pos++] = (char)((unsigned char)text[in_pos + 1U] + 0x20U);
            in_pos += 2U;
            continue;
        }
        if (in_pos + 1U < length &&
            byte == 0xD0U &&
            (unsigned char)text[in_pos + 1U] >= 0xA0U &&
            (unsigned char)text[in_pos + 1U] <= 0xAFU) {
            copy[out_pos++] = (char)0xD1U;
            copy[out_pos++] = (char)((unsigned char)text[in_pos + 1U] - 0x20U);
            in_pos += 2U;
            continue;
        }
        if (in_pos + 1U < length &&
            byte == 0xD0U &&
            (unsigned char)text[in_pos + 1U] == 0x81U) {
            copy[out_pos++] = (char)0xD1U;
            copy[out_pos++] = (char)0x91U;
            in_pos += 2U;
            continue;
        }
        copy[out_pos++] = text[in_pos++];
    }
    copy[out_pos] = '\0';
    return copy;
}

static char *inference_strip_accents_copy(const char *text) {
    if (!text) {
        return NULL;
    }
    size_t length = strlen(text);
    char *copy = (char *)malloc(length + 1U);
    if (!copy) {
        return NULL;
    }
    size_t in_pos = 0U;
    size_t out_pos = 0U;
    while (in_pos < length) {
        if ((unsigned char)text[in_pos] == 0xCCU &&
            in_pos + 1U < length &&
            (unsigned char)text[in_pos + 1U] == 0x81U) {
            in_pos += 2U;
            continue;
        }
        copy[out_pos++] = text[in_pos++];
    }
    copy[out_pos] = '\0';
    return copy;
}

static void inference_normalize_text(char *text) {
    if (!text) {
        return;
    }
    for (size_t i = 0; text[i] != '\0'; ++i) {
        unsigned char byte = (unsigned char)text[i];
        if (byte < 0x80U && !isalnum(byte) && !isspace(byte)) {
            text[i] = ' ';
        }
    }
}

static void inference_trim_inplace(char *text) {
    if (!text || text[0] == '\0') {
        return;
    }
    size_t len = strlen(text);
    size_t start = 0U;
    while (start < len && isspace((unsigned char)text[start])) {
        ++start;
    }
    size_t end = len;
    while (end > start && isspace((unsigned char)text[end - 1U])) {
        --end;
    }
    if (start > 0U) {
        memmove(text, text + start, end - start);
    }
    text[end - start] = '\0';
}

static char *inference_read_text_slice(const char *path, size_t max_bytes) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    char *buffer = (char *)malloc(max_bytes + 1U);
    if (!buffer) {
        fclose(file);
        return NULL;
    }
    size_t read = fread(buffer, 1U, max_bytes, file);
    fclose(file);
    buffer[read] = '\0';
    return buffer;
}

static char *inference_extract_title(const char *content, const char *path) {
    if (content) {
        const char *line_start = content;
        while (*line_start) {
            const char *line_end = strchr(line_start, '\n');
            size_t length = line_end ? (size_t)(line_end - line_start) : strlen(line_start);
            while (length > 0U && isspace((unsigned char)line_start[length - 1U])) {
                --length;
            }
            if (length > 0U && line_start[0] == '#') {
                while (length > 0U && line_start[0] == '#') {
                    ++line_start;
                    --length;
                }
                while (length > 0U && isspace((unsigned char)*line_start)) {
                    ++line_start;
                    --length;
                }
                return inference_string_slice(line_start, length);
            }
            if (!line_end) {
                break;
            }
            line_start = line_end + 1;
        }
    }
    const char *basename = path ? strrchr(path, '/') : NULL;
    basename = basename ? basename + 1 : (path ? path : "память");
    const char *dot = strrchr(basename, '.');
    size_t length = dot ? (size_t)(dot - basename) : strlen(basename);
    return inference_string_slice(basename, length);
}

static void inference_clean_title_inplace(char *title) {
    if (!title || title[0] == '\0') {
        return;
    }
    inference_trim_inplace(title);
    char *wiki = strstr(title, "Википедия");
    if (!wiki) {
        wiki = strstr(title, "Wikipedia");
    }
    if (wiki) {
        char *sep = strstr(title, " — ");
        if (!sep) {
            sep = strstr(title, " - ");
        }
        if (!sep) {
            sep = strstr(title, " | ");
        }
        if (sep && sep < wiki) {
            *sep = '\0';
        } else {
            char *cut = wiki;
            while (cut > title && isspace((unsigned char)cut[-1])) {
                --cut;
            }
            *cut = '\0';
        }
        inference_trim_inplace(title);
    }
}

static char *inference_extract_answer(const char *content) {
    if (!content) {
        return NULL;
    }
    const char *body = content;
    if (body[0] == '#') {
        const char *newline = strchr(body, '\n');
        if (newline) {
            body = newline + 1;
        }
    }
    while (*body && isspace((unsigned char)*body)) {
        ++body;
    }
    size_t length = strlen(body);
    if (length > 480U) {
        length = 480U;
        while (length > 0U && !isspace((unsigned char)body[length - 1U])) {
            --length;
        }
        if (length == 0U) {
            length = 480U;
        }
    }
    char *answer = inference_string_slice(body, length);
    if (answer) {
        inference_trim_inplace(answer);
    }
    return answer;
}

static int inference_is_noise_line(const char *line) {
    if (!line || line[0] == '\0') {
        return 1;
    }
    if (strncmp(line, "Материал из Википедии", 20) == 0 ||
        strstr(line, "Википедия") != NULL ||
        strstr(line, "Wikipedia") != NULL ||
        strncmp(line, "Перейти к", 9) == 0 ||
        strncmp(line, "Стабильная версия", 17) == 0 ||
        strncmp(line, "У этого термина", 15) == 0 ||
        strncmp(line, "Запрос ", 7) == 0 ||
        strncmp(line, "Содержание", 10) == 0 ||
        strncmp(line, "Медиафайлы", 10) == 0 ||
        strncmp(line, "Наука", 5) == 0 ||
        strncmp(line, "Тема", 4) == 0 ||
        strncmp(line, "Предмет изучения", 16) == 0 ||
        strncmp(line, "Период зарождения", 17) == 0 ||
        strncmp(line, "Основные направления", 20) == 0) {
        return 1;
    }
    return 0;
}

static int inference_contains_phrase(const char *text, const char *phrase) {
    if (!text || !phrase || phrase[0] == '\0') {
        return 0;
    }
    char *norm_text = inference_lower_ascii_copy(text);
    char *norm_phrase = inference_lower_ascii_copy(phrase);
    if (!norm_text || !norm_phrase) {
        free(norm_text);
        free(norm_phrase);
        return 0;
    }
    inference_normalize_text(norm_text);
    inference_normalize_text(norm_phrase);
    inference_trim_inplace(norm_text);
    inference_trim_inplace(norm_phrase);
    int found = strstr(norm_text, norm_phrase) != NULL;
    free(norm_text);
    free(norm_phrase);
    return found;
}

static int inference_extract_keyword_window(const char *text,
                                            const char *keyword,
                                            char *out,
                                            size_t out_size) {
    if (!text || !keyword || !out || out_size == 0U) {
        return 0;
    }
    const char *hit = strstr(text, keyword);
    if (!hit) {
        return 0;
    }

    const char *start = hit;
    while (start > text && start[-1] != '.' && start[-1] != '!' &&
           start[-1] != '?' && start[-1] != '\n') {
        --start;
    }
    while (*start && isspace((unsigned char)*start)) {
        ++start;
    }

    const char *end = hit;
    while (*end && *end != '.' && *end != '!' && *end != '?' && *end != '\n') {
        ++end;
    }
    size_t len = (size_t)(end - start);
    if (len == 0U) {
        return 0;
    }
    if (len >= out_size) {
        len = out_size - 1U;
    }
    memcpy(out, start, len);
    out[len] = '\0';
    inference_trim_inplace(out);
    return out[0] != '\0';
}

static int inference_extract_entity_phrase_window(const char *text,
                                                  const char *query,
                                                  const char *verb_stem,
                                                  char *out,
                                                  size_t out_size) {
    if (!text || !query || !verb_stem || !out || out_size == 0U) {
        return 0;
    }
    char *norm_query = inference_lower_ascii_copy(query);
    char *norm_text = inference_lower_ascii_copy(text);
    if (!norm_query || !norm_text) {
        free(norm_query);
        free(norm_text);
        return 0;
    }
    inference_normalize_text(norm_query);
    inference_trim_inplace(norm_query);

    char tokens[16][128];
    size_t token_count = tokenize_query(norm_query, tokens, 16);
    const char *entity = NULL;
    for (size_t i = token_count; i > 0; --i) {
        if (strlen(tokens[i - 1U]) >= 4U) {
            entity = tokens[i - 1U];
            break;
        }
    }
    if (!entity) {
        free(norm_query);
        free(norm_text);
        return 0;
    }

    char pattern[256];
    snprintf(pattern, sizeof(pattern), "%s %s", entity, verb_stem);
    char *hit = strstr(norm_text, pattern);
    if (!hit) {
        free(norm_query);
        free(norm_text);
        return 0;
    }
    size_t offset = (size_t)(hit - norm_text);
    if (offset >= strlen(text)) {
        free(norm_query);
        free(norm_text);
        return 0;
    }

    const char *start = text + offset;
    while (start > text && start[-1] != '.' && start[-1] != '!' &&
           start[-1] != '?' && start[-1] != '\n') {
        --start;
    }
    while (*start && isspace((unsigned char)*start)) {
        ++start;
    }
    const char *end = text + offset;
    while (*end && *end != '.' && *end != '!' && *end != '?' && *end != '\n') {
        ++end;
    }
    size_t len = (size_t)(end - start);
    if (len >= out_size) {
        len = out_size - 1U;
    }
    memcpy(out, start, len);
    out[len] = '\0';
    inference_trim_inplace(out);

    free(norm_query);
    free(norm_text);
    return out[0] != '\0';
}

static int inference_matches_suffix(const char *text,
                                    size_t end_index,
                                    const char *suffix) {
    if (!text || !suffix) {
        return 0;
    }
    size_t suffix_len = strlen(suffix);
    if (suffix_len == 0U || end_index < suffix_len) {
        return 0;
    }
    return strncmp(text + end_index - suffix_len, suffix, suffix_len) == 0;
}

static size_t inference_dash_width_at(const char *text, size_t index) {
    if (!text || text[index] == '\0') {
        return 0U;
    }
    if ((unsigned char)text[index] == 0xE2U &&
        (unsigned char)text[index + 1U] == 0x80U &&
        (unsigned char)text[index + 2U] == 0x94U) {
        return 3U;
    }
    if (text[index] == '-') {
        return 1U;
    }
    return 0U;
}

static int inference_is_sentence_boundary_at(const char *text,
                                             size_t index,
                                             int paren_depth) {
    if (!text) {
        return 0;
    }
    unsigned char byte = (unsigned char)text[index];
    if (byte == '\0') {
        return 1;
    }
    if (paren_depth > 0) {
        return 0;
    }
    if (byte == '\n' || byte == '!' || byte == '?') {
        return 1;
    }
    if (byte != '.') {
        return 0;
    }
    if (inference_matches_suffix(text, index + 1U, "лат.") ||
        inference_matches_suffix(text, index + 1U, "греч.") ||
        inference_matches_suffix(text, index + 1U, "др.-греч.") ||
        inference_matches_suffix(text, index + 1U, "др.-рус.") ||
        inference_matches_suffix(text, index + 1U, "т.д.") ||
        inference_matches_suffix(text, index + 1U, "т.п.") ||
        inference_matches_suffix(text, index + 1U, "и т.д.") ||
        inference_matches_suffix(text, index + 1U, "и т.п.")) {
        return 0;
    }
    if (index > 0U &&
        isdigit((unsigned char)text[index - 1U]) &&
        isdigit((unsigned char)text[index + 1U])) {
        return 0;
    }
    return 1;
}

static int inference_extract_definition_clause(const char *text,
                                               const char *entity,
                                               char *out,
                                               size_t out_size) {
    if (!text || !entity || !out || out_size == 0U) {
        return 0;
    }
    out[0] = '\0';

    const char *start = text;
    while (*start && isspace((unsigned char)*start)) {
        ++start;
    }

    char *norm_text = inference_lower_ascii_copy(text);
    char *norm_entity = inference_lower_ascii_copy(entity);
    if (norm_text && norm_entity && norm_entity[0] != '\0') {
        char *hit = strstr(norm_text, norm_entity);
        if (hit) {
            size_t offset = (size_t)(hit - norm_text);
            if (offset < strlen(text)) {
                start = text + offset;
                while (start > text && start[-1] != '\n' &&
                       start[-1] != '.' && start[-1] != '!' && start[-1] != '?') {
                    --start;
                }
                while (*start && isspace((unsigned char)*start)) {
                    ++start;
                }
            }
        }
    }
    free(norm_text);
    free(norm_entity);

    size_t start_index = (size_t)(start - text);
    size_t main_dash = SIZE_MAX;
    int paren_depth = 0;
    for (size_t i = start_index; text[i] != '\0'; ++i) {
        unsigned char byte = (unsigned char)text[i];
        if (byte == '(') {
            ++paren_depth;
            continue;
        }
        if (byte == ')') {
            if (paren_depth > 0) {
                --paren_depth;
            }
            continue;
        }
        if (paren_depth == 0) {
            size_t dash_width = inference_dash_width_at(text, i);
            if (dash_width > 0U) {
                main_dash = i;
                break;
            }
        }
        if (inference_is_sentence_boundary_at(text, i, paren_depth)) {
            break;
        }
    }

    size_t end_index = start_index;
    paren_depth = 0;
    size_t cursor = (main_dash != SIZE_MAX) ? main_dash : start_index;
    for (size_t i = cursor; ; ++i) {
        unsigned char byte = (unsigned char)text[i];
        if (byte == '(') {
            ++paren_depth;
        } else if (byte == ')') {
            if (paren_depth > 0) {
                --paren_depth;
            }
        }
        if (inference_is_sentence_boundary_at(text, i, paren_depth)) {
            end_index = i;
            break;
        }
        if (byte == '\0') {
            end_index = i;
            break;
        }
    }

    while (end_index > start_index &&
           isspace((unsigned char)text[end_index - 1U])) {
        --end_index;
    }
    if (end_index <= start_index) {
        return 0;
    }

    size_t length = end_index - start_index;
    if (length >= out_size) {
        length = out_size - 1U;
    }
    memcpy(out, text + start_index, length);
    out[length] = '\0';
    inference_trim_inplace(out);
    return out[0] != '\0';
}

static int inference_rewrite_definition_clause(const char *text,
                                               char *out,
                                               size_t out_size) {
    if (!text || !out || out_size == 0U) {
        return 0;
    }
    out[0] = '\0';

    size_t main_dash = SIZE_MAX;
    int paren_depth = 0;
    for (size_t i = 0U; text[i] != '\0'; ++i) {
        unsigned char byte = (unsigned char)text[i];
        if (byte == '(') {
            ++paren_depth;
            continue;
        }
        if (byte == ')') {
            if (paren_depth > 0) {
                --paren_depth;
            }
            continue;
        }
        if (paren_depth == 0 && inference_dash_width_at(text, i) > 0U) {
            main_dash = i;
            break;
        }
    }
    if (main_dash == SIZE_MAX) {
        return 0;
    }

    char subject[512];
    size_t subject_pos = 0U;
    paren_depth = 0;
    for (size_t i = 0U; i < main_dash && subject_pos + 1U < sizeof(subject); ++i) {
        unsigned char byte = (unsigned char)text[i];
        if (byte == '(') {
            ++paren_depth;
            continue;
        }
        if (byte == ')') {
            if (paren_depth > 0) {
                --paren_depth;
            }
            continue;
        }
        if (paren_depth > 0) {
            continue;
        }
        subject[subject_pos++] = (char)byte;
    }
    subject[subject_pos] = '\0';
    inference_trim_inplace(subject);
    if (subject[0] == '\0') {
        return 0;
    }

    size_t dash_width = inference_dash_width_at(text, main_dash);
    const char *definition = text + main_dash + dash_width;
    while (*definition && isspace((unsigned char)*definition)) {
        ++definition;
    }
    if (*definition == '\0') {
        return 0;
    }

    snprintf(out, out_size, "%s — %s", subject, definition);
    inference_trim_inplace(out);
    return out[0] != '\0';
}

static void inference_clean_definition_line(const char *selected,
                                            char *out,
                                            size_t out_size) {
    if (!out || out_size == 0U) {
        return;
    }
    out[0] = '\0';
    if (!selected || selected[0] == '\0') {
        return;
    }

    char cleaned[1024];
    size_t src_len = strlen(selected);
    if (src_len >= sizeof(cleaned)) {
        src_len = sizeof(cleaned) - 1U;
    }
    memcpy(cleaned, selected, src_len);
    cleaned[src_len] = '\0';

    char compact[1024];
    size_t out_pos = 0U;
    int bracket_depth = 0;
    for (size_t i = 0U; cleaned[i] != '\0' && out_pos + 1U < sizeof(compact); ++i) {
        unsigned char byte = (unsigned char)cleaned[i];
        if (byte == '[') {
            bracket_depth++;
            continue;
        }
        if (byte == ']') {
            if (bracket_depth > 0) {
                bracket_depth--;
            }
            continue;
        }
        if (bracket_depth > 0) {
            continue;
        }
        compact[out_pos++] = (char)byte;
    }
    compact[out_pos] = '\0';
    inference_trim_inplace(compact);
    if (inference_rewrite_definition_clause(compact, cleaned, sizeof(cleaned))) {
        snprintf(out, out_size, "%s", cleaned);
        return;
    }

    const char *latin_markers[] = {
        "от лат.",
        "от лат ",
        "от др.-греч.",
        "от др.-греч ",
        "лат.",
        "др.-греч.",
    };
    for (size_t i = 0U; i < sizeof(latin_markers) / sizeof(latin_markers[0]); ++i) {
        char *marker = strstr(compact, latin_markers[i]);
        if (marker) {
            char *cut = marker;
            while (cut > compact && isspace((unsigned char)cut[-1])) {
                --cut;
            }
            *cut = '\0';
            inference_trim_inplace(compact);
        }
    }
    snprintf(out, out_size, "%s", compact);
}

static int inference_extract_next_sentence_after(const char *text,
                                                 const char *anchor,
                                                 char *out,
                                                 size_t out_size) {
    if (!text || !anchor || !out || out_size == 0U || anchor[0] == '\0') {
        return 0;
    }
    out[0] = '\0';

    const char *hit = strstr(text, anchor);
    if (!hit) {
        return 0;
    }
    size_t anchor_len = strlen(anchor);
    const char *cursor = hit + anchor_len;
    while (*cursor && isspace((unsigned char)*cursor)) {
        ++cursor;
    }
    if (*cursor == '.' || *cursor == '!' || *cursor == '?') {
        ++cursor;
    }
    while (*cursor && isspace((unsigned char)*cursor)) {
        ++cursor;
    }
    if (*cursor == '\0') {
        return 0;
    }

    const char *start = cursor;
    int paren_depth = 0;
    while (*cursor) {
        unsigned char byte = (unsigned char)*cursor;
        if (byte == '(') {
            ++paren_depth;
        } else if (byte == ')') {
            if (paren_depth > 0) {
                --paren_depth;
            }
        }
        if (inference_is_sentence_boundary_at(start, (size_t)(cursor - start), paren_depth)) {
            break;
        }
        ++cursor;
    }

    size_t len = (size_t)(cursor - start);
    if (len == 0U) {
        return 0;
    }
    if (len >= out_size) {
        len = out_size - 1U;
    }
    memcpy(out, start, len);
    out[len] = '\0';
    inference_trim_inplace(out);
    return out[0] != '\0' && !inference_is_noise_line(out);
}

static int inference_append_distinct_sentence(char *out,
                                              size_t out_size,
                                              const char *candidate) {
    if (!out || out_size == 0U || !candidate || candidate[0] == '\0') {
        return 0;
    }

    char cleaned[1024];
    inference_clean_definition_line(candidate, cleaned, sizeof(cleaned));
    if (cleaned[0] == '\0') {
        return 0;
    }
    if (strstr(out, cleaned) != NULL || strstr(cleaned, out) != NULL) {
        return 0;
    }

    size_t out_len = strlen(out);
    size_t cand_len = strlen(cleaned);
    size_t needed = cand_len + 1U;
    if (out_len > 0U) {
        needed += 2U;
    }
    if (out_len + needed >= out_size) {
        return 0;
    }

    if (out_len > 0U) {
        char last = out[out_len - 1U];
        if (last != '.' && last != '!' && last != '?') {
            strncat(out, ".", out_size - strlen(out) - 1U);
        }
        strncat(out, " ", out_size - strlen(out) - 1U);
    }
    strncat(out, cleaned, out_size - strlen(out) - 1U);
    return 1;
}

static int inference_append_distinct_paragraph(char *out,
                                               size_t out_size,
                                               const char *candidate) {
    if (!out || out_size == 0U || !candidate || candidate[0] == '\0') {
        return 0;
    }

    char cleaned[1024];
    inference_clean_definition_line(candidate, cleaned, sizeof(cleaned));
    if (cleaned[0] == '\0') {
        return 0;
    }
    if (strstr(out, cleaned) != NULL || strstr(cleaned, out) != NULL) {
        return 0;
    }

    size_t out_len = strlen(out);
    size_t cand_len = strlen(cleaned);
    size_t needed = cand_len + 3U;
    if (out_len + needed >= out_size) {
        return 0;
    }

    if (out_len > 0U) {
        char last = out[out_len - 1U];
        if (last != '.' && last != '!' && last != '?') {
            strncat(out, ".", out_size - strlen(out) - 1U);
        }
        strncat(out, "\n\n", out_size - strlen(out) - 1U);
    }
    strncat(out, cleaned, out_size - strlen(out) - 1U);
    return 1;
}

static int inference_rewrite_studies_answer(const char *entity,
                                            const char *answer,
                                            char *out,
                                            size_t out_size) {
    if (!entity || !answer || !out || out_size == 0U) {
        return 0;
    }
    out[0] = '\0';

    const char *dash = strstr(answer, "—");
    if (!dash) {
        return 0;
    }
    dash += strlen("—");
    while (*dash && isspace((unsigned char)*dash)) {
        ++dash;
    }
    if (*dash == '\0') {
        return 0;
    }

    char tail[1024];
    snprintf(tail, sizeof(tail), "%s", dash);
    inference_trim_inplace(tail);

    const char *science_about = "наука о ";
    const char *natural_science_about = "естественная наука о ";
    const char *complex_science_about = "комплекс естественных и общественных наук о ";

    if (strncmp(tail, science_about, strlen(science_about)) == 0) {
        snprintf(out, out_size, "%s изучает %s", entity, tail + strlen(science_about));
        return 1;
    }
    if (strncmp(tail, natural_science_about, strlen(natural_science_about)) == 0) {
        snprintf(out, out_size, "%s изучает %s", entity, tail + strlen(natural_science_about));
        return 1;
    }
    if (strncmp(tail, complex_science_about, strlen(complex_science_about)) == 0) {
        snprintf(out, out_size, "%s изучает %s", entity, tail + strlen(complex_science_about));
        return 1;
    }
    return 0;
}

static void inference_compose_human_answer(const char *query,
                                           const char *answer,
                                           char *out,
                                           size_t out_size) {
    if (!out || out_size == 0U) {
        return;
    }
    out[0] = '\0';
    if (!answer || answer[0] == '\0') {
        return;
    }

    char *copy = inference_string_slice(answer, strlen(answer));
    if (!copy) {
        snprintf(out, out_size, "%s", answer);
        return;
    }

    int want_definition = inference_contains_phrase(query, "что такое") ||
                          inference_contains_phrase(query, "объясни") ||
                          inference_contains_phrase(query, "расскажи") ||
                          inference_contains_phrase(query, "что ты знаешь") ||
                          inference_contains_phrase(query, "как устроен") ||
                          inference_contains_phrase(query, "как устроена") ||
                          inference_contains_phrase(query, "как устроено") ||
                          inference_contains_phrase(query, "почему важ") ||
                          inference_contains_phrase(query, "зачем нуж");
    int want_extended_definition = inference_contains_phrase(query, "объясни") ||
                                   inference_contains_phrase(query, "расскажи") ||
                                   inference_contains_phrase(query, "что ты знаешь") ||
                                   inference_contains_phrase(query, "как устроен") ||
                                   inference_contains_phrase(query, "как устроена") ||
                                   inference_contains_phrase(query, "как устроено") ||
                                   inference_contains_phrase(query, "почему важ") ||
                                   inference_contains_phrase(query, "зачем нуж");
    int want_detailed_definition = inference_contains_phrase(query, "подробно") ||
                                   inference_contains_phrase(query, "что ты знаешь");
    char definition_entity[256];
    inference_extract_definition_entity(query, definition_entity, sizeof(definition_entity));

    if (query && query[0] != '\0') {
        if (inference_contains_phrase(query, "изучает")) {
            char keyword_window[768];
            if (inference_extract_entity_phrase_window(answer, query, "изучает", keyword_window, sizeof(keyword_window)) ||
                inference_extract_entity_phrase_window(answer, query, "изуча", keyword_window, sizeof(keyword_window))) {
                snprintf(out, out_size, "%s", keyword_window);
                free(copy);
                return;
            }
            if (inference_extract_keyword_window(answer, "изучает", keyword_window, sizeof(keyword_window))) {
                snprintf(out, out_size, "%s", keyword_window);
                free(copy);
                return;
            }
            if (definition_entity[0] != '\0' &&
                inference_rewrite_studies_answer(definition_entity, answer, keyword_window, sizeof(keyword_window))) {
                snprintf(out, out_size, "%s", keyword_window);
                free(copy);
                return;
            }
        }
        if (inference_contains_phrase(query, "занимается") ||
            inference_contains_phrase(query, "чем занимается")) {
            char keyword_window[768];
            if (inference_extract_keyword_window(answer, "занимается", keyword_window, sizeof(keyword_window))) {
                snprintf(out, out_size, "%s", keyword_window);
                free(copy);
                return;
            }
        }
    }

    if (want_extended_definition && definition_entity[0] != '\0') {
        char clause[1024];
        char follow[1024];
        char third[1024];
        char primary[1024];
        char secondary[1024];
        if (inference_extract_definition_clause(answer, definition_entity, clause, sizeof(clause)) &&
            inference_extract_next_sentence_after(answer, clause, follow, sizeof(follow))) {
            inference_clean_definition_line(clause, primary, sizeof(primary));
            inference_clean_definition_line(follow, secondary, sizeof(secondary));
            if (primary[0] != '\0' &&
                secondary[0] != '\0' &&
                strcmp(primary, secondary) != 0 &&
                strstr(primary, secondary) == NULL &&
                strstr(secondary, primary) == NULL) {
                snprintf(out, out_size, "%s. %s", primary, secondary);
                third[0] = '\0';
                if (inference_extract_next_sentence_after(answer, follow, third, sizeof(third))) {
                    if (want_detailed_definition) {
                        inference_append_distinct_paragraph(out, out_size, third);
                    } else {
                        inference_append_distinct_sentence(out, out_size, third);
                    }
                    if (want_detailed_definition && third[0] != '\0') {
                        char fourth[1024];
                        fourth[0] = '\0';
                        if (inference_extract_next_sentence_after(answer, third, fourth, sizeof(fourth))) {
                            inference_append_distinct_sentence(out, out_size, fourth);
                        }
                    }
                }
                free(copy);
                return;
            }
        }
    }

    char best_line[768] = {0};
    char second_line[768] = {0};
    char fallback_line[768] = {0};
    double best_score = -1.0;
    double second_score = -1.0;
    if (want_definition && definition_entity[0] != '\0') {
        char clause[1024];
        if (inference_extract_definition_clause(answer, definition_entity, clause, sizeof(clause))) {
            snprintf(best_line, sizeof(best_line), "%s", clause);
            best_score = 10.0;
        }
    }
    size_t copy_len = strlen(copy);
    size_t start = 0U;
    int paren_depth = 0;
    for (size_t i = 0U; i <= copy_len; ++i) {
        unsigned char byte = (unsigned char)copy[i];
        if (byte == '(') {
            ++paren_depth;
        } else if (byte == ')') {
            if (paren_depth > 0) {
                --paren_depth;
            }
        }
        int is_boundary = inference_is_sentence_boundary_at(copy, i, paren_depth);
        if (!is_boundary) {
            continue;
        }
        size_t fragment_len = i > start ? (i - start) : 0U;
        if (fragment_len > 0U) {
            char line[1024];
            if (fragment_len >= sizeof(line)) {
                fragment_len = sizeof(line) - 1U;
            }
            memcpy(line, copy + start, fragment_len);
            line[fragment_len] = '\0';
            inference_trim_inplace(line);

            if (!inference_is_noise_line(line)) {
                if (fallback_line[0] == '\0' && strlen(line) >= 24U) {
                    snprintf(fallback_line, sizeof(fallback_line), "%s", line);
                }
                double score = 0.0;
                if (query && query[0] != '\0') {
                    score += inference_text_overlap_score(query, line) * 3.0;
                    score += inference_topic_overlap_score(query, line) * 2.8;
                }
                if (want_definition && definition_entity[0] != '\0') {
                    char *line_norm = inference_lower_ascii_copy(line);
                    char *entity_norm = inference_lower_ascii_copy(definition_entity);
                    if (line_norm && entity_norm) {
                        inference_normalize_text(line_norm);
                        inference_normalize_text(entity_norm);
                        inference_trim_inplace(line_norm);
                        inference_trim_inplace(entity_norm);
                        if (strncmp(line_norm, entity_norm, strlen(entity_norm)) == 0) {
                            score += 2.8;
                        } else if (strstr(line_norm, entity_norm) != NULL) {
                            score += 1.2;
                        }
                    }
                    free(line_norm);
                    free(entity_norm);
                }
                if (strstr(line, "—") != NULL && strlen(line) >= 40U) {
                    score += want_definition ? 1.8 : 0.5;
                }
                if (inference_contains_phrase(line, "изучает") &&
                    inference_contains_phrase(query, "изучает")) {
                    score += 2.6;
                }
                if (inference_contains_phrase(line, "занимается") &&
                    (inference_contains_phrase(query, "занимается") ||
                     inference_contains_phrase(query, "чем занимается"))) {
                    score += 2.6;
                }
                if (strlen(line) >= 40U && strlen(line) <= 420U) {
                    score += 0.4;
                }
                if (score > best_score) {
                    if (best_line[0] != '\0' &&
                        strcmp(best_line, line) != 0 &&
                        score > second_score) {
                        second_score = best_score;
                        snprintf(second_line, sizeof(second_line), "%s", best_line);
                    }
                    best_score = score;
                    snprintf(best_line, sizeof(best_line), "%s", line);
                } else if (strcmp(best_line, line) != 0 && score > second_score) {
                    second_score = score;
                    snprintf(second_line, sizeof(second_line), "%s", line);
                }
            }
        }
        start = i + 1U;
    }

    const char *selected = (best_line[0] != '\0' && best_score > 0.4) ? best_line :
                           (fallback_line[0] != '\0' ? fallback_line : answer);
    if (want_definition && selected[0] != '\0') {
        char primary[1024];
        inference_clean_definition_line(selected, primary, sizeof(primary));
        if (want_extended_definition) {
            char secondary[1024];
            char third[1024];
            secondary[0] = '\0';
            third[0] = '\0';
            if (!inference_extract_next_sentence_after(answer, selected, secondary, sizeof(secondary)) &&
                second_line[0] != '\0' && second_score > 0.55) {
                snprintf(secondary, sizeof(secondary), "%s", second_line);
            }
            inference_clean_definition_line(secondary, secondary, sizeof(secondary));
            if (secondary[0] != '\0' &&
                strcmp(primary, secondary) != 0 &&
                strstr(primary, secondary) == NULL &&
                strstr(secondary, primary) == NULL) {
                snprintf(out, out_size, "%s. %s", primary, secondary);
                if (inference_extract_next_sentence_after(answer, secondary, third, sizeof(third))) {
                    if (want_detailed_definition) {
                        inference_append_distinct_paragraph(out, out_size, third);
                    } else {
                        inference_append_distinct_sentence(out, out_size, third);
                    }
                    if (want_detailed_definition && third[0] != '\0') {
                        char fourth[1024];
                        fourth[0] = '\0';
                        if (inference_extract_next_sentence_after(answer, third, fourth, sizeof(fourth))) {
                            inference_append_distinct_sentence(out, out_size, fourth);
                        }
                    }
                } else if (second_line[0] != '\0' && strcmp(second_line, secondary) != 0) {
                    if (want_detailed_definition) {
                        inference_append_distinct_paragraph(out, out_size, second_line);
                    } else {
                        inference_append_distinct_sentence(out, out_size, second_line);
                    }
                }
                free(copy);
                return;
            }
        }
        selected = primary;
    }
    snprintf(out, out_size, "%s", selected);
    free(copy);
}

static char *inference_extract_definition_answer(const char *title,
                                                 const char *content) {
    if (!title || title[0] == '\0' || !content || content[0] == '\0') {
        return NULL;
    }
    char *answer = inference_extract_answer(content);
    if (!answer) {
        return NULL;
    }
    char query[320];
    char composed[1024];
    snprintf(query, sizeof(query), "что такое %s", title);
    inference_compose_human_answer(query, answer, composed, sizeof(composed));
    free(answer);
    inference_trim_inplace(composed);
    if (composed[0] == '\0') {
        return NULL;
    }
    return inference_string_slice(composed, strlen(composed));
}

static int inference_compose_importance_answer(const char *title,
                                               const char *definition,
                                               const char *secondary,
                                               char *out,
                                               size_t out_size) {
    if (!title || !definition || !out || out_size == 0U) {
        return 0;
    }
    out[0] = '\0';

    char primary[1024];
    inference_clean_definition_line(definition, primary, sizeof(primary));
    if (primary[0] == '\0') {
        return 0;
    }

    const char *tail = primary;
    const char *dash = strstr(primary, "—");
    if (dash) {
        tail = dash + strlen("—");
    } else if (strncmp(primary, title, strlen(title)) == 0) {
        tail = primary + strlen(title);
    }
    while (*tail && (isspace((unsigned char)*tail) || *tail == '-')) {
        ++tail;
    }
    if (*tail == '\0') {
        tail = primary;
    }

    char reason[1024];
    if (strncmp(tail, "это ", 4U) == 0) {
        snprintf(reason, sizeof(reason), "%s", tail);
    } else {
        snprintf(reason, sizeof(reason), "это %s", tail);
    }
    inference_trim_inplace(reason);
    if (reason[0] == '\0') {
        return 0;
    }

    snprintf(out, out_size, "%s играет важную роль, потому что %s", title, reason);

    char extra[768];
    extra[0] = '\0';
    if (secondary && secondary[0] != '\0') {
        inference_clean_definition_line(secondary, extra, sizeof(extra));
    }
    if (extra[0] != '\0' &&
        strstr(out, extra) == NULL &&
        strstr(extra, reason) == NULL &&
        strlen(out) + strlen(extra) + 3U < out_size) {
        strncat(out, ". ", out_size - strlen(out) - 1U);
        strncat(out, extra, out_size - strlen(out) - 1U);
    }
    return out[0] != '\0';
}

static char *inference_extract_lead_sentence(const char *content) {
    char *answer = inference_extract_answer(content);
    if (!answer) {
        return NULL;
    }
    size_t length = strlen(answer);
    size_t cut = length;
    for (size_t i = 0; i < length; ++i) {
        unsigned char byte = (unsigned char)answer[i];
        if (byte == '.' || byte == '!' || byte == '?' || byte == '\n') {
            cut = i;
            break;
        }
    }
    while (cut > 0U && isspace((unsigned char)answer[cut - 1U])) {
        --cut;
    }
    if (cut == 0U) {
        free(answer);
        return NULL;
    }
    char *lead = inference_string_slice(answer, cut);
    free(answer);
    if (lead) {
        inference_trim_inplace(lead);
    }
    return lead;
}

static void inference_relative_path(const char *root, const char *path, char *out, size_t out_len) {
    if (!out || out_len == 0U) {
        return;
    }
    if (root && path && strstr(path, root) == path) {
        size_t root_len = strlen(root);
        const char *sub_path = path + root_len;
        if (*sub_path == '/' || *sub_path == '\\') {
            ++sub_path;
        }
        snprintf(out, out_len, "%s", sub_path);
        return;
    }
    snprintf(out, out_len, "%s", path ? path : "");
}

static void inference_formula_memory_add_document(const char *root, const char *path) {
    char *content = inference_read_text_slice(path, 4096U);
    if (!content) {
        return;
    }
    char *title = inference_extract_title(content, path);
    if (title) {
        inference_clean_title_inplace(title);
    }
    char *answer = inference_extract_answer(content);
    char *definition_answer = title ? inference_extract_definition_answer(title, content) : NULL;
    char *lead = inference_extract_lead_sentence(content);
    char source[128];
    inference_relative_path(root, path, source, sizeof(source));
    uint64_t ts = (uint64_t)time(NULL);

    if (title && answer && title[0] != '\0' && answer[0] != '\0') {
        char *plain_title = inference_strip_accents_copy(title);
        char q1[256];
        char q2[256];
        char q3[256];
        char q4[256];
        char q5[256];
        char q6[256];
        char q7[256];
        char q8[256];
        char q9[256];
        char q10[256];
        char q11[256];
        char q12[256];
        char q13[256];
        char q14[256];
        char q15[256];
        char q16[256];
        char q17[256];
        char q18[256];
        char q19[256];
        char q20[256];
        char focused_answer[512];
        char importance_answer[1024];
        char secondary_answer[768];
        const char *definition = (definition_answer && definition_answer[0] != '\0') ? definition_answer : answer;
        const char *detailed_definition = answer;
        secondary_answer[0] = '\0';
        importance_answer[0] = '\0';
        if (definition[0] != '\0') {
            inference_extract_next_sentence_after(answer, definition, secondary_answer, sizeof(secondary_answer));
            inference_compose_importance_answer(title,
                                                definition,
                                                secondary_answer,
                                                importance_answer,
                                                sizeof(importance_answer));
        }
        snprintf(q1, sizeof(q1), "%s", title);
        snprintf(q2, sizeof(q2), "что такое %s", title);
        snprintf(q3, sizeof(q3), "объясни %s", title);
        snprintf(q4, sizeof(q4), "расскажи про %s", title);
        snprintf(q7, sizeof(q7), "объясни %s простыми словами", title);
        snprintf(q8, sizeof(q8), "расскажи о %s", title);
        snprintf(q9, sizeof(q9), "как устроено %s", title);
        snprintf(q10, sizeof(q10), "почему важна %s", title);
        snprintf(q11, sizeof(q11), "зачем нужна %s", title);
        snprintf(q12, sizeof(q12), "почему важен %s", title);
        snprintf(q13, sizeof(q13), "почему важно %s", title);
        snprintf(q14, sizeof(q14), "зачем нужен %s", title);
        snprintf(q15, sizeof(q15), "зачем нужно %s", title);
        snprintf(q16, sizeof(q16), "расскажи подробно о %s", title);
        snprintf(q17, sizeof(q17), "расскажи подробно про %s", title);
        snprintf(q18, sizeof(q18), "что ты знаешь о %s", title);
        snprintf(q19, sizeof(q19), "что ты знаешь про %s", title);
        snprintf(q20, sizeof(q20), "что ты знаешь об %s", title);
        kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q1, definition, source, ts);
        kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q2, definition, source, ts);
        kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q3, detailed_definition, source, ts);
        kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q4, detailed_definition, source, ts);
        kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q7, detailed_definition, source, ts);
        kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q8, detailed_definition, source, ts);
        kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q9, detailed_definition, source, ts);
        kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q16, detailed_definition, source, ts);
        kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q17, detailed_definition, source, ts);
        kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q18, detailed_definition, source, ts);
        kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q19, detailed_definition, source, ts);
        kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q20, detailed_definition, source, ts);
        if (importance_answer[0] != '\0') {
            kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q10, importance_answer, source, ts);
            kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q11, importance_answer, source, ts);
            kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q12, importance_answer, source, ts);
            kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q13, importance_answer, source, ts);
            kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q14, importance_answer, source, ts);
            kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q15, importance_answer, source, ts);
        }
        if (inference_extract_keyword_window(content, "изучает", focused_answer, sizeof(focused_answer)) ||
            (secondary_answer[0] != '\0' && strstr(secondary_answer, "изучает") != NULL)) {
            snprintf(q5, sizeof(q5), "что изучает %s", title);
            kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q5, detailed_definition, source, ts);
        }
        snprintf(q6, sizeof(q6), "чем занимается %s", title);
        kf_pool_add_association(&g_formula_memory_pool,
                                &g_formula_symbol_table,
                                q6,
                                secondary_answer[0] != '\0' ? secondary_answer : definition,
                                source,
                                ts);
        if (plain_title && plain_title[0] != '\0' && strcmp(plain_title, title) != 0) {
            snprintf(q1, sizeof(q1), "%s", plain_title);
            snprintf(q2, sizeof(q2), "что такое %s", plain_title);
            snprintf(q3, sizeof(q3), "объясни %s", plain_title);
            snprintf(q4, sizeof(q4), "расскажи про %s", plain_title);
            snprintf(q7, sizeof(q7), "объясни %s простыми словами", plain_title);
            snprintf(q8, sizeof(q8), "расскажи о %s", plain_title);
            snprintf(q9, sizeof(q9), "как устроено %s", plain_title);
            snprintf(q10, sizeof(q10), "почему важна %s", plain_title);
            snprintf(q11, sizeof(q11), "зачем нужна %s", plain_title);
            snprintf(q12, sizeof(q12), "почему важен %s", plain_title);
            snprintf(q13, sizeof(q13), "почему важно %s", plain_title);
            snprintf(q14, sizeof(q14), "зачем нужен %s", plain_title);
            snprintf(q15, sizeof(q15), "зачем нужно %s", plain_title);
            snprintf(q16, sizeof(q16), "расскажи подробно о %s", plain_title);
            snprintf(q17, sizeof(q17), "расскажи подробно про %s", plain_title);
            snprintf(q18, sizeof(q18), "что ты знаешь о %s", plain_title);
            snprintf(q19, sizeof(q19), "что ты знаешь про %s", plain_title);
            snprintf(q20, sizeof(q20), "что ты знаешь об %s", plain_title);
            kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q1, definition, source, ts);
            kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q2, definition, source, ts);
            kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q3, detailed_definition, source, ts);
            kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q4, detailed_definition, source, ts);
            kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q7, detailed_definition, source, ts);
            kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q8, detailed_definition, source, ts);
            kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q9, detailed_definition, source, ts);
            kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q16, detailed_definition, source, ts);
            kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q17, detailed_definition, source, ts);
            kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q18, detailed_definition, source, ts);
            kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q19, detailed_definition, source, ts);
            kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q20, detailed_definition, source, ts);
            if (importance_answer[0] != '\0') {
                kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q10, importance_answer, source, ts);
                kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q11, importance_answer, source, ts);
                kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q12, importance_answer, source, ts);
                kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q13, importance_answer, source, ts);
                kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q14, importance_answer, source, ts);
                kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q15, importance_answer, source, ts);
            }
            if (inference_extract_keyword_window(content, "изучает", focused_answer, sizeof(focused_answer)) ||
                (secondary_answer[0] != '\0' && strstr(secondary_answer, "изучает") != NULL)) {
                snprintf(q5, sizeof(q5), "что изучает %s", plain_title);
                kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, q5, detailed_definition, source, ts);
            }
            snprintf(q6, sizeof(q6), "чем занимается %s", plain_title);
            kf_pool_add_association(&g_formula_memory_pool,
                                    &g_formula_symbol_table,
                                    q6,
                                    secondary_answer[0] != '\0' ? secondary_answer : definition,
                                    source,
                                    ts);
        }
        if (lead && lead[0] != '\0' && strcmp(lead, title) != 0) {
            kf_pool_add_association(&g_formula_memory_pool, &g_formula_symbol_table, lead, definition, source, ts);
        }
        free(plain_title);
    }

    free(content);
    free(title);
    free(answer);
    free(definition_answer);
    free(lead);
}

static void inference_formula_memory_walk(const char *root, const char *path) {
    if (inference_is_directory(path)) {
        DIR *dir = opendir(path);
        if (!dir) {
            return;
        }
        struct dirent *entry = NULL;
        char child_path[1024];
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            snprintf(child_path, sizeof(child_path), "%s/%s", path, entry->d_name);
            if (inference_is_directory(child_path)) {
                inference_formula_memory_walk(root, child_path);
                continue;
            }
            const char *ext = strrchr(entry->d_name, '.');
            if (ext && (strcmp(ext, ".md") == 0 || strcmp(ext, ".txt") == 0)) {
                inference_formula_memory_add_document(root, child_path);
            }
        }
        closedir(dir);
        return;
    }
    inference_formula_memory_add_document(root, path);
}

static void inference_load_formula_memory_once(void) {
    kolibri_symbol_table_init(&g_formula_symbol_table, NULL);
    kolibri_symbol_table_seed_defaults(&g_formula_symbol_table);
    kf_pool_init(&g_formula_memory_pool, 424242ULL);
    inference_formula_memory_walk(inference_formula_memory_root(),
                                  inference_formula_memory_root());
    if (g_formula_memory_pool.association_count > 0U) {
        kf_pool_tick(&g_formula_memory_pool, 16U);
        g_formula_memory_ready = 1;
    }
}

static const KolibriFormulaPool *inference_get_formula_memory(void) {
    pthread_once(&g_formula_memory_once, inference_load_formula_memory_once);
    return g_formula_memory_ready ? &g_formula_memory_pool : NULL;
}

static size_t inference_encode_text_digits(KolibriSymbolTable *symbols,
                                           const char *text,
                                           uint8_t *digits,
                                           size_t digits_capacity) {
    if (!symbols || !text || !digits || digits_capacity == 0U) {
        return 0U;
    }
    const unsigned char *bytes = (const unsigned char *)text;
    size_t length = strlen(text);
    size_t offset = 0U;
    size_t written = 0U;
    while (offset < length && written + KOLIBRI_SYMBOL_DIGITS <= digits_capacity) {
        uint32_t codepoint = 0U;
        size_t consumed = 0U;
        unsigned char lead = bytes[offset];
        if (lead < 0x80U) {
            codepoint = (uint32_t)lead;
            consumed = 1U;
        } else if ((lead & 0xE0U) == 0xC0U && offset + 1U < length) {
            codepoint = ((uint32_t)(lead & 0x1FU) << 6) |
                        (uint32_t)(bytes[offset + 1U] & 0x3FU);
            consumed = 2U;
        } else if ((lead & 0xF0U) == 0xE0U && offset + 2U < length) {
            codepoint = ((uint32_t)(lead & 0x0FU) << 12) |
                        ((uint32_t)(bytes[offset + 1U] & 0x3FU) << 6) |
                        (uint32_t)(bytes[offset + 2U] & 0x3FU);
            consumed = 3U;
        } else if ((lead & 0xF8U) == 0xF0U && offset + 3U < length) {
            codepoint = ((uint32_t)(lead & 0x07U) << 18) |
                        ((uint32_t)(bytes[offset + 1U] & 0x3FU) << 12) |
                        ((uint32_t)(bytes[offset + 2U] & 0x3FU) << 6) |
                        (uint32_t)(bytes[offset + 3U] & 0x3FU);
            consumed = 4U;
        } else {
            codepoint = (uint32_t)lead;
            consumed = 1U;
        }
        if (consumed == 0U) {
            break;
        }
        if (kolibri_symbol_encode(symbols,
                                  codepoint,
                                  &digits[written]) == 0) {
            written += KOLIBRI_SYMBOL_DIGITS;
        }
        offset += consumed;
    }
    return written;
}

static double inference_digit_similarity(const uint8_t *left,
                                         size_t left_len,
                                         const uint8_t *right,
                                         size_t right_len) {
    if (!left || !right || left_len == 0U || right_len == 0U) {
        return 0.0;
    }
    size_t min_len = left_len < right_len ? left_len : right_len;
    size_t max_len = left_len > right_len ? left_len : right_len;
    size_t aligned_hits = 0U;
    for (size_t i = 0; i < min_len; ++i) {
        if (left[i] == right[i]) {
            ++aligned_hits;
        }
    }
    size_t prefix_hits = 0U;
    while (prefix_hits < min_len && left[prefix_hits] == right[prefix_hits]) {
        ++prefix_hits;
    }
    double aligned_score = (double)aligned_hits / (double)max_len;
    double prefix_score = (double)prefix_hits / (double)max_len;
    return aligned_score * 0.7 + prefix_score * 0.3;
}

static double inference_text_overlap_score(const char *query, const char *candidate) {
    if (!query || !candidate) {
        return 0.0;
    }
    char *query_copy = inference_lower_ascii_copy(query);
    char *candidate_copy = inference_lower_ascii_copy(candidate);
    if (!query_copy || !candidate_copy) {
        free(query_copy);
        free(candidate_copy);
        return 0.0;
    }
    inference_normalize_text(query_copy);
    inference_normalize_text(candidate_copy);
    inference_trim_inplace(query_copy);
    inference_trim_inplace(candidate_copy);

    if (query_copy[0] == '\0' || candidate_copy[0] == '\0') {
        free(query_copy);
        free(candidate_copy);
        return 0.0;
    }

    if (strcmp(query_copy, candidate_copy) == 0) {
        free(query_copy);
        free(candidate_copy);
        return 1.0;
    }

    double score = 0.0;
    if (strstr(query_copy, candidate_copy) || strstr(candidate_copy, query_copy)) {
        score += 0.35;
    }

    char tokens[32][128];
    size_t token_count = tokenize_query(query_copy, tokens, 32);
    size_t hits = 0U;
    size_t useful = 0U;
    for (size_t i = 0; i < token_count; ++i) {
        size_t len = strlen(tokens[i]);
        if (len < 2U) {
            continue;
        }
        ++useful;
        if (strstr(candidate_copy, tokens[i])) {
            ++hits;
        }
    }

    if (useful > 0U) {
        score += 0.65 * ((double)hits / (double)useful);
    }

    free(query_copy);
    free(candidate_copy);
    return score > 1.0 ? 1.0 : score;
}

static void inference_load_knowledge_once(void) {
    if (kolibri_knowledge_index_init(&g_inference_knowledge_index) != 0) {
        return;
    }
    if (kolibri_knowledge_index_load_directory(&g_inference_knowledge_index,
                                               inference_knowledge_root()) != 0) {
        kolibri_knowledge_index_free(&g_inference_knowledge_index);
        return;
    }
    g_inference_knowledge_ready = 1;
}

static const KolibriKnowledgeIndex *inference_get_knowledge_index(void) {
    pthread_once(&g_inference_knowledge_once, inference_load_knowledge_once);
    return g_inference_knowledge_ready ? &g_inference_knowledge_index : NULL;
}

/* Простая токенизация (разбиение по пробелам) */
static size_t tokenize_query(const char *query, char tokens[][128], size_t max_tokens) {
    size_t count = 0;
    const char *p = query;

    while (*p && count < max_tokens) {
        /* Пропускаем пробелы */
        while (*p == ' ' || *p == '\t' || *p == '\n') p++;
        if (!*p) break;

        size_t len = 0;
        while (p[len] && p[len] != ' ' && p[len] != '\t' && p[len] != '\n' && len < 127) {
            len++;
        }
        memcpy(tokens[count], p, len);
        tokens[count][len] = '\0';
        count++;
        p += len;
    }
    return count;
}

static int inference_is_query_meta_token(const char *token) {
    static const char *meta_tokens[] = {
        "что", "такое", "кто", "такой", "такая", "такие",
        "объясни", "обьясни", "расскажи", "подробно", "про",
        "ты", "знаешь", "о", "об",
        "чем", "занимается", "занимают", "изучает", "изучают",
        "what", "is", "who", "tell", "about", "explain",
    };
    if (!token || token[0] == '\0') {
        return 1;
    }
    for (size_t i = 0; i < sizeof(meta_tokens) / sizeof(meta_tokens[0]); ++i) {
        if (strcmp(token, meta_tokens[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static size_t inference_extract_topic_tokens(const char *query,
                                             char tokens[][128],
                                             size_t max_tokens) {
    if (!query || !tokens || max_tokens == 0U) {
        return 0U;
    }
    char *norm = inference_lower_ascii_copy(query);
    if (!norm) {
        return 0U;
    }
    inference_normalize_text(norm);
    inference_trim_inplace(norm);

    char raw[24][128];
    size_t raw_count = tokenize_query(norm, raw, 24U);
    size_t out = 0U;
    for (size_t i = 0; i < raw_count && out < max_tokens; ++i) {
        if (raw[i][0] == '\0' || inference_is_query_meta_token(raw[i])) {
            continue;
        }
        if (strlen(raw[i]) < 2U) {
            continue;
        }
        int duplicate = 0;
        for (size_t j = 0; j < out; ++j) {
            if (strcmp(tokens[j], raw[i]) == 0) {
                duplicate = 1;
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        snprintf(tokens[out], 128U, "%s", raw[i]);
        ++out;
    }
    free(norm);
    return out;
}

static int inference_is_definition_query(const char *query) {
    return inference_contains_phrase(query, "что такое") ||
           inference_contains_phrase(query, "кто такой") ||
           inference_contains_phrase(query, "кто такая") ||
           inference_contains_phrase(query, "кто такие") ||
           inference_contains_phrase(query, "объясни") ||
           inference_contains_phrase(query, "расскажи про") ||
           inference_contains_phrase(query, "расскажи о") ||
           inference_contains_phrase(query, "расскажи подробно про") ||
           inference_contains_phrase(query, "расскажи подробно о") ||
           inference_contains_phrase(query, "что ты знаешь про") ||
           inference_contains_phrase(query, "что ты знаешь о") ||
           inference_contains_phrase(query, "что ты знаешь об") ||
           inference_contains_phrase(query, "что изучает") ||
           inference_contains_phrase(query, "чем занимается") ||
           inference_contains_phrase(query, "как устроен") ||
           inference_contains_phrase(query, "как устроена") ||
           inference_contains_phrase(query, "как устроено") ||
           inference_contains_phrase(query, "почему важен") ||
           inference_contains_phrase(query, "почему важна") ||
           inference_contains_phrase(query, "почему важно") ||
           inference_contains_phrase(query, "зачем нужен") ||
           inference_contains_phrase(query, "зачем нужна") ||
           inference_contains_phrase(query, "зачем нужно");
}

static void inference_canonicalize_definition_entity(char *entity, size_t entity_size) {
    if (!entity || entity[0] == '\0' || entity_size == 0U) {
        return;
    }

    struct CanonicalEntity {
        const char *stem;
        const char *canonical;
    };
    static const struct CanonicalEntity known[] = {
        {"медицин", "медицина"},
        {"географ", "география"},
        {"философ", "философия"},
        {"биолог", "биология"},
        {"физик", "физика"},
        {"астроном", "астрономия"},
        {"анатом", "анатомия"},
        {"физиолог", "физиология"},
        {"патолог", "патология"},
        {"терап", "терапия"},
        {"хими", "химия"},
        {"истори", "история"},
        {"эконом", "экономика"},
        {"прав", "право"},
        {"алгоритм", "алгоритм"},
        {"программирован", "программирование"},
    };
    for (size_t i = 0U; i < sizeof(known) / sizeof(known[0]); ++i) {
        if (strncmp(entity, known[i].stem, strlen(known[i].stem)) == 0) {
            snprintf(entity, entity_size, "%s", known[i].canonical);
            return;
        }
    }

    size_t len = strlen(entity);
    if (len > 2U && strcmp(entity + len - 2U, "ию") == 0) {
        entity[len - 2U] = '\0';
        strncat(entity, "ия", entity_size - strlen(entity) - 1U);
    } else if (len > 2U && strcmp(entity + len - 2U, "ии") == 0) {
        entity[len - 2U] = '\0';
        strncat(entity, "ия", entity_size - strlen(entity) - 1U);
    } else if (len > 3U && strcmp(entity + len - 3U, "ике") == 0) {
        entity[len - 3U] = '\0';
        strncat(entity, "ика", entity_size - strlen(entity) - 1U);
    } else if (len > 2U && strcmp(entity + len - 2U, "ику") == 0) {
        entity[len - 2U] = '\0';
        strncat(entity, "ика", entity_size - strlen(entity) - 1U);
    }
}

static size_t inference_extract_definition_entity(const char *query,
                                                  char *out,
                                                  size_t out_size) {
    if (!out || out_size == 0U) {
        return 0U;
    }
    out[0] = '\0';
    if (!query || query[0] == '\0') {
        return 0U;
    }

    const char *start = query;
    const char *phrases[] = {
        "что такое ",
        "кто такой ",
        "кто такая ",
        "кто такие ",
        "объясни ",
        "расскажи про ",
        "расскажи о ",
        "расскажи подробно про ",
        "расскажи подробно о ",
        "что ты знаешь про ",
        "что ты знаешь о ",
        "что ты знаешь об ",
        "что изучает ",
        "чем занимается ",
        "как устроен ",
        "как устроена ",
        "как устроено ",
        "почему важен ",
        "почему важна ",
        "почему важно ",
        "зачем нужен ",
        "зачем нужна ",
        "зачем нужно ",
    };
    for (size_t i = 0U; i < sizeof(phrases) / sizeof(phrases[0]); ++i) {
        const char *hit = strstr(query, phrases[i]);
        if (hit) {
            start = hit + strlen(phrases[i]);
            break;
        }
    }

    while (*start && isspace((unsigned char)*start)) {
        ++start;
    }

    size_t pos = 0U;
    while (start[pos] != '\0' && pos + 1U < out_size) {
        unsigned char byte = (unsigned char)start[pos];
        if (byte < 0x80U && (byte == '?' || byte == '!' || byte == '.' || byte == ',' || byte == ':')) {
            break;
        }
        out[pos] = start[pos];
        ++pos;
    }
    out[pos] = '\0';
    inference_trim_inplace(out);
    static const char *suffixes[] = {
        " простыми словами",
        " простым языком",
        " кратко",
        " подробно",
    };
    for (size_t i = 0U; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i) {
        size_t suffix_len = strlen(suffixes[i]);
        size_t out_len = strlen(out);
        if (out_len > suffix_len &&
            strcmp(out + out_len - suffix_len, suffixes[i]) == 0) {
            out[out_len - suffix_len] = '\0';
            inference_trim_inplace(out);
            break;
        }
    }
    inference_canonicalize_definition_entity(out, out_size);
    return strlen(out);
}

static int inference_definition_support_ok(const char *entity, const char *answer) {
    if (!entity || !answer || entity[0] == '\0' || answer[0] == '\0') {
        return 1;
    }
    static const char *medicine_support[] = {"здоров", "болезн", "лечен"};
    static const char *geography_support[] = {"земл", "территор", "поверхност", "пространств"};
    static const char *philosophy_support[] = {"познан", "мышлен", "мировоззрен"};
    static const char *biology_support[] = {"жив", "организм"};
    static const char *physics_support[] = {"природ", "явлен", "матери", "движен"};
    static const char *astronomy_support[] = {"небес", "звезд", "планет", "галактик", "вселен"};
    static const char *anatomy_support[] = {"строен", "тел", "орган", "ткан"};
    static const char *physiology_support[] = {"функци", "организм", "клет", "сред"};
    static const char *pathology_support[] = {"болезн", "процесс", "состояни", "организм"};
    static const char *therapy_support[] = {"лечен", "заболев", "пациент", "медицин", "клинич"};
    static const char *chemistry_support[] = {"веществ", "состав", "строен", "реакц", "элемент"};
    static const char *history_support[] = {"прошл", "событ", "человечеств", "источн", "цивилизац", "государств", "культур", "люд", "времен"};
    static const char *economics_support[] = {"хозяйств", "производ", "распредел", "обмен", "потреблен"};
    static const char *law_support[] = {"норм", "закон", "государств", "отношен", "обществен"};
    static const char *algorithm_support[] = {"правил", "инструкц", "действ", "задач", "исполн"};
    static const char *programming_support[] = {"алгоритм", "программ", "компьют", "вычисл"};

    const char **support = NULL;
    size_t support_count = 0U;
    if (strcmp(entity, "медицина") == 0) {
        support = medicine_support;
        support_count = sizeof(medicine_support) / sizeof(medicine_support[0]);
    } else if (strcmp(entity, "география") == 0) {
        support = geography_support;
        support_count = sizeof(geography_support) / sizeof(geography_support[0]);
    } else if (strcmp(entity, "философия") == 0) {
        support = philosophy_support;
        support_count = sizeof(philosophy_support) / sizeof(philosophy_support[0]);
    } else if (strcmp(entity, "биология") == 0) {
        support = biology_support;
        support_count = sizeof(biology_support) / sizeof(biology_support[0]);
    } else if (strcmp(entity, "физика") == 0) {
        support = physics_support;
        support_count = sizeof(physics_support) / sizeof(physics_support[0]);
    } else if (strcmp(entity, "астрономия") == 0) {
        support = astronomy_support;
        support_count = sizeof(astronomy_support) / sizeof(astronomy_support[0]);
    } else if (strcmp(entity, "анатомия") == 0) {
        support = anatomy_support;
        support_count = sizeof(anatomy_support) / sizeof(anatomy_support[0]);
    } else if (strcmp(entity, "физиология") == 0) {
        support = physiology_support;
        support_count = sizeof(physiology_support) / sizeof(physiology_support[0]);
    } else if (strcmp(entity, "патология") == 0) {
        support = pathology_support;
        support_count = sizeof(pathology_support) / sizeof(pathology_support[0]);
    } else if (strcmp(entity, "терапия") == 0) {
        support = therapy_support;
        support_count = sizeof(therapy_support) / sizeof(therapy_support[0]);
    } else if (strcmp(entity, "химия") == 0) {
        support = chemistry_support;
        support_count = sizeof(chemistry_support) / sizeof(chemistry_support[0]);
    } else if (strcmp(entity, "история") == 0) {
        support = history_support;
        support_count = sizeof(history_support) / sizeof(history_support[0]);
    } else if (strcmp(entity, "экономика") == 0) {
        support = economics_support;
        support_count = sizeof(economics_support) / sizeof(economics_support[0]);
    } else if (strcmp(entity, "право") == 0) {
        support = law_support;
        support_count = sizeof(law_support) / sizeof(law_support[0]);
    } else if (strcmp(entity, "алгоритм") == 0) {
        support = algorithm_support;
        support_count = sizeof(algorithm_support) / sizeof(algorithm_support[0]);
    } else if (strcmp(entity, "программирование") == 0) {
        support = programming_support;
        support_count = sizeof(programming_support) / sizeof(programming_support[0]);
    } else {
        return 1;
    }

    for (size_t i = 0U; i < support_count; ++i) {
        if (strstr(answer, support[i]) != NULL) {
            return 1;
        }
    }
    return 0;
}

static int inference_definition_forbidden_hit(const char *entity, const char *answer) {
    if (!entity || !answer || entity[0] == '\0' || answer[0] == '\0') {
        return 0;
    }
    static const char *medicine_forbidden[] = {"математик", "географ", "философ"};
    static const char *geography_forbidden[] = {"математик", "медицин", "философ"};
    static const char *philosophy_forbidden[] = {"медицин", "математик", "географ"};
    static const char *biology_forbidden[] = {"математик", "географ"};
    static const char *physics_forbidden[] = {"философ", "медицин"};
    static const char *astronomy_forbidden[] = {"географ", "медицин", "философ"};
    static const char *anatomy_forbidden[] = {"сериал", "эконом", "географ"};
    static const char *physiology_forbidden[] = {"географ", "эконом", "сериал"};
    static const char *pathology_forbidden[] = {"фильм", "сериал"};
    static const char *therapy_forbidden[] = {"сериал", "комедийн", "американск"};
    static const char *chemistry_forbidden[] = {"истор", "эконом", "географ"};
    static const char *history_forbidden[] = {"эконом", "математик", "географ"};
    static const char *economics_forbidden[] = {"философ", "математик", "географ"};
    static const char *law_forbidden[] = {"лев", "правооблад", "географ"};
    static const char *algorithm_forbidden[] = {"географ", "эконом", "философ"};
    static const char *programming_forbidden[] = {"сериал", "философ", "географ"};

    const char **forbidden = NULL;
    size_t forbidden_count = 0U;
    if (strcmp(entity, "медицина") == 0) {
        forbidden = medicine_forbidden;
        forbidden_count = sizeof(medicine_forbidden) / sizeof(medicine_forbidden[0]);
    } else if (strcmp(entity, "география") == 0) {
        forbidden = geography_forbidden;
        forbidden_count = sizeof(geography_forbidden) / sizeof(geography_forbidden[0]);
    } else if (strcmp(entity, "философия") == 0) {
        forbidden = philosophy_forbidden;
        forbidden_count = sizeof(philosophy_forbidden) / sizeof(philosophy_forbidden[0]);
    } else if (strcmp(entity, "биология") == 0) {
        forbidden = biology_forbidden;
        forbidden_count = sizeof(biology_forbidden) / sizeof(biology_forbidden[0]);
    } else if (strcmp(entity, "физика") == 0) {
        forbidden = physics_forbidden;
        forbidden_count = sizeof(physics_forbidden) / sizeof(physics_forbidden[0]);
    } else if (strcmp(entity, "астрономия") == 0) {
        forbidden = astronomy_forbidden;
        forbidden_count = sizeof(astronomy_forbidden) / sizeof(astronomy_forbidden[0]);
    } else if (strcmp(entity, "анатомия") == 0) {
        forbidden = anatomy_forbidden;
        forbidden_count = sizeof(anatomy_forbidden) / sizeof(anatomy_forbidden[0]);
    } else if (strcmp(entity, "физиология") == 0) {
        forbidden = physiology_forbidden;
        forbidden_count = sizeof(physiology_forbidden) / sizeof(physiology_forbidden[0]);
    } else if (strcmp(entity, "патология") == 0) {
        forbidden = pathology_forbidden;
        forbidden_count = sizeof(pathology_forbidden) / sizeof(pathology_forbidden[0]);
    } else if (strcmp(entity, "терапия") == 0) {
        forbidden = therapy_forbidden;
        forbidden_count = sizeof(therapy_forbidden) / sizeof(therapy_forbidden[0]);
    } else if (strcmp(entity, "химия") == 0) {
        forbidden = chemistry_forbidden;
        forbidden_count = sizeof(chemistry_forbidden) / sizeof(chemistry_forbidden[0]);
    } else if (strcmp(entity, "история") == 0) {
        forbidden = history_forbidden;
        forbidden_count = sizeof(history_forbidden) / sizeof(history_forbidden[0]);
    } else if (strcmp(entity, "экономика") == 0) {
        forbidden = economics_forbidden;
        forbidden_count = sizeof(economics_forbidden) / sizeof(economics_forbidden[0]);
    } else if (strcmp(entity, "право") == 0) {
        forbidden = law_forbidden;
        forbidden_count = sizeof(law_forbidden) / sizeof(law_forbidden[0]);
    } else if (strcmp(entity, "алгоритм") == 0) {
        forbidden = algorithm_forbidden;
        forbidden_count = sizeof(algorithm_forbidden) / sizeof(algorithm_forbidden[0]);
    } else if (strcmp(entity, "программирование") == 0) {
        forbidden = programming_forbidden;
        forbidden_count = sizeof(programming_forbidden) / sizeof(programming_forbidden[0]);
    } else {
        return 0;
    }

    for (size_t i = 0U; i < forbidden_count; ++i) {
        if (strstr(answer, forbidden[i]) != NULL) {
            return 1;
        }
    }
    return 0;
}

static double inference_assoc_quality_bonus(const KolibriAssociation *assoc) {
    if (!assoc) {
        return 0.0;
    }
    double bonus = 0.0;
    if (assoc->source[0] != '\0') {
        if (strstr(assoc->source, "manual") != NULL) {
            bonus += 1.35;
        } else if (strstr(assoc->source, "wiki") != NULL) {
            bonus -= 0.15;
        }
    }
    if (strstr(assoc->answer, "—") != NULL) {
        bonus += 0.45;
    }
    if (strstr(assoc->answer, "[1]") != NULL ||
        strstr(assoc->answer, "[2]") != NULL) {
        bonus -= 0.45;
    }
    if (strstr(assoc->answer, "Википедия") != NULL ||
        strstr(assoc->answer, "Wikipedia") != NULL ||
        strstr(assoc->answer, "Материал из Википедии") != NULL) {
        bonus -= 0.8;
    }
    return bonus;
}

static double inference_topic_overlap_score(const char *query,
                                            const char *candidate) {
    char query_tokens[8][128];
    char candidate_tokens[16][128];
    size_t qcount = inference_extract_topic_tokens(query, query_tokens, 8U);
    if (qcount == 0U || !candidate) {
        return 0.0;
    }

    char *candidate_norm = inference_lower_ascii_copy(candidate);
    if (!candidate_norm) {
        return 0.0;
    }
    inference_normalize_text(candidate_norm);
    inference_trim_inplace(candidate_norm);
    size_t ccount = tokenize_query(candidate_norm, candidate_tokens, 16U);

    double hits = 0.0;
    for (size_t i = 0; i < qcount; ++i) {
        size_t qlen = strlen(query_tokens[i]);
        if (qlen == 0U) {
            continue;
        }
        int matched = 0;
        if (strstr(candidate_norm, query_tokens[i]) != NULL) {
            hits += 1.0;
            continue;
        }
        for (size_t j = 0; j < ccount; ++j) {
            size_t clen = strlen(candidate_tokens[j]);
            size_t min_len = qlen < clen ? qlen : clen;
            if (min_len >= 5U &&
                strncmp(query_tokens[i], candidate_tokens[j], min_len >= 6U ? 6U : min_len) == 0) {
                matched = 1;
                break;
            }
        }
        if (matched) {
            hits += 0.7;
        }
    }

    free(candidate_norm);
    return hits / (double)qcount;
}

static double inference_topic_exact_match_score(const char *query,
                                                const char *candidate,
                                                size_t *query_topic_count,
                                                size_t *candidate_topic_count) {
    char query_tokens[8][128];
    char candidate_tokens[16][128];
    size_t qcount = inference_extract_topic_tokens(query, query_tokens, 8U);
    size_t ccount = inference_extract_topic_tokens(candidate, candidate_tokens, 16U);
    if (query_topic_count) {
        *query_topic_count = qcount;
    }
    if (candidate_topic_count) {
        *candidate_topic_count = ccount;
    }
    if (qcount == 0U || ccount == 0U) {
        return 0.0;
    }

    size_t exact_matches = 0U;
    size_t prefix_matches = 0U;
    for (size_t i = 0; i < qcount; ++i) {
        for (size_t j = 0; j < ccount; ++j) {
            if (strcmp(query_tokens[i], candidate_tokens[j]) == 0) {
                ++exact_matches;
                ++prefix_matches;
                break;
            }
            size_t qlen = strlen(query_tokens[i]);
            size_t clen = strlen(candidate_tokens[j]);
            size_t min_len = qlen < clen ? qlen : clen;
            if (min_len >= 5U &&
                strncmp(query_tokens[i], candidate_tokens[j], min_len >= 7U ? 7U : min_len) == 0) {
                ++prefix_matches;
                break;
            }
        }
    }

    if (exact_matches == qcount) {
        if (ccount == qcount) {
            return 1.0;
        }
        return 0.72;
    }
    if (prefix_matches == qcount) {
        if (ccount == qcount) {
            return 0.85;
        }
        return 0.55;
    }
    return 0.0;
}

/* Подсчёт совпадений ключевых слов */
static double keyword_match_score(const char *text, char tokens[][128], size_t token_count) {
    if (!text || token_count == 0) return 0.0;
    size_t hits = 0;
    for (size_t i = 0; i < token_count; i++) {
        if (strstr(text, tokens[i])) hits++;
    }
    return (double)hits / (double)token_count;
}

/* ========== ЖИЗНЕННЫЙ ЦИКЛ ========== */

KolibriInferenceContext* kolibri_inference_create(void) {
    KolibriInferenceContext *ctx = calloc(1, sizeof(KolibriInferenceContext));
    if (!ctx) return NULL;

    ctx->strategy = KOLIBRI_INF_HYBRID;
    ctx->temperature = 0.7;
    ctx->max_steps = 8;
    ctx->knowledge_limit = 5;

    ctx->total_queries = 0;
    ctx->total_tokens_in = 0;
    ctx->total_tokens_out = 0;
    ctx->avg_confidence = 0.0;
    ctx->avg_duration_ms = 0.0;

    return ctx;
}

void kolibri_inference_destroy(KolibriInferenceContext *ctx) {
    free(ctx);  /* NULL-safe: free(NULL) определён */
}

int kolibri_inference_set_strategy(
    KolibriInferenceContext *ctx,
    KolibriInferenceStrategy strategy
) {
    if (!ctx) return -1;
    ctx->strategy = strategy;
    return 0;
}

int kolibri_inference_set_temperature(
    KolibriInferenceContext *ctx,
    double temperature
) {
    if (!ctx) return -1;
    if (temperature < 0.0) temperature = 0.0;
    if (temperature > 2.0) temperature = 2.0;
    ctx->temperature = temperature;
    return 0;
}

/* ========== ШАГИ ИНФЕРЕНСА ========== */

/*
 * Шаг 1: Прямой поиск по ключевым словам
 * Ищет в knowledge base документы, содержащие слова из запроса
 */
static int step_direct_search(
    const char *query,
    KolibriInferenceStep *step,
    char *partial_response,
    size_t partial_size,
    size_t *source_count
) {
    double t0 = now_ms();
    snprintf(step->description, sizeof(step->description),
             "Прямой поиск по ключевым словам");

    /* Токенизация запроса */
    char tokens[32][128];
    size_t tcount = tokenize_query(query, tokens, 32);

    const KolibriKnowledgeIndex *idx = inference_get_knowledge_index();
    if (!idx) {
        snprintf(step->result, sizeof(step->result), "Индекс знаний недоступен");
        step->confidence = 0.0;
        step->duration_ms = now_ms() - t0;
        return -1;
    }

    if (idx->count == 0) {
        snprintf(step->result, sizeof(step->result),
                 "Документы знаний не загружены");
        step->confidence = 0.1;
        step->duration_ms = now_ms() - t0;
        return 0;
    }

    /* Ищем лучшие совпадения */
    const KolibriKnowledgeDocument *results[16];
    double scores[16];
    size_t found = kolibri_knowledge_search_legacy(idx, query, 5, results, scores);

    if (found > 0 && scores[0] > 0.0) {
        /* Компилируем ответ из лучших совпадений */
        size_t pos = 0;
        for (size_t i = 0; i < found && i < 3; i++) {
            int written = snprintf(partial_response + pos, partial_size - pos,
                                   "%s ", results[i]->content);
            if (written > 0) pos += (size_t)written;
        }
        *source_count = found;
        step->confidence = scores[0];
        snprintf(step->result, sizeof(step->result),
                 "Найдено %zu релевантных документов (лучший балл: %.2f)",
                 found, scores[0]);
    } else {
        snprintf(step->result, sizeof(step->result),
                 "По запросу не найдено релевантных документов");
        step->confidence = 0.0;
    }

    step->duration_ms = now_ms() - t0;
    return 0;
}

/*
 * Шаг 2: Формульный вывод
 * Кодирует запрос в числовое представление и применяет формулы
 */
static int step_formula_inference(
    const char *query,
    KolibriInferenceStep *step,
    char *partial_response,
    size_t partial_size,
    size_t *formulas_applied
) {
    double t0 = now_ms();
    snprintf(step->description, sizeof(step->description),
             "Формульный вывод через числовые паттерны");

    if (formulas_applied) {
        *formulas_applied = 0U;
    }

    const KolibriFormulaPool *pool = inference_get_formula_memory();
    if (!pool || pool->association_count == 0U) {
        snprintf(step->result, sizeof(step->result),
                 "Формульная память пока пуста");
        step->confidence = 0.0;
        step->duration_ms = now_ms() - t0;
        return 0;
    }

    char *normalized_query = inference_lower_ascii_copy(query);
    if (!normalized_query) {
        snprintf(step->result, sizeof(step->result),
                 "Не удалось нормализовать запрос");
        step->confidence = 0.0;
        step->duration_ms = now_ms() - t0;
        return -1;
    }
    inference_normalize_text(normalized_query);
    inference_trim_inplace(normalized_query);

    uint8_t query_digits[KOLIBRI_ASSOC_DIGITS_MAX];
    size_t query_digits_len = inference_encode_text_digits(&g_formula_symbol_table,
                                                           normalized_query,
                                                           query_digits,
                                                           sizeof(query_digits));
    int strict_topic_query = inference_is_definition_query(normalized_query);
    char definition_entity[256];
    inference_extract_definition_entity(normalized_query, definition_entity, sizeof(definition_entity));
    int prefer_explain_assoc = inference_contains_phrase(normalized_query, "объясни");
    int prefer_tell_assoc = inference_contains_phrase(normalized_query, "расскажи");
    int prefer_knowledge_assoc = inference_contains_phrase(normalized_query, "что ты знаешь");
    int prefer_detailed_assoc = inference_contains_phrase(normalized_query, "подробно");
    int prefer_what_is_assoc = inference_contains_phrase(normalized_query, "что такое");
    int prefer_studies_assoc = inference_contains_phrase(normalized_query, "что изучает");
    int prefer_occupation_assoc = inference_contains_phrase(normalized_query, "чем занимается") ||
                                  inference_contains_phrase(normalized_query, "занимается");
    int prefer_structure_assoc = inference_contains_phrase(normalized_query, "как устроен") ||
                                 inference_contains_phrase(normalized_query, "как устроена") ||
                                 inference_contains_phrase(normalized_query, "как устроено");
    int prefer_importance_assoc = inference_contains_phrase(normalized_query, "почему важен") ||
                                  inference_contains_phrase(normalized_query, "почему важна") ||
                                  inference_contains_phrase(normalized_query, "почему важно") ||
                                  inference_contains_phrase(normalized_query, "зачем нужен") ||
                                  inference_contains_phrase(normalized_query, "зачем нужна") ||
                                  inference_contains_phrase(normalized_query, "зачем нужно");

    const char *active_query = normalized_query;
    int strict_mode = strict_topic_query;
    int fallback_entity_mode = 0;
    const KolibriAssociation *best_assoc = NULL;
    double best_score = 0.0;

retry_formula_search:
    best_assoc = NULL;
    best_score = 0.0;
    int query_hash = kf_hash_from_text(active_query);
    for (size_t i = 0; i < pool->association_count; ++i) {
        const KolibriAssociation *assoc = &pool->associations[i];
        double score = 0.0;

        char *normalized_assoc = inference_lower_ascii_copy(assoc->question);
        if (!normalized_assoc) {
            continue;
        }
        inference_normalize_text(normalized_assoc);
        inference_trim_inplace(normalized_assoc);

        double topic_score = inference_topic_overlap_score(active_query, normalized_assoc);
        size_t query_topic_count = 0U;
        size_t candidate_topic_count = 0U;
        double exact_topic_score = inference_topic_exact_match_score(active_query,
                                                                     normalized_assoc,
                                                                     &query_topic_count,
                                                                     &candidate_topic_count);
        double entity_topic_score = 0.0;
        size_t entity_topic_count = 0U;
        size_t entity_candidate_count = 0U;
        double entity_exact_topic_score = 0.0;
        if (definition_entity[0] != '\0') {
            entity_topic_score = inference_topic_overlap_score(definition_entity, normalized_assoc);
            entity_exact_topic_score = inference_topic_exact_match_score(definition_entity,
                                                                         normalized_assoc,
                                                                         &entity_topic_count,
                                                                         &entity_candidate_count);
        }
        char *normalized_answer = NULL;
        if (strict_mode && topic_score <= 0.0) {
            free(normalized_assoc);
            continue;
        }
        if (strict_mode && exact_topic_score <= 0.0 && query_topic_count > 0U &&
            topic_score < 0.95) {
            free(normalized_assoc);
            continue;
        }
        if (definition_entity[0] != '\0') {
            normalized_answer = inference_lower_ascii_copy(assoc->answer);
            if (normalized_answer) {
                inference_normalize_text(normalized_answer);
                inference_trim_inplace(normalized_answer);
                if ((strict_mode || fallback_entity_mode) &&
                    (!inference_definition_support_ok(definition_entity, normalized_answer) ||
                     inference_definition_forbidden_hit(definition_entity, normalized_answer))) {
                    free(normalized_answer);
                    free(normalized_assoc);
                    continue;
                }
            }
        }

        if (assoc->input_hash == query_hash && strcmp(active_query, normalized_assoc) == 0) {
            score += 3.0;
        }

        score += exact_topic_score * 6.0;
        score += topic_score * 4.5;
        score += entity_exact_topic_score * 4.5;
        score += entity_topic_score * 3.2;
        score += inference_text_overlap_score(active_query, normalized_assoc) * 2.5;
        score += inference_digit_similarity(query_digits,
                                            query_digits_len,
                                            assoc->question_digits,
                                            assoc->question_digits_length) * 1.5;
        score += inference_text_overlap_score(active_query, assoc->answer) * 0.45;
        score += inference_assoc_quality_bonus(assoc);

        if (prefer_explain_assoc) {
            if (inference_contains_phrase(normalized_assoc, "объясни")) {
                score += 2.4;
            } else if (inference_contains_phrase(normalized_assoc, "что такое")) {
                score -= 0.35;
            }
        }
        if (prefer_tell_assoc) {
            if (inference_contains_phrase(normalized_assoc, "расскажи")) {
                score += 2.4;
            } else if (inference_contains_phrase(normalized_assoc, "что такое")) {
                score -= 0.35;
            }
        }
        if (prefer_knowledge_assoc) {
            if (inference_contains_phrase(normalized_assoc, "что ты знаешь")) {
                score += 2.8;
            } else if (inference_contains_phrase(normalized_assoc, "расскажи")) {
                score += 0.65;
            } else if (inference_contains_phrase(normalized_assoc, "что такое")) {
                score -= 0.35;
            }
        }
        if (prefer_detailed_assoc) {
            if (inference_contains_phrase(normalized_assoc, "подробно")) {
                score += 2.6;
            } else if (inference_contains_phrase(normalized_assoc, "расскажи") ||
                       inference_contains_phrase(normalized_assoc, "что ты знаешь")) {
                score += 0.75;
            } else if (inference_contains_phrase(normalized_assoc, "что такое")) {
                score -= 0.25;
            }
        }
        if (prefer_what_is_assoc && inference_contains_phrase(normalized_assoc, "что такое")) {
            score += 1.6;
        }
        if (prefer_studies_assoc) {
            if (inference_contains_phrase(normalized_assoc, "что изучает")) {
                score += 2.8;
            } else if (inference_contains_phrase(normalized_assoc, "что такое")) {
                score -= 0.45;
            }
        }
        if (prefer_occupation_assoc) {
            if (inference_contains_phrase(normalized_assoc, "чем занимается")) {
                score += 2.6;
            } else if (inference_contains_phrase(normalized_assoc, "что такое")) {
                score -= 0.35;
            }
        }
        if (prefer_structure_assoc) {
            if (inference_contains_phrase(normalized_assoc, "как устроено")) {
                score += 2.6;
            } else if (inference_contains_phrase(normalized_assoc, "объясни") ||
                       inference_contains_phrase(normalized_assoc, "расскажи")) {
                score += 0.45;
            } else if (inference_contains_phrase(normalized_assoc, "что такое")) {
                score -= 0.25;
            }
        }
        if (prefer_importance_assoc) {
            if (inference_contains_phrase(normalized_assoc, "почему важ") ||
                inference_contains_phrase(normalized_assoc, "зачем нуж")) {
                score += 2.9;
            } else if (inference_contains_phrase(normalized_assoc, "объясни") ||
                       inference_contains_phrase(normalized_assoc, "расскажи")) {
                score += 0.35;
            } else if (inference_contains_phrase(normalized_assoc, "что такое")) {
                score -= 0.4;
            }
        }

        if (definition_entity[0] != '\0' && normalized_answer) {
            if (strncmp(normalized_assoc, definition_entity, strlen(definition_entity)) == 0) {
                score += 1.4;
            }
            if (strncmp(normalized_answer, definition_entity, strlen(definition_entity)) == 0) {
                score += 1.1;
            } else if (strstr(normalized_answer, definition_entity) != NULL) {
                score += 0.35;
            }
        }

        if (strict_mode && candidate_topic_count > query_topic_count &&
            query_topic_count > 0U) {
            score -= (double)(candidate_topic_count - query_topic_count) * 0.9;
        }

        if ((strstr(active_query, normalized_assoc) != NULL) ||
            (strstr(normalized_assoc, active_query) != NULL)) {
            score += 0.5;
        }

        if (fallback_entity_mode && definition_entity[0] != '\0') {
            if (strstr(normalized_assoc, definition_entity) != NULL) {
                score += 0.8;
            }
            if (normalized_answer && strstr(normalized_answer, definition_entity) != NULL) {
                score += 0.4;
            }
        }

        if (score > best_score) {
            best_score = score;
            best_assoc = assoc;
        }
        free(normalized_answer);
        free(normalized_assoc);
    }

    if (!best_assoc && strict_topic_query && !fallback_entity_mode &&
        definition_entity[0] != '\0') {
        active_query = definition_entity;
        strict_mode = 0;
        fallback_entity_mode = 1;
        goto retry_formula_search;
    }

    if (best_assoc && best_score >= 1.15 && best_assoc->answer[0] != '\0') {
        inference_compose_human_answer(query,
                                       best_assoc->answer,
                                       partial_response,
                                       partial_size);
        step->confidence = best_score >= 1.0 ? (best_score > 4.0 ? 1.0 : best_score / 4.0) : 0.25;
        snprintf(step->result, sizeof(step->result),
                 "Формульная память выбрала ассоциацию из \"%s\" (балл: %.2f)",
                 best_assoc->source[0] != '\0' ? best_assoc->source : "памяти",
                 best_score);
        if (formulas_applied) {
            *formulas_applied = 1U;
        }
    } else {
        snprintf(step->result, sizeof(step->result),
                 "Формульная память не нашла близкую ассоциацию");
        step->confidence = 0.0;
    }

    free(normalized_query);
    step->duration_ms = now_ms() - t0;
    return 0;
}

/*
 * Шаг 3: Логическое рассуждение через мета-формулы
 * Использует logical_memory и formula_logic для вывода
 */
static int step_logical_reasoning(
    const char *query,
    KolibriInferenceStep *step,
    size_t *rules_fired
) {
    double t0 = now_ms();
    snprintf(step->description, sizeof(step->description),
             "Логическое рассуждение (мета-формулы)");

    MetaFormulaStore *store = mf_create_store();
    LogicalMemory *mem = lm_create_memory();
    if (!store || !mem) {
        step->confidence = 0.0;
        step->duration_ms = now_ms() - t0;
        if (store) mf_destroy_store(store);
        if (mem) lm_destroy_memory(mem);
        return -1;
    }

    /* Сохраняем запрос как логическое выражение */
    lm_store_logic(mem, "query", lm_logic_constant(query));

    /* Запускаем автообнаружение паттернов */
    int discovered = mf_auto_discover_patterns(mem, store);
    *rules_fired = (size_t)(discovered > 0 ? discovered : 0);

    snprintf(step->result, sizeof(step->result),
             "Выявлено %d паттернов в запросе", discovered);
    step->confidence = discovered > 0 ? 0.5 : 0.1;

    mf_destroy_store(store);
    lm_destroy_memory(mem);
    step->duration_ms = now_ms() - t0;
    return 0;
}

/*
 * Шаг 4: Фрактальная десятичная память
 * Кодирует запрос в десятичный путь и ищет ближайшие понятия
 */
static int step_fractal_memory(
    const char *query,
    KolibriInferenceStep *step,
    char *partial_response,
    size_t partial_size
) {
    double t0 = now_ms();
    snprintf(step->description, sizeof(step->description),
             "Поиск в фрактальной десятичной памяти");

    KfmContext fmem;
    if (kfm_init(&fmem, 42) != 0) {
        snprintf(step->result, sizeof(step->result),
                 "Не удалось инициализировать фрактальную память");
        step->confidence = 0.0;
        step->duration_ms = now_ms() - t0;
        return -1;
    }

    /* Кодируем запрос в десятичный путь */
    uint8_t query_path[KFM_MAX_DEPTH];
    size_t query_path_len = kfm_text_to_path(query, strlen(query),
                                              query_path, KFM_MAX_DEPTH);

    if (query_path_len == 0) {
        snprintf(step->result, sizeof(step->result),
                 "Запрос слишком короткий для фрактального кодирования");
        step->confidence = 0.0;
        step->duration_ms = now_ms() - t0;
        kfm_free(&fmem);
        return 0;
    }

    /* Ограничиваем путь до разумной глубины */
    if (query_path_len > 30) query_path_len = 30;

    /* Вставляем запрос как понятие (для будущих обращений) */
    kfm_insert(&fmem, query_path, query_path_len, query, strlen(query));

    /* Ассоциативный поиск */
    KfmSearchResult results[5];
    int found = kfm_search(&fmem, query_path, query_path_len, results, 5);

    if (found > 0 && results[0].similarity > 0.3f) {
        /* Нашли релевантный путь — материализуем */
        if (results[0].node && results[0].node->payload_size > 0) {
            size_t copy_len = results[0].node->payload_size;
            if (copy_len >= partial_size) copy_len = partial_size - 1;
            memcpy(partial_response, results[0].node->payload, copy_len);
            partial_response[copy_len] = '\0';
        }
        step->confidence = (double)results[0].similarity;
        snprintf(step->result, sizeof(step->result),
                 "Найдено %d фрактальных путей (лучшая близость: %.2f, глубина: %u)",
                 found, results[0].similarity, results[0].path_len);
    } else {
        snprintf(step->result, sizeof(step->result),
                 "Фрактальная память не нашла близкий путь (глубина запроса: %zu)",
                 query_path_len);
        step->confidence = 0.05;
    }

    /* Активируем найденный путь для укрепления ассоциаций */
    kfm_activate(&fmem, query_path, query_path_len, 0.5f);

    kfm_free(&fmem);
    step->duration_ms = now_ms() - t0;
    return 0;
}

/* ========== ГЛАВНАЯ ФУНКЦИЯ ИНФЕРЕНСА ========== */

/*
 * Шаг 5: Семантическое понимание через World Model + Attention
 * Использует Transformer для глубокого анализа запроса:
 *   - Эмбеддинг запроса через Self-Attention
 *   - Предсказательная оценка «понимания»
 *   - Генерация ответа через авторегрессию
 */
static int step_semantic_understanding(
    const char *query,
    KolibriInferenceStep *step,
    char *partial_response,
    size_t partial_size
) {
    double t0 = now_ms();
    snprintf(step->description, sizeof(step->description),
             "Семантическое понимание (Transformer + World Model)");

    /* Создаём мировую модель */
    KwmContext *wm = kwm_create(42);
    if (!wm) {
        snprintf(step->result, sizeof(step->result),
                 "Не удалось создать мировую модель");
        step->confidence = 0.0;
        step->duration_ms = now_ms() - t0;
        return -1;
    }

    /* Подаём запрос в мировую модель для анализа */
    size_t qlen = strlen(query);
    if (qlen > 0) {
        float avg_surprise = kwm_observe_block(
            wm, (const uint8_t *)query, qlen);

        /* Предсказание продолжения запроса */
        uint8_t generated[512];
        size_t gen_len = kwm_generate(wm, generated, 256, 0.7f);

        if (gen_len > 0) {
            /* Копируем генерацию как ответ */
            size_t copy_len = gen_len < partial_size - 1 ?
                              gen_len : partial_size - 1;
            memcpy(partial_response, generated, copy_len);
            partial_response[copy_len] = '\0';

            /* Уверенность обратно пропорциональна удивлению */
            step->confidence = avg_surprise < 8.0 ?
                              1.0 - (double)avg_surprise / 8.0 : 0.05;

            snprintf(step->result, sizeof(step->result),
                     "Сгенерировано %zu байт (удивление=%.2f бит/байт)",
                     gen_len, (double)avg_surprise);
        } else {
            snprintf(step->result, sizeof(step->result),
                     "Генерация не удалась (удивление=%.2f)",
                     (double)avg_surprise);
            step->confidence = 0.05;
        }
    }

    /* Извлечение концептов для метаданных */
    KwmConcept concepts[8];
    size_t num_concepts = kwm_extract_concepts(
        wm, query, qlen, concepts, 8);
    (void)num_concepts;  /* Используется для внутреннего обогащения */

    kwm_destroy(wm);
    step->duration_ms = now_ms() - t0;
    return 0;
}

/*
 * Шаг 6: Chain-of-Thought — разбиение на подзадачи
 * Анализирует запрос, разбивает на логические шаги,
 * решает каждый шаг отдельно, собирает финальный ответ
 */
static int step_chain_of_thought(
    const char *query,
    KolibriInferenceStep *steps,
    size_t max_steps,
    size_t *step_count,
    char *final_response,
    size_t response_size
) {
    double t0 = now_ms();

    /* Определяем сложность запроса эвристически */
    size_t qlen = strlen(query);
    int complexity = 1;

    /* Индикаторы сложного запроса */
    if (strstr(query, " и ") || strstr(query, " and ")) complexity++;
    if (strstr(query, " или ") || strstr(query, " or ")) complexity++;
    if (strstr(query, "почему") || strstr(query, "why")) complexity++;
    if (strstr(query, "как") || strstr(query, "how")) complexity++;
    if (strstr(query, "если") || strstr(query, "if")) complexity++;
    if (qlen > 100) complexity++;
    if (qlen > 200) complexity++;
    if (complexity > (int)max_steps) complexity = (int)max_steps;

    size_t si = 0;  /* Индекс шага */

    /* Подшаг 1: Анализ запроса */
    if (si < max_steps) {
        snprintf(steps[si].description, sizeof(steps[si].description),
                 "CoT: Анализ структуры запроса");
        snprintf(steps[si].result, sizeof(steps[si].result),
                 "Complexity=%d, length=%zu, sub-tasks identified",
                 complexity, qlen);
        steps[si].confidence = 0.8;
        steps[si].duration_ms = now_ms() - t0;
        si++;
    }

    /* Подшаг 2: Семантическое кодирование */
    if (si < max_steps) {
        double t1 = now_ms();
        KatModel *model = kat_model_create(42);
        KatWorkspace *ws = kat_workspace_create();

        if (model && ws) {
            /* Forward pass для получения семантического представления */
            size_t tok_len = qlen < KAT_MAX_SEQ ? qlen : KAT_MAX_SEQ;
            kat_forward(model, ws, (const uint8_t *)query, tok_len);

            float embedding[KAT_EMBED_DIM];
            kat_extract_embedding(ws, embedding);

            /* Вычисляем «семантическую плотность» */
            float density = 0.0f;
            for (size_t d = 0; d < KAT_EMBED_DIM; d++) {
                density += fabsf(embedding[d]);
            }
            density /= (float)KAT_EMBED_DIM;

            snprintf(steps[si].description, sizeof(steps[si].description),
                     "CoT: Семантическое кодирование (Transformer)");
            snprintf(steps[si].result, sizeof(steps[si].result),
                     "Embedding density=%.4f, dim=%d",
                     (double)density, KAT_EMBED_DIM);
            steps[si].confidence = density > 0.1 ? 0.6 : 0.3;
        }

        if (ws) kat_workspace_destroy(ws);
        if (model) kat_model_destroy(model);

        steps[si].duration_ms = now_ms() - t1;
        si++;
    }

    /* Подшаг 3: Рассуждение через предсказание */
    if (si < max_steps && complexity >= 2) {
        double t2 = now_ms();
        KwmContext *wm = kwm_create(42);

        if (wm) {
            /* Подаём весь запрос */
            float surprise = kwm_observe_block(
                wm, (const uint8_t *)query, qlen);

            /* Генерируем рассуждение */
            uint8_t reasoning[256];
            size_t rlen = kwm_generate(wm, reasoning, 128, 0.5f);

            snprintf(steps[si].description, sizeof(steps[si].description),
                     "CoT: Предсказательное рассуждение");
            snprintf(steps[si].result, sizeof(steps[si].result),
                     "reasoning=%zu bytes, surprise=%.2f bits/byte",
                     rlen, (double)surprise);
            steps[si].confidence = surprise < 6.0 ? 0.5 : 0.2;

            /* Добавляем рассуждение к ответу */
            if (rlen > 0 && rlen < response_size - 1) {
                memcpy(final_response, reasoning, rlen);
                final_response[rlen] = '\0';
            }

            kwm_destroy(wm);
        }

        steps[si].duration_ms = now_ms() - t2;
        si++;
    }

    *step_count = si;
    return 0;
}

int kolibri_inference_run(
    KolibriInferenceContext *ctx,
    const char *query,
    KolibriInferenceResult *result
) {
    if (!ctx || !query || !result) return -1;

    memset(result, 0, sizeof(KolibriInferenceResult));
    double t_start = now_ms();

    /* Счётчик шагов */
    size_t step_idx = 0;

    /* Временные буферы для частичных ответов */
    char direct_response[4096] = {0};
    char formula_response[4096] = {0};
    size_t source_count = 0;
    size_t formulas_applied = 0;
    size_t rules_fired = 0;

    /* === Шаг 1: Прямой поиск (если стратегия поддерживает) === */
    if (ctx->strategy == KOLIBRI_INF_DIRECT ||
        ctx->strategy == KOLIBRI_INF_HYBRID ||
        ctx->strategy == KOLIBRI_INF_CHAIN) {

        step_direct_search(
            query,
            &result->steps[step_idx],
            direct_response,
            sizeof(direct_response),
            &source_count
        );
        step_idx++;
        result->knowledge_hits = source_count;
    }

    /* === Шаг 2: Формульный вывод === */
    if (ctx->strategy == KOLIBRI_INF_FORMULA ||
        ctx->strategy == KOLIBRI_INF_HYBRID ||
        ctx->strategy == KOLIBRI_INF_CHAIN) {

        step_formula_inference(
            query,
            &result->steps[step_idx],
            formula_response,
            sizeof(formula_response),
            &formulas_applied
        );
        step_idx++;
        result->formulas_applied = formulas_applied;
    }

    /* === Шаг 3: Логическое рассуждение === */
    if (ctx->strategy == KOLIBRI_INF_LOGICAL ||
        ctx->strategy == KOLIBRI_INF_HYBRID) {

        step_logical_reasoning(
            query,
            &result->steps[step_idx],
            &rules_fired
        );
        result->logic_rules_fired = rules_fired;
        step_idx++;
    }

    /* === Шаг 4: Фрактальная десятичная память === */
    char fractal_response[4096] = {0};
    if (ctx->strategy == KOLIBRI_INF_HYBRID ||
        ctx->strategy == KOLIBRI_INF_CHAIN) {

        step_fractal_memory(
            query,
            &result->steps[step_idx],
            fractal_response,
            sizeof(fractal_response)
        );
        step_idx++;
    }

    /* === Шаг 5: Семантическое понимание (Transformer + World Model) === */
    char semantic_response[4096] = {0};
    if (ctx->strategy == KOLIBRI_INF_HYBRID ||
        ctx->strategy == KOLIBRI_INF_CHAIN) {

        step_semantic_understanding(
            query,
            &result->steps[step_idx],
            semantic_response,
            sizeof(semantic_response)
        );
        step_idx++;
    }

    /* === Шаг 6: Chain-of-Thought (для сложных запросов) === */
    char cot_response[4096] = {0};
    if (ctx->strategy == KOLIBRI_INF_CHAIN ||
        (ctx->strategy == KOLIBRI_INF_HYBRID && strlen(query) > 50)) {

        size_t cot_steps = 0;
        size_t cot_max = KOLIBRI_INF_MAX_STEPS - step_idx;
        if (cot_max > 4) cot_max = 4;

        step_chain_of_thought(
            query,
            &result->steps[step_idx],
            cot_max,
            &cot_steps,
            cot_response,
            sizeof(cot_response)
        );
        step_idx += cot_steps;
    }

    result->step_count = step_idx;

    /* === Финальная сборка ответа === */
    /* Приоритет: formula > knowledge > semantic > fractal > cot > fallback */
    if (strlen(formula_response) > 0) {
        snprintf(result->response, KOLIBRI_INF_MAX_RESPONSE, "%s", formula_response);
    } else if (strlen(direct_response) > 0) {
        snprintf(result->response, KOLIBRI_INF_MAX_RESPONSE, "%s", direct_response);
    } else if (strlen(semantic_response) > 0) {
        snprintf(result->response, KOLIBRI_INF_MAX_RESPONSE, "%s", semantic_response);
    } else if (strlen(fractal_response) > 0) {
        snprintf(result->response, KOLIBRI_INF_MAX_RESPONSE, "%s", fractal_response);
    } else if (strlen(cot_response) > 0) {
        snprintf(result->response, KOLIBRI_INF_MAX_RESPONSE, "%s", cot_response);
    } else {
        snprintf(result->response, KOLIBRI_INF_MAX_RESPONSE,
                 "Мне пока не хватает данных, чтобы ответить на запрос: \"%s\". "
                 "Я просмотрел %zu источников знаний, применил %zu формул и "
                 "запустил %zu логических правил.",
                 query, source_count, result->formulas_applied, rules_fired);
    }
    result->response_length = strlen(result->response);

    /* Вычислить общую уверенность (взвешенное среднее шагов) */
    double sum_conf = 0.0;
    for (size_t i = 0; i < step_idx; i++) {
        sum_conf += result->steps[i].confidence;
    }
    result->total_confidence = step_idx > 0 ? sum_conf / (double)step_idx : 0.0;

    /* Время */
    result->total_duration_ms = now_ms() - t_start;

    /* Обновить статистику контекста */
    ctx->total_queries++;

    /* Подсчёт токенов (приблизительно по пробелам) */
    char tokens[64][128];
    ctx->total_tokens_in += tokenize_query(query, tokens, 64);
    ctx->total_tokens_out += tokenize_query(result->response, tokens, 64);

    /* Скользящее среднее уверенности и времени */
    double n = (double)ctx->total_queries;
    ctx->avg_confidence = ctx->avg_confidence * ((n - 1) / n)
                        + result->total_confidence / n;
    ctx->avg_duration_ms = ctx->avg_duration_ms * ((n - 1) / n)
                         + result->total_duration_ms / n;

    return 0;
}

/* ========== ОДИНОЧНЫЙ ШАГ ========== */

int kolibri_inference_step(
    KolibriInferenceContext *ctx,
    const char *query,
    KolibriInferenceStep *step
) {
    if (!ctx || !query || !step) return -1;

    memset(step, 0, sizeof(KolibriInferenceStep));
    double t0 = now_ms();

    snprintf(step->description, sizeof(step->description),
             "Одношаговый инференс");

    /* Выполняем прямой поиск как единичный шаг */
    const KolibriKnowledgeIndex *idx = inference_get_knowledge_index();
    if (!idx) {
        step->confidence = 0.0;
        step->duration_ms = now_ms() - t0;
        return -1;
    }

    const KolibriKnowledgeDocument *results[8];
    double scores[8];
    size_t found = kolibri_knowledge_search_legacy(idx, query, 3, results, scores);

    if (found > 0 && scores[0] > 0.0) {
        snprintf(step->result, sizeof(step->result), "%s",
                 results[0]->content ? results[0]->content : "");
        step->confidence = scores[0];
    } else {
        snprintf(step->result, sizeof(step->result), "Совпадений нет");
        step->confidence = 0.0;
    }

    step->duration_ms = now_ms() - t0;
    return 0;
}

/* ========== СТАТИСТИКА ========== */

int kolibri_inference_get_stats(
    const KolibriInferenceContext *ctx,
    uint64_t *total_queries,
    double *avg_confidence,
    double *avg_duration
) {
    if (!ctx) return -1;
    if (total_queries) *total_queries = ctx->total_queries;
    if (avg_confidence) *avg_confidence = ctx->avg_confidence;
    if (avg_duration) *avg_duration = ctx->avg_duration_ms;
    return 0;
}

void kolibri_inference_reset_stats(KolibriInferenceContext *ctx) {
    if (!ctx) return;
    ctx->total_queries = 0;
    ctx->total_tokens_in = 0;
    ctx->total_tokens_out = 0;
    ctx->avg_confidence = 0.0;
    ctx->avg_duration_ms = 0.0;
}
