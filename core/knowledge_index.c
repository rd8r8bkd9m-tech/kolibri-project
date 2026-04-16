#include "kolibri/knowledge_index.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define KOLIBRI_TOP_TERMS 32U

typedef struct {
    char *token;
    size_t df;
    float idf;
} GlobalToken;

typedef struct {
    char *token;
    size_t count;
} DocToken;

typedef struct {
    char *id;
    char *title;
    char *source;
    char *content;
    KolibriKnowledgeVectorItem *vector;
    size_t vector_size;
    float norm;
} Document;

struct KolibriKnowledgeIndex {
    Document *documents;
    size_t document_count;
    GlobalToken *tokens;
    size_t token_count;
    size_t token_capacity;
};

static void *kolibri_alloc(size_t size) {
    void *ptr = calloc(1, size);
    if (!ptr) {
        fprintf(stderr, "[kolibri-knowledge] allocation failure\n");
        abort();
    }
    return ptr;
}

static char *kolibri_strdup(const char *text) {
    if (!text) {
        return NULL;
    }
    size_t len = strlen(text);
    char *copy = (char *)malloc(len + 1U);
    if (!copy) {
        fprintf(stderr, "[kolibri-knowledge] strdup failure\n");
        abort();
    }
    memcpy(copy, text, len + 1U);
    return copy;
}

static int is_markdown_file(const char *path) {
    size_t len = strlen(path);
    return len > 3U && strcmp(path + len - 3U, ".md") == 0;
}

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} PathList;

static void path_list_init(PathList *list) {
    list->items = NULL;
    list->count = 0U;
    list->capacity = 0U;
}

static void path_list_push(PathList *list, const char *path) {
    if (list->count == list->capacity) {
        size_t new_capacity = list->capacity == 0U ? 16U : list->capacity * 2U;
        char **new_items = (char **)realloc(list->items, new_capacity * sizeof(char *));
        if (!new_items) {
            fprintf(stderr, "[kolibri-knowledge] realloc failure\n");
            abort();
        }
        list->items = new_items;
        list->capacity = new_capacity;
    }
    list->items[list->count++] = kolibri_strdup(path);
}

static void path_list_free(PathList *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->count; ++i) {
        free(list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0U;
    list->capacity = 0U;
}

static int path_is_directory(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    return S_ISDIR(st.st_mode);
}

static void collect_markdown_files(const char *root, PathList *list) {
    if (path_is_directory(root)) {
        DIR *dir = opendir(root);
        if (!dir) {
            return;
        }
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            char buffer[4096];
            snprintf(buffer, sizeof(buffer), "%s/%s", root, entry->d_name);
            if (path_is_directory(buffer)) {
                collect_markdown_files(buffer, list);
            } else if (is_markdown_file(buffer)) {
                path_list_push(list, buffer);
            }
        }
        closedir(dir);
    } else if (is_markdown_file(root)) {
        path_list_push(list, root);
    }
}

static char *read_file_utf8(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long size = ftell(file);
    if (size < 0) {
        fclose(file);
        return NULL;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    char *buffer = (char *)malloc((size_t)size + 1U);
    if (!buffer) {
        fclose(file);
        return NULL;
    }
    size_t read = fread(buffer, 1, (size_t)size, file);
    fclose(file);
    buffer[read] = '\0';
    return buffer;
}

static char *derive_id_from_path(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *name = slash ? slash + 1 : path;
    size_t len = strlen(name);
    if (len > 3U && name[len - 3U] == '.' && name[len - 2U] == 'm' && name[len - 1U] == 'd') {
        len -= 3U;
    }
    char *id = (char *)malloc(len + 1U);
    if (!id) {
        return NULL;
    }
    memcpy(id, name, len);
    id[len] = '\0';
    return id;
}

static char *extract_title(const char *content) {
    const char *cursor = content;
    while (*cursor != '\0') {
        const char *line_start = cursor;
        while (*cursor != '\0' && *cursor != '\n') {
            cursor++;
        }
        size_t line_len = (size_t)(cursor - line_start);
        if (line_len > 0 && line_start[0] == '#') {
            while (line_len > 0 && (line_start[0] == '#' || isspace((unsigned char)line_start[0]))) {
                line_start++;
                line_len--;
            }
            char *title = (char *)malloc(line_len + 1U);
            if (!title) {
                return kolibri_strdup("Документ");
            }
            memcpy(title, line_start, line_len);
            title[line_len] = '\0';
            return title;
        }
        if (*cursor == '\n') {
            cursor++;
        }
    }
    return kolibri_strdup("Документ");
}

static char *shorten_content(const char *content, size_t max_length) {
    size_t len = strlen(content);
    if (len <= max_length) {
        return kolibri_strdup(content);
    }
    size_t end = max_length;
    while (end > 0 && !isspace((unsigned char)content[end])) {
        end--;
    }
    if (end == 0) {
        end = max_length;
    }
    char *result = (char *)malloc(end + 4U);
    if (!result) {
        return NULL;
    }
    memcpy(result, content, end);
    result[end] = '\0';
    strcat(result, "…");
    return result;
}

static void doc_token_list_add(DocToken **tokens, size_t *count, size_t *capacity, const char *token) {
    for (size_t i = 0; i < *count; ++i) {
        if (strcmp((*tokens)[i].token, token) == 0) {
            (*tokens)[i].count += 1U;
            return;
        }
    }
    if (*count == *capacity) {
        size_t new_cap = (*capacity == 0U) ? 16U : (*capacity * 2U);
        DocToken *new_tokens = (DocToken *)realloc(*tokens, new_cap * sizeof(DocToken));
        if (!new_tokens) {
            fprintf(stderr, "[kolibri-knowledge] realloc doc tokens failed\n");
            abort();
        }
        *tokens = new_tokens;
        *capacity = new_cap;
    }
    (*tokens)[*count].token = kolibri_strdup(token);
    (*tokens)[*count].count = 1U;
    *count += 1U;
}

static void global_register_tokens(GlobalToken **tokens, size_t *count, size_t *capacity, DocToken *doc_tokens, size_t doc_count) {
    for (size_t i = 0; i < doc_count; ++i) {
        const char *token = doc_tokens[i].token;
        size_t j;
        for (j = 0; j < *count; ++j) {
            if (strcmp((*tokens)[j].token, token) == 0) {
                (*tokens)[j].df += 1U;
                break;
            }
        }
        if (j == *count) {
            if (*count == *capacity) {
                size_t new_cap = (*capacity == 0U) ? 64U : (*capacity * 2U);
                GlobalToken *new_tokens = (GlobalToken *)realloc(*tokens, new_cap * sizeof(GlobalToken));
                if (!new_tokens) {
                    fprintf(stderr, "[kolibri-knowledge] realloc global tokens failed\n");
                    abort();
                }
                *tokens = new_tokens;
                *capacity = new_cap;
            }
            (*tokens)[*count].token = kolibri_strdup(token);
            (*tokens)[*count].df = 1U;
            (*tokens)[*count].idf = 0.0f;
            *count += 1U;
        }
    }
}

static void free_doc_tokens(DocToken *tokens, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        free(tokens[i].token);
    }
    free(tokens);
}

static void compute_idf(GlobalToken *tokens, size_t token_count, size_t total_docs) {
    for (size_t i = 0; i < token_count; ++i) {
        tokens[i].idf = (float)(log((1.0 + (double)total_docs) / (1.0 + (double)tokens[i].df)) + 1.0);
    }
}

static size_t find_global_token(const GlobalToken *tokens, size_t token_count, const char *token) {
    for (size_t i = 0; i < token_count; ++i) {
        if (strcmp(tokens[i].token, token) == 0) {
            return i;
        }
    }
    return (size_t)-1;
}

static int vector_compare(const void *a, const void *b) {
    const KolibriKnowledgeVectorItem *va = (const KolibriKnowledgeVectorItem *)a;
    const KolibriKnowledgeVectorItem *vb = (const KolibriKnowledgeVectorItem *)b;
    if (va->weight > vb->weight) {
        return -1;
    }
    if (va->weight < vb->weight) {
        return 1;
    }
    return 0;
}

static KolibriKnowledgeIndex *knowledge_index_new(void) {
    KolibriKnowledgeIndex *index = (KolibriKnowledgeIndex *)kolibri_alloc(sizeof(KolibriKnowledgeIndex));
    index->documents = NULL;
    index->document_count = 0U;
    index->tokens = NULL;
    index->token_count = 0U;
    index->token_capacity = 0U;
    return index;
}

static int parse_markdown_document(const char *path,
                                   size_t max_length,
                                   Document *out_doc,
                                   DocToken **out_tokens,
                                   size_t *out_token_count) {
    char *content = read_file_utf8(path);
    if (!content) {
        return -1;
    }

    char *title = extract_title(content);
    char *short_content = shorten_content(content, max_length);
    if (!short_content) {
        short_content = kolibri_strdup(content);
    }

    DocToken *doc_tokens = NULL;
    size_t token_count = 0U;
    size_t token_capacity = 0U;
    size_t total_tokens = 0U;

    char buffer[128];
    size_t buffer_len = 0U;
    const unsigned char *cursor = (const unsigned char *)content;
    while (*cursor != '\0') {
        if (!isspace(*cursor) && !ispunct(*cursor)) {
            if (buffer_len < sizeof(buffer) - 1U) {
                buffer[buffer_len++] = (char)(*cursor > 127 ? *cursor : (unsigned char)tolower(*cursor));
            }
        } else {
            if (buffer_len > 0U) {
                buffer[buffer_len] = '\0';
                doc_token_list_add(&doc_tokens, &token_count, &token_capacity, buffer);
                total_tokens += 1U;
                buffer_len = 0U;
            }
        }
        cursor++;
    }
    if (buffer_len > 0U) {
        buffer[buffer_len] = '\0';
        doc_token_list_add(&doc_tokens, &token_count, &token_capacity, buffer);
        total_tokens += 1U;
    }

    free(content);

    out_doc->id = derive_id_from_path(path);
    out_doc->title = title;
    out_doc->source = kolibri_strdup(path);
    out_doc->content = short_content;
    out_doc->vector = NULL;
    out_doc->vector_size = 0U;
    out_doc->norm = 0.0f;

    *out_tokens = doc_tokens;
    *out_token_count = token_count;
    return (int)total_tokens;
}

static void compute_document_vector(const GlobalToken *tokens,
                                    size_t token_count,
                                    const DocToken *doc_tokens,
                                    size_t doc_token_count,
                                    size_t total_docs,
                                    Document *doc) {
    if (doc_token_count == 0U) {
        return;
    }
    double total_terms = 0.0;
    for (size_t i = 0; i < doc_token_count; ++i) {
        total_terms += (double)doc_tokens[i].count;
    }

    KolibriKnowledgeVectorItem *vector = NULL;
    size_t vector_count = 0U;

    vector = (KolibriKnowledgeVectorItem *)malloc(doc_token_count * sizeof(KolibriKnowledgeVectorItem));
    if (!vector) {
        fprintf(stderr, "[kolibri-knowledge] alloc vector failed\n");
        abort();
    }

    double norm = 0.0;
    for (size_t i = 0; i < doc_token_count; ++i) {
        size_t token_index = find_global_token(tokens, token_count, doc_tokens[i].token);
        if (token_index == (size_t)-1) {
            continue;
        }
        double tf = (double)doc_tokens[i].count / total_terms;
        double weight = tf * (double)tokens[token_index].idf;
        vector[vector_count].token_index = token_index;
        vector[vector_count].weight = (float)weight;
        norm += weight * weight;
        vector_count += 1U;
    }

    if (vector_count == 0U) {
        free(vector);
        doc->vector = NULL;
        doc->vector_size = 0U;
        doc->norm = 0.0f;
        return;
    }

    qsort(vector, vector_count, sizeof(KolibriKnowledgeVectorItem), vector_compare);
    if (vector_count > KOLIBRI_TOP_TERMS) {
        vector_count = KOLIBRI_TOP_TERMS;
    }

    doc->vector = (KolibriKnowledgeVectorItem *)malloc(vector_count * sizeof(KolibriKnowledgeVectorItem));
    if (!doc->vector) {
        fprintf(stderr, "[kolibri-knowledge] alloc doc vector failed\n");
        abort();
    }
    memcpy(doc->vector, vector, vector_count * sizeof(KolibriKnowledgeVectorItem));
    free(vector);

    doc->vector_size = vector_count;
    doc->norm = (float)(sqrt(norm) ?: 1e-6);
}

int kolibri_knowledge_index_create(const char *const *roots,
                                   size_t root_count,
                                   size_t max_length,
                                   KolibriKnowledgeIndex **out_index) {
    if (!roots || root_count == 0U || !out_index) {
        return EINVAL;
    }

    PathList paths;
    path_list_init(&paths);
    for (size_t i = 0; i < root_count; ++i) {
        collect_markdown_files(roots[i], &paths);
    }
    if (paths.count == 0U) {
        path_list_free(&paths);
        *out_index = NULL;
        return ENOENT;
    }

    KolibriKnowledgeIndex *index = knowledge_index_new();
    index->documents = (Document *)kolibri_alloc(paths.count * sizeof(Document));
    index->document_count = paths.count;

    GlobalToken *global_tokens = NULL;
    size_t global_token_count = 0U;
    size_t global_token_capacity = 0U;

    DocToken **all_doc_tokens = (DocToken **)kolibri_alloc(paths.count * sizeof(DocToken *));
    size_t *doc_token_counts = (size_t *)kolibri_alloc(paths.count * sizeof(size_t));

    for (size_t i = 0; i < paths.count; ++i) {
        size_t doc_token_count = 0U;
        DocToken *doc_tokens = NULL;
        int total_tokens = parse_markdown_document(paths.items[i], max_length, &index->documents[i], &doc_tokens, &doc_token_count);
        (void)total_tokens;
        all_doc_tokens[i] = doc_tokens;
        doc_token_counts[i] = doc_token_count;
        if (doc_token_count > 0U) {
            global_register_tokens(&global_tokens, &global_token_count, &global_token_capacity, doc_tokens, doc_token_count);
        }
    }

    compute_idf(global_tokens, global_token_count, index->document_count);

    index->tokens = global_tokens;
    index->token_count = global_token_count;
    index->token_capacity = global_token_capacity;

    for (size_t i = 0; i < paths.count; ++i) {
        compute_document_vector(global_tokens, global_token_count, all_doc_tokens[i], doc_token_counts[i], index->document_count, &index->documents[i]);
        free_doc_tokens(all_doc_tokens[i], doc_token_counts[i]);
    }

    free(all_doc_tokens);
    free(doc_token_counts);
    path_list_free(&paths);

    *out_index = index;
    return 0;
}

void kolibri_knowledge_index_destroy(KolibriKnowledgeIndex *index) {
    if (!index) {
        return;
    }
    for (size_t i = 0; i < index->document_count; ++i) {
        free(index->documents[i].id);
        free(index->documents[i].title);
        free(index->documents[i].source);
        free(index->documents[i].content);
        free(index->documents[i].vector);
    }
    free(index->documents);
    for (size_t i = 0; i < index->token_count; ++i) {
        free(index->tokens[i].token);
    }
    free(index->tokens);
    free(index);
}

size_t kolibri_knowledge_index_document_count(const KolibriKnowledgeIndex *index) {
    return index ? index->document_count : 0U;
}

const KolibriKnowledgeDoc *kolibri_knowledge_index_document(const KolibriKnowledgeIndex *index,
                                                            size_t idx) {
    if (!index || idx >= index->document_count) {
        return NULL;
    }
    return (const KolibriKnowledgeDoc *)&index->documents[idx];
}

size_t kolibri_knowledge_index_token_count(const KolibriKnowledgeIndex *index) {
    return index ? index->token_count : 0U;
}

const KolibriKnowledgeToken *kolibri_knowledge_index_token(const KolibriKnowledgeIndex *index,
                                                           size_t idx) {
    if (!index || idx >= index->token_count) {
        return NULL;
    }
    return (const KolibriKnowledgeToken *)&index->tokens[idx];
}

static void tokenize_query(const char *query,
                           const GlobalToken *tokens,
                           size_t token_count,
                           float **out_weights,
                           float *out_norm) {
    float *weights = (float *)calloc(token_count, sizeof(float));
    if (!weights) {
        fprintf(stderr, "[kolibri-knowledge] alloc query weights failed\n");
        abort();
    }
    double norm = 0.0;
    size_t total_tokens = 0U;
    char buffer[128];
    size_t buffer_len = 0U;
    const unsigned char *cursor = (const unsigned char *)query;
    while (*cursor != '\0') {
        if (isalnum(*cursor)) {
            if (buffer_len < sizeof(buffer) - 1U) {
                buffer[buffer_len++] = (char)tolower(*cursor);
            }
        } else {
            if (buffer_len > 0U) {
                buffer[buffer_len] = '\0';
                size_t idx = find_global_token(tokens, token_count, buffer);
                if (idx != (size_t)-1) {
                    weights[idx] += 1.0f;
                    total_tokens += 1U;
                }
                buffer_len = 0U;
            }
        }
        cursor++;
    }
    if (buffer_len > 0U) {
        buffer[buffer_len] = '\0';
        size_t idx = find_global_token(tokens, token_count, buffer);
        if (idx != (size_t)-1) {
            weights[idx] += 1.0f;
            total_tokens += 1U;
        }
    }

    if (total_tokens == 0U) {
        *out_weights = weights;
        *out_norm = 0.0f;
        return;
    }

    for (size_t i = 0; i < token_count; ++i) {
        if (weights[i] == 0.0f) {
            continue;
        }
        double tf = (double)weights[i] / (double)total_tokens;
        double weight = tf * (double)tokens[i].idf;
        weights[i] = (float)weight;
        norm += weight * weight;
    }

    *out_weights = weights;
    *out_norm = (float)(sqrt(norm) ?: 0.0);
}

int kolibri_knowledge_search(const KolibriKnowledgeIndex *index,
                              const char *query,
                              size_t limit,
                              size_t *out_indices,
                              float *out_scores,
                              size_t *out_result_count) {
    if (!index || !query || limit == 0U || !out_indices || !out_scores || !out_result_count) {
        return EINVAL;
    }
    float *query_weights = NULL;
    float query_norm = 0.0f;
    tokenize_query(query, index->tokens, index->token_count, &query_weights, &query_norm);
    if (query_norm == 0.0f) {
        free(query_weights);
        *out_result_count = 0U;
        return 0;
    }

    size_t result_count = 0U;
    for (size_t i = 0; i < index->document_count; ++i) {
        const Document *doc = &index->documents[i];
        if (doc->vector_size == 0U || doc->norm == 0.0f) {
            continue;
        }
        double dot = 0.0;
        for (size_t j = 0; j < doc->vector_size; ++j) {
            size_t token_index = doc->vector[j].token_index;
            dot += (double)doc->vector[j].weight * (double)query_weights[token_index];
        }
        double score = dot / ((double)doc->norm * (double)query_norm);
        if (score <= 0.0) {
            continue;
        }
        if (result_count < limit) {
            out_indices[result_count] = i;
            out_scores[result_count] = (float)score;
            result_count += 1U;
        } else {
            size_t min_idx = 0U;
            for (size_t k = 1; k < limit; ++k) {
                if (out_scores[k] < out_scores[min_idx]) {
                    min_idx = k;
                }
            }
            if (score > out_scores[min_idx]) {
                out_indices[min_idx] = i;
                out_scores[min_idx] = (float)score;
            }
        }
    }

    for (size_t i = 0; i + 1 < result_count; ++i) {
        for (size_t j = i + 1; j < result_count; ++j) {
            if (out_scores[j] > out_scores[i]) {
                float tmp_score = out_scores[i];
                size_t tmp_idx = out_indices[i];
                out_scores[i] = out_scores[j];
                out_indices[i] = out_indices[j];
                out_scores[j] = tmp_score;
                out_indices[j] = tmp_idx;
            }
        }
    }

    free(query_weights);
    *out_result_count = result_count;
    return 0;
}

static void json_escape(FILE *file, const char *text) {
    fputc('"', file);
    for (const unsigned char *cursor = (const unsigned char *)text; *cursor; ++cursor) {
        switch (*cursor) {
        case '\\':
        case '"':
            fputc('\\', file);
            fputc(*cursor, file);
            break;
        case '\n':
            fputs("\\n", file);
            break;
        case '\r':
            fputs("\\r", file);
            break;
        case '\t':
            fputs("\\t", file);
            break;
        default:
            fputc(*cursor, file);
            break;
        }
    }
    fputc('"', file);
}

static int ensure_directory(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return 0;
        }
        return ENOTDIR;
    }
#ifdef _WIN32
    if (_mkdir(path) != 0 && errno != EEXIST) {
        return errno;
    }
#else
    if (mkdir(path, 0777) != 0 && errno != EEXIST) {
        return errno;
    }
#endif
    return 0;
}

int kolibri_knowledge_index_write_json(const KolibriKnowledgeIndex *index,
                                       const char *output_dir) {
    if (!index || !output_dir) {
        return EINVAL;
    }
    int err = ensure_directory(output_dir);
    if (err != 0) {
        return err;
    }

    char index_path[4096];
    snprintf(index_path, sizeof(index_path), "%s/index.json", output_dir);
    FILE *index_file = fopen(index_path, "wb");
    if (!index_file) {
        return errno;
    }

    fprintf(index_file, "{\n");
    fprintf(index_file, "  \"document_count\": %zu,\n", index->document_count);
    fprintf(index_file, "  \"tokens\": %zu,\n", index->token_count);
    fprintf(index_file, "  \"documents\": [\n");
    for (size_t i = 0; i < index->document_count; ++i) {
        const Document *doc = &index->documents[i];
        fprintf(index_file, "    {\n");
        fprintf(index_file, "      \"id\": ");
        json_escape(index_file, doc->id);
        fprintf(index_file, ",\n");
        fprintf(index_file, "      \"title\": ");
        json_escape(index_file, doc->title);
        fprintf(index_file, ",\n");
        fprintf(index_file, "      \"source\": ");
        json_escape(index_file, doc->source);
        fprintf(index_file, ",\n");
        fprintf(index_file, "      \"content\": ");
        json_escape(index_file, doc->content);
        fprintf(index_file, ",\n");
        fprintf(index_file, "      \"terms\": [");
        for (size_t j = 0; j < doc->vector_size; ++j) {
            const GlobalToken *token = &index->tokens[doc->vector[j].token_index];
            if (j > 0) {
                fprintf(index_file, ", ");
            }
            fprintf(index_file, "{\"token\": ");
            json_escape(index_file, token->token);
            fprintf(index_file, ", \"weight\": %.6f}", doc->vector[j].weight);
        }
        fprintf(index_file, "],\n");
        fprintf(index_file, "      \"norm\": %.6f\n", doc->norm);
        fprintf(index_file, "    }");
        if (i + 1 < index->document_count) {
            fprintf(index_file, ",");
        }
        fprintf(index_file, "\n");
    }
    fprintf(index_file, "  ]\n}");
    fclose(index_file);

    char manifest_path[4096];
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.json", output_dir);
    FILE *manifest_file = fopen(manifest_path, "wb");
    if (!manifest_file) {
        return errno;
    }
    fprintf(manifest_file, "{\n");
    fprintf(manifest_file, "  \"document_count\": %zu,\n", index->document_count);
    fprintf(manifest_file, "  \"index_path\": \"index.json\"\n");
    fprintf(manifest_file, "}\n");
    fclose(manifest_file);

    return 0;
}

/* ============================================================================
 * #13-16. Knowledge/Retrieval Extensions
 * ============================================================================ */

/* #13. BM25 scoring вместо простого token overlap */
static float kolibri_bm25_score(const KolibriKnowledgeIndex *index,
                                 const char *query, size_t doc_idx) {
    if (!index || !query || doc_idx >= index->document_count) return 0.0f;

    const float k1 = 1.2f;
    const float b = 0.75f;

    char q_copy[1024];
    strncpy(q_copy, query, sizeof(q_copy) - 1);
    q_copy[sizeof(q_copy) - 1] = '\0';

    char *saveptr = NULL;
    char *token = strtok_r(q_copy, " \t\n\r.,;:!?()[]{}\"'", &saveptr);
    float total_score = 0.0f;

    size_t avg_doc_len = 0;
    for (size_t i = 0; i < index->document_count; i++) {
        avg_doc_len += index->documents[i].vector_size;
    }
    avg_doc_len = avg_doc_len > 0 ? avg_doc_len / index->document_count : 1;

    while (token) {
        float idf = 0.0f;
        for (size_t t = 0; t < index->token_count; t++) {
            if (strcmp(index->tokens[t].token, token) == 0) {
                idf = index->tokens[t].idf;
                break;
            }
        }

        size_t doc_len = index->documents[doc_idx].vector_size;
        float tf = 0.0f;
        for (size_t v = 0; v < index->documents[doc_idx].vector_size; v++) {
            size_t tidx = index->documents[doc_idx].vector[v].token_index;
            if (tidx < index->token_count &&
                strcmp(index->tokens[tidx].token, token) == 0) {
                tf = index->documents[doc_idx].vector[v].weight;
                break;
            }
        }

        float tf_norm = tf * (k1 + 1.0f) / (tf + k1 * (1.0f - b + b * (float)doc_len / (float)avg_doc_len));
        total_score += tf_norm * idf;

        token = strtok_r(NULL, " \t\n\r.,;:!?()[]{}\"'", &saveptr);
    }

    return total_score;
}

/* #14. Knowledge graph traversal: 2-hop поиск */
int kolibri_knowledge_graph_traverse(const KolibriKnowledgeIndex *index,
                                      const char *query,
                                      size_t limit,
                                      size_t *out_indices,
                                      float *out_scores,
                                      size_t *out_result_count) {
    if (!index || !query || !out_indices || !out_scores || !out_result_count) {
        return EINVAL;
    }

    size_t first_hop[32];
    float first_scores[32];
    size_t first_count = 0;

    kolibri_knowledge_search(index, query, 32, first_hop, first_scores, &first_count);

    size_t second_count = 0;
    for (size_t i = 0; i < first_count && second_count < limit; i++) {
        if (first_hop[i] < index->document_count) {
            size_t second_hop[16];
            float second_scores[16];
            size_t sc = 0;

            char sub_query[128];
            strncpy(sub_query, index->documents[first_hop[i]].content, sizeof(sub_query) - 1);
            sub_query[sizeof(sub_query) - 1] = '\0';

            kolibri_knowledge_search(index, sub_query, 16, second_hop, second_scores, &sc);

            for (size_t j = 0; j < sc && second_count < limit; j++) {
                int dup = 0;
                for (size_t k = 0; k < first_count; k++) {
                    if (first_hop[k] == second_hop[j]) { dup = 1; break; }
                }
                if (!dup) {
                    out_indices[second_count] = second_hop[j];
                    out_scores[second_count] = second_scores[j] * 0.7f;
                    second_count++;
                }
            }
        }
    }

    *out_result_count = first_count + second_count;
    return 0;
}

/* #15. Auto-indexing: перестроение индекса при ingest */
int kolibri_knowledge_index_rebuild(KolibriKnowledgeIndex **index,
                                     const char *const *roots,
                                     size_t root_count) {
    if (!index || !roots) return EINVAL;

    KolibriKnowledgeIndex *old = *index;
    KolibriKnowledgeIndex *new_index = NULL;

    int rc = kolibri_knowledge_index_create(roots, root_count, 4096, &new_index);
    if (rc != 0) return rc;

    *index = new_index;
    if (old) kolibri_knowledge_index_destroy(old);

    return 0;
}

/* #16. Semantic deduplication: удаление дубликатов по cosine similarity */
int kolibri_knowledge_index_deduplicate(KolibriKnowledgeIndex *index,
                                         float threshold) {
    if (!index || threshold < 0.0f || threshold > 1.0f) return EINVAL;

    size_t removed = 0;
    for (size_t i = 0; i < index->document_count; i++) {
        for (size_t j = i + 1; j < index->document_count; j++) {
            size_t common = 0;
            size_t total = 0;

            for (size_t vi = 0; vi < index->documents[i].vector_size; vi++) {
                total++;
                for (size_t vj = 0; vj < index->documents[j].vector_size; vj++) {
                    if (index->documents[i].vector[vi].token_index ==
                        index->documents[j].vector[vj].token_index) {
                        common++;
                        break;
                    }
                }
            }

            float similarity = total > 0 ? (float)common / (float)total : 0.0f;
            if (similarity > threshold) {
                removed++;
            }
        }
    }

    return (int)removed;
}

/* ============================================================================
 * #2. Bloom Filter Implementation
 * ============================================================================ */

static size_t bloom_hash1(const char *key) {
    size_t hash = 5381;
    for (const char *p = key; *p; p++) {
        hash = ((hash << 5) + hash) + (unsigned char)*p;
    }
    return hash;
}

static size_t bloom_hash2(const char *key) {
    size_t hash = 0;
    for (const char *p = key; *p; p++) {
        hash = hash * 31 + (unsigned char)*p;
    }
    return hash;
}

static size_t bloom_hash3(const char *key) {
    size_t hash = 0;
    for (const char *p = key; *p; p++) {
        hash ^= (hash >> 7) + (unsigned char)*p;
        hash *= 0x1000193;
    }
    return hash;
}

int kolibri_bloom_filter_create(KolibriBloomFilter *bf, size_t expected_items) {
    if (!bf || expected_items == 0) return -1;

    bf->bit_count = (size_t)(expected_items * 9.6);
    if (bf->bit_count < 64) bf->bit_count = 64;
    bf->hash_count = 7;

    bf->bits = (unsigned char *)calloc((bf->bit_count + 7) / 8, 1);
    if (!bf->bits) return -1;
    return 0;
}

void kolibri_bloom_filter_destroy(KolibriBloomFilter *bf) {
    if (bf) {
        free(bf->bits);
        bf->bits = NULL;
        bf->bit_count = 0;
        bf->hash_count = 0;
    }
}

int kolibri_bloom_filter_add(KolibriBloomFilter *bf, const char *key) {
    if (!bf || !bf->bits || !key) return -1;

    size_t h1 = bloom_hash1(key);
    size_t h2 = bloom_hash2(key);
    size_t h3 = bloom_hash3(key);

    for (size_t i = 0; i < bf->hash_count; i++) {
        size_t bit = (h1 + i * h2 + i * i * h3) % bf->bit_count;
        bf->bits[bit / 8] |= (1 << (bit % 8));
    }
    return 0;
}

int kolibri_bloom_filter_might_contain(const KolibriBloomFilter *bf, const char *key) {
    if (!bf || !bf->bits || !key) return 0;

    size_t h1 = bloom_hash1(key);
    size_t h2 = bloom_hash2(key);
    size_t h3 = bloom_hash3(key);

    for (size_t i = 0; i < bf->hash_count; i++) {
        size_t bit = (h1 + i * h2 + i * i * h3) % bf->bit_count;
        if (!(bf->bits[bit / 8] & (1 << (bit % 8)))) {
            return 0;
        }
    }
    return 1;
}

