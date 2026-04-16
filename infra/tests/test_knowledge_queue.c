#include "kolibri/knowledge_queue.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>

static const char *DB_PATH = "./queue_test.db";

static void cleanup(void) {
    remove(DB_PATH);
    system("rm -rf ./queue_export");
}

void test_knowledge_queue(void) {
    KolibriQueue *queue = NULL;
    if (kolibri_queue_open(DB_PATH, &queue) != SQLITE_OK) {
        fprintf(stderr, "queue open failed\n");
        exit(1);
    }

    long long id = 0;
    if (kolibri_queue_enqueue(queue, "Тест", "Контент", "lab", "{\"tags\":[\"demo\"]}", &id) != SQLITE_OK) {
        fprintf(stderr, "enqueue failed\n");
        kolibri_queue_close(queue);
        exit(1);
    }

    KolibriQueueRecord *records = NULL;
    size_t count = 0U;
    if (kolibri_queue_fetch(queue, KOLIBRI_QUEUE_STATUS_PENDING, 10U, &records, &count) != SQLITE_OK || count != 1U) {
        fprintf(stderr, "fetch pending failed\n");
        kolibri_queue_close(queue);
        exit(1);
    }
    kolibri_queue_free_records(records, count);

    if (kolibri_queue_moderate(queue, id, KOLIBRI_QUEUE_STATUS_APPROVED, "tester", "ok") != SQLITE_OK) {
        fprintf(stderr, "moderate failed\n");
        kolibri_queue_close(queue);
        exit(1);
    }

    if (kolibri_queue_export_markdown(queue, KOLIBRI_QUEUE_STATUS_APPROVED, "./queue_export", &count) != SQLITE_OK || count != 1U) {
        fprintf(stderr, "export failed\n");
        kolibri_queue_close(queue);
        exit(1);
    }

    kolibri_queue_close(queue);
    cleanup();
}
