/*
 * Kolibri Knowledge Queue — moderation queue management in pure C.
 */

#ifndef KOLIBRI_KNOWLEDGE_QUEUE_H
#define KOLIBRI_KNOWLEDGE_QUEUE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    KOLIBRI_QUEUE_STATUS_PENDING,
    KOLIBRI_QUEUE_STATUS_APPROVED,
    KOLIBRI_QUEUE_STATUS_REJECTED,
} KolibriQueueStatus;

typedef struct KolibriQueue KolibriQueue;

typedef struct {
    long long submission_id;
    char *created_at;
    char *title;
    char *content;
    char *source;
    char *metadata;
    KolibriQueueStatus status;
    char *moderator;
    char *moderation_note;
    char *moderated_at;
} KolibriQueueRecord;

int kolibri_queue_open(const char *database_path, KolibriQueue **out_queue);

void kolibri_queue_close(KolibriQueue *queue);

int kolibri_queue_enqueue(KolibriQueue *queue,
                          const char *title,
                          const char *content,
                          const char *source,
                          const char *metadata_json,
                          long long *out_submission_id);

int kolibri_queue_fetch(KolibriQueue *queue,
                        KolibriQueueStatus status,
                        size_t limit,
                        KolibriQueueRecord **out_records,
                        size_t *out_count);

void kolibri_queue_free_records(KolibriQueueRecord *records, size_t count);

int kolibri_queue_moderate(KolibriQueue *queue,
                           long long submission_id,
                           KolibriQueueStatus status,
                           const char *moderator,
                           const char *note);

int kolibri_queue_export_markdown(KolibriQueue *queue,
                                  KolibriQueueStatus status,
                                  const char *destination_dir,
                                  size_t *out_exported);

const char *kolibri_queue_status_to_string(KolibriQueueStatus status);

int kolibri_queue_status_from_string(const char *text, KolibriQueueStatus *out_status);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_KNOWLEDGE_QUEUE_H */

