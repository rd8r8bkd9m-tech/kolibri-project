#include "kolibri/estimate_memory.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *DB_PATH = "./kem_test.db";

static void fail(const char *message, int rc) {
    fprintf(stderr, "FAIL: %s (rc=%d)\n", message, rc);
    remove(DB_PATH);
    exit(1);
}

static void expect(int condition, const char *message) {
    if (!condition) fail(message, -1);
}

int main(void) {
    remove(DB_PATH);
    KolibriEstimateMemory *memory = NULL;
    int rc = kem_open(DB_PATH, &memory);
    if (rc != SQLITE_OK) fail("open", rc);

    KemPriceObservationInput first = {
        .observation_id = "obs-001",
        .workspace_id = "workspace-local",
        .actor_id = "owner",
        .estimate_id = "estimate-001",
        .estimate_version = 1,
        .line_id = "line-work",
        .normalized_key = "mechanized-gypsum-plaster-work",
        .original_name = "Механизированная гипсовая штукатурка стен",
        .domain = "finishing.plaster",
        .region = "Татарстан/Лениногорск",
        .unit = "m2",
        .price_minor = 50000,
        .currency = "RUB",
        .vat_status = "not_applicable",
        .source_kind = "agent_draft",
        .source_ref = "message:1",
        .signal = KEM_SIGNAL_AGENT_DRAFT,
        .observed_at = "2026-07-25T10:00:00Z",
    };
    rc = kem_record_price(memory, &first);
    if (rc != SQLITE_OK) fail("record first price", rc);

    KemPriceObservationInput approved = first;
    approved.observation_id = "obs-002";
    approved.estimate_version = 2;
    approved.line_id = "line-work-v2";
    approved.price_minor = 65000;
    approved.source_kind = "user_edit";
    approved.signal = KEM_SIGNAL_USER_EDITED;
    approved.observed_at = "2026-07-25T11:00:00Z";
    rc = kem_record_price(memory, &approved);
    if (rc != SQLITE_OK) fail("record approved price", rc);

    rc = kem_record_approval(memory, "obs-002", KEM_SIGNAL_SENT_TO_CLIENT,
                             "2026-07-25T12:00:00Z", "{\"channel\":\"share\"}");
    if (rc != SQLITE_OK) fail("sent-to-client approval", rc);

    KemPriceSuggestion suggestion;
    rc = kem_find_best_price(memory, "workspace-local",
                             "mechanized-gypsum-plaster-work",
                             "finishing.plaster", "Татарстан/Лениногорск", "m2",
                             &suggestion);
    if (rc != SQLITE_OK) fail("find best price", rc);
    expect(suggestion.price_minor == 65000, "personal confirmed price must win");
    expect(strcmp(suggestion.trust_signal, "sent_to_client") == 0,
           "sent_to_client must promote trust");
    expect(suggestion.trust_rank == 45, "sent_to_client rank");
    kem_price_suggestion_clear(&suggestion);

    rc = kem_record_price_use(memory, "obs-002", "2026-07-25T12:05:00Z");
    if (rc != SQLITE_OK) fail("record use", rc);

    int64_t sequence = 0;
    rc = kem_enqueue_price_for_relay(memory, "obs-002", "evt-001",
                                     "relay-contributor-pseudonym", "2026-07-25T12:10:00Z",
                                     &sequence);
    if (rc != SQLITE_OK) fail("enqueue relay", rc);
    expect(sequence > 0, "local outbox sequence must be assigned");

    KemOutboxEvent *events = NULL;
    size_t event_count = 0;
    rc = kem_fetch_pending_outbox(memory, 10U, &events, &event_count);
    if (rc != SQLITE_OK) fail("fetch outbox", rc);
    expect(event_count == 1U, "one pending relay event");
    expect(strstr(events[0].payload_json, "65000") != NULL,
           "relay payload contains minor-unit price");
    expect(strstr(events[0].payload_json, "estimate-001") == NULL,
           "relay payload must omit estimate id");
    expect(strstr(events[0].payload_json, "Механизированная") == NULL,
           "relay payload must omit original free text");
    kem_free_outbox_events(events, event_count);

    rc = kem_mark_outbox_sent(memory, "evt-001", "server-cursor-1",
                              "2026-07-25T12:11:00Z");
    if (rc != SQLITE_OK) fail("mark sent", rc);
    rc = kem_set_sync_cursor(memory, "prices:Tatarstan:finishing.plaster",
                             "server-cursor-1", "2026-07-25T12:11:00Z");
    if (rc != SQLITE_OK) fail("set cursor", rc);
    char *cursor = NULL;
    rc = kem_get_sync_cursor(memory, "prices:Tatarstan:finishing.plaster", &cursor);
    if (rc != SQLITE_OK) fail("get cursor", rc);
    expect(strcmp(cursor, "server-cursor-1") == 0, "cursor round-trip");
    free(cursor);

    KemStats stats;
    rc = kem_stats(memory, &stats);
    if (rc != SQLITE_OK) fail("stats", rc);
    expect(stats.price_observations == 2, "two observations");
    expect(stats.approval_events == 3, "two initial + sent approval events");
    expect(stats.pending_outbox_events == 0, "no pending events");
    expect(stats.sent_outbox_events == 1, "one sent event");

    kem_close(memory);
    memory = NULL;
    rc = kem_open(DB_PATH, &memory);
    if (rc != SQLITE_OK) fail("reopen", rc);
    rc = kem_find_best_price(memory, "workspace-local",
                             "mechanized-gypsum-plaster-work",
                             "finishing.plaster", "Татарстан/Лениногорск", "m2",
                             &suggestion);
    if (rc != SQLITE_OK) fail("find after reopen", rc);
    expect(suggestion.price_minor == 65000, "offline persistence after restart");
    expect(suggestion.usage_count == 1, "usage count persists");
    kem_price_suggestion_clear(&suggestion);

    kem_close(memory);
    remove(DB_PATH);
    remove("./kem_test.db-wal");
    remove("./kem_test.db-shm");
    puts("PASS: kolibri estimate memory");
    return 0;
}
