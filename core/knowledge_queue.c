#include "kolibri/knowledge_queue.h"

#include <dirent.h>
#include <errno.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#define kolibri_mkdir(path) _mkdir(path)
#else
#define kolibri_mkdir(path) mkdir(path, 0777)
#endif

struct KolibriQueue {
    sqlite3 *db;
};

static const char *QUEUE_SCHEMA =
    "CREATE TABLE IF NOT EXISTS submissions ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "created_at TEXT NOT NULL,"
    "title TEXT NOT NULL,"
    "content TEXT NOT NULL,"
    "source TEXT,"
    "metadata TEXT,"
    "status TEXT NOT NULL,"
    "moderator TEXT,"
    "moderation_note TEXT,"
    "moderated_at TEXT"
    ");"
    "CREATE INDEX IF NOT EXISTS submissions_status_idx ON submissions(status);";

static const char *STATUS_PENDING = "pending";
static const char *STATUS_APPROVED = "approved";
static const char *STATUS_REJECTED = "rejected";

const char *kolibri_queue_status_to_string(KolibriQueueStatus status) {
    switch (status) {
    case KOLIBRI_QUEUE_STATUS_PENDING:
        return STATUS_PENDING;
    case KOLIBRI_QUEUE_STATUS_APPROVED:
        return STATUS_APPROVED;
    case KOLIBRI_QUEUE_STATUS_REJECTED:
        return STATUS_REJECTED;
    }
    return STATUS_PENDING;
}

int kolibri_queue_status_from_string(const char *text, KolibriQueueStatus *out_status) {
    if (!text || !out_status) {
        return 1;
    }
    if (strcmp(text, STATUS_PENDING) == 0) {
        *out_status = KOLIBRI_QUEUE_STATUS_PENDING;
        return 0;
    }
    if (strcmp(text, STATUS_APPROVED) == 0) {
        *out_status = KOLIBRI_QUEUE_STATUS_APPROVED;
        return 0;
    }
    if (strcmp(text, STATUS_REJECTED) == 0) {
        *out_status = KOLIBRI_QUEUE_STATUS_REJECTED;
        return 0;
    }
    return 1;
}

static char *kolibri_strdup(const char *text) {
    if (!text) {
        return NULL;
    }
    size_t len = strlen(text);
    char *copy = (char *)malloc(len + 1U);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, text, len + 1U);
    return copy;
}

static int ensure_directory(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return 0;
        }
        return -1;
    }
    if (kolibri_mkdir(path) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

int kolibri_queue_open(const char *database_path, KolibriQueue **out_queue) {
    if (!database_path || !out_queue) {
        return SQLITE_MISUSE;
    }
    KolibriQueue *queue = (KolibriQueue *)calloc(1, sizeof(KolibriQueue));
    if (!queue) {
        return SQLITE_NOMEM;
    }
    int rc = sqlite3_open(database_path, &queue->db);
    if (rc != SQLITE_OK) {
        sqlite3_close(queue->db);
        free(queue);
        return rc;
    }
    rc = sqlite3_exec(queue->db, QUEUE_SCHEMA, NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        sqlite3_close(queue->db);
        free(queue);
        return rc;
    }
    *out_queue = queue;
    return SQLITE_OK;
}

void kolibri_queue_close(KolibriQueue *queue) {
    if (!queue) {
        return;
    }
    sqlite3_close(queue->db);
    free(queue);
}

static char *current_iso8601(void) {
    time_t now = time(NULL);
    struct tm t;
#ifdef _WIN32
    gmtime_s(&t, &now);
#else
    gmtime_r(&now, &t);
#endif
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &t);
    return kolibri_strdup(buffer);
}

int kolibri_queue_enqueue(KolibriQueue *queue,
                          const char *title,
                          const char *content,
                          const char *source,
                          const char *metadata_json,
                          long long *out_submission_id) {
    if (!queue || !title || !content) {
        return SQLITE_MISUSE;
    }
    char *created = current_iso8601();
    const char *sql =
        "INSERT INTO submissions (created_at, title, content, source, metadata, status) "
        "VALUES (?, ?, ?, ?, ?, ?)";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(queue->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        free(created);
        return rc;
    }
    sqlite3_bind_text(stmt, 1, created, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, title, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, content, -1, SQLITE_TRANSIENT);
    if (source) {
        sqlite3_bind_text(stmt, 4, source, -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 4);
    }
    if (metadata_json) {
        sqlite3_bind_text(stmt, 5, metadata_json, -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 5);
    }
    sqlite3_bind_text(stmt, 6, STATUS_PENDING, -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        free(created);
        return rc;
    }
    if (out_submission_id) {
        *out_submission_id = sqlite3_last_insert_rowid(queue->db);
    }
    free(created);
    return SQLITE_OK;
}

int kolibri_queue_fetch(KolibriQueue *queue,
                        KolibriQueueStatus status,
                        size_t limit,
                        KolibriQueueRecord **out_records,
                        size_t *out_count) {
    if (!queue || !out_records || !out_count) {
        return SQLITE_MISUSE;
    }
    *out_records = NULL;
    *out_count = 0U;
    const char *sql =
        "SELECT id, created_at, title, content, source, metadata, status, "
        "moderator, moderation_note, moderated_at "
        "FROM submissions WHERE status = ? ORDER BY created_at ASC LIMIT ?";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(queue->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return rc;
    }
    sqlite3_bind_text(stmt, 1, kolibri_queue_status_to_string(status), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, (int)limit);

    KolibriQueueRecord *records = NULL;
    size_t capacity = 0U;
    size_t count = 0U;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (count == capacity) {
            size_t new_cap = capacity == 0U ? 16U : capacity * 2U;
            KolibriQueueRecord *new_records = (KolibriQueueRecord *)realloc(records, new_cap * sizeof(KolibriQueueRecord));
            if (!new_records) {
                sqlite3_finalize(stmt);
                free(records);
                return SQLITE_NOMEM;
            }
            records = new_records;
            capacity = new_cap;
        }
        KolibriQueueRecord *rec = &records[count++];
        rec->submission_id = sqlite3_column_int64(stmt, 0);
        rec->created_at = kolibri_strdup((const char *)sqlite3_column_text(stmt, 1));
        rec->title = kolibri_strdup((const char *)sqlite3_column_text(stmt, 2));
        rec->content = kolibri_strdup((const char *)sqlite3_column_text(stmt, 3));
        rec->source = kolibri_strdup((const char *)sqlite3_column_text(stmt, 4));
        rec->metadata = kolibri_strdup((const char *)sqlite3_column_text(stmt, 5));
        KolibriQueueStatus st;
        if (kolibri_queue_status_from_string((const char *)sqlite3_column_text(stmt, 6), &st) != 0) {
            st = KOLIBRI_QUEUE_STATUS_PENDING;
        }
        rec->status = st;
        rec->moderator = kolibri_strdup((const char *)sqlite3_column_text(stmt, 7));
        rec->moderation_note = kolibri_strdup((const char *)sqlite3_column_text(stmt, 8));
        rec->moderated_at = kolibri_strdup((const char *)sqlite3_column_text(stmt, 9));
    }

    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        kolibri_queue_free_records(records, count);
        return rc;
    }
    *out_records = records;
    *out_count = count;
    return SQLITE_OK;
}

void kolibri_queue_free_records(KolibriQueueRecord *records, size_t count) {
    if (!records) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        free(records[i].created_at);
        free(records[i].title);
        free(records[i].content);
        free(records[i].source);
        free(records[i].metadata);
        free(records[i].moderator);
        free(records[i].moderation_note);
        free(records[i].moderated_at);
    }
    free(records);
}

int kolibri_queue_moderate(KolibriQueue *queue,
                           long long submission_id,
                           KolibriQueueStatus status,
                           const char *moderator,
                           const char *note) {
    if (!queue) {
        return SQLITE_MISUSE;
    }
    char *timestamp = current_iso8601();
    const char *sql =
        "UPDATE submissions SET status = ?, moderator = ?, moderation_note = ?, moderated_at = ? "
        "WHERE id = ?";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(queue->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        free(timestamp);
        return rc;
    }
    sqlite3_bind_text(stmt, 1, kolibri_queue_status_to_string(status), -1, SQLITE_TRANSIENT);
    if (moderator) {
        sqlite3_bind_text(stmt, 2, moderator, -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 2);
    }
    if (note) {
        sqlite3_bind_text(stmt, 3, note, -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 3);
    }
    sqlite3_bind_text(stmt, 4, timestamp, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, submission_id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    free(timestamp);
    if (rc != SQLITE_DONE) {
        return rc;
    }
    if (sqlite3_changes(queue->db) == 0) {
        return SQLITE_NOTFOUND;
    }
    return SQLITE_OK;
}

static void delete_markdown_files(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) {
        return;
    }
    struct dirent *entry;
    char path[4096];
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        size_t len = strlen(path);
        if (len > 3U && strcmp(path + len - 3U, ".md") == 0) {
            remove(path);
        }
    }
    closedir(d);
}

static void write_markdown_file(const KolibriQueueRecord *record, const char *dir, size_t index) {
    char slug[64];
    size_t pos = 0U;
    const char *src = record->title ? record->title : "submission";
    for (const unsigned char *c = (const unsigned char *)src; *c && pos < sizeof(slug) - 1U; ++c) {
        if (*c == ' ' || *c == '/' || *c == '\\') {
            slug[pos++] = '_';
        } else if ((*c >= '0' && *c <= '9') || (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z')) {
            slug[pos++] = (char)*c;
        }
    }
    slug[pos] = '\0';
    char filename[256];
    snprintf(filename, sizeof(filename), "%s/%06zu_%s.md", dir, index + 1U, slug[0] ? slug : "submission");
    FILE *file = fopen(filename, "wb");
    if (!file) {
        return;
    }
    fprintf(file, "# %s\n\n", record->title ? record->title : "Без названия");
    if (record->source && record->source[0]) {
        fprintf(file, "Источник: %s\n\n", record->source);
    }
    fprintf(file, "%s\n", record->content ? record->content : "");
    fclose(file);
}

int kolibri_queue_export_markdown(KolibriQueue *queue,
                                  KolibriQueueStatus status,
                                  const char *destination_dir,
                                  size_t *out_exported) {
    if (!queue || !destination_dir) {
        return SQLITE_MISUSE;
    }
    if (ensure_directory(destination_dir) != 0) {
        return SQLITE_ERROR;
    }
    delete_markdown_files(destination_dir);
    KolibriQueueRecord *records = NULL;
    size_t count = 0U;
    int rc = kolibri_queue_fetch(queue, status, 10000U, &records, &count);
    if (rc != SQLITE_OK) {
        return rc;
    }
    for (size_t i = 0; i < count; ++i) {
        write_markdown_file(&records[i], destination_dir, i);
    }
    kolibri_queue_free_records(records, count);
    if (out_exported) {
        *out_exported = count;
    }
    return SQLITE_OK;
}
