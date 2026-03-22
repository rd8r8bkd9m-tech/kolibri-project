#include "kolibri/knowledge.h"

#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/*
 * Retrieval is heuristic and runs inside the C-core runtime.
 * Keep document slices bounded so indexing remains fast and predictable.
 */
#define KOLIBRI_KNOWLEDGE_MAX_INDEX_BYTES (16U * 1024U)
#define KOLIBRI_KNOWLEDGE_MAX_TOKEN_LEN 63U
#define KOLIBRI_KNOWLEDGE_MAX_QUERY_TOKENS 16U

typedef struct {
    char term[KOLIBRI_KNOWLEDGE_MAX_TOKEN_LEN + 1U];
    unsigned short freq;
} LocalTerm;

typedef struct {
    double score;
    size_t index;
} RankedDocument;

static int ensure_doc_capacity(KolibriKnowledgeIndex *index, size_t additional) {
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

static int ensure_term_hit_capacity(KolibriKnowledgeIndex *index, size_t additional) {
    if (!index) {
        return -1;
    }
    size_t required = index->term_hit_count + additional;
    if (required <= index->term_hit_capacity) {
        return 0;
    }
    size_t new_capacity = index->term_hit_capacity ? index->term_hit_capacity * 2U : 64U;
    while (new_capacity < required) {
        new_capacity *= 2U;
    }
    KolibriKnowledgeTermHit *hits =
        (KolibriKnowledgeTermHit *)realloc(index->term_hits, new_capacity * sizeof(KolibriKnowledgeTermHit));
    if (!hits) {
        return -1;
    }
    index->term_hits = hits;
    index->term_hit_capacity = new_capacity;
    return 0;
}

static int ensure_posting_capacity(KolibriKnowledgeIndex *index, size_t additional) {
    if (!index) {
        return -1;
    }
    size_t required = index->posting_count + additional;
    if (required <= index->posting_capacity) {
        return 0;
    }
    size_t new_capacity = index->posting_capacity ? index->posting_capacity * 2U : 64U;
    while (new_capacity < required) {
        new_capacity *= 2U;
    }
    KolibriKnowledgePosting *postings =
        (KolibriKnowledgePosting *)realloc(index->postings, new_capacity * sizeof(KolibriKnowledgePosting));
    if (!postings) {
        return -1;
    }
    index->postings = postings;
    index->posting_capacity = new_capacity;
    return 0;
}

int kolibri_knowledge_index_init(KolibriKnowledgeIndex *index) {
    if (!index) {
        return -1;
    }
    memset(index, 0, sizeof(*index));
    return 0;
}

static void free_document(KolibriKnowledgeDocument *doc) {
    if (!doc) {
        return;
    }
    free(doc->id);
    free(doc->title);
    free(doc->title_lower);
    free(doc->content);
    free(doc->content_lower);
    free(doc->source);
    free(doc->source_lower);
    memset(doc, 0, sizeof(*doc));
}

void kolibri_knowledge_index_free(KolibriKnowledgeIndex *index) {
    if (!index) {
        return;
    }
    if (index->documents && (uintptr_t)index->documents < 4096U) {
        memset(index, 0, sizeof(*index));
        return;
    }
    if (index->term_hits && (uintptr_t)index->term_hits < 4096U) {
        index->term_hits = NULL;
        index->term_hit_count = 0;
        index->term_hit_capacity = 0;
    }
    if (index->postings && (uintptr_t)index->postings < 4096U) {
        index->postings = NULL;
        index->posting_count = 0;
        index->posting_capacity = 0;
    }
    for (size_t i = 0; i < index->count; ++i) {
        free_document(&index->documents[i]);
    }
    for (size_t i = 0; i < index->term_hit_count; ++i) {
        free(index->term_hits[i].term);
    }
    for (size_t i = 0; i < index->posting_count; ++i) {
        free(index->postings[i].term);
    }
    free(index->documents);
    free(index->term_hits);
    free(index->postings);
    memset(index, 0, sizeof(*index));
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
        if (length > 0U && line_start[0] == '#') {
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
    if ((size_t)size > (512UL * 1024UL * 1024UL) || (size_t)size == (size_t)-1) {
        fclose(file);
        return NULL;
    }
    if (fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    size_t to_read = (size_t)size;
    if (to_read > KOLIBRI_KNOWLEDGE_MAX_INDEX_BYTES) {
        to_read = KOLIBRI_KNOWLEDGE_MAX_INDEX_BYTES;
    }
    char *buffer = (char *)malloc(to_read + 1U);
    if (!buffer) {
        fclose(file);
        return NULL;
    }
    size_t read = fread(buffer, 1U, to_read, file);
    fclose(file);
    buffer[read] = '\0';
    return buffer;
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
    while (cut > 0U && !isspace((unsigned char)content[cut])) {
        --cut;
    }
    if (cut == 0U) {
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

static int is_directory(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    return S_ISDIR(st.st_mode);
}

static int is_token_char(unsigned char c) {
    return isalnum(c) || c >= 0x80;
}

static int is_stop_word(const char *token) {
    static const char *const stop_words[] = {
        "a", "an", "and", "are", "as", "at", "be", "by", "for", "from",
        "how", "in", "is", "it", "of", "on", "or", "that", "the", "this",
        "to", "what", "when", "where", "who", "why",
        "в", "во", "и", "или", "как", "на", "о", "об", "по",
        "что", "это", "для", "из", "к", "ко", "не", "но", "а"
    };
    if (!token || token[0] == '\0') {
        return 1;
    }
    size_t len = strlen(token);
    if (len <= 1U) {
        return 1;
    }
    for (size_t i = 0; i < sizeof(stop_words) / sizeof(stop_words[0]); ++i) {
        if (strcmp(token, stop_words[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static size_t tokenize_text_unique(const char *text, LocalTerm *terms, size_t max_terms, unsigned short weight) {
    size_t count = 0;
    size_t length = text ? strlen(text) : 0U;
    size_t i = 0;
    while (i < length) {
        while (i < length && !is_token_char((unsigned char)text[i])) {
            ++i;
        }
        if (i >= length) {
            break;
        }
        size_t start = i;
        while (i < length && is_token_char((unsigned char)text[i])) {
            ++i;
        }
        size_t token_len = i - start;
        if (token_len == 0U) {
            continue;
        }
        if (token_len > KOLIBRI_KNOWLEDGE_MAX_TOKEN_LEN) {
            token_len = KOLIBRI_KNOWLEDGE_MAX_TOKEN_LEN;
        }
        char token[KOLIBRI_KNOWLEDGE_MAX_TOKEN_LEN + 1U];
        for (size_t j = 0; j < token_len; ++j) {
            token[j] = (char)tolower((unsigned char)text[start + j]);
        }
        token[token_len] = '\0';
        if (is_stop_word(token)) {
            continue;
        }
        size_t existing = 0;
        for (; existing < count; ++existing) {
            if (strcmp(terms[existing].term, token) == 0) {
                unsigned int boosted = (unsigned int)terms[existing].freq + (unsigned int)weight;
                terms[existing].freq = (unsigned short)(boosted > 65535U ? 65535U : boosted);
                break;
            }
        }
        if (existing < count) {
            continue;
        }
        if (count >= max_terms) {
            continue;
        }
        memcpy(terms[count].term, token, token_len + 1U);
        terms[count].freq = weight;
        ++count;
    }
    return count;
}

static size_t merge_local_terms(LocalTerm *dst, size_t dst_count, size_t dst_max,
                                const LocalTerm *src, size_t src_count) {
    for (size_t i = 0; i < src_count; ++i) {
        size_t existing = 0;
        for (; existing < dst_count; ++existing) {
            if (strcmp(dst[existing].term, src[i].term) == 0) {
                unsigned int boosted = (unsigned int)dst[existing].freq + (unsigned int)src[i].freq;
                dst[existing].freq = (unsigned short)(boosted > 65535U ? 65535U : boosted);
                break;
            }
        }
        if (existing < dst_count) {
            continue;
        }
        if (dst_count >= dst_max) {
            break;
        }
        dst[dst_count] = src[i];
        ++dst_count;
    }
    return dst_count;
}

static int append_term_hit(KolibriKnowledgeIndex *index, const char *term, size_t doc_id, unsigned short freq) {
    if (!index || !term || term[0] == '\0' || freq == 0U) {
        return -1;
    }
    if (ensure_term_hit_capacity(index, 1U) != 0) {
        return -1;
    }
    KolibriKnowledgeTermHit *hit = &index->term_hits[index->term_hit_count++];
    hit->term = duplicate_string(term);
    hit->doc_id = doc_id;
    hit->freq = freq;
    if (!hit->term) {
        --index->term_hit_count;
        return -1;
    }
    return 0;
}

static int source_is_noise(const char *source_lower, const char *title_lower, const char *content_lower) {
    const char *haystacks[] = { source_lower, title_lower, content_lower };
    for (size_t i = 0; i < sizeof(haystacks) / sizeof(haystacks[0]); ++i) {
        const char *s = haystacks[i];
        if (!s) {
            continue;
        }
        if (strstr(s, "special:random") != NULL) {
            return 1;
        }
        if (strstr(s, "wikipedia.org/wiki/special:random") != NULL) {
            return 1;
        }
    }
    return 0;
}

static double compute_quality_score(const char *source_lower, const char *title_lower, const char *content_lower) {
    double score = 1.0;
    if (!source_lower) {
        return score;
    }
    if (strstr(source_lower, "kolibri") != NULL) {
        score += 0.35;
    }
    if (strstr(source_lower, "bootstrap_") != NULL) {
        score += 0.20;
    }
    if (strstr(source_lower, "agent_") != NULL) {
        score -= 0.12;
    }
    if (title_lower && strstr(title_lower, "kolibri") != NULL) {
        score += 0.25;
    }
    if (content_lower && strstr(content_lower, "kolibri") != NULL) {
        score += 0.10;
    }
    if (score < 0.25) {
        score = 0.25;
    }
    return score;
}

static int compare_term_hits(const void *lhs, const void *rhs) {
    const KolibriKnowledgeTermHit *a = (const KolibriKnowledgeTermHit *)lhs;
    const KolibriKnowledgeTermHit *b = (const KolibriKnowledgeTermHit *)rhs;
    int term_cmp = strcmp(a->term, b->term);
    if (term_cmp != 0) {
        return term_cmp;
    }
    if (a->doc_id < b->doc_id) {
        return -1;
    }
    if (a->doc_id > b->doc_id) {
        return 1;
    }
    if (a->freq < b->freq) {
        return -1;
    }
    if (a->freq > b->freq) {
        return 1;
    }
    return 0;
}

static int compare_postings(const void *lhs, const void *rhs) {
    const KolibriKnowledgePosting *a = (const KolibriKnowledgePosting *)lhs;
    const KolibriKnowledgePosting *b = (const KolibriKnowledgePosting *)rhs;
    return strcmp(a->term, b->term);
}

static int build_postings(KolibriKnowledgeIndex *index) {
    if (!index) {
        return -1;
    }
    for (size_t i = 0; i < index->posting_count; ++i) {
        free(index->postings[i].term);
    }
    index->posting_count = 0;
    if (index->term_hit_count == 0U) {
        index->finalized = 1;
        return 0;
    }
    qsort(index->term_hits, index->term_hit_count, sizeof(KolibriKnowledgeTermHit), compare_term_hits);
    size_t i = 0;
    while (i < index->term_hit_count) {
        size_t start = i;
        const char *term = index->term_hits[i].term;
        while (i < index->term_hit_count && strcmp(index->term_hits[i].term, term) == 0) {
            ++i;
        }
        if (ensure_posting_capacity(index, 1U) != 0) {
            return -1;
        }
        KolibriKnowledgePosting *posting = &index->postings[index->posting_count++];
        posting->term = duplicate_string(term);
        posting->start = start;
        posting->count = i - start;
        if (!posting->term) {
            --index->posting_count;
            return -1;
        }
    }
    qsort(index->postings, index->posting_count, sizeof(KolibriKnowledgePosting), compare_postings);
    index->finalized = 1;
    return 0;
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

    char *title = extract_title(content);
    char *title_lower = to_lowercase_copy(title ? title : "");
    char *source = duplicate_string(relative);
    char *source_lower = to_lowercase_copy(source ? source : "");
    char *content_lower = to_lowercase_copy(content);
    if (!title || !title_lower || !source || !source_lower || !content_lower) {
        free(content);
        free(title);
        free(title_lower);
        free(source);
        free(source_lower);
        free(content_lower);
        return -1;
    }

    if (source_is_noise(source_lower, title_lower, content_lower)) {
        free(content);
        free(title);
        free(title_lower);
        free(source);
        free(source_lower);
        free(content_lower);
        return 0;
    }

    if (ensure_doc_capacity(index, 1U) != 0) {
        free(content);
        free(title);
        free(title_lower);
        free(source);
        free(source_lower);
        free(content_lower);
        return -1;
    }

    size_t doc_id = index->count;
    KolibriKnowledgeDocument *doc = &index->documents[index->count++];
    memset(doc, 0, sizeof(*doc));
    doc->id = make_id_from_path(path);
    doc->title = title;
    doc->title_lower = title_lower;
    doc->content = shorten_content(content);
    doc->content_lower = content_lower;
    doc->source = source;
    doc->source_lower = source_lower;
    doc->quality_score = compute_quality_score(source_lower, title_lower, content_lower);
    free(content);

    if (!doc->id || !doc->title || !doc->title_lower || !doc->content ||
        !doc->content_lower || !doc->source || !doc->source_lower) {
        free_document(doc);
        --index->count;
        return -1;
    }

    LocalTerm terms[512];
    LocalTerm title_terms[128];
    LocalTerm source_terms[128];
    LocalTerm content_terms[512];
    size_t term_count = 0;
    size_t title_count = tokenize_text_unique(doc->title_lower, title_terms, 128U, 4U);
    size_t source_count = tokenize_text_unique(doc->source_lower, source_terms, 128U, 2U);
    size_t content_count = tokenize_text_unique(doc->content_lower, content_terms, 512U, 1U);
    term_count = merge_local_terms(terms, term_count, 512U, title_terms, title_count);
    term_count = merge_local_terms(terms, term_count, 512U, source_terms, source_count);
    term_count = merge_local_terms(terms, term_count, 512U, content_terms, content_count);
    for (size_t i = 0; i < term_count; ++i) {
        if (append_term_hit(index, terms[i].term, doc_id, terms[i].freq) != 0) {
            return -1;
        }
    }

    index->finalized = 0;
    return 0;
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
    if (load_directory_recursive(index, root_path, root_path) != 0) {
        return -1;
    }
    return build_postings(index);
}

static size_t tokenize_query_terms(const char *query, char tokens[][64], size_t max_tokens) {
    LocalTerm local[KOLIBRI_KNOWLEDGE_MAX_QUERY_TOKENS];
    size_t count = tokenize_text_unique(query, local, KOLIBRI_KNOWLEDGE_MAX_QUERY_TOKENS, 1U);
    if (count > max_tokens) {
        count = max_tokens;
    }
    for (size_t i = 0; i < count; ++i) {
        snprintf(tokens[i], 64U, "%s", local[i].term);
    }
    return count;
}

static const KolibriKnowledgePosting *find_posting(const KolibriKnowledgeIndex *index, const char *term) {
    if (!index || !term || index->posting_count == 0U) {
        return NULL;
    }
    size_t left = 0;
    size_t right = index->posting_count;
    while (left < right) {
        size_t mid = left + (right - left) / 2U;
        int cmp = strcmp(term, index->postings[mid].term);
        if (cmp == 0) {
            return &index->postings[mid];
        }
        if (cmp < 0) {
            right = mid;
        } else {
            left = mid + 1U;
        }
    }
    return NULL;
}

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
    if (!index || index->count == 0U || !query || !results || !index->finalized) {
        return 0;
    }
    if (limit == 0U) {
        return 0;
    }

    char tokens[KOLIBRI_KNOWLEDGE_MAX_QUERY_TOKENS][64];
    size_t token_count = tokenize_query_terms(query, tokens, KOLIBRI_KNOWLEDGE_MAX_QUERY_TOKENS);
    if (token_count == 0U) {
        return 0;
    }

    char *query_lower = to_lowercase_copy(query);
    double *doc_scores = (double *)calloc(index->count, sizeof(double));
    unsigned short *doc_hits = (unsigned short *)calloc(index->count, sizeof(unsigned short));
    RankedDocument *ranked = (RankedDocument *)malloc(index->count * sizeof(RankedDocument));
    if (!query_lower || !doc_scores || !doc_hits || !ranked) {
        free(query_lower);
        free(doc_scores);
        free(doc_hits);
        free(ranked);
        return 0;
    }

    for (size_t t = 0; t < token_count; ++t) {
        const KolibriKnowledgePosting *posting = find_posting(index, tokens[t]);
        if (!posting) {
            continue;
        }
        double rarity = log1p(((double)index->count + 1.0) / ((double)posting->count + 1.0));
        if (rarity < 0.25) {
            rarity = 0.25;
        }
        for (size_t i = 0; i < posting->count; ++i) {
            const KolibriKnowledgeTermHit *hit = &index->term_hits[posting->start + i];
            doc_scores[hit->doc_id] += rarity * (double)hit->freq;
            doc_hits[hit->doc_id] += 1U;
        }
    }

    size_t ranked_count = 0;
    for (size_t i = 0; i < index->count; ++i) {
        if (doc_scores[i] <= 0.0) {
            continue;
        }
        const KolibriKnowledgeDocument *doc = &index->documents[i];
        double score = doc_scores[i] * doc->quality_score;
        if (doc->title_lower && strstr(doc->title_lower, query_lower) != NULL) {
            score += 4.0;
        } else if (doc->content_lower && strstr(doc->content_lower, query_lower) != NULL) {
            score += 2.0;
        }
        for (size_t t = 0; t < token_count; ++t) {
            if (doc->title_lower && strstr(doc->title_lower, tokens[t]) != NULL) {
                score += 1.5;
            }
            if (doc->source_lower && strstr(doc->source_lower, tokens[t]) != NULL) {
                score += 0.5;
            }
        }
        score += (double)doc_hits[i] * 0.35;
        ranked[ranked_count].score = score;
        ranked[ranked_count].index = i;
        ++ranked_count;
    }

    if (ranked_count == 0U) {
        free(query_lower);
        free(doc_scores);
        free(doc_hits);
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

    free(query_lower);
    free(doc_scores);
    free(doc_hits);
    free(ranked);
    return ranked_count;
}
