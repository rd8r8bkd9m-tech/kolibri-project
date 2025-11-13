#include "kolibri/knowledge.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int ensure_capacity(KolibriKnowledgeIndex *index, size_t additional) {
    if (!index) {
        return -1;
    }
    size_t required = index->count + additional;
    if (required <= index->capacity) {
        return 0;
    }
    size_t new_capacity = index->capacity ? index->capacity * 2U : 8U;
    while (new_capacity < required) {
        new_capacity *= 2U;
    }
    KolibriKnowledgeDocument *docs =
        (KolibriKnowledgeDocument *)realloc(index->documents, new_capacity * sizeof(KolibriKnowledgeDocument));
    if (!docs) {
        return -1;
    }
    index->documents = docs;
    index->capacity = new_capacity;
    return 0;
}

int kolibri_knowledge_index_init(KolibriKnowledgeIndex *index) {
    if (!index) {
        return -1;
    }
    index->documents = NULL;
    index->count = 0;
    index->capacity = 0;
    return 0;
}

static void free_document(KolibriKnowledgeDocument *doc) {
    if (!doc) {
        return;
    }
    free(doc->id);
    free(doc->title);
    free(doc->content);
    free(doc->content_lower);
    free(doc->source);
    doc->id = NULL;
    doc->title = NULL;
    doc->content = NULL;
    doc->content_lower = NULL;
    doc->source = NULL;
}

void kolibri_knowledge_index_free(KolibriKnowledgeIndex *index) {
    if (!index) {
        return;
    }
    for (size_t i = 0; i < index->count; ++i) {
        free_document(&index->documents[i]);
    }
    free(index->documents);
    index->documents = NULL;
    index->count = 0;
    index->capacity = 0;
}

static char *duplicate_string(const char *src) {
    if (!src) {
        return NULL;
    }
    size_t length = strlen(src);
    char *copy = (char *)malloc(length + 1U);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, src, length + 1U);
    return copy;
}

static char *string_slice(const char *begin, size_t length) {
    char *copy = (char *)malloc(length + 1U);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, begin, length);
    copy[length] = '\0';
    return copy;
}

static char *extract_title(const char *content) {
    if (!content) {
        return duplicate_string("Документ Kolibri");
    }
    const char *line_start = content;
    while (*line_start) {
        const char *line_end = strchr(line_start, '\n');
        size_t length = line_end ? (size_t)(line_end - line_start) : strlen(line_start);
        while (length > 0U && isspace((unsigned char)line_start[length - 1U])) {
            --length;
        }
        if (length > 0U) {
            if (line_start[0] == '#') {
                while (length > 0U && line_start[0] == '#') {
                    ++line_start;
                    --length;
                }
                while (length > 0U && isspace((unsigned char)*line_start)) {
                    ++line_start;
                    --length;
                }
                return string_slice(line_start, length);
            }
        }
        if (!line_end) {
            break;
        }
        line_start = line_end + 1;
    }
    return duplicate_string("Документ Kolibri");
}

static char *make_id_from_path(const char *path) {
    if (!path) {
        return duplicate_string("kolibri-doc");
    }
    const char *basename = strrchr(path, '/');
    basename = basename ? basename + 1 : path;
    const char *dot = strrchr(basename, '.');
    size_t length = dot ? (size_t)(dot - basename) : strlen(basename);
    return string_slice(basename, length);
}

static char *read_file_contents(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    if (fseek(file, 0L, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long size = ftell(file);
    if (size < 0L) {
        fclose(file);
        return NULL;
    }
    if (fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    char *buffer = (char *)malloc((size_t)size + 1U);
    if (!buffer) {
        fclose(file);
        return NULL;
    }
    size_t read = fread(buffer, 1U, (size_t)size, file);
    fclose(file);
    buffer[read] = '\0';
    return buffer;
}

static char *to_lowercase_copy(const char *text) {
    if (!text) {
        return NULL;
    }
    size_t length = strlen(text);
    char *copy = (char *)malloc(length + 1U);
    if (!copy) {
        return NULL;
    }
    for (size_t i = 0; i < length; ++i) {
        copy[i] = (char)tolower((unsigned char)text[i]);
    }
    copy[length] = '\0';
    return copy;
}

static char *shorten_content(const char *content) {
    if (!content) {
        return duplicate_string("");
    }
    const size_t limit = 512U;
    size_t length = strlen(content);
    if (length <= limit) {
        return duplicate_string(content);
    }
    size_t cut = limit;
    while (cut > 0 && !isspace((unsigned char)content[cut])) {
        --cut;
    }
    if (cut == 0) {
        cut = limit;
    }
    char *snippet = (char *)malloc(cut + 4U);
    if (!snippet) {
        return NULL;
    }
    memcpy(snippet, content, cut);
    snippet[cut] = '\0';
    strcat(snippet, "...");
    return snippet;
}

static int add_document(KolibriKnowledgeIndex *index, const char *path, const char *root) {
    char *content = read_file_contents(path);
    if (!content) {
        return -1;
    }
    char relative[1024];
    if (root && strstr(path, root) == path) {
        size_t root_len = strlen(root);
        const char *sub_path = path + root_len;
        if (*sub_path == '/' || *sub_path == '\\') {
            ++sub_path;
        }
        snprintf(relative, sizeof(relative), "%s", sub_path);
    } else {
        snprintf(relative, sizeof(relative), "%s", path);
    }

    if (ensure_capacity(index, 1U) != 0) {
        free(content);
        return -1;
    }
    KolibriKnowledgeDocument *doc = &index->documents[index->count++];
    doc->id = make_id_from_path(path);
    doc->title = extract_title(content);
    doc->content = shorten_content(content);
    doc->content_lower = to_lowercase_copy(content);
    doc->source = duplicate_string(relative);
    free(content);
    if (!doc->id || !doc->title || !doc->content || !doc->content_lower || !doc->source) {
        free_document(doc);
        --index->count;
        return -1;
    }
    return 0;
}

static int is_directory(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    return S_ISDIR(st.st_mode);
}

static int load_directory_recursive(KolibriKnowledgeIndex *index, const char *root, const char *path) {
    DIR *dir = opendir(path);
    if (!dir) {
        return -1;
    }
    struct dirent *entry = NULL;
    char child_path[1024];
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        snprintf(child_path, sizeof(child_path), "%s/%s", path, entry->d_name);
        if (is_directory(child_path)) {
            load_directory_recursive(index, root, child_path);
            continue;
        }
        const char *ext = strrchr(entry->d_name, '.');
        if (ext && (strcmp(ext, ".md") == 0 || strcmp(ext, ".txt") == 0)) {
            add_document(index, child_path, root);
        }
    }
    closedir(dir);
    return 0;
}

int kolibri_knowledge_index_load_directory(KolibriKnowledgeIndex *index, const char *root_path) {
    if (!index || !root_path) {
        return -1;
    }
    if (!is_directory(root_path)) {
        return 0;
    }
    return load_directory_recursive(index, root_path, root_path);
}

static size_t tokenize_query(const char *query, char tokens[][64], size_t max_tokens) {
    size_t count = 0;
    size_t length = query ? strlen(query) : 0U;
    size_t i = 0;
    while (i < length && count < max_tokens) {
        while (i < length && !isalnum((unsigned char)query[i])) {
            ++i;
        }
        if (i >= length) {
            break;
        }
        size_t start = i;
        while (i < length && isalnum((unsigned char)query[i])) {
            ++i;
        }
        size_t token_length = i - start;
        if (token_length > 0) {
            if (token_length >= 64U) {
                token_length = 63U;
            }
            for (size_t j = 0; j < token_length; ++j) {
                tokens[count][j] = (char)tolower((unsigned char)query[start + j]);
            }
            tokens[count][token_length] = '\0';
            ++count;
        }
    }
    return count;
}

typedef struct {
    double score;
    size_t index;
} RankedDocument;

static int compare_ranked(const void *lhs, const void *rhs) {
    const RankedDocument *a = (const RankedDocument *)lhs;
    const RankedDocument *b = (const RankedDocument *)rhs;
    if (a->score < b->score) {
        return 1;
    }
    if (a->score > b->score) {
        return -1;
    }
    if (a->index > b->index) {
        return 1;
    }
    if (a->index < b->index) {
        return -1;
    }
    return 0;
}

size_t kolibri_knowledge_search_legacy(const KolibriKnowledgeIndex *index,
                                       const char *query,
                                       size_t limit,
                                       const KolibriKnowledgeDocument **results,
                                       double *scores) {
    if (!index || index->count == 0 || !query || !results) {
        return 0;
    }
    if (limit == 0) {
        return 0;
    }
    char tokens[16][64];
    size_t token_count = tokenize_query(query, tokens, 16U);
    if (token_count == 0) {
        return 0;
    }
    RankedDocument *ranked = (RankedDocument *)malloc(index->count * sizeof(RankedDocument));
    if (!ranked) {
        return 0;
    }
    size_t ranked_count = 0;
    for (size_t i = 0; i < index->count; ++i) {
        const KolibriKnowledgeDocument *doc = &index->documents[i];
        double score = 0.0;
        for (size_t t = 0; t < token_count; ++t) {
            if (strstr(doc->content_lower, tokens[t]) != NULL) {
                score += 1.0;
            }
        }
        if (score > 0.0) {
            ranked[ranked_count].score = score;
            ranked[ranked_count].index = i;
            ++ranked_count;
        }
    }
    if (ranked_count == 0) {
        free(ranked);
        return 0;
    }
    qsort(ranked, ranked_count, sizeof(RankedDocument), compare_ranked);
    if (ranked_count > limit) {
        ranked_count = limit;
    }
    for (size_t i = 0; i < ranked_count; ++i) {
        results[i] = &index->documents[ranked[i].index];
        if (scores) {
            scores[i] = ranked[i].score;
        }
    }
    free(ranked);
    return ranked_count;
}
